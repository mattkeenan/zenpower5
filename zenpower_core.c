/*
 * Zenpower - Driver for reading temperature, voltage, current and power for AMD Zen family CPUs
 *
 * Copyright (c) 2018-2021 Anthony Wang
 * Copyright (c) 2025 Matt Keenan - Zen 5 support and multi-file refactoring
 *
 * This driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * forked from the original zenpower by Ondrej Čerman
 *
 * based on k10temp by Clemens Ladisch
 *
 * Docs:
 *  - https://www.kernel.org/doc/Documentation/hwmon/hwmon-kernel-api.txt
 *  - https://developer.amd.com/wp-content/resources/56255_3_03.PDF
 *
 * Sources:
 *  - Temp monitoring is from k10temp
 *  - SVI address and voltage formula is from LibreHardwareMonitor
 *  - Current formulas and CCD temp addresses were discovered experimentally
 */

#include <linux/version.h>

#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/msr.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0)
#include <asm/amd/nb.h>
#else
#include <asm/amd_nb.h>
#endif

#include "zenpower.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0) /* asm/amd_node.h */
static u16 amd_pci_dev_to_node_id(struct pci_dev *pdev)
{
	return PCI_SLOT(pdev->devfn) - AMD_NODE0_PCI_SLOT;
}
#endif

MODULE_DESCRIPTION("AMD ZEN family CPU Sensors Driver");
MODULE_AUTHOR("Anthony Wang");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.5.0");

static bool zen1_calc;
module_param(zen1_calc, bool, 0);
MODULE_PARM_DESC(zen1_calc, "Set to 1 to use ZEN1 calculation");


#ifndef PCI_DEVICE_ID_AMD_17H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_DF_F3         0x1463
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M10H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M10H_DF_F3    0x15eb
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M30H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M30H_DF_F3    0x1493
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M60H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M60H_DF_F3    0x144b
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M70H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M70H_DF_F3    0x1443
#endif

/* ZEN3 */
#ifndef PCI_DEVICE_ID_AMD_19H_DF_F3
#define PCI_DEVICE_ID_AMD_19H_DF_F3         0x1653
#endif

#ifndef PCI_DEVICE_ID_AMD_19H_M40H_DF_F3
#define PCI_DEVICE_ID_AMD_19H_M40H_DF_F3	0x167c
#endif

#ifndef PCI_DEVICE_ID_AMD_19H_M50H_DF_F3
#define PCI_DEVICE_ID_AMD_19H_M50H_DF_F3	0x166d
#endif

#ifndef PCI_DEVICE_ID_AMD_1AH_M70H_DF_F3
#define PCI_DEVICE_ID_AMD_1AH_M70H_DF_F3	0x12bb
#endif

/* F17H_M01H_SVI, should be renamed to something generic I think... */

#define F17H_M01H_REPORTED_TEMP_CTRL        0x00059800
#define F17H_M01H_SVI                       0x0005A000
#define F17H_M02H_SVI                       0x0006F000
#define F17H_M01H_SVI_TEL_PLANE0            (F17H_M01H_SVI + 0xC)
#define F17H_M01H_SVI_TEL_PLANE1            (F17H_M01H_SVI + 0x10)
#define F17H_M30H_SVI_TEL_PLANE0            (F17H_M01H_SVI + 0x14)
#define F17H_M30H_SVI_TEL_PLANE1            (F17H_M01H_SVI + 0x10)
#define F17H_M60H_SVI_TEL_PLANE0            (F17H_M02H_SVI + 0x38)
#define F17H_M60H_SVI_TEL_PLANE1            (F17H_M02H_SVI + 0x3C)
#define F17H_M70H_SVI_TEL_PLANE0            (F17H_M01H_SVI + 0x10)
#define F17H_M70H_SVI_TEL_PLANE1            (F17H_M01H_SVI + 0xC)
/* ZEN3 SP3/TR */
#define F19H_M01H_SVI_TEL_PLANE0            (F17H_M01H_SVI + 0x14)
#define F19H_M01H_SVI_TEL_PLANE1            (F17H_M01H_SVI + 0x10)
/* ZEN3 Ryzen desktop */
#define F19H_M21H_SVI_TEL_PLANE0            (F17H_M01H_SVI + 0x10)
#define F19H_M21H_SVI_TEL_PLANE1            (F17H_M01H_SVI + 0xC)
/* ZEN3 APU */
#define F19H_M50H_SVI_TEL_PLANE0            (F17H_M02H_SVI + 0x38)
#define F19H_M50H_SVI_TEL_PLANE1            (F17H_M02H_SVI + 0x3C)

