// Makes the root file system of AVM's FRITZ!Box 7390 image readable for unsquashfs.
//
// AVM's squashfs (version 3:76, big endian) holds its metadata 256 bytes behind
// the places that its pointers name: the superblock's table pointers and the
// fragment index point 0x100 bytes too low, while the data blocks sit where the
// inodes say. This adds 0x100 to those pointers. Found on 06.88 by comparing the
// pointers with the metadata block headers; Freetz's unsquashfs then read the
// bitstream and modules byte for byte as copied from a box.
//
// Usage: fix-avm-squashfs <kernel.image from the firmware image, or - for stdin> <output squashfs>
//
// The squashfs starts at the first big endian magic on a 4 byte boundary, as
// Freetz's find-squashfs finds it. The input is read once from start to end, so
// that it can come through a pipe: on the box, kernel.image then never lies in
// RAM next to the squashfs. The pointers are fixed in the output afterwards.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    METADATA_SHIFT = 0x100,
    MAGIC_BYTES = 4,
    FRAGMENTS_OFFSET = 0x37,
    // The 64 bit table pointers of a squashfs 3 superblock, in this order:
    // bytes_used, uid_start, guid_start, inode_table_start,
    // directory_table_start, fragment_table_start, lookup_table_start.
    TABLE_POINTERS_OFFSET = 0x3f,
    TABLE_POINTER_COUNT = 7,
    FRAGMENT_TABLE_POINTER = 5,
    SUPERBLOCK_BYTES = TABLE_POINTERS_OFFSET + 8 * TABLE_POINTER_COUNT,
    FRAGMENT_ENTRY_BYTES = 16,
    METADATA_BLOCK_BYTES = 8192,
    COPY_BUFFER_BYTES = 64 * 1024,
};

static const char MAGIC_BIG_ENDIAN[] = "sqsh";

static uint64_t read_be64(const unsigned char *bytes) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value = value << 8 | bytes[i];
    }
    return value;
}

static uint32_t read_be32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

// Adds METADATA_SHIFT to the 64 bit big endian pointer at offset in the output
// file and returns the new value, or 0 on failure.
static uint64_t shift_pointer(int fd, uint64_t offset) {
    unsigned char bytes[8];
    if (pread(fd, bytes, sizeof(bytes), offset) != sizeof(bytes)) {
        return 0;
    }
    uint64_t value = read_be64(bytes) + METADATA_SHIFT;
    uint64_t remaining = value;
    for (int i = 7; i >= 0; i--) {
        bytes[i] = remaining & 0xff;
        remaining >>= 8;
    }
    if (pwrite(fd, bytes, sizeof(bytes), offset) != sizeof(bytes)) {
        return 0;
    }
    return value;
}

static int write_all(int fd, const unsigned char *data, size_t size) {
    while (size > 0) {
        ssize_t written = write(fd, data, size);
        if (written < 0) {
            return -1;
        }
        data += written;
        size -= written;
    }
    return 0;
}

// Skips the input up to the squashfs magic and copies the rest to the output.
// Returns the size of the squashfs, or 0 if the input has none.
static uint64_t copy_squashfs(FILE *input, int output) {
    unsigned char buffer[COPY_BUFFER_BYTES];
    while (fread(buffer, 1, MAGIC_BYTES, input) == MAGIC_BYTES) {
        if (memcmp(buffer, MAGIC_BIG_ENDIAN, MAGIC_BYTES) != 0) {
            continue;
        }
        uint64_t size = MAGIC_BYTES;
        size_t count = MAGIC_BYTES;
        do {
            if (write_all(output, buffer, count) != 0) {
                return 0;
            }
            count = fread(buffer, 1, sizeof(buffer), input);
            size += count;
        } while (count > 0);
        return ferror(input) ? 0 : size;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <kernel.image, or - for stdin> <output squashfs>\n", argv[0]);
        return 2;
    }
    const char *input_path = argv[1];
    const char *output_path = argv[2];

    FILE *input = strcmp(input_path, "-") == 0 ? stdin : fopen(input_path, "rb");
    if (!input) {
        perror(input_path);
        return 1;
    }
    int output = open(output_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (output < 0) {
        perror(output_path);
        return 1;
    }
    uint64_t image_size = copy_squashfs(input, output);
    if (image_size < SUPERBLOCK_BYTES) {
        fprintf(stderr, "no big endian squashfs in the kernel image\n");
        return 1;
    }

    uint64_t fragment_table_start = 0;
    for (int index = 0; index < TABLE_POINTER_COUNT; index++) {
        uint64_t offset = TABLE_POINTERS_OFFSET + 8 * index;
        unsigned char bytes[8];
        if (pread(output, bytes, sizeof(bytes), offset) != sizeof(bytes)) {
            perror(output_path);
            return 1;
        }
        if (read_be64(bytes) == 0) {
            continue;
        }
        uint64_t value = shift_pointer(output, offset);
        if (value == 0) {
            perror(output_path);
            return 1;
        }
        if (index == FRAGMENT_TABLE_POINTER) {
            fragment_table_start = value;
        }
    }

    unsigned char fragments_bytes[4];
    if (pread(output, fragments_bytes, sizeof(fragments_bytes), FRAGMENTS_OFFSET) != sizeof(fragments_bytes)) {
        perror(output_path);
        return 1;
    }
    uint64_t fragments = read_be32(fragments_bytes);
    uint64_t indexes = (fragments * FRAGMENT_ENTRY_BYTES + METADATA_BLOCK_BYTES - 1) / METADATA_BLOCK_BYTES;
    if (fragment_table_start + 8 * indexes > image_size) {
        fprintf(stderr, "the fragment index lies outside the squashfs\n");
        return 1;
    }
    for (uint64_t index = 0; index < indexes; index++) {
        if (shift_pointer(output, fragment_table_start + 8 * index) == 0) {
            perror(output_path);
            return 1;
        }
    }

    if (close(output) != 0) {
        perror(output_path);
        return 1;
    }
    return 0;
}
