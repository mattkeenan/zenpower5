/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zenpower - RAPL (Running Average Power Limit) backend
 *
 * RAPL provides power measurements via MSR energy counters.
 * Used by Zen 5.
 *
 * Power is calculated from energy delta between reads divided by time delta.
 */

#include "zenpower.h"
#include <linux/version.h>
#include <asm/msr.h>

/* Kernel 6.16+ renamed rdmsrl_safe to rdmsrq_safe */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0)
#define zenpower_rdmsrq_safe rdmsrq_safe
#else
#define zenpower_rdmsrq_safe rdmsrl_safe
#endif

/* AMD RAPL MSRs */
#define MSR_AMD_RAPL_POWER_UNIT             0xc0010299
#define MSR_AMD_PKG_ENERGY_STATUS           0xc001029b
#define MSR_AMD_PP0_ENERGY_STATUS           0xc001029a

/* RAPL energy unit masks for MSR 0xc0010299 */
#define RAPL_ENERGY_UNIT_MASK  0x1f00
#define RAPL_ENERGY_UNIT_SHIFT 8

int zenpower_rapl_init(struct zenpower_data *data, struct device *dev)
{
	u64 val;
	u32 energy_unit;
	int err;

	/* Read RAPL power unit MSR (0xc0010299) */
	err = zenpower_rdmsrq_safe(MSR_AMD_RAPL_POWER_UNIT, &val);
	if (err)
		return err;

	/* Extract energy unit: ESU = 1/(2^energy_unit) Joules */
	energy_unit = (val & RAPL_ENERGY_UNIT_MASK) >> RAPL_ENERGY_UNIT_SHIFT;

	/* Convert to microjoules per count: (1/(2^ESU)) * 1000000 */
	data->rapl_energy_unit = 1000000 >> energy_unit;

	/* Read initial package energy (channel 0) */
	err = zenpower_rdmsrq_safe(MSR_AMD_PKG_ENERGY_STATUS, &data->rapl_prev_energy[0]);
	if (err)
		return err;

	data->rapl_available[0] = true;

	/* Read initial core energy (channel 1) */
	err = zenpower_rdmsrq_safe(MSR_AMD_PP0_ENERGY_STATUS, &data->rapl_prev_energy[1]);
	if (err) {
		/* Core power MSR not available (expected on APUs) */
		data->rapl_available[1] = false;
		dev_dbg(dev, "RAPL Core power MSR not available\n");
	} else {
		data->rapl_available[1] = true;
	}

	/* Initialize timestamps for both channels */
	data->rapl_prev_time[0] = ktime_get();
	data->rapl_prev_time[1] = data->rapl_prev_time[0];
	data->rapl_initialized = true;

	return 0;
}

int zenpower_rapl_read_power(struct zenpower_data *data, int channel, long *val)
{
	u64 energy_now, energy_delta;
	ktime_t time_now, time_delta_ns;
	u32 time_delta_ms;
	int err;
	u32 msr;

	if (!data->rapl_initialized)
		return -EAGAIN;

	/* Select MSR based on channel: 0=package, 1=core */
	msr = (channel == 0) ? MSR_AMD_PKG_ENERGY_STATUS :
			       MSR_AMD_PP0_ENERGY_STATUS;

	err = zenpower_rdmsrq_safe(msr, &energy_now);
	if (err)
		return err;

	time_now = ktime_get();

	/* Handle 32-bit counter rollover */
	if (energy_now >= data->rapl_prev_energy[channel]) {
		energy_delta = energy_now - data->rapl_prev_energy[channel];
	} else {
		/* Counter wrapped (32-bit counter in 64-bit register) */
		energy_delta = (0x100000000ULL - data->rapl_prev_energy[channel]) +
			       energy_now;
	}

	time_delta_ns = ktime_sub(time_now, data->rapl_prev_time[channel]);
	time_delta_ms = ktime_to_ms(time_delta_ns);

	if (time_delta_ms == 0)
		return -EAGAIN;

	/* Power (microwatts) = energy (microjoules) / time (milliseconds) * 1000 */
	*val = (energy_delta * data->rapl_energy_unit * 1000) / time_delta_ms;

	/* Update state for next read */
	data->rapl_prev_energy[channel] = energy_now;
	data->rapl_prev_time[channel] = time_now;

	return 0;
}
