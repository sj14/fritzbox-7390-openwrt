// Makes the root file system of AVM's FRITZ!Box 7390 image readable for unsquashfs.
//
// AVM's squashfs (version 3:76, big endian) holds its metadata 256 bytes behind
// the places that its pointers name: the superblock's table pointers and the
// fragment index point 0x100 bytes too low, while the data blocks sit where the
// inodes say. This adds 0x100 to those pointers. Found on 06.88 by comparing the
// pointers with the metadata block headers; Freetz's unsquashfs then read the
// bitstream and modules byte for byte as copied from a box.
//
// Usage: fix-avm-squashfs <kernel.image from the firmware image> <output squashfs>
//
// The squashfs starts at the first big endian magic on a 4 byte boundary, as
// Freetz's find-squashfs finds it. The input is mapped, not read, so that the box
// needs no second copy of it in RAM.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    METADATA_SHIFT = 0x100,
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

// Writes a 64 bit big endian value at offset in the output file.
static int write_be64(int fd, uint64_t offset, uint64_t value) {
    unsigned char bytes[8];
    for (int i = 7; i >= 0; i--) {
        bytes[i] = value & 0xff;
        value >>= 8;
    }
    return pwrite(fd, bytes, sizeof(bytes), offset) == sizeof(bytes) ? 0 : -1;
}

static const unsigned char *map_file(const char *path, size_t *size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return NULL;
    }
    struct stat status;
    if (fstat(fd, &status) != 0) {
        perror(path);
        close(fd);
        return NULL;
    }
    *size = status.st_size;
    void *data = mmap(NULL, *size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) {
        perror(path);
        return NULL;
    }
    return data;
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

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <kernel.image> <output squashfs>\n", argv[0]);
        return 2;
    }
    const char *output_path = argv[2];

    size_t kernel_image_size;
    const unsigned char *kernel_image = map_file(argv[1], &kernel_image_size);
    if (!kernel_image) {
        return 1;
    }
    size_t start = 0;
    while (start + SUPERBLOCK_BYTES <= kernel_image_size &&
           memcmp(kernel_image + start, MAGIC_BIG_ENDIAN, 4) != 0) {
        start += 4;
    }
    if (start + SUPERBLOCK_BYTES > kernel_image_size) {
        fprintf(stderr, "no big endian squashfs in the kernel image\n");
        return 1;
    }
    const unsigned char *image = kernel_image + start;
    size_t image_size = kernel_image_size - start;

    int output = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0 || write_all(output, image, image_size) != 0) {
        perror(output_path);
        return 1;
    }

    uint64_t fragment_table_start = 0;
    for (int index = 0; index < TABLE_POINTER_COUNT; index++) {
        uint64_t offset = TABLE_POINTERS_OFFSET + 8 * index;
        uint64_t value = read_be64(image + offset);
        if (value == 0) {
            continue;
        }
        value += METADATA_SHIFT;
        if (write_be64(output, offset, value) != 0) {
            perror(output_path);
            return 1;
        }
        if (index == FRAGMENT_TABLE_POINTER) {
            fragment_table_start = value;
        }
    }

    uint32_t fragments = read_be32(image + FRAGMENTS_OFFSET);
    uint64_t indexes = ((uint64_t)fragments * FRAGMENT_ENTRY_BYTES + METADATA_BLOCK_BYTES - 1) / METADATA_BLOCK_BYTES;
    if (fragment_table_start + 8 * indexes > image_size) {
        fprintf(stderr, "the fragment index lies outside the squashfs\n");
        return 1;
    }
    for (uint64_t index = 0; index < indexes; index++) {
        uint64_t offset = fragment_table_start + 8 * index;
        if (write_be64(output, offset, read_be64(image + offset) + METADATA_SHIFT) != 0) {
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
