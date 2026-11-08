# Development notes

What was found out while porting Linux 6.18 and OpenWrt to the FRITZ!Box 7390, and how the image is tested. How to build it is in `kernel/README.md`. As of 2026-11-08.

## Summary

- **State:** A current kernel with OpenWrt 25.12 boots from RAM on the box and works as an access point at the speed of the AVM firmware, with all four LAN ports, both radios, WPA3 and switchable LEDs. The installed firmware is unchanged.
- **Next step:** install the flash image, see Installing. It already booted from RAM.
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
| NOR flash | works, read only | device tree (`mtd-rom`) |
| NAND (512 MB) | open | AVM's `drivers/mtd/nand/direct_avm.c` |
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
- **Not used:** GPIOs 0 to 15 get no driver, because `/init` drives the FPGA and the switch reset there. The buttons (DECT 29, WLAN 31) are not set up yet. ath9k selects `MAC80211_LEDS` as soon as `LEDS_CLASS` is on, so it adds `ath9k-phy0` and `ath9k-phy1` and drives GPIO 1 of each radio chip (`ATH_LED_PIN_DEF`) as LED. Its wiring on the 7390 is unknown, and nothing visible changed when it was on.

## Milestones

1. **Kernel to userspace (reached):** Linux 6.18 boots from RAM, without a serial cable. A telnet shell worked (`hardware/first-shell-6.18.log`), but was removed again when both Wi-Fi networks got connected to the home network.
2. **Flash and Wi-Fi (mostly reached):** NOR read-only, PCI and `ath9k` work. A laptop reached the internet over WPA2 on 2.4 GHz and 5 GHz. NAND is still open.
3. **USB Ethernet as uplink (reached):** The USB port needs the board's FPGA for power, and `/init` loads it. Wi-Fi clients reach the internet through a bridge to an AX88179 adapter (`hardware/first-bridged-access-point-6.18.log`).
4. **LAN port (reached):** `lan0` works through the packet processors, see Ethernet.
5. **OpenWrt (reached from RAM, 2026-11-08):** OpenWrt 25.12 with LuCI from the ImageBuilder for `ath79`, with this kernel, see `kernel/README.md`. From RAM the box came up as an access point at `192.168.0.250` after about 2 minutes, answered over SSH and LuCI, and a laptop on the 2.4 GHz network downloaded at 70-81 Mbit/s. Problems on the way: `wifi-scripts` and `iw` come only with `kmod-ath9k`, OpenWrt 25.12 switches Wi-Fi networks off in `wifi-iface` instead of `wifi-device`, and without `CONFIG_BRIDGE_VLAN_FILTERING` the kernel refuses netifd's bridge settings, so `br-lan` never appeared.
6. **Installed OpenWrt (flash image works from RAM, 2026-11-08):** see Installing.

The kernel work is in `kernel/`, including how to test it without a console.

### Testing without flashing

An image can be loaded into RAM through EVA via FTP, the same way as OpenWrt's `eva_ramboot.py`. This works on the 7390, even over Wi-Fi from a machine in the same network, and the box returns to Freetz after the test.

The box is unavailable as an access point during tests, so a second, used 7390 as a test device would be helpful.

### Installing (prepared, not done yet)

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

Layout of an installed OpenWrt (`FLASH_IMAGE=1` in `kernel/build.sh`). On 2026-11-08 the complete flash image booted from RAM, came up as an access point with SSH and LuCI, and returned to Freetz:

1. The EVA image of the kernel, with only `/init` in its initramfs, at the start of `mtd1` in a partition of 4 MB (`kernel`), followed by the OpenWrt root file system as squashfs (`rootfs`). The proprietary files (FPGA bitstream, firmware of the packet processors) are only in the squashfs. `/init` mounts the squashfs through `mtdblock` at `/rom`, puts a tmpfs over it with overlayfs (changes at `/overlay`), switches into it, loads the FPGA, sets up the switch and starts OpenWrt's init with `INITRAMFS=1`, so that preinit skips `mount_root`.
2. Linux keeps the flash read only at first (`mtd-rom`), so every start begins with the defaults of the image, and the box can't damage its own flash. OpenWrt's `mount_root` can't set up the overlay here: with the squashfs on a block device it wanted a loop device behind the squashfs ("unable to create loop device"), left `/` read only, and the box came up without network configuration. A writable `rootfs_data` partition with jffs2, and with it saved settings and `sysupgrade`, needs a flash driver that can write (CFI) and comes as a second step.
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

- [ ] Install the flash image
- [ ] Saved settings: a writable `rootfs_data` partition, which needs a CFI flash driver
- [ ] Data rate between two LAN sockets, which needs a second wired device
- [ ] Buttons (WLAN, DECT) through `gpio-keys`
- [ ] NAND (512 MB)
- [ ] Measure the UART pins and connect the console
- [ ] Download the proprietary files from AVM's public firmware image, so that a build doesn't need a copy from a box
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
