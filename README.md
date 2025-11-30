# Zenpower5

Zenpower5 is a Linux kernel driver for reading temperature, voltage (SVI2), current (SVI2), and power (SVI2/RAPL) for AMD Zen family CPUs, with support for **Zen 1 through Zen 5** architectures.

This is a structural update to the [zenpower3](https://github.com/AliEmreSenel/zenpower3) project, featuring a modernised multi-file backend architecture and expanded CPU support.

## What's New in Zenpower5

- **Zen 5 Support:** Full support for Zen 5 CPUs (Strix Halo) using RAPL power monitoring
- **Multi-File Architecture:** Clean separation of SVI2, RAPL, and temperature backends for better maintainability
- **CPU Model Quirks System:** Data-driven configuration table replacing nested switch statements
- **Fixed CCD Temperature Formula:** Corrected bugs in CCD temperature calculations affecting all Zen generations
- **Kernel 6.15-6.17 Compatibility:** Updated MSR API compatibility for recent kernels
- **Improved Error Handling:** Better SMN register error handling following kernel k10temp patterns

## Supported CPUs

- **Zen 1** (Family 17h, Model 01h, 08h) - Ryzen 1000/2000 series
- **Zen+** (Family 17h, Model 08h, 18h) - Ryzen 2000 APU series
- **Zen 2** (Family 17h, Model 31h, 60h, 71h) - Ryzen 3000 series, Threadripper 3000, EPYC 7002
- **Zen 3** (Family 19h, Model 00h, 01h, 21h, 50h) - Ryzen 5000 series, Threadripper Pro, EPYC 7003
- **Zen 5** (Family 1Ah, Model 70h-7Fh) - Strix Halo (Ryzen AI Max+)

## Installation

This module can be installed via DKMS for automatic rebuilding across kernel updates.

### Prerequisites

Ensure your Linux kernel supports `amd_smn_read` for your CPU. For AMD Family 17h Model 70h (Ryzen 3000) CPUs, you need kernel version 5.3.4 or newer.

A fallback method (which may or may not work) will be used when kernel SMN support is unavailable for your CPU.

### Installation for Ubuntu/Debian

```sh
sudo apt install dkms git build-essential linux-headers-$(uname -r)
cd ~
git clone https://github.com/mattkeenan/zenpower5.git
cd zenpower5
sudo make dkms-install
```

### Installation for Arch Linux

**Note:** The existing [AUR package](https://aur.archlinux.org/packages/zenpower3-dkms/) is for zenpower3, not zenpower5. Use the manual installation method above until a zenpower5 AUR package is available.

```sh
sudo pacman -S dkms git base-devel linux-headers
cd ~
git clone https://github.com/mattkeenan/zenpower5.git
cd zenpower5
sudo make dkms-install
```

### Installation for Fedora

**Note:** The existing [copr package](https://copr.fedorainfracloud.org/coprs/birkch/zenpower3/) is for zenpower3, not zenpower5. Use the manual installation method below.

```sh
sudo dnf install dkms git kernel-devel
cd ~
git clone https://github.com/mattkeenan/zenpower5.git
cd zenpower5
sudo make dkms-install
```

## Module Activation

Because zenpower uses the same PCI device as the kernel's built-in k10temp driver, you must disable k10temp first.

1. Check if k10temp is active: `lsmod | grep k10temp`
2. Unload k10temp: `sudo modprobe -r k10temp`
3. Blacklist k10temp (recommended):
   ```sh
   sudo bash -c 'echo -e "\n# replaced with zenpower\nblacklist k10temp" >> /etc/modprobe.d/blacklist.conf'
   ```
4. Activate zenpower: `sudo modprobe zenpower`

If k10temp is not blacklisted, you'll need to manually unload it after each system restart.

## Sensors Monitoring

Use the `sensors` command from lm-sensors, or any hardware monitoring software that supports hwmon kernel interfaces.

Example output:
```
zenpower-pci-00c3
Adapter: PCI adapter
Tdie:         +45.2°C
Tctl:         +45.2°C
RAPL_P_Package: 28.50 W
```

## Update Instructions

1. Unload zenpower: `sudo modprobe -r zenpower`
2. Go to zenpower directory: `cd ~/zenpower5`
3. Uninstall old version: `sudo make dkms-uninstall`
4. Update code from git: `git pull`
5. Install new version: `sudo make dkms-install`
6. Activate zenpower: `sudo modprobe zenpower`

## Architecture

Zenpower5 uses a multi-file backend architecture:

- **zenpower_core.c** - Core driver framework, hwmon interface, CPU detection
- **zenpower_svi2.c** - SVI2 telemetry backend (voltage, current, power for Zen 1-3)
- **zenpower_rapl.c** - RAPL MSR backend (power monitoring for Zen 5)
- **zenpower_temp.c** - Temperature monitoring backend (all generations)
- **zenpower.h** - Shared data structures and function prototypes

This structure allows for easy addition of new monitoring backends as AMD introduces new telemetry methods.

## Module Parameters

- `zen1_calc` - Force use of Zen 1 current calculation formula (default: auto-detect)
- `multicpu` - Enable multi-CPU socket support for Threadripper/EPYC systems (default: auto)

## Development

This project was developed with assistance from Claude Code (claude-sonnet-4-5-20250929) for code analysis, refactoring, and Linux kernel best practices implementation.

Key development contributions:
- Multi-file backend refactoring
- CPU model quirks system implementation
- CCD temperature formula corrections
- Kernel version compatibility updates
- Error handling improvements

## Upstream Credits

Zenpower5 builds upon the excellent work of:

- **ocerman** - Original zenpower driver
- **Ta180m** - zenpower3 with Zen 3 support
- **AliEmreSenel** - Continued zenpower3 maintenance

This fork represents a structural modernisation while maintaining compatibility with the original zenpower design.

## Notes

- Some users report that a system restart is needed after initial module installation
- The meaning of raw current values from SVI2 telemetry is not standardised, so current/power readings may vary in accuracy depending on motherboard implementation
- Zen 5 systems use SVI3 (not SVI2) for voltage/current telemetry, which is not supported yet by zenpower5. RAPL provides power monitoring as an alternative
- CCD (Core Complex Die) temperatures may not be exposed on all CPU models, particularly mobile/APU variants

## License

This driver is licensed under the GNU General Public License v2.0 or later. See LICENSE file for details.

## Contributing

Bug reports, feature requests, and pull requests are welcome at:
https://github.com/mattkeenan/zenpower5

When reporting issues, please include:
- CPU model (from `lscpu` or `/proc/cpuinfo`)
- Kernel version (`uname -r`)
- Output of `sensors` command
- Relevant `dmesg` output after loading the module

## Troubleshooting

**Module fails to load:**
- Ensure k10temp is unloaded: `sudo modprobe -r k10temp`
- Check kernel log: `dmesg | tail -30`
- Verify kernel headers are installed: `ls /lib/modules/$(uname -r)/build`

**No sensors showing:**
- Confirm module is loaded: `lsmod | grep zenpower`
- Check for errors: `dmesg | grep zenpower`
- Some sensors may be unavailable on specific CPU models (expected behaviour)

**Compilation errors:**
- Ensure you have the correct kernel headers for your running kernel
- For older Ubuntu versions with newer kernels, see upstream zenpower issue #23

## Repository

**Canonical URL:** https://github.com/mattkeenan/zenpower5
