# Shell access to the box under OpenWrt, over SSH. Source this file.
# OPENWRT_ADDRESS defaults to 192.168.0.250, the address that
# openwrt/uci-defaults-test sets. OPENWRT_KEY, the private key of
# SSH_PUBLIC_KEY in build.sh, is required. The host key changes with every
# start of a test image, so it is not checked.
OPENWRT_ADDRESS=${OPENWRT_ADDRESS:-192.168.0.250}
: "${OPENWRT_KEY:?set OPENWRT_KEY to the SSH key that the OpenWrt image accepts}"

# Runs a command on the box, like ssh.
openwrt_ssh() {
    ssh -i "$OPENWRT_KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
        -o ConnectTimeout=5 -o BatchMode=yes "root@$OPENWRT_ADDRESS" "$@"
}

# Runs a local script file on the box and prints its output.
openwrt_run() {
    openwrt_ssh 'sh -s' < "$1"
}

# Copies a local file to the box.
openwrt_upload() {
    openwrt_ssh "cat > '$2'" < "$1"
}

openwrt_is_up() {
    openwrt_ssh true 2> /dev/null
}

# Waits up to $1 seconds for SSH.
openwrt_wait() {
    local start
    start=$(date +%s)
    while [ $(( $(date +%s) - start )) -lt "$1" ]; do
        openwrt_is_up && return 0
        sleep 2
    done
    return 1
}
