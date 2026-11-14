# Linux 6.18 and OpenWrt for the FRITZ!Box 7390

The kernel, the first userspace program and the build of the OpenWrt images: test images that run from RAM, and flash images to install. What was found out about the hardware is in `../DEVELOPMENT.md`.

## Layout

| Path | Content |
|---|---|
| `linux/` | new files, copied over the kernel tree |
| `linux/arch/mips/fusiv/prom.c` | boot arguments, memory, early console, reset, CP0 timer setup |
| `linux/arch/mips/fusiv/irq.c` | Vx180 interrupt controller |
| `linux/arch/mips/fusiv/bringup.c` | RAM log, LEDs, watchdog |
| `linux/arch/mips/fusiv/eth.c` | LAN port through the packet processors |
| `linux/arch/mips/fusiv/gpio.c` | GPIOs 16 to 31, for the LEDs |
| `linux/arch/mips/fusiv/usb.c` | USB PHY and clock |
| `linux/arch/mips/fusiv/ramboot.c` | the image that EVA started from RAM as an MTD device, for testing a flash image |
| `linux/net/bridge/br_fast_forward.c` | short path for IP packets between the LAN port and the radios, past the rest of the bridge |
| `linux/arch/mips/boot/dts/ikanos/` | device tree |
| `linux/arch/mips/configs/fusiv_defconfig` | kernel configuration |
| `patches/` | the few changes to existing kernel files |
| `initramfs/openwrt-init.c` | `/init`: mounts the squashfs of a flash image, finds the NAND and sets it up for OpenWrt's overlay at the first start from flash, sets up the switch and starts OpenWrt's init |
| `initramfs/board.c` | shared by `/init` and `load-fpga`: FPGA, switch over MDIO, logging to the kernel log |
| `initramfs/load-fpga.c` | loads the FPGA under OpenWrt, from `/etc/init.d/fpga` and `avm-firmware-install --usb` |
| `openwrt/` | settings and files for the OpenWrt image, among them `avm-firmware-install` and `/usr/libexec/avm-firmware/extract` |
| `test/` | scripts for test runs from another machine, through EVA |
| `tools/make-eva-image.py` | creates and checks images for the EVA bootloader |
| `tools/eva-ramboot.py` | loads an image through EVA into RAM or writes it to flash |
| `tools/fetch-openwrt-rootfs.sh` | builds the OpenWrt root file system with the ImageBuilder of a fixed release |
| `tools/check-kernel-modules.sh` | stops the build if the kernel lacks a kernel module that the OpenWrt image brings |
| `tools/fetch-avm-firmware.sh` | takes AVM's files from AVM's public firmware download, for an image that carries them |
| `tools/fix-avm-squashfs.c`, `unsquashfs-avm.nix` | make AVM's root file system readable |
| `tools/extract-ap-firmware.c` | writes the firmware of the packet processors from AVM's kernel modules |
| `avm-firmware-tools.nix` | builds the two C tools above, for the build machine or the box |
| `nixpkgs.nix` | the pinned nixpkgs that `shell.nix` and the other Nix files build with |

## Building

Everything builds inside `nix-shell` in `kernel/`; `shell.nix` brings all tools. The first run compiles the MIPS cross compiler, which can take an hour or more. Nothing marks it as in use, so a garbage collection of the Nix store removes it; keeping the build inputs as a garbage collection root avoids that:

```bash
nix-build shell.nix -A inputDerivation -o build/shell-inputs
```

