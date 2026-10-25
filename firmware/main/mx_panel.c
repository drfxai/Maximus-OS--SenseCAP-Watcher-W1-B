#include "mx_panel.h"

#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "sensecap-watcher.h"

static const char *TAG = "panel";

/* Every piece of picture sent to the panel goes through here (linker
   --wrap, see CMakeLists.txt): the installer's status line then tells a
   frozen drawing loop from a panel that lost its picture. */
static volatile uint32_t s_draws, s_errors;
static volatile int64_t s_last_us;
static volatile esp_err_t s_last_err;

esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x1, int y1, int x2, int y2, const void *data);

esp_err_t __wrap_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x1, int y1, int x2, int y2, const void *data)
{
    esp_err_t r = __real_esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, data);
    s_draws++;
    s_last_us = esp_timer_get_time();
    if (r != ESP_OK) {
        s_errors++;
        s_last_err = r;
    }
    return r;
}

void mx_panel_stats(mx_panel_stats_t *out)
{
    out->draws = s_draws;
    out->errors = s_errors;
    out->last_err = s_last_err;
    out->idle_s = s_last_us ? (uint32_t)((esp_timer_get_time() - s_last_us) / 1000000) : 0;
}

bool mx_panel_restart(void)
{
    esp_lcd_panel_handle_t p = bsp_lcd_get_panel_handle();
    if (!p || !lvgl_port_lock(1000)) return false;
    /* the same steps the board support package takes at power-on; the panel's
       reset line is not wired, so the reset is the software command */
    esp_err_t r = esp_lcd_panel_reset(p);
    if (r == ESP_OK) r = esp_lcd_panel_init(p);
    if (r == ESP_OK) r = esp_lcd_panel_mirror(p, DRV_LCD_MIRROR_X, DRV_LCD_MIRROR_Y);
    if (r == ESP_OK) r = esp_lcd_panel_disp_on_off(p, true);
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_obj_invalidate(lv_layer_sys());
    lvgl_port_unlock();
    ESP_LOGW(TAG, "panel restarted: %s", esp_err_to_name(r));
    return r == ESP_OK;
}
