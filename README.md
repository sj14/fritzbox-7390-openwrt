# OpenWrt for the FRITZ!Box 7390

A current Linux kernel (6.18) and OpenWrt 25.12 for the AVM FRITZ!Box Fon WLAN 7390, for use as an access point and switch behind an existing router. Unlike the AVM firmware, whose last update (06.88) is from 2023, it gets OpenWrt's security updates and its packages and settings, for example VLANs, several Wi-Fi networks or a guest network.

> **Early state.** The image is installed and runs on one box since 2026-11-08, but settings are not saved yet, and the steps to install it are not written up for others yet.

This is an unofficial hobby project, tested on one box only. It is not affiliated with AVM or the OpenWrt project.

## What works

| Feature | State |
|---|---|
| Wi-Fi 2.4 GHz and 5 GHz | works, WPA2 and WPA3 |
| Wi-Fi speed | about 80 Mbit/s on both bands, the same as with the AVM firmware on the same connection |
| LAN sockets 1 to 4 | work at 1 Gbit/s; the speed between two sockets is not measured yet |
| Web interface (LuCI) and SSH | work |
| LEDs | can be switched off or set in LuCI under System, LED Configuration |
| USB | works, tested with a USB Ethernet adapter |

The processor is the limit for Wi-Fi. Estimated from its load at 80 Mbit/s, it can move about 130 Mbit/s between LAN and Wi-Fi.

## What doesn't work

| Feature | State |
|---|---|
| DSL | not supported, there is no open driver, so the box only works behind another router |
| Telephony (FON, ISDN, DECT) | not supported, there are no open drivers |
| Saved settings | not yet: the first version keeps the flash read only, so every start begins with the settings of the image |
| Updates from the web interface (`sysupgrade`) | not yet |
| Internal storage (512 MB NAND) | not usable yet |
| WLAN and DECT buttons | not set up yet |
| Ready-made images | not possible: the image needs firmware files from AVM that may not be passed on, so everyone builds it with their own copy (see `DEVELOPMENT.md`) |

## Going back to the AVM firmware

The box's bootloader (EVA) stays untouched and can be reached over the network at every start, so an AVM firmware image can always be written back. This was done once already, with a backup of the box's own firmware. The steps will be described together with the installation.

## Development

How the image is built and tested, and what was found out about the hardware: `DEVELOPMENT.md` and `kernel/README.md`.

## License

GPL-2.0-only, see `LICENSE`, the same as the Linux kernel. The firmware files from AVM that a build needs are not part of this repository and not covered by it.
