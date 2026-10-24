/*
 * Memory card handling. The card shares the SPI bus with the camera chip, and
 * the slot has a detect switch on the IO expander, so a small task mounts the
 * card when it is inserted and drops it when it is pulled out.
 */
#include "mx_sd.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensecap-watcher.h"

static const char *TAG = "sd";
static volatile mx_sd_state_t s_state = MX_SD_NONE;
static volatile int s_next_index;     /* highest used file number, shared by IMG_ and VID_ */

static void scan_highest(void)
{
    int hi = 0;
    DIR *d = opendir(MX_SD_DCIM);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *p = strchr(e->d_name, '_');
        if (!p) continue;
        int n = atoi(p + 1);
        if (n > hi) hi = n;
    }
    closedir(d);
    s_next_index = hi;
}

static void try_mount(void)
{
    if (bsp_sdcard_init_default() != ESP_OK) {
        s_state = MX_SD_ERROR;
        ESP_LOGW(TAG, "card present but could not be mounted");
        return;
    }
    mkdir(MX_SD_DCIM, 0777);
    struct stat st;
    if (stat(MX_SD_DCIM, &st) != 0) {
        s_state = MX_SD_ERROR;
        return;
    }
    scan_highest();
    s_state = MX_SD_OK;
    ESP_LOGI(TAG, "card ready, last file number %d", s_next_index);
}

static void sd_task(void *arg)
{
    bool was_in = false;
    for (;;) {
        bool in = bsp_sdcard_is_inserted();
        if (in && !was_in) {
            vTaskDelay(pdMS_TO_TICKS(300));      /* let the contacts settle */
            try_mount();
        } else if (!in && was_in) {
            bsp_sdcard_deinit_default();
            s_state = MX_SD_NONE;
            ESP_LOGI(TAG, "card removed");
        } else if (in && s_state == MX_SD_ERROR) {
            /* a card that failed once is retried every few seconds */
            static int tick;
            if (++tick % 6 == 0) try_mount();
        }
        was_in = in;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void mx_sd_start(void)
{
    xTaskCreatePinnedToCore(sd_task, "mx_sd", 4096, NULL, 3, NULL, 0);
}

mx_sd_state_t mx_sd_state(void) { return s_state; }
bool mx_sd_ready(void) { return s_state == MX_SD_OK; }

bool mx_sd_space(uint32_t *total_mb, uint32_t *free_mb)
{
    if (s_state != MX_SD_OK) return false;
    uint64_t total = 0, fr = 0;
    if (esp_vfs_fat_info(MX_SD_ROOT, &total, &fr) != ESP_OK) return false;
    if (total_mb) *total_mb = (uint32_t)(total >> 20);
    if (free_mb) *free_mb = (uint32_t)(fr >> 20);
    return true;
}

bool mx_sd_next_name(const char *prefix, const char *ext, char *path, unsigned cap)
{
    if (s_state != MX_SD_OK) return false;
    int n = ++s_next_index;
    snprintf(path, cap, MX_SD_DCIM "/%s_%04d.%s", prefix, n, ext);
    return true;
}
