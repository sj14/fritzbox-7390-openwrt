#!/usr/bin/env bash
# Loads an image into RAM through EVA, lets it run and saves its RAM log after the
# box is back in its installed firmware. Usage: box-test.sh <image> <log file>
#
#   INSTALLED_FIRMWARE  freetz (default) or openwrt, see box.sh, with the
#                       variables of freetz.sh or openwrt.sh
#   BOX_BSSID_PREFIX    start of the box's own BSSIDs, such as "AA:BB:CC:". This
#                       machine must not reach the network through the box's radios.
#   WIFI_DEVICE         this machine's Wi-Fi interface, default wlp5s0
#   RAMLOG_TOOL         ../tools/fusiv-ramlog.c built for the box
#   BOX_WAIT            seconds to wait for the installed firmware, default 600
set -u
TEST_DIR=$(cd "$(dirname "$0")" && pwd)
. "$TEST_DIR/box.sh"
. "$TEST_DIR/eva.sh"
IMAGE=$1
LOG=$2
: "${RAMLOG_TOOL:?set RAMLOG_TOOL to fusiv-ramlog built for the box}"

eva_load "$IMAGE" "$TEST_WORK_DIR" || exit $?

box_wait "${BOX_WAIT:-600}" || { echo "the installed firmware did not come back"; exit 3; }
echo "the installed firmware is back"

box_read_ramlog "$RAMLOG_TOOL" "$LOG" "$TEST_WORK_DIR"
