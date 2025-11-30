/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zenpower - Driver for AMD Zen CPU hardware monitoring
 * Header file for multi-file backend architecture
 */

#ifndef ZENPOWER_H
#define ZENPOWER_H

#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/ktime.h>

/* CPU model configuration flags */
#define ZEN_CFG_ZEN2_CALC    BIT(0)  /* Use Zen2+ current formula */
#define ZEN_CFG_MULTINODE    BIT(1)  /* Multinode (TR/EPYC) configuration */
#define ZEN_CFG_RAPL         BIT(2)  /* Use RAPL for power monitoring */
#define ZEN_CFG_IS_ZEN5      BIT(3)  /* Zen 5 architecture */
#define ZEN_CFG_NO_RAPL_CORE BIT(4)  /* RAPL Core power unavailable/meaningless */

/* CPU model configuration entry */
struct zenpower_model_config {
	u8 family;              /* x86 family (0x17, 0x19, 0x1a) */
	u8 model;               /* x86 model (0x01, 0x31, 0x70, etc.) */
	u32 svi_core_addr;      /* SVI2 core telemetry address */
	u32 svi_soc_addr;       /* SVI2 SoC telemetry address */
	u32 ccd_temp_base;      /* Base address for CCD temperatures */
	u8 num_ccds;            /* Number of CCDs to check */
	u16 flags;              /* Configuration flags (ZEN_CFG_*) */
	const char *name;       /* Model name for debugging */
};

/* Shared data structure */
struct zenpower_data {
	struct pci_dev *pdev;
	void (*read_amdsmn_addr)(struct pci_dev *pdev, u16 node_id, u32 address, u32 *regval);
	u32 svi_core_addr;
	u32 svi_soc_addr;
	u16 node_id;
	u8 cpu_id;
	u8 nodes_per_cpu;
	int temp_offset;
	bool zen2;
	bool zen5;
	bool kernel_smn_support;
	bool amps_visible;
	bool ccd_visible[8];
	bool no_rapl_core;

	/* RAPL power tracking (zen5 only) - [0]=package, [1]=core */
	u64 rapl_prev_energy[2];
	ktime_t rapl_prev_time[2];
	bool rapl_available[2];
	u32 rapl_energy_unit;
	bool rapl_initialized;
};

/* SVI2 backend functions */
u32 zenpower_svi2_plane_to_vcc(u32 plane);
u32 zenpower_svi2_get_core_current(u32 plane, bool zen2);
u32 zenpower_svi2_get_soc_current(u32 plane, bool zen2);

/* RAPL backend functions */
int zenpower_rapl_init(struct zenpower_data *data, struct device *dev);
int zenpower_rapl_read_power(struct zenpower_data *data, int channel, long *val);

/* Temperature backend functions */
unsigned int zenpower_temp_get_ccd(struct zenpower_data *data, u32 ccd_addr);
unsigned int zenpower_temp_get_ctl(struct zenpower_data *data);

#endif /* ZENPOWER_H */
