/*
 * Settings and its sub-pages: Wi-Fi (networks and password), display,
 * date and time, camera chip. Sub-pages are off the carousel; every one ends
 * in a Back row (or a swipe to the right) that returns to its parent.
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

static const char *timeout_name(int i)
{
    static const char *n[3] = { "30 seconds", "1 minute", "2 minutes" };
    return n[i < 0 || i > 2 ? 1 : i];
}

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

enum { S_WIFI, S_BT, S_DISPLAY, S_SOUND, S_TIME, S_STORAGE, S_CHIP, S_TG, S_SEC, S_REBOOT, S_OFF, S_BACK, S_N };
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
}

static bool settings_rotate(int dir)
{
    if (!P[MX_PAGE_SETTINGS].capture) return false;
    return ui_list_rotate(&L_set, dir);
}

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
        if (A->reboot) A->reboot();
        break;
    case S_OFF:
        if (A->power_off) A->power_off();
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
    L_wifi.sel = 0;
    L_wifi.first = 0;
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
/* WI-FI PASSWORD (on-screen keyboard; the knob walks the keys)         */
/* ------------------------------------------------------------------ */

static lv_obj_t *wp_ta, *wp_kb, *wp_name, *wp_eye;
static bool wp_hidden;

static void wp_join(void)
{
    if (A->wifi_join) A->wifi_join(wp_ssid, lv_textarea_get_text(wp_ta));
    ui_goto(MX_PAGE_WIFI, true);
}

static void wp_kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) wp_join();
    else if (code == LV_EVENT_CANCEL) ui_back();
}