1. Build the OpenWrt files: `nix-shell --run "tools/fetch-openwrt-rootfs.sh build/openwrt"`. It downloads the ImageBuilder of OpenWrt 25.12.6 for `ath79/generic`, whose packages are built for the same CPU (`mips_24kc`, big endian), checks its SHA256, builds an image with LuCI and without kernel modules, and prints its root directory as `OPENWRT_ROOTFS`. The kernel stays this one, with everything built in. The release and the package list are set at the top of the script. The packages come from the release's package feeds, so a later run can pick up newer versions outside OpenWrt's base feed, such as LuCI; the script writes them and their versions to `build/openwrt/packages.manifest`.
2. Build a test image: `OPENWRT_ROOTFS=<printed directory> SSH_PUBLIC_KEY=<public key> nix-shell --run ./build.sh`. The result is `build/fritzbox-7390.eva`, with OpenWrt in its initramfs, to load into RAM.

`openwrt/uci-defaults-test` sets up a test image as an access point in the home network on its first start: the LAN at `192.168.0.250` without a DHCP server of its own, both radios with WPA2/WPA3 (`sae-mixed`). The passwords of its Wi-Fi networks and of root are random, in `build/wifi-password` and `build/root-password`, and `SSH_PUBLIC_KEY` names a key for SSH. It leaves a box alone that it set up before, because OpenWrt runs it again after each sysupgrade.

`/init` restarts a test image after `TEST_DURATION_S` seconds (600 by default), so that the box returns to its installed OpenWrt even when the image can't be reached; 0 keeps it running.

### Flash image

With `FLASH_IMAGE=1` as well, `build.sh` also writes `build/fritzbox-7390-flash.bin`: the EVA image, whose initramfs holds only `/init`, padded to 64 KB, a dummy squashfs that EVA expects after the kernel, padding to 4 MB, the OpenWrt files as squashfs padded to 6 MB, and the mark `0xdeadc0de` that starts `rootfs_data`. It fills at most the firmware area of 14.875 MB, see "Flash layout of the installed image" in `../DEVELOPMENT.md`. The same file can be tested from RAM first, where it finds its squashfs in `rootfs_ram`. For an installed image, build it with `TEST_DURATION_S=0`.

### Building the release image

The image of the releases, the one of the README's "Installing". For each tag, the workflow `../.github/workflows/release.yml` runs these steps on GitHub, with `nix-shell --pure`, and attaches the image to the release.

1. Build the OpenWrt files as above.
2. Build the image:

   ```bash
   RELEASE_IMAGE=1 \
     OPENWRT_ROOTFS=build/openwrt/openwrt-imagebuilder-25.12.6-ath79-generic.Linux-x86_64/build_dir/target-mips_24kc_musl/root-ath79 \
     nix-shell --run ./build.sh
   ```

   The image is `build/fritzbox-7390-flash.bin`. Flash it as in the README, with `tools/eva-ramboot.py`.

`RELEASE_IMAGE=1` makes a flash image with `TEST_DURATION_S=0`, OpenWrt's own defaults (`192.168.1.1` with DHCP, no root password) and the setup network, without `uci-defaults-test`, `test-diagnostics` and an SSH key, and without AVM's files. `build.sh` stops if `AP_FIRMWARE_DIR`, `BITFILE` or `SSH_PUBLIC_KEY` is set with it, so that none of them ends up in it by mistake. `TEST_DURATION_S` can still be set, for a test from RAM.

## AVM's files

The box needs files from AVM's firmware, which must not be published:

- `fusiv-bmu.bin` and `fusiv-eth.bin`, the firmware of the packet processors, written by `tools/extract-ap-firmware.c` from `fusivlib_lkm.ko` (prefix `BMUAP`) and `ethdriver_lkm.ko` (prefix `ETHAP`). Without them `lan0` can't start.
- `fpga.bit`, the FPGA bitstream, which powers the USB port. Once loaded, the FPGA also lights a red LED inside the box, because nothing sets up the telephony behind it as AVM's driver does, so leave it out if you don't need USB.

