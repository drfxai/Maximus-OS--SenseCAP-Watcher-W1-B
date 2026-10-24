/*
 * Shared internals of the UI files (ui.c core and pages, ui_shell.c,
 * ui_kb.c, ui_settings.c, ui_camera.c). Nothing here is part of the public
 * ui.h API.
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
#define C_PANEL     lv_color_hex(0x141A26)
#define C_PANEL_HI  lv_color_hex(0x1E2738)
#define C_PANEL_SEL lv_color_hex(0x162A50)
#define C_TEXT      lv_color_hex(0xF4F6FA)
#define C_DIM       lv_color_hex(0x8C95A8)
#define C_FAINT     lv_color_hex(0x3A4254)
#define C_BLUE      lv_color_hex(0x3D8BFF)
#define C_GREEN     lv_color_hex(0x2ED573)
#define C_RED       lv_color_hex(0xFF4757)
#define C_ORANGE    lv_color_hex(0xFFA502)
#define C_PURPLE    lv_color_hex(0xA55EEA)
#define C_GREY      lv_color_hex(0x3A4254)

/* battery icon: outline, fill level, charging bolt inside, percentage */
typedef struct {
    lv_obj_t *cont, *body, *fill, *nub, *bolt, *pct;
    int shown_pct, shown_chg;
} batt_t;

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *time;
    batt_t batt;
    void (*refresh)(void);
    bool (*rotate)(int dir);          /* true = consumed */
    void (*confirm)(void);            /* knob click */
    void (*tap)(void);                /* a tap on empty screen space */
    void (*enter)(void);              /* page became current */
    void (*leave)(void);              /* page is being left */
    bool (*gesture)(lv_dir_t dir);    /* true = consumed */
    bool capture;
    bool hidden;                      /* not on the carousel */
    bool no_swipe;                    /* keyboards: a finger on a key is not a swipe */
    mx_page_t parent;                 /* Back target of a hidden page */
} page_t;

/* A list is a touch-scrollable column of rows. The knob moves the
   highlighted row and scrolls it into view; a tap runs a row directly. */
#define LIST_MAX 28
#define ROW_W 292
#define ROW_H 58
#define ROW_GAP 8
#define LIST_TOP 88
#define LIST_BOTTOM 352

typedef struct {
    lv_obj_t *cont;
    lv_obj_t *row[LIST_MAX], *ic[LIST_MAX], *tx[LIST_MAX], *sub[LIST_MAX], *val[LIST_MAX];
    int n, sel, first, count;         /* sel counts visible rows; first is unused */
    bool skip[LIST_MAX];
    int ord[LIST_MAX];
    lv_obj_t *parent;
    int top, height;                  /* viewport, 0 = defaults */
    uint32_t sig;                     /* layout inputs last applied */
    bool sig_ok;
    int shown_sel;
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
void ui_grad(lv_obj_t *o, lv_color_t top, lv_color_t bottom);
void ui_glow(lv_obj_t *o, lv_color_t c, int spread);
lv_obj_t *ui_button(lv_obj_t *p, int w, int h, lv_color_t bg, const char *txt, lv_event_cb_t cb, void *user);
void ui_chrome(page_t *pg, bool with_time);
void ui_batt_create(batt_t *b, lv_obj_t *parent);
void ui_batt_apply(batt_t *b);
void ui_chrome_place(page_t *pg);
void ui_set_text(lv_obj_t *l, const char *t);
void ui_text_color(lv_obj_t *o, lv_color_t c);   /* no-ops when unchanged, so no needless redraw */
void ui_bg_color(lv_obj_t *o, lv_color_t c);
void ui_show(lv_obj_t *o, bool on);

/* lists */
lv_obj_t *ui_list_add(ui_list_t *L, lv_color_t ic_bg, const char *ic_txt, const char *title, const char *sub, lv_event_cb_t cb);
void ui_list_layout(ui_list_t *L);
void ui_list_reset(ui_list_t *L);
void ui_list_set(ui_list_t *L, int i, const char *title, const char *sub, const char *val);
bool ui_list_rotate(ui_list_t *L, int dir);
int ui_list_row(const ui_list_t *L);              /* row index of the highlight, -1 = none */
void ui_row_click(lv_event_t *e);
/* taps: a swipe must not also count as a click */
void ui_press_cb(lv_event_t *e);
bool ui_is_tap(void);
bool ui_knob_mode(void);                          /* the knob was used last: show highlights */

/* navigation */
void ui_back(void);
void ui_register_hidden(mx_page_t page, mx_page_t parent);
void ui_swipe(lv_dir_t dir);

/* shell: app grid (swipe up), quick panel (swipe down), confirm sheet */
void ui_shell_build(void);
bool ui_shell_open(void);
bool ui_shell_input(mx_input_t in);              /* true = consumed */
bool ui_shell_swipe(lv_dir_t dir);
void ui_shell_refresh(void);
void ui_shell_close(void);
void ui_launcher_open(void);
void ui_panel_open(void);
void ui_confirm(const char *title, const char *msg, const char *ok, lv_color_t ok_col, void (*cb)(void));

/* keyboards (ui_kb.c) */
enum { UI_KB_TEXT, UI_KB_PIN };
enum { UI_KEY_CHAR, UI_KEY_BKSP, UI_KEY_SHIFT, UI_KEY_LAYER, UI_KEY_SPACE, UI_KEY_OK };
typedef struct ui_kb ui_kb_t;
typedef void (*ui_kb_cb_t)(int kind, const char *txt);
ui_kb_t *ui_kb_create(lv_obj_t *parent, int type, int y0, ui_kb_cb_t cb);
void ui_kb_reset(ui_kb_t *kb);                   /* lower case letters, highlight on the first key */
bool ui_kb_rotate(ui_kb_t *kb, int dir);
void ui_kb_confirm(ui_kb_t *kb);

/* pages built in the other files */
void ui_build_settings_pages(void);
void ui_build_camera_pages(void);
