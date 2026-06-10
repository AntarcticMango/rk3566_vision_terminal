#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/types.h>

/*
 * ota_slotctl - RK3566 Buildroot A/B RootFS slot control helper
 *
 * 这版代码的设计目标：
 *   1. 默认不修改启动状态：
 *      get-current / get-inactive / dump-map / dump-raw / dump 都只读。
 *
 *   2. 写操作只修改 Rockchip U-Boot 真正使用的主 A/B metadata：
 *      U-Boot 源码中 AB_METADATA_MISC_PARTITION_OFFSET = 2048，
 *      也就是 misc + 0x800。
 *
 *      之前扫描 misc 时发现 0x860 也像 AB0 且 CRC 有效，但从 U-Boot 源码看，
 *      rk_avb_get_current_slot() 读写的是 0x800。因此本工具不再扫描并同步写
 *      0x860，避免把调试候选结构误当成主控结构。
 *
 *   3. 修正 mark-successful：
 *      U-Boot 认为 tries_remaining > 0 && successful_boot == 1 是非法状态，
 *      下次启动会把该 slot 清成 unbootable。
 *
 *      所以正确稳定态是：
 *        successful_boot = 1
 *        tries_remaining = 0
 *
 *      不是 tries_remaining = 7。
 *
 *   4. 支持开发调试：
 *      make-stable a|b   : 把指定槽位固定为稳定态，普通 RESET/reboot 不应切槽。
 *      disable-slot a|b  : 禁用指定槽位，开发时可避免自动回滚到另一槽位。
 *
 *      例如固定 A 调试：
 *        ota_slotctl make-stable a
 *        ota_slotctl disable-slot b
 *        sync
 *
 * 写 misc 前建议先备份：
 *   mkdir -p /userdata/ota_backup
 *   dd if=/dev/block/by-name/misc of=/userdata/ota_backup/misc_before.bin bs=1M count=4
 */

#define CMDLINE_PATH        "/proc/cmdline"
#define MOUNTS_PATH         "/proc/mounts"

#define SYSTEM_A_DEV        "/dev/mmcblk0p6"
#define SYSTEM_B_DEV        "/dev/mmcblk0p7"

#define SYSTEM_A_BYNAME     "/dev/block/by-name/system_a"
#define SYSTEM_B_BYNAME     "/dev/block/by-name/system_b"
#define MISC_BYNAME         "/dev/block/by-name/misc"
#define USERDATA_BYNAME     "/dev/block/by-name/userdata"

#define AB_METADATA_OFFSET       2048
#define AB_METADATA_DUMP_SIZE    256

#define RK_AB_MAGIC_OFFSET       1
#define RK_AB_VERSION_OFF        4
#define RK_AB_SLOT_A_OFF         8
#define RK_AB_SLOT_B_OFF         12
#define RK_AB_CRC_OFFSET         28
#define RK_AB_CRC_SIZE           4
#define RK_AB_CRC_CALC_SIZE      RK_AB_CRC_OFFSET
#define RK_AB_HEADER_SIZE        (RK_AB_CRC_OFFSET + RK_AB_CRC_SIZE)

#define RK_AB_MAX_PRIORITY       15
#define RK_AB_MAX_TRIES          7

struct rk_slot_info {
    uint8_t priority;
    uint8_t tries_remaining;
    uint8_t successful_boot;
    uint8_t verity_corrupted;
};

struct rk_ab_metadata {
    off_t offset;
    uint8_t raw[RK_AB_HEADER_SIZE];
};

static int read_text_file(const char *path, char *buf, size_t size)
{
    FILE *fp;
    size_t n;

    if (!path || !buf || size == 0) {
        return -1;
    }

    fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    n = fread(buf, 1, size - 1, fp);
    buf[n] = '\0';
    fclose(fp);

    return 0;
}

static char get_slot_from_cmdline(void)
{
    char buf[4096];

    if (read_text_file(CMDLINE_PATH, buf, sizeof(buf)) != 0) {
        return 0;
    }

    /*
     * 当前板子 cmdline 使用 android_slotsufix=_a/_b。
     * slotsufix 是板端实际拼写，不是这里写错。
     */
    if (strstr(buf, "slot_suffix=_a") ||
        strstr(buf, "slotsufix=_a") ||
        strstr(buf, "slot=_a")) {
        return 'a';
    }

    if (strstr(buf, "slot_suffix=_b") ||
        strstr(buf, "slotsufix=_b") ||
        strstr(buf, "slot=_b")) {
        return 'b';
    }

    return 0;
}

