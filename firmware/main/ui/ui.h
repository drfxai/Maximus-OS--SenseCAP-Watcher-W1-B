#pragma once

#include "model.h"

typedef enum {
    MX_IN_LEFT,
    MX_IN_RIGHT,
    MX_IN_CONFIRM,   /* knob released after a short press */
    MX_IN_HOME,      /* knob held 5 s */
} mx_input_t;

typedef enum {
    MX_PAGE_HOME,
    MX_PAGE_MARKET,
    MX_PAGE_ASSIST,
    MX_PAGE_PORTFOLIO,
    MX_PAGE_JOURNAL,
    MX_PAGE_MODES,
    MX_PAGE_NOTES,
    MX_PAGE_SETTINGS,
    MX_PAGE_SECURITY,
    MX_PAGE_COUNT,
} mx_page_t;

/* LVGL context only (call under lvgl_port_lock or from an lv_timer). */
void ui_init(mx_model_t *model, const mx_actions_t *actions);
void ui_input(mx_input_t in);
void ui_goto(mx_page_t page, bool animate);
void ui_refresh(void);                          /* redraw from the model */
void ui_set_hold(int held_ms);                  /* knob hold overlay, 0 hides */
void ui_show_alert(const char *title, const char *sub, const char *price, bool critical);
void ui_ring_color(uint32_t rgb);               /* mirror of the RGB ring */
mx_page_t ui_page(void);

/* Provided by the host application: called on every user input so it can
   track idleness (dimming, auto-lock). */
void mx_activity(void);
