/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _FUSIV_BRINGUP_H
#define _FUSIV_BRINGUP_H

#include <linux/types.h>

/* The AVM reboot status text, which survives a software reset. */
#define FUSIV_MAILBOX_PHYS	0x00fffe00
#define FUSIV_MAILBOX_SIZE	512

#define FUSIV_RAMLOG_PHYS	0x01c00000
#define FUSIV_RAMLOG_SIZE	0x40000

/* How far the boot got. The value is stored in the RAM log header. */
enum bringup_stage {
	STAGE_PROM_INIT = 1,
	STAGE_MEM_SETUP,
	STAGE_IRQ_INIT,
	STAGE_TIME_INIT,
	STAGE_TIMER_RUNNING,
	STAGE_RESTART,
};

/*
 * write_log is false for a kernel started from flash, so that it keeps the RAM log
 * of a test kernel that EVA started from RAM before it.
 */
void bringup_init(bool write_log);
void bringup_set_stage(enum bringup_stage stage);
void bringup_log_char(char c);
void bringup_mark_ram(void);

#endif