static char get_slot_from_root_mount(void)
{
    FILE *fp;
    char line[1024];

    fp = fopen(MOUNTS_PATH, "r");
    if (!fp) {
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        char dev[256] = {0};
        char mountpoint[256] = {0};
        char fstype[64] = {0};

        if (sscanf(line, "%255s %255s %63s", dev, mountpoint, fstype) != 3) {
            continue;
        }

        if (strcmp(mountpoint, "/") == 0) {
            fclose(fp);

            if (strcmp(dev, SYSTEM_A_DEV) == 0) {
                return 'a';
            }

            if (strcmp(dev, SYSTEM_B_DEV) == 0) {
                return 'b';
            }

            return 0;
        }
    }

    fclose(fp);
    return 0;
}

static char get_current_slot(void)
{
    char slot;

    slot = get_slot_from_cmdline();
    if (slot == 'a' || slot == 'b') {
        return slot;
    }

    slot = get_slot_from_root_mount();
    if (slot == 'a' || slot == 'b') {
        return slot;
    }

    return 0;
}

static uint32_t get_le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xff);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

static uint32_t crc32_calc(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xffffffffU;
    size_t i;
    int bit;

    for (i = 0; i < len; i++) {
        crc ^= data[i];

        for (bit = 0; bit < 8; bit++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xedb88320U;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xffffffffU;
}

static void load_slot_info(const uint8_t *buf, size_t off, struct rk_slot_info *slot)
{
    slot->priority = buf[off + 0];
    slot->tries_remaining = buf[off + 1];
    slot->successful_boot = buf[off + 2];
    slot->verity_corrupted = buf[off + 3];
}

static void store_slot_info(uint8_t *buf, size_t off, const struct rk_slot_info *slot)
{
    buf[off + 0] = slot->priority;
    buf[off + 1] = slot->tries_remaining;
    buf[off + 2] = slot->successful_boot;
    buf[off + 3] = slot->verity_corrupted;
}

static int slot_is_bootable(const struct rk_slot_info *slot)
{
    return (slot->priority > 0) &&
           (slot->successful_boot || slot->tries_remaining > 0);
}

static int slot_is_illegal(const struct rk_slot_info *slot)
{
    return (slot->priority > 0) &&
           (slot->tries_remaining > 0) &&
           (slot->successful_boot != 0);
}

static void slot_set_unbootable(struct rk_slot_info *slot)
{
    slot->priority = 0;
    slot->tries_remaining = 0;
    slot->successful_boot = 0;
    slot->verity_corrupted = 0;
}

/*
 * 对齐 U-Boot slot_normalize()：
 *   - priority == 0：规范化为 unbootable
 *   - priority > 0 && tries == 0 && successful == 0：尝试耗尽，unbootable
 *   - tries > 0 && successful == 1：非法状态，unbootable
 */
static void slot_normalize(struct rk_slot_info *slot)
{
    if (slot->priority > 0) {
        if (slot->tries_remaining == 0 && !slot->successful_boot) {
            slot_set_unbootable(slot);
            return;
        }

        if (slot->tries_remaining > 0 && slot->successful_boot) {
            slot_set_unbootable(slot);
            return;
        }
    } else {
        slot_set_unbootable(slot);
    }
}

static void print_slot_info(const char *name, const struct rk_slot_info *slot)
{
    printf("%s:\n", name);
    printf("  priority:         %u\n", slot->priority);
    printf("  tries_remaining:  %u\n", slot->tries_remaining);
    printf("  successful_boot:  %u\n", slot->successful_boot);
    printf("  verity_corrupted: %u\n", slot->verity_corrupted);
    printf("  bootable:         %s\n", slot_is_bootable(slot) ? "yes" : "no");

    if (slot_is_illegal(slot)) {
        printf("  WARNING: illegal for U-Boot: tries_remaining > 0 && successful_boot = 1\n");
        printf("           U-Boot may normalize this slot to unbootable on next boot.\n");
    }
}

/* ------------------------------------------------------------------------- */
/* misc 主 metadata 读写                                                      */
/* ------------------------------------------------------------------------- */

static int read_misc_at(off_t offset, uint8_t *buf, size_t size)
{
    int fd;
    ssize_t n;

    if (!buf || size == 0) {
        return -1;
    }

    fd = open(MISC_BYNAME, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "failed to open %s: %s\n", MISC_BYNAME, strerror(errno));
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) < 0) {
        fprintf(stderr, "failed to seek %s to 0x%lx: %s\n",
                MISC_BYNAME, (unsigned long)offset, strerror(errno));
        close(fd);
        return -1;
    }

    n = read(fd, buf, size);
    if (n < 0) {
        fprintf(stderr, "failed to read %s: %s\n", MISC_BYNAME, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);

    if ((size_t)n != size) {
        fprintf(stderr, "short read from %s: expected %zu, got %zd\n",
                MISC_BYNAME, size, n);
        return -1;
    }

    return 0;
}

static int write_misc_at(off_t offset, const uint8_t *buf, size_t size)
{
    int fd;
    ssize_t n;

    if (!buf || size == 0) {
        return -1;
    }

    fd = open(MISC_BYNAME, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "failed to open %s for write: %s\n",
                MISC_BYNAME, strerror(errno));
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) < 0) {
        fprintf(stderr, "failed to seek %s to 0x%lx: %s\n",
                MISC_BYNAME, (unsigned long)offset, strerror(errno));
        close(fd);
        return -1;
    }

    n = write(fd, buf, size);
    if (n < 0) {
        fprintf(stderr, "failed to write %s: %s\n", MISC_BYNAME, strerror(errno));
        close(fd);
        return -1;
    }

    if ((size_t)n != size) {
        fprintf(stderr, "short write to %s: expected %zu, got %zd\n",
                MISC_BYNAME, size, n);
        close(fd);
        return -1;
    }

    fsync(fd);
    close(fd);
    sync();

    return 0;
}

