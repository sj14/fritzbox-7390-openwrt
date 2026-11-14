#!/usr/bin/env bash
# Saves the RAM log of the last kernel that EVA started from RAM, once the box
# runs its installed OpenWrt again. Only a kernel started from RAM writes it.
# Usage: read-ramlog.sh <log file>. Needs the variables of openwrt.sh and
# RAMLOG_TOOL, ../tools/fusiv-ramlog.c built for the box.
set -u
TEST_DIR=$(cd "$(dirname "$0")" && pwd)
LOG=$1
. "$TEST_DIR/openwrt.sh"
: "${RAMLOG_TOOL:?set RAMLOG_TOOL to fusiv-ramlog built for the box}"
# fusiv-ramlog dump prints a header line and this many bytes of the log at a time.
PIECE_BYTES=30000

box_ssh 'cat > /tmp/fusiv-ramlog && chmod 755 /tmp/fusiv-ramlog' < "$RAMLOG_TOOL"
: > "$LOG"
offset=0
while true; do
    box_ssh "/tmp/fusiv-ramlog dump $offset" > "$TEST_WORK_DIR/ramlog-piece.txt"
    header=$(head -1 "$TEST_WORK_DIR/ramlog-piece.txt")
    if ! echo "$header" | grep -q "^magic=0x46534c47 "; then
        echo "no RAM log found: $header"
        exit 5
    fi
    length=$(echo "$header" | sed -nE 's/.*length=([0-9]+).*/\1/p')
    sed -n '2,$p' "$TEST_WORK_DIR/ramlog-piece.txt" >> "$LOG"
    offset=$((offset + PIECE_BYTES))
    [ "$offset" -lt "$length" ] || break
done
echo "$header"
