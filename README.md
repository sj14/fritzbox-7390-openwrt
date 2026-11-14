# OpenWrt for the FRITZ!Box 7390

A current Linux kernel (6.18) and OpenWrt 25.12 for the AVM FRITZ!Box Fon WLAN 7390, for use as an access point and switch behind an existing router. Unlike the AVM firmware, whose last update (06.88) is from 2023, it gets OpenWrt's security updates and its packages and settings, for example VLANs, several Wi-Fi networks or a guest network.

> **Early state.** The image is installed and runs on one box since 2026-11-08. The steps below are the ones used there; nobody else has followed them yet.

This is an unofficial hobby project, tested on one box only. It is not affiliated with AVM or the OpenWrt project.

## What works

| Feature | State |
|---|---|
| Wi-Fi 2.4 GHz and 5 GHz | works, WPA2 and WPA3 |
| Wi-Fi speed | about 80 Mbit/s on both bands, the same as with the AVM firmware on the same connection |
| LAN sockets 1 to 4 | work at 1 Gbit/s; the speed between two sockets is not measured yet |
| Web interface (LuCI) and SSH | work |
| LEDs | can be switched off or set in LuCI under System, LED Configuration |
| USB | works, tested with a USB Ethernet adapter; needs the FPGA bitstream (`avm-firmware-install --usb`), which also lights a red LED inside the box, so leave it out if you don't use USB |
| Saved settings | work, in about 460 MB of the internal NAND (UBIFS), which also holds installed packages; see "Settings and packages on the NAND" |
| Updates (`sysupgrade`, also in LuCI) | work, and keep the settings |

The processor is the limit for Wi-Fi. Estimated from its load at 80 Mbit/s, it can move about 130 Mbit/s between LAN and Wi-Fi.

## What doesn't work

| Feature | State |
|---|---|
| DSL | not supported, there is no open driver, so the box only works behind another router |
| Telephony (FON, ISDN, DECT) | not supported, there are no open drivers |
| WLAN and DECT buttons | not set up yet |
| Ready-made images | none published yet. The image no longer contains AVM's firmware files, which may not be passed on: the box takes them from AVM's public firmware download itself, see "Installing" |

## Installing

> **Not tested end to end yet.** These steps install the image without AVM's files, which the box then takes from AVM's download itself. Each part was tested on its own: the build, the setup network and `avm-firmware-install` from RAM, flashing with an earlier image. OpenWrt's defaults at `192.168.1.1` and the settings in LuCI (steps 9 to 11) were not tried on this box yet.

You need a Linux machine with [Nix](https://nixos.org/download/) and Wi-Fi, connected by cable to one of the box's LAN sockets or to the same switch. The box's bootloader EVA writes the image. It answers at `192.168.178.1` for a few seconds after each power-on, and it is never written itself, so it stays available for a rescue. Your AVM settings in flash (`tffs`) stay untouched as well.

Before you start:

- **Back up the AVM firmware** if you might want it back exactly as it is. With Freetz on the box, `kernel/test/flash-backup.sh` copies it, see "Installing" in `DEVELOPMENT.md`.
- **Copy what you need from the AVM internal storage.** The first start of OpenWrt erases the NAND that holds it, see "Settings and packages on the NAND".
- **Download AVM's firmware 06.88** for the 7390, [`FRITZ.Box_Fon_WLAN_7390-06.88.image`](https://download.avm.de/fritzbox/fritzbox-7390/deutschland/fritz.os/FRITZ.Box_Fon_WLAN_7390-06.88.image). The box takes the firmware of its LAN port and, if you want USB, the FPGA bitstream from it. Get it now: the network through which you hand it to the box has no internet.

Building, in `kernel/`:

1. Build the OpenWrt files with OpenWrt's ImageBuilder: `nix-shell --run "tools/fetch-openwrt-rootfs.sh build/openwrt"`.
2. Build the image. `RELEASE_IMAGE=1` builds it with OpenWrt's own defaults and without any of AVM's files:

   ```bash
   RELEASE_IMAGE=1 \
     OPENWRT_ROOTFS=build/openwrt/openwrt-imagebuilder-25.12.6-ath79-generic.Linux-x86_64/build_dir/target-mips_24kc_musl/root-ath79 \
     nix-shell --run ./build.sh
   ```

   The image is `build/fritzbox-7390-flash.bin`.

Flashing:

3. Give the machine an address next to EVA's, for example `sudo ip addr add 192.168.178.2/24 dev <interface>`.
4. Start the tool. It waits up to four minutes for EVA:

   ```bash
   nix-shell --run "python3 tools/eva-ramboot.py --wait 240 192.168.178.1 flash build/fritzbox-7390-flash.bin"
   ```

5. Unplug the box's power, wait a few seconds and plug it in again. The tool writes the image (one to two minutes, don't cut the power) and restarts the box.
6. Wait about three minutes. The power LED blinks while OpenWrt starts, then stays on. At the first start it blinks once more while OpenWrt sets up the space for its settings; the log line "failed to sync jffs2 overlay" from that step is harmless.