static int metadata_validate(const uint8_t *raw)
{
    uint32_t stored_be;
    uint32_t calc_crc;

    if (!(raw[RK_AB_MAGIC_OFFSET + 0] == 'A' &&
          raw[RK_AB_MAGIC_OFFSET + 1] == 'B' &&
          raw[RK_AB_MAGIC_OFFSET + 2] == '0')) {
        fprintf(stderr, "invalid A/B metadata magic at 0x%08x\n", AB_METADATA_OFFSET);
        return -1;
    }

    if (raw[RK_AB_VERSION_OFF] != 1) {
        fprintf(stderr, "unsupported A/B metadata version: %u\n", raw[RK_AB_VERSION_OFF]);
        return -1;
    }

    stored_be = get_be32(&raw[RK_AB_CRC_OFFSET]);
    calc_crc = crc32_calc(raw, RK_AB_CRC_CALC_SIZE);

    if (stored_be != calc_crc) {
        fprintf(stderr, "invalid primary A/B metadata CRC\n");
        fprintf(stderr, "stored_be=0x%08x calculated=0x%08x\n", stored_be, calc_crc);
        return -1;
    }

    return 0;
}

static void metadata_update_crc(uint8_t *raw)
{
    uint32_t crc = crc32_calc(raw, RK_AB_CRC_CALC_SIZE);
    put_be32(&raw[RK_AB_CRC_OFFSET], crc);
}

static int metadata_load_primary(struct rk_ab_metadata *meta)
{
    if (!meta) {
        return -1;
    }

    memset(meta, 0, sizeof(*meta));
    meta->offset = AB_METADATA_OFFSET;

    if (read_misc_at(meta->offset, meta->raw, sizeof(meta->raw)) != 0) {
        return -1;
    }

    if (metadata_validate(meta->raw) != 0) {
        return -1;
    }

    return 0;
}

static int metadata_save_primary(struct rk_ab_metadata *meta)
{
    if (!meta) {
        return -1;
    }

    metadata_update_crc(meta->raw);
    return write_misc_at(meta->offset, meta->raw, sizeof(meta->raw));
}

static void metadata_get_slots(const struct rk_ab_metadata *meta,
                               struct rk_slot_info *slot_a,
                               struct rk_slot_info *slot_b)
{
    load_slot_info(meta->raw, RK_AB_SLOT_A_OFF, slot_a);
    load_slot_info(meta->raw, RK_AB_SLOT_B_OFF, slot_b);
}

static void metadata_put_slots(struct rk_ab_metadata *meta,
                               const struct rk_slot_info *slot_a,
                               const struct rk_slot_info *slot_b)
{
    store_slot_info(meta->raw, RK_AB_SLOT_A_OFF, slot_a);
    store_slot_info(meta->raw, RK_AB_SLOT_B_OFF, slot_b);
}

/* ------------------------------------------------------------------------- */
/* dump 命令                                                                 */
/* ------------------------------------------------------------------------- */

