// Loads the FPGA, which powers the USB port, from /lib/firmware/fpga.bit, for
// OpenWrt (/etc/init.d/fpga and avm-firmware-install). Unlike /init, it runs
// after OpenWrt mounted its overlay, so it also finds a bitstream that
// avm-firmware-install put there. Logs to the kernel log, like /init.
//
// Usage: load-fpga [--release-prog]
// --release-prog leaves PROG an input afterwards, as AVM's Piglet driver does
// (piglet_reset). Whether that turns off the red LED inside the box is not
// tested yet, see "LEDs and the FPGA" in DEVELOPMENT.md.
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "board.h"

int main(int argc, char **argv) {
    bool release_prog = argc == 2 && strcmp(argv[1], "--release-prog") == 0;

    if (argc > 2 || (argc == 2 && !release_prog)) {
        fprintf(stderr, "Usage: load-fpga [--release-prog]\n");
        return 2;
    }
    kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (load_fpga(release_prog) != 0) {
        fprintf(stderr, "load-fpga: the FPGA was not loaded, see the kernel log\n");
        return 1;
    }
    return 0;
}
