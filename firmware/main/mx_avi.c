#include "mx_avi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "avi";

#define MAX_CHUNKS 72000        /* video frames plus sound chunks; 8 bytes each */
#define HDR_V 224               /* header of a video-only file, up to the movi data */
#define HDR_AV 326              /* the same with a sound stream */
#define AUDIO_BIT 0x80000000u   /* marks a sound chunk in the index */

typedef struct {
    uint32_t off, size;         /* off has AUDIO_BIT set for sound */
} idx_t;

struct mx_avi {
    FILE *f;
    int w, h;
    bool audio;
    uint32_t frames, n_idx, max_chunk, max_audio, samples;
    size_t movi_bytes;          /* bytes of chunk data after the 'movi' tag */
    idx_t *idx;
};

static void p32(uint8_t *b, uint32_t v) { b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }
static void p16(uint8_t *b, uint32_t v) { b[0] = v; b[1] = v >> 8; }
static void tag(uint8_t *b, const char *t) { memcpy(b, t, 4); }

static int hdr_size(const mx_avi_t *a) { return a->audio ? HDR_AV : HDR_V; }

static void make_header(uint8_t *h, const mx_avi_t *a, uint32_t us_per_frame, size_t idx_bytes)
{
    const int H = hdr_size(a);
    memset(h, 0, H);
    uint8_t *p = h;
    uint32_t movi_list = 4 + (uint32_t)a->movi_bytes;
    tag(p, "RIFF"); p32(p + 4, (uint32_t)(H - 8 + a->movi_bytes + idx_bytes)); tag(p + 8, "AVI "); p += 12;
    tag(p, "LIST"); p32(p + 4, a->audio ? 294 : 192); tag(p + 8, "hdrl"); p += 12;
    tag(p, "avih"); p32(p + 4, 56); p += 8;
    p32(p + 0, us_per_frame);
    p32(p + 4, us_per_frame ? (uint32_t)(1000000.0 / us_per_frame * a->max_chunk) + (a->audio ? 32000 : 0) : 0);
    p32(p + 8, 0);
    p32(p + 12, 0x10);                      /* AVIF_HASINDEX */
    p32(p + 16, a->frames);
    p32(p + 20, 0);
    p32(p + 24, a->audio ? 2 : 1);          /* streams */
    p32(p + 28, a->max_chunk);
    p32(p + 32, a->w);
    p32(p + 36, a->h);
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
    p32(p + 32, a->frames);
    p32(p + 36, a->max_chunk);
    p32(p + 40, 0xFFFFFFFF);                /* default quality */
    p32(p + 44, 0);
    p16(p + 48, 0); p16(p + 50, 0); p16(p + 52, a->w); p16(p + 54, a->h);
    p += 56;
    tag(p, "strf"); p32(p + 4, 40); p += 8;
    p32(p + 0, 40);
    p32(p + 4, a->w);
    p32(p + 8, a->h);
    p16(p + 12, 1);
    p16(p + 14, 24);
    tag(p + 16, "MJPG");
    p32(p + 20, (uint32_t)a->w * a->h * 3);
    p += 40;
    if (a->audio) {
        /* 16 kHz mono 16-bit PCM, in chunks between the frames */
        tag(p, "LIST"); p32(p + 4, 94); tag(p + 8, "strl"); p += 12;
        tag(p, "strh"); p32(p + 4, 56); p += 8;
        tag(p + 0, "auds");
        p32(p + 20, 2);                     /* scale: one block = one sample */
        p32(p + 24, 32000);                 /* rate: bytes a second */
        p32(p + 32, a->samples);
        p32(p + 36, a->max_audio);
        p32(p + 40, 0xFFFFFFFF);
        p32(p + 44, 2);                     /* sample size */
        p += 56;
        tag(p, "strf"); p32(p + 4, 18); p += 8;
        p16(p + 0, 1);                      /* WAVE_FORMAT_PCM */
        p16(p + 2, 1);                      /* mono */
        p32(p + 4, 16000);
        p32(p + 8, 32000);
        p16(p + 12, 2);
        p16(p + 14, 16);
        p16(p + 16, 0);
        p += 18;
    }
    tag(p, "LIST"); p32(p + 4, movi_list); tag(p + 8, "movi");
}

