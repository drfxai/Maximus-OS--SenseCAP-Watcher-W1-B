#include "mx_camera.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"

static const char *TAG = "camera";
static sscma_client_handle_t s_client;
static volatile bool s_ready, s_streaming, s_probing;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_q;

static void flush_queue(void)
{
    mx_frame_t f;
    while (xQueueReceive(s_q, &f, 0) == pdTRUE) free(f.jpeg);
}

static void on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    char *img = NULL;
    int img_size = 0;
    if (sscma_utils_fetch_image_from_reply(reply, &img, &img_size) != ESP_OK || !img) return;
    size_t cap = (size_t)img_size * 3 / 4 + 16, out = 0;
    uint8_t *dec = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (dec && mbedtls_base64_decode(dec, cap, &out, (const unsigned char *)img, strlen(img)) == 0 && out > 100) {
        mx_frame_t f = { dec, out };
        if (xQueueSend(s_q, &f, 0) != pdTRUE) {
            /* queue full: drop the oldest so the preview never lags behind */
            mx_frame_t old;
            if (xQueueReceive(s_q, &old, 0) == pdTRUE) free(old.jpeg);
            if (xQueueSend(s_q, &f, 0) != pdTRUE) free(dec);
        }
    } else {
        free(dec);
    }
    free(img);
}

static void probe_task(void *arg)
{
    /* sscma_client_init only resets the chip; asking for its info is the real test */
    const sscma_client_callback_t cb = { .on_event = on_event };
    sscma_client_register_callback(s_client, &cb, NULL);
    sscma_client_init(s_client);
    for (int i = 0; i < 3 && !s_ready; i++) {
        sscma_client_info_t *info = NULL;
        if (sscma_client_get_info(s_client, &info, false) == ESP_OK && info) {
            ESP_LOGI(TAG, "camera chip answered: fw %s", info->fw_ver ? info->fw_ver : "?");
            s_ready = true;
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    if (!s_ready) ESP_LOGW(TAG, "camera chip did not answer (no firmware?)");
    s_probing = false;
    vTaskDelete(NULL);
}

void mx_camera_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_q) s_q = xQueueCreate(3, sizeof(mx_frame_t));
    s_client = bsp_sscma_client_init();
    if (!s_client) {
        ESP_LOGW(TAG, "camera module not found");
        return;
    }
    s_probing = true;
    xTaskCreatePinnedToCore(probe_task, "mx_cam_probe", 4096, NULL, 4, NULL, 0);
}

bool mx_camera_ready(void) { return s_ready; }
bool mx_camera_probing(void) { return s_probing; }
sscma_client_handle_t mx_camera_client(void) { return s_client; }
bool mx_camera_streaming(void) { return s_streaming; }
void mx_camera_lock(void) { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void mx_camera_unlock(void) { xSemaphoreGiveRecursive(s_lock); }

/* Starts frames flowing at the given sensor option; waits for the first one. */
static bool begin(int opt, int times)
{
    flush_queue();
    sscma_client_break(s_client);
    vTaskDelay(pdMS_TO_TICKS(40));
    flush_queue();
    if (sscma_client_set_sensor(s_client, 1, opt, true) != ESP_OK) ESP_LOGW(TAG, "set_sensor(%d) failed", opt);
    vTaskDelay(pdMS_TO_TICKS(80));
    /* plain sampling first (no AI model, fastest); inference mode as fallback */
    for (int attempt = 0; attempt < 2; attempt++) {
        esp_err_t r = attempt == 0 ? sscma_client_sample(s_client, times) : sscma_client_invoke(s_client, times, false, true);
        if (r != ESP_OK) continue;
        mx_frame_t f;
        if (xQueuePeek(s_q, &f, pdMS_TO_TICKS(3500)) == pdTRUE) return true;
        if (times < 0) sscma_client_break(s_client);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    return false;
}

bool mx_camera_stream(bool on)
{
    if (!s_ready) return false;
    mx_camera_lock();
    bool ok = true;
    if (on) {
        if (!s_streaming) {
            ok = begin(MX_CAM_PREVIEW_OPT, -1);
            s_streaming = ok;
        }
    } else if (s_streaming) {
        sscma_client_break(s_client);
        s_streaming = false;
        vTaskDelay(pdMS_TO_TICKS(60));
        flush_queue();
    }
    mx_camera_unlock();
    return ok;
}

bool mx_camera_next(mx_frame_t *f, int timeout_ms)
{
    return s_q && xQueueReceive(s_q, f, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool mx_camera_still(uint8_t **jpeg, size_t *len)
{
    if (!s_ready) return false;
    mx_camera_lock();
    bool was = s_streaming;
    if (was) {
        sscma_client_break(s_client);
        s_streaming = false;
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    bool ok = false;
    static const int opts[] = { 3, 2, MX_CAM_PREVIEW_OPT };       /* 640x480, 480x480, 416x416 */
    for (unsigned i = 0; i < sizeof opts / sizeof *opts && !ok; i++) {
        if (!begin(opts[i], 2)) continue;
        mx_frame_t f;
        /* the first frame after a sensor switch can be dark: ask for two, keep the newest */
        vTaskDelay(pdMS_TO_TICKS(450));
        while (uxQueueMessagesWaiting(s_q) > 1) {
            if (xQueueReceive(s_q, &f, 0) == pdTRUE) free(f.jpeg);
        }
        if (xQueueReceive(s_q, &f, pdMS_TO_TICKS(500)) == pdTRUE) {
            *jpeg = f.jpeg;
            *len = f.len;
            ok = true;
        }
    }
    if (was) {
        s_streaming = begin(MX_CAM_PREVIEW_OPT, -1);
    }
    mx_camera_unlock();
    return ok;
}

bool mx_camera_snapshot(uint8_t **jpeg, size_t *len)
{
    return mx_camera_still(jpeg, len);
}
