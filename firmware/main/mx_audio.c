#include "mx_audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "sensecap-watcher.h"

static const char *TAG = "audio";
static int s_volume = 70;

static QueueHandle_t s_snd_q;
static volatile bool s_snd_busy;
static StreamBufferHandle_t s_play_sb, s_cap_sb;
static volatile bool s_play_on, s_play_started, s_cap_on, s_cap_running;

#define PLAY_SB_BYTES (32 * 1024)      /* 1 s of sound */
#define CAP_SB_BYTES (64 * 1024)       /* 2 s of microphone */
#define CHUNK 512                      /* samples per write: 32 ms */
#define PREROLL_BYTES 4800             /* 150 ms queued before a stream starts */

/* ---- sound design ------------------------------------------------------
   Each sound is a few struck notes: a sine with two overtones and an
   exponential decay, which gives the soft glass and marimba timbre of phone
   system sounds. Notes stay above ~500 Hz, where the small speaker is loud. */

typedef struct {
    float hz;        /* 0 = a short noise burst (shutter) */
    int at_ms;
    float tau;       /* decay time constant, seconds */
    float amp;
} note_t;

typedef struct {
    const note_t *n;
    int count, total_ms;
} sound_def_t;

#define E5 659.26f
#define C5 523.25f
#define G5 783.99f
#define B5 987.77f
#define D6 1174.66f
#define E6 1318.51f
#define GS6 1661.22f
#define A6 1760.00f
#define B6 1975.53f

static const note_t N_OK[] = { { A6, 0, 0.06f, 0.8f } };
static const note_t N_NOTIFY[] = { { E6, 0, 0.30f, 1.0f }, { GS6, 110, 0.30f, 0.9f }, { B6, 220, 0.40f, 1.0f } };
static const note_t N_REPLY[] = { { B5, 0, 0.18f, 0.9f }, { E6, 90, 0.28f, 1.0f } };
static const note_t N_ERROR[] = { { E5, 0, 0.16f, 1.0f }, { C5, 150, 0.24f, 1.0f } };
static const note_t N_OFF[] = { { B6, 0, 0.20f, 0.9f }, { E6, 120, 0.22f, 0.9f }, { B5, 240, 0.35f, 1.0f } };
static const note_t N_PLUG[] = { { G5, 0, 0.15f, 0.9f }, { D6, 100, 0.30f, 1.0f } };
static const note_t N_UNPLUG[] = { { D6, 0, 0.15f, 0.9f }, { G5, 100, 0.30f, 1.0f } };
static const note_t N_LISTEN[] = { { D6, 0, 0.07f, 0.8f }, { A6, 70, 0.10f, 1.0f } };
static const note_t N_LISTEN_END[] = { { A6, 0, 0.07f, 0.8f }, { D6, 70, 0.10f, 1.0f } };
static const note_t N_SHUTTER[] = { { 0, 0, 0.012f, 1.0f }, { 0, 55, 0.020f, 0.8f } };
static const note_t N_REC_START[] = { { E6, 0, 0.12f, 1.0f } };
static const note_t N_REC_STOP[] = { { E6, 0, 0.06f, 0.8f }, { E6, 110, 0.10f, 0.8f } };

#define S(a, ms) { a, sizeof a / sizeof *a, ms }
static const sound_def_t SOUNDS[] = {
    [MX_SND_OK] = S(N_OK, 160),
    [MX_SND_NOTIFY] = S(N_NOTIFY, 900),
    [MX_SND_REPLY] = S(N_REPLY, 600),
    [MX_SND_ERROR] = S(N_ERROR, 650),
    [MX_SND_OFF] = S(N_OFF, 900),
    [MX_SND_PLUG] = S(N_PLUG, 550),
    [MX_SND_UNPLUG] = S(N_UNPLUG, 550),
    [MX_SND_LISTEN] = S(N_LISTEN, 260),
    [MX_SND_LISTEN_END] = S(N_LISTEN_END, 260),
    [MX_SND_SHUTTER] = S(N_SHUTTER, 140),
    [MX_SND_REC_START] = S(N_REC_START, 300),
    [MX_SND_REC_STOP] = S(N_REC_STOP, 360),
};

