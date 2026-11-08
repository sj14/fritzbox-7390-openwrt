#!/usr/bin/env bash
# Loads an OpenWrt test image into RAM through EVA, saves its state over SSH and
# restarts it, so that the box returns to its installed firmware.
# Usage: openwrt-test.sh <image> <SSH private key> <output prefix>
#
# Needs the variables of box-test.sh except RAMLOG_TOOL. The test image comes up
# at OPENWRT_ADDRESS (see openwrt.sh) and accepts the given key.
set -u
TEST_DIR=$(cd "$(dirname "$0")" && pwd)
IMAGE=$1
OPENWRT_KEY=$2
OUT=$3
. "$TEST_DIR/openwrt.sh"
. "$TEST_DIR/box.sh"
. "$TEST_DIR/eva.sh"

eva_load "$IMAGE" "$TEST_WORK_DIR" || exit $?

start=$(date +%s)
until openwrt_is_up; do
    if [ $(( $(date +%s) - start )) -ge 300 ]; then
        echo "no SSH after 300 s, the test image restarts by itself"
        exit 6
    fi
    sleep 5
done
echo "SSH after $(( $(date +%s) - start )) s"
echo "LuCI answers with HTTP $(curl -s -o /dev/null --max-time 10 -w '%{http_code}' "http://$OPENWRT_ADDRESS/cgi-bin/luci")"

# The state report leaves the Wi-Fi keys out.
openwrt_ssh test-diagnostics > "$OUT.state" 2>&1
openwrt_ssh 'dmesg; logread' > "$OUT.log" 2>&1
openwrt_ssh reboot

# An installed OpenWrt answers at the same address, so give the test image time
# to go down first.
sleep 20
box_wait "${BOX_WAIT:-600}" || { echo "the installed firmware did not come back"; exit 3; }
echo "the installed firmware is back"
