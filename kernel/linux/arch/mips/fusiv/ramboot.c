// SPDX-License-Identifier: GPL-2.0
/*
 * The image that EVA started from RAM, as an MTD device with the partitions of
 * the flash. tools/eva-ramboot.py leaves the image at the end of RAM, keeps that
 * area out of Linux's memory with "memsize" and passes the area as
 * mtdram1=<start>,<end>, as AVM's tools do for their kernel_ram and rootfs_ram.
 * So a flash image tested from RAM finds its root file system in rootfs_ram, and
 * /init mounts that instead of rootfs in the flash.
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/plat-ram.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <asm/addrspace.h>

/* The same as the kernel partition in the device tree and in build.sh. */
#define KERNEL_PARTITION_SIZE	SZ_4M

static unsigned long image_start;
static unsigned long image_end;

static int __init parse_mtdram(char *arg)
{
	if (!arg || sscanf(arg, "%lx,%lx", &image_start, &image_end) != 2 || image_end <= image_start) {
		pr_warn("fusiv: cannot read mtdram1=%s\n", arg ? arg : "");
		image_start = 0;
		image_end = 0;
	}
	return 0;
}
early_param("mtdram1", parse_mtdram);

static struct mtd_partition image_partitions[] = {
	{
		.name = "kernel_ram",
		.size = KERNEL_PARTITION_SIZE,
		.mask_flags = MTD_WRITEABLE,
	},
	{
		.name = "rootfs_ram",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
		.mask_flags = MTD_WRITEABLE,
	},
};

/* plat-ram takes the area as it is. physmap would round it down to a power of two. */
static struct platdata_mtd_ram image_data = {
	.mapname = "ram-image",
	.partitions = image_partitions,
	.nr_partitions = ARRAY_SIZE(image_partitions),
	.bankwidth = 4,
};

static int __init register_ram_image(void)
{
	struct platform_device *device;
	struct resource area;

	/* A test image without a root file system fits into the kernel partition. */
	if (image_end - image_start <= KERNEL_PARTITION_SIZE)
		return 0;

	area = DEFINE_RES_MEM(CPHYSADDR(image_start), image_end - image_start);
	device = platform_device_register_resndata(NULL, "mtd-ram", PLATFORM_DEVID_AUTO,
						   &area, 1, &image_data, sizeof(image_data));
	return PTR_ERR_OR_ZERO(device);
}
device_initcall(register_ram_image);
