/*
 * Maximus OS for the SenseCAP Watcher W1-B.
 */
#include "esp_err.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"
#include "sensecap-watcher.h"

#include "mx_app.h"
#include "mx_audio.h"
#include "mx_camera.h"
#include "mx_config.h"
#include "mx_knob.h"
#include "mx_net.h"
#include "mx_rgb.h"

static const char *TAG = "maximus";

void app_main(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);
    mx_cfg_load();

    /* line-based console on the USB port, used by the web installer */
    usb_serial_jtag_driver_config_t uc = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&uc);

    bsp_spiffs_init_default();
    bsp_io_expander_init();
    lv_disp_t *disp = bsp_lvgl_init();
    if (!disp) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    bsp_rtc_init();
    mx_rgb_start();
    mx_audio_init(mx_cfg()->volume);
    mx_camera_init();
    mx_net_start();
    mx_console_start();
    mx_knob_start(mx_cfg()->knob_invert);
    mx_app_start();
    ESP_LOGI(TAG, "Maximus OS %s up", MX_FW_VERSION);
}
