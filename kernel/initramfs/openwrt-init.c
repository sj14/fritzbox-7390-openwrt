// /init of the OpenWrt images. It prepares the hardware that has no kernel driver
// yet, then hands over to OpenWrt's init: in a test image with OpenWrt in the
// initramfs the same way as OpenWrt's own initramfs, in an installed image after
// switching to the squashfs in flash.

#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <mtd/ubi-user.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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

// The NAND partition, the UBI volume that holds OpenWrt's overlay there, and the
// partition in the NOR flash that holds it otherwise.
static const char NAND_PARTITION[] = "ubi";
static const char OVERLAY_VOLUME[] = "rootfs_data";
static const char NOR_OVERLAY_PARTITION[] = "rootfs_data";
// UBI's erase counter header starts every block that UBI has formatted.
static const char UBI_MAGIC[] = "UBI#";
// The kernel's limits: UBI_MAX_DEVICES in drivers/mtd/ubi/ubi.h and
// UBI_MAX_VOLUMES in drivers/mtd/ubi/ubi-media.h.
enum { MAX_UBI_DEVICES = 32, MAX_UBI_VOLUMES = 128 };
// Where the NOR overlay and the new volume are mounted for a moment. Both
// directories are in the initramfs and unused when the box starts from flash.
static const char NOR_MOUNT[] = "/rom";
static const char VOLUME_MOUNT[] = "/overlay";

static void log_error(const char *what) {
    char line[160];

    snprintf(line, sizeof(line), "NAND: %s: %s\n", what, strerror(errno));
    log_line(line);
}

// Reads the first line of a sysfs file. Returns 0 on success.
static int read_sysfs(const char *path, char *value, size_t size) {
    FILE *file = fopen(path, "r");

    if (file == NULL) {
        return -1;
    }
    bool ok = fgets(value, (int)size, file) != NULL;
    fclose(file);
    if (!ok) {
        return -1;
    }
    value[strcspn(value, "\n")] = '\0';
    return 0;
}

// Returns the number of the UBI device on this MTD partition, or -1.
static int find_ubi_device(int mtd_number) {
    char path[64];
    char value[16];

    for (int ubi = 0; ubi < MAX_UBI_DEVICES; ubi++) {
        snprintf(path, sizeof(path), "/sys/class/ubi/ubi%d/mtd_num", ubi);
        if (read_sysfs(path, value, sizeof(value)) == 0 && atoi(value) == mtd_number) {
            return ubi;
        }
    }
    return -1;
}