static void wp_eye_cb(lv_event_t *e)
{
    (void)e;
    if (!ui_is_tap()) return;
    wp_hidden = !wp_hidden;
    lv_textarea_set_password_mode(wp_ta, wp_hidden);
    lv_label_set_text(lv_obj_get_child(wp_eye, 0), wp_hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void wp_select(int id)
{
    uint16_t n = ((lv_btnmatrix_t *)wp_kb)->btn_cnt;
    if (!n) return;
    if (id < 0) id = n - 1;
    if (id >= n) id = 0;
    lv_btnmatrix_set_selected_btn(wp_kb, (uint16_t)id);
}

static bool wp_rotate(int dir)
{
    int sel = lv_btnmatrix_get_selected_btn(wp_kb);
    if (sel == LV_BTNMATRIX_BTN_NONE) sel = dir > 0 ? -1 : 0;
    wp_select(sel + dir);
    return true;
}

static void wp_confirm(void)
{
    if (lv_btnmatrix_get_selected_btn(wp_kb) == LV_BTNMATRIX_BTN_NONE) wp_select(0);
    lv_event_send(wp_kb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void wp_enter(void)
{
    lv_textarea_set_text(wp_ta, "");
    lv_keyboard_set_mode(wp_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_label_set_text(wp_name, wp_ssid);
    wp_select(0);
}

static void wp_leave(void)
{
    lv_textarea_set_text(wp_ta, "");          /* do not leave the password behind */
}

static void wp_refresh(void) {}

static void build_wifipass(void)
{
    page_t *pg = &P[MX_PAGE_WIFIPASS];
    pg->scr = ui_scr();
    ui_chrome(pg, false);
    ui_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "Password for", LV_ALIGN_TOP_MID, 0, 44);
    wp_name = ui_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_width(wp_name, 220);
    lv_label_set_long_mode(wp_name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(wp_name, LV_TEXT_ALIGN_CENTER, 0);

    wp_ta = lv_textarea_create(pg->scr);
    lv_obj_set_size(wp_ta, 214, 36);
    lv_obj_align(wp_ta, LV_ALIGN_TOP_MID, -20, 86);
    lv_textarea_set_one_line(wp_ta, true);
    lv_textarea_set_max_length(wp_ta, 63);
    lv_textarea_set_placeholder_text(wp_ta, "type the password");
    lv_obj_set_style_text_font(wp_ta, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(wp_ta, C_PANEL, 0);
    ui_text_color(wp_ta, C_TEXT);
    lv_obj_set_style_border_color(wp_ta, C_BLUE, 0);
    lv_obj_set_style_border_width(wp_ta, 2, 0);
    lv_obj_set_style_radius(wp_ta, 12, 0);

    wp_eye = ui_box(pg->scr, 36, 36, C_PANEL, 12);
    lv_obj_align(wp_eye, LV_ALIGN_TOP_MID, 112, 86);
    lv_obj_add_flag(wp_eye, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wp_eye, ui_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(wp_eye, wp_eye_cb, LV_EVENT_CLICKED, NULL);
    ui_lbl(wp_eye, &lv_font_montserrat_16, C_TEXT, LV_SYMBOL_EYE_OPEN, LV_ALIGN_CENTER, 0, 0);

    wp_kb = lv_keyboard_create(pg->scr);
    lv_obj_set_size(wp_kb, 304, 204);
    lv_obj_align(wp_kb, LV_ALIGN_TOP_MID, 0, 132);
    lv_keyboard_set_textarea(wp_kb, wp_ta);
    lv_obj_set_style_bg_opa(wp_kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wp_kb, 0, 0);
    lv_obj_set_style_pad_all(wp_kb, 0, 0);
    lv_obj_set_style_pad_gap(wp_kb, 4, 0);
    lv_obj_set_style_bg_color(wp_kb, C_PANEL, LV_PART_ITEMS);
    lv_obj_set_style_text_color(wp_kb, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(wp_kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(wp_kb, 8, LV_PART_ITEMS);
    /* the key the knob is on */
    lv_obj_set_style_bg_color(wp_kb, C_BLUE, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_add_state(wp_kb, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(wp_kb, wp_kb_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(wp_kb, wp_kb_event, LV_EVENT_CANCEL, NULL);

    pg->refresh = wp_refresh;
    pg->enter = wp_enter;
    pg->leave = wp_leave;
    pg->rotate = wp_rotate;
    pg->confirm = wp_confirm;
    ui_register_hidden(MX_PAGE_WIFIPASS, MX_PAGE_WIFI);
}

/* ------------------------------------------------------------------ */
/* DISPLAY                                                             */
/* ------------------------------------------------------------------ */

enum { D_BRIGHT, D_TIMEOUT, D_SPEED, D_BACK, D_N };
static ui_list_t L_dsp;
static bool dsp_adjust;

static void display_refresh(void)
{
    char b[24];
    if (dsp_adjust) snprintf(b, sizeof b, LV_SYMBOL_LEFT " %d%% " LV_SYMBOL_RIGHT, M->brightness);
    else snprintf(b, sizeof b, "%d%%", M->brightness);
    ui_list_set(&L_dsp, D_BRIGHT, NULL, dsp_adjust ? "Turn the knob, click to set" : "Click to adjust", b);
    ui_text_color(L_dsp.val[D_BRIGHT], dsp_adjust ? C_BLUE : C_DIM);
    ui_list_set(&L_dsp, D_TIMEOUT, NULL, "Screen turns off after", timeout_name(M->timeout_idx));
    ui_list_set(&L_dsp, D_SPEED, NULL, M->disp_speed ? "80 MHz pixel clock (restart)" : "40 MHz pixel clock", M->disp_speed ? "Turbo" : "Normal");
    ui_list_layout(&L_dsp);
}

static bool display_rotate(int dir)
{
    if (dsp_adjust) {
        int v = M->brightness + dir * 10;
        if (v < 10) v = 10;
        if (v > 100) v = 100;
        if (v != M->brightness) {
            M->brightness = v;
            if (A->set_brightness) A->set_brightness(v);
        }
        return true;
    }
    return ui_list_rotate(&L_dsp, dir);
}

static void display_confirm(void)
{
    switch (L_dsp.sel) {
    case D_BRIGHT: dsp_adjust = !dsp_adjust; break;
    case D_TIMEOUT:
        M->timeout_idx = (M->timeout_idx + 1) % 3;
        if (A->set_timeout) A->set_timeout(M->timeout_idx);
        break;
    case D_SPEED:
        M->disp_speed = !M->disp_speed;
        if (A->set_disp_speed) A->set_disp_speed(M->disp_speed);
        break;
    case D_BACK: dsp_adjust = false; ui_back(); return;
    }
    display_refresh();
}

static void display_enter(void)
{
    dsp_adjust = false;
    L_dsp.sel = 0;
    L_dsp.first = 0;
}

static void build_display(void)
{
    page_t *pg = &P[MX_PAGE_DISPLAY];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    page_title(pg, "Display");
    L_dsp.parent = pg->scr;
    ui_list_of[MX_PAGE_DISPLAY] = &L_dsp;
    ui_list_add(&L_dsp, lv_color_hex(0xF5B301), LV_SYMBOL_EYE_OPEN, "Brightness", "", ui_row_click);
    ui_list_add(&L_dsp, lv_color_hex(0x7C4DFF), LV_SYMBOL_POWER, "Screen timeout", "", ui_row_click);
    ui_list_add(&L_dsp, lv_color_hex(0xE040A0), LV_SYMBOL_CHARGE, "Display speed", "", ui_row_click);
    ui_list_add(&L_dsp, C_GREY, LV_SYMBOL_LEFT, "Back", NULL, ui_row_click);
    ui_list_layout(&L_dsp);
    pg->refresh = display_refresh;
    pg->enter = display_enter;
    pg->rotate = display_rotate;
    pg->confirm = display_confirm;
    ui_register_hidden(MX_PAGE_DISPLAY, MX_PAGE_SETTINGS);
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
    case T_ZONE:
        M->tz_idx = (M->tz_idx + 1) % mx_tz_n;
        if (A->set_tz) A->set_tz(M->tz_idx);
        break;
    case T_SET: ui_goto(MX_PAGE_TIMESET, true); return;
    case T_BACK: ui_back(); return;
    }
    time_refresh();
}

static void time_enter(void)
{
    L_time.sel = 0;
    L_time.first = 0;
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
static bool c_armed;

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
    ui_list_set(&L_chip, C_REINSTALL, c_armed ? "Tap again to install" : "Reinstall firmware", c_armed ? "About a minute, do not unplug" : "Write chip firmware", NULL);
    L_chip.skip[C_REINSTALL] = flashing;
    L_chip.skip[C_BACK] = flashing;
    ui_list_layout(&L_chip);
}

static bool chip_rotate(int dir)
{
    if (M->chip_state == MX_CHIP_FLASHING) return true;
    c_armed = false;
    return ui_list_rotate(&L_chip, dir);
}

static void chip_confirm(void)
{
    if (M->chip_state == MX_CHIP_FLASHING) return;
    if (L_chip.sel == C_BACK) { c_armed = false; ui_back(); return; }
    if (!c_armed) { c_armed = true; chip_refresh(); return; }
    c_armed = false;
    if (A->chip_reinstall) A->chip_reinstall();
    chip_refresh();
}

static void chip_enter(void)
{
    c_armed = false;
    L_chip.sel = 0;
    L_chip.first = 0;
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
    build_chip();
}
