#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MX_SAMPLE_RATE 16000

void mx_audio_init(int volume);
void mx_audio_set_volume(int pct);
void mx_audio_beep(int hz, int ms);
void mx_audio_chime(int kind);        /* 0 ok, 1 notify, 2 error, 3 off */

/* Records 16 kHz mono PCM into a PSRAM buffer (caller frees).
   Stops at max_ms, when *stop becomes true, or after trailing silence when
   vad is set. level_cb gets 0-100 about every 40 ms. */
int16_t *mx_audio_record(int max_ms, volatile bool *stop, bool vad, void (*level_cb)(int level), size_t *samples);
