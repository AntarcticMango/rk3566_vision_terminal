/*
 * RK3566 TaishanPi Device Manager - Day 2 minimal version
 *
 * Goal:
 *   Aggregate system/network/vision/OTA/CAN status and print JSON.
 *
 * Build on board:
 *   gcc -O2 -Wall -Wextra -o device_manager device_manager.c
 *
 * Cross build in SDK/container:
 *   make CROSS_COMPILE=aarch64-buildroot-linux-gnu-
 * or:
 *   aarch64-linux-gnu-gcc -O2 -Wall -Wextra -o device_manager device_manager.c
 */
//adb push device_manager /usr/bin/device_manager
//adb shell chmod +x /usr/bin/device_manager
//
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUF_SZ 512
#define SMALL_SZ 128
#define LOG_PATH "/tmp/device_manager.log"
#define FW_VERSION_PATH "/etc/firmware_version"
#define OTA_STATUS_PATH "/tmp/ota_status"

static void trim(char *s)
{
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || isspace((unsigned char)s[len - 1]))) {
        s[--len] = '\0';
    }
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static bool path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int read_first_line(const char *path, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return -1;
    out[0] = '\0';

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    if (!fgets(out, (int)out_sz, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    trim(out);
    return 0;
}

static int run_cmd_first_line(const char *cmd, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return -1;
    out[0] = '\0';

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    if (!fgets(out, (int)out_sz, fp)) {
        pclose(fp);
        return -1;
    }
    int rc = pclose(fp);
    trim(out);
    return rc;
}

static void log_event(const char *fmt, ...)
{
    FILE *fp = fopen(LOG_PATH, "a");
    if (!fp) return;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    fprintf(fp, "[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fprintf(fp, "\n");
    fclose(fp);
}

static void get_uptime(char *out, size_t out_sz)
{
    char buf[SMALL_SZ];
    if (read_first_line("/proc/uptime", buf, sizeof(buf)) == 0) {
        double seconds = 0.0;
        if (sscanf(buf, "%lf", &seconds) == 1) {
            snprintf(out, out_sz, "%.0fs", seconds);
            return;
        }
    }
    snprintf(out, out_sz, "unknown");
}

static void get_memory(char *used_out, size_t used_sz, char *total_out, size_t total_sz)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    long mem_total = -1, mem_available = -1;
    char key[64], unit[32];
    long value = 0;

    if (fp) {
        while (fscanf(fp, "%63[^:]: %ld %31s\n", key, &value, unit) == 3) {
            if (strcmp(key, "MemTotal") == 0) mem_total = value;
            else if (strcmp(key, "MemAvailable") == 0) mem_available = value;
            if (mem_total >= 0 && mem_available >= 0) break;
        }
        fclose(fp);
    }

    if (mem_total >= 0) {
        long used = (mem_available >= 0) ? (mem_total - mem_available) : -1;
        if (used >= 0) snprintf(used_out, used_sz, "%ldMB", used / 1024);
        else snprintf(used_out, used_sz, "unknown");
        snprintf(total_out, total_sz, "%ldMB", mem_total / 1024);
    } else {
        snprintf(used_out, used_sz, "unknown");
        snprintf(total_out, total_sz, "unknown");
    }
}

static void get_cpu_temp(char *out, size_t out_sz)
{
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) {
        snprintf(out, out_sz, "unknown");
        return;
    }

    struct dirent *de;
    long best_temp = -1;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "thermal_zone", 12) != 0) continue;
        char path[512];
        char temp_str[SMALL_SZ];
        snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", de->d_name);
        if (read_first_line(path, temp_str, sizeof(temp_str)) == 0) {
            char *endp = NULL;
            long t = strtol(temp_str, &endp, 10);
            if (endp != temp_str) {
                /* choose the first plausible SoC temperature; if multiple, use max */
                if (best_temp < 0 || t > best_temp) best_temp = t;
            }
        }
    }
    closedir(dir);

    if (best_temp >= 1000) snprintf(out, out_sz, "%.1f", best_temp / 1000.0);
    else if (best_temp >= 0) snprintf(out, out_sz, "%ld", best_temp);
    else snprintf(out, out_sz, "unknown");
}

static void get_slot_from_cmdline(char *slot_out, size_t slot_sz)
{
    char cmdline[2048];
    if (read_first_line("/proc/cmdline", cmdline, sizeof(cmdline)) != 0) {
        snprintf(slot_out, slot_sz, "unknown");
        return;
    }

    const char *keys[] = {"androidboot.slot_suffix=", "android_slotsufix=", "androidboot.slot=", NULL};
    for (int i = 0; keys[i]; ++i) {
        char *p = strstr(cmdline, keys[i]);
        if (p) {
            p += strlen(keys[i]);
            char val[SMALL_SZ] = {0};
            size_t j = 0;
            while (*p && !isspace((unsigned char)*p) && j + 1 < sizeof(val)) val[j++] = *p++;
            val[j] = '\0';
            if (strcmp(keys[i], "androidboot.slot=") == 0 && val[0] != '_') {
                snprintf(slot_out, slot_sz, "_%s", val);
            } else {
                snprintf(slot_out, slot_sz, "%s", val);
            }
            return;
        }
    }

    snprintf(slot_out, slot_sz, "unknown");
}

