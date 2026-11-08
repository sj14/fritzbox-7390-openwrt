#!/usr/bin/env bash
# Saves the RAM log of the last kernel that EVA started from RAM, after the box is
# back in its installed firmware.
# Usage: read-ramlog.sh <log file>. Needs RAMLOG_TOOL and the variables of
# box.sh, see box-test.sh.
set -u
TEST_DIR=$(cd "$(dirname "$0")" && pwd)
. "$TEST_DIR/box.sh"
: "${RAMLOG_TOOL:?set RAMLOG_TOOL to fusiv-ramlog built for the box}"
box_read_ramlog "$RAMLOG_TOOL" "$1" "$TEST_WORK_DIR"
