# Access to the box through the firmware installed on it, so that the test
# scripts work the same way with both. Source this file.
# INSTALLED_FIRMWARE is freetz (the default, see freetz.sh) or openwrt (see
# openwrt.sh).
TEST_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
INSTALLED_FIRMWARE=${INSTALLED_FIRMWARE:-freetz}

# The scripts write their small work files here and overwrite them in the next
# run, so nothing has to be deleted. build/ stays out of the repository.
TEST_WORK_DIR=${TEST_WORK_DIR:-$(cd "$TEST_DIR/.." && pwd)/build/test-work}
mkdir -p "$TEST_WORK_DIR"

case "$INSTALLED_FIRMWARE" in
    freetz)
        . "$TEST_DIR/freetz.sh"
        # Writable on the box, for tools copied there.
        BOX_TMP=/var/tmp
        BOX_RADIOS_OFF='ifconfig ath0 down; ifconfig ath1 down'
        box_run() { freetz_run "$@"; }
        box_upload() { freetz_upload "$@"; }
        box_is_up() { freetz_is_up; }
        box_wait() { freetz_wait "$@"; }
        ;;
    openwrt)
        . "$TEST_DIR/openwrt.sh"
        BOX_TMP=/tmp
        BOX_RADIOS_OFF='wifi down'
        box_run() { openwrt_run "$@"; }
        box_upload() { openwrt_upload "$@"; }
        box_is_up() { openwrt_is_up; }
        box_wait() { openwrt_wait "$@"; }
        ;;
    *)
        echo "INSTALLED_FIRMWARE must be freetz or openwrt" >&2
        exit 1
        ;;
esac

# Saves the RAM log of the last kernel that EVA started from RAM, read with
# ../tools/fusiv-ramlog.c built for the box. Usage: box_read_ramlog <tool> <log>
# <work directory>, which receives part.sh and part.txt.
box_read_ramlog() {
    local tool=$1
    local log=$2
    local work=$3
    local offset=0
    local header length

    box_upload "$tool" "$BOX_TMP/fusiv-ramlog"
    : > "$log"
    while true; do
        printf 'chmod 755 %s/fusiv-ramlog\n%s/fusiv-ramlog dump %s\n' "$BOX_TMP" "$BOX_TMP" "$offset" > "$work/part.sh"
        box_run "$work/part.sh" > "$work/part.txt"
        header=$(head -1 "$work/part.txt")
        if ! echo "$header" | grep -q "^magic=0x46534c47 "; then
            echo "no RAM log found: $header"
            return 5
        fi
        length=$(echo "$header" | sed -nE 's/.*length=([0-9]+).*/\1/p')
        sed -n '2,$p' "$work/part.txt" >> "$log"
        offset=$((offset + 30000))
        [ "$offset" -lt "$length" ] || break
    done
    echo "$header"
}