static void print_hex_dump(const uint8_t *buf, size_t size)
{
    size_t i;
    size_t j;

    for (i = 0; i < size; i += 16) {
        printf("%08zx  ", i);

        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02x ", buf[i + j]);
            } else {
                printf("   ");
            }

            if (j == 7) {
                printf(" ");
            }
        }

        printf(" |");

        for (j = 0; j < 16 && i + j < size; j++) {
            unsigned char c = buf[i + j];
            printf("%c", isprint(c) ? c : '.');
        }

        printf("|\n");
    }
}

static int cmd_dump_raw(void)
{
    uint8_t buf[AB_METADATA_DUMP_SIZE];

    if (read_misc_at(AB_METADATA_OFFSET, buf, sizeof(buf)) != 0) {
        return 1;
    }

    printf("misc:   %s\n", MISC_BYNAME);
    printf("offset: 0x%08x\n", AB_METADATA_OFFSET);
    printf("size:   %d\n", AB_METADATA_DUMP_SIZE);
    printf("\n");

    print_hex_dump(buf, sizeof(buf));

    return 0;
}

static int cmd_dump(void)
{
    struct rk_ab_metadata meta;
    struct rk_slot_info slot_a;
    struct rk_slot_info slot_b;
    uint32_t stored_le;
    uint32_t stored_be;
    uint32_t calc_crc;
    char current;

    if (metadata_load_primary(&meta) != 0) {
        return 1;
    }

    metadata_get_slots(&meta, &slot_a, &slot_b);

    stored_le = get_le32(&meta.raw[RK_AB_CRC_OFFSET]);
    stored_be = get_be32(&meta.raw[RK_AB_CRC_OFFSET]);
    calc_crc = crc32_calc(meta.raw, RK_AB_CRC_CALC_SIZE);
    current = get_current_slot();

    printf("Bootloader A/B metadata\n");
    printf("=======================\n");
    printf("misc:        %s\n", MISC_BYNAME);
    printf("offset:      0x%08x\n", AB_METADATA_OFFSET);
    printf("current:     %c\n", current ? current : '?');
    printf("inactive:    %c\n", current ? (current == 'a' ? 'b' : 'a') : '?');
    printf("\n");

    printf("Header:\n");
    printf("  raw[0..3]:  %02x %02x %02x %02x\n",
           meta.raw[0], meta.raw[1], meta.raw[2], meta.raw[3]);
    printf("  magic text: %c%c%c\n",
           isprint(meta.raw[RK_AB_MAGIC_OFFSET + 0]) ? meta.raw[RK_AB_MAGIC_OFFSET + 0] : '.',
           isprint(meta.raw[RK_AB_MAGIC_OFFSET + 1]) ? meta.raw[RK_AB_MAGIC_OFFSET + 1] : '.',
           isprint(meta.raw[RK_AB_MAGIC_OFFSET + 2]) ? meta.raw[RK_AB_MAGIC_OFFSET + 2] : '.');
    printf("  version:    %u\n", meta.raw[RK_AB_VERSION_OFF]);
    printf("\n");

    print_slot_info("Slot A", &slot_a);
    printf("\n");
    print_slot_info("Slot B", &slot_b);
    printf("\n");

    printf("CRC:\n");
    printf("  stored_le:  0x%08x\n", stored_le);
    printf("  stored_be:  0x%08x\n", stored_be);
    printf("  calculated: 0x%08x\n", calc_crc);
    printf("  status:     %s\n", stored_be == calc_crc ? "valid, big-endian" : "mismatch");

    return 0;
}

static void cmd_dump_map(void)
{
    char current = get_current_slot();

    printf("system_a=%s\n", SYSTEM_A_DEV);
    printf("system_b=%s\n", SYSTEM_B_DEV);
    printf("system_a_byname=%s\n", SYSTEM_A_BYNAME);
    printf("system_b_byname=%s\n", SYSTEM_B_BYNAME);
    printf("misc=%s\n", MISC_BYNAME);
    printf("userdata=%s\n", USERDATA_BYNAME);

    if (current) {
        printf("current=%c\n", current);
        printf("inactive=%c\n", current == 'a' ? 'b' : 'a');
    } else {
        printf("current=unknown\n");
        printf("inactive=unknown\n");
    }
}

/* ------------------------------------------------------------------------- */
/* 写操作                                                                    */
/* ------------------------------------------------------------------------- */