#define F1AH_M70H_SVI                       0x0007300C
#define F1AH_M70H_SVI_TEL_PLANE0            0x00073010
#define F1AH_M70H_SVI_TEL_PLANE1            0x00073014

#define F17H_M70H_CCD_TEMP(x)               (0x00059954 + ((x) * 4))
/* Zen5 CCD temp - uses offset 0x308 per k10temp driver */
#define F1AH_M70H_CCD_TEMP(x)               (0x00059b08 + ((x) * 4))

/* CCD temperature base addresses for configuration table */
#define F17H_M70H_CCD_TEMP_BASE             0x00059954
#define F1AH_M70H_CCD_TEMP_BASE             0x00059b08

#ifndef HWMON_CHANNEL_INFO
#define HWMON_CHANNEL_INFO(stype, ...)	\
	(&(struct hwmon_channel_info) {		\
		.type = hwmon_##stype,			\
		.config = (u32 []) {			\
			__VA_ARGS__, 0				\
		}								\
	})
#endif

struct tctl_offset {
	u8 model;
	char const *id;
	int offset;
};

static const struct tctl_offset tctl_offset_table[] = {
	{ 0x17, "AMD Ryzen 5 1600X", 20000 },
	{ 0x17, "AMD Ryzen 7 1700X", 20000 },
	{ 0x17, "AMD Ryzen 7 1800X", 20000 },
	{ 0x17, "AMD Ryzen 7 2700X", 10000 },
	{ 0x17, "AMD Ryzen Threadripper 19", 27000 }, /* 19{00,20,50}X */
	{ 0x17, "AMD Ryzen Threadripper 29", 27000 }, /* 29{20,50,70,90}[W]X */
};

/*
 * CPU model configuration table
 *
 * Each entry defines register addresses and capabilities for a specific
 * CPU family/model combination. Adding support for a new CPU requires
 * adding one entry to this table.
 *
 * Entries are ordered by family, then by model for readability.
 */
