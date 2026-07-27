/*
 * KernelShoot - file_writer.c
 * 将 JPEG 写入 /sdcard/DCIM/, 命名 {timestamp}_{pkg}.jpg
 * 写后 chmod 0644 确保相册/其他应用可读
 *
 * 不依赖 Android MediaStore API (守护进程是 native, 无 JVM)
 * 可选: 通过写 .nomedia 旁的文件让 MediaScanner 扫描 (系统会自动扫描 DCIM)
 */
#include "ks_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

int ks_write_jpeg(const char *dir, const char *pkg,
                  const uint8_t *jpg, size_t len,
                  char *path_out, size_t path_len) {
    if (!dir || !jpg || len == 0) return KS_ERR_FILE_WRITE;

    /* 确保目录存在 (root 可创建) */
    mkdir(dir, 0777);

    /* 时间戳 */
    char ts[32];
    ks_timestamp_str(ts, sizeof(ts));

    /* 包名清洗: 只保留 [A-Za-z0-9._], 其余换 _ */
    char safe_pkg[256];
    const char *p = pkg ? pkg : "unknown";
    size_t i = 0;
    for (; i < sizeof(safe_pkg) - 1 && p[i]; i++) {
        char c = p[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_') {
            safe_pkg[i] = c;
        } else {
            safe_pkg[i] = '_';
        }
    }
    safe_pkg[i] = '\0';

    char fname[300];
    snprintf(fname, sizeof(fname), "%s_%s.jpg", ts, safe_pkg);

    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, fname);

    int fd = open(fullpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        KS_LOGE("open(%s) failed: %s", fullpath, strerror(errno));
        return KS_ERR_FILE_WRITE;
    }

    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, jpg + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            KS_LOGE("write(%s) failed: %s", fullpath, strerror(errno));
            close(fd);
            unlink(fullpath);
            return KS_ERR_FILE_WRITE;
        }
        done += (size_t)n;
    }
    /* 确保落盘 */
    fsync(fd);
    close(fd);

    /* 确保权限 (相册/其他 app 可读) */
    chmod(fullpath, 0644);

    if (path_out && path_len) {
        strncpy(path_out, fullpath, path_len - 1);
        path_out[path_len - 1] = '\0';
    }

    KS_LOGI("jpeg written: %s (%zu bytes)", fullpath, len);
    return KS_OK;
}
