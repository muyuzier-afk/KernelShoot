/*
 * KernelShoot - common.h
 * 公共定义：日志、Socket 抽象命名空间、协议、错误码
 */
#ifndef KERNELSHOOT_COMMON_H
#define KERNELSHOOT_COMMON_H

#include <stddef.h>
#include <stdint.h>

/* ---------- 日志 ---------- */
#define KS_TAG "KernelShoot"
#ifdef ANDROID
#include <android/log.h>
#define KS_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  KS_TAG, __VA_ARGS__)
#define KS_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  KS_TAG, __VA_ARGS__)
#define KS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, KS_TAG, __VA_ARGS__)
#define KS_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, KS_TAG, __VA_ARGS__)
#else
#include <stdio.h>
#define KS_LOGI(...) do { fprintf(stderr, "[I/%s] ", KS_TAG); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#define KS_LOGW(...) do { fprintf(stderr, "[W/%s] ", KS_TAG); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#define KS_LOGE(...) do { fprintf(stderr, "[E/%s] ", KS_TAG); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#define KS_LOGD(...) do { fprintf(stderr, "[D/%s] ", KS_TAG); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#endif

/* ---------- LocalSocket 抽象命名空间 ---------- */
/* Linux 抽象命名空间: sun_path[0]='\0' 后跟名字 (无 \0 终止) */
#define KS_SOCKET_NAME "kernelshoot.daemon"

/* ---------- 通信协议 ---------- */
/* 请求 JSON:  {"cmd":"screenshot"}                      */
/* 响应 JSON:  {"code":0,"msg":"ok","path":"/sdcard/..."}  */
/*            {"code":<err>,"msg":"<reason>","path":""}    */
#define KS_CMD_SCREENSHOT "screenshot"

#define KS_REQ_MAX_LEN   512
#define KS_RESP_MAX_LEN  1024

/* ---------- 输出 ---------- */
#define KS_OUTPUT_DIR    "/sdcard/DCIM"
#define KS_OUTPUT_PREFIX ""  /* 文件名: {timestamp}_{pkg}.jpg */

/* ---------- 错误码 ---------- */
#define KS_OK                 0
#define KS_ERR_SOCKET         1
#define KS_ERR_NO_FB          2
#define KS_ERR_FB_IOCTL       3
#define KS_ERR_FB_MMAP        4
#define KS_ERR_NO_DRM         5
#define KS_ERR_DRM            6
#define KS_ERR_JPEG           7
#define KS_ERR_FILE_WRITE     8
#define KS_ERR_NO_FOREGROUND  9
#define KS_ERR_OOM            10
#define KS_ERR_BAD_REQ        11

/* ---------- 像素缓冲 ---------- */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;   /* 2 (RGB565) / 4 (RGBA8888/BGRA8888) */
    uint32_t stride;            /* bytes per row */
    uint8_t *data;              /* RAW 像素, size = stride * height */
    /* libjpeg-turbo 像素格式标识, 见 jpeg_encoder.c 映射 */
    int      tj_pf;             /* TJPF_* */
} ks_frame_t;

void ks_frame_free(ks_frame_t *f);

/* ---------- 工具函数 ---------- */
/* 当前毫秒时间戳 */
int64_t ks_now_ms(void);

/* 生成时间戳字符串 YYYYMMDD_HHMMSS */
void ks_timestamp_str(char *buf, size_t buflen);

#endif /* KERNELSHOOT_COMMON_H */