static const struct zenpower_model_config model_configs[] = {
	/* Family 17h - Zen, Zen+, Zen2 */
	{ .family = 0x17, .model = 0x01,
	  .svi_core_addr = F17H_M01H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F17H_M01H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 4,
	  .flags = 0,
	  .name = "Zen/Zen+ (17h/01h)" },

	{ .family = 0x17, .model = 0x08,
	  .svi_core_addr = F17H_M01H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F17H_M01H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 4,
	  .flags = 0,
	  .name = "Zen+ (17h/08h)" },

	{ .family = 0x17, .model = 0x11,
	  .svi_core_addr = F17H_M01H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F17H_M01H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 0,
	  .flags = 0,
	  .name = "Zen APU (17h/11h)" },

	{ .family = 0x17, .model = 0x18,
	  .svi_core_addr = F17H_M01H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F17H_M01H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 0,
	  .flags = 0,
	  .name = "Zen+ APU (17h/18h)" },

	{ .family = 0x17, .model = 0x31,
	  .svi_core_addr = F17H_M30H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F17H_M30H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 8,
	  .flags = ZEN_CFG_ZEN2_CALC | ZEN_CFG_MULTINODE,
	  .name = "Zen2 TR/EPYC (17h/31h)" },

	{ .family = 0x17, .model = 0x60,
	  .svi_core_addr = F17H_M60H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F17H_M60H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 8,
	  .flags = ZEN_CFG_ZEN2_CALC,
	  .name = "Zen2 APU (17h/60h)" },

	{ .family = 0x17, .model = 0x71,
	  .svi_core_addr = F17H_M70H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F17H_M70H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 8,
	  .flags = ZEN_CFG_ZEN2_CALC,
	  .name = "Zen2 Ryzen (17h/71h)" },

	/* Family 19h - Zen3 */
	{ .family = 0x19, .model = 0x00,
	  .svi_core_addr = F19H_M01H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F19H_M01H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 8,
	  .flags = ZEN_CFG_ZEN2_CALC,
	  .name = "Zen3 SP3/TR (19h/00h)" },

	{ .family = 0x19, .model = 0x01,
	  .svi_core_addr = F19H_M01H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F19H_M01H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 8,
	  .flags = ZEN_CFG_ZEN2_CALC,
	  .name = "Zen3 SP3/TR (19h/01h)" },

	{ .family = 0x19, .model = 0x21,
	  .svi_core_addr = F19H_M21H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F19H_M21H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 2,
	  .flags = ZEN_CFG_ZEN2_CALC,
	  .name = "Zen3 Ryzen (19h/21h)" },

	{ .family = 0x19, .model = 0x50,
	  .svi_core_addr = F19H_M50H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F19H_M50H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F17H_M70H_CCD_TEMP_BASE,
	  .num_ccds = 2,
	  .flags = ZEN_CFG_ZEN2_CALC,
	  .name = "Zen3 APU (19h/50h)" },

	/* Family 1Ah - Zen5 */
	{ .family = 0x1a, .model = 0x70,
	  .svi_core_addr = F1AH_M70H_SVI_TEL_PLANE0,
	  .svi_soc_addr = F1AH_M70H_SVI_TEL_PLANE1,
	  .ccd_temp_base = F1AH_M70H_CCD_TEMP_BASE,
	  .num_ccds = 8,
	  .flags = ZEN_CFG_ZEN2_CALC | ZEN_CFG_RAPL | ZEN_CFG_IS_ZEN5 | ZEN_CFG_NO_RAPL_CORE,
	  .name = "Zen5 Strix Halo (1Ah/70h)" },

	{ } /* sentinel - must be last */
};

static DEFINE_MUTEX(nb_smu_ind_mutex);
static bool multicpu = false;

static umode_t zenpower_is_visible(const void *rdata,
									enum hwmon_sensor_types type,
									u32 attr, int channel)
{
	const struct zenpower_data *data = rdata;

	switch (type) {
		case hwmon_temp:
			if (channel >= 2 && data->ccd_visible[channel-2] == false) // Tccd1-8
				return 0;
			break;

		case hwmon_curr:
			/* Zen5 uses SVI3 (not SVI2), which is not supported yet */
			if (data->zen5)
				return 0;
			if (data->amps_visible == false)
				return 0;
			if (channel == 0 && data->svi_core_addr == 0)
				return 0;
			if (channel == 1 && data->svi_soc_addr == 0)
				return 0;
			break;

		case hwmon_power:
			if (data->amps_visible == false)
				return 0;
			if (channel == 0 && data->svi_core_addr == 0)
				return 0;
			if (channel == 1 && data->svi_soc_addr == 0)
				return 0;
			/* Hide Core power if unavailable/meaningless (e.g., Strix Halo APU) */
			if (data->no_rapl_core && channel == 1)
				return 0;
			break;

		case hwmon_in:
			if (channel == 0)	// fake item to align different indexing,
				return 0;		// see note at zenpower_info
			/* Zen5 uses SVI3 (not SVI2), which is not supported yet */
			if (data->zen5)
				return 0;
			if (channel == 1 && data->svi_core_addr == 0)
				return 0;
			if (channel == 2 && data->svi_soc_addr == 0)
				return 0;
			break;

		default:
			break;
	}

	return 0444;
}

static int debug_addrs_arr[] = {
	F17H_M01H_SVI + 0x8, F17H_M01H_SVI + 0xC, F17H_M01H_SVI + 0x10,
	F17H_M01H_SVI + 0x14, 0x000598BC, 0x0005994C, F17H_M70H_CCD_TEMP(0),
	F17H_M70H_CCD_TEMP(1), F17H_M70H_CCD_TEMP(2), F17H_M70H_CCD_TEMP(3),
	F17H_M70H_CCD_TEMP(4), F17H_M70H_CCD_TEMP(5), F17H_M70H_CCD_TEMP(6),
	F17H_M70H_CCD_TEMP(7), F17H_M02H_SVI + 0x38, F17H_M02H_SVI + 0x3C,
	F1AH_M70H_SVI, F1AH_M70H_SVI_TEL_PLANE0, F1AH_M70H_SVI_TEL_PLANE1,
	F1AH_M70H_SVI + 0xC
};

