/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zenpower - SVI2 (Serial VID Interface 2) backend
 *
 * SVI2 telemetry provides voltage and current measurements via SMN registers.
 * Used by Zen 1, Zen+, Zen 2, Zen 3, and Zen 4.
 *
 * Voltage formula from LibreHardwareMonitor.
 * Current formulas discovered experimentally.
 */

#include "zenpower.h"

/*
 * Convert SVI2 plane value to voltage in millivolts
 * Formula: V = 1550 - 6.25 * VDD_COR
 */
u32 zenpower_svi2_plane_to_vcc(u32 plane)
{
	u32 vdd_cor;
	s32 v;

	vdd_cor = (plane >> 16) & 0xff;

	v = 1550 - (s32)((625 * vdd_cor) / 100);
	if (v < 0)
		v = 0;
	else if (v > 2000)
		v = 2000;

	return (u32)v;
}

/*
 * Get core current from SVI2 plane value
 * Returns current in milliamps
 *
 * Zen1: I = 1039.211 * IDD_COR
 * Zen2+: I = 658.823 * IDD_COR
 */
u32 zenpower_svi2_get_core_current(u32 plane, bool zen2)
{
	u32 idd_cor, fc;

	idd_cor = plane & 0xff;
	fc = zen2 ? 658823 : 1039211;

	return (fc * idd_cor) / 1000;
}

/*
 * Get SoC current from SVI2 plane value
 * Returns current in milliamps
 *
 * Zen1: I = 360.772 * IDD_COR
 * Zen2+: I = 294.3 * IDD_COR
 */
u32 zenpower_svi2_get_soc_current(u32 plane, bool zen2)
{
	u32 idd_cor, fc;

	idd_cor = plane & 0xff;
	fc = zen2 ? 294300 : 360772;

	return (fc * idd_cor) / 1000;
}
