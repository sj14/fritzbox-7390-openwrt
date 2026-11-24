# Development notes

What was found out while porting Linux 6.18 and OpenWrt 25.12 to the FRITZ!Box 7390, and what to know when changing or testing it. How to build is in `kernel/README.md`, how to install in `README.md`.

OpenWrt runs installed on one box, as an access point with all four LAN sockets, both radios, USB, the settings on the NAND and `sysupgrade`. Official inclusion in OpenWrt is not realistic as it stands, because the Ethernet driver needs the firmware of AVM's packet processors.

## The box

Values read from the box under the AVM firmware unless stated otherwise.

| Property | Value |
|---|---|
| Model | FRITZ!Box Fon WLAN 7390, HWRevision 156, HWSubRevision 3 |
| Firmware | 06.88, the last one |
| SoC | Ikanos Fusiv Vx180, 500 MHz: 25 MHz crystal × 40 (`PROC_PLL_CONTROL` `0x2800`) / 2, as AVM's `ikan_clk.c` computes it; the PLL reads the same under OpenWrt |
| CPU | MIPS 24Kc V4.12, big endian, single core |
| RAM | 128 MB (`memsize=128`), about 110 MB for Linux |
| NOR | 16 MB at `0x1f000000`, Macronix MX29GL (CFI, AMD command set, x16, uniform 128 KB blocks) |
| NAND | 512 MB Micron MT29F4G08ABADA at `0x1e000000`, without a NAND controller |
| PCI | memory window `0x1a000000`, two Atheros AR9280 radios, IRQ 25 |
| Switch | AR8316 according to the forum, five PHYs `0x004d 0xd041` |
| Bootloader | EVA, `bootloaderVersion 1.1939` |
| Console | `console=ttyS0,115200n8r`, UART at `0xb90a0000`, IRQ 29, not connected yet |

The AVM firmware's flash layout, which EVA and the backup use:

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

The starting point was AVM's GPL package 06.86, which has open sources for 2.6.28 for everything except Ethernet.

| Part | Where |
|---|---|
| IRQ, timer, early console, reset | `kernel/linux/arch/mips/fusiv/` (`prom.c`, `irq.c`) |
| PCI | patch 0002, `arch/mips/pci/pci-fusiv.c` |
| NOR flash | `cfi-flash` in the device tree; all partitions read only except `rootfs_data` and the firmware partition for sysupgrade |
| NAND | `gpio-control-nand` in the device tree, patch 0012, rebind in `/init` |
| USB | `usb.c`, power through the FPGA |
| Wi-Fi | `ath9k` with calibration from flash, patches 0003, 0006 to 0008 |
| Ethernet and switch | `eth.c` with AVM's firmware for the packet processors, switch setup in `kernel/initramfs/board.c` |
| GPIOs 16 to 31, LEDs | `gpio.c`, device tree |
| Buttons | open, GPIOs 29 (DECT) and 31 (WLAN) |
| VDSL, telephony, DECT | not planned: closed AVM drivers, not needed for an access point |

## A restart keeps what a power-on resets

Most of the work ran on restarts: from the AVM firmware (with Freetz) into an image in RAM, from one image into the next, through `sysupgrade`. A restart keeps registers that the AVM firmware once set, and that hid two faults until the first power-on with OpenWrt installed:

- `CLK_GATE_CONTROL` (`0x190000cc`) reads `0xb5ff` after a power-on instead of `0xffff`. The clock of the buffer manager's processor (bit 9) is then off, and neither EVA nor Linux switched it on, so `lan0` sent nothing. `eth.c` now switches on the clocks it needs (bits 3, 4, 9 and 10).
- The NAND isn't found at the first probe after a power-on, see NAND.

So test a change after a power-on as well, not only after a restart.

## EVA: loading and flashing

