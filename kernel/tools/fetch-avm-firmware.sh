#!/usr/bin/env bash
# Takes the proprietary files that an image needs from AVM's public firmware
# for the FRITZ!Box 7390 instead of a copy from a box: the FPGA bitstream and
# the firmware of the packet processors. They may not be passed on, so they
# stay in the output directory.
# Usage: fetch-avm-firmware.sh <output directory>. Run it inside "nix-shell".
# Then build with BITFILE=<output>/bitfile.bit AP_FIRMWARE_DIR=<output>/ap-firmware.
set -euo pipefail

FIRMWARE_URL=https://download.avm.de/fritzbox/fritzbox-7390/deutschland/fritz.os/FRITZ.Box_Fon_WLAN_7390-06.88.image
FIRMWARE_SHA256=35cf96b6d9417c46bdf227a7b2beb0d9c39df0c9dc6eff6c39c30c9c27819ae5
MODULES=lib/modules/2.6.28.10/kernel/fusiv_src/kernel
BITSTREAM=lib/modules/bitfile.bit
BMU_MODULE=$MODULES/ap_code/lib/fusivlib_lkm.ko
ETH_MODULE=$MODULES/drivers/ethdriver/ethdriver_lkm.ko

OUT=$1
: "${UNSQUASHFS_AVM:?run this inside nix-shell, which provides UNSQUASHFS_AVM}"
: "${AVM_FIRMWARE_TOOLS:?run this inside nix-shell, which provides AVM_FIRMWARE_TOOLS}"

mkdir -p "$OUT/unpacked" "$OUT/ap-firmware"
FIRMWARE=$OUT/$(basename "$FIRMWARE_URL")
if [ ! -f "$FIRMWARE" ]; then
    curl -fL -o "$FIRMWARE" "$FIRMWARE_URL"
fi
echo "$FIRMWARE_SHA256  $FIRMWARE" | sha256sum -c -

# The image is a tar. kernel.image holds the kernel and then the root file system.
tar -xf "$FIRMWARE" -C "$OUT/unpacked" ./var/tmp/kernel.image
"$AVM_FIRMWARE_TOOLS/fix-avm-squashfs" "$OUT/unpacked/var/tmp/kernel.image" "$OUT/rootfs.squashfs"
"$UNSQUASHFS_AVM" -f -d "$OUT/root" "$OUT/rootfs.squashfs" "$BITSTREAM" "$BMU_MODULE" "$ETH_MODULE"

install -m 600 "$OUT/root/$BITSTREAM" "$OUT/bitfile.bit"
"$AVM_FIRMWARE_TOOLS/extract-ap-firmware" "$OUT/root/$BMU_MODULE" BMUAP "$OUT/ap-firmware/fusiv-bmu.bin"
"$AVM_FIRMWARE_TOOLS/extract-ap-firmware" "$OUT/root/$ETH_MODULE" ETHAP "$OUT/ap-firmware/fusiv-eth.bin"
chmod 600 "$OUT/ap-firmware/fusiv-bmu.bin" "$OUT/ap-firmware/fusiv-eth.bin"

echo "BITFILE=$OUT/bitfile.bit AP_FIRMWARE_DIR=$OUT/ap-firmware"
