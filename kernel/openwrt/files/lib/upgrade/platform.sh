# sysupgrade on the FRITZ!Box 7390. The image is the flash image of build.sh,
# the same file that EVA writes: the kernel partition, the squashfs and the mark
# 0xdeadc0de where rootfs_data begins. It goes to the firmware partition, the
# area where EVA starts the firmware.
#
# Where the saved settings go depends on where OpenWrt keeps its overlay:
# - in rootfs_data in the NOR flash: mtd -j puts them behind the mark as jffs2,
#   from where mount_root restores them, as on other boards.
# - in the UBI volume rootfs_data on the NAND, which mount_root prefers when it
#   exists: the image goes to the NOR flash alone, the overlay on the NAND is
#   emptied and the settings are put there as sysupgrade.tgz, from where
#   preinit restores them. That is what nand_restore_config in OpenWrt's
#   nand.sh does on NAND boards; ath79 doesn't ship nand.sh, and emptying the
#   volume instead of making it again needs no ubi-utils.

PART_NAME=firmware
# The EVA image header, little endian: 0xfeed1281.
EVA_MAGIC=8112edfe
# The firmware partition, 0x20000 to 0xf00000 in flash.
FIRMWARE_AREA_SIZE=$((0xee0000))
NAND_OVERLAY_MOUNT=/tmp/nand-overlay

platform_check_image() {
	local size

	if [ "$(get_magic_long "$1")" != "$EVA_MAGIC" ]; then
		echo "Not a flash image for the FRITZ!Box 7390: it doesn't start with an EVA image."
		return 1
	fi
	size=$(wc -c < "$1")
	if [ "$size" -gt "$FIRMWARE_AREA_SIZE" ]; then
		echo "The image ($size bytes) is larger than the firmware partition."
		return 1
	fi
	return 0
}

# Prints the UBI volume called rootfs_data, such as ubi0_0, if there is one.
nand_overlay_volume() {
	local volume

	for volume in /sys/class/ubi/ubi*_*; do
		if [ "$(cat "$volume/name" 2> /dev/null)" = rootfs_data ]; then
			echo "${volume##*/}"
			return 0
		fi
	done
	return 1
}

# Empties the overlay on the NAND and puts the saved settings there.
# Usage: nand_overlay_reset <volume>
nand_overlay_reset() {
	local volume=$1

	mkdir -p "$NAND_OVERLAY_MOUNT"
	if ! mount -t ubifs "/dev/$volume" "$NAND_OVERLAY_MOUNT"; then
		echo "Cannot mount the overlay volume $volume."
		return 1
	fi
	# mount_root keeps the changes in upper and work, and its state in .fs_state.
	rm -rf -- "$NAND_OVERLAY_MOUNT/upper" "$NAND_OVERLAY_MOUNT/work" "$NAND_OVERLAY_MOUNT/.fs_state"
	if [ -n "$UPGRADE_BACKUP" ]; then
		cp "$UPGRADE_BACKUP" "$NAND_OVERLAY_MOUNT/$BACKUP_FILE"
	fi
	sync
	umount "$NAND_OVERLAY_MOUNT"
}

platform_do_upgrade() {
	local volume

	volume=$(nand_overlay_volume)
	if [ -z "$volume" ]; then
		default_do_upgrade "$1"
		return
	fi

	sync
	get_image "$1" | mtd write - "$PART_NAME" || exit 1
	nand_overlay_reset "$volume" || exit 1
}