static ssize_t debug_data_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 smndata;

	len += sprintf(buf + len, "KERN_SUP: %d\n", data->kernel_smn_support);
	len += sprintf(buf + len, "NODE%d; CPU%d; ", data->node_id, data->cpu_id);
	len += sprintf(buf + len, "N/CPU: %d\n", data->nodes_per_cpu);

	for (i = 0; i < ARRAY_SIZE(debug_addrs_arr); i++){
		data->read_amdsmn_addr(data->pdev, data->node_id, debug_addrs_arr[i], &smndata);
		len += sprintf(buf + len, "%08x = %08x\n", debug_addrs_arr[i], smndata);
	}

	return len;
}

static int zenpower_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	switch (type) {

		// Temperatures
		case hwmon_temp:
			switch (attr) {
				case hwmon_temp_input:
					switch (channel) {
						case 0: // Tdie
							*val = zenpower_temp_get_ctl(data) - data->temp_offset;
							break;
						case 1: // Tctl
							*val = zenpower_temp_get_ctl(data);
							break;
						case 2 ... 9: // Tccd1-8
							if (data->zen5) {
								*val = zenpower_temp_get_ccd(data, F1AH_M70H_CCD_TEMP(channel-2));
							} else {
								*val = zenpower_temp_get_ccd(data, F17H_M70H_CCD_TEMP(channel-2));
							}
							break;
						default:
							return -EOPNOTSUPP;
					}
					break;

				case hwmon_temp_max: // Tdie max
					// source: https://www.amd.com/en/products/cpu/amd-ryzen-7-3700x
					//         other cpus have also same* Tmax on AMD website
					//         * = when taking into consideration a tctl offset
					*val = 95 * 1000;
					break;

				default:
					return -EOPNOTSUPP;
			}
			break;

		// Voltage
		case hwmon_in:
			if (channel == 0)
				return -EOPNOTSUPP;
			channel -= 1;	// hwmon_in have different indexing, see note at zenpower_info
							// fall through
		// Power / Current
		case hwmon_curr:
		case hwmon_power:
			if (attr != hwmon_in_input && attr != hwmon_curr_input &&
				attr != hwmon_power_input) {
				return -EOPNOTSUPP;
			}

			/* Zen5 uses RAPL for power monitoring (SVI3 not supported yet) */
			if (type == hwmon_power && data->zen5) {
				return zenpower_rapl_read_power(data, channel, val);
			}

			switch (channel) {
				case 0: // Core SVI2
					data->read_amdsmn_addr(data->pdev, data->node_id,
											data->svi_core_addr, &plane);
					break;
				case 1: // SoC SVI2
					data->read_amdsmn_addr(data->pdev, data->node_id,
											data->svi_soc_addr, &plane);
					break;
				default:
					return -EOPNOTSUPP;
			}

			switch (type) {
				case hwmon_in:
					*val = zenpower_svi2_plane_to_vcc(plane);
					break;
				case hwmon_curr:
					*val = (channel == 0) ?
						zenpower_svi2_get_core_current(plane, data->zen2):
						zenpower_svi2_get_soc_current(plane, data->zen2);
					break;
				case hwmon_power:
					*val = (channel == 0) ?
						zenpower_svi2_get_core_current(plane, data->zen2) * zenpower_svi2_plane_to_vcc(plane):
						zenpower_svi2_get_soc_current(plane, data->zen2) * zenpower_svi2_plane_to_vcc(plane);
					break;
				default:
					break;
			}
			break;

		default:
			return -EOPNOTSUPP;
	}

	return 0;
}

