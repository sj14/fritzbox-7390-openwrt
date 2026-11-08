# sysupgrade on the FRITZ!Box 7390. The image is the flash image of build.sh,
# the same file that EVA writes: the kernel partition, the squashfs and the mark
# 0xdeadc0de where rootfs_data begins. It goes to the firmware partition, the
# area where EVA starts the firmware. mtd -j puts the saved settings behind the
# mark as jffs2, from where mount_root restores them, as on other boards.

PART_NAME=firmware
# The EVA image header, little endian: 0xfeed1281.
EVA_MAGIC=8112edfe
# The firmware partition, 0x20000 to 0xf00000 in flash.
FIRMWARE_AREA_SIZE=$((0xee0000))

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

platform_do_upgrade() {
	default_do_upgrade "$1"
}
