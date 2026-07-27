/*
 * KernelShoot - util.c
 * 工具函数实现
 */
#include "common.h"

#include <time.h>
#include <string.h>
#include <stdlib.h>

int64_t ks_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void ks_timestamp_str(char *buf, size_t buflen) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, buflen, "%Y%m%d_%H%M%S", &tm);
}

void ks_frame_free(ks_frame_t *f) {
    if (!f) return;
    if (f->data) {
        free(f->data);
        f->data = NULL;
    }
    f->width = f->height = f->stride = f->bytes_per_pixel = 0;
}
