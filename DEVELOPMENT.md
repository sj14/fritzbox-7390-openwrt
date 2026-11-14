# Development notes

What was found out while porting Linux 6.18 and OpenWrt to the FRITZ!Box 7390, and how the image is tested. How to build it is in `kernel/README.md`. As of 2026-11-08.

## Summary

- **State:** A current kernel with OpenWrt 25.12 boots from RAM on the box and works as an access point at the speed of the AVM firmware, with all four LAN ports, both radios, WPA3 and switchable LEDs.
- **Installed:** since 2026-11-08 OpenWrt runs from flash on the box, see Installing.
- **Next step:** open items below; the main features work.
- **Not realistic:** official inclusion in OpenWrt as it stands, because the Ethernet driver needs proprietary firmware from AVM's modules.

## The box

Values were read from the box unless stated otherwise.

| Property | Value |
|---|---|
| Model | FRITZ!Box Fon WLAN 7390, HWRevision 156, HWSubRevision 3 |
| Firmware | 06.88 with Freetz-NG, web interface on port 81 |
| Kernel | 2.6.28.10 |
| SoC | Ikanos Fusiv (`Ikanos Fusiv Core`), Vx180 at 500 MHz according to the forum |
| CPU | MIPS 24Kc V4.12, big endian, single core |
| RAM | 128 MB (`memsize=128`) |
| NOR | 16 MB, `physmap-flash` at `0x1f000000` |
| NAND | 512 MB, `direct-avm-nand` at `0x1e000000` |
| PCI | memory window `0x1a000000`, two devices `168c:ff1d`, IRQ 25 |
| Wi-Fi | AR9220 and AR9223 according to the forum |
| Switch | AR8316 according to the forum |
| Bootloader | EVA, `bootloaderVersion 1.1939` |
| Console | `console=ttyS0,115200n8r`, UART at `0xb90a0000`, IRQ 29 |

### Flash layout

```text
mtd0  00d85b00  rootfs
mtd1  0015a500  kernel
mtd2  00020000  urlader
mtd3  00080000  tffs (1)
mtd4  00080000  tffs (2)
mtd5  01000000  reserved
mtd6  20000000  nand-filesystem
```

## Parts of the port

The starting point was AVM's newest GPL package (06.86, the box runs 06.88), which has open sources for 2.6.28 for everything except Ethernet.

| Part | State | Where |
|---|---|---|
| IRQ, timer, early console, reset | works | `kernel/linux/arch/mips/fusiv/` (`prom.c`, `irq.c`) |
| PCI | works | patch 0002 |
| NOR flash | detected through CFI, all partitions read only | device tree (`cfi-flash`) |
| NAND (512 MB) | works, UBI and UBIFS, not used by OpenWrt yet | `gpio-control-nand` in the device tree, patch 0012 |
| USB | works, needs the FPGA for power | `usb.c`, FPGA loaded by `/init` |
| Wi-Fi | works | `ath9k` with calibration from flash, patches 0003, 0006 to 0008 |
| Ethernet and switch | works with AVM's firmware for the packet processors | `eth.c`, switch setup in `kernel/initramfs/board.c` |
| GPIOs 16 to 31, LEDs | works | `gpio.c`, device tree |
| Buttons | open | GPIOs 29 (DECT) and 31 (WLAN) |
| VDSL, telephony, DECT | not planned | closed AVM drivers, not needed for an access point |

### Throughput

The goal is the speed of the AVM firmware. A laptop downloading a file from the internet through the box over 2.4 GHz reached 78.6-79.6 Mbit/s under AVM, close to the roughly 80 Mbit/s of the connection. AVM's CPU was about 90% busy doing so, against about 8% without traffic. Linux reaches 80 Mbit/s too (2.4 GHz, 40 MHz), with the CPU about 60% busy.

The CPU time per megabyte is a better measure than the speed of a single download, because the connection varies from run to run. It is the data from the LAN port divided by the softirq time in `/proc/stat`: 7.4 MB per second of softirq time with the bridge skipped, 10.1 MB after the later changes in the table below, and 16.2 MB with the write-back cache, enough for about 130 Mbit/s.

What helped, in order:

| Change | Effect |
|---|---|
| USB adapter keeps the IP header aligned (patch 0004) | 29 to 31 Mbit/s |
| LAN port instead of the USB adapter, no GRO, no copy of received packets | 34 to 45 Mbit/s |
| Kernel hardening and debug options off (with the write-through cache) | 51 Mbit/s |
| Packets between LAN and Wi-Fi skip the bridge (`net/bridge/br_fast_forward.c`, patches 0005 and 0009) | 56-57 Mbit/s |
| Call the driver directly, flow hash from the IPv4 header | 69-73 Mbit/s |
| ath9k doesn't schedule while its queue is full, fair queuing quantum of one Ethernet frame (patches 0007, 0008) | 76-78 Mbit/s |
| Write-back instead of write-through cache (`cca=3`) | 80 Mbit/s with the CPU 60% busy |
| Kernel hardening back on (stack protector, zeroed stack variables, hardened slab freelists) | about 1% more CPU time, no change in speed |

