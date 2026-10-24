/*
 * Host simulator: runs ui.c against a canned model and writes each page to a
 * PPM so the layout can be checked without hardware.
 *
 *   make -C sim && ./sim/render out/    (then tools/sim_montage.py)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "../firmware/main/ui/ui.h"

#define W 412
#define H 412
static uint32_t fb[W * H];
static lv_color_t buf1[W * H];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c)
{
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * W + x] = lv_color_to32(*c++);
    lv_disp_flush_ready(d);
}

static void settle(int ms)
{
    for (int i = 0; i < ms / 10; i++) { lv_tick_inc(10); lv_timer_handler(); }
}

static void dump(const char *dir, const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint32_t c = fb[i];
        fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
    }
    fclose(f);
}

static void tk(mx_ticker_t *t, const char *n, const char *p, float c, const char *hi, const char *lo, double qty, int seed)
{
    snprintf(t->sym, sizeof t->sym, "%sUSDT", n);
    snprintf(t->name, sizeof t->name, "%s", n);
    snprintf(t->price, sizeof t->price, "%s", p);
    snprintf(t->hi, sizeof t->hi, "%s", hi);
    snprintf(t->lo, sizeof t->lo, "%s", lo);
    t->change = c;
    t->qty = qty;
    t->spark_n = MX_SPARK_N;
    int v = 30;
    for (int i = 0; i < MX_SPARK_N; i++) {
        v += ((i * 7 + seed * 5) % 13) - (c >= 0 ? 4 : 8) + (c >= 0 ? 3 : 0);
        if (v < 5) v = 5;
        if (v > 95) v = 95;
        t->spark[i] = (uint8_t)(c >= 0 ? (i * 70 / MX_SPARK_N + (v % 25)) : (95 - i * 70 / MX_SPARK_N - (v % 25)));
    }
}

static void note(mx_model_t *m, mx_note_kind_t k, const char *t, const char *b, int mins_ago)
{
    mx_note_t *n = &m->notes[m->n_notes++];
    n->kind = k;
    snprintf(n->title, sizeof n->title, "%s", t);
    snprintf(n->body, sizeof n->body, "%s", b);
    n->ts = 1790000000 - mins_ago * 60;
}

void mx_activity(void) {}

/* a scripted finger, so taps, swipes and typing go through LVGL's real input path */
static lv_point_t f_pt;
static bool f_down;

