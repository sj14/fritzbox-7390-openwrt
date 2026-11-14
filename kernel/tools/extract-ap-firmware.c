// Writes the packet processor firmware from AVM's kernel modules to a file.
//
// Usage: extract-ap-firmware <module.ko> <prefix> <output>
//
// The firmware belongs to Ikanos and must not be published, so the output stays
// outside the repository, like the FPGA bitstream.
//
// The module is a relocatable big endian ELF32 file. The firmware and its layout
// are variables named <prefix>_<name> in its .data section; a layout variable in
// .bss is zero. Output format, big endian: eleven 32 bit values, the names in
// LAYOUT_FIELDS, followed by the code, data and small data sections. The kernel
// reads it in arch/mips/fusiv/eth.c.
#include <elf.h>
#include <endian.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    LAYOUT_COUNT = 11,
    CODE_LENGTH = 0,
    DATA_LENGTH = 1,
    SDATA_LENGTH = 2,
    NAME_BYTES = 64,
};

static const char *const LAYOUT_FIELDS[LAYOUT_COUNT] = {
    "codeSectionLength", "dataSectionLength", "sDataSectionLength",
    "startData", "endData", "startSData", "endSData",
    "startBSS", "endBSS", "startSBSS", "endSBSS",
};

// The parts of the module that the extraction needs.
struct module {
    const unsigned char *file;
    size_t size;
    Elf32_Shdr symbols;
    Elf32_Shdr names;
    uint16_t data_index;
    Elf32_Shdr data;
};

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

static int in_file(const struct module *module, uint32_t offset, uint32_t size) {
    return offset <= module->size && size <= module->size - offset;
}

// Reads section header index, converted to the host's byte order.
static int read_section(const struct module *module, const Elf32_Ehdr *header, uint16_t index, Elf32_Shdr *section) {
    uint32_t offset = be32toh(header->e_shoff) + (uint32_t)index * sizeof(*section);
    if (index >= be16toh(header->e_shnum) || !in_file(module, offset, sizeof(*section))) {
        return -1;
    }
    memcpy(section, module->file + offset, sizeof(*section));
    section->sh_name = be32toh(section->sh_name);
    section->sh_type = be32toh(section->sh_type);
    section->sh_offset = be32toh(section->sh_offset);
    section->sh_size = be32toh(section->sh_size);
    section->sh_link = be32toh(section->sh_link);
    section->sh_entsize = be32toh(section->sh_entsize);
    if (section->sh_type != SHT_NOBITS && !in_file(module, section->sh_offset, section->sh_size)) {
        return -1;
    }
    return 0;
}

// Returns the string at offset in a string table section, or NULL.
static const char *string_at(const struct module *module, const Elf32_Shdr *table, uint32_t offset) {
    if (offset >= table->sh_size) {
        return NULL;
    }
    const char *string = (const char *)module->file + table->sh_offset + offset;
    if (!memchr(string, '\0', table->sh_size - offset)) {
        return NULL;
    }
    return string;
}

// Finds the symbol table, its names and the .data section.
static int open_module(struct module *module) {
    Elf32_Ehdr header;
    if (module->size < sizeof(header)) {
        return -1;
    }
    memcpy(&header, module->file, sizeof(header));
    if (memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 || header.e_ident[EI_CLASS] != ELFCLASS32 ||
        header.e_ident[EI_DATA] != ELFDATA2MSB || be16toh(header.e_shentsize) != sizeof(Elf32_Shdr)) {
        return -1;
    }

    Elf32_Shdr section_names;
    if (read_section(module, &header, be16toh(header.e_shstrndx), &section_names) != 0) {
        return -1;
    }
    int found_symbols = 0;
    int found_data = 0;
    for (uint16_t index = 0; index < be16toh(header.e_shnum); index++) {
        Elf32_Shdr section;
        if (read_section(module, &header, index, &section) != 0) {
            return -1;
        }
        const char *name = string_at(module, &section_names, section.sh_name);
        if (section.sh_type == SHT_SYMTAB) {
            module->symbols = section;
            if (read_section(module, &header, section.sh_link, &module->names) != 0) {
                return -1;
            }
            found_symbols = 1;
        } else if (name && strcmp(name, ".data") == 0) {
            module->data_index = index;
            module->data = section;
            found_data = 1;
        }
    }
    return found_symbols && found_data && module->symbols.sh_entsize == sizeof(Elf32_Sym) ? 0 : -1;
}

