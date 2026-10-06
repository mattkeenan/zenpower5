/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zenpower - SMU PM table backend
 *
 * Fetches the SMU's power-monitoring (PM) table over the RSMU mailbox and
 * decodes live per-core/SoC/GFX telemetry. Used for Zen 5 Strix Point
 * (family 1Ah model 24h), where SVI2 telemetry planes are unused and the
 * SVI3 register layout is not supported.
 *
 * Protocol modelled on the ryzen_smu driver (gitlab.com/leogx9r/ryzen_smu,
 * amkillam fork); implementation is independent.
 */

#include "zenpower.h"
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/string.h>

/* RSMU mailbox SMN addresses (Strix Point / Strix Halo class) */
#define SMU_MB_CMD    0x3B10A20
#define SMU_MB_RSP    0x3B10A80
#define SMU_MB_ARGS   0x3B10A88
#define SMU_RSP_OK    0x1
#define SMU_RETRIES   8192

/* PM table (version 0x5D0009, verified on Ryzen AI 9 HX 370) */
#define SMU_PM_TABLE_SIZE   0xD54
#define SMU_PM_VERSION      0x5D0009
#define SMU_REFRESH_MS      250

/* Mailbox message IDs */
#define SMU_MSG_TABLE_VERSION    0x06
#define SMU_MSG_TRANSFER_TO_DRAM 0x65
#define SMU_MSG_DRAM_BASE        0x66

/* Field offsets in the PM table; values are IEEE754 float32.
 * Validated live: 0x014 (SoC power), 0x990/0x998/0x99C (GFX), 0xA0C+
 * (per-core voltage), 0xA3C+ (per-core temp). 0x3C reads ~2.1 V under
 * stress (not a real rail) and 0xC1C does not track package power, so
 * both are NOT exposed as Vcore/Icore. */
#define PM_SOC_POWER   0x014
#define PM_GFX_TEMP    0x990
#define PM_GFX_VOLT    0x998
#define PM_GFX_CURR    0x99C
#define PM_CORE_VOLT   0xA0C   /* +4*i, 12 entries */
#define PM_CORE_TEMP   0xA3C   /* +4*i, 12 entries */

/*
 * Scale an IEEE754 single-precision value by mul/div using integer math
 * (the kernel forbids FP math in modules). All sensor fields are
 * non-negative, so the sign bit is ignored. Precision retained at these
 * magnitudes is far beyond sensor resolution.
 * hwmon sysfs units: temperature m°C, voltage mV, current mA, power µW.
 */
static long smu_f32_scaled(u32 bits, long mul, long div)
{
	int e = ((bits >> 23) & 0xff) - 127 - 23;
	s64 mant = (1LL << 23) | (bits & 0x7fffff);
	s64 v;

	if (bits == 0)
		return 0;

	/* Clamp the exponent so the shifts below stay in range */
	if (e > 20)
		e = 20;
	else if (e < -63)
		e = -63;

	v = (s64)mant * mul;
	if (e >= 0)
		v <<= e;
	else
		v >>= -e;

	return (long)(v / div);
}

/*
 * Execute one SMU mailbox command. Caller must hold data->smu_lock.
 * Return values are placed back into args[] by the SMU.
 */
static int smu_send_command(struct zenpower_data *data, u32 op, u32 args[6])
{
	u32 rsp = 0;
	int i, retries;

	/* Wait until the mailbox is idle (RSP non-zero) */
	retries = SMU_RETRIES;
	do {
		data->read_amdsmn_addr(data->pdev, data->node_id, SMU_MB_RSP, &rsp);
	} while (rsp == 0 && --retries);
	if (rsp == 0)
		return -ETIMEDOUT;

	/* Mark the mailbox busy */
	data->write_amdsmn_addr(data->pdev, data->node_id, SMU_MB_RSP, 0);

	/* Write arguments */
	for (i = 0; i < 6; i++)
		data->write_amdsmn_addr(data->pdev, data->node_id, SMU_MB_ARGS + i * 4, args[i]);

	/* Issue the command */
	data->write_amdsmn_addr(data->pdev, data->node_id, SMU_MB_CMD, op);

	/* Wait for completion */
	retries = SMU_RETRIES;
	do {
		data->read_amdsmn_addr(data->pdev, data->node_id, SMU_MB_RSP, &rsp);
	} while (rsp == 0 && --retries);
	if (rsp == 0)
		return -ETIMEDOUT;
	if (rsp != SMU_RSP_OK)
		return -EIO;

	/* Fetch return arguments */
	for (i = 0; i < 6; i++)
		data->read_amdsmn_addr(data->pdev, data->node_id, SMU_MB_ARGS + i * 4, &args[i]);

	return 0;
}

