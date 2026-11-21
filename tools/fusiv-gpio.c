// Reads or drives one of GPIOs 0 to 15, which have no Linux driver, through
// /dev/mem. Used to compare pins with AVM's configuration, see "Registers
// compared with the AVM firmware" in DEVELOPMENT.md.
//
// Usage: fusiv-gpio <pin> read | high | low | pulse | input
//   read   prints the direction and input registers of GPIOs 0 to 15
//   high   drives the pin high (sets the level first, then makes it an output)
//   low    drives the pin low
//   pulse  as Piglet's DECT reset: low for 1 ms, then high; stays an output
//   input  makes the pin an input again
//
// Some pins hold the box together: 15 is the switch's reset, 8, 11, 12 and 14
// load the FPGA (PROG low erases it). Look the pin up before driving it.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum {
    PAGE_SIZE_BYTES = 4096,
    GPIO_PAGE = 0x19040000,
    GPIO_DIRECTION = 0x0,
    GPIO_FLAGS = 0x4, // reads the pins
    GPIO_CLEAR = 0x4, // a 16 bit write clears the pins set in it
    GPIO_SET = 0x6,   // a 16 bit write sets the pins set in it
    PIN_FPGA_DONE = 1 << 8,
    PULSE_LOW_US = 1000,
    HIGHEST_PIN = 15,
};

static void usage(void) {
    fprintf(stderr, "Usage: fusiv-gpio <pin> read | high | low | pulse | input\n");
}

int main(int argc, char **argv) {
    if (argc != 3) {
        usage();
        return 2;
    }
    char *end;
    long pin_number = strtol(argv[1], &end, 10);
    if (*end != '\0' || pin_number < 0 || pin_number > HIGHEST_PIN) {
        fprintf(stderr, "the pin must be 0 to %d\n", HIGHEST_PIN);
        return 2;
    }
    const unsigned short pin = (unsigned short)(1 << pin_number);
    const char *command = argv[2];

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }
    volatile char *gpio = mmap(NULL, PAGE_SIZE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_PAGE);
    if (gpio == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    volatile unsigned short *direction = (volatile unsigned short *)(gpio + GPIO_DIRECTION);
    volatile unsigned short *clear_pins = (volatile unsigned short *)(gpio + GPIO_CLEAR);
    volatile unsigned short *set_pins = (volatile unsigned short *)(gpio + GPIO_SET);
    volatile unsigned int *flags = (volatile unsigned int *)(gpio + GPIO_FLAGS);

    if (strcmp(command, "high") == 0) {
        *set_pins = pin;
        *direction = *direction | pin;
    } else if (strcmp(command, "low") == 0) {
        *clear_pins = pin;
        *direction = *direction | pin;
    } else if (strcmp(command, "pulse") == 0) {
        *set_pins = pin;
        *direction = *direction | pin;
        *clear_pins = pin;
        usleep(PULSE_LOW_US);
        *set_pins = pin;
    } else if (strcmp(command, "input") == 0) {
        *direction = *direction & ~pin;
    } else if (strcmp(command, "read") != 0) {
        usage();
        return 2;
    }

    unsigned int direction_now = *direction;
    unsigned int flags_now = *flags;
    printf("direction 0x%04x, flags 0x%08x, GPIO %ld %s %d, FPGA done %d\n", direction_now, flags_now, pin_number,
           (direction_now & pin) ? "output" : "input", (flags_now & pin) != 0, (flags_now & PIN_FPGA_DONE) != 0);
    return 0;
}