Tried without a measurable effect: `-Os` (slower, 50-53 Mbit/s), four OpenWrt patches for MIPS and mac80211 (cache loops, `-mtune=24kc`, `-mno-branch-likely`, airtime quantum, together about 1.5%).

**Write-through cache:** EVA leaves the CPU's cache mode for kernel memory (K0 in CP0 `Config`, read as `0x80248480`) at 0, write-through without write-allocate, and Linux keeps the bootloader's setting unless the command line sets `cca=`. So every store went to the slow memory, and the profile showed mostly stores. `cca=3` selects write-back. All code that the hardware reads uses uncached addresses or writes the cache back first, so this is safe. `memcpy` reached about 60 MB/s under Linux and under AVM, so AVM probably runs with write-through too.

**No accelerator for Wi-Fi:** AVM's `avm_pa_fusiv.c` (in AVM's GPL sources) shows what the packet processors do. From LAN to Wi-Fi, the MAC processor looks up the destination address in a flow table and still passes the packet to the CPU, which sends it through the Wi-Fi driver. From Wi-Fi to LAN, AVM turned the hardware off ("no HW acceleration for ingress WLAN"). So the processors only take work off the CPU between LAN and WAN.

### Ethernet

- **LAN port works (2026-11-07):** `kernel/linux/arch/mips/fusiv/eth.c` loads AVM's firmware into two packet processors (APs), and a Wi-Fi client reached the internet through `lan0` at about 34 Mbit/s, about 45 Mbit/s without GRO and without copying received packets, and about 51 Mbit/s with the kernel's stack hardening and debug options off. See `hardware/first-lan-port-6.18.log`.
- **How packets move:** The buffer manager at `0x19210000` asks for free buffers one by one with status bit `0x1000`, 4081 in total, and only then answers its configuration. The MAC processor at `0x19260000` writes received buffers to four queues in RAM, whose indexes are in shared memory at `0x191f0040`. To send, the driver takes a buffer from `0x19210810`, fills the header, sets the first egress port to `0x19260510` and writes the address there.
- **Details that cost time:** AVM forwards received packets to a third processor at `0x19100510`, which doesn't run here, so the configuration sends them to the host. In 3 of 8 runs no packets arrived, and port 0 read back as `0x3e` or `0x7e` independently of that. So MDIO writes look unreliable when they follow each other too quickly. `/init` now waits 0.5 ms after each MDIO command and logs what it reads back. All 7 runs since then received packets.
- **Limit:** 2.4 GHz with 40 MHz (270 Mbit/s link) reached about 51 Mbit/s too, the same as 5 GHz with 20 MHz (130 Mbit/s link), with the CPU fully busy in both. So the CPU is the limit, not the radio. 5 GHz with 40 MHz failed twice with `AP-STA-POSSIBLE-PSK-MISMATCH`, cause unknown.
- **Link speed (2026-11-07):** PHY 3, the port with the cable to the home network, negotiated 1000 Mbit/s full duplex (`/init` logs register `0x11` of each PHY). The data rate between two LAN ports at that speed has not been measured, it needs a second wired device.
- **All four LAN sockets (2026-11-08):** Under OpenWrt from RAM, the cable to the home network was moved from socket to socket while `tools/fusiv-links.c` watched the PHYs. Each socket negotiated 1000 Mbit/s full duplex, and through each one the box stayed reachable over SSH and reached the home router. LAN 1 is PHY 0, LAN 2 is PHY 1, LAN 3 is PHY 2 and LAN 4 is PHY 3. PHY 4 never had a link. The data rate between two sockets is still not measured: a laptop with a USB-C Ethernet adapter showed no network device under Linux, so a second wired device is still missing.
- **Open:** The MAC address of `lan0` is random. Link speeds other than the current one are not handled.
- **Switch reached (2026-11-07):** `/init` releases the switch from reset (GPIO 15, active low) and reads it over MDIO at `0x19110080`. It reports id `0x1001`, and the PHYs report `0x004d 0xd041`. PHY 3 lost its link when the cable to the home network was pulled and got it back when it was plugged in again. It is LAN 4. See `hardware/first-switch-6.18.log` and `hardware/first-link-change-6.18.log`.

What AVM's sources showed before `eth.c` existed:

- `fusiv_src/` contains only headers and Makefiles (the only `.c` file is `fusiv_srcd.c`).
- Packets pass through a network coprocessor. Its firmware is probably `ETHAP_codeSection` (11,236 bytes) in the module, loaded via `loadAPCode` from `fusivlib_lkm`.
- Switch access (`fusiv_mdio_read`, `fusiv_mdio_write`) is also in the closed module.
- Module dependencies: `fusivlib_lkm`, `ap2ap_lkm`, `timers_lkm`, `periap_driver_lkm`.
- For the LAN ports, you would have to load the firmware from your own box and rebuild the host driver through reverse engineering. The headers under `fusiv_src/kernel/ap_code/inc/netpro/` could help. The firmware probably may not be redistributed.