`openwrt/files/usr/libexec/avm-firmware/extract` takes all three out of AVM's public firmware 06.88 for the 7390. It reads the image from stdin and never stores it: `sha256sum` checks it while `tar` passes `kernel.image` on to `fix-avm-squashfs`, so only AVM's squashfs (14 MB) lies on disk for a moment, which also fits into the RAM of the box (about 26 MB free under OpenWrt). It reads that squashfs with Freetz's `unsquashfs` (`unsquashfs-avm.nix`), after `fix-avm-squashfs` corrected AVM's metadata pointers, and writes nothing unless the checksum matches. The files are identical to those copied from a box. The same script runs in two places:

- **On the box**, as in the README's "Installing": `avm-firmware-install` reads the image from stdin (`ssh root@<box> avm-firmware-install < FRITZ.Box_Fon_WLAN_7390-06.88.image`), installs `fusiv-bmu.bin` and `fusiv-eth.bin` in `/lib/firmware` when they differ, where `/lib/upgrade/keep.d/avm-firmware` keeps them through a sysupgrade, removes the setup network and restarts the network. With `--usb` it also installs `fpga.bit` and loads it with `load-fpga`. The tools lie next to `extract` in `/usr/libexec/avm-firmware/`.
- **On the build machine**, for an image that carries the files: `nix-shell --run "tools/fetch-avm-firmware.sh build/avm-firmware"` downloads AVM's image and writes the three files to `build/avm-firmware`. Then build with `AP_FIRMWARE_DIR=build/avm-firmware` and, for USB, `BITFILE=build/avm-firmware/fpga.bit`.

Without the firmware of the packet processors, an image comes up with the setup network instead: at the first start, `openwrt/files/etc/board.d/02_network` sets OpenWrt's default network (`ucidef_set_wireless`) to `OpenWrt-7390-Setup-` and the last four hex digits of the LAN address, on 2.4 GHz, WPA2 with the fixed key `fritzbox7390-setup` (both in `openwrt/files/lib/functions/fritzbox7390-setup.sh`). It is the only way onto the box until `avm-firmware-install` has installed the firmware. The key is public, so anyone in range who knows it could take over the box while it waits. A factory reset empties the overlay and with it the installed firmware, so the box comes up with the setup network again.

OpenWrt loads the FPGA with `/etc/init.d/fpga` (start 15, before the network), because only then is the overlay mounted, where `avm-firmware-install --usb` puts the bitstream.

## Testing

The scripts in `test/` run on another machine, inside `nix-shell`, while the box runs its installed OpenWrt. They restart it into EVA over SSH, load an image into RAM and let it restart into the installed OpenWrt at the end. They need:

- `BOX_ADDRESS`, the address of the installed OpenWrt, and `OPENWRT_KEY`, the key that it and the test images accept (`SSH_PUBLIC_KEY` in `build.sh`); the public half of a key in the SSH agent works. A test image comes up at `TEST_ADDRESS`, `192.168.0.250` by default.
- `BOX_BSSID_PREFIX`, the start of the box's own BSSIDs: the scripts take the box's radios down first and wait until this machine is on another access point, because it can't see EVA through the box.
- `WIFI_DEVICE`, this machine's Wi-Fi interface (default `wlp5s0`), which gets an address next to EVA's (`192.168.178.2/24`, with `nmcli dev modify`, no root needed).

The scripts:

