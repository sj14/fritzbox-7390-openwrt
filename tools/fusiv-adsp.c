// Loads AVM's firmware into DSP 0 (an ADSP-218x core) of the Fusiv and starts
// it, as AVM's pcmlink.ko does, to look at the DSP from OpenWrt. See "The red
// LED, the DSP and the PCM bus" in DEVELOPMENT.md.
//
// Usage: fusiv-adsp <adsp0_firmware.bin>       load and start DSP 0
//        fusiv-adsp --peek <address> <count>   print data memory words
//        fusiv-adsp --write <reg> <value>      queue an FPGA register write,
//                                              as pcmlink's ADSPLink_WriteReg
//        fusiv-adsp --ack <count>              acknowledge DSP 0's interrupt
//                                              once a millisecond, count times
//        fusiv-adsp --stop                     hold DSP 0 in reset again
//
// Firmware format (big endian): sections of an 8 byte header {u32 magic,
// u16 address, u16 count} and their words: 0xdddddddf data memory (16 bit
// words), 0xeeeeeeef program memory (32 bit words as the IDMA port takes
// them), 0xbbbbbbbb the end. Program word 0 is written last: writing it
// starts the DSP.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum {
    PAGE_SIZE_BYTES = 4096,

    // DSP control and the interrupt acknowledge, as in pcmlink.ko.
    CONTROL_PAGE = 0x19050000,
    CONTROL_OFFSET = 0x300,
    INTERRUPT_ACK_OFFSET = 0x634,
    DSP0_RESET = 1 << 9,
    DSP0_POWER_DOWN = 1 << 7,
    DSP0_ACK = 1,
    RESET_WAIT_US = 10000,

    // IDMA port of DSP 0. Data memory addresses start at 0x4000.
    IDMA_PAGE = 0x191c0000,
    IDMA_ADDRESS_OFFSET = 0x0,
    IDMA_PROGRAM_OFFSET = 0x4,
    IDMA_DATA_OFFSET = 0x8,
    DATA_MEMORY = 0x4000,
    DATA_WORDS = 15872,
    PROGRAM_WORDS = 14336,
    TEST_WORD = 0xaabb00dd,
    TEST_MASK = 0xffff00ff,

    MAGIC_DATA = 0xdddddddf,
    MAGIC_PROGRAM = 0xeeeeeeef,
    MAGIC_END = 0xbbbbbbbb,
    SECTION_HEADER_BYTES = 8,
    MAX_FIRMWARE_BYTES = 64 * 1024,

    // Header words of the firmware's shared memory, as ADSPLink_Init reads them.
    HEADER_QUEUE_READY = 0x04,
    HEADER_QUEUE = 0x0e,
    QUEUE_HEADER_WORDS = 3,
    QUEUE_ENTRY_WORDS = 3,
    QUEUE_WAIT_US = 100000,

    PEEK_WORDS_AFTER_START = 0x40,
    START_WAIT_US = 100000,
    ACK_INTERVAL_US = 1000,
};

static volatile uint32_t *control;
static volatile uint32_t *interrupt_ack;
static volatile uint32_t *idma_address;
static volatile uint32_t *program_data;
static volatile uint16_t *data_data;

static void usage(void) {
    fprintf(stderr, "Usage: fusiv-adsp <firmware> | --peek <address> <count> | --write <reg> <value> |"
                    " --ack <count> | --stop\n");
}

static uint32_t read_be32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

static uint16_t read_be16(const unsigned char *bytes) {
    return (uint16_t)(bytes[0] << 8 | bytes[1]);
}

static void hold_in_reset(int hold) {
    uint32_t value = *control;
    if (hold) {
        value |= DSP0_RESET | DSP0_POWER_DOWN;
    } else {
        value &= ~(uint32_t)(DSP0_RESET | DSP0_POWER_DOWN);
    }
    *control = value;
    usleep(RESET_WAIT_US);
}

static int dsp_released(void) {
    return (*control & DSP0_RESET) == 0;
}

