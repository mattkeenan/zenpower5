# Changelog

All notable changes to zenpower5 will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [0.5.1] - 2026-10-05

### Added

- **Zen 4 Support:** AMD Family 19h Model 61h (Raphael desktop / Dragon Range - Ryzen 7000/8000 series)
  - Tctl/Tdie and CCD temperatures (CCD base 0x59b08, per k10temp 19h M60h-7Fh offset 0x308)
  - RAPL package power via MSR 0xc001029b (per-core energy MSR is hidden: reading it without CPU affinity pinning yields inconsistent values)
  - SVI2 voltage/current unavailable on Zen 4 - the SMU does not update the SVI telemetry planes on Raphael (same treatment as LibreHardwareMonitor for Ryzen 7000)

### Fixed

- **Tctl/Tdie 49 °C offset:** now also applied when TJ_SEL[17:16] = 11, matching k10temp; previously Tctl could read up to 49 °C too high on parts that signal the adjustment via TJ_SEL instead of RANGE_SEL
- **PCI device ID labelling:** 0x14e3 is `AMD_19H_M60H_DF_F3` (Zen 4 Raphael DF F3; Zen 5 Granite Ridge presents the same ID), previously mislabelled as 1AH_M40H
- **Per-model CCD base and RAPL plumbing:** CCD temperature base now comes from the model config table and the RAPL power path is selected by config flag instead of a hardcoded Zen 5 check

## [0.5.0] - 2025-11-30

### Added

- **Zen 5 Support:** Full support for AMD Zen 5 CPUs (Family 1Ah, Model 70h-7Fh)
  - Strix Halo (Ryzen AI Max+) support
  - RAPL (Running Average Power Limit) power monitoring backend for Zen 5
  - MSR-based energy counter reading (MSR 0xc001029b for package power)
  - Automatic energy-to-power conversion with proper rollover handling

- **Multi-File Backend Architecture:**
  - Split monolithic zenpower.c into logical backend files:
    - `zenpower_core.c` - Core driver framework and hwmon interface
    - `zenpower_svi2.c` - SVI2 telemetry backend (Zen 1-3)
    - `zenpower_rapl.c` - RAPL MSR backend (Zen 5)
    - `zenpower_temp.c` - Temperature monitoring backend
    - `zenpower.h` - Shared data structures and prototypes
  - Improved code organisation and maintainability
  - Easier addition of future monitoring backends

- **CPU Model Quirks System:**
  - Data-driven configuration table for CPU model detection
  - Replaced 140+ lines of nested switch statements with table lookup
  - Configuration flags system (ZEN_CFG_ZEN2_CALC, ZEN_CFG_RAPL, etc.)
  - Better CPU model identification in kernel logs
  - Single table entry required to add new CPU support

- **Kernel 6.16+ Compatibility:**
  - MSR API compatibility layer for rdmsrl_safe → rdmsrq_safe rename
  - Automatic kernel version detection and API selection
  - Supports Linux kernels 6.15 through 6.17+

### Fixed

- **CCD Temperature Formula (All Generations):**
  - Corrected temperature mask from 0xfff to 0x7ff (was including valid bit!)
  - Fixed temperature offset from -305000 to -49000 millidegrees
  - Added valid bit (BIT(11)) checking before exposing CCD sensors
  - These bugs affected temperature readings on ALL Zen generations since zenpower inception
  - Zen 5 CCD offset updated to 0x308 (was 0x154, per k10temp kernel driver)

- **Error Handling:**
  - Added proper amd_smn_read() return value checking
  - Set register value to 0 on SMN read errors (follows k10temp pattern)
  - Improved error reporting in kernel logs

- **Zen 5 Specific:**
  - Hide RAPL Core power sensor (PP0) - exists but meaningless on APU architecture
  - Proper RAPL initialisation with both package and core energy counters
  - 32-bit counter rollover handling in power calculations

### Changed

- **Project Rebrand:**
  - Renamed from zenpower3 to zenpower5
  - Version number updated to 0.5.0 (from 0.2.0)
  - Repository URL: https://github.com/mattkeenan/zenpower5
  - Module name remains "zenpower" for DKMS compatibility

- **Code Quality:**
  - Updated copyright headers
  - Improved code comments and documentation
  - Better alignment with Linux kernel coding standards
  - Reduced code duplication through backend separation

- **Documentation:**
  - Comprehensive README.md rewrite
  - Added architecture section explaining multi-file structure
  - Improved troubleshooting guide
  - Added supported CPU list with generation mapping
  - Created CHANGELOG.md for release tracking

### Development

This release was developed with assistance from Claude Code (claude-sonnet-4-5-20250929) for:
- Code analysis and refactoring
- Linux kernel best practices implementation
- CCD temperature formula debugging
- Multi-file architecture design
- CPU model quirks system implementation

### Notes

- Module remains named "zenpower" (not "zenpower5") for compatibility
- DKMS package name remains "zenpower" to avoid conflicts
- All zenpower3 users can upgrade directly to zenpower5
- No breaking changes in sensor naming or module parameters

## Upgrading from zenpower3

1. Unload zenpower3: `sudo modprobe -r zenpower`
2. Uninstall zenpower3: `sudo make dkms-uninstall` (in zenpower3 directory)
3. Clone zenpower5: `git clone https://github.com/mattkeenan/zenpower5.git`
4. Install zenpower5: `cd zenpower5 && sudo make dkms-install`
5. Load zenpower: `sudo modprobe zenpower`

Sensor names and module parameters remain unchanged for compatibility.

## Credits

Zenpower5 builds upon the work of:
- **ocerman** - Original zenpower driver
- **Ta180m** - zenpower3 with Zen 3 support
- **AliEmreSenel** - Continued zenpower3 maintenance
- **Matt Keenan** - Zen 5 support and architectural improvements
- **Claude Code** - Development assistance and code analysis

---

## Prior History (zenpower3)

For changes prior to the zenpower5 fork, see the original zenpower3 repository:
https://github.com/AliEmreSenel/zenpower3

Key zenpower3 milestones:
- 0.2.0 - Last zenpower3 release
- Added Family 17h Model 70h (Ryzen 3000) support
- Added Zen 3 (Family 19h) support
- DKMS installation support
- Multiple CPU socket support for Threadripper/EPYC