- `test/openwrt-test.sh <image> <prefix>` loads a test image, waits for SSH, saves the output of `test-diagnostics` and the logs, and restarts the box.
- `test/openwrt-setup-test.sh <image> <AVM firmware image> <prefix>` tests an image built without `AP_FIRMWARE_DIR` the way a new box is set up: it joins this machine to the setup network (`SETUP_SSID`, given rather than scanned for, because the test machine's brcmfmac lists only the access point it is connected to), runs `avm-firmware-install` there (with `INSTALL_ARGS`, for example `--usb`), returns to the previous network and checks that the image now answers over the LAN. `openwrt/uci-defaults-test` leaves the setup network on its radio, so such a test image has it on 2.4 GHz and the test network on 5 GHz.
- `test/read-ramlog.sh <log>` saves the RAM log of the last kernel that EVA started from RAM, with `RAMLOG_TOOL`, `../tools/fusiv-ramlog.c` built for the box. When a test image never answers, it restarts by itself after `TEST_DURATION_S`, and the RAM log holds its kernel log, OpenWrt's log and the state report.

`tools/eva-ramboot.py 192.168.178.1 reboot` leaves EVA if the box stays there.

The kernel has bring-up aids in `linux/arch/mips/fusiv/bringup.c`:

- **RAM log:** all kernel messages also go to 256 KB of RAM at `0x01c00000`, but only from a kernel that EVA started from RAM (`mtdram1=` on its command line), so an installed OpenWrt keeps the log of the test before it. `fusiv-ramlog dump` reads it, `fusiv-ramlog scan` finds marks that the kernel writes every 64 KB right before a restart.
- **LEDs:** Internet = `prom_init` reached, Festnetz = interrupt setup reached, WLAN = timer setup reached, Info = timer interrupts work.
- **Watchdog:** it runs from the start, so a hang should restart the box. Whether it really resets the chip is unconfirmed. A panic restarts the box after 10 s.

**With a serial console** (not connected yet, see `../DEVELOPMENT.md`): connect a PC directly to a LAN socket with `192.168.178.2/24`, power-cycle the box and run `python3 tools/eva-ramboot.py 192.168.178.1 load build/fritzbox-7390.eva` within the first seconds; the console then shows the kernel log and OpenWrt's start. `eva-ramboot.py` also runs `SETENV memsize` and `SETENV kernel_args_tmp`; whether those persist in the flash environment is unconfirmed, so compare `memsize` with its normal value `0x08000000` afterwards. `eva_ramboot.py` comes from [OpenWrt](https://github.com/openwrt/openwrt/blob/main/scripts/flashing/eva_ramboot.py).

## PCI bridge problems

Both are handled in `patches/0003-ath9k-work-around-fusiv-pci-bridge.patch`. When they happen, the registers of the bridge itself read as zero, the chips stop answering, and the unserved Wi-Fi interrupt ends in `irq 25: nobody cared`.

- **Reading analog registers (`0x7800` to `0x78ff`) kills the bridge.** Reads of `0x7860` under the AVM firmware and `0x7894` under this kernel both did it, but `0x7810` and `0x7834` did not, so the range is a safe generalization. The patch answers these reads from a copy of the last written values. Do not read this range from tools, because it takes down the Wi-Fi of the running firmware too.
- **Too many register writes in flight end in a master abort.** The patch reads from the chip after each write, as far as it is allowed, and pauses after analog writes.

The chips get their real PCI ID in the first bus scan (`arch/mips/pci/pci-fusiv.c`), so `ath9k_pci_owl_loader` is not used. With the owl loader, the second chip read ID 0, which is explained by the first problem.

## USB power needs the FPGA

The USB port only gets power after the board's Xilinx FPGA (Spartan XC3S250E) is programmed. Without that, EHCI and OHCI start and report port power, but no device is seen and interrupt 35 stays at zero. A keyboard with backlight showed it: it stays dark under Linux without the FPGA, and it lights up about 13 s after the start of the AVM kernel, when the firmware loads the FPGA. With the FPGA loaded, Linux finds USB devices (see the USB Ethernet log in `../hardware/`).

`load-fpga` programs the FPGA in slave serial mode through GPIOs (`PROG` 14, `CLK` 11, `DATA` 12, `DONE` 8) from `/lib/firmware/fpga.bit`, which comes from `avm-firmware-install --usb` or from `BITFILE` at the build. Without it, USB has no power, and the red LED inside the box stays off.

`usbcore.autosuspend=-1` is also needed, because the controller otherwise stops with the root hub suspended.

Under the AVM firmware the keyboard also goes off for about 3 s after coming on. The cause is unconfirmed. What the FPGA does besides USB power is unknown.