static void get_rootfs(char *rootfs_out, size_t rootfs_sz)
{
    char line[BUF_SZ];
    char root_dev[SMALL_SZ] = {0};
    FILE *fp = fopen("/proc/mounts", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char dev[SMALL_SZ], mnt[SMALL_SZ], fs[SMALL_SZ];
            if (sscanf(line, "%127s %127s %127s", dev, mnt, fs) == 3) {
                if (strcmp(mnt, "/") == 0) {
                    snprintf(root_dev, sizeof(root_dev), "%s", dev);
                    break;
                }
            }
        }
        fclose(fp);
    }

    if (root_dev[0] == '\0') {
        snprintf(rootfs_out, rootfs_sz, "unknown");
        return;
    }

    DIR *dir = opendir("/dev/block/by-name");
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (de->d_name[0] == '.') continue;
            if (strncmp(de->d_name, "system_", 7) != 0 && strcmp(de->d_name, "rootfs") != 0) continue;

            char link_path[512];
            char resolved[512];
            snprintf(link_path, sizeof(link_path), "/dev/block/by-name/%s", de->d_name);
            if (realpath(link_path, resolved) && strcmp(resolved, root_dev) == 0) {
                size_t n = strlen(de->d_name);
                if (n >= rootfs_sz) n = rootfs_sz - 1;
                memcpy(rootfs_out, de->d_name, n);
                rootfs_out[n] = '\0';
                closedir(dir);
                return;
            }
        }
        closedir(dir);
    }

    /* fallback: infer from device name or boot slot */
    char slot[SMALL_SZ];
    get_slot_from_cmdline(slot, sizeof(slot));
    if (strcmp(slot, "_a") == 0) snprintf(rootfs_out, rootfs_sz, "system_a");
    else if (strcmp(slot, "_b") == 0) snprintf(rootfs_out, rootfs_sz, "system_b");
    else snprintf(rootfs_out, rootfs_sz, "%s", root_dev);
}

static void get_inactive_slot(const char *current, char *inactive, size_t inactive_sz)
{
    if (strcmp(current, "_a") == 0) snprintf(inactive, inactive_sz, "_b");
    else if (strcmp(current, "_b") == 0) snprintf(inactive, inactive_sz, "_a");
    else snprintf(inactive, inactive_sz, "unknown");
}

static void get_wlan0_ip(char *ip_out, size_t ip_sz, char *wifi_out, size_t wifi_sz)
{
    char ip[SMALL_SZ];
    int rc = run_cmd_first_line("ip -4 addr show wlan0 2>/dev/null | awk '/inet / {print $2}' | cut -d/ -f1 | head -n 1", ip, sizeof(ip));
    if (rc == 0 && ip[0] != '\0') {
        snprintf(ip_out, ip_sz, "%s", ip);
        snprintf(wifi_out, wifi_sz, "connected");
        return;
    }

    /* fallback for very small BusyBox images */
    rc = run_cmd_first_line("ifconfig wlan0 2>/dev/null | awk '/inet addr:/ {print $2}' | cut -d: -f2 | head -n 1", ip, sizeof(ip));
    if (rc == 0 && ip[0] != '\0') {
        snprintf(ip_out, ip_sz, "%s", ip);
        snprintf(wifi_out, wifi_sz, "connected");
        return;
    }

    if (path_exists("/sys/class/net/wlan0")) {
        snprintf(ip_out, ip_sz, "none");
        snprintf(wifi_out, wifi_sz, "disconnected");
    } else {
        snprintf(ip_out, ip_sz, "none");
        snprintf(wifi_out, wifi_sz, "missing");
    }
}

static bool any_video_node_exists(void)
{
    DIR *dir = opendir("/dev");
    if (!dir) return false;
    struct dirent *de;
    bool found = false;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "video", 5) == 0) {
            found = true;
            break;
        }
    }
    closedir(dir);
    return found;
}

static void get_camera_status(char *out, size_t out_sz)
{
    if (path_exists("/dev/video1") || any_video_node_exists()) snprintf(out, out_sz, "online");
    else snprintf(out, out_sz, "offline");
}

static void get_stream_status(char *out, size_t out_sz)
{
    char buf[SMALL_SZ];
    int rc = run_cmd_first_line("ps w 2>/dev/null | grep -E '[r]k_camera_preview_mplane|[g]st-launch-1.0|[m]pph264enc' | awk 'NR==1{print $1}'", buf, sizeof(buf));
    if (rc == 0 && buf[0] != '\0') snprintf(out, out_sz, "running");
    else snprintf(out, out_sz, "stopped");
}

