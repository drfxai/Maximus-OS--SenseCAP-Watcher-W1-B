#include "mx_himax.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensecap-watcher.h"

#include "mx_camera.h"

static const char *TAG = "himax";
#define CHUNK 256

bool mx_himax_image_present(size_t *bytes)
{
    struct stat st;
    if (stat(MX_HIMAX_IMAGE, &st) != 0 || st.st_size < 4096) return false;
    if (bytes) *bytes = (size_t)st.st_size;
    return true;
}

bool mx_himax_version(char *out, size_t cap)
{
    if (cap) out[0] = 0;
    sscma_client_handle_t c = mx_camera_client();
    if (!c || !mx_camera_ready()) return false;
    mx_camera_lock();
    sscma_client_info_t *info = NULL;
    bool ok = sscma_client_get_info(c, &info, true) == ESP_OK && info;
    if (ok && info->fw_ver) snprintf(out, cap, "%s", info->fw_ver);
    else if (ok && info->sw_ver) snprintf(out, cap, "%s", info->sw_ver);
    mx_camera_unlock();
    return ok;
}

bool mx_himax_flash(mx_himax_progress_t progress)
{
    size_t total = 0;
    if (!mx_himax_image_present(&total)) {
        ESP_LOGE(TAG, "no image in flash");
        return false;
    }
    sscma_client_handle_t client = mx_camera_client();
    if (!client) {
        /* the chip never answered at boot: bring the client up for flashing anyway */
        mx_camera_init();
        client = mx_camera_client();
        if (!client) return false;
    }
    sscma_client_flasher_handle_t flasher = bsp_sscma_flasher_init();
    if (!flasher) {
        ESP_LOGE(TAG, "flasher init failed");
        return false;
    }
    FILE *f = fopen(MX_HIMAX_IMAGE, "rb");
    if (!f) return false;

    mx_camera_lock();
    mx_camera_stream(false);
    bool ok = false, direct = false;
    if (progress) progress(0);

    if (sscma_client_ota_start(client, flasher, 0x000000) != ESP_OK) {
        /* A blank chip cannot answer the AT+OTA request. The ISP link below
           does not need it, so talk to the flasher directly. */
        ESP_LOGW(TAG, "chip did not enter OTA mode, trying the ISP path");
        if (client->process_task.handle) vTaskResume(client->process_task.handle);
        sscma_client_reset(client);
        vTaskDelay(pdMS_TO_TICKS(200));
        if (sscma_client_flasher_start(flasher, 0x000000) != ESP_OK) {
            ESP_LOGE(TAG, "flasher start failed");
            goto done;
        }
        direct = true;
    }

    uint8_t buf[CHUNK];
    size_t sent = 0;
    int last_pct = -1;
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        memset(buf, 0, sizeof buf);
        size_t n = fread(buf, 1, sizeof buf, f);
        if (n == 0) break;
        esp_err_t r = direct ? sscma_client_flasher_write(flasher, buf, sizeof buf) : sscma_client_ota_write(client, buf, sizeof buf);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "write failed at %u of %u", (unsigned)sent, (unsigned)total);
            if (direct) sscma_client_flasher_abort(flasher);
            goto done;
        }
        sent += n;
        int pct = (int)((uint64_t)sent * 100 / total);
        if (pct != last_pct) {
            last_pct = pct;
            if (progress) progress(pct);
        }
        if ((sent / CHUNK) % 256 == 0) vTaskDelay(1);          /* let the watchdog and other tasks breathe */
    }
    esp_err_t fin = direct ? sscma_client_flasher_finish(flasher) : sscma_client_ota_finish(client);
    ok = fin == ESP_OK;
    ESP_LOGI(TAG, "%s: %u bytes in %lld ms", ok ? "done" : "finish failed", (unsigned)sent, (long long)((esp_timer_get_time() - t0) / 1000));
    if (ok && progress) progress(100);

done:
    fclose(f);
    mx_camera_unlock();
    return ok;
}