static const char *zenpower_temp_label[][10] = {
	{
		"Tdie",
		"Tctl",
		"Tccd1",
		"Tccd2",
		"Tccd3",
		"Tccd4",
		"Tccd5",
		"Tccd6",
		"Tccd7",
		"Tccd8",
	},
	{
		"cpu0 Tdie",
		"cpu0 Tctl",
		"cpu0 Tccd1",
		"cpu0 Tccd2",
		"cpu0 Tccd3",
		"cpu0 Tccd4",
		"cpu0 Tccd5",
		"cpu0 Tccd6",
		"cpu0 Tccd7",
		"cpu0 Tccd8",
	},
	{
		"cpu1 Tdie",
		"cpu1 Tctl",
		"cpu1 Tccd1",
		"cpu1 Tccd2",
		"cpu1 Tccd3",
		"cpu1 Tccd4",
		"cpu1 Tccd5",
		"cpu1 Tccd6",
		"cpu1 Tccd7",
		"cpu1 Tccd8",
	}
};

static const char *zenpower_in_label[][3] = {
	{
		"",
		"SVI2_Core",
		"SVI2_SoC",
	},
	{
		"",
		"cpu0 SVI2_Core",
		"cpu0 SVI2_SoC",
	},
	{
		"",
		"cpu1 SVI2_Core",
		"cpu1 SVI2_SoC",
	}
};

static const char *zenpower_curr_label[][2] = {
	{
		"SVI2_C_Core",
		"SVI2_C_SoC",
	},
	{
		"cpu0 SVI2_C_Core",
		"cpu0 SVI2_C_SoC",
	},
	{
		"cpu1 SVI2_C_Core",
		"cpu1 SVI2_C_SoC",
	}
};

static const char *zenpower_power_label[][2] = {
	{
		"SVI2_P_Core",
		"SVI2_P_SoC",
	},
	{
		"cpu0 SVI2_P_Core",
		"cpu0 SVI2_P_SoC",
	},
	{
		"cpu1 SVI2_P_Core",
		"cpu1 SVI2_P_SoC",
	}
};

static const char *zenpower_power_label_zen5[][2] = {
	{
		"RAPL_P_Package",
		"RAPL_P_Core",
	},
	{
		"cpu0 RAPL_P_Package",
		"cpu0 RAPL_P_Core",
	},
	{
		"cpu1 RAPL_P_Package",
		"cpu1 RAPL_P_Core",
	}
};

static int zenpower_read_labels(struct device *dev,
				enum hwmon_sensor_types type, u32 attr,
				int channel, const char **str)
{
	struct zenpower_data *data;
	u8 i = 0;

	if (multicpu) {
		data = dev_get_drvdata(dev);
		if (data->cpu_id <= 1)
			i = data->cpu_id + 1;
	}

	switch (type) {
		case hwmon_temp:
			*str = zenpower_temp_label[i][channel];
			break;
		case hwmon_in:
			*str = zenpower_in_label[i][channel];
			break;
		case hwmon_curr:
			*str = zenpower_curr_label[i][channel];
			break;
		case hwmon_power:
			data = dev_get_drvdata(dev);
			if (data->zen5) {
				*str = zenpower_power_label_zen5[i][channel];
			} else {
				*str = zenpower_power_label[i][channel];
			}
			break;
		default:
			return -EOPNOTSUPP;
	}

	return 0;
}

static void kernel_smn_read(struct pci_dev *pdev, u16 node_id, u32 address, u32 *regval)
{
	if (amd_smn_read(node_id, address, regval))
		*regval = 0;
}

// fallback method from k10temp
// may return inaccurate results on multi-die chips
static void nb_index_read(struct pci_dev *pdev, u16 node_id, u32 address, u32 *regval)
{
	mutex_lock(&nb_smu_ind_mutex);
	pci_bus_write_config_dword(pdev->bus, PCI_DEVFN(0, 0), 0x60, address);
	pci_bus_read_config_dword(pdev->bus, PCI_DEVFN(0, 0), 0x64, regval);
	mutex_unlock(&nb_smu_ind_mutex);
}