static int cmd_mark_successful(void)
{
    struct rk_ab_metadata meta;
    struct rk_slot_info slot_a;
    struct rk_slot_info slot_b;
    struct rk_slot_info *current_slot;
    char current;

    current = get_current_slot();
    if (current != 'a' && current != 'b') {
        fprintf(stderr, "failed to detect current slot, refuse to mark successful\n");
        return 1;
    }

    if (metadata_load_primary(&meta) != 0) {
        return 1;
    }

    metadata_get_slots(&meta, &slot_a, &slot_b);
    slot_normalize(&slot_a);
    slot_normalize(&slot_b);

    current_slot = (current == 'a') ? &slot_a : &slot_b;

    printf("mark-successful current slot: %c\n", current);
    printf("Before write:\n");
    print_slot_info("Slot A", &slot_a);
    print_slot_info("Slot B", &slot_b);
    printf("\n");

    if (!slot_is_bootable(current_slot)) {
        fprintf(stderr, "current slot is not bootable, refuse to mark successful\n");
        return 1;
    }

    current_slot->tries_remaining = 0;
    current_slot->successful_boot = 1;
    current_slot->verity_corrupted = 0;

    metadata_put_slots(&meta, &slot_a, &slot_b);

    if (metadata_save_primary(&meta) != 0) {
        return 1;
    }

    printf("After:\n");
    print_slot_info(current == 'a' ? "Current Slot A" : "Current Slot B",
                    current_slot);
    printf("\nmark-successful: ok\n");

    return 0;
}

static int cmd_set_active(char target)
{
    struct rk_ab_metadata meta;
    struct rk_slot_info slot_a;
    struct rk_slot_info slot_b;
    struct rk_slot_info *target_slot;
    struct rk_slot_info *other_slot;

    if (target != 'a' && target != 'b') {
        fprintf(stderr, "invalid target slot: %c, expected a or b\n", target);
        return 1;
    }

    if (metadata_load_primary(&meta) != 0) {
        return 1;
    }

    metadata_get_slots(&meta, &slot_a, &slot_b);
    slot_normalize(&slot_a);
    slot_normalize(&slot_b);

    target_slot = (target == 'a') ? &slot_a : &slot_b;
    other_slot = (target == 'a') ? &slot_b : &slot_a;

    printf("set-active target slot: %c\n", target);
    printf("Before:\n");
    print_slot_info("Slot A", &slot_a);
    print_slot_info("Slot B", &slot_b);
    printf("\n");

    target_slot->priority = RK_AB_MAX_PRIORITY;
    target_slot->tries_remaining = RK_AB_MAX_TRIES;
    target_slot->successful_boot = 0;
    target_slot->verity_corrupted = 0;

    if (other_slot->priority == RK_AB_MAX_PRIORITY) {
        other_slot->priority = RK_AB_MAX_PRIORITY - 1;
    }

    metadata_put_slots(&meta, &slot_a, &slot_b);

    if (metadata_save_primary(&meta) != 0) {
        return 1;
    }

    printf("After:\n");
    print_slot_info(target == 'a' ? "Target Slot A" : "Target Slot B",
                    target_slot);
    print_slot_info(target == 'a' ? "Other Slot B" : "Other Slot A",
                    other_slot);
    printf("\nset-active %c: ok\n", target);

    return 0;
}

/*
 * 开发调试命令：把指定槽位固定为稳定态。
 *
 * 用途：
 *   驱动/外设调试时，如果希望 RESET 后仍固定从 A 启动：
 *
 *     ota_slotctl make-stable a
 *     sync
 *
 * 若要彻底避免回滚到 B，可再执行：
 *
 *     ota_slotctl disable-slot b
 */
static int cmd_make_stable(char slot)
{
    struct rk_ab_metadata meta;
    struct rk_slot_info slot_a;
    struct rk_slot_info slot_b;
    struct rk_slot_info *stable_slot;
    struct rk_slot_info *other_slot;

    if (slot != 'a' && slot != 'b') {
        fprintf(stderr, "invalid slot: %c, expected a or b\n", slot);
        return 1;
    }

    if (metadata_load_primary(&meta) != 0) {
        return 1;
    }

    metadata_get_slots(&meta, &slot_a, &slot_b);
    slot_normalize(&slot_a);
    slot_normalize(&slot_b);

    stable_slot = (slot == 'a') ? &slot_a : &slot_b;
    other_slot = (slot == 'a') ? &slot_b : &slot_a;

    stable_slot->priority = RK_AB_MAX_PRIORITY;
    stable_slot->tries_remaining = 0;
    stable_slot->successful_boot = 1;
    stable_slot->verity_corrupted = 0;

    if (other_slot->priority >= RK_AB_MAX_PRIORITY) {
        other_slot->priority = RK_AB_MAX_PRIORITY - 1;
    }

    metadata_put_slots(&meta, &slot_a, &slot_b);

    if (metadata_save_primary(&meta) != 0) {
        return 1;
    }

    printf("make-stable %c: ok\n", slot);
    print_slot_info(slot == 'a' ? "Stable Slot A" : "Stable Slot B",
                    stable_slot);

    return 0;
}