static uint16_t read_data_word(unsigned int address) {
    *idma_address = DATA_MEMORY + address;
    return *data_data;
}

static void write_data_word(unsigned int address, uint16_t value) {
    *idma_address = DATA_MEMORY + address;
    *data_data = value;
}

// Prints count data memory words from address, 16 to a line. The IDMA port
// counts the address up by itself after each word.
static void peek(unsigned int address, unsigned int count) {
    *idma_address = DATA_MEMORY + address;
    for (unsigned int i = 0; i < count; i++) {
        if (i % 16 == 0) {
            printf("%04x:", address + i);
        }
        printf(" %04x", *data_data);
        if (i % 16 == 15 || i + 1 == count) {
            printf("\n");
        }
    }
}

// Adds one entry {reg, value >> 16, value & 0xffff} to the firmware's ring of
// register writes, whose header is {write index, read index, size}, and shows
// whether the DSP took it.
static int queue_write(unsigned int reg, uint32_t value) {
    if (read_data_word(HEADER_QUEUE_READY) == 0) {
        fprintf(stderr, "the firmware has no register queue\n");
        return 1;
    }
    unsigned int queue = read_data_word(HEADER_QUEUE);
    unsigned int write_index = read_data_word(queue);
    unsigned int read_index = read_data_word(queue + 1);
    unsigned int size = read_data_word(queue + 2);
    printf("queue at 0x%04x: write %u, read %u, size %u\n", queue, write_index, read_index, size);

    unsigned int next = write_index + 1;
    if (next >= size) {
        next = 0;
    }
    if (next == read_index) {
        fprintf(stderr, "queue full\n");
        return 1;
    }
    unsigned int entry = queue + QUEUE_HEADER_WORDS + write_index * QUEUE_ENTRY_WORDS;
    write_data_word(entry, (uint16_t)reg);
    write_data_word(entry + 1, (uint16_t)(value >> 16));
    write_data_word(entry + 2, (uint16_t)value);
    write_data_word(queue, (uint16_t)next);

    usleep(QUEUE_WAIT_US);
    printf("100 ms later: write %u, read %u\n", read_data_word(queue), read_data_word(queue + 1));
    return 0;
}

static int load_section(const unsigned char *firmware, size_t size, size_t *position, uint32_t *first_program_word,
                        int *have_first_word) {
    uint32_t magic = read_be32(firmware + *position);
    unsigned int address = read_be16(firmware + *position + 4);
    unsigned int count = read_be16(firmware + *position + 6);
    *position += SECTION_HEADER_BYTES;

    if (magic == MAGIC_DATA) {
        if (address + count >= DATA_WORDS || *position + 2 * count > size) {
            fprintf(stderr, "data section 0x%x+%u out of range\n", address, count);
            return -1;
        }
        *idma_address = DATA_MEMORY + address;
        for (unsigned int i = 0; i < count; i++) {
            *data_data = read_be16(firmware + *position);
            *position += 2;
        }
        printf("data memory 0x%04x: %u words\n", address, count);
        return 0;
    }

    if (magic == MAGIC_PROGRAM) {
        if (address + count >= PROGRAM_WORDS || *position + 4 * count > size) {
            fprintf(stderr, "program section 0x%x+%u out of range\n", address, count);
            return -1;
        }
        printf("program memory 0x%04x: %u words\n", address, count);
        if (address == 0 && count > 0) {
            *first_program_word = read_be32(firmware + *position);
            *have_first_word = 1;
            *position += 4;
            address = 1;
            count--;
        }
        *idma_address = address;
        for (unsigned int i = 0; i < count; i++) {
            *program_data = read_be32(firmware + *position);
            *position += 4;
        }
        return 0;
    }

    if (magic == MAGIC_END) {
        return 1;
    }
    fprintf(stderr, "unknown section 0x%08x at %zu\n", magic, *position - SECTION_HEADER_BYTES);
    return -1;
}