static void touch_read(lv_indev_drv_t *d, lv_indev_data_t *data)
{
    (void)d;
    data->point = f_pt;
    data->state = f_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void press(int x, int y) { f_pt.x = x; f_pt.y = y; f_down = true; settle(60); }
static void release(void) { f_down = false; settle(120); }
static void tap(int x, int y) { press(x, y); release(); settle(250); }

static void swipe(int x0, int y0, int x1, int y1)
{
    press(x0, y0);
    for (int i = 1; i <= 10; i++) {
        f_pt.x = x0 + (x1 - x0) * i / 10;
        f_pt.y = y0 + (y1 - y0) * i / 10;
        settle(20);
    }
    release();
    settle(400);
}

/* centre of a key of the text keyboard (y0 = 110, keys 50 high, 56 apart) */
static void key(int row, int idx)
{
    static const int x0[4] = { 27, 46, 30, 57 };
    static const int unit[4][10] = {
        { 32, 32, 32, 32, 32, 32, 32, 32, 32, 32 }, { 32, 32, 32, 32, 32, 32, 32, 32, 32 },
        { 48, 32, 32, 32, 32, 32, 32, 32, 48 }, { 70, 141, 70 } };
    int x = x0[row];
    int gap = row == 3 ? 8 : 4;
    for (int i = 0; i < idx; i++) x += unit[row][i] + gap;
    tap(x + unit[row][idx] / 2, 110 + row * 56 + 25);
}

/* a stand-in for a camera frame: sky, hills and a sun, so scaling is visible */
static lv_color_t cam_buf[416 * 416], gal_buf[412 * 309];

static void paint(lv_color_t *b, int w, int h, int seed)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int r = 40 + y * 90 / h, g = 90 + y * 80 / h, bl = 200 - y * 60 / h;
            int dx = x - (w * (30 + seed * 10) / 100), dy = y - h / 4;
            if (dx * dx + dy * dy < (h / 9) * (h / 9)) { r = 255; g = 220; bl = 90; }
            if (y > h * 6 / 10 + (int)(20 * ((x * 7 + seed * 40) % 97) / 97.0)) { r = 40 + seed * 8; g = 120; bl = 60; }
            if (((x / 16) + (y / 16)) % 2 == 0 && y < 24) { r = g = bl = 255; }
            b[y * w + x] = lv_color_make(r, g, bl);
        }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    lv_init();
    static lv_disp_draw_buf_t db;
    lv_disp_draw_buf_init(&db, buf1, NULL, W * H);
    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.hor_res = W; dd.ver_res = H; dd.flush_cb = flush; dd.draw_buf = &db;
    lv_disp_drv_register(&dd);
    static lv_indev_drv_t id;
    lv_indev_drv_init(&id);
    id.type = LV_INDEV_TYPE_POINTER;
    id.read_cb = touch_read;
    lv_indev_drv_register(&id);

    static mx_model_t m;
    memset(&m, 0, sizeof m);
    m.time_valid = true;
    m.now.tm_hour = 10; m.now.tm_min = 24; m.now.tm_mday = 26; m.now.tm_mon = 7; m.now.tm_year = 126; m.now.tm_wday = 3;
    mktime(&m.now);
    m.battery = 92; m.charging = true; m.wifi = true; m.relay_ok = true; m.brightness = 80; m.volume = 50;
    m.wifi_state = MX_WIFI_CONNECTED; snprintf(m.ssid, sizeof m.ssid, "Maximus-5G");
    m.bt_on = true; m.timeout_idx = 1; m.auto_time = true; m.tz_idx = 1;
    m.sd_state = MX_SD_ST_OK; m.sd_total_mb = 30436; m.sd_free_mb = 28110;
    m.cam_state = MX_CAM_LIVE; m.chip_state = MX_CHIP_OK; snprintf(m.chip_ver, sizeof m.chip_ver, "2024.05.1");
    snprintf(m.ip, sizeof m.ip, "192.168.1.42");
    snprintf(m.pair_code, sizeof m.pair_code, "483920");
    snprintf(m.price_src, sizeof m.price_src, "COINBASE");
    static const struct { const char *s; int r; bool sec; } aps[] = {
        { "Maximus-5G", -48, true }, { "Home WiFi", -61, true }, { "CoffeeShop Guest", -72, false },
        { "Neighbour-2.4", -81, true }, { "xfinitywifi", -84, false } };
    for (unsigned i = 0; i < sizeof aps / sizeof *aps; i++) {
        snprintf(m.aps[i].ssid, sizeof m.aps[i].ssid, "%s", aps[i].s);
        m.aps[i].rssi = aps[i].r; m.aps[i].secure = aps[i].sec;
    }
    m.n_aps = 5;
    m.gal_count = 24; m.gal_idx = 2; snprintf(m.gal_name, sizeof m.gal_name, "IMG_0022.JPG");
    snprintf(m.gal_info, sizeof m.gal_info, "640x480  41 KB");
    tk(&m.tk[0], "BTC", "63782.41", 2.36f, "64210.00", "61400.00", 0.3, 1);
    tk(&m.tk[1], "ETH", "3420.18", 1.87f, "3480.00", "3310.00", 2.5, 3);
    tk(&m.tk[2], "SOL", "142.61", 4.21f, "146.00", "135.20", 40, 5);
    tk(&m.tk[3], "BNB", "582.30", -0.74f, "590.00", "575.10", 5, 8);
    m.n_tk = 4;
    snprintf(m.heard, sizeof m.heard, "What is the BTC trend?");
    snprintf(m.reply, sizeof m.reply, "BTC is up 2.4%% on the day, holding above the 62k level. Momentum is positive but volume is thin; I would watch 64.2k as the next resistance.");
    snprintf(m.journal_last, sizeof m.journal_last, "Felt confident about this breakout. Good entry and solid momentum.");
    m.journal_ts = 1790000000; m.journal_count = 12;
    note(&m, MX_NOTE_PRICE, "Price Alert", "BTC reached $63,500", 6);
    note(&m, MX_NOTE_NEWS, "News Alert", "Fed holds rates steady", 41);
    note(&m, MX_NOTE_SYSTEM, "System", "Battery 92%", 63);
    note(&m, MX_NOTE_MESSAGE, "Telegram", "Check the 4h chart", 90);
    m.pin_set = true;
    paint(cam_buf, 416, 416, 1);
    paint(gal_buf, 412, 309, 3);

    static mx_actions_t a;
    ui_init(&m, &a);
    settle(400);

