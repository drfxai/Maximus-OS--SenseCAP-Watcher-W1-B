/*
 * Maximus OS for the SenseCAP Watcher W1-B.
 */
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
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
#include "mx_log.h"
#include "mx_web.h"

static const char *TAG = "maximus";

/* The board support package asks for the QSPI pixel clock through this hook
   (tools/fetch-deps.sh adds the weak default). Turbo doubles it. */
uint32_t bsp_lcd_pclk_hz(void);

/* Touch clean-up between the touch controller and LVGL. The SPD2010 now and
   then reports a lifted finger for one or two reads in the middle of a touch,
   or a point far away from the finger: LVGL took the first as a second tap
   (one touch typed several letters) and the second as a swipe. A release only
   counts after three released reads in a row, and a single wild jump is held
   back until the next read confirms it. */
static void (*s_touch_read)(lv_indev_drv_t *, lv_indev_data_t *);

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static bool down;
    static lv_point_t last;
    static int up_reads, far_reads;
    s_touch_read(drv, data);
    bool pressed = data->state == LV_INDEV_STATE_PRESSED && !(data->point.x == 0 && data->point.y == 0);
    if (!pressed) {
        if (down && ++up_reads < 3) {
            data->state = LV_INDEV_STATE_PRESSED;
            data->point = last;
            return;
        }
        down = false;
        data->state = LV_INDEV_STATE_RELEASED;
        data->point = last;
        return;
    }
    up_reads = 0;
    if (down) {
        int dx = data->point.x - last.x, dy = data->point.y - last.y;
        if (dx * dx + dy * dy > 90 * 90 && ++far_reads < 2) {
            data->point = last;
            return;
        }
    }
    far_reads = 0;
    down = true;
    last = data->point;
}

static void touch_filter_install(void)
{
    if (!lvgl_port_lock(0)) return;
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        if (i->driver->type == LV_INDEV_TYPE_POINTER && i->driver->read_cb != touch_read) {
            s_touch_read = i->driver->read_cb;
            i->driver->read_cb = touch_read;
            break;
        }
    }
    lvgl_port_unlock();
}

uint32_t bsp_lcd_pclk_hz(void)
{
    return mx_cfg()->disp_speed ? 80 * 1000 * 1000 : 40 * 1000 * 1000;
}

void app_main(void)
{
    mx_log_init();
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);
    mx_cfg_load();

    /* Line-based console for the web installer. The Watcher's USB-C socket
       reaches the ESP32-S3 through a CH342 USB-serial bridge on UART0, so
       that is where the installer talks; the chip's own USB port is read
       too, for boards that wire it out. */
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    usb_serial_jtag_driver_config_t uc = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&uc);
    /* the battery gauge logs every reading at info level */
    esp_log_level_set("BSP", ESP_LOG_WARN);

    bsp_spiffs_init_default();
    bsp_io_expander_init();

    /* A picture that comes out garbled at Turbo speed can be undone without a
       working screen: hold the knob while powering on. */
    if ((mx_cfg()->disp_speed || mx_cfg()->draw_mode) && bsp_exp_io_get_level(BSP_KNOB_BTN) == 0) {
        ESP_LOGW(TAG, "knob held at boot: display speed back to normal");
        mx_cfg()->disp_speed = 0;
        mx_cfg()->draw_mode = 0;
        mx_cfg_save();
    }

    /* Factory drawing (the default) is what Seeed's firmware does: two
       full-frame buffers in PSRAM, so the panel gets each change in one
       transfer. Fast drawing uses 32-row buffers in internal DMA memory. */
    bool fast = mx_cfg()->draw_mode == 1;
    bsp_display_cfg_t dcfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = DRV_LCD_H_RES * (fast ? 32 : DRV_LCD_V_RES),
        .double_buffer = true,
        .flags = { .buff_dma = fast, .buff_spiram = !fast },
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
    touch_filter_install();
    bsp_rtc_init();
    mx_rgb_start();
    mx_audio_init(mx_cfg()->volume);
    mx_camera_init();
    mx_net_start();
    mx_web_init();
    mx_console_start();
    mx_knob_start(mx_cfg()->knob_invert);
    mx_app_start();
    ESP_LOGI(TAG, "Maximus OS %s up", MX_FW_VERSION);
}
