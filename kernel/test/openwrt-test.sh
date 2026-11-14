#!/usr/bin/env bash
# Loads an OpenWrt test image into RAM through EVA, saves its state over SSH and
# restarts it, so that the box returns to its installed OpenWrt.
# Usage: openwrt-test.sh <image> <output prefix>
#
# Needs the variables of openwrt.sh and eva.sh. The test image comes up at
# TEST_ADDRESS and accepts OPENWRT_KEY, as the installed OpenWrt at BOX_ADDRESS.
set -u
TEST_DIR=$(cd "$(dirname "$0")" && pwd)
IMAGE=$1
OUT=$2
. "$TEST_DIR/openwrt.sh"
. "$TEST_DIR/eva.sh"

eva_load "$IMAGE" "$TEST_WORK_DIR" || exit $?

start=$(date +%s)
test_image_wait 300 || { echo "no SSH after 300 s, the test image restarts by itself"; exit 6; }
echo "SSH after $(( $(date +%s) - start )) s"
echo "LuCI answers with HTTP $(curl -s -o /dev/null --max-time 10 -w '%{http_code}' "http://$TEST_ADDRESS/cgi-bin/luci")"

# The state report leaves the Wi-Fi keys out.
test_image_ssh test-diagnostics > "$OUT.state" 2>&1
test_image_ssh 'dmesg; logread' > "$OUT.log" 2>&1
test_image_ssh reboot

box_wait "${BOX_WAIT:-600}" || { echo "the installed OpenWrt did not come back"; exit 3; }
echo "the installed OpenWrt is back"
