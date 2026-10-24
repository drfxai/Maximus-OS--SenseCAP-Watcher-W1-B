/*
 * Settings and its sub-pages: Wi-Fi (networks and password), display,
 * date and time, camera chip. Sub-pages are off the carousel; a swipe from
 * left to right (or the Back row) returns to the parent. Anything that cannot
 * be undone (Restart, Power off, chip reinstall) asks first.
 */
#include "ui_priv.h"

#define M ui_M
#define A ui_A
#define P ui_P
#define cur ui_cur

const mx_tz_info_t mx_tz[] = {
    { "UTC", "UTC0" },
    { "London", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Paris / Berlin", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Athens / Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Istanbul", "TRT-3" },
    { "Moscow", "MSK-3" },
    { "Tehran", "IRST-3:30" },
    { "Dubai", "GST-4" },
    { "Karachi", "PKT-5" },
    { "Delhi", "IST-5:30" },
    { "Dhaka", "BDT-6" },
    { "Bangkok", "ICT-7" },
    { "Singapore / HK", "SGT-8" },
    { "Tokyo / Seoul", "JST-9" },
    { "Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    { "Sao Paulo", "BRT3" },
    { "New York", "EST5EDT,M3.2.0,M11.1.0" },
    { "Chicago", "CST6CDT,M3.2.0,M11.1.0" },
    { "Denver", "MST7MDT,M3.2.0,M11.1.0" },
    { "Los Angeles", "PST8PDT,M3.2.0,M11.1.0" },
    { "Anchorage", "AKST9AKDT,M3.2.0,M11.1.0" },
    { "Honolulu", "HST10" },
};
const int mx_tz_n = sizeof mx_tz / sizeof mx_tz[0];
const int mx_timeout_s[3] = { 30, 60, 120 };

/* every hidden list page rotates its own list */
static bool list_page_rotate(int dir)
{
    return ui_list_of[cur] ? ui_list_rotate(ui_list_of[cur], dir) : false;
}

static void page_title(page_t *pg, const char *t)
{
    ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, t, LV_ALIGN_TOP_MID, 0, 56);
}

/* ------------------------------------------------------------------ */
/* SETTINGS                                                            */
/* ------------------------------------------------------------------ */

enum { S_WIFI, S_WEB, S_BT, S_DISPLAY, S_SOUND, S_TIME, S_STORAGE, S_CHIP, S_TG, S_SEC, S_REBOOT, S_OFF, S_BACK, S_N };
static ui_list_t L_set;

static const char *wifi_state_text(char *b, size_t n)
{
    switch (M->wifi_state) {
    case MX_WIFI_OFF: return "Switched off";
    case MX_WIFI_IDLE: return "Choose a network";
    case MX_WIFI_CONNECTING: return "Connecting...";
    case MX_WIFI_FAILED: return "Could not connect";
    default:
        snprintf(b, n, "%s", M->ip[0] ? M->ip : "Connected");
        return b;
    }
}

static void storage_text(char *b, size_t n)
{
    if (M->sd_state == MX_SD_ST_NONE) snprintf(b, n, "No memory card");
    else if (M->sd_state == MX_SD_ST_ERROR) snprintf(b, n, "Card unreadable, use FAT32");
    else if (M->sd_total_mb >= 1024) snprintf(b, n, "%.1f GB free of %.1f GB", (double)M->sd_free_mb / 1024.0, (double)M->sd_total_mb / 1024.0);
    else snprintf(b, n, "%u MB free", (unsigned)M->sd_free_mb);
}

static void settings_refresh(void)
{
    char b[48], c[48];
    ui_list_set(&L_set, S_WIFI, NULL, wifi_state_text(c, sizeof c), M->wifi_state == MX_WIFI_OFF ? "Off" : M->ssid[0] ? M->ssid : "On");
    if (M->web_on) snprintf(b, sizeof b, "Join %s, open 192.168.4.1", M->web_ssid);
    ui_list_set(&L_set, S_WEB, NULL, M->web_on ? b : M->web_failed ? "Could not start" : "Live log in a browser", M->web_on ? "On" : "Off");
    ui_text_color(L_set.val[S_WEB], M->web_on ? C_GREEN : C_DIM);
    ui_list_set(&L_set, S_BT, NULL, M->bt_failed ? "Could not start" : M->bt_on ? "Visible as Maximus OS" : "Not visible", M->bt_on ? "On" : "Off");
    ui_text_color(L_set.val[S_BT], M->bt_on ? C_GREEN : C_DIM);
    snprintf(b, sizeof b, "%d%%", M->brightness);
    ui_list_set(&L_set, S_DISPLAY, NULL, NULL, b);
    snprintf(b, sizeof b, "%d%%", M->volume);
    ui_list_set(&L_set, S_SOUND, NULL, NULL, b);
    if (M->time_valid) {
        snprintf(b, sizeof b, "%s - %s", M->auto_time ? "Automatic" : "Set by hand", mx_tz[M->tz_idx < 0 ? 0 : M->tz_idx].name);
        if (M->tz_idx < 0) snprintf(b, sizeof b, "%s - custom zone", M->auto_time ? "Automatic" : "Set by hand");
    } else {
        snprintf(b, sizeof b, "Time not set");
    }
    ui_list_set(&L_set, S_TIME, NULL, b, NULL);
    storage_text(b, sizeof b);
    ui_list_set(&L_set, S_STORAGE, NULL, b, NULL);
    const char *chip = M->chip_state == MX_CHIP_OK ? "Ready" : M->chip_state == MX_CHIP_MISSING ? "Not responding" :
                       M->chip_state == MX_CHIP_FLASHING ? "Installing..." : "Firmware and status";
    ui_list_set(&L_set, S_CHIP, NULL, chip, NULL);
    if (M->telegram_paired) ui_list_set(&L_set, S_TG, NULL, "Bot paired", LV_SYMBOL_OK);
    else {
        snprintf(b, sizeof b, "Send /start %s", M->pair_code[0] ? M->pair_code : "<code>");
        ui_list_set(&L_set, S_TG, NULL, b, "");
    }
    ui_list_set(&L_set, S_SEC, NULL, M->pin_set ? "Lock the watch now" : "Create a 4-digit PIN", "");
    L_set.skip[S_BACK] = !P[MX_PAGE_SETTINGS].capture;     /* only while the knob drives the list */
}