static void get_ota_status(char *out, size_t out_sz)
{
    char buf[SMALL_SZ];
    if (read_first_line(OTA_STATUS_PATH, buf, sizeof(buf)) == 0 && buf[0] != '\0') {
        snprintf(out, out_sz, "%s", buf);
    } else {
        snprintf(out, out_sz, "idle");
    }
}

static void get_can_status(char *iface_out, size_t iface_sz, char *status_out, size_t status_sz)
{
    const char *candidates[] = {"can0", "vcan0", NULL};
    for (int i = 0; candidates[i]; ++i) {
        char path[512];
        snprintf(path, sizeof(path), "/sys/class/net/%s", candidates[i]);
        if (path_exists(path)) {
            snprintf(iface_out, iface_sz, "%s", candidates[i]);
            char oper_path[512], state[SMALL_SZ];
            snprintf(oper_path, sizeof(oper_path), "/sys/class/net/%s/operstate", candidates[i]);
            if (read_first_line(oper_path, state, sizeof(state)) == 0 && state[0] != '\0') {
                snprintf(status_out, status_sz, "%s", state);
            } else {
                snprintf(status_out, status_sz, "exists");
            }
            return;
        }
    }
    snprintf(iface_out, iface_sz, "can0");
    snprintf(status_out, status_sz, "reserved");
}

static void get_firmware_version(char *out, size_t out_sz)
{
    if (read_first_line(FW_VERSION_PATH, out, out_sz) == 0 && out[0] != '\0') return;
    snprintf(out, out_sz, "dev-day2");
}

static void print_status_json(void)
{
    char uptime[SMALL_SZ], temp[SMALL_SZ], mem_used[SMALL_SZ], mem_total[SMALL_SZ];
    char rootfs[SMALL_SZ], slot[SMALL_SZ], inactive[SMALL_SZ];
    char wlan_ip[SMALL_SZ], wifi[SMALL_SZ];
    char camera[SMALL_SZ], stream[SMALL_SZ], ota_status[SMALL_SZ];
    char can_iface[SMALL_SZ], can_status[SMALL_SZ], version[SMALL_SZ];

    get_uptime(uptime, sizeof(uptime));
    get_cpu_temp(temp, sizeof(temp));
    get_memory(mem_used, sizeof(mem_used), mem_total, sizeof(mem_total));
    get_rootfs(rootfs, sizeof(rootfs));
    get_slot_from_cmdline(slot, sizeof(slot));
    get_inactive_slot(slot, inactive, sizeof(inactive));
    get_wlan0_ip(wlan_ip, sizeof(wlan_ip), wifi, sizeof(wifi));
    get_camera_status(camera, sizeof(camera));
    get_stream_status(stream, sizeof(stream));
    get_ota_status(ota_status, sizeof(ota_status));
    get_can_status(can_iface, sizeof(can_iface), can_status, sizeof(can_status));
    get_firmware_version(version, sizeof(version));

    log_event("status queried: slot=%s rootfs=%s wifi=%s camera=%s stream=%s ota=%s can=%s/%s",
              slot, rootfs, wifi, camera, stream, ota_status, can_iface, can_status);

    printf("{\n");
    printf("  \"device\": \"RK3566-TaishanPi\",\n");
    printf("  \"version\": \"%s\",\n", version);
    printf("  \"system\": {\n");
    printf("    \"uptime\": \"%s\",\n", uptime);
    printf("    \"cpu_temp_c\": \"%s\",\n", temp);
    printf("    \"memory_used\": \"%s\",\n", mem_used);
    printf("    \"memory_total\": \"%s\",\n", mem_total);
    printf("    \"rootfs\": \"%s\"\n", rootfs);
    printf("  },\n");
    printf("  \"network\": {\n");
    printf("    \"wlan0_ip\": \"%s\",\n", wlan_ip);
    printf("    \"wifi\": \"%s\"\n", wifi);
    printf("  },\n");
    printf("  \"vision\": {\n");
    printf("    \"camera\": \"%s\",\n", camera);
    printf("    \"stream\": \"%s\"\n", stream);
    printf("  },\n");
    printf("  \"ota\": {\n");
    printf("    \"current_slot\": \"%s\",\n", slot);
    printf("    \"inactive_slot\": \"%s\",\n", inactive);
    printf("    \"status\": \"%s\",\n", ota_status);
    printf("    \"rollback_available\": %s\n", strcmp(inactive, "unknown") == 0 ? "false" : "true");
    printf("  },\n");
    printf("  \"can\": {\n");
    printf("    \"interface\": \"%s\",\n", can_iface);
    printf("    \"status\": \"%s\"\n", can_status);
    printf("  }\n");
    printf("}\n");
}

static void print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s --status      Print device status JSON\n", prog);
    printf("  %s --version     Print firmware version\n", prog);
    printf("  %s --help        Show this help\n", prog);
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "--status") == 0) {
        print_status_json();
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0) {
        char version[SMALL_SZ];
        get_firmware_version(version, sizeof(version));
        printf("%s\n", version);
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    fprintf(stderr, "unknown option: %s\n", argv[1]);
    print_usage(argv[0]);
    return 1;
}
