// Sends a sequence of NAND cycles to the FRITZ!Box 7390's NAND through /dev/mem,
// the way AVM's direct_avm.c does: control lines on GPIOs 16 (/CE0), 18 (CLE)
// and 19 (ALE), ready on GPIO 21, 16 bit accesses to the bus at 0x1e000000 of
// which the chip uses the low byte, and a read of the NOR flash before and
// after each change of the control lines.
// Only commands that don't change the flash are accepted: RESET (ff), READ
// STATUS (70), READ ID (90), READ PARAMETER PAGE (ec), GET FEATURES (ee).
// Usage: fusiv-nand-cmd <step>..., each step one of
//   cXX  command cycle      aXX  address cycle
//   rN   read N bytes       w    wait for ready
// for example: fusiv-nand-cmd cff w c90 a00 r5

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

enum { PAGE = 4096 };

enum {
    GPIO1 = 0x190b0000,
    NAND_DATA = 0x1e000000,
    NOR = 0x1f000000,
};

// Bits in GPIO1, which holds GPIOs 16 to 31.
enum {
    NCE0 = 1 << 0,  // GPIO 16
    CLE = 1 << 2,   // GPIO 18
    ALE = 1 << 3,   // GPIO 19
    READY = 1 << 5, // GPIO 21
};

static volatile uint8_t *gpio1;
static volatile uint16_t *nand;
static volatile uint8_t *nor;

static volatile uint16_t *gpio_reg(unsigned offset) {
    return (volatile uint16_t *)(gpio1 + offset);
}

static void set_lines(uint16_t high, uint16_t low) {
    (void)*nor;
    *gpio_reg(0x6) = high;
    *gpio_reg(0x4) = low;
    (void)*nor;
}

static int is_ready(void) {
    return (*gpio_reg(0x4) & READY) != 0;
}

static int allowed_command(unsigned command) {
    switch (command) {
    case 0xff: case 0x70: case 0x90: case 0xec: case 0xee:
        return 1;
    default:
        return 0;
    }
}

static volatile void *map(int fd, uint32_t address) {
    void *p = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, address);
    if (p == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    return p;
}

int main(int argc, char **argv) {
    // Check all steps before the first cycle reaches the chip.
    for (int i = 1; i < argc; i++) {
        char kind = argv[i][0];
        unsigned value = strtoul(argv[i] + 1, NULL, kind == 'r' ? 10 : 16);
        if (kind == 'c' && !allowed_command(value)) {
            fprintf(stderr, "command %02x is not allowed\n", value);
            return 1;
        }
        if (kind != 'c' && kind != 'a' && kind != 'r' && kind != 'w') {
            fprintf(stderr, "unknown step %s\n", argv[i]);
            return 1;
        }
    }

    // O_SYNC requests an uncached mapping.
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }
    gpio1 = map(fd, GPIO1);
    nand = map(fd, NAND_DATA);
    nor = map(fd, NOR);

    uint16_t lines = NCE0 | CLE | ALE;
    uint16_t saved_flag = *gpio_reg(0x4);
    printf("before: flag 0x%04x\n", saved_flag);

    for (int i = 1; i < argc; i++) {
        char kind = argv[i][0];
        unsigned value = strtoul(argv[i] + 1, NULL, kind == 'r' ? 10 : 16);
        switch (kind) {
        case 'c':
            set_lines(CLE, NCE0 | ALE);
            *nand = value;
            break;
        case 'a':
            set_lines(ALE, NCE0 | CLE);
            *nand = value;
            break;
        case 'w': {
            int polls = 0;
            while (!is_ready() && polls < 1000000)
                polls++;
            printf("%s: ready %d after %d polls\n", argv[i], is_ready(), polls);
            break;
        }
        case 'r':
            set_lines(0, NCE0 | CLE | ALE);
            printf("%s:", argv[i]);
            for (unsigned n = 0; n < value; n++)
                printf(" %02x", (uint8_t)*nand);
            printf("\n");
            break;
        }
    }

    // Chip deselected, then back to the previous levels of the three lines.
    set_lines(NCE0, CLE | ALE);
    set_lines(saved_flag & lines, ~saved_flag & lines);
    printf("after: flag 0x%04x\n", *gpio_reg(0x4));
    return 0;
}
