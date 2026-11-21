# Adding the FRITZ!Box 7390 to OpenWrt

Whether this project could become an official OpenWrt device through a pull request, and what that would take. In short: not in its current form. The hardware is suitable and several parts already follow OpenWrt's conventions, but the project is a build of its own around OpenWrt, not an OpenWrt target, and one point, the firmware of the LAN port, could rule it out altogether.

## What already fits

- **Hardware:** 16 MB NOR, 128 MB RAM and 512 MB NAND, well above OpenWrt's minimum.
- **Bootloader and installation:** OpenWrt knows EVA from its other AVM devices, and `kernel/tools/eva-ramboot.py` follows OpenWrt's `scripts/flashing/eva_ramboot.py`.
- **Updates:** `sysupgrade` with OpenWrt's image metadata, a documented way back to the AVM firmware, and the bootloader is never written.
- **Device tree:** LEDs with `led-boot`, `led-failsafe`, `led-running` and `led-upgrade`, flash partitions and `gpio-leds`, as OpenWrt expects them.
- **Partial support:** missing DSL and telephony are no reason to refuse a device; many supported devices lack such parts.
- **License:** GPL-2.0.

## What is missing or would be refused

1. **The firmware of the packet processors, the hardest point.** Without `fusiv-bmu.bin` and `fusiv-eth.bin`, `lan0` can't start, and they may not be passed on. An official image would come up without Ethernet, and so without failsafe mode, which runs over Ethernet. The setup network with a published password is unlikely to be accepted in its place: OpenWrt leaves Wi-Fi off by default, and open root access over the air is a security problem. This needs an answer from OpenWrt's maintainers before any further work.
2. **Not an OpenWrt target.** The kernel (6.18.62) is built here with Nix, and the root file system comes from the ImageBuilder of `ath79/generic`. OpenWrt expects a `target/linux/fusiv/` on the kernel version OpenWrt uses at the time, with patches in OpenWrt's layout and an `image/Makefile` with a device profile, built in OpenWrt's build system.
3. **An `/init` of its own instead of OpenWrt's preinit.** `kernel/initramfs/openwrt-init.c` mounts the squashfs, sets up the NAND and sets up the switch. OpenWrt's own mechanisms (preinit, `mount_root` of fstools with a UBI volume `rootfs_data`) and kernel drivers would have to do this.
4. **Hardware access from userspace.** GPIOs 0 to 15 (FPGA, switch reset) are driven through `/dev/mem`, and the switch is set up over MDIO from `kernel/initramfs/board.c`. Upstream this needs a GPIO driver for all pins, an MDIO bus driver and the AR8316 through the existing `ar8216` driver (swconfig) or DSA. Whether swconfig is still acceptable for a new target is open.
5. **Drivers in the wrong place.** `eth.c`, `gpio.c` and `usb.c` lie in `arch/mips/fusiv/`. `eth.c` and `gpio.c` are device tree drivers already, but `usb.c` and the PCI host in `arch/mips/pci/pci-fusiv.c` are `arch_initcall`s with fixed addresses. The watchdog in `watchdog.c` is served by a kernel timer instead of being a driver for OpenWrt's `procd`. Expected are drivers under `drivers/` with device tree bindings.
6. **Patches unlikely to be accepted as they are:**
   - the fast-forward shortcuts in the bridge and mac80211 (`linux/net/bridge/br_fast_forward.c`, `0005`, `0009`),
   - the throughput changes to ath9k (`0006` to `0008`),
   - the PCI bridge workaround (`0003`) and the changes to jffs2 and gpio-nand (`0011`, `0012`), which would need a good justification or would have to go into the upstream kernel first.
7. **Bring-up aids would have to go:** the RAM log, the boot stage LEDs and `ramboot.c` are built only with `CONFIG_FUSIV_BRINGUP`, so a kernel for OpenWrt can leave them out by switching it off. `TEST_DURATION_S` lies in `/init` and goes with it (point 3).
8. **A maintainer for the target.** A new target is only taken in when someone keeps it working through OpenWrt's kernel updates; otherwise it becomes source-only or is removed.

## A realistic way

1. Ask first, in the OpenWrt forum or on the `openwrt-devel` mailing list: is a device acceptable whose Ethernet needs firmware that the box takes from AVM's public firmware download itself? If not, would a source-only target be?
2. If the answer is yes, rebuild the project as `target/linux/fusiv` in a fork of OpenWrt, work through points 3 to 7, and submit the kernel patches to the upstream kernel separately.

That is a rebuild of several weeks, not a formality for a pull request. Until then, a page in the OpenWrt wiki that points to this repository is the simple step in between.
