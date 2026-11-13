#!/usr/bin/env bash
# Checks that the kernel has everything built in that the OpenWrt image expects
# as kernel modules. The kernel loads no modules, so the kmod packages that
# OpenWrt packages pull in, for example firewall4, only lie unused in
# /lib/modules. For each of their .ko files this looks up the Kconfig symbol in
# the kernel's Makefiles and requires it to be =y in the kernel's .config.
# Symbols that only switch an option on, without a .ko of their own (such as
# CONFIG_NF_CONNTRACK_EVENTS), are not checked.
# Usage: check-kernel-modules.sh <configured kernel tree> <OpenWrt root directory>
set -euo pipefail

TREE=$1
ROOT=$2
CONFIG=$TREE/.config

missing=0
while read -r module_file; do
    module=$(basename "$module_file" .ko)
    # Lines such as "obj-$(CONFIG_NF_CT_NETLINK) += nf_conntrack_netlink.o".
    symbols=$(grep -rhoE "obj-\\\$\(CONFIG_[A-Z0-9_]+\)[[:space:]]*\+=.*\b$module\.o\b" \
                  --include=Makefile --include=Kbuild "$TREE" \
              | grep -oE "CONFIG_[A-Z0-9_]+" | sort -u || true)
    if [ -z "$symbols" ]; then
        # For example libcrc32c, which newer kernels have merged into their
        # CRC library.
        echo "note: $module.ko has no counterpart in this kernel"
        continue
    fi
    for symbol in $symbols; do
        if ! grep -qx "$symbol=y" "$CONFIG"; then
            echo "missing: $symbol=y for $module.ko" >&2
            missing=1
        fi
    done
done < <(find "$ROOT/lib/modules" -name '*.ko' | sort)

if [ "$missing" -ne 0 ]; then
    echo "Add the missing symbols to linux/arch/mips/configs/fusiv_defconfig." >&2
    exit 1
fi
echo "The kernel has all kernel modules of the image built in."
