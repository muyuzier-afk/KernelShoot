/*
 * KernelShoot - ks_internal.h
 * 守护进程内部模块 API 声明 (单二进制, 内部链接)
 */
#ifndef KERNELSHOOT_INTERNAL_H
#define KERNELSHOOT_INTERNAL_H

#include "common.h"

/* ---- daemon_init.c ---- */
int ks_daemonize(void);

/* ---- socket_server.c ---- */
int  ks_server_create(void);
void ks_server_loop(int listen_fd,
                    int (*handler)(int client_fd, const char *req, size_t req_len));
int  ks_client_connect(void);
int  ks_server_send_resp(int fd, const char *json, size_t json_len);
int  ks_client_request(const char *req_json, size_t req_len,
                       char **resp_out, size_t *resp_len_out);

/* ---- foreground_detector.c ---- */
/* 读取 /proc 找出前台包名, 写入 out (NUL 结束).
 * 成功返回 0, 失败返回 -1.
 * 策略: 遍历 /proc/[pid]/oom_score_adj, 取值为 0 且 cmdline 形如包名者 */
int ks_get_foreground_package(char *out, size_t outlen);

/* ---- fb_screenshot.c ---- */
/* 打开 /dev/graphics/fb0, ioctl 取宽高像素格式, mmap, 拷贝到堆.
 * 成功填充 out->data 等, 调用方负责 ks_frame_free().
 * 返回 KS_OK 或 KS_ERR_FB_* */
int ks_fb_capture(ks_frame_t *out);

/* ---- drm_screenshot.c ---- */
/* DRM 回退: 打开 /dev/dri/card0, 取主 connector, dump dumb buffer.
 * 成功返回 KS_OK, 否则 KS_ERR_NO_DRM / KS_ERR_DRM */
int ks_drm_capture(ks_frame_t *out);

/* ---- jpeg_encoder.c ---- */
/* RAW -> JPEG 内存到内存压缩.
 * quality: 1-100, 推荐 95
 * 成功返回 0, *out 指向 malloc 的 JPEG (调用方 free), *outlen 为长度 */
int ks_jpeg_encode(const ks_frame_t *f, int quality,
                   uint8_t **out, size_t *outlen);

/* ---- file_writer.c ---- */
/* 将 JPEG 写入 dir 目录, 文件名 {timestamp}_{pkg}.jpg
 * 成功把绝对路径写入 path_out, 返回 0; 失败返回 KS_ERR_FILE_WRITE */
int ks_write_jpeg(const char *dir, const char *pkg,
                  const uint8_t *jpg, size_t len,
                  char *path_out, size_t path_len);

#endif /* KERNELSHOOT_INTERNAL_H */