mx_avi_t *mx_avi_open(const char *path, int width, int height, bool audio)
{
    mx_avi_t *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->idx = heap_caps_malloc(sizeof(idx_t) * MAX_CHUNKS, MALLOC_CAP_SPIRAM);
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
    a->audio = audio;
    uint8_t h[HDR_AV];
    make_header(h, a, 100000, 0);
    fwrite(h, 1, hdr_size(a), a->f);
    return a;
}

static bool add_chunk(mx_avi_t *a, const char *id, const void *data, size_t len, bool audio)
{
    if (!a || a->n_idx >= MAX_CHUNKS || len < 2) return false;
    uint8_t ch[8];
    tag(ch, id);
    p32(ch + 4, (uint32_t)len);
    if (fwrite(ch, 1, 8, a->f) != 8) return false;
    if (fwrite(data, 1, len, a->f) != len) return false;
    size_t pad = len & 1;
    if (pad) fputc(0, a->f);
    a->idx[a->n_idx].off = (uint32_t)(4 + a->movi_bytes) | (audio ? AUDIO_BIT : 0);   /* relative to 'movi' */
    a->idx[a->n_idx].size = (uint32_t)len;
    a->n_idx++;
    a->movi_bytes += 8 + len + pad;
    return true;
}

bool mx_avi_add(mx_avi_t *a, const uint8_t *jpeg, size_t len)
{
    if (len < 4 || !add_chunk(a, "00dc", jpeg, len, false)) return false;
    a->frames++;
    if (len > a->max_chunk) a->max_chunk = (uint32_t)len;
    return true;
}

bool mx_avi_add_audio(mx_avi_t *a, const int16_t *pcm, size_t samples)
{
    if (!a || !a->audio || !samples) return a != NULL;
    if (!add_chunk(a, "01wb", pcm, samples * 2, true)) return false;
    a->samples += (uint32_t)samples;
    if (samples * 2 > a->max_audio) a->max_audio = (uint32_t)(samples * 2);
    return true;
}

uint32_t mx_avi_frames(const mx_avi_t *a) { return a ? a->frames : 0; }
size_t mx_avi_bytes(const mx_avi_t *a) { return a ? a->movi_bytes + hdr_size(a) : 0; }

bool mx_avi_close(mx_avi_t *a, uint32_t elapsed_ms)
{
    if (!a) return false;
    bool ok = true;
    if (a->n_idx) {
        uint8_t ih[8];
        tag(ih, "idx1");
        p32(ih + 4, a->n_idx * 16);
        ok = fwrite(ih, 1, 8, a->f) == 8;
    }
    for (uint32_t i = 0; i < a->n_idx && ok; i++) {
        uint8_t e[16];
        bool au = a->idx[i].off & AUDIO_BIT;
        tag(e, au ? "01wb" : "00dc");
        p32(e + 4, 0x10);                  /* AVIIF_KEYFRAME: every JPEG is one, and so is PCM */
        p32(e + 8, a->idx[i].off & ~AUDIO_BIT);
        p32(e + 12, a->idx[i].size);
        ok = fwrite(e, 1, 16, a->f) == 16;
    }
    uint32_t us = a->frames && elapsed_ms ? (uint32_t)((uint64_t)elapsed_ms * 1000 / a->frames) : 100000;
    uint8_t h[HDR_AV];
    make_header(h, a, us, a->n_idx ? 8 + (size_t)a->n_idx * 16 : 0);
    fflush(a->f);
    fseek(a->f, 0, SEEK_SET);
    ok = ok && fwrite(h, 1, hdr_size(a), a->f) == (size_t)hdr_size(a);
    ok = (fclose(a->f) == 0) && ok;
    ESP_LOGI(TAG, "closed: %u frames, %u sound samples, %u ms, %s", (unsigned)a->frames, (unsigned)a->samples,
             (unsigned)elapsed_ms, ok ? "ok" : "write error");
    free(a->idx);
    free(a);
    return ok;
}