### Wi-Fi

- The radio chips sit directly on the SoC's PCI bus. This speaks against the second Wi-Fi CPU suggested in the forum.
- The ID `ff1d` matches the owl loader, which loads the calibration data from flash. The location is now known, see below.

Calibration data, found read-only in the `urlader` partition (`mtd2`):

| Flash offset | Header | PCI device | Radio |
|---|---|---|---|
| `0x980` | version 2, type `WLAN`, length `0x1001` | `0000:00:01.0` (`wifi0`) | 5 GHz, `opCapFlags=0x01` |
| `0x1985` | version 2, type `WLAN2`, length `0x1001` | `0000:00:02.0` (`wifi1`) | 2.4 GHz, `opCapFlags=0x02` |

- Each header is followed by one padding byte, then 4096 bytes of byte-swapped data starting at `0x985` and `0x198a`. Both sizes fit the owl loader limit, and it handles the swapped form.
- Both blocks pass: magic `0xa55a`, a register list of 12 entries with an end marker, EEPROM version `0xe010`, 2x2 masks, and an XOR checksum of `0xffff` over the declared length `0xcb8`.
- The MAC addresses in the blocks match the `macwlan` and `macwlan2` environment values and the running interfaces.
- The register list sets the PCI ID to `168c:0029`, which `ath9k` supports. `ath9k` reads these calibration blocks through `nvmem`.
- The partition also holds secrets such as `wlan_key` and `tr069_passphrase` in its environment, so a dump of it must not be published.
- **WPA3 (2026-11-08):** Under OpenWrt with `encryption=sae-mixed` (WPA2/WPA3), a laptop connected to both bands once with SAE only and once with PSK only, and reached the home router each time. hostapd logged `auth_alg=sae` and `auth_alg=open`. The image uses `wpad-basic-mbedtls`, and the kernel has the AES-CMAC that the required management frame protection needs. The test image now uses `sae-mixed`.

### LEDs

