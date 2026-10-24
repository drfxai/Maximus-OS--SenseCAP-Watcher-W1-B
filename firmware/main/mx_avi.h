#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Motion-JPEG AVI writer. The camera chip already delivers JPEG frames, so a
 * video is just those frames in an AVI container: nothing is re-encoded and
 * nothing is lost. The microphone goes alongside as 16 kHz PCM. Any desktop
 * player opens the files.
 */
typedef struct mx_avi mx_avi_t;

mx_avi_t *mx_avi_open(const char *path, int width, int height, bool audio);
bool mx_avi_add(mx_avi_t *a, const uint8_t *jpeg, size_t len);
bool mx_avi_add_audio(mx_avi_t *a, const int16_t *pcm, size_t samples);
uint32_t mx_avi_frames(const mx_avi_t *a);
size_t mx_avi_bytes(const mx_avi_t *a);
/* Finishes the index and header; elapsed_ms sets the real frame rate. */
bool mx_avi_close(mx_avi_t *a, uint32_t elapsed_ms);

/* Reader used by the gallery. */
typedef struct mx_avi_rd mx_avi_rd_t;
mx_avi_rd_t *mx_avi_rd_open(const char *path, int *w, int *h, uint32_t *us_per_frame, uint32_t *frames);
/* Reads the next frame into *buf (grown as needed); returns its length, 0 at the end. */
size_t mx_avi_rd_next(mx_avi_rd_t *r, uint8_t **buf, size_t *cap);
/* The same for frames and sound chunks both; *audio says which it was. */
size_t mx_avi_rd_chunk(mx_avi_rd_t *r, uint8_t **buf, size_t *cap, bool *audio);
bool mx_avi_rd_has_audio(const mx_avi_rd_t *r);
void mx_avi_rd_close(mx_avi_rd_t *r);
