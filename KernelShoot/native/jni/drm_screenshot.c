/*
 * KernelShoot - drm_screenshot.c
 * DRM 回退截图: 读取主 CRTC 当前 framebuffer 内容
 *
 * 适用场景: 设备无 /dev/graphics/fb0 (纯 DRM 驱动)
 *
 * 流程 (libdrm Mode API):
 *   1. open /dev/dri/card0
 *   2. drmModeGetResources -> connectors/crtcs
 *   3. 遍历 connector 找 connected + 有 mode + 有 encoder/crtc 且 CRTC 启用
 *   4. drmModeGetCrtc -> buffer_id (当前显示的 FB)
 *   5. drmModeGetFB(buffer_id) -> width/height/pitch/bpp/handle
 *   6. drmModeMapDumb(handle) -> mmap offset
 *   7. mmap drm fd, memcpy 一帧到堆
 *
 * 像素格式: Android ARM 上 32bpp 几乎都是 DRM_FORMAT_XRGB8888,
 *   内存字节序为 B G R X, 对应 TJPF_BGRA (alpha 通道被 JPEG 忽略无影响)
 *
 * 需要 root (DRM_MASTER 或 CAP_SYS_ADMIN) 才能 drmModeGetFB 他人 buffer
 */
#include "ks_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

int ks_drm_capture(ks_frame_t *out) {
    memset(out, 0, sizeof(*out));

    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        KS_LOGW("open /dev/dri/card0 failed: %s", strerror(errno));
        return KS_ERR_NO_DRM;
    }

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        KS_LOGE("drmModeGetResources failed: %s", strerror(errno));
        close(fd);
        return KS_ERR_DRM;
    }

    uint32_t fb_id = 0;
    /* 遍历 connector 找启用中的 CRTC */
    for (int i = 0; i < res->count_connectors && fb_id == 0; i++) {
        drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
        if (!conn) continue;
        if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
            drmModeFreeConnector(conn);
            continue;
        }
        drmModeEncoder *enc = NULL;
        if (conn->encoder_id) enc = drmModeGetEncoder(fd, conn->encoder_id);
        uint32_t crtc_id = 0;
        if (enc) {
            crtc_id = enc->crtc_id;
            drmModeFreeEncoder(enc);
        }
        if (crtc_id == 0 && conn->count_encoders > 0) {
            /* 退化: 取第一个 encoder 的可能 crtc */
            drmModeEncoder *e0 = drmModeGetEncoder(fd, conn->encoders[0]);
            if (e0) {
                /* 选 res 中匹配的 crtc */
                for (int c = 0; c < res->count_crtcs; c++) {
                    if (e0->possible_crtcs & (1 << c)) {
                        crtc_id = res->crtcs[c];
                        break;
                    }
                }
                drmModeFreeEncoder(e0);
            }
        }
        if (crtc_id) {
            drmModeCrtc *crtc = drmModeGetCrtc(fd, crtc_id);
            if (crtc) {
                if (crtc->mode_valid && crtc->buffer_id) {
                    fb_id = crtc->buffer_id;
                }
                drmModeFreeCrtc(crtc);
            }
        }
        drmModeFreeConnector(conn);
    }
    drmModeFreeResources(res);

    if (fb_id == 0) {
        KS_LOGE("no active crtc/fb found");
        close(fd);
        return KS_ERR_DRM;
    }

    drmModeFB *fb = drmModeGetFB(fd, fb_id);
    if (!fb) {
        KS_LOGE("drmModeGetFB(%u) failed: %s (need DRM_MASTER/root)",
                fb_id, strerror(errno));
        close(fd);
        return KS_ERR_DRM;
    }

    /* 通过 dumb buffer ioctl 拿 mmap offset */
    struct drm_mode_map_dumb mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = fb->handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        KS_LOGE("DRM_IOCTL_MODE_MAP_DUMB failed: %s", strerror(errno));
        drmModeFreeFB(fb);
        close(fd);
        return KS_ERR_DRM;
    }

    size_t map_len = (size_t)fb->pitch * fb->height;
    void *map = mmap(NULL, map_len, PROT_READ, MAP_SHARED, fd, mreq.offset);
    if (map == MAP_FAILED) {
        KS_LOGE("drm mmap failed: %s", strerror(errno));
        drmModeFreeFB(fb);
        close(fd);
        return KS_ERR_DRM;
    }

    uint8_t *buf = (uint8_t *)malloc(map_len);
    if (!buf) {
        munmap(map, map_len);
        drmModeFreeFB(fb);
        close(fd);
        return KS_ERR_OOM;
    }
    memcpy(buf, map, map_len);
    munmap(map, map_len);

    out->width  = fb->width;
    out->height = fb->height;
    out->stride = fb->pitch;
    out->bytes_per_pixel = fb->bpp / 8;
    out->data   = buf;
    /* Android DRM 默认 XRGB8888, 内存 BGRX -> 用 BGRA 给 libjpeg (alpha 忽略) */
    out->tj_pf  = 1; /* TJPF_BGRA */

    KS_LOGI("drm captured: %ux%u bpp=%u pitch=%u",
            fb->width, fb->height, fb->bpp, fb->pitch);

    drmModeFreeFB(fb);
    close(fd);
    return KS_OK;
}
