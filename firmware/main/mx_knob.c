/*
 * Rotary knob: rotate = navigate, press = confirm, 5 s = home, 10 s = power off.
 *
 * The button is on the IO expander (active low) so it is polled every 20 ms.
 * Release decides the action: a tap under 5 s confirms, a 5-10 s hold goes
 * home. Reaching 10 s powers off straight away, without waiting for release.
 * While held, a progress event (every 100 ms) lets the UI draw the ring.
 */
#include "mx_knob.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iot_knob.h"
#include "sensecap-watcher.h"

static const char *TAG = "knob";
static QueueHandle_t s_q;
static bool s_invert;

static void post(mx_knob_type_t t, int ms)
{
    mx_knob_evt_t e = { t, ms };
    if (t == MX_K_HOLD) {
        /* progress events are droppable; never let them evict a real one */
        if (uxQueueSpacesAvailable(s_q) < 4) return;
    }
    xQueueSend(s_q, &e, 0);
}

static void on_left(void *h, void *u) { post(s_invert ? MX_K_RIGHT : MX_K_LEFT, 0); }
static void on_right(void *h, void *u) { post(s_invert ? MX_K_LEFT : MX_K_RIGHT, 0); }

static void button_task(void *arg)
{
    bool down = false, off_sent = false;
    TickType_t t0 = 0, last_prog = 0;
    for (;;) {
        bool pressed = bsp_exp_io_get_level(BSP_KNOB_BTN) == 0;
        TickType_t now = xTaskGetTickCount();
        if (pressed && !down) {
            down = true;
            off_sent = false;
            t0 = now;
            last_prog = now;
        } else if (pressed && down) {
            int held = (int)((now - t0) * portTICK_PERIOD_MS);
            if (held >= MX_HOLD_OFF_MS && !off_sent) {
                off_sent = true;
                post(MX_K_POWEROFF, held);
            } else if (!off_sent && (now - last_prog) * portTICK_PERIOD_MS >= 100) {
                last_prog = now;
                post(MX_K_HOLD, held);
            }
        } else if (!pressed && down) {
            down = false;
            int held = (int)((now - t0) * portTICK_PERIOD_MS);
            post(MX_K_HOLD, 0);                      /* hide the ring */
            if (!off_sent) {
                if (held >= MX_HOLD_HOME_MS) post(MX_K_HOME, held);
                else if (held >= 40) post(MX_K_CONFIRM, held);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void mx_knob_start(bool invert)
{
    s_invert = invert;
    s_q = xQueueCreate(24, sizeof(mx_knob_evt_t));
    const knob_config_t kc = {
        .default_direction = 0,
        .gpio_encoder_a = BSP_KNOB_A,
        .gpio_encoder_b = BSP_KNOB_B,
    };
    knob_handle_t k = iot_knob_create(&kc);
    if (!k) {
        ESP_LOGE(TAG, "knob create failed");
    } else {
        iot_knob_register_cb(k, KNOB_LEFT, on_left, NULL);
        iot_knob_register_cb(k, KNOB_RIGHT, on_right, NULL);
    }
    xTaskCreate(button_task, "mx_knob_btn", 3072, NULL, 5, NULL);
}

bool mx_knob_poll(mx_knob_evt_t *out)
{
    return xQueueReceive(s_q, out, 0) == pdTRUE;
}
