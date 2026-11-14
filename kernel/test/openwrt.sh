# Shell access over SSH to OpenWrt on the box: to the installed OpenWrt at
# BOX_ADDRESS and to a test image at TEST_ADDRESS. Source this file.
#
# BOX_ADDRESS is required, for example 192.168.0.2. TEST_ADDRESS defaults to
# 192.168.0.250, the address that openwrt/uci-defaults-test sets. OPENWRT_KEY,
# the key that both accept (SSH_PUBLIC_KEY in build.sh), is required; it may be
# the public half of a key in the SSH agent. The host key changes with every
# start of a test image, so it is not checked. The keepalives end a session
# within about 15 s when the box restarts or its network goes away.
: "${BOX_ADDRESS:?set BOX_ADDRESS to the address of the installed OpenWrt}"
: "${OPENWRT_KEY:?set OPENWRT_KEY to the SSH key that the box accepts}"
TEST_ADDRESS=${TEST_ADDRESS:-192.168.0.250}

# The scripts write their small work files here and overwrite them in the next
# run, so nothing has to be deleted. build/ stays out of the repository.
TEST_WORK_DIR=${TEST_WORK_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/test-work}
mkdir -p "$TEST_WORK_DIR"

# Runs a command on OpenWrt at an address, like ssh.
# Usage: openwrt_ssh <address> <command>
openwrt_ssh() {
    local address=$1
    shift
    ssh -i "$OPENWRT_KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
        -o ConnectTimeout=5 -o BatchMode=yes -o ServerAliveInterval=5 -o ServerAliveCountMax=3 \
        "root@$address" "$@"
}

# Waits up to the given seconds for SSH at an address.
# Usage: openwrt_wait <address> <seconds>
openwrt_wait() {
    local address=$1
    local seconds=$2
    local start
    start=$(date +%s)
    while [ $(( $(date +%s) - start )) -lt "$seconds" ]; do
        openwrt_ssh "$address" true 2> /dev/null && return 0
        sleep 2
    done
    return 1
}

box_ssh() {
    openwrt_ssh "$BOX_ADDRESS" "$@"
}

box_wait() {
    openwrt_wait "$BOX_ADDRESS" "$@"
}

test_image_ssh() {
    openwrt_ssh "$TEST_ADDRESS" "$@"
}

test_image_wait() {
    openwrt_wait "$TEST_ADDRESS" "$@"
}