static const struct hwmon_channel_info *zenpower_info[] = {
	HWMON_CHANNEL_INFO(temp,
			HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_LABEL,	// Tdie
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tctl
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd1
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd2
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd3
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd4
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd5
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd6
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd7
			HWMON_T_INPUT | HWMON_T_LABEL),					// Tccd8

	HWMON_CHANNEL_INFO(in,
			HWMON_I_LABEL,	// everything is using 1 based indexing except
							// hwmon_in - that is using 0 based indexing
							// let's make fake item so corresponding SVI2 data is
							// associated with same index
			HWMON_I_INPUT | HWMON_I_LABEL,		// Core Voltage (SVI2)
			HWMON_I_INPUT | HWMON_I_LABEL),		// SoC Voltage (SVI2)

	HWMON_CHANNEL_INFO(curr,
			HWMON_C_INPUT | HWMON_C_LABEL,		// Core Current (SVI2)
			HWMON_C_INPUT | HWMON_C_LABEL),		// SoC Current (SVI2)

	HWMON_CHANNEL_INFO(power,
			HWMON_P_INPUT | HWMON_P_LABEL,		// Core Power (SVI2)
			HWMON_P_INPUT | HWMON_P_LABEL),		// SoC Power (SVI2)

	NULL
};

static const struct hwmon_ops zenpower_hwmon_ops = {
	.is_visible = zenpower_is_visible,
	.read = zenpower_read,
	.read_string = zenpower_read_labels,
};

static const struct hwmon_chip_info zenpower_chip_info = {
	.ops = &zenpower_hwmon_ops,
	.info = zenpower_info,
};

static DEVICE_ATTR_RO(debug_data);

static struct attribute *zenpower_attrs[] = {
	&dev_attr_debug_data.attr,
	NULL
};

static const struct attribute_group zenpower_group = {
	.attrs = zenpower_attrs
};
__ATTRIBUTE_GROUPS(zenpower);

/*
 * Look up CPU model configuration
 *
 * Returns pointer to configuration entry for the given CPU family/model,
 * or NULL if not found.
 */
static const struct zenpower_model_config *
lookup_model_config(u8 family, u8 model)
{
	const struct zenpower_model_config *cfg;

	for (cfg = model_configs; cfg->family; cfg++) {
		if (cfg->family == family && cfg->model == model)
			return cfg;
	}
	return NULL;
}

