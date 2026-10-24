/*
 * The shell: sheets that open over any page.
 *
 *   swipe bottom-to-top   app grid: every app one tap away
 *   swipe top-to-bottom   quick panel: Wi-Fi, Bluetooth, silent, brightness, volume
 *   ui_confirm()          "Restart?"-style dialog with Cancel / OK
 *
 * All three live on the top layer above the pages and below the knob-hold,
 * alert and screen-off overlays. The knob works in each: rotate moves (or sets
 * brightness on the quick panel), click selects (or closes the panel).
 */
#include "ui_priv.h"

#define M ui_M
#define A ui_A

/* ---------------------------------------------------------------- */
/* app grid                                                          */
/* ---------------------------------------------------------------- */

typedef struct {
    mx_page_t page;
    const char *ic, *name;
    uint32_t col;
} app_t;

static const app_t APPS[9] = {
    { MX_PAGE_ASSIST, LV_SYMBOL_AUDIO, "Assistant", 0x3D8BFF },
    { MX_PAGE_CAMERA, LV_SYMBOL_EYE_OPEN, "Camera", 0x7C4DFF },
    { MX_PAGE_GALLERY, LV_SYMBOL_IMAGE, "Gallery", 0xE040A0 },
    { MX_PAGE_MARKET, LV_SYMBOL_SHUFFLE, "Market", 0xF7931A },
    { MX_PAGE_PORTFOLIO, LV_SYMBOL_LIST, "Portfolio", 0x2ED573 },
    { MX_PAGE_JOURNAL, LV_SYMBOL_EDIT, "Journal", 0xFF4757 },
    { MX_PAGE_MODES, LV_SYMBOL_CHARGE, "AI Mode", 0xF5B301 },
    { MX_PAGE_NOTES, LV_SYMBOL_BELL, "Alerts", 0x1EC8C8 },
    { MX_PAGE_SETTINGS, LV_SYMBOL_SETTINGS, "Settings", 0x6B7894 },
};

static lv_obj_t *ln_sheet, *ln_ic[9];
static int ln_sel;

static lv_obj_t *sheet(void)
{
    lv_obj_t *s = ui_box(lv_layer_top(), W, H, lv_color_hex(0x03050A), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s, ui_press_cb, LV_EVENT_PRESSED, NULL);
    return s;
}

static void ln_paint(void)
{
    bool hl = ui_knob_mode();
    for (int i = 0; i < 9; i++) {
        lv_obj_set_style_border_width(ln_ic[i], hl && i == ln_sel ? 3 : 0, 0);
    }
}

static void ln_tap(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!ui_is_tap()) return;
    mx_activity();
    ui_goto(APPS[i].page, true);
}