static bool settings_rotate(int dir)
{
    if (!P[MX_PAGE_SETTINGS].capture) return false;
    return ui_list_rotate(&L_set, dir);
}

static void do_reboot(void) { if (A->reboot) A->reboot(); }
static void do_web_off(void) { if (A->web_set) A->web_set(false); }

static void web_info(void)
{
    static char msg[120];
    snprintf(msg, sizeof msg, "Wi-Fi: %s\nPassword: %s\nThen open http://192.168.4.1%s%s", M->web_ssid, M->web_pass,
             M->ip[0] ? "\nor http://" : "", M->ip);
    ui_confirm("Web console", msg, "Turn off", C_RED, do_web_off);
}
static void do_power_off(void) { if (A->power_off) A->power_off(); }

static int step_pct(int cur_v, const int *steps, int n)
{
    for (int i = 0; i < n; i++) if (cur_v < steps[i]) return steps[i];
    return steps[0];
}

static void settings_confirm(void)
{
    page_t *pg = &P[MX_PAGE_SETTINGS];
    if (!pg->capture) { pg->capture = true; ui_list_layout(&L_set); return; }
    static const int vs[] = { 0, 25, 50, 75, 100 };
    switch (L_set.sel) {
    case S_WIFI: ui_goto(MX_PAGE_WIFI, true); return;
    case S_WEB:
        if (M->web_on) web_info();
        else if (A->web_set) A->web_set(true);
        break;
    case S_BT:
        if (A->bt_set_enabled) A->bt_set_enabled(!M->bt_on);
        break;
    case S_DISPLAY: ui_goto(MX_PAGE_DISPLAY, true); return;
    case S_SOUND:
        M->volume = step_pct(M->volume, vs, 5);
        if (A->set_volume) A->set_volume(M->volume);
        break;
    case S_TIME: ui_goto(MX_PAGE_TIME, true); return;
    case S_STORAGE:
        if (A->sd_refresh) A->sd_refresh();
        break;
    case S_CHIP: ui_goto(MX_PAGE_CHIP, true); return;
    case S_SEC:
        pg->capture = false;
        if (M->pin_set) { M->locked = true; }
        ui_goto(MX_PAGE_SECURITY, true);
        return;
    case S_REBOOT:
        ui_confirm("Restart?", "The watch restarts and comes back on its own.", "Restart", C_ORANGE, do_reboot);
        break;
    case S_OFF:
        ui_confirm("Power off?", "Hold the knob or plug in USB to switch it back on.", "Power off", C_RED, do_power_off);
        break;
    case S_BACK:
        pg->capture = false;
        break;
    default:
        break;
    }
    settings_refresh();
}

static void build_settings(void)
{
    page_t *pg = &P[MX_PAGE_SETTINGS];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    page_title(pg, "Settings");
    L_set.parent = pg->scr;
    ui_list_of[MX_PAGE_SETTINGS] = &L_set;
    ui_list_add(&L_set, lv_color_hex(0x1E90FF), LV_SYMBOL_WIFI, "Wi-Fi", "", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0x00A8A8), LV_SYMBOL_LIST, "Web console", "", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0x3B6FE0), LV_SYMBOL_BLUETOOTH, "Bluetooth", "", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0xF5B301), LV_SYMBOL_EYE_OPEN, "Display", "Brightness, timeout, speed", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0x7C4DFF), LV_SYMBOL_VOLUME_MAX, "Sound", "Volume", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0x16A085), LV_SYMBOL_LOOP, "Date & time", "", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0x8E6E53), LV_SYMBOL_SD_CARD, "Storage", "", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0xE040A0), LV_SYMBOL_VIDEO, "Camera chip", "", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0x229ED9), LV_SYMBOL_ENVELOPE, "Telegram", "", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0x2ED573), LV_SYMBOL_EYE_CLOSE, "Security", "", ui_row_click);
    ui_list_add(&L_set, lv_color_hex(0xFFA502), LV_SYMBOL_REFRESH, "Restart", NULL, ui_row_click);
    ui_list_add(&L_set, C_RED, LV_SYMBOL_POWER, "Power off", NULL, ui_row_click);
    ui_list_add(&L_set, C_GREY, LV_SYMBOL_LEFT, "Back", NULL, ui_row_click);
    ui_list_layout(&L_set);
    pg->refresh = settings_refresh;
    pg->rotate = settings_rotate;
    pg->confirm = settings_confirm;
}

/* ------------------------------------------------------------------ */
/* WI-FI                                                               */
/* ------------------------------------------------------------------ */

enum { W_TOGGLE, W_SCAN, W_AP0, W_FORGET = W_AP0 + MX_MAX_APS, W_BACK, W_N };
static ui_list_t L_wifi;
static char wp_ssid[33];

static const char *signal_name(int rssi)
{
    return rssi >= -60 ? "Strong" : rssi >= -75 ? "Good" : "Weak";
}

static lv_color_t signal_color(int rssi)
{
    return rssi >= -60 ? C_GREEN : rssi >= -75 ? C_ORANGE : C_RED;
}

static void wifi_refresh(void)
{
    bool on = M->wifi_state != MX_WIFI_OFF;
    char c[48];
    ui_list_set(&L_wifi, W_TOGGLE, NULL, wifi_state_text(c, sizeof c), on ? "On" : "Off");
    ui_text_color(L_wifi.val[W_TOGGLE], on ? C_GREEN : C_DIM);
    ui_list_set(&L_wifi, W_SCAN, M->wifi_scanning ? "Scanning..." : "Scan again", M->n_aps ? "Tap a network to join" : "Looks for networks nearby", NULL);
    L_wifi.skip[W_SCAN] = !on;
    for (int i = 0; i < MX_MAX_APS; i++) {
        int r = W_AP0 + i;
        L_wifi.skip[r] = !on || i >= M->n_aps;
        if (L_wifi.skip[r]) continue;
        const mx_ap_t *ap = &M->aps[i];
        bool mine = M->wifi_state == MX_WIFI_CONNECTED && !strcmp(ap->ssid, M->ssid);
        snprintf(c, sizeof c, "%s - %s", ap->secure ? "Secured" : "Open", signal_name(ap->rssi));
        ui_list_set(&L_wifi, r, ap->ssid, c, mine ? LV_SYMBOL_OK : "");
        ui_text_color(L_wifi.val[r], C_GREEN);
        ui_bg_color(lv_obj_get_child(L_wifi.row[r], 0), signal_color(ap->rssi));
    }
    L_wifi.skip[W_FORGET] = !on || !M->ssid[0];
    if (!L_wifi.skip[W_FORGET]) {
        snprintf(c, sizeof c, "Saved: %s", M->ssid);
        ui_list_set(&L_wifi, W_FORGET, NULL, c, NULL);
    }
    ui_list_layout(&L_wifi);
}