// Finds the symbol <prefix>_<field>, converted to the host's byte order.
static int find_symbol(const struct module *module, const char *prefix, const char *field, Elf32_Sym *symbol) {
    char wanted[NAME_BYTES];
    snprintf(wanted, sizeof(wanted), "%s_%s", prefix, field);
    uint32_t count = module->symbols.sh_size / sizeof(*symbol);
    for (uint32_t index = 0; index < count; index++) {
        memcpy(symbol, module->file + module->symbols.sh_offset + index * sizeof(*symbol), sizeof(*symbol));
        const char *name = string_at(module, &module->names, be32toh(symbol->st_name));
        if (name && strcmp(name, wanted) == 0) {
            symbol->st_value = be32toh(symbol->st_value);
            symbol->st_shndx = be16toh(symbol->st_shndx);
            return 0;
        }
    }
    fprintf(stderr, "no symbol %s\n", wanted);
    return -1;
}

// Returns where <prefix>_<field> starts in the file, if length bytes of it lie
// in .data, or else NULL.
static const unsigned char *data_of(const struct module *module, const Elf32_Sym *symbol, uint32_t length) {
    if (symbol->st_shndx != module->data_index || symbol->st_value > module->data.sh_size ||
        length > module->data.sh_size - symbol->st_value) {
        return NULL;
    }
    return module->file + module->data.sh_offset + symbol->st_value;
}

static int read_layout(const struct module *module, const char *prefix, uint32_t layout[LAYOUT_COUNT]) {
    for (int index = 0; index < LAYOUT_COUNT; index++) {
        Elf32_Sym symbol;
        if (find_symbol(module, prefix, LAYOUT_FIELDS[index], &symbol) != 0) {
            return -1;
        }
        if (symbol.st_shndx == SHN_UNDEF || symbol.st_shndx >= SHN_LORESERVE) {
            fprintf(stderr, "%s_%s is not in a section\n", prefix, LAYOUT_FIELDS[index]);
            return -1;
        }
        // Variables in .bss are zero in the module's image.
        if (symbol.st_shndx != module->data_index) {
            layout[index] = 0;
            continue;
        }
        const unsigned char *value = data_of(module, &symbol, sizeof(uint32_t));
        if (!value) {
            fprintf(stderr, "%s_%s lies outside .data\n", prefix, LAYOUT_FIELDS[index]);
            return -1;
        }
        uint32_t big_endian;
        memcpy(&big_endian, value, sizeof(big_endian));
        layout[index] = be32toh(big_endian);
    }
    return 0;
}

static int write_section(FILE *output, const struct module *module, const char *prefix, const char *name,
                         uint32_t length) {
    Elf32_Sym symbol;
    if (find_symbol(module, prefix, name, &symbol) != 0) {
        return -1;
    }
    const unsigned char *section = data_of(module, &symbol, length);
    if (!section) {
        fprintf(stderr, "%s_%s with %u bytes lies outside .data\n", prefix, name, length);
        return -1;
    }
    return fwrite(section, 1, length, output) == length ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <module.ko> <prefix> <output>\n", argv[0]);
        return 2;
    }
    const char *module_path = argv[1];
    const char *prefix = argv[2];
    const char *output_path = argv[3];

    struct module module = {0};
    module.file = map_file(module_path, &module.size);
    if (!module.file) {
        return 1;
    }
    if (open_module(&module) != 0) {
        fprintf(stderr, "%s: not a big endian ELF32 module with symbols and .data\n", module_path);
        return 1;
    }

    uint32_t layout[LAYOUT_COUNT];
    if (read_layout(&module, prefix, layout) != 0) {
        return 1;
    }
    uint32_t layout_big_endian[LAYOUT_COUNT];
    for (int index = 0; index < LAYOUT_COUNT; index++) {
        layout_big_endian[index] = htobe32(layout[index]);
    }

    FILE *output = fopen(output_path, "wb");
    if (!output) {
        perror(output_path);
        return 1;
    }
    if (fwrite(layout_big_endian, sizeof(layout_big_endian), 1, output) != 1 ||
        write_section(output, &module, prefix, "codeSection", layout[CODE_LENGTH]) != 0 ||
        write_section(output, &module, prefix, "dataSection", layout[DATA_LENGTH]) != 0 ||
        write_section(output, &module, prefix, "sDataSection", layout[SDATA_LENGTH]) != 0 ||
        fclose(output) != 0) {
        fprintf(stderr, "%s: cannot write the firmware\n", output_path);
        return 1;
    }
    return 0;
}