- **From RAM:** `kernel/tools/eva-ramboot.py` loads an image into RAM through EVA's FTP, as OpenWrt's `eva_ramboot.py` does, also over Wi-Fi from a machine in the same network. It passes the uploaded area as `mtdram1=start,end`, and `arch/mips/fusiv/ramboot.c` registers it as `kernel_ram` and `rootfs_ram`, the names the AVM firmware uses, so a flash image can be tested from RAM. `physmap` was tried first, but it rounds its window down to a power of two and cut off `rootfs_ram`.
- **To flash:** `eva-ramboot.py flash` writes the area that EVA starts from (`mtd1` in EVA, `0x9F020000` to `0x9FF00000`, 14.875 MB) with `MEDIA FLSH`, `STOR mtd1` and `REBOOT`, as Freetz's `push_firmware`. The bootloader (`urlader`) and the AVM settings (`tffs`) are never written.
- **EVA starts a kernel from flash only if a squashfs follows it**, as in AVM's images. An image with only padding behind the kernel stayed in EVA, with or without AVM's TI checksum at the end. OpenWrt's dummy squashfs (`eva.dummy.squashfs`, 256 bytes) after the kernel, padded to 64 KB, is enough; `build.sh` adds it.
- **EVA's window** is a few seconds after power-on. A machine on a cable straight to the box must keep its address while the link drops with the box's power, or it misses EVA (NetworkManager drops a profile without autoconnect).
- **Image size:** a 5.9 MB image made EVA stop responding twice early on, while 7.2 MB RAM images loaded fine later. The cause is unknown; it may be the load address near the top of RAM.

This box's AVM firmware was backed up before the first flash, from `/dev/mtd1ro` and `/dev/mtd0ro` under Freetz; `/dev/mtdblock1` is not suitable, because its size is not a multiple of 512 bytes and the block device drops the last 256 bytes. The backup boots from RAM through EVA like a test image (`kernel_ram`, `rootfs_ram`), and it went back to flash the same way.

Rescue paths:

| Problem | Rescue |
|---|---|
| The kernel hangs or panics | the watchdog restarts the box (whether it really resets the chip is unconfirmed), EVA answers at every start |
| Back to the AVM firmware | write the backup with `eva-ramboot.py flash` |
| No network at all | flash a known image through EVA over a cable |
| EVA itself broken | only JTAG, which is why `urlader` is never written |

## Flash layout of the installed image

`kernel/build.sh` with `FLASH_IMAGE=1` fills the EVA area with the partitions of the device tree:

1. `kernel`, 4 MB: the EVA image, whose initramfs holds only `/init`, padded to 64 KB, then the dummy squashfs.
2. `rootfs`, 6 MB: the OpenWrt files as squashfs. `/init` mounts it through `mtdblock` and starts OpenWrt's init; from RAM it puts a tmpfs overlay over it and sets `INITRAMFS=1`, so a test never writes the flash.
3. `rootfs_data`, the rest (4.875 MB, from `0xa20000`), starting with OpenWrt's mark `0xdeadc0de`. `mount_root` erases it and sets up jffs2 there at the first start. Mainline jffs2 refuses the mark ("Cowardly refusing to erase blocks"), so the kernel carries OpenWrt's `532-jffs2_eofdetect.patch` (patch 0011). At that first start `mount_root` logs "failed to sync jffs2 overlay": fstools' second copy "to be in sync" runs after `/tmp/root` was detached, as on other OpenWrt boards, and the settings arrive anyway.

Once the NAND has the UBI volume `rootfs_data`, `mount_root` takes it instead: fstools' UBI driver has a higher priority (20) than its MTD driver (10), as on OpenWrt's NAND boards.

`sysupgrade` writes a partition `firmware` over all three. `kernel/openwrt/files/lib/upgrade/platform.sh` checks the EVA header and the size, then:

- With the overlay in the NOR flash, it writes the image with `mtd -j`, which puts the saved settings behind the mark, from where `mount_root` restores them.
- With the volume on the NAND, `mtd -j` would leave them where nothing reads them. So it writes the image alone, empties the overlay on the NAND (`upper`, `work`, `.fs_state`) and puts `sysupgrade.tgz` there, from where preinit restores it, as `nand_restore_config` in OpenWrt's `nand.sh` does (ath79 doesn't ship it). An overlay copied by hand instead, without `.fs_state`, made `mount_root` take the volume as new ("overlay filesystem has not been fully initialized yet").

