#pragma once

#include "model.h"

typedef enum {
    MX_IN_LEFT,
    MX_IN_RIGHT,
    MX_IN_CONFIRM,   /* knob released after a click (under 2 s) */
    MX_IN_HOME,      /* knob held 2-5 s */
} mx_input_t;

typedef enum {
    /* the carousel: rotate the knob or swipe sideways */
    MX_PAGE_HOME,
    MX_PAGE_CAMERA,
    MX_PAGE_GALLERY,
    MX_PAGE_ASSIST,
    MX_PAGE_MARKET,
    MX_PAGE_PORTFOLIO,
    MX_PAGE_JOURNAL,
    MX_PAGE_MODES,
    MX_PAGE_NOTES,
    MX_PAGE_SETTINGS,
    MX_PAGE_CAROUSEL_N,
    /* pages reached from another page; their Back row returns to the parent */
    MX_PAGE_SECURITY = MX_PAGE_CAROUSEL_N,
    MX_PAGE_VIEWER,
    MX_PAGE_WIFI,
    MX_PAGE_WIFIPASS,
    MX_PAGE_DISPLAY,
    MX_PAGE_TIME,
    MX_PAGE_TIMESET,
    MX_PAGE_TZ,
    MX_PAGE_CHIP,
    MX_PAGE_DTEST,
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
void ui_camera_frame(void);                     /* a new preview frame is in the model */
void ui_gallery_frame(void);                    /* a new picture / video frame is in the model */
void ui_screen_blank(bool blank);               /* screen timeout: cover the UI, eat the wake-up touch */
mx_page_t ui_page(void);
bool ui_overlay_open(void);                     /* app grid, quick panel or a dialog is up */

/* Provided by the host application: called on every user input so it can
   track idleness (dimming, screen timeout). */
void mx_activity(void);
