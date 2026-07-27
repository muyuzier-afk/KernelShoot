/*
 * KernelShoot - jpeg_encoder.c
 * RAW -> JPEG 内存到内存压缩 (libjpeg-turbo)
 *
 * tjCompress2:
 *   src 指向像素, pitch = stride (字节/行, 可大于 width*bpp)
 *   pixelFormat = TJPF_* (来自 ks_frame_t.tj_pf)
 *   flags = TJFLAG_FASTDCT (速度优先)
 *   quality = 95 (画质优先, 文件仍远小于 PNG)
 *
 * 输出 JPEG 在堆, 调用方 free
 */
#include "ks_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <turbojpeg.h>

int ks_jpeg_encode(const ks_frame_t *f, int quality,
                   uint8_t **out, size_t *outlen) {
    if (!f || !f->data || !out || !outlen) return KS_ERR_JPEG;
    *out = NULL;
    *outlen = 0;

    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    /* 子采样: RGB565/24bit 用 4:4:4, 32bit 用 4:2:2 兼顾画质与体积 */
    int subsamp = (f->bytes_per_pixel >= 4) ? TJSAMP_422 : TJSAMP_444;

    tjhandle h = tjInitCompress();
    if (!h) {
        KS_LOGE("tjInitCompress failed");
        return KS_ERR_JPEG;
    }

    unsigned char *jpg = NULL;
    unsigned long jpg_sz = 0;
    int flags = TJFLAG_FASTDCT;

    int rc = tjCompress2(h,
                         f->data,
                         (int)f->width,
                         (int)f->stride,   /* pitch: 每行字节数 */
                         (int)f->height,
                         f->tj_pf,         /* TJPF_* */
                         &jpg,
                         &jpg_sz,
                         subsamp,
                         quality,
                         flags);
    if (rc != 0) {
        KS_LOGE("tjCompress2 failed: %s", tjGetErrorStr2(h));
        tjDestroy(h);
        if (jpg) tjFree(jpg);
        return KS_ERR_JPEG;
    }
    tjDestroy(h);

    *out = (uint8_t *)jpg;     /* libjpeg-turbo 用 tjFree 释放, 但底层是 free/tjFree 一致 */
    *outlen = (size_t)jpg_sz;

    KS_LOGI("jpeg encoded: %ux%u -> %zu bytes (q=%d, subsamp=%d)",
            f->width, f->height, *outlen, quality, subsamp);
    return KS_OK;
}

/* 注意: tjFree 与 free 在 libjpeg-turbo 中等价 (内部即 free),
 * 因此调用方 file_writer 写完后用 free() 释放即可; 这里保持 tjFree 语义. */