static void wifi_enter(void)
{
    ui_list_reset(&L_wifi);
    if (M->wifi_state != MX_WIFI_OFF && A->wifi_scan) A->wifi_scan();
}

static void wifi_confirm(void)
{
    int o = L_wifi.sel, row = -1;
    for (int i = 0; i < L_wifi.n; i++) if (L_wifi.ord[i] == o) row = i;
    if (row == W_TOGGLE) {
        if (A->wifi_set_enabled) A->wifi_set_enabled(M->wifi_state == MX_WIFI_OFF);
        if (M->wifi_state == MX_WIFI_OFF && A->wifi_scan) A->wifi_scan();      /* turning on */
    } else if (row == W_SCAN) {
        if (A->wifi_scan) A->wifi_scan();
    } else if (row >= W_AP0 && row < W_FORGET) {
        const mx_ap_t *ap = &M->aps[row - W_AP0];
        snprintf(wp_ssid, sizeof wp_ssid, "%s", ap->ssid);
        if (ap->secure) ui_goto(MX_PAGE_WIFIPASS, true);
        else if (A->wifi_join) A->wifi_join(ap->ssid, "");
    } else if (row == W_FORGET) {
        if (A->wifi_forget) A->wifi_forget();
    } else if (row == W_BACK) {
        ui_back();
        return;
    }
    wifi_refresh();
}

static void build_wifi(void)
{
    page_t *pg = &P[MX_PAGE_WIFI];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    page_title(pg, "Wi-Fi");
    L_wifi.parent = pg->scr;
    ui_list_of[MX_PAGE_WIFI] = &L_wifi;
    ui_list_add(&L_wifi, lv_color_hex(0x1E90FF), LV_SYMBOL_WIFI, "Wi-Fi", "", ui_row_click);
    ui_list_add(&L_wifi, lv_color_hex(0x3A4254), LV_SYMBOL_REFRESH, "Scan again", "", ui_row_click);
    for (int i = 0; i < MX_MAX_APS; i++) ui_list_add(&L_wifi, C_GREEN, LV_SYMBOL_WIFI, "", "", ui_row_click);
    ui_list_add(&L_wifi, lv_color_hex(0x8E3B46), LV_SYMBOL_TRASH, "Forget network", "", ui_row_click);
    ui_list_add(&L_wifi, C_GREY, LV_SYMBOL_LEFT, "Back", NULL, ui_row_click);
    ui_list_layout(&L_wifi);
    pg->refresh = wifi_refresh;
    pg->enter = wifi_enter;
    pg->rotate = list_page_rotate;
    pg->confirm = wifi_confirm;
    ui_register_hidden(MX_PAGE_WIFI, MX_PAGE_SETTINGS);
}

/* ------------------------------------------------------------------ */
/* WI-FI PASSWORD (phone-style keyboard; the knob can walk the keys too)  */
/* ------------------------------------------------------------------ */

static lv_obj_t *wp_ta, *wp_name, *wp_eye;
static ui_kb_t *wp_kb;
static bool wp_hidden;

static void wp_join(void)
{
    if (A->wifi_join) A->wifi_join(wp_ssid, lv_textarea_get_text(wp_ta));
    ui_goto(MX_PAGE_WIFI, true);
}

static void wp_key(int kind, const char *txt)
{
    if (kind == UI_KEY_CHAR) lv_textarea_add_text(wp_ta, txt);
    else if (kind == UI_KEY_BKSP) lv_textarea_del_char(wp_ta);
    else if (kind == UI_KEY_OK) wp_join();
}

