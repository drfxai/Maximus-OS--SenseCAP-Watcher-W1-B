/*
 * Maximus OS for the SenseCAP Watcher W1-B.
 */
#include "esp_err.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"
#include "sensecap-watcher.h"

#include "esp_lvgl_port.h"

#include "mx_app.h"
#include "mx_audio.h"
#include "mx_camera.h"
#include "mx_config.h"
#include "mx_knob.h"
#include "mx_net.h"
#include "mx_rgb.h"

static const char *TAG = "maximus";

/* The board support package asks for the QSPI pixel clock through this hook
   (tools/fetch-deps.sh adds the weak default). Turbo doubles it. */
uint32_t bsp_lcd_pclk_hz(void)
{
    return mx_cfg()->disp_speed ? 80 * 1000 * 1000 : 40 * 1000 * 1000;
}

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

    /* A picture that comes out garbled at Turbo speed can be undone without a
       working screen: hold the knob while powering on. */
    if (mx_cfg()->disp_speed && bsp_exp_io_get_level(BSP_KNOB_BTN) == 0) {
        ESP_LOGW(TAG, "knob held at boot: display speed back to normal");
        mx_cfg()->disp_speed = 0;
        mx_cfg_save();
    }

    /* Draw buffers in internal RAM and DMA capable: rendering from PSRAM was
       the main cause of the slow, striped redraw. */
    bsp_display_cfg_t dcfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = DRV_LCD_H_RES * 32,
        .double_buffer = true,
        .flags = { .buff_dma = true, .buff_spiram = false },
    };
    dcfg.lvgl_port_cfg.task_priority = CONFIG_LVGL_PORT_TASK_PRIORITY;
    dcfg.lvgl_port_cfg.task_affinity = CONFIG_LVGL_PORT_TASK_AFFINITY;
    dcfg.lvgl_port_cfg.task_stack = CONFIG_LVGL_PORT_TASK_STACK_SIZE;
    dcfg.lvgl_port_cfg.task_max_sleep_ms = CONFIG_LVGL_PORT_TASK_MAX_SLEEP_MS;
    dcfg.lvgl_port_cfg.timer_period_ms = CONFIG_LVGL_PORT_TIMER_PERIOD_MS;
    lv_disp_t *disp = bsp_lvgl_init_with_cfg(&dcfg);
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