Installing AVM's files:

7. The LAN sockets don't work yet. Instead the box brings up the Wi-Fi network `OpenWrt-7390-Setup-` followed by four characters, on 2.4 GHz, with the password `fritzbox7390-setup`. Join it; the box gives your machine an address in `192.168.1.0/24`.

   That password is published here, and `root` has no password yet. Until step 9, anyone in range who knows this could log in to the box, so do steps 7 to 9 without a break.
8. Hand AVM's file to the box. Add `--usb` after `avm-firmware-install` if you want USB:

   ```bash
   ssh root@192.168.1.1 avm-firmware-install < FRITZ.Box_Fon_WLAN_7390-06.88.image
   ```

   SSH asks for no password. The script checks the file, installs the firmware, removes the setup network and restarts the network, which ends the connection. The LAN sockets work from now on, and the files stay through updates.

Setting it up:

9. Connect the machine by cable directly to a LAN socket of the box, not to your home network yet, and open `http://192.168.1.1`. Log in as `root` with an empty password and set one under System, Administration.
10. Under Network, Wireless, add your own networks. On 2.4 GHz, pick a fixed channel (1, 6 or 11) that your router doesn't use. Click "Save & Apply" at the end; "Save" alone only collects the changes.
11. Make the box an access point in your network: under Network, Interfaces, edit `lan`, give it a free address in your network with your router as gateway and DNS server, and under "DHCP Server" tick "Ignore interface", because your router hands out the addresses. "Save & Apply" then asks whether to keep the change, because the box leaves `192.168.1.1`; choose "Apply unchecked".
12. Connect the box to your home network and open it at its new address. Make a backup under System, Backup / Flash Firmware, and keep it.

## Updating

Build a new image the same way and install it under System, Backup / Flash Firmware, with "Keep settings" checked, or over SSH: copy it to `/tmp` on the box (`ssh root@<box> "cat > /tmp/fritzbox-7390-flash.bin" < build/fritzbox-7390-flash.bin`) and run `sysupgrade /tmp/fritzbox-7390-flash.bin` there. OpenWrt warns that the image has no metadata; the image check of this board still runs. Tested over SSH, not yet in LuCI.

As on any OpenWrt device, an update keeps the settings, and here also AVM's files, but not the packages you installed yourself, nor their package feeds. Install them again afterwards.

## Settings and packages on the NAND

The box has a 512 MB NAND that the AVM firmware uses as internal storage. OpenWrt keeps its settings and installed packages there, as on OpenWrt's own NAND devices, instead of in the 4.9 MB of the NOR flash. The image sets it up by itself at its first start from flash, after installing or after an update from an image without it: it **erases the NAND**, AVM's internal storage included, and makes the UBI volume `rootfs_data` there. Settings that the update kept move along. Afterwards `df -h /overlay` shows `/dev/ubi0_0` with about 460 MB.

So copy anything you still need from the AVM internal storage before you install. A test from RAM never touches the NAND.

It leaves the NAND alone, and OpenWrt stays in the NOR flash, in three cases. The kernel log says which (`dmesg | grep NAND:`):

- The NAND has UBI already, as after the first start.
- OpenWrt's overlay in the NOR flash is in use: its settings move along only with an update that keeps them.
- The NAND has UBI headers that UBI couldn't attach, for example after damage. To start over on the NAND, erase it by hand (`flash_erase` from the package `nand-utils` of `ath79/nand`, see `DEVELOPMENT.md`), and the next start sets it up again, with the settings of the first start.

## Going back to the AVM firmware

Write your backup of the AVM firmware the same way as the OpenWrt image, in steps 5 to 7, with the backup file instead of `build/fritzbox-7390-flash.bin`. This was done with the box's own backup. AVM's public firmware download should work as well, using `var/tmp/kernel.image` from inside it, but that is not tested. The NAND then still holds OpenWrt's UBI; whether the AVM firmware formats it again for its internal storage is not tested either.

## Development

How the image is built and tested, and what was found out about the hardware: `DEVELOPMENT.md` and `kernel/README.md`.

## License

GPL-2.0-only, see `LICENSE`, the same as the Linux kernel. The firmware files from AVM that a build needs are not part of this repository and not covered by it.
