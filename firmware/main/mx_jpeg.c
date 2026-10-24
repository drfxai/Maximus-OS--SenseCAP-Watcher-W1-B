#include "mx_jpeg.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"

static const char *TAG = "jpeg";

void *mx_jpeg_alloc(size_t bytes)
{
    return heap_caps_aligned_alloc(16, (bytes + 15) & ~15u, MALLOC_CAP_SPIRAM);
}

void mx_jpeg_free(void *p) { heap_caps_free(p); }

static bool parse(const uint8_t *jpeg, size_t len, jpeg_dec_handle_t **dec, jpeg_dec_io_t *io, jpeg_dec_header_info_t *info)
{
    jpeg_dec_config_t cfg = { .output_type = JPEG_RAW_TYPE_RGB565_BE, .rotate = JPEG_ROTATE_0D };
    *dec = jpeg_dec_open(&cfg);
    if (!*dec) return false;
    memset(io, 0, sizeof *io);
    io->inbuf = (uint8_t *)jpeg;
    io->inbuf_len = (int)len;
    if (jpeg_dec_parse_header(*dec, io, info) != JPEG_ERR_OK || info->width <= 0 || info->height <= 0) {
        jpeg_dec_close(*dec);
        *dec = NULL;
        return false;
    }
    return true;
}

bool mx_jpeg_size(const uint8_t *jpeg, size_t len, int *w, int *h)
{
    jpeg_dec_handle_t *dec;
    jpeg_dec_io_t io;
    jpeg_dec_header_info_t *info = calloc(1, sizeof *info);
    if (!info) return false;
    bool ok = parse(jpeg, len, &dec, &io, info);
    if (ok) {
        if (w) *w = info->width;
        if (h) *h = info->height;
        jpeg_dec_close(dec);
    }
    free(info);
    return ok;
}

bool mx_jpeg_decode(const uint8_t *jpeg, size_t len, uint8_t *out, size_t out_cap, int *w, int *h)
{
    jpeg_dec_handle_t *dec;
    jpeg_dec_io_t io;
    jpeg_dec_header_info_t *info = calloc(1, sizeof *info);
    if (!info) return false;
    bool ok = parse(jpeg, len, &dec, &io, info);
    if (!ok) {
        free(info);
        return false;
    }
    size_t need = (size_t)info->width * info->height * 2;
    if (need > out_cap) {
        ESP_LOGW(TAG, "picture %dx%d does not fit the buffer", info->width, info->height);
        ok = false;
    } else {
        int used = io.inbuf_len - io.inbuf_remain;
        io.outbuf = out;
        io.inbuf = (uint8_t *)jpeg + used;
        io.inbuf_len = io.inbuf_remain;
        ok = jpeg_dec_process(dec, &io) == JPEG_ERR_OK;
        if (ok) {
            if (w) *w = info->width;
            if (h) *h = info->height;
        }
    }
    jpeg_dec_close(dec);
    free(info);
    return ok;
}

/* RGB565 big-endian (byte-swapped) pixel helpers */
static inline void unpack(uint16_t v, int *r, int *g, int *b)
{
    v = (uint16_t)((v >> 8) | (v << 8));
    *r = (v >> 11) & 31;
    *g = (v >> 5) & 63;
    *b = v & 31;
}

static inline uint16_t pack(int r, int g, int b)
{
    uint16_t v = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((v >> 8) | (v << 8));
}

bool mx_jpeg_load_fit(const uint8_t *jpeg, size_t len, int max_w, int max_h, uint16_t **out, int *w, int *h)
{
    int sw = 0, sh = 0;
    if (!mx_jpeg_size(jpeg, len, &sw, &sh)) return false;
    uint8_t *full = mx_jpeg_alloc((size_t)sw * sh * 2);
    if (!full) return false;
    if (!mx_jpeg_decode(jpeg, len, full, (size_t)sw * sh * 2, &sw, &sh)) {
        mx_jpeg_free(full);
        return false;
    }
    if (sw <= max_w && sh <= max_h) {
        *out = (uint16_t *)full;
        *w = sw;
        *h = sh;
        return true;
    }
    int ow, oh;
    if ((int64_t)sw * max_h >= (int64_t)sh * max_w) {
        ow = max_w;
        oh = (int)((int64_t)sh * max_w / sw);
    } else {
        oh = max_h;
        ow = (int)((int64_t)sw * max_h / sh);
    }
    if (ow < 1) ow = 1;
    if (oh < 1) oh = 1;
    uint16_t *dst = mx_jpeg_alloc((size_t)ow * oh * 2);
    if (!dst) {
        mx_jpeg_free(full);
        return false;
    }
    const uint16_t *src = (const uint16_t *)full;
    for (int oy = 0; oy < oh; oy++) {
        int y0 = (int)((int64_t)oy * sh / oh), y1 = (int)((int64_t)(oy + 1) * sh / oh);
        if (y1 <= y0) y1 = y0 + 1;
        for (int ox = 0; ox < ow; ox++) {
            int x0 = (int)((int64_t)ox * sw / ow), x1 = (int)((int64_t)(ox + 1) * sw / ow);
            if (x1 <= x0) x1 = x0 + 1;
            int r = 0, g = 0, b = 0, n = 0;
            for (int y = y0; y < y1; y++) {
                const uint16_t *row = src + (size_t)y * sw;
                for (int x = x0; x < x1; x++) {
                    int pr, pg, pb;
                    unpack(row[x], &pr, &pg, &pb);
                    r += pr; g += pg; b += pb; n++;
                }
            }
            dst[(size_t)oy * ow + ox] = pack((r + n / 2) / n, (g + n / 2) / n, (b + n / 2) / n);
        }
    }
    mx_jpeg_free(full);
    *out = dst;
    *w = ow;
    *h = oh;
    return true;
}
