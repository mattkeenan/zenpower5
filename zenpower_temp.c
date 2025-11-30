/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zenpower - Temperature monitoring backend
 *
 * Temperature measurements via SMN registers.
 * Used by all Zen generations.
 *
 * Supports Tctl (control temp) and per-CCD temperatures.
 */

#include "zenpower.h"

#define F17H_M01H_REPORTED_TEMP_CTRL        0x00059800
#define F17H_TEMP_ADJUST_MASK               0x80000
#define ZEN_CCD_TEMP_VALID                  BIT(11)
#define ZEN_CCD_TEMP_MASK                   0x7ff  /* GENMASK(10, 0) */

unsigned int zenpower_temp_get_ctl(struct zenpower_data *data)
{
	unsigned int temp;
	u32 regval;

	data->read_amdsmn_addr(data->pdev, data->node_id,
							F17H_M01H_REPORTED_TEMP_CTRL, &regval);
	temp = (regval >> 21) * 125;
	if (regval & F17H_TEMP_ADJUST_MASK)
		temp -= 49000;
	return temp;
}

unsigned int zenpower_temp_get_ccd(struct zenpower_data *data, u32 ccd_addr)
{
	u32 regval;
	data->read_amdsmn_addr(data->pdev, data->node_id, ccd_addr, &regval);

	/* Check if CCD temperature is valid */
	if (!(regval & ZEN_CCD_TEMP_VALID))
		return 0;

	return (regval & ZEN_CCD_TEMP_MASK) * 125 - 49000;
}