static int zenpower_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct zenpower_data *data;
	struct device *hwmon_dev;
	struct pci_dev *misc;
	int i, ccd_check = 0;
	bool multinode;
	u8 node_of_cpu;
	u32 val;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->zen2 = false;
	data->pdev = pdev;
	data->temp_offset = 0;
	data->read_amdsmn_addr = nb_index_read;
	data->kernel_smn_support = false;
	data->svi_core_addr = false;
	data->svi_soc_addr = false;
	data->amps_visible = false;
	data->no_rapl_core = false;
	data->node_id = 0;
	for (i = 0; i < 8; i++) {
		data->ccd_visible[i] = false;
	}

	for (i = 0; i < amd_nb_num(); i++) {
		misc = node_to_amd_nb(i)->misc;
		if (pdev->vendor == misc->vendor && pdev->device == misc->device) {
			data->kernel_smn_support = true;
			data->read_amdsmn_addr = kernel_smn_read;
			data->node_id = amd_pci_dev_to_node_id(pdev);
			break;
		}
	}

	// CPUID_Fn8000001E_ECX [Node Identifiers] (Core::X86::Cpuid::NodeId)
	// 10:8 NodesPerProcessor
	data->nodes_per_cpu = 1 + ((cpuid_ecx(0x8000001E) >> 8) & 0b111);
	multinode = (data->nodes_per_cpu > 1);

	node_of_cpu = data->node_id % data->nodes_per_cpu;
	data->cpu_id = data->node_id / data->nodes_per_cpu;

	if (data->cpu_id > 0)
		multicpu = true;

	/* Look up CPU configuration from table */
	{
		const struct zenpower_model_config *config;
		u8 family = boot_cpu_data.x86;
		u8 model = boot_cpu_data.x86_model;

		config = lookup_model_config(family, model);
		if (!config) {
			dev_err(dev, "Unsupported CPU family=%02xh model=%02xh\n",
				family, model);
			dev_info(dev, "Please report this CPU to zenpower developers\n");
			return -ENODEV;
		}

		dev_info(dev, "Detected %s\n", config->name);

		/* Apply base configuration from table */
		data->svi_core_addr = config->svi_core_addr;
		data->svi_soc_addr = config->svi_soc_addr;
		data->amps_visible = true;
		ccd_check = config->num_ccds;

		/* Apply Zen2 calculation formula (unless zen1_calc override) */
		if (config->flags & ZEN_CFG_ZEN2_CALC) {
			if (!zen1_calc) {
				data->zen2 = true;
			}
		}

		/* Set Zen5 architecture flag */
		if (config->flags & ZEN_CFG_IS_ZEN5) {
			data->zen5 = true;
		}

		/* Set RAPL Core power availability flag */
		if (config->flags & ZEN_CFG_NO_RAPL_CORE) {
			data->no_rapl_core = true;
		}

		/* Handle Zen5 RAPL initialization */
		if (config->flags & ZEN_CFG_RAPL) {
			if (zenpower_rapl_init(data, dev)) {
				dev_warn(dev, "RAPL initialization failed, power monitoring unavailable\n");
				data->amps_visible = false;
			}
		}

		/* Handle multinode configuration (Threadripper/EPYC) */
		if (config->flags & ZEN_CFG_MULTINODE) {
			if (multinode && node_of_cpu == 0) {
				/* Node 0: SoC telemetry only */
				data->svi_soc_addr = config->svi_core_addr;
				data->svi_core_addr = 0;
			} else if (multinode && node_of_cpu == 1) {
				/* Node 1: Core telemetry only */
				data->svi_core_addr = config->svi_core_addr;
				data->svi_soc_addr = 0;
			}
		}

		/* Log configured measurement backends */
		dev_info(dev, "Measurement methods:\n");
		if (config->flags & ZEN_CFG_RAPL) {
			dev_info(dev, "  Power: RAPL MSRs (Package only)\n");
		} else {
			dev_info(dev, "  Power: SVI2 via SMN (Core + SoC)\n");
		}
		if (data->svi_core_addr) {
			dev_info(dev, "  Core voltage/current: SVI2 via SMN (addr 0x%08x, %s formula)\n",
				data->svi_core_addr,
				(data->zen2 && !zen1_calc) ? "ZEN2" : "ZEN1");
		}
		if (data->svi_soc_addr) {
			dev_info(dev, "  SoC voltage/current: SVI2 via SMN (addr 0x%08x, %s formula)\n",
				data->svi_soc_addr,
				(data->zen2 && !zen1_calc) ? "ZEN2" : "ZEN1");
		}
		dev_info(dev, "  Tctl temperature: SMN register (MSR 0x59800)\n");
		if (config->num_ccds > 0) {
			dev_info(dev, "  CCD temperatures: SMN registers (base 0x%08x, %d CCDs)\n",
				config->ccd_temp_base, config->num_ccds);
		}
	}

	for (i = 0; i < ccd_check; i++) {
		u32 ccd_addr = data->zen5 ? F1AH_M70H_CCD_TEMP(i) : F17H_M70H_CCD_TEMP(i);
		data->read_amdsmn_addr(pdev, data->node_id, ccd_addr, &val);
		/* Check valid bit (BIT(11)) per k10temp driver */
		if (val & BIT(11)) {
			data->ccd_visible[i] = true;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tctl_offset_table); i++) {
		const struct tctl_offset *entry = &tctl_offset_table[i];

		if (boot_cpu_data.x86 == entry->model &&
			strstr(boot_cpu_data.x86_model_id, entry->id)) {
			data->temp_offset = entry->offset;
			break;
		}
	}

	hwmon_dev = devm_hwmon_device_register_with_info(
		dev, "zenpower", data, &zenpower_chip_info, zenpower_groups
	);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct pci_device_id zenpower_id_table[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M10H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M30H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M60H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M70H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_M40H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_M50H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_1AH_M70H_DF_F3) },
	{}
};
MODULE_DEVICE_TABLE(pci, zenpower_id_table);

static struct pci_driver zenpower_driver = {
	.name = "zenpower",
	.id_table = zenpower_id_table,
	.probe = zenpower_probe,
};

module_pci_driver(zenpower_driver);
