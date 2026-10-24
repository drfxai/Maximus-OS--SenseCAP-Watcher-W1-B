#include "mx_rgb.h"

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensecap-watcher.h"

static volatile mx_rgb_state_t s_state = MX_RGB_OFF;
static volatile mx_rgb_state_t s_after = MX_RGB_OFF;
static volatile TickType_t s_until;

uint32_t mx_rgb_color(mx_rgb_state_t s)
{
    switch (s) {
    case MX_RGB_LISTENING:  return 0x1FDB6B;
    case MX_RGB_THINKING:   return 0x2F7BFF;
    case MX_RGB_PROCESSING: return 0xA55EEA;
    case MX_RGB_WARNING:    return 0xFFA502;
    case MX_RGB_CRITICAL:   return 0xFF3344;
    default:                return 0;
    }
}

void mx_rgb_set(mx_rgb_state_t s)
{
    s_until = 0;
    s_state = s;
}

void mx_rgb_flash(mx_rgb_state_t s, int ms)
{
    s_after = MX_RGB_OFF;
    s_until = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    s_state = s;
}

static void rgb_task(void *arg)
{
    mx_rgb_state_t last = MX_RGB_OFF;
    uint32_t t = 0;
    for (;;) {
        if (s_until && (int32_t)(xTaskGetTickCount() - s_until) >= 0) {
            s_until = 0;
            s_state = s_after;
        }
        mx_rgb_state_t s = s_state;
        if (s == MX_RGB_OFF) {
            if (last != MX_RGB_OFF) bsp_rgb_set(0, 0, 0);
            last = s;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        last = s;
        uint32_t c = mx_rgb_color(s);
        /* breathing; critical flashes faster, thinking pulses slowly */
        float period = s == MX_RGB_CRITICAL ? 500.0f : s == MX_RGB_LISTENING ? 1400.0f : 2000.0f;
        float ph = (float)(t % (uint32_t)period) / period;
        float k = 0.15f + 0.85f * (0.5f - 0.5f * cosf(2.0f * (float)M_PI * ph));
        bsp_rgb_set((uint8_t)(((c >> 16) & 255) * k * 0.6f), (uint8_t)(((c >> 8) & 255) * k * 0.6f), (uint8_t)((c & 255) * k * 0.6f));
        t += 40;
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void mx_rgb_start(void)
{
    bsp_rgb_init();
    xTaskCreate(rgb_task, "mx_rgb", 3072, NULL, 2, NULL);
}