- **Switchable under OpenWrt (2026-11-08):** The LEDs are GPIOs 22 to 28, active low, as in AVM's hw156 configuration: info green 22, info red 23, power green 24, power red 25, Festnetz 26, WLAN 27, Internet 28. `kernel/linux/arch/mips/fusiv/gpio.c` drives GPIOs 16 to 31 with Linux's generic MMIO GPIO code, and the device tree lists the LEDs for `gpio-leds`. On the box, power and WLAN lit as expected, and switching both off through `uci` (as in LuCI under System, LED Configuration) left all LEDs dark.
- **Defaults:** Power shows OpenWrt's boot state through the `led-boot`, `led-running`, `led-failsafe` and `led-upgrade` aliases, WLAN is on while the 2.4 GHz network is up (`kernel/openwrt/files/etc/board.d/01_leds`), and the others stay off.
- **Red LED inside the box (2026-11-08):** It lights while the FPGA runs AVM's bitstream without AVM's Piglet driver, and stays off without `BITFILE`. That driver (`Piglet_noemif.ko`, closed) boots the DECT chip (Dialog SC14488) through the FPGA in two stages, switches the PCM bus and calls its hardware functions through a profile table; the FPGA has no register on the memory bus ("noemif"). Turning the LED off with the FPGA loaded would most likely mean booting the DECT chip like AVM, days of reverse engineering with an uncertain result, so an image without USB leaves the bitstream out instead. GPIO 6 (DECT reset in AVM's configuration) must not be driven: setting it low reset the box.
- **Not used:** GPIOs 0 to 15 get no driver, because `/init` drives the FPGA and the switch reset there. The buttons (DECT 29, WLAN 31) are not set up yet. ath9k selects `MAC80211_LEDS` as soon as `LEDS_CLASS` is on, so it adds `ath9k-phy0` and `ath9k-phy1` and drives GPIO 1 of each radio chip (`ATH_LED_PIN_DEF`) as LED. Its wiring on the 7390 is unknown, and nothing visible changed when it was on.

## Milestones

1. **Kernel to userspace (reached):** Linux 6.18 boots from RAM, without a serial cable. A telnet shell worked (`hardware/first-shell-6.18.log`), but was removed again when both Wi-Fi networks got connected to the home network.
2. **Flash and Wi-Fi (mostly reached):** NOR read-only, PCI and `ath9k` work. A laptop reached the internet over WPA2 on 2.4 GHz and 5 GHz. NAND is still open.
3. **USB Ethernet as uplink (reached):** The USB port needs the board's FPGA for power, and `/init` loads it. Wi-Fi clients reach the internet through a bridge to an AX88179 adapter (`hardware/first-bridged-access-point-6.18.log`).
4. **LAN port (reached):** `lan0` works through the packet processors, see Ethernet.
5. **OpenWrt (reached from RAM, 2026-11-08):** OpenWrt 25.12 with LuCI from the ImageBuilder for `ath79`, with this kernel, see `kernel/README.md`. From RAM the box came up as an access point at `192.168.0.250` after about 2 minutes, answered over SSH and LuCI, and a laptop on the 2.4 GHz network downloaded at 70-81 Mbit/s. Problems on the way: `wifi-scripts` and `iw` come only with `kmod-ath9k`, OpenWrt 25.12 switches Wi-Fi networks off in `wifi-iface` instead of `wifi-device`, and without `CONFIG_BRIDGE_VLAN_FILTERING` the kernel refuses netifd's bridge settings, so `br-lan` never appeared.
6. **Installed OpenWrt (reached, 2026-11-08):** the flash image runs from flash, see Installing. Settings are not saved yet.

The kernel work is in `kernel/`, including how to test it without a console.

### Testing without flashing

An image can be loaded into RAM through EVA via FTP, the same way as OpenWrt's `eva_ramboot.py`. This works on the 7390, even over Wi-Fi from a machine in the same network, and the box returns to Freetz after the test.

The box is unavailable as an access point during tests, so a second, used 7390 as a test device would be helpful.

### Installing (works, 2026-11-08)

EVA boots from the area it calls `mtd1` (`0x9F020000`-`0x9FF00000`, 14.875 MB). The AVM firmware keeps its kernel and its root file system there one after the other; under Freetz they appear as `mtd1` ("kernel", `0x15a500` bytes) and `mtd0` ("rootfs", `0xd85b00` bytes). The flash image `kernel/build/fritzbox-7390-flash.bin` is written there over EVA's FTP with `MEDIA FLSH`, `STOR mtd1` and `REBOOT`, the same way as Freetz's `tools/push_firmware`. The bootloader (`urlader`, `mtd2`) and the settings (`tffs`, `mtd3` and `mtd4`) are never written.

Before the first flash:

1. Back up the area under the current firmware, read only, from `/dev/mtd1ro` and `/dev/mtd0ro` in that order, and compare the MD5 sums with the box. `kernel/test/flash-backup.sh` does this: the Freetz shell cuts its output after 64,000 bytes, so the copy goes in 32 KiB pieces. `/dev/mtdblock1` is not suitable, because its size is not a multiple of 512 bytes and the block device drops the last 256 bytes.
2. Load the backup into RAM through EVA, like a test kernel (`kernel/tools/eva-ramboot.py`). This was done on 2026-11-07: Freetz came up from RAM with `mtdram1=0x87120000,0x88000000`, and `/proc/mtd` showed `kernel_ram` and `rootfs_ram`. So the AVM firmware can still be started for comparison runs after an install, and the backup is known to boot.

Rescue paths after an install:

| Problem | Rescue |
|---|---|
| The kernel hangs or panics | the watchdog restarts the box, EVA answers at every start |
| Back to the AVM firmware | write the backup with `MEDIA FLSH` and `STOR mtd1`, the settings in `tffs` stay |
| Logs of the installed kernel | over the network (`logread`, `dmesg`). The RAM log doesn't help here: the AVM firmware started from RAM found no log of the previous kernel, so it overwrites that area, unlike when it starts from flash |
| EVA itself broken | only JTAG, so `mtd2` is never written |

An installed image needs `TEST_DURATION_S=0`, otherwise `/init` restarts the box after each test period.

**First attempt (2026-11-08):** `tools/eva-ramboot.py flash` wrote the flash image in 45 to 80 s, and EVA confirmed it. But at the next start EVA stayed in the bootloader instead of starting the kernel, also after appending the TI checksum that ends AVM's image (`0xc453de23` and the inverted POSIX `cksum` of everything before it, both little endian, checked against the backup, where it sits at `0xed4500`). Writing the backup back the same way restored Freetz. The image booted fine from RAM, so EVA checks more when it starts from flash. The likely reason: AVM's images, and OpenWrt's for other AVM boards, have a squashfs right after the kernel, and the first image had only padding there. OpenWrt adds a dummy squashfs of 256 bytes (`eva.dummy.squashfs`) after the kernel, padded to 64 KB, and appends no TI checksum. `build.sh` now does the same. The second attempt on the same day booted: EVA started the kernel from flash, `/init` mounted `/dev/mtdblock2` (`rootfs`), and OpenWrt came up with SSH after 135 s, the LAN at `192.168.0.250` and both radios.

Layout of an installed OpenWrt (`FLASH_IMAGE=1` in `kernel/build.sh`). On 2026-11-08 the complete flash image booted from RAM, came up as an access point with SSH and LuCI, and returned to Freetz:

1. The EVA image of the kernel, with only `/init` in its initramfs, at the start of `mtd1` in a partition of 4 MB (`kernel`), followed by the OpenWrt root file system as squashfs (`rootfs`). The proprietary files (FPGA bitstream, firmware of the packet processors) are only in the squashfs. `/init` mounts the squashfs through `mtdblock` at `/rom`, puts a tmpfs over it with overlayfs (changes at `/overlay`), switches into it, loads the FPGA, sets up the switch and starts OpenWrt's init with `INITRAMFS=1`, so that preinit skips `mount_root`.
2. Linux keeps all flash partitions read only except `rootfs_data` (`0xa20000` to `0xf00000`, 4.875 MB). The first installed image had no `rootfs_data`, so every start began with the defaults of the image, and its own tmpfs overlay in `/init`: OpenWrt's `mount_root` wanted a loop device behind the squashfs on a block device ("unable to create loop device"), left `/` read only, and the box came up without network configuration. With `rootfs_data` present, `/init` leaves the overlay to `mount_root` again when the box starts from flash; the image puts OpenWrt's mark `0xdeadc0de` at the start of `rootfs_data`, so that `mount_root` erases it and sets up jffs2 at the first start. A test from RAM keeps its tmpfs overlay and never writes the flash: the data area had the same MD5 sum before and after such a test. Mainline jffs2 refused the mark ("Cowardly refusing to erase blocks on filesystem with no valid JFFS2 nodes"), so the kernel now carries OpenWrt's `532-jffs2_eofdetect.patch` (patch 0011), which erases the blocks after the mark. With it, on 2026-11-08, the first start from flash set up jffs2 on `rootfs_data`, and a setting survived a restart. At that first start `mount_root` logged "failed to sync jffs2 overlay" while copying the changes from the temporary tmpfs overlay, but the settings arrived, and later starts switch to jffs2 directly. The message comes from fstools itself: `switch2jffs` copies the changes to jffs2 first (`cp -a /tmp/root/* /rom/overlay`), and only a second copy "to be in sync" fails, because it runs after `/tmp/root` was detached. Others see it at the first start on OpenWrt boards as well, so it is left as it is. A writable `rootfs_data` partition with jffs2, and with it saved settings and `sysupgrade`, needs a flash driver that can write (CFI) and comes as a second step.
3. The same image is tested from RAM before it goes to flash: `tools/eva-ramboot.py` passes the uploaded area as `mtdram1=start,end`, and `arch/mips/fusiv/ramboot.c` registers it with `plat-ram` as `kernel_ram` and `rootfs_ram`, the names the AVM firmware uses. `/init` prefers `rootfs_ram`. `physmap` was tried first, but it rounds its window down to a power of two, so 7.2 MB became 4 MB and `rootfs_ram` was cut off.

## Serial console

So far there is **no confirmed pinout** and no verified photo for the 7390. The pins have to be measured, because the order differs between models.

### Where to look

- According to a 7390 owner: four solder pads in a row, one of them rectangular.
- On older FRITZ!Boxes, at the board edge with the LEDs. This is not confirmed for the 7390.
- Do not confuse it with the 14-pin connector on the underside, which is JTAG.

### Identifying the pins (with a multimeter)

| Pin | Measurement | Result |
|---|---|---|
| GND | power unplugged, continuity to the metal shell of LAN or USB | beeps |
| 3.3 V | box on, voltage against GND | constant at about 3.3 V, **do not connect** |
| Box TX | voltage against GND during boot | visibly fluctuates |
| Box RX | the remaining pin | only needed later |

If you measure significantly more than 3.3 V anywhere, do not connect anything.

### Connecting a Raspberry Pi

The Pi's GPIOs use 3.3 V, and its 5 V pins must not be used. Start with only two wires, so nothing can be sent to the box. Both devices must be off while connecting, and each keeps its own power supply.

| Box | Pi (physical pin) |
|---|---|
| GND | pin 6 |
| TX | pin 10 (GPIO15, RX) |
| RX, only later | pin 8 (GPIO14, TX) |
| 3.3 V | **do not connect** |

Pins can be soldered in, and the forum also mentions pogo pins as an alternative. A pin header pushed in without soldering can work if the pads are through-holes, but the contact may be unreliable.

### Reading the output

1. In `sudo raspi-config`, under "Interface Options" → "Serial Port", disable the login shell over serial and enable the hardware, then reboot.
2. Start the terminal:

   ```bash
   sudo apt install picocom
   picocom -b 115200 /dev/serial0
   ```

3. Turn on the box. At 115200 baud, the kernel log should appear.

Garbled characters or a silent phase right after power-on may come from the bootloader, since `modetty0` specifies 38400 baud. If the kernel part does not appear either, try the other pin, or `picocom -b 38400` if needed.

### Without a console

All work so far was done without a console. The kernel writes its log to a reserved RAM area, which `tools/fusiv-ramlog.c` reads under Freetz after the box restarted (see `kernel/README.md`). Early crashes before the log starts produce no output, and the LEDs show how far the boot got.

## Open items

- [x] Install the flash image
- [x] Test scripts for a box that runs OpenWrt instead of Freetz (`test/box.sh`, restart into EVA over SSH)
- [x] Saved settings: the writable `rootfs_data` partition, see Installing. Linux detects the chip through CFI (2026-11-08, from RAM): Macronix, manufacturer ID `0xc2`, chip ID `0x227e` (MX29GL family, 16 MB), one x16 chip with the AMD command set, extended query 1.3, uniform erase blocks of 128 KB, so `urlader` is exactly the first block. AVM's kernel uses the same driver (`CONFIG_MTD_CFI_AMDSTD`, bank width 2)
- [x] `sysupgrade` (2026-11-08): a firmware partition over kernel, rootfs and rootfs_data; `kernel/openwrt/files/lib/upgrade/platform.sh` checks the EVA header and the size and writes the flash image with `mtd -j`, which puts the saved settings behind the mark. `sysupgrade -T` refused a damaged image. A real sysupgrade on the box kept the Wi-Fi settings and the SSH host key, after `openwrt/uci-defaults-test` learned to skip a box that it set up before: OpenWrt runs uci-defaults again after each sysupgrade, and the first attempt reset the test networks over the restored settings
- [ ] Data rate between two LAN sockets, which needs a second wired device
- [ ] Buttons (WLAN, DECT) through `gpio-keys`
- [x] Read the NAND (512 MB), 2026-11-14 from RAM. AVM's `direct_avm.c` drives it without a NAND controller (`CONFIG_MTD_NAND_DIRECT_AVM_GPIO`): data through the bus at `0x1e000000`, /CE0, CLE, ALE, /WP and ready on GPIOs 16, 18, 19, 20 and 21. That is mainline's `gpio-control-nand`, with two changes in patch 0012. The 8 bit chip sits on the low byte of a 16 bit bus and only answers 16 bit accesses, as AVM's `volatile unsigned short` does: `../tools/fusiv-nand-cmd.c` read the ID `2c dc 90 95 d6` (Micron MT29F4G08ABADA) with them and nothing usable with 8 bit accesses. And without a ready line the driver polls the status in nested operations, which deselect the chip, so the data after them was read with /CE high and every block counted as bad. The ready line itself reads ready too early, because writes to the bus are posted, so the parameter page failed with it; AVM read the NOR flash after each change of the lines for that reason. Linux then found `Micron MT29F4G08ABADAWP`, the chip's own 4 bit ECC (on, as AVM leaves it), 0 bad blocks and no ECC failures. The content is not readable text, so AVM's data there is compressed or encrypted
- [x] Write the NAND, 2026-11-14 from RAM: the driver has /WP, and the whole NAND is a partition `ubi` that UBI attaches at boot (`linux,ubi`); UBI refuses other content, such as AVM's, until `ubiformat` has prepared it. Before that, the whole NAND was copied over SSH (512 MiB in 339 s, no ECC failures); AVM had used only 33 of its 4096 blocks. Two more changes in patch 0012. Writes to the bus and to the GPIOs can overtake each other, so the chip now and then took a command with the wrong control lines: the first `ubiformat` failed to erase 8 blocks and marked them bad, and a freshly written page read back with an uncorrectable ECC error. AVM's driver reads the NOR flash around each change of the lines; the driver's `io-sync-reg` does the same, but only on ARM, so the patch enables it on MIPS too and maps the NOR without requesting it. With it, erasing all 4088 good blocks, `ubiformat` and 128 MB through UBIFS ran without a single failure (write 1.3 MB/s, read 2.4 MB/s, 0 ECC failures). And `nand_soft_waitrdy()` timed out under network load after 58 MB of the copy, because its timeout comes from the chip's times, 1 ms, and it gives up without asking the chip again when interrupts delay the polling; the driver now waits at least 100 ms. The 8 blocks stay marked bad, since the kernel doesn't erase them and they were empty under AVM, so nothing shows whether they really are bad. `ubi-utils` and `nand-utils` are only built for targets with NAND; those of `ath79/nand` (same CPU) work, checked against the release's `sha256sums`, since their signing key differs
- [x] OpenWrt's settings and packages on the NAND (2026-11-14, installed): a UBI volume `rootfs_data`, as on OpenWrt's NAND boards, instead of the 4.9 MB of `rootfs_data` in the NOR flash. fstools needs no change: its UBI driver has a higher priority (20) than its MTD driver (10), so `mount_root` takes the volume when it exists and the NOR partition otherwise, and `/init` already leaves the overlay to `mount_root` when the box starts from flash. sysupgrade did need one: `default_do_upgrade` puts the settings behind the mark in the NOR flash with `mtd -j`, where nothing reads them while the NAND has the overlay, and the old overlay with its packages would stay. With the volume present, `openwrt/files/lib/upgrade/platform.sh` now writes the image without the settings, empties the overlay on the NAND (`upper`, `work`, `.fs_state`) and puts `sysupgrade.tgz` there, from where preinit restores it, as `nand_restore_config` in OpenWrt's `nand.sh` does; ath79 doesn't ship `nand.sh`, and emptying instead of making the volume again needs no `ubi-utils` in the image. The first attempt copied `/overlay/upper` by hand into the new volume: `mount_root` found no `.fs_state` ("overlay filesystem has not been fully initialized yet"), took the volume as new and started with the defaults of the first start, at `192.168.0.250` with the test networks. The NOR overlay was still intact: its settings, copied into the running system, and a sysupgrade with "Keep settings" brought the box back at its address with its networks, with the overlay on the NAND (`/dev/ubi0_0`, 462 MB). Packages installed afterwards survived a restart
- [x] Set up the NAND by itself, 2026-11-14: OpenWrt's NAND boards get UBI and `rootfs_data` with their image, but EVA only writes the NOR flash, so `/init` does it at the start from flash (never from RAM), with the kernel's MTD and UBI ioctls and no `ubi-utils`. It does nothing while UBI is attached, and leaves the NAND alone while an overlay in the NOR flash is in use (`upper` there), whose settings only a sysupgrade carries over, and when a good block starts with `UBI#`: that is UBI that couldn't be attached, which an automatic erase would wipe together with all settings. Otherwise it erases all good blocks, attaches UBI, which formats erased blocks itself, makes `rootfs_data` over the free space and copies `sysupgrade.tgz` from `rootfs_data` in the NOR flash, where `mtd -j` leaves it (in the root directory, despite `mtd`'s help naming `tmp`), into the volume, as `platform.sh` does. Tested on the box: a sysupgrade onto it with the overlay on the NAND left the NAND alone; then, from RAM, AVM's first 16 MB went back onto the NAND and the settings into the NOR flash with `mtd jffs2write`, and the next start from flash erased the NAND in 2 s without a failure, made the volume, moved the settings and came up at its address with its networks
- [ ] Measure the UART pins and connect the console
- [x] Download the proprietary files from AVM's public firmware image, so that a build doesn't need a copy from a box: `kernel/tools/fetch-avm-firmware.sh`. `FRITZ.Box_Fon_WLAN_7390-06.88.image` is a tar, whose `var/tmp/kernel.image` holds the kernel and at `0x15a500` the root file system as AVM's squashfs 3.76, big endian, LZMA. `sasquatch -be` read only its superblock. Freetz's squashfs 3.4 with its patches (`kernel/unsquashfs-avm.nix`) recognized it, but read the fragment table from a wrong place: AVM's metadata (all tables and the fragment index entries) lies 256 bytes behind where the superblock and the index point, while the data blocks lie where the inodes say. With those pointers raised by `0x100` (`kernel/tools/fix-avm-squashfs.c`), the bitstream and both modules came out byte for byte as copied from the box.
- [x] Take the packet processor firmware out of AVM's image on the box, 2026-11-14, as the first step towards images without AVM's files: `fix-avm-squashfs` and `extract-ap-firmware` are C now (`kernel/avm-firmware-tools.nix`), for the build machine and the box, and `kernel/unsquashfs-avm.nix` builds Freetz's `unsquashfs` statically for MIPS as well. On the installed box (110 MB RAM, about 26 MB free), `avm-firmware-install` read the image from stdin over SSH, and the squashfs and both firmware files came out byte for byte as on the build machine and as the files the box runs with: about 10 s for `fix-avm-squashfs`, 1 s for `unsquashfs`. A truncated image and one with a byte appended stopped it before it installed anything. Not tested yet: installing into a box without the files, which needs an image without them
- [x] An image without AVM's LAN firmware, 2026-11-14: `build.sh` builds an OpenWrt image without `AP_FIRMWARE_DIR`, which comes up with a setup network on 2.4 GHz (`ucidef_set_wireless` in `board.d/02_network`, OpenWrt's own way to enable Wi-Fi by default). Tested from RAM with `kernel/test/openwrt-setup-test.sh`: `lan0` failed with "no firmware fusiv-bmu.bin", `OpenWrt-7390-Setup-B68E` came up on channel 1, this machine joined it after 169 s and ran `avm-firmware-install` over SSH there; it installed the files, removed the setup network and restarted the network, and `lan0` loaded both firmwares and joined the bridge 20 s later. The SSH session through the setup network never noticed that the network went away, so the test hung there and didn't check the LAN; `test/openwrt.sh` now sets SSH keepalives. With them, the third run passed: after the install the test image answered at `192.168.0.250` over the LAN, `lan0` was in `br-lan` and only the 5 GHz test network was left. The log also showed "Auth succeeded with blank password for 'root'": no image so far set the root password of `build/root-password`, because `build.sh` replaced only the first `@ROOT_PASSWORD@` of the `passwd` line, so `passwd` got the placeholder as the confirmation and failed into `/dev/null`. `build.sh` now replaces every one, and a failure goes to the kernel log
- [x] The FPGA from OpenWrt instead of `/init`, 2026-11-14: `/etc/init.d/fpga` loads it with `load-fpga` once the overlay is mounted, and `avm-firmware-install --usb` installs `bitfile.bit` from AVM's image as `fpga.bit` and loads it. Tested from RAM with `openwrt-setup-test.sh` and `INSTALL_ARGS=--usb`, with the USB Ethernet adapter in the port: 192 s after the start, `load-fpga` sent 169216 bytes and the done pin went up, and 1 s later the kernel found the adapter's hub and the AX88179 (`eth0`). So USB also comes up when it gets its power long after EHCI and OHCI started. The same run showed root with a password, so the fix of `build.sh` works. Not tested yet: `/etc/init.d/fpga` at a start with `fpga.bit` already there
- [x] An image to pass on, 2026-11-14: `RELEASE_IMAGE=1` in `build.sh`, with OpenWrt's defaults and without the test settings and AVM's files; its squashfs held no `fusiv-*.bin`, `fpga.bit`, test settings or SSH key. The README's "Installing" now uses it
- [ ] Test the README's "Installing" in a row, from a box with AVM's firmware: each part was tested on its own, but not DHCP from the box on the setup network and on `192.168.1.1`, nor the steps in LuCI
- [x] A sysupgrade from an image with AVM's files built in to one without, 2026-11-14: the running image's keep.d decides what is saved, and the installed one predated `keep.d/avm-firmware`, so the file was put there by hand first; `sysupgrade -l` then listed both firmware files, and the box came back at its address with them, over the LAN
- [x] The NAND and lan0 after a power-on, 2026-11-14. All starts before had been restarts, which keep registers that AVM's firmware set long ago. After the first power-on, Linux didn't find the NAND ("second ID read did not match ff,ff against 2c,01"), so `mount_root` took `rootfs_data` in the NOR flash and the box came up with OpenWrt's defaults; and `lan0` sent nothing at all, counted on a cable straight to another machine. `CLK_GATE_CONTROL` (`0x190000cc`) read `0xb5ff` after the power-on instead of `0xffff`: the clock of the buffer manager's processor (bit 9) was off, which neither EVA nor our driver switched on. `eth.c` now switches on the clocks of the buffer manager and of GIGE1, and from RAM after a power-on `lan0` then worked. For the NAND, neither 50 resets before the scan, as AVM's `direct_avm.c` does after a cold start, nor releasing the board's reset line (GPIO 15) before the probe helped, so both were taken out again. But binding the driver again worked, at runtime and right after the failed probe: `/init` now does that while the NAND is missing, and the next start after a power-on found it at the first attempt, 0.7 s later, with the settings on it and `lan0` working
- [x] `lan0` with the release image, 2026-11-14: with the settings on the NAND it stayed silent, also over a cable straight to another machine, and devices on the box's Wi-Fi got no IPv4; its answer at its address right after the first sysupgrade came from the old system, which keeps its network while it writes the flash. Seen through a separate Wi-Fi network on its own bridge (`br-debug`, `192.168.77.1`): preinit opens `lan0` at 10 s, before the overlay with the firmware is mounted ("no firmware fusiv-bmu.bin"), and netifd again at 63 s, with OpenWrt's services running. The buffer manager answers its configuration only once it has 4080 buffers, one per pass of the wait loop, and a pass sleeps a whole tick (4 ms at HZ=250): in preinit, with the firmware in the image, that took 14.6 s, at 63 s it hit the limit of 15 s at 3967 buffers. With 60 s, the release image started `lan0` after 15.2 s, with the settings and firmware from the NAND
- [ ] Find out what overwrites the RAM log: after two test runs of the flash image that ran for 10 minutes, Freetz found no log, after one of 4 minutes it did

