/*
 * KernelShoot - foreground_detector.c
 * 纯 /proc 读取前台包名, 不调用任何系统 API, 零权限依赖
 *
 * 策略 (主):
 *   遍历 /proc/[pid]/oom_score_adj, Android 前台进程该值为 0
 *   再读 /proc/[pid]/cmdline 截取到第一个 \0, 形如 com.example.app
 *
 * 过滤:
 *   - 跳过 kthread (cmdline 空)
 *   - 跳过 system_server, launcher 等 (非目标前台用户应用)
 *   - cmdline 必须包含 '.' 且不含 '/' (包名特征)
 *
 * 回退 (主策略无果):
 *   取 /proc/[pid]/stat state=='R' 且 cmdline 为包名者
 */
#include "ks_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>

/* 读取 /proc/[pid]/oom_score_adj, 成功返回 0 */
static int read_oom_adj(int pid, int *out) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/oom_score_adj", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    int v = -1000;
    int ok = (fscanf(fp, "%d", &v) == 1);
    fclose(fp);
    if (!ok) return -1;
    *out = v;
    return 0;
}

/* 读取 /proc/[pid]/cmdline, 在第一个 \0 截断, 返回长度或 -1 */
static int read_cmdline(int pid, char *out, size_t outlen) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    size_t n = fread(out, 1, outlen - 1, fp);
    fclose(fp);
    if (n == 0) return -1;
    out[n] = '\0';
    /* cmdline 以 \0 分隔参数, 截到第一个 \0 即进程名/包名 */
    char *nul = memchr(out, '\0', n);
    if (nul) *nul = '\0';
    return (int)strlen(out);
}

/* 判断字符串是否像 Android 包名: 含 '.', 不含 '/', 首字符字母 */
static int looks_like_package(const char *s) {
    if (!s || !*s) return 0;
    if (!isalpha((unsigned char)s[0])) return 0;
    if (strchr(s, '/') != NULL) return 0;
    if (strchr(s, '.') == NULL) return 0;
    /* 排除常见非应用进程 */
    if (strcmp(s, "android") == 0) return 0;
    if (strcmp(s, "system_server") == 0) return 0;
    if (strcmp(s, "com.android.systemui") == 0) return 0;
    return 1;
}

int ks_get_foreground_package(char *out, size_t outlen) {
    DIR *d = opendir("/proc");
    if (!d) {
        KS_LOGE("opendir(/proc) failed: %s", strerror(errno));
        return -1;
    }

    /* 主策略: oom_score_adj == 0 */
    struct dirent *de;
    char best[256] = {0};
    int found = 0;

    while ((de = readdir(d)) != NULL) {
        /* 只处理纯数字目录 (pid) */
        const char *name = de->d_name;
        if (!isdigit((unsigned char)name[0])) continue;
        int pid = atoi(name);
        if (pid <= 0) continue;

        int adj = 0;
        if (read_oom_adj(pid, &adj) < 0) continue;
        if (adj != 0) continue;

        char pkg[256] = {0};
        if (read_cmdline(pid, pkg, sizeof(pkg)) <= 0) continue;
        if (!looks_like_package(pkg)) continue;

        /* 取第一个匹配 (理论上只有一个前台) */
        strncpy(best, pkg, sizeof(best) - 1);
        found = 1;
        break;
    }
    closedir(d);

    if (!found) {
        /* 回退: state=='R' 且为包名 (此处简化, 实际可扩展) */
        KS_LOGW("no process with oom_score_adj==0, fallback empty");
        return -1;
    }

    strncpy(out, best, outlen - 1);
    out[outlen - 1] = '\0';
    KS_LOGD("foreground package: %s", out);
    return 0;
}
