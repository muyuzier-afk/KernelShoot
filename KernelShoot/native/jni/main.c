/*
 * KernelShoot - main.c
 * 守护进程入口
 *
 * 职责:
 *   1. (可选) 守护进程化: 双重 fork + setsid
 *   2. 创建 LocalSocket server (抽象命名空间)
 *   3. 进入 epoll 事件循环, CPU 占用 0%
 *   4. 收到 {"cmd":"screenshot"} 后执行截图流水线:
 *        前台包名 -> fb0/DRM 抓帧 -> libjpeg-turbo 压缩 -> 写 DCIM -> 回响应
 *
 * 用法:
 *   KernelShoot_daemon              守护进程模式 (生产)
 *   KernelShoot_daemon -f           前台模式 (调试, 不 fork)
 */
#include "ks_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

/* ---- 极简 JSON 字段检测 (协议只有一个 cmd, 无需完整 parser) ---- */
static int req_is_screenshot(const char *req, size_t len) {
    /* 查找 "cmd" 之后是否出现 "screenshot" */
    const char *end = req + len;
    const char *p = req;
    while (p < end) {
        const char *hit = memchr(p, '"', (size_t)(end - p));
        if (!hit) return 0;
        p = hit + 1;
        if (p + 3 <= end && memcmp(p, "cmd\"", 4) == 0) {
            /* 找到 "cmd", 跳到下一个字符串值 */
            const char *q = p + 4;
            while (q < end && *q != '"') q++;
            if (q >= end) return 0;
            q++; /* 跳过开引号 */
            if ((size_t)(end - q) >= strlen(KS_CMD_SCREENSHOT) &&
                memcmp(q, KS_CMD_SCREENSHOT, strlen(KS_CMD_SCREENSHOT)) == 0) {
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

/* 转义路径里的双引号/反斜杠, 写入 JSON 响应缓冲 */
static int build_response(char *buf, size_t buflen, int code,
                          const char *msg, const char *path) {
    char esc_path[1024] = {0};
    const char *src = path ? path : "";
    size_t j = 0;
    for (size_t i = 0; src[i] && j < sizeof(esc_path) - 2; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            if (j < sizeof(esc_path) - 2) esc_path[j++] = '\\';
        }
        esc_path[j++] = src[i];
    }
    return snprintf(buf, buflen,
                    "{\"code\":%d,\"msg\":\"%s\",\"path\":\"%s\"}",
                    code, msg ? msg : "", esc_path);
}

/* ---- 截图流水线 ---- */
static int do_screenshot(char *resp, size_t resp_len) {
    int64_t t0 = ks_now_ms();

    /* 1. 前台包名 (仅用于命名, 失败不致命) */
    char pkg[256] = {0};
    if (ks_get_foreground_package(pkg, sizeof(pkg)) < 0) {
        strncpy(pkg, "unknown", sizeof(pkg) - 1);
    }

    /* 2. 抓帧: fb0 优先, 失败回退 DRM */
    ks_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    int rc = ks_fb_capture(&frame);
    if (rc != KS_OK) {
        KS_LOGW("fb0 capture failed (code=%d), trying DRM fallback", rc);
        rc = ks_drm_capture(&frame);
        if (rc != KS_OK) {
            KS_LOGE("drm capture also failed (code=%d)", rc);
            return build_response(resp, resp_len, rc,
                                  "no framebuffer device available", "");
        }
    }

    /* 3. JPEG 压缩 */
    uint8_t *jpg = NULL;
    size_t jpg_len = 0;
    int erc = ks_jpeg_encode(&frame, 95, &jpg, &jpg_len);
    ks_frame_free(&frame);
    if (erc != KS_OK) {
        return build_response(resp, resp_len, erc, "jpeg encode failed", "");
    }

    /* 4. 写文件 */
    char path[1024] = {0};
    int wrc = ks_write_jpeg(KS_OUTPUT_DIR, pkg, jpg, jpg_len,
                            path, sizeof(path));
    free(jpg);
    if (wrc != KS_OK) {
        return build_response(resp, resp_len, wrc, "file write failed", "");
    }

    int64_t t1 = ks_now_ms();
    KS_LOGI("screenshot done in %lld ms", (long long)(t1 - t0));

    return build_response(resp, resp_len, KS_OK, "ok", path);
}

/* ---- 请求分发器 (传给 ks_server_loop) ---- */
static int handle_request(int client_fd, const char *req, size_t req_len) {
    char resp[KS_RESP_MAX_LEN];
    int resp_len;

    if (req_is_screenshot(req, req_len)) {
        resp_len = do_screenshot(resp, sizeof(resp));
    } else {
        resp_len = build_response(resp, sizeof(resp),
                                  KS_ERR_BAD_REQ, "unknown cmd", "");
    }

    if (resp_len > 0) {
        ks_server_send_resp(client_fd, resp, (size_t)resp_len);
    }
    return 0;
}

int main(int argc, char **argv) {
    int foreground = 0;
    int opt;
    while ((opt = getopt(argc, argv, "f")) != -1) {
        if (opt == 'f') foreground = 1;
    }

    KS_LOGI("KernelShoot daemon starting (foreground=%d)", foreground);

    if (!foreground) {
        if (ks_daemonize() < 0) {
            KS_LOGE("daemonize failed, exit");
            return 1;
        }
    }

    int listen_fd = ks_server_create();
    if (listen_fd < 0) {
        KS_LOGE("server create failed, exit");
        return 1;
    }

    /* 进入事件循环: 非截图期间 epoll_wait 阻塞, CPU 0% */
    ks_server_loop(listen_fd, handle_request);

    close(listen_fd);
    return 0;
}
