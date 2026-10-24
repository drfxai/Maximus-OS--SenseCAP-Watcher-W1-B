#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MX_SAMPLE_RATE 16000

typedef enum {
    MX_SND_OK,           /* a soft pop: done, saved */
    MX_SND_NOTIFY,       /* three rising glass notes */
    MX_SND_REPLY,        /* the assistant answered */
    MX_SND_ERROR,
    MX_SND_OFF,          /* powering off */
    MX_SND_PLUG,         /* charger connected */
    MX_SND_UNPLUG,       /* charger removed */
    MX_SND_LISTEN,       /* microphone on */
    MX_SND_LISTEN_END,   /* microphone off */
    MX_SND_SHUTTER,
    MX_SND_REC_START,
    MX_SND_REC_STOP,
} mx_sound_t;

void mx_audio_init(int volume);
void mx_audio_set_volume(int pct);
void mx_audio_beep(int hz, int ms);   /* plays at once and blocks */
void mx_audio_sound(mx_sound_t s);    /* queued; returns at once */
void mx_audio_wait(int max_ms);       /* until the queued sounds have played */
void mx_audio_chime(int kind);        /* 0 ok, 1 notify, 2 error, 3 off */

/* Records 16 kHz mono PCM into a PSRAM buffer (caller frees).
   Stops at max_ms, when *stop becomes true, or after trailing silence when
   vad is set. level_cb gets 0-100 about every 40 ms. */
int16_t *mx_audio_record(int max_ms, volatile bool *stop, bool vad, void (*level_cb)(int level), size_t *samples);

/* Microphone in the background, for the sound track of a video: the samples
   collect in a buffer that the caller empties with take(). */
bool mx_audio_capture_begin(void);
size_t mx_audio_capture_take(int16_t *dst, size_t max_samples);
void mx_audio_capture_end(void);

/* Streamed playback, for the sound track of a video. push() waits up to
   timeout_ms for room; end() plays out what is queued first when drain is set. */
bool mx_audio_stream_begin(void);
size_t mx_audio_stream_push(const int16_t *pcm, size_t samples, int timeout_ms);
void mx_audio_stream_end(bool drain);