## Sources

- [AVM GPL sources for the FRITZ!Box 7390](https://osp.avm.de/fritzbox/fritzbox-7390/)
- [`ath9k_pci_owl_loader.c` in the Linux kernel](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/ath/ath9k/ath9k_pci_owl_loader.c)
- [`eva_ramboot.py` patch (7412)](https://lists.infradead.org/pipermail/lede-dev/2018-March/011499.html)
- [OpenWrt forum: OpenWrt on AVM FritzBox 7390](https://forum.openwrt.org/t/openwrt-on-avm-fritzbox-7390/46306)
- [OpenWrt forum: Compatibility with Fritz!Box 7390](https://forum.openwrt.org/t/compatibility-with-fritz-box-7390/112699/2)
- [ip-phone-forum: flashing the 7390 over the serial interface](https://www.ip-phone-forum.de/threads/fritz-box-7390-neu-flashen-%C3%BCber-serielle-schnittstelle-m%C3%B6glich.301164/)
- [ip-phone-forum: UART console on the 7390/7590](https://www.ip-phone-forum.de/threads/telnet-ssh-zugang-auf-fb-7390-7590-%C3%BCber-uart-konsole-einrichten.295979/)
- [ip-phone-forum: adapter for the serial console](https://www.ip-phone-forum.de/threads/welchen-adapter-f%C3%BCr-zugriff-auf-serielle-konsole.303107/)
- [ip-phone-forum: serial console on a 7390](https://www.ip-phone-forum.de/threads/fritz-box-fon-wlan-7390-tot-dead-bricked-repair-possible.252004/latest)
- [ZebraDem wiki: Serial console](https://wiki.zebradem.com/wiki/index.php?title=Serielle_Konsole) (older models, not the 7390)