static bool has_volume(int ubi, const char *name) {
    char path[64];
    char value[UBI_MAX_VOLUME_NAME + 1];

    for (int volume = 0; volume < MAX_UBI_VOLUMES; volume++) {
        snprintf(path, sizeof(path), "/sys/class/ubi/ubi%d_%d/name", ubi, volume);
        if (read_sysfs(path, value, sizeof(value)) == 0 && strcmp(value, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_bad_block(int fd, uint32_t offset) {
    long long position = offset;

    return ioctl(fd, MEMGETBADBLOCK, &position) > 0;
}

// Whether any good block starts with a UBI header: then this is UBI content
// that the kernel couldn't attach, for example after a damage, and not content
// of another system.
static bool has_ubi_headers(int fd, const struct mtd_info_user *info) {
    char start[sizeof(UBI_MAGIC) - 1];

    for (uint32_t offset = 0; offset < info->size; offset += info->erasesize) {
        if (is_bad_block(fd, offset)) {
            continue;
        }
        if (pread(fd, start, sizeof(start), offset) == (ssize_t)sizeof(start) &&
            memcmp(start, UBI_MAGIC, sizeof(start)) == 0) {
            return true;
        }
    }
    return false;
}

// Erases all good blocks. Returns the number of blocks that failed.
static int erase_good_blocks(int fd, const struct mtd_info_user *info) {
    int failures = 0;

    for (uint32_t offset = 0; offset < info->size; offset += info->erasesize) {
        if (is_bad_block(fd, offset)) {
            continue;
        }
        struct erase_info_user erase = {.start = offset, .length = info->erasesize};
        if (ioctl(fd, MEMERASE, &erase) != 0) {
            failures++;
        }
    }
    return failures;
}

// Attaches UBI to the MTD partition. UBI formats erased blocks itself. Returns
// the number of the UBI device, or -1.
static int attach_ubi(int mtd_number) {
    struct ubi_attach_req request = {.ubi_num = UBI_DEV_NUM_AUTO, .mtd_num = mtd_number};
    int fd = open("/dev/ubi_ctrl", O_RDONLY);

    if (fd < 0) {
        log_error("cannot open /dev/ubi_ctrl");
        return -1;
    }
    int result = ioctl(fd, UBI_IOCATT, &request);
    if (result != 0) {
        log_error("cannot attach UBI");
    }
    close(fd);
    return result == 0 ? request.ubi_num : -1;
}

// Makes the overlay volume over all the space that UBI has available.
static int make_overlay_volume(int ubi) {
    char path[64];
    char value[16];
    char device[16];

    snprintf(path, sizeof(path), "/sys/class/ubi/ubi%d/avail_eraseblocks", ubi);
    if (read_sysfs(path, value, sizeof(value)) != 0) {
        log_error("cannot read the free space");
        return -1;
    }
    long long blocks = atoll(value);
    snprintf(path, sizeof(path), "/sys/class/ubi/ubi%d/eraseblock_size", ubi);
    if (read_sysfs(path, value, sizeof(value)) != 0) {
        log_error("cannot read the block size");
        return -1;
    }
    long long block_size = atoll(value);

    struct ubi_mkvol_req request = {
        .vol_id = UBI_VOL_NUM_AUTO,
        .alignment = 1,
        .bytes = blocks * block_size,
        .vol_type = UBI_DYNAMIC_VOLUME,
        .name_len = (short)strlen(OVERLAY_VOLUME),
    };
    strcpy(request.name, OVERLAY_VOLUME);
    snprintf(device, sizeof(device), "/dev/ubi%d", ubi);
    int fd = open(device, O_RDONLY);
    if (fd < 0) {
        log_error("cannot open the UBI device");
        return -1;
    }
    int result = ioctl(fd, UBI_IOCMKVOL, &request);
    if (result != 0) {
        log_error("cannot make the volume");
    }
    close(fd);
    return result;
}

static int copy_file(const char *source, const char *target) {
    char buffer[4096];
    ssize_t length = 0;
    int result = 0;
    int in = open(source, O_RDONLY);
    int out = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (in < 0 || out < 0) {
        result = -1;
    }
    while (result == 0 && (length = read(in, buffer, sizeof(buffer))) > 0) {
        if (write(out, buffer, (size_t)length) != length) {
            result = -1;
        }
    }
    if (length < 0) {
        result = -1;
    }
    if (out >= 0 && (fsync(out) != 0 || close(out) != 0)) {
        result = -1;
    }
    if (in >= 0) {
        close(in);
    }
    return result;
}

// Puts the settings that a sysupgrade saved in the NOR flash (mtd -j) into the
// new volume, where preinit restores them, the same way as platform.sh does when
// the NAND has the overlay already.
static void move_saved_settings(int ubi) {
    char source[64];
    char target[64];
    char volume[32];

    snprintf(source, sizeof(source), "%s/sysupgrade.tgz", NOR_MOUNT);
    if (access(source, F_OK) != 0) {
        return;
    }
    snprintf(volume, sizeof(volume), "ubi%d:%s", ubi, OVERLAY_VOLUME);
    if (mount(volume, VOLUME_MOUNT, "ubifs", MS_NOATIME, NULL) != 0) {
        log_error("cannot mount the new volume");
        return;
    }
    snprintf(target, sizeof(target), "%s/sysupgrade.tgz", VOLUME_MOUNT);
    if (copy_file(source, target) == 0) {
        log_line("NAND: moved the saved settings from the NOR flash\n");
    } else {
        log_error("cannot move the saved settings");
    }
    umount(VOLUME_MOUNT);
}

// OpenWrt's NAND boards come with a UBI volume rootfs_data for the overlay in
// their image, which mount_root prefers to rootfs_data in the NOR flash. EVA
// only writes the NOR flash, so at the first start from flash this erases the
// NAND, the AVM firmware's internal storage, and makes that volume. It leaves
// the NAND alone while it has UBI already, while an overlay in the NOR flash is
// in use, whose settings only a sysupgrade carries over, and when it finds UBI
// headers that UBI couldn't attach, which are left for a repair by hand.
// Needs /proc and /sys.
static void prepare_nand_overlay(void) {
    char device[32];
    char line[128];
    int nand = find_mtd(NAND_PARTITION);

    if (nand < 0) {
        return;
    }
    int ubi = find_ubi_device(nand);
    if (ubi >= 0) {
        if (!has_volume(ubi, OVERLAY_VOLUME)) {
            log_line("NAND: UBI has no volume rootfs_data, left as it is\n");
        }
        return;
    }

    int nor = find_mtd(NOR_OVERLAY_PARTITION);
    bool nor_mounted = false;
    if (nor >= 0) {
        snprintf(device, sizeof(device), "/dev/mtdblock%d", nor);
        nor_mounted = mount(device, NOR_MOUNT, "jffs2", MS_RDONLY, NULL) == 0;
    }
    char nor_upper[32];
    snprintf(nor_upper, sizeof(nor_upper), "%s/upper", NOR_MOUNT);
    if (nor_mounted && access(nor_upper, F_OK) == 0) {
        log_line("NAND: the overlay in the NOR flash is in use, NAND left as it is\n");
        umount(NOR_MOUNT);
        return;
    }

    snprintf(device, sizeof(device), "/dev/mtd%d", nand);
    int fd = open(device, O_RDWR);
    struct mtd_info_user info;
    if (fd < 0 || ioctl(fd, MEMGETINFO, &info) != 0) {
        log_error("cannot open the NAND");
    } else if (has_ubi_headers(fd, &info)) {
        log_line("NAND: UBI headers that UBI couldn't attach, left for a repair by hand\n");
    } else {
        log_line("NAND: no UBI, erasing it for OpenWrt's overlay\n");
        int failures = erase_good_blocks(fd, &info);
        snprintf(line, sizeof(line), "NAND: erased, %d blocks failed\n", failures);
        log_line(line);
        close(fd);
        fd = -1;
        ubi = attach_ubi(nand);
        if (ubi >= 0 && make_overlay_volume(ubi) == 0) {
            log_line("NAND: made the UBI volume rootfs_data\n");
            if (nor_mounted) {
                move_saved_settings(ubi);
            }
        }
    }
    if (fd >= 0) {
        close(fd);
    }
    if (nor_mounted) {
        umount(NOR_MOUNT);
    }
}

// Puts a tmpfs over the squashfs at /rom with overlayfs, at /newroot, in the same
// layout as OpenWrt (changes at /overlay).
static int mount_ram_overlay(void) {
    static const char OVERLAY_OPTIONS[] = "lowerdir=/rom,upperdir=/overlay/upper,workdir=/overlay/work";
    char line[96];

    if (mount("tmpfs", "/overlay", "tmpfs", MS_NOATIME, "mode=0755") != 0 ||
        mkdir("/overlay/upper", 0755) != 0 || mkdir("/overlay/work", 0755) != 0 ||
        mount("overlayfs:/overlay", "/newroot", "overlay", MS_NOATIME, OVERLAY_OPTIONS) != 0) {
        snprintf(line, sizeof(line), "cannot set up the overlay: %s\n", strerror(errno));
        log_line(line);
        return -1;
    }
    mount("/rom", "/newroot/rom", NULL, MS_MOVE, NULL);
    mount("/overlay", "/newroot/overlay", NULL, MS_MOVE, NULL);
    return 0;
}

// An installed image has no OpenWrt files in the initramfs. Its root file system
// is the squashfs in the rootfs partition, or in rootfs_ram while the same image
// is tested from RAM (arch/mips/fusiv/ramboot.c). This makes it the root
// directory, like switch_root.
//
// From flash, OpenWrt's mount_root then puts its overlay on the rootfs_data
// partition, as on other boards. From RAM, the changes stay in a tmpfs, so that a
// test never writes the flash, and *root_writable tells to skip mount_root.
static int switch_to_flash_root(bool *root_writable) {
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
    *root_writable = strcmp(name, "rootfs_ram") == 0;
    // Only from flash: a test from RAM must not change the flash.
    if (number >= 0 && !*root_writable) {
        mount("sysfs", "/sys", "sysfs", 0, NULL);
        prepare_nand_overlay();
        umount("/sys");
    }
    umount("/proc");
    if (number < 0) {
        log_line("no rootfs partition\n");
        return -1;
    }

    const char *squashfs_target = *root_writable ? "/rom" : "/newroot";
    snprintf(device, sizeof(device), "/dev/mtdblock%d", number);
    if (mount(device, squashfs_target, "squashfs", MS_RDONLY, NULL) != 0) {
        snprintf(line, sizeof(line), "cannot mount %s (%s): %s\n", device, name, strerror(errno));
        log_line(line);
        return -1;
    }
    if (*root_writable && mount_ram_overlay() != 0) {
        return -1;
    }
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
    // A test image with OpenWrt in the initramfs is writable already.
    bool root_writable = access("/sbin/init", X_OK) == 0;
    if (!root_writable && switch_to_flash_root(&root_writable) != 0) {
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
    // With a writable root directory, OpenWrt's preinit skips mount_root, as for
    // its own initramfs.
    if (root_writable) {
        setenv("INITRAMFS", "1", 1);
    }
    execl("/sbin/init", "/sbin/init", (char *)NULL);
    snprintf(line, sizeof(line), "cannot start /sbin/init: %s\n", strerror(errno));
    log_line(line);
    return 1;
}
