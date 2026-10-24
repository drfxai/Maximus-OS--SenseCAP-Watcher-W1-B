/*
 * The Watcher's camera is wired to a Himax AI chip, not to the ESP32-S3. The
 * ESP talks to it over SPI using the SSCMA protocol; a frame arrives as a
 * base64 JPEG inside an event. If the Himax firmware has been erased, init
 * fails and snapshots report "camera unavailable" instead of hanging.
 */
#include "mx_camera.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "sensecap-watcher.h"

static const char *TAG = "camera";
static sscma_client_handle_t s_client;
static bool s_ready;
static SemaphoreHandle_t s_frame_sem, s_lock;
static uint8_t *s_jpeg;
static size_t s_jpeg_len;

static void on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    char *img = NULL;
    int img_size = 0;
    if (sscma_utils_fetch_image_from_reply(reply, &img, &img_size) != ESP_OK || !img) return;
    size_t cap = (size_t)img_size * 3 / 4 + 16, out = 0;
    uint8_t *dec = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (dec && mbedtls_base64_decode(dec, cap, &out, (const unsigned char *)img, strlen(img)) == 0 && out > 100) {
        free(s_jpeg);
        s_jpeg = dec;
        s_jpeg_len = out;
        xSemaphoreGive(s_frame_sem);
    } else {
        free(dec);
    }
    free(img);
}

void mx_camera_init(void)
{
    s_frame_sem = xSemaphoreCreateBinary();
    s_lock = xSemaphoreCreateMutex();
    s_client = bsp_sscma_client_init();
    if (!s_client) {
        ESP_LOGW(TAG, "camera module not found");
        return;
    }
    const sscma_client_callback_t cb = { .on_event = on_event };
    sscma_client_register_callback(s_client, &cb, NULL);
    if (sscma_client_init(s_client) != ESP_OK) {
        ESP_LOGW(TAG, "camera module did not answer");
        return;
    }
    s_ready = true;
}

bool mx_camera_ready(void) { return s_ready; }

bool mx_camera_snapshot(uint8_t **jpeg, size_t *len)
{
    if (!s_ready) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    xSemaphoreTake(s_frame_sem, 0);
    bool ok = false;
    sscma_client_set_sensor(s_client, 1, 1, true);       /* sensor 1, 416x416 */
    vTaskDelay(pdMS_TO_TICKS(60));
    if (sscma_client_sample(s_client, 1) == ESP_OK && xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
        ok = true;
    } else if (sscma_client_invoke(s_client, 1, false, true) == ESP_OK && xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
        ok = true;
        sscma_client_break(s_client);
    }
    if (ok) {
        *jpeg = s_jpeg;
        *len = s_jpeg_len;
        s_jpeg = NULL;
    }
    xSemaphoreGive(s_lock);
    return ok;
}
