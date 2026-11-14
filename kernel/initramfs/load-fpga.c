// Loads the FPGA, which powers the USB port, from /lib/firmware/fpga.bit, for
// OpenWrt (/etc/init.d/fpga and avm-firmware-install). Unlike /init, it runs
// after OpenWrt mounted its overlay, so it also finds a bitstream that
// avm-firmware-install put there. Logs to the kernel log, like /init.
#include <fcntl.h>
#include <stdio.h>

#include "board.h"

int main(void) {
    kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (load_fpga() != 0) {
        fprintf(stderr, "load-fpga: the FPGA was not loaded, see the kernel log\n");
        return 1;
    }
    return 0;
}
