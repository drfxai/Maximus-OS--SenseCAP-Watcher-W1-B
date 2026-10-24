#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * JPEG helpers on top of the SIMD decoder. Output is RGB565 with the byte
 * order LVGL expects for this build (LV_COLOR_16_SWAP), so the buffers can be
 * handed straight to an lv_img.
 */

/* Decodes into a caller buffer (16-byte aligned, w*h*2 bytes). Fills w/h. */
bool mx_jpeg_decode(const uint8_t *jpeg, size_t len, uint8_t *out, size_t out_cap, int *w, int *h);

/* Reads only the header. */
bool mx_jpeg_size(const uint8_t *jpeg, size_t len, int *w, int *h);

/*
 * Decodes and, if the picture is larger than max_w x max_h, shrinks it with a
 * box filter (sharper than nearest, no moire). *out is allocated in PSRAM and
 * must be released with mx_jpeg_free.
 */
bool mx_jpeg_load_fit(const uint8_t *jpeg, size_t len, int max_w, int max_h, uint16_t **out, int *w, int *h);

void *mx_jpeg_alloc(size_t bytes);      /* 16-byte aligned, PSRAM */
void mx_jpeg_free(void *p);
