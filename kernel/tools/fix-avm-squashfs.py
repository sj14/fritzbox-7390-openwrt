#!/usr/bin/env python3
"""Make the root file system of AVM's FRITZ!Box 7390 image readable for unsquashfs.

AVM's squashfs (version 3:76, big endian) holds its metadata 256 bytes behind
the places that its pointers name: the superblock's table pointers and the
fragment index point 0x100 bytes too low, while the data blocks sit where the
inodes say. This adds 0x100 to those pointers. Found on 06.88 by comparing the
pointers with the metadata block headers; Freetz's unsquashfs then read the
bitstream and modules byte for byte as copied from a box.

Usage: fix-avm-squashfs.py <kernel.image from the firmware image> <output squashfs>

The squashfs starts at the first big endian magic on a 4 byte boundary, as Freetz's
find-squashfs finds it.
"""

import struct
import sys

METADATA_SHIFT = 0x100
MAGIC_BIG_ENDIAN = b"sqsh"
FRAGMENTS_OFFSET = 0x37
# The 64 bit table pointers of a squashfs 3 superblock, in this order.
TABLE_POINTERS_OFFSET = 0x3f
TABLE_POINTERS = (
    "bytes_used",
    "uid_start",
    "guid_start",
    "inode_table_start",
    "directory_table_start",
    "fragment_table_start",
    "lookup_table_start",
)
FRAGMENT_ENTRY_BYTES = 16
METADATA_BLOCK_BYTES = 8192


def find_squashfs(kernel_image: bytes) -> int:
    for offset in range(0, len(kernel_image), 4):
        if kernel_image[offset:offset + 4] == MAGIC_BIG_ENDIAN:
            return offset
    raise SystemExit("no big endian squashfs in the kernel image")


def main() -> None:
    kernel_image_path, output_path = sys.argv[1:3]
    kernel_image = open(kernel_image_path, "rb").read()
    image = bytearray(kernel_image[find_squashfs(kernel_image):])

    pointers = {}
    for index, name in enumerate(TABLE_POINTERS):
        offset = TABLE_POINTERS_OFFSET + 8 * index
        value = struct.unpack_from(">q", image, offset)[0]
        if value:
            value += METADATA_SHIFT
            struct.pack_into(">q", image, offset, value)
        pointers[name] = value

    fragments = struct.unpack_from(">I", image, FRAGMENTS_OFFSET)[0]
    indexes = (fragments * FRAGMENT_ENTRY_BYTES + METADATA_BLOCK_BYTES - 1) // METADATA_BLOCK_BYTES
    for index in range(indexes):
        offset = pointers["fragment_table_start"] + 8 * index
        value = struct.unpack_from(">q", image, offset)[0]
        struct.pack_into(">q", image, offset, value + METADATA_SHIFT)

    with open(output_path, "wb") as output:
        output.write(image)


if __name__ == "__main__":
    main()
