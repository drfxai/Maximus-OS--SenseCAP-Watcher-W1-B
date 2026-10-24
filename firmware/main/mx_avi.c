#include "mx_avi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "avi";

#define MAX_FRAMES 36000        /* one hour at 10 fps; the index costs 8 bytes a frame */
#define HDR_SIZE 224            /* RIFF header .. start of the movi data */

typedef struct {
    uint32_t off, size;
} idx_t;

struct mx_avi {
    FILE *f;
    int w, h;
    uint32_t frames, max_chunk;
    size_t movi_bytes;          /* bytes of chunk data after the 'movi' tag */
    idx_t *idx;
};

static void p32(uint8_t *b, uint32_t v) { b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }
static void p16(uint8_t *b, uint32_t v) { b[0] = v; b[1] = v >> 8; }
static void tag(uint8_t *b, const char *t) { memcpy(b, t, 4); }

static void make_header(uint8_t *h, int w, int hgt, uint32_t frames, uint32_t us_per_frame, uint32_t max_chunk, size_t movi_bytes, size_t idx_bytes)
{
    memset(h, 0, HDR_SIZE);
    uint8_t *p = h;
    uint32_t movi_list = 4 + (uint32_t)movi_bytes;
    tag(p, "RIFF"); p32(p + 4, (uint32_t)(HDR_SIZE - 8 + movi_bytes + idx_bytes)); tag(p + 8, "AVI "); p += 12;
    tag(p, "LIST"); p32(p + 4, 192); tag(p + 8, "hdrl"); p += 12;
    tag(p, "avih"); p32(p + 4, 56); p += 8;
    p32(p + 0, us_per_frame);
    p32(p + 4, us_per_frame ? (uint32_t)(1000000.0 / us_per_frame * max_chunk) : 0);
    p32(p + 8, 0);
    p32(p + 12, 0x10);                      /* AVIF_HASINDEX */
    p32(p + 16, frames);
    p32(p + 20, 0);
    p32(p + 24, 1);                         /* one stream */
    p32(p + 28, max_chunk);
    p32(p + 32, w);
    p32(p + 36, hgt);
    p += 56;
    tag(p, "LIST"); p32(p + 4, 116); tag(p + 8, "strl"); p += 12;
    tag(p, "strh"); p32(p + 4, 56); p += 8;
    tag(p + 0, "vids"); tag(p + 4, "MJPG");
    p32(p + 8, 0);
    p16(p + 12, 0); p16(p + 14, 0);
    p32(p + 16, 0);
    p32(p + 20, us_per_frame);              /* scale */
    p32(p + 24, 1000000);                   /* rate: rate/scale = frames per second */
    p32(p + 28, 0);
    p32(p + 32, frames);
    p32(p + 36, max_chunk);
    p32(p + 40, 0xFFFFFFFF);                /* default quality */
    p32(p + 44, 0);
    p16(p + 48, 0); p16(p + 50, 0); p16(p + 52, w); p16(p + 54, hgt);
    p += 56;
    tag(p, "strf"); p32(p + 4, 40); p += 8;
    p32(p + 0, 40);
    p32(p + 4, w);
    p32(p + 8, hgt);
    p16(p + 12, 1);
    p16(p + 14, 24);
    tag(p + 16, "MJPG");
    p32(p + 20, (uint32_t)w * hgt * 3);
    p += 40;
    tag(p, "LIST"); p32(p + 4, movi_list); tag(p + 8, "movi");
}

mx_avi_t *mx_avi_open(const char *path, int width, int height)
{
    mx_avi_t *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->idx = heap_caps_malloc(sizeof(idx_t) * MAX_FRAMES, MALLOC_CAP_SPIRAM);
    a->f = fopen(path, "wb");
    if (!a->idx || !a->f) {
        if (a->f) fclose(a->f);
        free(a->idx);
        free(a);
        return NULL;
    }
    setvbuf(a->f, NULL, _IOFBF, 16384);
    a->w = width;
    a->h = height;
    uint8_t h[HDR_SIZE];
    make_header(h, width, height, 0, 100000, 0, 0, 0);
    fwrite(h, 1, HDR_SIZE, a->f);
    return a;
}

