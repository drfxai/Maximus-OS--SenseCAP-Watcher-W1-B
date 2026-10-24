#include "mx_rgb.h"

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensecap-watcher.h"

#define C_AI      0x2F7BFF
#define C_CHARGE  0x1FDB6B
#define C_FULL    0x2F7BFF
#define C_LOW     0xFF2A2A

static volatile mx_rgb_state_t s_act = MX_RGB_OFF;     /* assistant activity */
static volatile mx_rgb_state_t s_flash = MX_RGB_OFF;   /* alert */
static volatile TickType_t s_flash_until, s_notify_at;
static volatile bool s_ai, s_usb, s_full, s_low;

uint32_t mx_rgb_color(mx_rgb_state_t s)
{
    switch (s) {
    case MX_RGB_LISTENING:  return 0x3DA5FF;
    case MX_RGB_THINKING:   return C_AI;
    case MX_RGB_PROCESSING: return 0xA55EEA;
    case MX_RGB_WARNING:    return 0xFFA502;
    case MX_RGB_CRITICAL:   return 0xFF3344;
    default:                return 0;
    }
}

void mx_rgb_set(mx_rgb_state_t s) { s_act = s; }

void mx_rgb_flash(mx_rgb_state_t s, int ms)
{
    s_flash_until = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    s_flash = s;
}

void mx_rgb_notify(void) { s_notify_at = xTaskGetTickCount() | 1; }
void mx_rgb_ai(bool on) { s_ai = on; }

void mx_rgb_power(bool usb, bool full, int pct)
{
    s_usb = usb;
    s_full = usb && full;
    s_low = !usb && pct < 30;
}

static void put(uint32_t c, float k)
{
    bsp_rgb_set((uint8_t)(((c >> 16) & 255) * k), (uint8_t)(((c >> 8) & 255) * k), (uint8_t)((c & 255) * k));
}

static float breathe(uint32_t t, float period, float lo)
{
    float ph = (float)(t % (uint32_t)period) / period;
    return lo + (1.0f - lo) * (0.5f - 0.5f * cosf(2.0f * (float)M_PI * ph));
}

static void rgb_task(void *arg)
{
    uint32_t t = 0, last_c = 1;
    float last_k = -1;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        uint32_t c = 0;
        float k = 0;
        if (s_flash != MX_RGB_OFF && (int32_t)(now - s_flash_until) < 0) {
            c = mx_rgb_color(s_flash);
            k = 0.6f * breathe(t, s_flash == MX_RGB_CRITICAL ? 500.0f : 1000.0f, 0.15f);
        } else if (s_act != MX_RGB_OFF) {
            s_flash = MX_RGB_OFF;
            c = mx_rgb_color(s_act);
            k = 0.6f * breathe(t, s_act == MX_RGB_LISTENING ? 1200.0f : 2000.0f, 0.25f);
        } else if (s_notify_at && now - s_notify_at < pdMS_TO_TICKS(3000)) {
            s_flash = MX_RGB_OFF;
            uint32_t ms = (now - s_notify_at) * portTICK_PERIOD_MS;
            c = C_AI;
            /* three blinks a second apart on battery; a steady glow on the charger */
            k = s_usb ? 0.5f : ((ms % 1000) < 500 ? 0.6f : 0.0f);
        } else {
            s_flash = MX_RGB_OFF;
            s_notify_at = 0;
            if (s_ai) { c = C_AI; k = 0.35f * breathe(t, 3000.0f, 0.4f); }
            else if (s_full) { c = C_FULL; k = 0.25f; }
            else if (s_usb) { c = C_CHARGE; k = 0.25f; }
            else if (s_low) { c = C_LOW; k = 0.18f; }
        }
        if (c != last_c || fabsf(k - last_k) > 0.004f) {
            put(c, k);
            last_c = c;
            last_k = k;
        }
        t += 40;
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void mx_rgb_start(void)
{
    bsp_rgb_init();
    xTaskCreate(rgb_task, "mx_rgb", 3072, NULL, 2, NULL);
}
