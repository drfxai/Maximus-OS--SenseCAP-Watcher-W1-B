#include "mx_audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensecap-watcher.h"

static const char *TAG = "audio";
static int s_volume = 70;

void mx_audio_init(int volume)
{
    bsp_codec_init();
    mx_audio_set_volume(volume);
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
    size_t w;
    bsp_i2s_write(b, n * sizeof(int16_t), &w, 1000);
    free(b);
}

void mx_audio_chime(int kind)
{
    switch (kind) {
    case 0: mx_audio_beep(880, 70); mx_audio_beep(1320, 90); break;
    case 1: mx_audio_beep(1040, 80); mx_audio_beep(780, 110); break;
    case 2: mx_audio_beep(330, 160); mx_audio_beep(220, 220); break;
    case 3: mx_audio_beep(880, 80); mx_audio_beep(660, 80); mx_audio_beep(440, 160); break;
    }
}

int16_t *mx_audio_record(int max_ms, volatile bool *stop, bool vad, void (*level_cb)(int), size_t *samples)
{
    const size_t cap = (size_t)MX_SAMPLE_RATE * max_ms / 1000;
    int16_t *buf = heap_caps_malloc(cap * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    *samples = 0;
    if (!buf) return NULL;

    enum { CHUNK = 640 };                       /* 40 ms */
    int16_t chunk[CHUNK];
    size_t n = 0;
    int quiet_ms = 0, speech_ms = 0;
    const int thresh = 700;                     /* peak above this counts as speech */

    while (n + CHUNK <= cap && !(stop && *stop)) {
        size_t got = 0;
        if (bsp_i2s_read(chunk, sizeof chunk, &got, 200) != ESP_OK) {
            ESP_LOGW(TAG, "mic read failed");
            break;
        }
        int peak = 0;
        for (int i = 0; i < CHUNK; i++) {
            int a = abs(chunk[i]);
            if (a > peak) peak = a;
        }
        memcpy(buf + n, chunk, sizeof chunk);
        n += CHUNK;
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