#define SHOT(name) do { ui_refresh(); settle(450); dump(dir, name); } while (0)

    m.pin_set = false; m.locked = false;
    ui_ring_color(0x2F7BFF);
    SHOT("01_home");
    ui_ring_color(0);

    m.cam_pix = cam_buf; m.cam_w = 416; m.cam_h = 416;
    ui_goto(MX_PAGE_CAMERA, false); ui_camera_frame();
    SHOT("02_camera_photo");
    m.cam_video = true; SHOT("03_camera_video");
    m.cam_rec = true; m.cam_rec_s = 75; snprintf(m.cam_msg, sizeof m.cam_msg, "Recording to VID_0007.AVI"); SHOT("04_camera_recording");
    m.cam_rec = false; m.cam_video = false; m.cam_msg[0] = 0; m.sd_state = MX_SD_ST_NONE; SHOT("05_camera_no_card");
    m.sd_state = MX_SD_ST_OK; m.cam_state = MX_CAM_NOCHIP; m.cam_pix = NULL; ui_camera_frame(); SHOT("06_camera_no_chip");
    m.cam_state = MX_CAM_LIVE;

    m.gal_pix = gal_buf; m.gal_w = 412; m.gal_h = 309;
    ui_goto(MX_PAGE_GALLERY, false); ui_gallery_frame();
    SHOT("07_gallery");
    m.sd_state = MX_SD_ST_NONE; SHOT("08_gallery_no_card"); m.sd_state = MX_SD_ST_OK;
    ui_goto(MX_PAGE_VIEWER, false); ui_gallery_frame();
    SHOT("09_viewer");
    m.gal_video = true; snprintf(m.gal_name, sizeof m.gal_name, "VID_0006.AVI"); snprintf(m.gal_info, sizeof m.gal_info, "416x416  18 s  3.1 MB");
    SHOT("10_viewer_video");
    ui_input(MX_IN_CONFIRM); SHOT("11_viewer_menu");
    ui_input(MX_IN_RIGHT); ui_input(MX_IN_CONFIRM); SHOT("12_viewer_delete_armed");
    ui_input(MX_IN_RIGHT); ui_input(MX_IN_RIGHT); ui_input(MX_IN_CONFIRM);

    ui_goto(MX_PAGE_ASSIST, false);
    m.assist = MX_ASSIST_LISTENING; m.mic_level = 70; SHOT("13_assist");
    m.assist = MX_ASSIST_REPLY; SHOT("14_assist_reply");
    ui_goto(MX_PAGE_MARKET, false); SHOT("15_market");
    ui_goto(MX_PAGE_PORTFOLIO, false); SHOT("16_portfolio");
    ui_goto(MX_PAGE_JOURNAL, false); SHOT("17_journal");
    ui_goto(MX_PAGE_MODES, false); SHOT("18_modes");
    ui_goto(MX_PAGE_NOTES, false); SHOT("19_notes");

    ui_goto(MX_PAGE_SETTINGS, false); SHOT("20_settings");
    swipe(206, 320, 206, 140); SHOT("21_settings_scrolled");
    ui_goto(MX_PAGE_WIFI, false); SHOT("22_wifi");
    m.wifi_state = MX_WIFI_OFF; SHOT("23_wifi_off"); m.wifi_state = MX_WIFI_CONNECTED;
    /* tap "Home WiFi": the second network row */
    ui_goto(MX_PAGE_WIFI, false); settle(300); tap(206, 88 + 4 + 3 * 66 + 29); SHOT("24_wifi_password");
    key(2, 0); key(0, 9); key(1, 0); key(1, 1); key(1, 1); key(3, 0); key(0, 0); key(0, 1);
    SHOT("25_wifi_password_typed");
    press(27 + 36 + 16, 135); settle(200); dump(dir, "26_wifi_key_held"); release();
    key(3, 0); SHOT("27_wifi_letters");
    ui_goto(MX_PAGE_DISPLAY, false); SHOT("28_display");
    swipe(206, 330, 206, 120); SHOT("29_display_scrolled");
    ui_goto(MX_PAGE_DTEST, false); SHOT("30_dtest_white");
    for (int i = 0; i < 8; i++) tap(206, 150);
    SHOT("31_dtest_bars");
    tap(206, 150); settle(2800); SHOT("32_dtest_checker");
    swipe(80, 200, 330, 200); SHOT("33_dtest_back_to_display");
    ui_goto(MX_PAGE_TIME, false); SHOT("34_time");
    tap(206, 88 + 4 + 66 + 29); SHOT("35_time_zone");
    ui_goto(MX_PAGE_TIMESET, false); ui_input(MX_IN_RIGHT); ui_input(MX_IN_CONFIRM); ui_input(MX_IN_CONFIRM); ui_input(MX_IN_CONFIRM); SHOT("36_time_set");
    ui_goto(MX_PAGE_CHIP, false); SHOT("37_chip");
    m.chip_state = MX_CHIP_FLASHING; m.chip_pct = 63; SHOT("38_chip_flashing");
    m.chip_state = MX_CHIP_MISSING; SHOT("39_chip_missing"); m.chip_state = MX_CHIP_OK;
    ui_goto(MX_PAGE_SECURITY, false); SHOT("40_security");
    tap(206 - 88, 94 + 27); tap(206, 94 + 27); SHOT("41_security_two_digits");
    m.locked = false;

    /* Settings > Restart asks first */
    ui_goto(MX_PAGE_SETTINGS, false);
    ui_input(MX_IN_CONFIRM); for (int i = 0; i < 9; i++) ui_input(MX_IN_RIGHT); SHOT("42_settings_knob_on_restart");
    ui_input(MX_IN_CONFIRM); SHOT("43_restart_confirm");
    ui_input(MX_IN_CONFIRM);        /* the highlight starts on Cancel */

    /* swipes from Home */
    ui_goto(MX_PAGE_HOME, false); settle(300);
    swipe(206, 340, 206, 120); SHOT("44_app_grid");
    tap(206 + 98, 58 + 204 + 35); SHOT("45_grid_opened_settings");
    ui_goto(MX_PAGE_HOME, false); settle(300);
    swipe(206, 60, 206, 300); SHOT("46_quick_panel");
    swipe(206, 340, 206, 100); settle(200);
    swipe(340, 200, 80, 200); SHOT("47_swiped_to_camera");
    swipe(80, 200, 340, 200); SHOT("48_swiped_back_home");

    ui_show_alert("BTC/USDT", "Stop Loss Triggered", "$61,230.00", true); settle(300); dump(dir, "49_alert");
    ui_input(MX_IN_CONFIRM);
    ui_set_hold(1400); settle(300); dump(dir, "50_hold_click");
    ui_set_hold(3200); settle(300); dump(dir, "51_hold_home");
    ui_set_hold(5000); settle(300); dump(dir, "52_hold_off");
    ui_set_hold(0);
    m.battery = 18; m.charging = false; ui_refresh(); settle(300); dump(dir, "53_home_low_battery");
    /* a tap whose last point is misread far away must not count as a swipe */
    press(150, 220); f_pt.x = 400; settle(20); release(); settle(400);
    printf("after glitch tap page %d (expect %d)\n", ui_page(), MX_PAGE_HOME);
    printf("final page %d (expect %d)\n", ui_page(), MX_PAGE_HOME);
    puts("ok");
    return 0;
}
