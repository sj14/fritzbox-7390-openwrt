// SPDX-License-Identifier: GPL-2.0-only
/*
 * The hardware watchdog of the Vx180 restarts the box if the kernel hangs. A
 * kernel timer serves it, so that it only fires when the kernel no longer runs
 * its timers.
 *
 * Register usage follows AVM's GPL kernel (ikan_wdt.c, vx180.h).
 */

#include <linux/bits.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/printk.h>
#include <linux/timer.h>

#include "bringup.h"
#include "fusiv.h"

#define WDT_CTRL		CKSEG1ADDR(0x19000008)
#define WDT_CTRL_DISABLE	BIT(0)
#define WDT_CTRL_SERVICE	(0xa << 8)
#define WDT_CTRL_RELOAD_MAX	(0xfffc << 16)

/* The counter runs at about 10170 Hz, so 0xfffc lasts about 6.4 s. */
#define WDT_SERVICE_INTERVAL	(HZ / 10)

static struct timer_list wdt_timer;

void fusiv_watchdog_start(void)
{
	void __iomem *ctrl = (void __iomem *)WDT_CTRL;

	__raw_writel(WDT_CTRL_DISABLE | WDT_CTRL_RELOAD_MAX, ctrl);
	__raw_writel(WDT_CTRL_SERVICE | WDT_CTRL_RELOAD_MAX, ctrl);
}

static void wdt_timer_tick(struct timer_list *timer)
{
	static bool first_tick_seen;

	__raw_writel(WDT_CTRL_SERVICE, (void __iomem *)WDT_CTRL);

	if (!first_tick_seen) {
		first_tick_seen = true;
		bringup_set_stage(STAGE_TIMER_RUNNING);
		pr_info("fusiv: timer interrupts work\n");
	}

	mod_timer(timer, jiffies + WDT_SERVICE_INTERVAL);
}

static int __init wdt_timer_init(void)
{
	timer_setup(&wdt_timer, wdt_timer_tick, 0);
	mod_timer(&wdt_timer, jiffies + WDT_SERVICE_INTERVAL);

	return 0;
}
early_initcall(wdt_timer_init);
