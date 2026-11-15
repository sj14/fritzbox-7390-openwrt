// Hardware setup and logging, shared by /init and load-fpga.
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>

// /dev/kmsg, so that output reaches the kernel log and with it the RAM log.
extern int kmsg_fd;

void log_line(const char *line);
// Loads the FPGA, which powers the USB port, from /lib/firmware/fpga.bit, and
// with release_prog leaves PROG an input. Returns 0 if it worked.
int load_fpga(bool release_prog);
// Releases the switch from reset and logs its identity.
void log_switch_identity(void);
// Sets up the switch's connection to the Ethernet block.
void configure_switch(void);

#endif