static int cmd_disable_slot(char slot)
{
    struct rk_ab_metadata meta;
    struct rk_slot_info slot_a;
    struct rk_slot_info slot_b;
    struct rk_slot_info *target_slot;

    if (slot != 'a' && slot != 'b') {
        fprintf(stderr, "invalid slot: %c, expected a or b\n", slot);
        return 1;
    }

    if (metadata_load_primary(&meta) != 0) {
        return 1;
    }

    metadata_get_slots(&meta, &slot_a, &slot_b);

    target_slot = (slot == 'a') ? &slot_a : &slot_b;
    slot_set_unbootable(target_slot);

    metadata_put_slots(&meta, &slot_a, &slot_b);

    if (metadata_save_primary(&meta) != 0) {
        return 1;
    }

    printf("disable-slot %c: ok\n", slot);

    return 0;
}

/* ------------------------------------------------------------------------- */
/* main                                                                      */
/* ------------------------------------------------------------------------- */

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s get-current\n"
            "  %s get-inactive\n"
            "  %s dump-map\n"
            "  %s dump-raw\n"
            "  %s dump\n"
            "  %s mark-successful\n"
            "  %s set-active a|b\n"
            "  %s make-stable a|b\n"
            "  %s disable-slot a|b\n",
            prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

int main(int argc, char *argv[])
{
    char current;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    current = get_current_slot();

    if (strcmp(argv[1], "get-current") == 0) {
        if (argc != 2) {
            print_usage(argv[0]);
            return 1;
        }

        if (!current) {
            fprintf(stderr, "failed to detect current slot\n");
            return 2;
        }

        printf("%c\n", current);
        return 0;
    }

    if (strcmp(argv[1], "get-inactive") == 0) {
        if (argc != 2) {
            print_usage(argv[0]);
            return 1;
        }

        if (!current) {
            fprintf(stderr, "failed to detect current slot\n");
            return 2;
        }

        printf("%c\n", current == 'a' ? 'b' : 'a');
        return 0;
    }

    if (strcmp(argv[1], "dump-map") == 0) {
        if (argc != 2) {
            print_usage(argv[0]);
            return 1;
        }

        cmd_dump_map();
        return 0;
    }

    if (strcmp(argv[1], "dump-raw") == 0) {
        if (argc != 2) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_dump_raw();
    }

    if (strcmp(argv[1], "dump") == 0) {
        if (argc != 2) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_dump();
    }

    if (strcmp(argv[1], "mark-successful") == 0) {
        if (argc != 2) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_mark_successful();
    }

    if (strcmp(argv[1], "set-active") == 0) {
        if (argc != 3) {
            print_usage(argv[0]);
            return 1;
        }

        if (strcmp(argv[2], "a") == 0) {
            return cmd_set_active('a');
        }

        if (strcmp(argv[2], "b") == 0) {
            return cmd_set_active('b');
        }

        fprintf(stderr, "invalid slot: %s, expected a or b\n", argv[2]);
        return 1;
    }

    if (strcmp(argv[1], "make-stable") == 0) {
        if (argc != 3) {
            print_usage(argv[0]);
            return 1;
        }

        if (strcmp(argv[2], "a") == 0) {
            return cmd_make_stable('a');
        }

        if (strcmp(argv[2], "b") == 0) {
            return cmd_make_stable('b');
        }

        fprintf(stderr, "invalid slot: %s, expected a or b\n", argv[2]);
        return 1;
    }

    if (strcmp(argv[1], "disable-slot") == 0) {
        if (argc != 3) {
            print_usage(argv[0]);
            return 1;
        }

        if (strcmp(argv[2], "a") == 0) {
            return cmd_disable_slot('a');
        }

        if (strcmp(argv[2], "b") == 0) {
            return cmd_disable_slot('b');
        }

        fprintf(stderr, "invalid slot: %s, expected a or b\n", argv[2]);
        return 1;
    }

    print_usage(argv[0]);
    return 1;
}
