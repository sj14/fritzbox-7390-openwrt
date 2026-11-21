/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _FUSIV_H
#define _FUSIV_H

/* Returns the Linux interrupt for a source of the Vx180 interrupt controller. */
unsigned int fusiv_ipc_map_irq(unsigned int source);

/* Starts the hardware watchdog. A kernel timer serves it from early_initcall on. */
void fusiv_watchdog_start(void);

#endif
