/*
 * Shared internals of the UI files (ui.c core and pages, ui_settings.c,
 * ui_camera.c). Nothing here is part of the public ui.h API.
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "ui.h"

#define W 412
#define H 412

#define C_BG        lv_color_hex(0x000000)
#define C_PANEL     lv_color_hex(0x121826)
#define C_PANEL_SEL lv_color_hex(0x16294d)
#define C_TEXT      lv_color_hex(0xF2F4F8)
#define C_DIM       lv_color_hex(0x8A93A6)
#define C_BLUE      lv_color_hex(0x2F7BFF)
#define C_GREEN     lv_color_hex(0x2ED573)
#define C_RED       lv_color_hex(0xFF4757)
#define C_ORANGE    lv_color_hex(0xFFA502)
#define C_PURPLE    lv_color_hex(0xA55EEA)
#define C_GREY      lv_color_hex(0x3A4254)

/* battery icon: outline, fill level, charging bolt, percentage */
typedef struct {
    lv_obj_t *cont, *body, *fill, *nub, *bolt, *pct;
} batt_t;

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *time;
    batt_t batt;
    void (*refresh)(void);
    bool (*rotate)(int dir);          /* true = consumed */
    void (*confirm)(void);
    void (*enter)(void);              /* page became current */
    void (*leave)(void);              /* page is being left */
    bool (*gesture)(lv_dir_t dir);    /* true = consumed */
    bool capture;
    bool hidden;                      /* not on the carousel */
    mx_page_t parent;                 /* Back target of a hidden page */
} page_t;

/* one row of a list page */
#define LIST_MAX 18
#define LIST_VIS 4
#define ROW_H 56
#define ROW_GAP 8
#define LIST_TOP 92

typedef struct {
    lv_obj_t *row[LIST_MAX], *ic[LIST_MAX], *tx[LIST_MAX], *sub[LIST_MAX], *val[LIST_MAX];
    int n, sel, first, count;
    bool skip[LIST_MAX];
    int ord[LIST_MAX];
    lv_obj_t *parent;
    int top;                          /* y of the first row, 0 = LIST_TOP */
    uint32_t sig;                     /* layout inputs last applied */
    bool sig_ok;
} ui_list_t;

extern mx_model_t *ui_M;
extern const mx_actions_t *ui_A;
extern page_t ui_P[MX_PAGE_COUNT];
extern mx_page_t ui_cur;
extern ui_list_t *ui_list_of[MX_PAGE_COUNT];

/* widgets */
lv_obj_t *ui_scr(void);
lv_obj_t *ui_lbl(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *t, lv_align_t al, int x, int y);
lv_obj_t *ui_box(lv_obj_t *p, int w, int h, lv_color_t bg, int radius);
lv_obj_t *ui_circle(lv_obj_t *p, int d, lv_color_t bg);
void ui_glow(lv_obj_t *o, lv_color_t c, int spread);
void ui_chrome(page_t *pg, bool with_time);
void ui_batt_create(batt_t *b, lv_obj_t *parent);
void ui_chrome_place(page_t *pg);
void ui_set_text(lv_obj_t *l, const char *t);
void ui_text_color(lv_obj_t *o, lv_color_t c);   /* no-ops when unchanged, so no needless redraw */
void ui_bg_color(lv_obj_t *o, lv_color_t c);

/* lists */
lv_obj_t *ui_list_add(ui_list_t *L, lv_color_t ic_bg, const char *ic_txt, const char *title, const char *sub, lv_event_cb_t cb);
void ui_list_layout(ui_list_t *L);
void ui_list_set(ui_list_t *L, int i, const char *title, const char *sub, const char *val);
bool ui_list_rotate(ui_list_t *L, int dir);
void ui_row_click(lv_event_t *e);
/* taps: a swipe must not also count as a click */
void ui_press_cb(lv_event_t *e);
bool ui_is_tap(void);

/* navigation */
void ui_back(void);
void ui_register_hidden(mx_page_t page, mx_page_t parent);

/* pages built in the other files */
void ui_build_settings_pages(void);
void ui_build_camera_pages(void);