static int load(const char *path) {
    static unsigned char firmware[MAX_FIRMWARE_BYTES];
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return 1;
    }
    size_t size = fread(firmware, 1, sizeof(firmware), file);
    fclose(file);

    printf("control 0x%08x before reset\n", *control);
    hold_in_reset(1);
    hold_in_reset(0);
    printf("control 0x%08x after release\n", *control);

    *idma_address = 1;
    *program_data = TEST_WORD;
    *idma_address = 1;
    uint32_t read_back = *program_data;
    printf("IDMA test: wrote 0x%08x, read 0x%08x\n", (unsigned int)TEST_WORD, read_back);
    if ((read_back & TEST_MASK) != TEST_WORD) {
        fprintf(stderr, "IDMA test failed, DSP 0 left in reset\n");
        hold_in_reset(1);
        return 1;
    }

    *idma_address = DATA_MEMORY;
    for (unsigned int i = 0; i < DATA_WORDS - 1; i++) {
        *data_data = 0;
    }

    uint32_t first_program_word = 0;
    int have_first_word = 0;
    size_t position = 0;
    int result = 0;
    while (result == 0 && position + SECTION_HEADER_BYTES <= size) {
        result = load_section(firmware, size, &position, &first_program_word, &have_first_word);
    }
    if (result < 0) {
        hold_in_reset(1);
        return 1;
    }
    if (!have_first_word) {
        fprintf(stderr, "no program word 0, DSP 0 not started\n");
        hold_in_reset(1);
        return 1;
    }
    *idma_address = 0;
    *program_data = first_program_word;
    printf("started DSP 0 with program word 0x%08x\n", first_program_word);

    usleep(START_WAIT_US);
    peek(0, PEEK_WORDS_AFTER_START);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const char *command = argv[1];

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }
    volatile char *control_page = mmap(NULL, PAGE_SIZE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, CONTROL_PAGE);
    volatile char *idma_page = mmap(NULL, PAGE_SIZE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, IDMA_PAGE);
    if (control_page == MAP_FAILED || idma_page == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    control = (volatile uint32_t *)(control_page + CONTROL_OFFSET);
    interrupt_ack = (volatile uint32_t *)(control_page + INTERRUPT_ACK_OFFSET);
    idma_address = (volatile uint32_t *)(idma_page + IDMA_ADDRESS_OFFSET);
    program_data = (volatile uint32_t *)(idma_page + IDMA_PROGRAM_OFFSET);
    data_data = (volatile uint16_t *)(idma_page + IDMA_DATA_OFFSET);

    if (strcmp(command, "--stop") == 0 && argc == 2) {
        // As after a start of the box: in reset, but not powered down.
        hold_in_reset(1);
        *control = *control & ~(uint32_t)DSP0_POWER_DOWN;
        printf("control 0x%08x, DSP 0 in reset\n", *control);
        return 0;
    }

    int needs_running_dsp = strcmp(command, "--peek") == 0 || strcmp(command, "--write") == 0 ||
                            strcmp(command, "--ack") == 0;
    if (needs_running_dsp && !dsp_released()) {
        // Untested what IDMA does to a DSP in reset, so don't try.
        fprintf(stderr, "DSP 0 is in reset, load the firmware first\n");
        return 1;
    }
    if (strcmp(command, "--peek") == 0 && argc == 4) {
        peek(strtoul(argv[2], NULL, 0), strtoul(argv[3], NULL, 0));
        return 0;
    }
    if (strcmp(command, "--write") == 0 && argc == 4) {
        return queue_write(strtoul(argv[2], NULL, 0), strtoul(argv[3], NULL, 0));
    }
    if (strcmp(command, "--ack") == 0 && argc == 3) {
        unsigned long count = strtoul(argv[2], NULL, 0);
        for (unsigned long i = 0; i < count; i++) {
            *interrupt_ack = DSP0_ACK;
            usleep(ACK_INTERVAL_US);
        }
        return 0;
    }
    if (command[0] == '-' || argc != 2) {
        usage();
        return 2;
    }
    return load(command);
}
