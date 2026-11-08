# OpenWrt for the FRITZ!Box 7390

A current Linux kernel (6.18) and OpenWrt 25.12 for the AVM FRITZ!Box Fon WLAN 7390, for use as an access point and switch behind an existing router.

> **Not ready to install yet.** The image runs on a real box when started from RAM, which leaves the installed AVM firmware untouched. The version for the flash is being tested. This page will describe how to install it once that has been done on a box.

This is an unofficial hobby project. It is not affiliated with AVM or the OpenWrt project.

## What works

| Feature | State |
|---|---|
| Wi-Fi 2.4 GHz and 5 GHz | works, WPA2 and WPA3 |
| Wi-Fi speed | about 80 Mbit/s on both bands, the same as with the AVM firmware on the same connection |
| LAN sockets 1 to 4 | work at 1 Gbit/s; the speed between two sockets is not measured yet |
| Web interface (LuCI) and SSH | work |
| LEDs | can be switched off or set in LuCI under System, LED Configuration |
| USB | works, tested with a USB Ethernet adapter |

## What doesn't work

| Feature | State |
|---|---|
| DSL | not supported, there is no open driver |
| Telephony (FON, ISDN, DECT) | not supported, there are no open drivers |
| Saved settings | not yet: the first version keeps the flash read only, so every start begins with the settings of the image |
| Updates from the web interface (`sysupgrade`) | not yet |
| Internal storage (512 MB NAND) | not usable yet |
| WLAN and DECT buttons | not set up yet |

The processor is the limit for Wi-Fi. Estimated from its load at 80 Mbit/s, it can move about 130 Mbit/s between LAN and Wi-Fi.

## Compared with the AVM firmware

Advantages:

- Current kernel and OpenWrt with their security updates. AVM's last update for the 7390 (06.88) is from 2023.
- WPA3 for Wi-Fi.
- OpenWrt's packages and settings, for example VLANs, several Wi-Fi networks or a guest network.

Disadvantages:

- No DSL and no telephony, so the box only works behind another router.
- Settings are not saved yet.
- Experimental: tested on one box only.
- The image has to be built yourself, because it needs firmware files from AVM that may not be passed on (see `DEVELOPMENT.md`).

## Going back to the AVM firmware

The box's bootloader (EVA) stays untouched and can be reached over the network at every start, so an AVM firmware image can always be written back. The steps will be described together with the installation.

## Alternative

If you mainly want current updates and faster Wi-Fi, a used device with official OpenWrt support is far less effort, for example a FRITZ!Box 7530. Check the OpenWrt device list before buying.

## Development

How the image is built and tested, and what was found out about the hardware: `DEVELOPMENT.md` and `kernel/README.md`.
