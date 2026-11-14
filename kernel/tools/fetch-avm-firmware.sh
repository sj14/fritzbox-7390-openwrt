#!/usr/bin/env bash
# Takes the proprietary files that an image may carry from AVM's public firmware
# for the FRITZ!Box 7390, with the same script as the box's avm-firmware-install:
# fusiv-bmu.bin and fusiv-eth.bin, the firmware of the packet processors, and
# fpga.bit, the FPGA bitstream. They may not be passed on, so they stay in the
# output directory.
# Usage: fetch-avm-firmware.sh <output directory>. Run it inside "nix-shell".
# Then build with AP_FIRMWARE_DIR=<output> BITFILE=<output>/fpga.bit.
set -euo pipefail

FIRMWARE_URL=https://download.avm.de/fritzbox/fritzbox-7390/deutschland/fritz.os/FRITZ.Box_Fon_WLAN_7390-06.88.image
EXTRACT=$(cd "$(dirname "$0")/.." && pwd)/openwrt/files/usr/libexec/avm-firmware/extract
OUT=$1

mkdir -p "$OUT"
FIRMWARE=$OUT/$(basename "$FIRMWARE_URL")
if [ ! -f "$FIRMWARE" ]; then
    curl -fL -o "$FIRMWARE" "$FIRMWARE_URL"
fi
# extract checks the image's SHA256.
"$EXTRACT" "$OUT" < "$FIRMWARE"

echo "AP_FIRMWARE_DIR=$OUT BITFILE=$OUT/fpga.bit"
