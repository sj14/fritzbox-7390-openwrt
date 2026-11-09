#!/usr/bin/env bash
# Builds the OpenWrt root file system for the image with OpenWrt's ImageBuilder
# for ath79/generic, whose packages are built for the same CPU (mips_24kc, big
# endian). The release and the ImageBuilder's SHA256 are fixed here; the
# packages come from that release's package feeds.
# Usage: fetch-openwrt-rootfs.sh <output directory>. Run it inside "nix-shell".
# It prints the OPENWRT_ROOTFS to build with and writes the list of packages
# with their versions to <output directory>/packages.manifest.
set -euo pipefail

OPENWRT_VERSION=25.12.6
IMAGEBUILDER_NAME=openwrt-imagebuilder-$OPENWRT_VERSION-ath79-generic.Linux-x86_64
IMAGEBUILDER_URL=https://downloads.openwrt.org/releases/$OPENWRT_VERSION/targets/ath79/generic/$IMAGEBUILDER_NAME.tar.zst
IMAGEBUILDER_SHA256=6f40746139c833336e67b03d5bdb0501d3a80fb789bf7e5b7a9b9984b83a5e23
# The profile doesn't matter, only the root directory of its image is used.
PROFILE=tplink_tl-wdr4300-v1
# wifi-scripts and iw are listed on their own, because they come otherwise with
# kmod-ath9k. The kernel has everything built in, so no kernel modules.
PACKAGES="luci wifi-scripts iw iwinfo -procd-ujail -ppp -ppp-mod-pppoe -kmod-ath9k -kmod-gpio-button-hotplug -swconfig -uboot-envtools -kmod-usb2 -kmod-usb-ledtrig-usbport"

OUT=$1
IMAGEBUILDER_TARBALL=$OUT/$IMAGEBUILDER_NAME.tar.zst
IMAGEBUILDER_DIR=$OUT/$IMAGEBUILDER_NAME
ROOT=$IMAGEBUILDER_DIR/build_dir/target-mips_24kc_musl/root-ath79
IMAGEBUILDER_MANIFEST=$IMAGEBUILDER_DIR/bin/targets/ath79/generic/openwrt-$OPENWRT_VERSION-ath79-generic-$PROFILE.manifest
MANIFEST=$OUT/packages.manifest

mkdir -p "$OUT"
if [ ! -f "$IMAGEBUILDER_TARBALL" ]; then
    curl -fL -o "$IMAGEBUILDER_TARBALL" "$IMAGEBUILDER_URL"
fi
echo "$IMAGEBUILDER_SHA256  $IMAGEBUILDER_TARBALL" | sha256sum -c -

if [ ! -d "$IMAGEBUILDER_DIR" ]; then
    tar --zstd -xf "$IMAGEBUILDER_TARBALL" -C "$OUT"
fi

make -C "$IMAGEBUILDER_DIR" image PROFILE="$PROFILE" PACKAGES="$PACKAGES"

# The packages and their versions, because a later run can get newer ones from
# the package feeds. Compare two runs with diff.
cp "$IMAGEBUILDER_MANIFEST" "$MANIFEST"
echo "Packages: $MANIFEST"

# Each run of this script builds it again, and build.sh copies it.
echo "OPENWRT_ROOTFS=$ROOT"