The flash image ends with OpenWrt's metadata (`fwtool -I`, from the ImageBuilder), with `avm,fritzbox-7390` in `supported_devices`, the OpenWrt release and `IMAGE_VERSION`, and `platform.sh` sets `REQUIRE_IMAGE_METADATA=1`. Images for other AVM boxes with EVA, such as the 7360, start with an EVA header too and passed the size check before; sysupgrade now refuses them. The metadata come after the mark, as on OpenWrt's own boards (`pad-rootfs | append-metadata`), so `mtd -j` stops at the mark and never writes them, and the 240 bytes that `mtd write` and EVA put behind the mark lie in `rootfs_data`, which `mount_root` erases or doesn't use.

What `sysupgrade` keeps comes from the `keep.d` lists of the running image, not the new one. OpenWrt runs the uci-defaults again after each sysupgrade, so `openwrt/uci-defaults-test` leaves a box alone that it set up before. While `sysupgrade` writes the flash, the old system still answers on the network for a minute or two; an answer then says nothing about the new image.

## NAND

AVM's `direct_avm.c` drives the NAND without a NAND controller: data through the bus at `0x1e000000`, /CE, CLE, ALE, /WP and ready on GPIOs 16, 18, 19, 20 and 21. That is mainline's `gpio-control-nand` with patch 0012:

- **8 bit chip on a 16 bit bus:** it sits on the low byte and only answers 16 bit accesses, as AVM's `volatile unsigned short`. `tools/fusiv-nand-cmd.c` read the ID `2c dc 90 95 d6` that way and nothing usable with 8 bit accesses.
- **No ready line:** it reads ready too early, because writes to the bus are posted, so the driver asks the chip for its status instead. Those status reads are operations of their own that deselect the chip, so the patch selects it again after them; before, every block counted as bad.
- **Bus sync:** writes to the bus and to the GPIOs can overtake each other, so the chip now and then took a command with the wrong control lines (failed erases, ECC errors). AVM's driver reads the NOR flash around each change of the lines; the driver's `io-sync-reg` does the same, which the patch enables on MIPS and maps without requesting it.
- **Waiting:** `nand_soft_waitrdy()` takes its timeout from the chip's times, often 1 ms, and gave up under network load; the driver waits at least 100 ms.

With that, erasing all good blocks, `ubiformat` and UBIFS ran without a failure (write 1.3 MB/s, read 2.4 MB/s), with the chip's own 4 bit ECC, which AVM switches on too. 8 blocks stay marked bad from an early failed `ubiformat`; they were empty under AVM, so nothing shows whether they really are bad. AVM used 33 of the 4096 blocks; the content is not readable text.

**After a power-on the first probe fails** ("second ID read did not match ff,ff against 2c,01"), and a second one right after it works. 50 resets before the scan, as `direct_avm.c` does because the chip "has trouble starting after a cold start", didn't help, nor did releasing GPIO 15 earlier. So `/init` binds the driver again while the NAND is missing, which finds it at the first attempt.

**Setting it up:** EVA only writes the NOR flash, so `/init` makes OpenWrt's UBI volume `rootfs_data` itself at the start from flash, never from RAM, with the kernel's MTD and UBI ioctls (no `ubi-utils` in the image). The whole NAND is one partition `ubi`, which the kernel attaches at boot (`linux,ubi`) once it holds UBI. `/init` does nothing while UBI is attached, and leaves the NAND alone while the overlay in the NOR flash is in use (its settings move along only with a sysupgrade) and when a block starts with `UBI#`, which is UBI that couldn't be attached and that an erase would wipe with all settings. Otherwise it erases the good blocks, attaches UBI, makes the volume over the free space and moves `sysupgrade.tgz` from the NOR overlay into it.

`ubi-utils` and `nand-utils` are only built for targets with NAND; those of `ath79/nand` (same CPU) work, after checking them against the release's `sha256sums`, because their signing key differs.

## Ethernet

`eth.c` loads AVM's firmware into two packet processors (APs) and drives them:

- **Buffer manager** at `0x19210000`: it asks for free buffers one by one with status bit `0x1000`, about 4080 in all, and only then answers its configuration. It asks for the next one within microseconds, but a sleep lasts a whole tick (4 ms at HZ=250): with one buffer per sleep that took 15 s, longer under load, and `lan0` failed at the old limit of 15 s when it was opened while OpenWrt's services ran. The wait loop now waits up to 100 µs for the next request before it sleeps, and the buffers take about 0.1 s; the limit is 60 s.
- **MAC processor** at `0x19260000`: it writes received buffers to four queues in RAM, whose indexes are in shared memory at `0x191f0040`. To send, the driver takes a buffer from `0x19210810`, fills the header, sets the first egress port to `0x19260510` and writes the address there. AVM forwards received packets to a third processor at `0x19100510`, which doesn't run here, so the configuration sends them to the host.
- **Firmware:** `fusiv-bmu.bin` and `fusiv-eth.bin` come from AVM's modules, see `kernel/README.md`. OpenWrt's preinit opens `lan0` before the overlay is mounted, so firmware that lies only in the overlay (an image without AVM's files) fails there and works at netifd's later attempt.
- **Switch:** the board's reset line (GPIO 0 pin 15, active low) holds it in reset until `/init` releases it. `/init` reads and sets it over MDIO at `0x19110080`. MDIO writes looked unreliable when they followed each other quickly (3 of 8 runs received nothing), so `/init` waits 0.5 ms after each command and logs what it reads back.
- **Sockets:** LAN 1 is PHY 0, LAN 2 PHY 1, LAN 3 PHY 2, LAN 4 PHY 3 (found with `tools/fusiv-links.c`); PHY 4 never had a link. All four negotiate 1000 Mbit/s full duplex. Other link speeds are not handled.
- **Address:** the driver starts with a random MAC address; OpenWrt sets the one from `maca` in the bootloader environment (`board.d/02_network`).

### Throughput

The goal was the speed of the AVM firmware: 78.6 to 79.6 Mbit/s for a download over 2.4 GHz, close to the connection's 80 Mbit/s, with AVM's CPU about 90% busy. Linux reaches 80 Mbit/s too (2.4 GHz, 40 MHz) with the CPU about 60% busy. The CPU is the limit, not the radio.

The CPU time per megabyte varies less than a single download: data from the LAN port divided by the softirq time in `/proc/stat`. It is 16.2 MB per second of softirq time, enough for about 130 Mbit/s.

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
| Kernel hardening back on (stack protector, zeroed stack variables, hardened slab freelists) | about 1% more CPU time, same speed |

Without a measurable effect: `-Os` (slower, 50-53 Mbit/s), four OpenWrt patches for MIPS and mac80211 (cache loops, `-mtune=24kc`, `-mno-branch-likely`, airtime quantum, together about 1.5%).

- **Write-through cache:** EVA leaves the cache mode for kernel memory (K0 in CP0 `Config`) at 0, write-through without write-allocate, and Linux keeps the bootloader's setting unless the command line sets `cca=`. `cca=3` selects write-back; all code that the hardware reads uses uncached addresses or writes the cache back first. `memcpy` reached about 60 MB/s under Linux and under AVM, so AVM probably runs with write-through too.
- **No accelerator for Wi-Fi:** AVM's `avm_pa_fusiv.c` shows that from LAN to Wi-Fi the MAC processor still passes each packet to the CPU, and that AVM turned the hardware off from Wi-Fi to LAN. So the processors only help between LAN and WAN.

## Wi-Fi

The radio chips sit directly on the SoC's PCI bus. Their calibration data is in the `urlader` partition:

| Flash offset | Header | PCI device | Radio |
|---|---|---|---|
| `0x980` | version 2, type `WLAN`, length `0x1001` | `0000:00:01.0` | 5 GHz |
| `0x1985` | version 2, type `WLAN2`, length `0x1001` | `0000:00:02.0` | 2.4 GHz |

Each header is followed by one padding byte, then 4096 bytes of byte-swapped data from `0x985` and `0x198a`, with magic `0xa55a`, EEPROM version `0xe010`, 2x2 masks and an XOR checksum of `0xffff`. The register list sets the PCI ID to `168c:0029`; the chips get it in the first bus scan, so `ath9k_pci_owl_loader` is not used, and `ath9k` reads the blocks through `nvmem`. The MAC addresses in them match `macwlan` and `macwlan2` in the environment. The partition also holds secrets such as `wlan_key` and `tr069_passphrase`, so a dump of it must not be published.

The PCI bridge has two traps, both handled in patch 0003, see `kernel/README.md`. WPA3 works with `wpad-basic-mbedtls` (`sae-mixed`); the management frame protection it needs uses the kernel's AES-CMAC.

## LEDs and the FPGA

- **LEDs:** GPIOs 22 to 28, active low, as in AVM's hw156 configuration: info green 22, info red 23, power green 24, power red 25, Festnetz 26, WLAN 27, Internet 28. Power shows OpenWrt's boot state (`led-boot`, `led-running`, `led-failsafe`, `led-upgrade`), WLAN is on while the 2.4 GHz network is up (`board.d/01_leds`).
- **GPIOs 0 to 15** have no driver: `/init` and `load-fpga` drive the switch reset and the FPGA there through `/dev/mem`. GPIO 6 is the DECT chip's reset (active low). Setting it low once reset the box, early in the port and without a note whether the FPGA was loaded; with the FPGA loaded, holding it low for seconds and Piglet's 1 ms pulse left the box running. Leave it an input all the same; AVM drives it high.
- **ath9k's own LEDs:** ath9k selects `MAC80211_LEDS` with `LEDS_CLASS`, adds `ath9k-phy0` and `ath9k-phy1` and drives GPIO 1 of each radio. Their wiring is unknown; nothing visible changed.
- **Red LED inside the box:** it lights while the FPGA runs AVM's bitstream, and nothing found so far turns it off while USB runs, so an image without USB leaves the bitstream out. Erasing the FPGA (PROG low) turns it off at once, and USB's power with it. See below for what was tried.
- **USB power** needs the FPGA, see `kernel/README.md`. USB also comes up when the FPGA is loaded long after EHCI and OHCI started.

### The red LED, the DSP and the PCM bus

AVM never switches the LED on purpose: `SetStatusLED_FPGA` in the telephony driver `isdn_fbox_fon5.ko` is an empty function. It goes off under AVM as a side effect of starting the telephony, which these modules do (all closed, in AVM's image):

- **`Piglet_noemif.ko`** loads the bitstream over DATA 12, DONE 8, CLK 11 and PROG 14 and reaches the FPGA in no other way ("noemif"). Its profile for HWRevision 152 and 156 also names GPIO 6 (DECT reset, and the TE detection input of other models) and the DECT UART at `0x19020000` (UART1). After the load it boots the DECT chip (Dialog SC14488) through that UART; it touches no other GPIO. Its `piglet_reset` pulses PROG and then leaves it an input, which let PROG sink low and erased the FPGA (DONE low): the LED went off, and so did USB's power. So PROG has no pull-up and `piglet_reset` unloads the FPGA. On the 7390 there is one bitstream; the POTS and TE (ISDN) bitstreams in Piglet's code are for other models. Before reloading the bitstream, Piglet switches the PCM bus off (`pcmlink_bus_off`) and on again afterwards.
- **`pcmlink.ko`** reaches the FPGA's registers only through DSP 0, an ADSP-218x core in the Fusiv. It releases the DSP from reset (`0x19050300`, bits 9 and 7; DSP 1 has bits 25 and 23), checks the IDMA port at `0x191c0000` (address at `+0`, program memory words at `+4`, data memory at `+8`, data memory from `0x4000`) by writing and reading back `0xaabb00dd`, and loads `adsp0_firmware.bin`: sections of a big endian header {magic, address, count}, `0xdddddddf` for data memory (16 bit words), `0xeeeeeeef` for program memory, `0xbbbbbbbb` at the end. Each 24 bit program word is stored as `(w >> 16) << 8 | (w & 0xff)` of a 32 bit word, and program word 0 is written last, which starts the DSP. Interrupts from DSP 0 (IRQ 17) are acknowledged with 1 at `0x19050634`.
- **The firmware's shared memory** starts with a header at data address 0 (magic `0xabcd`). Word `0x0a` points to a mirror of the FPGA registers (`0x800`, two words per 32 bit register at `0x800 + reg/2`), word `0x0e` to a ring of register writes (`0x19d`: write index, read index, size 64, then entries {reg, value >> 16, value & 0xffff}). `pcmlink_ul_writeregister(reg, value, mask)` merges the value into the mirror's old one. `ADSPLink_Poll` first writes `0x540` to register `0x48` and waits to read it back; `Reset_FPGA` in the telephony driver then writes registers `0x40`, `0x48`, `0x64` and `0x3c`.
- **The DSP code** (disassembled, see below) handles the ring only in the interrupt of its serial port SPORT0, and receives the mirror there. SPORT0 runs in multichannel mode with an external bit clock (`0x800F` in `0x3FF6`); the timer interrupt counts while SPORT0 stands still (data word `0x11`).

Tried on the box with the FPGA loaded, the LED stayed on in each case:

| Step | Result |
|---|---|
| GPIO 6 high, Piglet's reset pulse (1 ms low), held low (`tools/fusiv-gpio.c`) | no change, the box kept running |
| GPIO 5 (`gpio_avm_piglet_irq` in AVM's table, an output there) high and low | no change |
| DSP 0 started with AVM's firmware (`tools/fusiv-adsp.c`) | the DSP runs, but SPORT0 never gets a clock: its interrupt counters (`0xE28`, `0xE29`) stay 0, the ring isn't read, the mirror stays 0 |
| Interrupt acknowledged as `pcmlink`'s handler does, GPIOs 5 and 6 and `MODE_CONTROL` as under AVM | still no SPORT0 clock |

Neither `pcmlink` nor AVM's kernel set up a PCM clock, so it comes from outside the Fusiv, most likely from the DECT chip as the bus master, routed through the FPGA. Booting the DECT chip is the remaining step, and it was not done: AVM's DECT firmware makes the box a DECT base station, Piglet patches calibration data and the base's identity (RFPI) from AVM's configuration into it, and nothing here would control the radio. Piglet's loader is the UART boot of Dialog's SC144xx: GPIO 6 low, UART1 at 9600 baud 8N1, GPIO 6 high; the chip sends STX (`0x02`), the host answers SOH (`0x01`) and the length, the chip ACK (`0x06`), then the data follow and the chip's XOR checksum must match before the host's ACK. The first stage (`dectfw_firstlevel_488.hex`) is a small loader, the second (`dectfw_secondlevel_488.hex`) goes the same way at 230400 baud with a 4 byte length. Even with a PCM clock, which FPGA register switches the LED is unknown.

The project [FRITZ.Box_7530-08.25-kernel-4.4.271-telephony-re](https://github.com/Rockstein2/FRITZ.Box_7530-08.25-kernel-4.4.271-telephony-re/tree/feature/fon-standalone-driver) takes apart the telephony modules of the 7530 (FRITZ!OS 08.25, ARM). Its hardware side (Qualcomm TDM, SC14448) doesn't apply here, but its `notes/piglet.md` describes the same `dect_loader144xx` that our MIPS Piglet contains: the image format of `ihex2bin` (blocks of a big endian {address, length} after the total length, an XOR byte at the end, which in AVM's real images doesn't match a plain XOR), the tags `patch_dectfw` writes (1 the RFPI from the MAC `maca`, 7 the `dectmode`, 9 a version), the loader's retries and timeouts, and `Check_TDMInterface`, which measures FSC and DCL. Not checked against our module yet. The repository has no license, so take findings from it, not code.

## Registers compared with the AVM firmware

`tools/fusiv-regdump.c` read the system and GPIO registers under AVM's firmware (`hardware/registers-hw156.txt`) and under OpenWrt (2026-11-21, FPGA loaded). AVM's kernel sets its pins in `init_gpio_config()` (`ikan_gpio.c`) from the table in `avm_hw_config_hw156.h`. The differences:

| Register | AVM | OpenWrt | Meaning |
|---|---|---|---|
| `PROC_PLL_CONTROL`, `SYSTEM_PLL_CONTROL`, `DSP_PLL_CONTROL` | `0x2800`, `0x2800`, `0x1c00` | same | same clocks: the CPU runs at 500 MHz, which `prom.c` assumes as AVM's `plat_time.c` does |
| `CLK_GATE_CONTROL` | `0xffff` | `0xb7ff` | bits 11 and 14 (WAP and SAP APU clocks) stay off; the Ethernet clocks that `eth.c` needs are on |
| `MODE_CONTROL` | `0x00081c10` | `0x00081c20` | AVM clears `UART2_EN` (bit 5), which puts the console UART2 on GPIOs 3 and 4 as well, and sets bit 4 for the DECT UART there; EVA leaves bit 5 set, so the console also reaches the DECT chip's UART pins. AVM's console is the same UART2 (`console=ttyS1,115200n8r`), so it has pins of its own. Ikanos' `vx180.h` calls bit 4 `NTR_CLK_ENABLE` (NTR clock on GPIO 7), AVM's table uses it for the DECT UART |
| `SYSTEM_CONTROL` | `0xe6000000` | `0xe0000000` | AVM puts SPI chip selects on GPIOs 1 and 2 (`gpio_avm_spi_flash_cs`, `gpio_avm_spi_dect_cs`), for the DECT chip; nothing here uses SPI |
| `PAD_PIO_MISC_CTRL` | `0x2` | `0x3` | bit 0 is the VDSL PHY's CPU release, unused here |
| `GPIO0_DIR` | `0xd861` | `0xd800` | GPIO 0 (`spi_flash_wp`), 5 (`piglet_irq`, an output although its name says interrupt) and 6 (DECT reset) are outputs under AVM, all three high; here they are inputs and read high |
| `GPIO1_DIR` | `0x1fdf` | `0x1fdd` | GPIO 17 (`nand_cs2`) is an output under AVM; the 7390's NAND has one chip, on /CE GPIO 16 |
| `GPIO0/1_IRQ_ENABLE`, `POLAR`, `SENSITIVITY` | GPIOs 7, 13, 21, 29, 31 | none | AVM's interrupts: NAND ready (7 and 21 in AVM's table), the DECT (29) and WLAN (31) buttons, and 13, which the table doesn't name |

None of them explains the red LED. The `MODE_CONTROL` difference is the only one with an effect on OpenWrt: clearing bit 5 as AVM does would keep the console off the DECT chip's pins, but it waits for a connected console to check that the console still works.

## AVM's firmware files

`FRITZ.Box_Fon_WLAN_7390-06.88.image` is a tar, whose `var/tmp/kernel.image` holds the kernel and at `0x15a500` the root file system: AVM's squashfs 3.76, big endian, LZMA. `sasquatch -be` read only its superblock. Freetz's squashfs 3.4 with its patches (`kernel/unsquashfs-avm.nix`) recognizes the format, but AVM's metadata (all tables and the fragment index entries) lies 256 bytes behind where the superblock and the index point, while the data blocks lie where the inodes say; `kernel/tools/fix-avm-squashfs.c` raises those pointers by `0x100`. The files then come out byte for byte as copied from a box.

The same tools run on the box for `avm-firmware-install`. It streams the image, because with about 26 MB of RAM free under OpenWrt the image, `kernel.image` and the squashfs don't fit into `/tmp` together.

Reading AVM's closed modules and firmware:

- **MIPS modules** (`.ko`): `objdump -dr` from `kernel/shell.nix`'s cross binutils for single functions; Ghidra 12 from nixpkgs decompiles a whole module to C headless (`ghidra-analyzeHeadless <project dir> <name> -import <module.ko> -postScript <script> -deleteProject`, with a small script that prints `DecompInterface.decompileFunction()` for each function). Its addresses are the `.text` offsets plus `0x10060`.
- **DSP firmware** (`adsp0_firmware.bin`): MAME's `unidasm -arch adsp21xx` from `mame-tools`, which loads SDL3 at start (`LD_LIBRARY_PATH` with nixpkgs' `sdl3.lib`). It takes the 24 bit program words as 32 bit little endian words, converted from AVM's layout as described above. The interrupt vectors are 4 words apart: reset `0x00`, SPORT0 transmit `0x10`, SPORT0 receive `0x14`, timer `0x28`.
- **AVM's GPL kernel** (`GPL-release_kernel.tar.gz` in the source package) has the Fusiv headers with the register bits (`vx180.h`), the clock code (`ikan_clk.c`) and the pin table (`avm_hw_config_hw156.h`).

## Testing

- `kernel/test/` has scripts for runs from another machine while the box runs its installed OpenWrt, see `kernel/README.md`. They take the box's radios down before restarting it into EVA, so that a machine on the box's Wi-Fi moves to another access point.
- An SSH session doesn't notice when the box's network goes away; the scripts set keepalives.
- Where the box's LAN doesn't work, a separate Wi-Fi network in a network and bridge of its own reaches it (`br-debug` with its own address). After moving a Wi-Fi interface to another network, run `wifi down` and `wifi up`: a reload left it in `br-lan`, where `arp_ignore=1` dropped ARP for the new address.
- The test machine's `brcmfmac` lists only the access point it is connected to and couldn't join the box's WPA2/WPA3 network; WPA2 networks of the box joined as hidden networks work.
- `/init` restarts a test image after `TEST_DURATION_S` (600 s for OpenWrt images), so a box that can't be reached comes back by itself. An installed image needs `TEST_DURATION_S=0`.

## Serial console

There is **no confirmed pinout** and no verified photo for the 7390 yet, so the pins have to be measured.

**Where to look:** according to a 7390 owner, four solder pads in a row, one of them rectangular. On older FRITZ!Boxes they sit at the board edge with the LEDs, not confirmed for the 7390. The 14-pin connector on the underside is JTAG.

**Identifying the pins** with a multimeter:

| Pin | Measurement | Result |
|---|---|---|
| GND | power unplugged, continuity to the metal shell of LAN or USB | beeps |
| 3.3 V | box on, voltage against GND | constant at about 3.3 V, **do not connect** |
| Box TX | voltage against GND during boot | visibly fluctuates |
| Box RX | the remaining pin | only needed later |

If you measure much more than 3.3 V anywhere, don't connect anything.

**Connecting a Raspberry Pi** (3.3 V GPIOs; never its 5 V pins). Start with two wires, so nothing can be sent to the box; both devices off while connecting, each with its own power supply:

| Box | Pi (physical pin) |
|---|---|
| GND | pin 6 |
| TX | pin 10 (GPIO15, RX) |
| RX, only later | pin 8 (GPIO14, TX) |
| 3.3 V | **do not connect** |

Pins can be soldered in; the forum also mentions pogo pins.

**Reading the output:** in `sudo raspi-config`, under "Interface Options", "Serial Port", disable the login shell over serial and enable the hardware, reboot, then run `picocom -b 115200 /dev/serial0` and turn on the box. Garbled characters right after power-on may come from the bootloader, since `modetty0` specifies 38400 baud.

Without a console, the kernel's RAM log, the reboot status and the LEDs show how far a start got, see `kernel/README.md`.

## Open items

- [ ] Test the README's "Installing" in a row, from a box with AVM's firmware: each part was tested on its own, but not DHCP from the box on the setup network and on `192.168.1.1`, nor the steps in LuCI
- [ ] `/etc/init.d/fpga` at a start with `fpga.bit` already in the overlay
- [ ] Data rate between two LAN sockets, which needs a second wired device
- [ ] Buttons (WLAN, DECT) through `gpio-keys`
- [ ] Measure the UART pins and connect the console
- [ ] With the console connected: clear `UART2_EN` in `MODE_CONTROL` as AVM does and check that the console still works
- [ ] Why the NAND's first probe fails after a power-on

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
