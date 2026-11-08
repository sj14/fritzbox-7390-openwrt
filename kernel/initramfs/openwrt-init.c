// /init of the OpenWrt images. It prepares the hardware that has no kernel driver
// yet, then hands over to OpenWrt's init: in a test image with OpenWrt in the
// initramfs the same way as OpenWrt's own initramfs, in an installed image after
// switching to the squashfs in flash.

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board.h"

// 0 keeps the box running, for an installed image.
#ifndef TEST_DURATION_S
#define TEST_DURATION_S 600
#endif

// OpenWrt's log of a test image, see openwrt/uci-defaults-test.
static const char OPENWRT_LOG[] = "/tmp/openwrt.log";
// Adds the state of the test image to that log, see openwrt/test-diagnostics.
static const char DIAGNOSTICS[] = "/usr/sbin/test-diagnostics >> /tmp/openwrt.log 2>&1";

// Copies OpenWrt's log to the kernel log, so that it survives the restart in the
// RAM log. Kernel messages are left out, they are there already.
static void copy_openwrt_log(void) {
    char line[512];
    FILE *log = fopen(OPENWRT_LOG, "r");

    if (log == NULL) {
        log_line("test image: no OpenWrt log\n");
        return;
    }
    while (fgets(line, sizeof(line), log) != NULL) {
        if (strstr(line, " kernel: ") == NULL) {
            log_line(line);
        }
    }
    fclose(log);
}

// A test image restarts the box after TEST_DURATION_S, so that the box returns to
// the installed firmware even if OpenWrt never becomes reachable.
static void schedule_restart(void) {
    char line[96];

    if (TEST_DURATION_S == 0 || fork() != 0) {
        return;
    }
    setsid();
    sleep(TEST_DURATION_S);
    if (system(DIAGNOSTICS) != 0) {
        log_line("test image: diagnostics failed\n");
    }
    copy_openwrt_log();
    snprintf(line, sizeof(line), "test image: restarting after %d s\n", TEST_DURATION_S);
    log_line(line);
    sync();
    reboot(RB_AUTOBOOT);
    _exit(1);
}

// Returns the number of the MTD partition with this name, or -1.
static int find_mtd(const char *name) {
    char line[128];
    char quoted_name[64];
    int number = -1;
    FILE *mtd = fopen("/proc/mtd", "r");

    if (mtd == NULL) {
        return -1;
    }
    snprintf(quoted_name, sizeof(quoted_name), "\"%s\"", name);
    while (fgets(line, sizeof(line), mtd) != NULL) {
        int candidate;
        if (sscanf(line, "mtd%d:", &candidate) == 1 && strstr(line, quoted_name) != NULL) {
            number = candidate;
            break;
        }
    }
    fclose(mtd);
    return number;
}

// An installed image has no OpenWrt files in the initramfs. Its root file system
// is the squashfs in the rootfs partition, or in rootfs_ram while the same image
// is tested from RAM (arch/mips/fusiv/ramboot.c). This puts a tmpfs over it with
// overlayfs, in the same layout as OpenWrt (squashfs at /rom, changes at /overlay),
// and makes that the root directory, like switch_root. OpenWrt's mount_root can't
// do it here: for a squashfs on a block device it wants a loop device behind the
// squashfs, which would write into the flash.
static int switch_to_flash_root(void) {
    static const char OVERLAY_OPTIONS[] = "lowerdir=/rom,upperdir=/overlay/upper,workdir=/overlay/work";
    char device[32];
    char line[128];
    const char *name = "rootfs_ram";

    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    int number = find_mtd(name);
    if (number < 0) {
        name = "rootfs";
        number = find_mtd(name);
    }
    umount("/proc");
    if (number < 0) {
        log_line("no rootfs partition\n");
        return -1;
    }

    snprintf(device, sizeof(device), "/dev/mtdblock%d", number);
    if (mount(device, "/rom", "squashfs", MS_RDONLY, NULL) != 0) {
        snprintf(line, sizeof(line), "cannot mount %s (%s): %s\n", device, name, strerror(errno));
        log_line(line);
        return -1;
    }
    if (mount("tmpfs", "/overlay", "tmpfs", MS_NOATIME, "mode=0755") != 0 ||
        mkdir("/overlay/upper", 0755) != 0 || mkdir("/overlay/work", 0755) != 0 ||
        mount("overlayfs:/overlay", "/newroot", "overlay", MS_NOATIME, OVERLAY_OPTIONS) != 0) {
        snprintf(line, sizeof(line), "cannot set up the overlay: %s\n", strerror(errno));
        log_line(line);
        return -1;
    }
    mount("/rom", "/newroot/rom", NULL, MS_MOVE, NULL);
    mount("/overlay", "/newroot/overlay", NULL, MS_MOVE, NULL);
    mount("/dev", "/newroot/dev", NULL, MS_MOVE, NULL);
    if (chdir("/newroot") != 0 || mount(".", "/", NULL, MS_MOVE, NULL) != 0 || chroot(".") != 0 ||
        chdir("/") != 0) {
        snprintf(line, sizeof(line), "cannot switch to %s: %s\n", device, strerror(errno));
        log_line(line);
        return -1;
    }
    snprintf(line, sizeof(line), "root file system: %s (%s)\n", device, name);
    log_line(line);
    return 0;
}

int main(void) {
    char line[128];

    // O_CLOEXEC: OpenWrt's init opens the kernel log itself.
    kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    bool in_initramfs = access("/sbin/init", X_OK) == 0;
    if (!in_initramfs && switch_to_flash_root() != 0) {
        return 1;
    }

    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    load_fpga();
    log_switch_identity();
    configure_switch();
    umount("/sys");
    umount("/proc");

    schedule_restart();
    // The root directory is writable in both cases, so OpenWrt's preinit skips
    // mount_root, as for its own initramfs.
    setenv("INITRAMFS", "1", 1);
    execl("/sbin/init", "/sbin/init", (char *)NULL);
    snprintf(line, sizeof(line), "cannot start /sbin/init: %s\n", strerror(errno));
    log_line(line);
    return 1;
}