static void build_launcher(void)
{
    ln_sheet = sheet();
    for (int i = 0; i < 9; i++) {
        int cx = (i % 3 - 1) * 98, top = 58 + (i / 3) * 102;
        lv_color_t c = lv_color_hex(APPS[i].col);
        lv_obj_t *b = ui_circle(ln_sheet, 70, c);
        ui_grad(b, lv_color_lighten(c, 60), lv_color_darken(c, 70));
        lv_obj_align(b, LV_ALIGN_TOP_MID, cx, top);
        lv_obj_set_style_border_color(b, C_TEXT, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_transform_width(b, -4, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(b, -4, LV_STATE_PRESSED);
        lv_obj_set_style_bg_grad_color(b, c, LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, ui_press_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(b, ln_tap, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_lbl(b, &lv_font_montserrat_24, C_TEXT, APPS[i].ic, LV_ALIGN_CENTER, 0, 0);
        ln_ic[i] = b;
        lv_obj_t *l = ui_lbl(ln_sheet, &lv_font_montserrat_12, C_DIM, APPS[i].name, LV_ALIGN_TOP_MID, cx, top + 76);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }
}

void ui_launcher_open(void)
{
    ui_shell_close();
    ln_sel = 0;
    for (int i = 0; i < 9; i++) if (APPS[i].page == ui_cur) ln_sel = i;
    ln_paint();
    ui_show(ln_sheet, true);
    lv_obj_fade_in(ln_sheet, 140, 0);
}

/* ---------------------------------------------------------------- */
/* quick panel                                                       */
/* ---------------------------------------------------------------- */

static lv_obj_t *qp_sheet, *qp_time, *qp_date, *qp_tg[3], *qp_tgl[3], *qp_bri, *qp_vol, *qp_bri_v, *qp_vol_v;
static int vol_before_mute = 60;

static void qp_toggle(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!ui_is_tap()) return;
    mx_activity();
    if (i == 0) {
        if (A->wifi_set_enabled) A->wifi_set_enabled(M->wifi_state == MX_WIFI_OFF);
    } else if (i == 1) {
        if (A->bt_set_enabled) A->bt_set_enabled(!M->bt_on);
    } else {
        int v = M->volume > 0 ? 0 : (vol_before_mute > 0 ? vol_before_mute : 60);
        if (M->volume > 0) vol_before_mute = M->volume;
        M->volume = v;
        if (A->set_volume) A->set_volume(v);
    }
    ui_refresh();
}

static void qp_slider(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int v = lv_slider_get_value(s);
    mx_activity();
    if (s == qp_bri) {
        if (code == LV_EVENT_VALUE_CHANGED) {
            if (A->preview_brightness) A->preview_brightness(v);
        } else {
            M->brightness = v;
            if (A->set_brightness) A->set_brightness(v);
        }
    } else if (code != LV_EVENT_VALUE_CHANGED) {
        M->volume = v;
        if (A->set_volume) A->set_volume(v);
    }
    lv_label_set_text_fmt(s == qp_bri ? qp_bri_v : qp_vol_v, "%d%%", v);
}

static lv_obj_t *qp_make_slider(int y, const char *name, int min, lv_obj_t **val)
{
    ui_lbl(qp_sheet, &lv_font_montserrat_14, C_DIM, name, LV_ALIGN_TOP_LEFT, 76, y);
    *val = ui_lbl(qp_sheet, &lv_font_montserrat_14, C_TEXT, "", LV_ALIGN_TOP_RIGHT, -76, y);
    lv_obj_t *s = lv_slider_create(qp_sheet);
    lv_obj_set_size(s, 256, 14);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, y + 26);
    lv_slider_set_range(s, min, 100);
    lv_obj_set_ext_click_area(s, 18);
    lv_obj_set_style_bg_color(s, C_PANEL_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, C_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 7, LV_PART_KNOB);
    lv_obj_add_event_cb(s, qp_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, qp_slider, LV_EVENT_RELEASED, NULL);
    return s;
}

static void qp_settings(lv_event_t *e)
{
    (void)e;
    if (!ui_is_tap()) return;
    mx_activity();
    ui_goto(MX_PAGE_SETTINGS, true);
}

static void build_panel(void)
{
    qp_sheet = sheet();
    qp_time = ui_lbl(qp_sheet, &lv_font_montserrat_28, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 30);
    qp_date = ui_lbl(qp_sheet, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_TOP_MID, 0, 66);
    static const char *ic[3] = { LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_MUTE };
    static const char *nm[3] = { "Wi-Fi", "Bluetooth", "Silent" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = ui_circle(qp_sheet, 62, C_PANEL_HI);
        lv_obj_align(b, LV_ALIGN_TOP_MID, (i - 1) * 84, 94);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_transform_width(b, -3, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(b, -3, LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, ui_press_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(b, qp_toggle, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_lbl(b, &lv_font_montserrat_24, C_TEXT, ic[i], LV_ALIGN_CENTER, 0, 0);
        qp_tg[i] = b;
        qp_tgl[i] = ui_lbl(qp_sheet, &lv_font_montserrat_12, C_DIM, nm[i], LV_ALIGN_TOP_MID, (i - 1) * 84, 162);
    }
    qp_bri = qp_make_slider(190, "Brightness", 10, &qp_bri_v);
    qp_vol = qp_make_slider(246, "Volume", 0, &qp_vol_v);
    lv_obj_t *b = ui_button(qp_sheet, 150, 42, C_PANEL_HI, LV_SYMBOL_SETTINGS "  Settings", qp_settings, NULL);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 314);
    lv_obj_t *bar = ui_box(qp_sheet, 44, 5, C_FAINT, 3);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 374);
}

static void qp_refresh(void)
{
    char b[32];
    if (M->time_valid) snprintf(b, sizeof b, "%02d:%02d", M->now.tm_hour, M->now.tm_min);
    else snprintf(b, sizeof b, "--:--");
    ui_set_text(qp_time, b);
    if (M->time_valid) strftime(b, sizeof b, "%a, %b %d", &M->now);
    else snprintf(b, sizeof b, "Time not set");
    ui_set_text(qp_date, b);
    bool on[3] = { M->wifi_state != MX_WIFI_OFF, M->bt_on, M->volume == 0 };
    for (int i = 0; i < 3; i++) ui_bg_color(qp_tg[i], on[i] ? C_BLUE : C_PANEL_HI);
    ui_set_text(qp_tgl[0], M->wifi_state == MX_WIFI_CONNECTED && M->ssid[0] ? M->ssid : "Wi-Fi");
    if (!lv_obj_has_state(qp_bri, LV_STATE_PRESSED) && lv_slider_get_value(qp_bri) != M->brightness) {
        lv_slider_set_value(qp_bri, M->brightness, LV_ANIM_OFF);
        lv_label_set_text_fmt(qp_bri_v, "%d%%", M->brightness);
    }
    if (!lv_obj_has_state(qp_vol, LV_STATE_PRESSED) && lv_slider_get_value(qp_vol) != M->volume) {
        lv_slider_set_value(qp_vol, M->volume, LV_ANIM_OFF);
        lv_label_set_text_fmt(qp_vol_v, "%d%%", M->volume);
    }
}

void ui_panel_open(void)
{
    ui_shell_close();
    lv_slider_set_value(qp_bri, M->brightness, LV_ANIM_OFF);
    lv_label_set_text_fmt(qp_bri_v, "%d%%", M->brightness);
    lv_slider_set_value(qp_vol, M->volume, LV_ANIM_OFF);
    lv_label_set_text_fmt(qp_vol_v, "%d%%", M->volume);
    qp_refresh();
    ui_show(qp_sheet, true);
    lv_obj_fade_in(qp_sheet, 140, 0);
}

/* ---------------------------------------------------------------- */
/* confirm dialog                                                    */
/* ---------------------------------------------------------------- */

static lv_obj_t *dl_sheet, *dl_title, *dl_msg, *dl_btn[2], *dl_ok_lbl;
static void (*dl_cb)(void);
static int dl_sel;

static void dl_paint(void)
{
    bool hl = ui_knob_mode();
    for (int i = 0; i < 2; i++) lv_obj_set_style_border_width(dl_btn[i], hl && i == dl_sel ? 3 : 0, 0);
}

static void dl_run(int ok)
{
    void (*cb)(void) = dl_cb;
    dl_cb = NULL;
    ui_show(dl_sheet, false);
    if (ok && cb) cb();
}

static void dl_tap(lv_event_t *e)
{
    if (!ui_is_tap()) return;
    mx_activity();
    dl_run((int)(intptr_t)lv_event_get_user_data(e));
    ui_refresh();
}

static void build_dialog(void)
{
    dl_sheet = sheet();
    lv_obj_set_style_bg_opa(dl_sheet, LV_OPA_90, 0);
    lv_obj_t *card = ui_box(dl_sheet, 312, 214, C_PANEL, 28);
    lv_obj_center(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);       /* taps on the card do not cancel */
    dl_title = ui_lbl(card, &lv_font_montserrat_20, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 22);
    dl_msg = ui_lbl(card, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_width(dl_msg, 264);
    lv_label_set_long_mode(dl_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(dl_msg, LV_TEXT_ALIGN_CENTER, 0);
    dl_btn[0] = ui_button(card, 124, 50, C_GREY, "Cancel", dl_tap, (void *)0);
    lv_obj_align(dl_btn[0], LV_ALIGN_BOTTOM_LEFT, 20, -20);
    dl_btn[1] = ui_button(card, 124, 50, C_RED, "OK", dl_tap, (void *)1);
    lv_obj_align(dl_btn[1], LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    dl_ok_lbl = lv_obj_get_child(dl_btn[1], 0);
    for (int i = 0; i < 2; i++) lv_obj_set_style_border_color(dl_btn[i], C_TEXT, 0);
    lv_obj_add_event_cb(dl_sheet, dl_tap, LV_EVENT_CLICKED, (void *)0);
}

void ui_confirm(const char *title, const char *msg, const char *ok, lv_color_t ok_col, void (*cb)(void))
{
    ui_shell_close();
    lv_label_set_text(dl_title, title);
    lv_label_set_text(dl_msg, msg ? msg : "");
    lv_label_set_text(dl_ok_lbl, ok ? ok : "OK");
    lv_obj_set_style_bg_color(dl_btn[1], ok_col, 0);
    lv_obj_set_style_bg_color(dl_btn[1], lv_color_lighten(ok_col, 40), LV_STATE_PRESSED);
    dl_cb = cb;
    dl_sel = 0;                       /* the safe choice first */
    dl_paint();
    ui_show(dl_sheet, true);
    lv_obj_fade_in(dl_sheet, 120, 0);
}

/* ---------------------------------------------------------------- */

void ui_shell_build(void)
{
    build_launcher();
    build_panel();
    build_dialog();
}

static bool up(lv_obj_t *o) { return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }

bool ui_shell_open(void)
{
    return up(ln_sheet) || up(qp_sheet) || up(dl_sheet);
}

void ui_shell_close(void)
{
    ui_show(ln_sheet, false);
    ui_show(qp_sheet, false);
    if (up(dl_sheet)) dl_run(0);
}

void ui_shell_refresh(void)
{
    if (up(qp_sheet)) qp_refresh();
    if (up(ln_sheet)) ln_paint();
    if (up(dl_sheet)) dl_paint();
}

bool ui_shell_input(mx_input_t in)
{
    int dir = in == MX_IN_RIGHT ? 1 : in == MX_IN_LEFT ? -1 : 0;
    if (up(dl_sheet)) {
        if (dir) dl_sel = !dl_sel;
        else if (in == MX_IN_CONFIRM) dl_run(dl_sel);
        dl_paint();
        return true;
    }
    if (up(ln_sheet)) {
        if (dir) ln_sel = (ln_sel + dir + 9) % 9;
        else if (in == MX_IN_CONFIRM) { ui_goto(APPS[ln_sel].page, true); return true; }
        ln_paint();
        return true;
    }
    if (up(qp_sheet)) {
        if (dir) {
            int v = M->brightness + dir * 5;
            v = v < 10 ? 10 : v > 100 ? 100 : v;
            M->brightness = v;
            if (A->set_brightness) A->set_brightness(v);
        } else if (in == MX_IN_CONFIRM) {
            ui_shell_close();
        }
        return true;
    }
    return false;
}

bool ui_shell_swipe(lv_dir_t d)
{
    if (up(dl_sheet)) { dl_run(0); return true; }
    if (up(ln_sheet)) {
        if (d == LV_DIR_BOTTOM || d == LV_DIR_RIGHT) ui_shell_close();
        return true;
    }
    if (up(qp_sheet)) {
        if (d == LV_DIR_TOP) ui_shell_close();
        return true;
    }
    return false;
}