static int16_t *render(const sound_def_t *d, int *out_n)
{
    const int n = MX_SAMPLE_RATE * d->total_ms / 1000;
    float *acc = heap_caps_calloc(n, sizeof(float), MALLOC_CAP_SPIRAM);
    int16_t *pcm = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!acc || !pcm) { free(acc); free(pcm); return NULL; }
    uint32_t seed = 0x2545F491;
    for (int k = 0; k < d->count; k++) {
        const note_t *nt = &d->n[k];
        int start = MX_SAMPLE_RATE * nt->at_ms / 1000;
        float lp = 0;
        for (int i = start; i < n; i++) {
            float t = (float)(i - start) / MX_SAMPLE_RATE;
            float att = t < 0.004f ? t / 0.004f : 1.0f;
            float env = att * expf(-t / nt->tau);
            if (env < 0.0005f && t > 0.01f) break;
            float v;
            if (nt->hz <= 0) {
                seed = seed * 1664525u + 1013904223u;
                float white = (float)(int32_t)seed / 2147483648.0f;
                lp += 0.35f * (white - lp);                  /* soften the hiss */
                v = lp * 1.6f;
            } else {
                float w = 2.0f * (float)M_PI * nt->hz * t;
                v = sinf(w) + 0.30f * expf(-t / (nt->tau * 0.5f)) * sinf(2.0f * w)
                    + 0.10f * expf(-t / (nt->tau * 0.3f)) * sinf(3.0f * w);
            }
            acc[i] += v * env * nt->amp;
        }
    }
    float peak = 0.001f;
    for (int i = 0; i < n; i++) if (fabsf(acc[i]) > peak) peak = fabsf(acc[i]);
    float g = 12000.0f / peak;
    for (int i = 0; i < n; i++) {
        float fade = n - i < 160 ? (n - i) / 160.0f : 1.0f;
        pcm[i] = (int16_t)(acc[i] * g * fade);
    }
    free(acc);
    *out_n = n;
    return pcm;
}

static void write_pcm(const int16_t *p, int n)
{
    size_t w;
    for (int i = 0; i < n; i += CHUNK) {
        int m = n - i < CHUNK ? n - i : CHUNK;
        bsp_i2s_write((void *)(p + i), m * sizeof(int16_t), &w, 1000);
    }
}

/* One task owns the speaker: queued sounds first, then any video sound. */
static void snd_task(void *arg)
{
    static int16_t chunk[CHUNK];
    for (;;) {
        int kind;
        if (xQueueReceive(s_snd_q, &kind, s_play_on ? 0 : pdMS_TO_TICKS(200)) == pdTRUE) {
            s_snd_busy = true;
            int n = 0;
            int16_t *pcm = s_volume ? render(&SOUNDS[kind], &n) : NULL;
            if (pcm) { write_pcm(pcm, n); free(pcm); }
            s_snd_busy = uxQueueMessagesWaiting(s_snd_q) > 0;
            continue;
        }
        if (s_play_on) {
            /* a little sound in hand first, so small delays in the video do not cut it */
            if (!s_play_started) {
                if (xStreamBufferBytesAvailable(s_play_sb) < PREROLL_BYTES) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
                s_play_started = true;
            }
            size_t got = xStreamBufferReceive(s_play_sb, chunk, sizeof chunk, pdMS_TO_TICKS(20));
            if (got >= 2) write_pcm(chunk, (int)(got / 2));
        }
    }
}