/* ---- reader --------------------------------------------------------- */

struct mx_avi_rd {
    FILE *f;
    long end;
    bool audio;
};

static uint32_t g32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

static const uint8_t *find(const uint8_t *b, size_t n, const char *t)
{
    for (size_t i = 0; i + 4 <= n; i++) if (!memcmp(b + i, t, 4)) return b + i;
    return NULL;
}

mx_avi_rd_t *mx_avi_rd_open(const char *path, int *w, int *h, uint32_t *us_per_frame, uint32_t *frames)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    /* the header is small; read enough of it to find avih and the movi list */
    uint8_t hd[1024];
    size_t n = fread(hd, 1, sizeof hd, f);
    const uint8_t *avih = n >= 12 && !memcmp(hd, "RIFF", 4) && !memcmp(hd + 8, "AVI ", 4) ? find(hd, n, "avih") : NULL;
    const uint8_t *movi = NULL;
    for (const uint8_t *p = hd; avih && (p = find(p, n - (p - hd), "LIST")) && p + 12 <= hd + n; p += 4) {
        if (!memcmp(p + 8, "movi", 4)) { movi = p; break; }
    }
    if (!avih || !movi || avih + 8 + 56 > hd + n) {
        fclose(f);
        return NULL;
    }
    const uint8_t *m = avih + 8;
    if (us_per_frame) *us_per_frame = g32(m) ? g32(m) : 100000;
    if (frames) *frames = g32(m + 16);
    if (w) *w = (int)g32(m + 32);
    if (h) *h = (int)g32(m + 36);
    mx_avi_rd_t *r = calloc(1, sizeof *r);
    if (!r) { fclose(f); return NULL; }
    r->f = f;
    r->audio = find(hd, movi - hd, "auds") != NULL;
    long movi_at = (long)(movi - hd);
    r->end = movi_at + 8 + (long)g32(movi + 4);      /* end of the movi list */
    fseek(f, movi_at + 12, SEEK_SET);
    return r;
}

bool mx_avi_rd_has_audio(const mx_avi_rd_t *r) { return r && r->audio; }

size_t mx_avi_rd_chunk(mx_avi_rd_t *r, uint8_t **buf, size_t *cap, bool *audio)
{
    if (!r) return 0;
    for (;;) {
        if (ftell(r->f) + 8 > r->end) return 0;
        uint8_t ch[8];
        if (fread(ch, 1, 8, r->f) != 8) return 0;
        uint32_t len = g32(ch + 4);
        if (len > 4 * 1024 * 1024) return 0;
        if (len == 0) continue;
        if (len > *cap) {
            uint8_t *nb = heap_caps_realloc(*buf, len + 64, MALLOC_CAP_SPIRAM);
            if (!nb) return 0;
            *buf = nb;
            *cap = len + 64;
        }
        if (fread(*buf, 1, len, r->f) != len) return 0;
        if (len & 1) fgetc(r->f);
        bool v = !memcmp(ch + 2, "dc", 2), a = !memcmp(ch + 2, "wb", 2);
        if (!v && !a) continue;
        if (audio) *audio = a;
        else if (a) continue;
        return len;
    }
}

size_t mx_avi_rd_next(mx_avi_rd_t *r, uint8_t **buf, size_t *cap)
{
    return mx_avi_rd_chunk(r, buf, cap, NULL);
}

void mx_avi_rd_close(mx_avi_rd_t *r)
{
    if (!r) return;
    fclose(r->f);
    free(r);
}
