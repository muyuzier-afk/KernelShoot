/*
 * KernelShoot - fb_screenshot.c
 * 通过 /dev/graphics/fb0 直接读内核 framebuffer, mmap 零拷贝
 *
 * 流程:
 *   open /dev/graphics/fb0 (O_RDONLY)
 *   FBIOGET_VSCREENINFO  -> 宽高/像素位深/RGB 偏移
 *   FBIOGET_FSCREENINFO  -> 行长度 line_length / 显存总长 smem_len
 *   mmap 整个 smem_len
 *   memcpy 一帧到堆 (此后立即 munmap, 释放显存映射)
 *   根据 bits_per_pixel + RGB 偏移映射 libjpeg-turbo 像素格式
 *
 * 注意: fb0 在现代 DRM-only 设备可能不存在, 此时返回 KS_ERR_NO_FB
 */
#include "ks_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

/* 根据 vinfo 推断 libjpeg-turbo 像素格式 (TJPF_*) */
static int detect_tj_pf(const struct fb_var_screeninfo *v) {
    if (v->bits_per_pixel == 16 &&
        v->red.length == 5 && v->green.length == 6 && v->blue.length == 5) {
        return 2; /* TJPF_RGB565 */
    }
    if (v->bits_per_pixel == 32) {
        /* 内存字节序: offset 小的字节排在低地址
         * red.offset==0  -> 内存为 R G B X -> TJPF_RGBA(0) / 实际 XBGR? 取 RGBA
         * blue.offset==0 -> 内存为 B G R X -> TJPF_BGRA(1) */
        if (v->red.offset == 0)  return 0; /* TJPF_RGBA */
        if (v->blue.offset == 0) return 1; /* TJPF_BGRA */
        /* 默认按 RGBA */
        return 0;
    }
    if (v->bits_per_pixel == 24) {
        if (v->red.offset == 0)  return 3; /* TJPF_RGB */
        if (v->blue.offset == 0) return 4; /* TJPF_BGR */
    }
    return -1; /* 未知 */
}

int ks_fb_capture(ks_frame_t *out) {
    memset(out, 0, sizeof(*out));

    int fd = open("/dev/graphics/fb0", O_RDONLY);
    if (fd < 0) {
        KS_LOGW("open /dev/graphics/fb0 failed: %s", strerror(errno));
        return KS_ERR_NO_FB;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        KS_LOGE("FBIOGET_VSCREENINFO failed: %s", strerror(errno));
        close(fd);
        return KS_ERR_FB_IOCTL;
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        KS_LOGE("FBIOGET_FSCREENINFO failed: %s", strerror(errno));
        close(fd);
        return KS_ERR_FB_IOCTL;
    }

    int tj_pf = detect_tj_pf(&vinfo);
    if (tj_pf < 0) {
        KS_LOGE("unsupported fb format: bpp=%d r<%d,%d> g<%d,%d> b<%d,%d>",
                vinfo.bits_per_pixel,
                vinfo.red.offset, vinfo.red.length,
                vinfo.green.offset, vinfo.green.length,
                vinfo.blue.offset, vinfo.blue.length);
        close(fd);
        return KS_ERR_FB_IOCTL;
    }

    size_t stride = finfo.line_length;
    size_t map_len = finfo.smem_len;
    uint32_t w = vinfo.xres;
    uint32_t h = vinfo.yres;

    if (map_len == 0 || stride == 0 || w == 0 || h == 0) {
        KS_LOGE("invalid fb geometry: %ux%u stride=%zu smem=%zu",
                w, h, stride, map_len);
        close(fd);
        return KS_ERR_FB_IOCTL;
    }

    void *map = mmap(NULL, map_len, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        KS_LOGE("mmap fb0 failed: %s", strerror(errno));
        close(fd);
        return KS_ERR_FB_MMAP;
    }

    /* 拷贝一帧到堆 (stride * h), 立即 munmap 释放显存映射 */
    size_t copy_len = stride * h;
    uint8_t *buf = (uint8_t *)malloc(copy_len);
    if (!buf) {
        munmap(map, map_len);
        close(fd);
        return KS_ERR_OOM;
    }
    memcpy(buf, map, copy_len);
    munmap(map, map_len);
    close(fd);

    out->width  = w;
    out->height = h;
    out->stride = (uint32_t)stride;
    out->bytes_per_pixel = vinfo.bits_per_pixel / 8;
    out->data   = buf;
    out->tj_pf  = tj_pf;

    KS_LOGI("fb0 captured: %ux%u bpp=%u stride=%u tjpf=%d",
            w, h, out->bytes_per_pixel * 8, (uint32_t)stride, tj_pf);
    return KS_OK;
}