static void cap_task(void *arg)
{
    static int16_t chunk[640];               /* 40 ms */
    for (;;) {
        if (!s_cap_on) {
            s_cap_running = false;
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        s_cap_running = true;
        size_t got = 0;
        if (bsp_i2s_read(chunk, sizeof chunk, &got, 200) != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        /* when nobody empties it fast enough, the newest audio is dropped */
        xStreamBufferSend(s_cap_sb, chunk, got, 0);
    }
}

static TaskHandle_t s_cap_task;

void mx_audio_init(int volume)
{
    bsp_codec_init();
    /* the BSP sets the microphone gain while the input is closed; set it again now it is open */
    esp_codec_dev_set_in_gain(bsp_codec_microphone_get(), 27.0f);
    mx_audio_set_volume(volume);
    s_snd_q = xQueueCreate(6, sizeof(int));
    s_play_sb = xStreamBufferCreateWithCaps(PLAY_SB_BYTES, 2, MALLOC_CAP_SPIRAM);
    s_cap_sb = xStreamBufferCreateWithCaps(CAP_SB_BYTES, 2, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCore(snd_task, "mx_snd", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(cap_task, "mx_mic", 4096, NULL, 6, &s_cap_task, 0);
}

void mx_audio_set_volume(int pct)
{
    s_volume = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    bsp_codec_volume_set(s_volume, NULL);
}

void mx_audio_beep(int hz, int ms)
{
    if (s_volume == 0) return;
    const int n = MX_SAMPLE_RATE * ms / 1000;
    int16_t *b = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!b) return;
    for (int i = 0; i < n; i++) {
        float env = 1.0f;
        if (i < 160) env = i / 160.0f;                 /* 10 ms attack/release to avoid clicks */
        if (n - i < 160) env = (n - i) / 160.0f;
        b[i] = (int16_t)(sinf(2.0f * (float)M_PI * hz * i / MX_SAMPLE_RATE) * 9000.0f * env);
    }
    write_pcm(b, n);
    free(b);
}

void mx_audio_sound(mx_sound_t s)
{
    if (!s_snd_q || (unsigned)s >= sizeof SOUNDS / sizeof *SOUNDS) return;
    int k = s;
    s_snd_busy = true;
    if (xQueueSend(s_snd_q, &k, 0) != pdTRUE) ESP_LOGD(TAG, "sound queue full");
}

void mx_audio_wait(int max_ms)
{
    for (int t = 0; t < max_ms && s_snd_q && (s_snd_busy || uxQueueMessagesWaiting(s_snd_q)); t += 10)
        vTaskDelay(pdMS_TO_TICKS(10));
}

void mx_audio_chime(int kind)
{
    static const mx_sound_t map[] = { MX_SND_OK, MX_SND_NOTIFY, MX_SND_ERROR, MX_SND_OFF };
    if (kind >= 0 && kind < 4) mx_audio_sound(map[kind]);
}

int16_t *mx_audio_record(int max_ms, volatile bool *stop, bool vad, void (*level_cb)(int), size_t *samples)
{
    const size_t cap = (size_t)MX_SAMPLE_RATE * max_ms / 1000;
    int16_t *buf = heap_caps_malloc(cap * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    *samples = 0;
    if (!buf) return NULL;

    enum { REC_CHUNK = 640 };                   /* 40 ms */
    int16_t chunk[REC_CHUNK];
    size_t n = 0;
    int quiet_ms = 0, speech_ms = 0;
    const int thresh = 700;                     /* peak above this counts as speech */

    while (n + REC_CHUNK <= cap && !(stop && *stop)) {
        size_t got = 0;
        if (bsp_i2s_read(chunk, sizeof chunk, &got, 200) != ESP_OK) {
            ESP_LOGW(TAG, "mic read failed");
            break;
        }
        int peak = 0;
        for (int i = 0; i < REC_CHUNK; i++) {
            int a = abs(chunk[i]);
            if (a > peak) peak = a;
        }
        memcpy(buf + n, chunk, sizeof chunk);
        n += REC_CHUNK;
        if (level_cb) level_cb(peak * 100 / 12000 > 100 ? 100 : peak * 100 / 12000);
        if (peak > thresh) { speech_ms += 40; quiet_ms = 0; }
        else quiet_ms += 40;
        /* stop 1.4 s after the person stops talking, once they have said something */
        if (vad && speech_ms >= 200 && quiet_ms >= 1400) break;
        /* give up if nothing is said for 6 s */
        if (vad && speech_ms < 200 && quiet_ms >= 6000) { n = 0; break; }
    }
    *samples = n;
    if (n == 0) { free(buf); return NULL; }
    return buf;
}

bool mx_audio_capture_begin(void)
{
    if (!s_cap_sb || !s_cap_task) return false;
    xStreamBufferReset(s_cap_sb);
    s_cap_on = true;
    xTaskNotifyGive(s_cap_task);
    return true;
}

size_t mx_audio_capture_take(int16_t *dst, size_t max_samples)
{
    if (!s_cap_sb) return 0;
    return xStreamBufferReceive(s_cap_sb, dst, max_samples * 2, 0) / 2;
}

void mx_audio_capture_end(void)
{
    s_cap_on = false;
    for (int i = 0; i < 30 && s_cap_running; i++) vTaskDelay(pdMS_TO_TICKS(10));
}

bool mx_audio_stream_begin(void)
{
    if (!s_play_sb) return false;
    xStreamBufferReset(s_play_sb);
    s_play_started = false;
    s_play_on = true;
    return true;
}

size_t mx_audio_stream_push(const int16_t *pcm, size_t samples, int timeout_ms)
{
    if (!s_play_on) return 0;
    return xStreamBufferSend(s_play_sb, pcm, samples * 2, pdMS_TO_TICKS(timeout_ms)) / 2;
}

void mx_audio_stream_end(bool drain)
{
    if (drain) {
        s_play_started = true;
        for (int i = 0; i < 150 && s_play_on && xStreamBufferBytesAvailable(s_play_sb) > 0; i++) vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_play_on = false;
    vTaskDelay(pdMS_TO_TICKS(40));
    xStreamBufferReset(s_play_sb);
}