static void wp_eye_cb(lv_event_t *e)
{
    (void)e;
    if (!ui_is_tap()) return;
    wp_hidden = !wp_hidden;
    lv_textarea_set_password_mode(wp_ta, wp_hidden);
    lv_label_set_text(lv_obj_get_child(wp_eye, 0), wp_hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void wp_back_cb(lv_event_t *e)
{
    (void)e;
    if (!ui_is_tap()) return;
    ui_back();
}

static bool wp_rotate(int dir) { return ui_kb_rotate(wp_kb, dir); }
static void wp_confirm(void) { ui_kb_confirm(wp_kb); }

static void wp_enter(void)
{
    lv_textarea_set_text(wp_ta, "");
    lv_label_set_text(wp_name, wp_ssid);
    ui_kb_reset(wp_kb);
}

static void wp_leave(void)
{
    lv_textarea_set_text(wp_ta, "");          /* do not leave the password behind */
}

static void build_wifipass(void)
{
    page_t *pg = &P[MX_PAGE_WIFIPASS];
    pg->scr = ui_scr();
    ui_chrome(pg, false);
    lv_obj_align(pg->batt.cont, LV_ALIGN_TOP_MID, -20, 8);
    lv_obj_align_to(pg->batt.pct, pg->batt.cont, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    wp_name = ui_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_width(wp_name, 240);
    lv_label_set_long_mode(wp_name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(wp_name, LV_TEXT_ALIGN_CENTER, 0);

    /* [<]  [ password field ]  [eye] */
    lv_obj_t *back = ui_button(pg->scr, 42, 42, C_PANEL_HI, LV_SYMBOL_LEFT, wp_back_cb, NULL);
    lv_obj_align(back, LV_ALIGN_TOP_MID, -128, 56);
    wp_ta = lv_textarea_create(pg->scr);
    lv_obj_set_size(wp_ta, 200, 44);
    lv_obj_align(wp_ta, LV_ALIGN_TOP_MID, 0, 55);
    lv_textarea_set_one_line(wp_ta, true);
    lv_textarea_set_max_length(wp_ta, 63);
    lv_textarea_set_placeholder_text(wp_ta, "Password");
    lv_textarea_set_password_show_time(wp_ta, 900);
    lv_obj_clear_flag(wp_ta, LV_OBJ_FLAG_CLICKABLE);     /* the keyboard below types into it */
    lv_obj_add_state(wp_ta, LV_STATE_FOCUSED);            /* keeps the cursor blinking */
    lv_obj_set_style_text_font(wp_ta, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(wp_ta, C_PANEL, 0);
    lv_obj_set_style_text_color(wp_ta, C_TEXT, 0);
    lv_obj_set_style_border_color(wp_ta, C_BLUE, 0);
    lv_obj_set_style_border_width(wp_ta, 2, 0);
    lv_obj_set_style_radius(wp_ta, 22, 0);
    lv_obj_set_style_pad_hor(wp_ta, 14, 0);
    lv_obj_set_style_pad_ver(wp_ta, 11, 0);
    lv_obj_set_style_border_color(wp_ta, C_BLUE, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(wp_ta, C_BLUE, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(wp_ta, C_BLUE, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(wp_ta, C_DIM, LV_PART_TEXTAREA_PLACEHOLDER);

    wp_eye = ui_button(pg->scr, 42, 42, C_PANEL_HI, LV_SYMBOL_EYE_OPEN, wp_eye_cb, NULL);
    lv_obj_align(wp_eye, LV_ALIGN_TOP_MID, 128, 56);

    wp_kb = ui_kb_create(pg->scr, UI_KB_TEXT, 110, wp_key);

    pg->enter = wp_enter;
    pg->leave = wp_leave;
    pg->rotate = wp_rotate;
    pg->confirm = wp_confirm;
    pg->no_swipe = true;
    ui_register_hidden(MX_PAGE_WIFIPASS, MX_PAGE_WIFI);
}

/* ------------------------------------------------------------------ */
/* DISPLAY (a form: slider and option buttons; the knob steps through)  */
/* ------------------------------------------------------------------ */

enum { D_BRIGHT, D_TIMEOUT, D_BLFREQ, D_SPEED, D_DRAW, D_TEST, D_BACK, D_N };
enum { G_TIMEOUT, G_BLFREQ, G_SPEED, G_DRAW, G_N };
static const char *const seg_names[G_N][3] = {
    { "30 s", "1 min", "2 min" },
    { "5 kHz", "20 kHz", "40 kHz" },
    { "Normal", "Turbo", NULL },
    { "Factory", "Fast", NULL },
};
static lv_obj_t *d_form, *d_item[D_N], *d_slider, *d_bri_v, *d_seg[G_N][3], *d_hint[G_N];
static int d_sel;
static bool d_adjust;          /* knob turns the brightness */

static int seg_value(int g)
{
    return g == G_TIMEOUT ? M->timeout_idx : g == G_BLFREQ ? M->bl_freq : g == G_SPEED ? M->disp_speed : M->draw_mode;
}

static void seg_set(int g, int v)
{
    if (g == G_TIMEOUT) { M->timeout_idx = v; if (A->set_timeout) A->set_timeout(v); }
    else if (g == G_BLFREQ) { M->bl_freq = v; if (A->set_bl_freq) A->set_bl_freq(v); }
    else if (g == G_SPEED) { M->disp_speed = v; if (A->set_disp_speed) A->set_disp_speed(v); }
    else { M->draw_mode = v; if (A->set_draw_mode) A->set_draw_mode(v); }
}

static void seg_cb(lv_event_t *e)
{
    int c = (int)(intptr_t)lv_event_get_user_data(e);
    if (!ui_is_tap()) return;
    mx_activity();
    seg_set(c >> 4, c & 15);
    ui_refresh();
}

static void bri_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(d_slider);
    mx_activity();
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        if (A->preview_brightness) A->preview_brightness(v);
    } else {
        M->brightness = v;
        if (A->set_brightness) A->set_brightness(v);
    }
    lv_label_set_text_fmt(d_bri_v, "%d%%", v);
}

static void d_btn_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!ui_is_tap()) return;
    mx_activity();
    if (i == D_TEST) ui_goto(MX_PAGE_DTEST, true);
    else ui_back();
}

static lv_obj_t *form_item(int h)
{
    lv_obj_t *o = ui_box(d_form, ROW_W, h, C_PANEL, 20);
    lv_obj_set_style_border_color(o, C_BLUE, 0);
    return o;
}

static void display_refresh(void)
{
    if (!lv_obj_has_state(d_slider, LV_STATE_PRESSED) && lv_slider_get_value(d_slider) != M->brightness) {
        lv_slider_set_value(d_slider, M->brightness, LV_ANIM_OFF);
        lv_label_set_text_fmt(d_bri_v, "%d%%", M->brightness);
    }
    ui_text_color(d_bri_v, d_adjust ? C_BLUE : C_TEXT);
    for (int g = 0; g < G_N; g++)
        for (int k = 0; k < 3 && d_seg[g][k]; k++) {
            bool on = seg_value(g) == k;
            ui_bg_color(d_seg[g][k], on ? C_BLUE : C_PANEL_HI);
        }
    ui_set_text(d_hint[G_SPEED], M->disp_speed ? "80 MHz pixel clock, applies after a restart" : "40 MHz pixel clock (factory)");
    ui_set_text(d_hint[G_DRAW], M->draw_mode ? "Small buffers in fast RAM, after a restart" : "Full-frame buffers like Seeed's firmware");
    bool hl = ui_knob_mode();
    for (int i = 0; i < D_N; i++) lv_obj_set_style_border_width(d_item[i], hl && i == d_sel ? 2 : 0, 0);
}

static bool display_rotate(int dir)
{
    if (d_adjust) {
        int v = M->brightness + dir * 5;
        v = v < 10 ? 10 : v > 100 ? 100 : v;
        if (v != M->brightness) {
            M->brightness = v;
            if (A->set_brightness) A->set_brightness(v);
        }
        return true;
    }
    d_sel = (d_sel + dir + D_N) % D_N;
    lv_obj_scroll_to_view(d_item[d_sel], LV_ANIM_ON);
    return true;
}

static void display_confirm(void)
{
    switch (d_sel) {
    case D_BRIGHT: d_adjust = !d_adjust; break;
    case D_TIMEOUT: seg_set(G_TIMEOUT, (M->timeout_idx + 1) % 3); break;
    case D_BLFREQ: seg_set(G_BLFREQ, (M->bl_freq + 1) % 3); break;
    case D_SPEED: seg_set(G_SPEED, !M->disp_speed); break;
    case D_DRAW: seg_set(G_DRAW, !M->draw_mode); break;
    case D_TEST: ui_goto(MX_PAGE_DTEST, true); return;
    case D_BACK: ui_back(); return;
    }
}

static void display_enter(void)
{
    d_adjust = false;
    d_sel = 0;
    lv_obj_scroll_to_y(d_form, 0, LV_ANIM_OFF);
}

static void build_display(void)
{
    page_t *pg = &P[MX_PAGE_DISPLAY];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    page_title(pg, "Display");

    d_form = lv_obj_create(pg->scr);
    lv_obj_remove_style_all(d_form);
    lv_obj_set_size(d_form, ROW_W + 8, LIST_BOTTOM + 8 - LIST_TOP);
    lv_obj_set_pos(d_form, (W - ROW_W - 8) / 2, LIST_TOP);
    lv_obj_set_flex_flow(d_form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(d_form, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(d_form, ROW_GAP, 0);
    lv_obj_set_style_pad_ver(d_form, 4, 0);
    lv_obj_set_scroll_dir(d_form, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(d_form, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(d_form, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *it = d_item[D_BRIGHT] = form_item(78);
    ui_lbl(it, &lv_font_montserrat_16, C_TEXT, "Brightness", LV_ALIGN_TOP_LEFT, 18, 12);
    d_bri_v = ui_lbl(it, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_RIGHT, -18, 12);
    d_slider = lv_slider_create(it);
    lv_obj_set_size(d_slider, ROW_W - 48, 12);
    lv_obj_align(d_slider, LV_ALIGN_TOP_MID, 0, 50);
    lv_slider_set_range(d_slider, 10, 100);
    lv_obj_set_ext_click_area(d_slider, 16);
    lv_obj_set_style_bg_color(d_slider, C_PANEL_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d_slider, C_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(d_slider, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(d_slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(d_slider, bri_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(d_slider, bri_cb, LV_EVENT_RELEASED, NULL);

    static const char *titles[G_N] = { "Screen off after", "Backlight frequency", "Panel speed", "Drawing" };
    static const char *hints[G_N] = { "", "Higher = no bands in phone photos", "", "" };
    for (int g = 0; g < G_N; g++) {
        bool hint = g != G_TIMEOUT;
        it = d_item[D_TIMEOUT + g] = form_item(hint ? 100 : 84);
        ui_lbl(it, &lv_font_montserrat_16, C_TEXT, titles[g], LV_ALIGN_TOP_LEFT, 18, 10);
        int n = seg_names[g][2] ? 3 : 2;
        int bw = n == 3 ? 82 : 124, gap = 8, x0 = (ROW_W - (n * bw + (n - 1) * gap)) / 2;
        for (int k = 0; k < n; k++) {
            lv_obj_t *b = ui_button(it, bw, 36, C_PANEL_HI, seg_names[g][k], seg_cb, (void *)(intptr_t)(g << 4 | k));
            lv_obj_align(b, LV_ALIGN_TOP_LEFT, x0 + k * (bw + gap), 36);
            lv_obj_set_style_radius(b, 18, 0);
            lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &lv_font_montserrat_14, 0);
            d_seg[g][k] = b;
        }
        d_hint[g] = ui_lbl(it, &lv_font_montserrat_12, C_DIM, hints[g], LV_ALIGN_TOP_MID, 0, 78);
        lv_obj_set_width(d_hint[g], ROW_W - 30);
        lv_obj_set_style_text_align(d_hint[g], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(d_hint[g], LV_LABEL_LONG_DOT);
    }

    static const char *btn[2] = { LV_SYMBOL_IMAGE "  Test patterns", LV_SYMBOL_LEFT "  Back" };
    for (int i = 0; i < 2; i++) {
        it = d_item[D_TEST + i] = ui_button(d_form, ROW_W, 52, i ? C_GREY : C_PANEL, btn[i], d_btn_cb, (void *)(intptr_t)(D_TEST + i));
        lv_obj_set_style_radius(it, 20, 0);
        lv_obj_set_style_border_color(it, C_BLUE, 0);
    }

    pg->refresh = display_refresh;
    pg->enter = display_enter;
    pg->rotate = display_rotate;
    pg->confirm = display_confirm;
    ui_register_hidden(MX_PAGE_DISPLAY, MX_PAGE_SETTINGS);
}

/* ------------------------------------------------------------------ */
/* DISPLAY TEST: full-screen patterns to judge the panel by eye          */
/* ------------------------------------------------------------------ */

#define DT_BARS 8
#define DT_CHECK 8
static lv_obj_t *dt_bg, *dt_bar[DT_BARS], *dt_chk[DT_CHECK * DT_CHECK], *dt_pill, *dt_lbl;
static lv_timer_t *dt_timer;
static int dt_idx;
enum { DT_WHITE, DT_RED, DT_GREEN, DT_BLUE, DT_GREY, DT_BLACK, DT_VGRAD, DT_HGRAD, DT_COLBARS, DT_CHECKER, DT_N };
static const char *dt_names[DT_N] = { "White", "Red", "Green", "Blue", "Grey 50%", "Black", "Grey ramp, vertical",
                                      "Grey ramp, horizontal", "Colour bars", "Checkerboard" };

static void dt_pill_hide(lv_timer_t *t)
{
    (void)t;
    ui_show(dt_pill, false);
    dt_timer = NULL;
}

static void dt_draw(void)
{
    static const uint32_t solid[6] = { 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0x808080, 0x000000 };
    static const uint32_t bars[DT_BARS] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };
    lv_obj_set_style_bg_grad_dir(dt_bg, LV_GRAD_DIR_NONE, 0);
    if (dt_idx < 6) lv_obj_set_style_bg_color(dt_bg, lv_color_hex(solid[dt_idx]), 0);
    if (dt_idx == DT_VGRAD || dt_idx == DT_HGRAD) {
        lv_obj_set_style_bg_color(dt_bg, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_color(dt_bg, lv_color_white(), 0);
        lv_obj_set_style_bg_grad_dir(dt_bg, dt_idx == DT_VGRAD ? LV_GRAD_DIR_VER : LV_GRAD_DIR_HOR, 0);
    }
    for (int i = 0; i < DT_BARS; i++) {
        ui_show(dt_bar[i], dt_idx == DT_COLBARS);
        lv_obj_set_style_bg_color(dt_bar[i], lv_color_hex(bars[i]), 0);
    }
    if (dt_idx == DT_CHECKER) lv_obj_set_style_bg_color(dt_bg, lv_color_black(), 0);
    for (int i = 0; i < DT_CHECK * DT_CHECK; i++) ui_show(dt_chk[i], dt_idx == DT_CHECKER);

    lv_label_set_text_fmt(dt_lbl, "%d/%d  %s\ntap for next, swipe right to leave", dt_idx + 1, DT_N, dt_names[dt_idx]);
    ui_show(dt_pill, true);
    if (dt_timer) lv_timer_del(dt_timer);
    dt_timer = lv_timer_create(dt_pill_hide, 2500, NULL);
    lv_timer_set_repeat_count(dt_timer, 1);
}

static void dt_next(void) { dt_idx = (dt_idx + 1) % DT_N; dt_draw(); }
static bool dt_rotate(int dir) { dt_idx = (dt_idx + dir + DT_N) % DT_N; dt_draw(); return true; }
static void dt_enter(void) { dt_idx = 0; dt_draw(); }

static void dt_leave(void)
{
    if (dt_timer) { lv_timer_del(dt_timer); dt_timer = NULL; }
}

static void build_dtest(void)
{
    page_t *pg = &P[MX_PAGE_DTEST];
    pg->scr = ui_scr();
    ui_chrome(pg, false);
    ui_show(pg->batt.cont, false);
    ui_show(pg->batt.pct, false);
    dt_bg = ui_box(pg->scr, W, H, lv_color_white(), 0);
    for (int i = 0; i < DT_BARS; i++) {
        dt_bar[i] = ui_box(dt_bg, W / DT_BARS + 1, H, lv_color_white(), 0);
        lv_obj_set_pos(dt_bar[i], i * W / DT_BARS, 0);
    }
    int c = (W + DT_CHECK - 1) / DT_CHECK;
    for (int i = 0; i < DT_CHECK * DT_CHECK; i++) {
        int r = i / DT_CHECK, k = i % DT_CHECK;
        dt_chk[i] = ui_box(dt_bg, c, c, (r + k) & 1 ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_pos(dt_chk[i], k * c, r * c);
        if ((r + k) & 1) ui_show(dt_chk[i], false);
    }
    dt_pill = ui_box(pg->scr, 260, 52, C_BG, 26);
    lv_obj_set_style_bg_opa(dt_pill, LV_OPA_70, 0);
    lv_obj_align(dt_pill, LV_ALIGN_BOTTOM_MID, 0, -40);
    dt_lbl = ui_lbl(dt_pill, &lv_font_montserrat_14, C_TEXT, "", LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(dt_lbl, LV_TEXT_ALIGN_CENTER, 0);
    pg->tap = dt_next;
    pg->confirm = dt_next;
    pg->rotate = dt_rotate;
    pg->enter = dt_enter;
    pg->leave = dt_leave;
    ui_register_hidden(MX_PAGE_DTEST, MX_PAGE_DISPLAY);
}

/* ------------------------------------------------------------------ */
/* DATE & TIME                                                         */
/* ------------------------------------------------------------------ */

enum { T_AUTO, T_ZONE, T_SET, T_BACK, T_N };
static ui_list_t L_time;

static void time_refresh(void)
{
    char b[40];
    ui_list_set(&L_time, T_AUTO, NULL, "Sync over Wi-Fi", M->auto_time ? "On" : "Off");
    ui_text_color(L_time.val[T_AUTO], M->auto_time ? C_GREEN : C_DIM);
    ui_list_set(&L_time, T_ZONE, NULL, M->tz_idx >= 0 ? mx_tz[M->tz_idx].name : "Custom (from setup page)", NULL);
    if (M->time_valid) strftime(b, sizeof b, "%Y-%m-%d  %H:%M", &M->now);
    else snprintf(b, sizeof b, "Not set yet");
    ui_list_set(&L_time, T_SET, NULL, b, NULL);
    ui_list_layout(&L_time);
}

static void time_confirm(void)
{
    switch (L_time.sel) {
    case T_AUTO:
        M->auto_time = !M->auto_time;
        if (A->set_auto_time) A->set_auto_time(M->auto_time);
        break;
    case T_ZONE: ui_goto(MX_PAGE_TZ, true); return;
    case T_SET: ui_goto(MX_PAGE_TIMESET, true); return;
    case T_BACK: ui_back(); return;
    }
    time_refresh();
}

static void time_enter(void)
{
    ui_list_reset(&L_time);
}

static void build_time(void)
{
    page_t *pg = &P[MX_PAGE_TIME];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    page_title(pg, "Date & time");
    L_time.parent = pg->scr;
    ui_list_of[MX_PAGE_TIME] = &L_time;
    ui_list_add(&L_time, lv_color_hex(0x16A085), LV_SYMBOL_WIFI, "Automatic time", "", ui_row_click);
    ui_list_add(&L_time, lv_color_hex(0x1E90FF), LV_SYMBOL_GPS, "Time zone", "", ui_row_click);
    ui_list_add(&L_time, lv_color_hex(0xF5B301), LV_SYMBOL_EDIT, "Set date and time", "", ui_row_click);
    ui_list_add(&L_time, C_GREY, LV_SYMBOL_LEFT, "Back", NULL, ui_row_click);
    ui_list_layout(&L_time);
    pg->refresh = time_refresh;
    pg->enter = time_enter;
    pg->rotate = list_page_rotate;
    pg->confirm = time_confirm;
    ui_register_hidden(MX_PAGE_TIME, MX_PAGE_SETTINGS);
}

/* ---- time zone: the whole table as a list ------------------------- */

static ui_list_t L_tz;

static void tz_refresh(void)
{
    for (int i = 0; i < mx_tz_n && i < LIST_MAX - 1; i++) {
        ui_list_set(&L_tz, i, NULL, NULL, i == M->tz_idx ? LV_SYMBOL_OK : "");
        ui_text_color(L_tz.val[i], C_GREEN);
    }
}

static void tz_confirm(void)
{
    int row = ui_list_row(&L_tz);
    if (row >= 0 && row < mx_tz_n && row < L_tz.n - 1) {
        M->tz_idx = row;
        if (A->set_tz) A->set_tz(row);
    }
    ui_back();
}

static void tz_enter(void)
{
    ui_list_reset(&L_tz);
    if (M->tz_idx > 0) L_tz.sel = M->tz_idx;
}

static void build_tz(void)
{
    page_t *pg = &P[MX_PAGE_TZ];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    page_title(pg, "Time zone");
    L_tz.parent = pg->scr;
    ui_list_of[MX_PAGE_TZ] = &L_tz;
    for (int i = 0; i < mx_tz_n && i < LIST_MAX - 1; i++) ui_list_add(&L_tz, lv_color_hex(0x1E90FF), LV_SYMBOL_GPS, mx_tz[i].name, NULL, ui_row_click);
    ui_list_add(&L_tz, C_GREY, LV_SYMBOL_LEFT, "Back", NULL, ui_row_click);
    pg->refresh = tz_refresh;
    pg->enter = tz_enter;
    pg->rotate = list_page_rotate;
    pg->confirm = tz_confirm;
    ui_register_hidden(MX_PAGE_TZ, MX_PAGE_TIME);
}

/* ---- the editor: year, month, day, hour, minute ------------------- */

enum { F_YEAR, F_MONTH, F_DAY, F_HOUR, F_MIN, F_N };
static int ts_f[F_N], ts_field;
static lv_obj_t *ts_date, *ts_clock, *ts_hint, *ts_next_lbl;

static int days_in(int y, int m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[(m - 1) % 12];
}

static void ts_draw(void)
{
    char a[64], b[64];
    const char *on = "#2F7BFF ";
    snprintf(a, sizeof a, "%s%04d#-%s%02d#-%s%02d#", ts_field == F_YEAR ? on : "#F2F4F8 ", ts_f[F_YEAR],
             ts_field == F_MONTH ? on : "#F2F4F8 ", ts_f[F_MONTH], ts_field == F_DAY ? on : "#F2F4F8 ", ts_f[F_DAY]);
    snprintf(b, sizeof b, "%s%02d#:%s%02d#", ts_field == F_HOUR ? on : "#F2F4F8 ", ts_f[F_HOUR], ts_field == F_MIN ? on : "#F2F4F8 ", ts_f[F_MIN]);
    ui_set_text(ts_date, a);
    ui_set_text(ts_clock, b);
    static const char *names[F_N] = { "Year", "Month", "Day", "Hour", "Minute" };
    char h[48];
    snprintf(h, sizeof h, "%s - turn to change", names[ts_field]);
    ui_set_text(ts_hint, h);
    ui_set_text(ts_next_lbl, ts_field == F_MIN ? "Save" : "Next");
}

static void ts_adjust(int dir)
{
    int *v = &ts_f[ts_field];
    switch (ts_field) {
    case F_YEAR: *v += dir; if (*v < 2024) *v = 2024; if (*v > 2099) *v = 2099; break;
    case F_MONTH: *v = (*v - 1 + dir + 12) % 12 + 1; break;
    case F_DAY: { int n = days_in(ts_f[F_YEAR], ts_f[F_MONTH]); *v = (*v - 1 + dir + n) % n + 1; break; }
    case F_HOUR: *v = (*v + dir + 24) % 24; break;
    case F_MIN: *v = (*v + dir + 60) % 60; break;
    }
    int n = days_in(ts_f[F_YEAR], ts_f[F_MONTH]);
    if (ts_f[F_DAY] > n) ts_f[F_DAY] = n;
    ts_draw();
}

static void ts_save(void)
{
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = ts_f[F_YEAR] - 1900;
    t.tm_mon = ts_f[F_MONTH] - 1;
    t.tm_mday = ts_f[F_DAY];
    t.tm_hour = ts_f[F_HOUR];
    t.tm_min = ts_f[F_MIN];
    t.tm_isdst = -1;
    M->auto_time = false;
    if (A->set_time) A->set_time(&t);
    ui_back();
}

static void ts_next(void)
{
    if (ts_field == F_MIN) { ts_save(); return; }
    ts_field++;
    ts_draw();
}

static bool ts_rotate(int dir) { ts_adjust(dir); return true; }
static void ts_confirm(void) { ts_next(); }
static void ts_refresh(void) {}

static void ts_enter(void)
{
    struct tm t = M->now;
    if (!M->time_valid) { t.tm_year = 126; t.tm_mon = 0; t.tm_mday = 1; t.tm_hour = 12; t.tm_min = 0; }
    ts_f[F_YEAR] = t.tm_year + 1900;
    ts_f[F_MONTH] = t.tm_mon + 1;
    ts_f[F_DAY] = t.tm_mday;
    ts_f[F_HOUR] = t.tm_hour;
    ts_f[F_MIN] = t.tm_min;
    ts_field = F_YEAR;
    ts_draw();
}

static void ts_btn_cb(lv_event_t *e)
{
    if (!ui_is_tap()) return;
    int code = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_stop_bubbling(e);
    if (code == 0) ts_adjust(-1);
    else if (code == 1) ts_adjust(1);
    else ts_next();
}

static lv_obj_t *ts_button(lv_obj_t *scr, int w, int h, lv_color_t col, const char *txt, int code, int x, int y, lv_obj_t **lbl)
{
    lv_obj_t *b = ui_box(scr, w, h, col, h / 2);
    lv_obj_align(b, LV_ALIGN_TOP_MID, x, y);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, ui_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(b, ts_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    lv_obj_t *l = ui_lbl(b, &lv_font_montserrat_20, C_TEXT, txt, LV_ALIGN_CENTER, 0, 0);
    if (lbl) *lbl = l;
    return b;
}

static void build_timeset(void)
{
    page_t *pg = &P[MX_PAGE_TIMESET];
    pg->scr = ui_scr();
    ui_chrome(pg, false);
    page_title(pg, "Set date & time");
    ts_date = ui_lbl(pg->scr, &lv_font_montserrat_28, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 108);
    ts_clock = ui_lbl(pg->scr, &lv_font_montserrat_40, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 158);
    lv_label_set_recolor(ts_date, true);
    lv_label_set_recolor(ts_clock, true);
    ts_hint = ui_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_TOP_MID, 0, 216);
    ts_button(pg->scr, 64, 64, C_PANEL, LV_SYMBOL_MINUS, 0, -84, 250, NULL);
    ts_button(pg->scr, 64, 64, C_PANEL, LV_SYMBOL_PLUS, 1, 84, 250, NULL);
    ts_button(pg->scr, 110, 50, C_BLUE, "Next", 2, 0, 258, &ts_next_lbl);
    pg->refresh = ts_refresh;
    pg->enter = ts_enter;
    pg->rotate = ts_rotate;
    pg->confirm = ts_confirm;
    ui_register_hidden(MX_PAGE_TIMESET, MX_PAGE_TIME);
}

/* ------------------------------------------------------------------ */
/* CAMERA CHIP                                                         */
/* ------------------------------------------------------------------ */

enum { C_REINSTALL, C_BACK, C_N };
static ui_list_t L_chip;
static lv_obj_t *c_status, *c_detail, *c_arc;

static void chip_refresh(void)
{
    const char *st = "Checking...", *detail = "";
    char b[64];
    bool flashing = M->chip_state == MX_CHIP_FLASHING;
    switch (M->chip_state) {
    case MX_CHIP_OK:
        st = "Camera chip ready";
        snprintf(b, sizeof b, "Firmware %s", M->chip_ver[0] ? M->chip_ver : "installed");
        detail = b;
        break;
    case MX_CHIP_MISSING:
        st = "Chip not responding";
        detail = "It may have no firmware. Reinstall below.";
        break;
    case MX_CHIP_FLASHING:
        snprintf(b, sizeof b, "Installing %d%%", M->chip_pct);
        st = b;
        detail = "";
        break;
    case MX_CHIP_DONE:
        st = "Installed";
        detail = "Restarting the watch...";
        break;
    case MX_CHIP_FAILED:
        st = "Install failed";
        detail = "Check the card slot and try again.";
        break;
    default: break;
    }
    ui_set_text(c_status, st);
    ui_set_text(c_detail, detail);
    lv_arc_set_value(c_arc, flashing ? M->chip_pct : (M->chip_state == MX_CHIP_DONE ? 100 : 0));
    if (flashing) lv_obj_clear_flag(c_arc, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(c_arc, LV_OBJ_FLAG_HIDDEN);
    ui_list_set(&L_chip, C_REINSTALL, "Reinstall firmware", "Write the stock camera chip firmware", NULL);
    L_chip.skip[C_REINSTALL] = flashing;
    L_chip.skip[C_BACK] = flashing;
    ui_list_layout(&L_chip);
}

static void do_chip_install(void)
{
    if (A->chip_reinstall) A->chip_reinstall();
}

static bool chip_rotate(int dir)
{
    if (M->chip_state == MX_CHIP_FLASHING) return true;
    return ui_list_rotate(&L_chip, dir);
}

static void chip_confirm(void)
{
    if (M->chip_state == MX_CHIP_FLASHING) return;
    if (L_chip.sel == C_BACK) { ui_back(); return; }
    ui_confirm("Reinstall firmware?", "Takes about a minute. Keep the watch powered.", "Install", lv_color_hex(0xE040A0), do_chip_install);
}

static void chip_enter(void)
{
    ui_list_reset(&L_chip);
}

static void build_chip(void)
{
    page_t *pg = &P[MX_PAGE_CHIP];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    page_title(pg, "Camera chip");
    c_arc = lv_arc_create(pg->scr);
    lv_obj_set_size(c_arc, 84, 84);
    lv_obj_align(c_arc, LV_ALIGN_TOP_MID, 0, 108);
    lv_arc_set_rotation(c_arc, 270);
    lv_arc_set_bg_angles(c_arc, 0, 360);
    lv_arc_set_range(c_arc, 0, 100);
    lv_obj_remove_style(c_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(c_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(c_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(c_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(c_arc, lv_color_hex(0x1B2438), LV_PART_MAIN);
    lv_obj_set_style_arc_color(c_arc, C_GREEN, LV_PART_INDICATOR);
    lv_obj_add_flag(c_arc, LV_OBJ_FLAG_HIDDEN);
    c_status = ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 88);
    c_detail = ui_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_width(c_detail, 260);
    lv_label_set_long_mode(c_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(c_detail, LV_TEXT_ALIGN_CENTER, 0);
    L_chip.parent = pg->scr;
    L_chip.top = 196;
    ui_list_of[MX_PAGE_CHIP] = &L_chip;
    ui_list_add(&L_chip, lv_color_hex(0xE040A0), LV_SYMBOL_DOWNLOAD, "Reinstall firmware", "", ui_row_click);
    ui_list_add(&L_chip, C_GREY, LV_SYMBOL_LEFT, "Back", NULL, ui_row_click);
    ui_list_layout(&L_chip);
    pg->refresh = chip_refresh;
    pg->enter = chip_enter;
    pg->rotate = chip_rotate;
    pg->confirm = chip_confirm;
    ui_register_hidden(MX_PAGE_CHIP, MX_PAGE_SETTINGS);
}

/* ------------------------------------------------------------------ */

void ui_build_settings_pages(void)
{
    build_settings();
    build_wifi();
    build_wifipass();
    build_display();
    build_time();
    build_timeset();
    build_tz();
    build_chip();
    build_dtest();
}