bool mx_avi_add(mx_avi_t *a, const uint8_t *jpeg, size_t len)
{
    if (!a || a->frames >= MAX_FRAMES || len < 4) return false;
    uint8_t ch[8];
    tag(ch, "00dc");
    p32(ch + 4, (uint32_t)len);
    if (fwrite(ch, 1, 8, a->f) != 8) return false;
    if (fwrite(jpeg, 1, len, a->f) != len) return false;
    size_t pad = len & 1;
    if (pad) fputc(0, a->f);
    a->idx[a->frames].off = (uint32_t)(4 + a->movi_bytes);     /* relative to the 'movi' tag */
    a->idx[a->frames].size = (uint32_t)len;
    a->frames++;
    if (len > a->max_chunk) a->max_chunk = (uint32_t)len;
    a->movi_bytes += 8 + len + pad;
    return true;
}

uint32_t mx_avi_frames(const mx_avi_t *a) { return a ? a->frames : 0; }
size_t mx_avi_bytes(const mx_avi_t *a) { return a ? a->movi_bytes + HDR_SIZE : 0; }

bool mx_avi_close(mx_avi_t *a, uint32_t elapsed_ms)
{
    if (!a) return false;
    bool ok = true;
    for (uint32_t i = 0; i < a->frames && ok; i++) {
        uint8_t e[16];
        tag(e, "00dc");
        p32(e + 4, 0x10);                  /* AVIIF_KEYFRAME: every JPEG is one */
        p32(e + 8, a->idx[i].off);
        p32(e + 12, a->idx[i].size);
        if (i == 0) {
            uint8_t ih[8];
            tag(ih, "idx1");
            p32(ih + 4, a->frames * 16);
            ok = fwrite(ih, 1, 8, a->f) == 8;
        }
        ok = ok && fwrite(e, 1, 16, a->f) == 16;
    }
    uint32_t us = a->frames && elapsed_ms ? (uint32_t)((uint64_t)elapsed_ms * 1000 / a->frames) : 100000;
    uint8_t h[HDR_SIZE];
    make_header(h, a->w, a->h, a->frames, us, a->max_chunk, a->movi_bytes, a->frames ? 8 + (size_t)a->frames * 16 : 0);
    fflush(a->f);
    fseek(a->f, 0, SEEK_SET);
    ok = ok && fwrite(h, 1, HDR_SIZE, a->f) == HDR_SIZE;
    ok = (fclose(a->f) == 0) && ok;
    ESP_LOGI(TAG, "closed: %u frames, %u ms per file, %s", (unsigned)a->frames, (unsigned)elapsed_ms, ok ? "ok" : "write error");
    free(a->idx);
    free(a);
    return ok;
}

/* ---- reader --------------------------------------------------------- */

struct mx_avi_rd {
    FILE *f;
    long end;
};

static uint32_t g32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

mx_avi_rd_t *mx_avi_rd_open(const char *path, int *w, int *h, uint32_t *us_per_frame, uint32_t *frames)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t hd[HDR_SIZE];
    if (fread(hd, 1, HDR_SIZE, f) != HDR_SIZE || memcmp(hd, "RIFF", 4) || memcmp(hd + 8, "AVI ", 4) || memcmp(hd + 212, "LIST", 4) || memcmp(hd + 220, "movi", 4)) {
        fclose(f);
        return NULL;
    }
    if (us_per_frame) *us_per_frame = g32(hd + 12 + 12 + 8) ? g32(hd + 12 + 12 + 8) : 100000;
    if (frames) *frames = g32(hd + 12 + 12 + 8 + 16);
    if (w) *w = (int)g32(hd + 12 + 12 + 8 + 32);
    if (h) *h = (int)g32(hd + 12 + 12 + 8 + 36);
    mx_avi_rd_t *r = calloc(1, sizeof *r);
    if (!r) { fclose(f); return NULL; }
    r->f = f;
    r->end = 212 + 8 + (long)g32(hd + 216);      /* end of the movi list */
    return r;
}

size_t mx_avi_rd_next(mx_avi_rd_t *r, uint8_t **buf, size_t *cap)
{
    if (!r) return 0;
    for (;;) {
        if (ftell(r->f) + 8 > r->end) return 0;
        uint8_t ch[8];
        if (fread(ch, 1, 8, r->f) != 8) return 0;
        uint32_t len = g32(ch + 4);
        if (len == 0 || len > 4 * 1024 * 1024) return 0;
        if (len > *cap) {
            uint8_t *nb = heap_caps_realloc(*buf, len + 64, MALLOC_CAP_SPIRAM);
            if (!nb) return 0;
            *buf = nb;
            *cap = len + 64;
        }
        if (fread(*buf, 1, len, r->f) != len) return 0;
        if (len & 1) fgetc(r->f);
        if (!memcmp(ch, "00dc", 4)) return len;
    }
}

void mx_avi_rd_close(mx_avi_rd_t *r)
{
    if (!r) return;
    fclose(r->f);
    free(r);
}