int zenpower_smu_init(struct zenpower_data *data, struct device *dev)
{
	u32 args[6] = { 0, 0, 0, 0, 0, 0 };
	u64 base;
	int ret;

	mutex_lock(&data->smu_lock);

	/* PM table version */
	ret = smu_send_command(data, SMU_MSG_TABLE_VERSION, args);
	if (ret)
		goto out;

	if (args[0] != SMU_PM_VERSION) {
		dev_warn(dev, "Unsupported PM table version 0x%08x\n", args[0]);
		ret = -ENODEV;
		goto out;
	}

	/* PM table DRAM base */
	memset(args, 0, sizeof(args));
	args[0] = 1;
	args[1] = 1;
	ret = smu_send_command(data, SMU_MSG_DRAM_BASE, args);
	if (ret)
		goto out;

	base = args[0] | ((u64)args[1] << 32);
	if (base < 0x100000) {
		dev_warn(dev, "Unreasonable PM table DRAM base 0x%llx\n", base);
		ret = -ENODEV;
		goto out;
	}
	data->smu_dram_base = base;

	data->smu_table = devm_kzalloc(dev, SMU_PM_TABLE_SIZE, GFP_KERNEL);
	if (!data->smu_table) {
		ret = -ENOMEM;
		goto out;
	}

	data->smu_virt = ioremap_cache(data->smu_dram_base, SMU_PM_TABLE_SIZE);
	if (!data->smu_virt) {
		data->smu_table = NULL;
		ret = -ENOMEM;
		goto out;
	}

	data->smu_available = true;
	dev_info(dev, "SMU PM table 0x%08x: %d bytes at DRAM 0x%llx\n",
		 SMU_PM_VERSION, SMU_PM_TABLE_SIZE, data->smu_dram_base);

out:
	mutex_unlock(&data->smu_lock);
	return ret;
}

/*
 * Refresh the PM table snapshot if the cached one is older than
 * SMU_REFRESH_MS. Reads fail with -ENODATA until the first snapshot.
 */
int zenpower_smu_update(struct zenpower_data *data)
{
	u32 args[6] = { 3, 0, 0, 0, 0, 0 };
	int ret = 0;

	mutex_lock(&data->smu_lock);

	if (!data->smu_available) {
		ret = -ENODATA;
		goto out;
	}

	if (data->smu_jiffies &&
	    time_before(jiffies, data->smu_jiffies + msecs_to_jiffies(SMU_REFRESH_MS)))
		goto out; /* fresh snapshot */

	ret = smu_send_command(data, SMU_MSG_TRANSFER_TO_DRAM, args);
	if (ret) {
		dev_warn_ratelimited(&data->pdev->dev,
				     "SMU table transfer failed: %d\n", ret);
		ret = -EIO;
		goto out;
	}

	memcpy_fromio(data->smu_table, data->smu_virt, SMU_PM_TABLE_SIZE);
	data->smu_jiffies = jiffies;

out:
	mutex_unlock(&data->smu_lock);
	return ret;
}

static long smu_read(struct zenpower_data *data, u32 offset, long mul, long div)
{
	if (!data->smu_available)
		return -ENODATA;
	if (zenpower_smu_update(data))
		return -ENODATA;
	if (offset + 4 > SMU_PM_TABLE_SIZE)
		return -EOPNOTSUPP;

	return smu_f32_scaled(data->smu_table[offset / 4], mul, div);
}

long zenpower_smu_read_temp(struct zenpower_data *data, int channel)
{
	u32 offset;

	switch (channel) {
		case 0 ... 11:
			offset = PM_CORE_TEMP + channel * 4;
			break;
		case 12:
			offset = PM_GFX_TEMP;
			break;
		default:
			return -EOPNOTSUPP;
	}

	return smu_read(data, offset, 1000, 1);
}

long zenpower_smu_read_in(struct zenpower_data *data, int channel)
{
	u32 offset;

	switch (channel) {
		case 0 ... 11:
			offset = PM_CORE_VOLT + channel * 4;
			break;
		case 12:
			offset = PM_GFX_VOLT;
			break;
		default:
			return -EOPNOTSUPP;
	}

	/* hwmon voltage unit is millivolts; fields are V */
	return smu_read(data, offset, 1000, 1);
}

long zenpower_smu_read_curr(struct zenpower_data *data, int channel)
{
	/* hwmon current unit is milliamps; field is in A */
	if (channel != 0)
		return -EOPNOTSUPP;

	return smu_read(data, PM_GFX_CURR, 1000, 1);
}

long zenpower_smu_read_power(struct zenpower_data *data, int channel)
{
	if (channel != 0)
		return -EOPNOTSUPP;

	return smu_read(data, PM_SOC_POWER, 1000000, 1);
}
