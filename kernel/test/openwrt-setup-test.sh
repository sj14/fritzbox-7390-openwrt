#!/usr/bin/env bash
# Tests an OpenWrt image without the LAN firmware from RAM, the way a new box is
# set up: loads it through EVA, joins this machine to the image's setup network
# on 2.4 GHz, installs the firmware there with avm-firmware-install from AVM's
# image, returns to the previous network and checks that the image now answers
# over the LAN. Then it restarts the image, so that the box returns to its
# installed firmware.
# Usage: openwrt-setup-test.sh <image> <SSH key> <AVM firmware image> <output prefix>
#
# Needs the variables of box-test.sh except RAMLOG_TOOL; OPENWRT_ADDRESS is the
# address of the installed OpenWrt. SETUP_SSID is the name of the setup network:
# OpenWrt-7390-Setup- and the last four hex digits of the LAN address in capitals,
# for example OpenWrt-7390-Setup-B68E for 34:31:c4:bb:b6:8e. It is given rather
# than scanned for, because this machine's brcmfmac lists only the access point it
# is connected to. The test image comes up at TEST_ADDRESS (default 192.168.0.250,
# from openwrt/uci-defaults-test) and accepts the given key, which may also be the
# public half of a key in the SSH agent. While it tries the setup network, this
# machine is off the home network, for a few minutes at most.
set -u
TEST_DIR=$(cd "$(dirname "$0")" && pwd)
IMAGE=$1
OPENWRT_KEY=$2
AVM_IMAGE=$3
OUT=$4
TEST_ADDRESS=${TEST_ADDRESS:-192.168.0.250}
: "${SETUP_SSID:?set SETUP_SSID to the name of the setup network, see above}"
# The setup network of openwrt/files/etc/board.d/02_network. The test image
# serves no DHCP, so this machine takes a fixed address there.
SETUP_KEY=fritzbox7390-setup
SETUP_CONNECTION=fritzbox7390-setup-test
SETUP_LOCAL_ADDRESS=192.168.0.199/24
. "$TEST_DIR/openwrt.sh"
. "$TEST_DIR/box.sh"
. "$TEST_DIR/eva.sh"

test_image_ssh() {
    OPENWRT_ADDRESS=$TEST_ADDRESS openwrt_ssh "$@"
}

test_image_wait() {
    OPENWRT_ADDRESS=$TEST_ADDRESS openwrt_wait "$@"
}

# Puts this machine back on the network it was on before the test.
home_connection=$(nmcli -g GENERAL.CONNECTION dev show "$WIFI_DEVICE")
return_home() {
    if [ "$(nmcli -g GENERAL.CONNECTION dev show "$WIFI_DEVICE")" != "$home_connection" ]; then
        nmcli con up "$home_connection" > /dev/null || echo "could not return to $home_connection"
    fi
    nmcli con delete "$SETUP_CONNECTION" > /dev/null 2>&1
}
trap return_home EXIT

eva_load "$IMAGE" "$TEST_WORK_DIR" || exit $?

# OpenWrt needs about 90 s until its radios are up.
sleep 90
if test_image_ssh true 2> /dev/null; then
    echo "the test image answers over the LAN already, so it has the firmware"
    exit 7
fi

# A hidden network is probed for by name, so joining it needs no scan.
nmcli con add type wifi ifname "$WIFI_DEVICE" con-name "$SETUP_CONNECTION" autoconnect no \
    ssid "$SETUP_SSID" 802-11-wireless.hidden yes wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$SETUP_KEY" \
    ipv4.method manual ipv4.addresses "$SETUP_LOCAL_ADDRESS" ipv6.method disabled > /dev/null
start=$(date +%s)
until nmcli --wait 30 con up "$SETUP_CONNECTION" > /dev/null 2>&1; do
    if [ $(( $(date +%s) - start )) -ge 240 ]; then
        echo "could not join $SETUP_SSID, the test image restarts by itself"
        exit 6
    fi
    sleep 10
done
echo "joined $SETUP_SSID after $(( $(date +%s) - start + 90 )) s"
test_image_wait 120 || { echo "no SSH on the setup network"; exit 8; }
echo "SSH on the setup network"
test_image_ssh 'dmesg | grep -i "firmware"' > "$OUT.before" 2>&1

# The network restart at the end takes the setup network down and may end the
# SSH session, so the output tells whether it got there.
test_image_ssh avm-firmware-install < "$AVM_IMAGE" > "$OUT.install" 2>&1
cat "$OUT.install"
return_home

test_image_wait 120 || { echo "the test image doesn't answer over the LAN after the install"; exit 9; }
echo "the test image answers over the LAN"
test_image_ssh 'ip -br link show lan0; ls -l /lib/firmware; uci show wireless | grep ssid' 2>&1 | tee "$OUT.after"
test_image_ssh test-diagnostics > "$OUT.state" 2>&1
test_image_ssh 'dmesg; logread' > "$OUT.log" 2>&1
test_image_ssh reboot

# The installed OpenWrt comes back at its own address.
sleep 20
box_wait "${BOX_WAIT:-600}" || { echo "the installed firmware did not come back"; exit 3; }
echo "the installed firmware is back"
