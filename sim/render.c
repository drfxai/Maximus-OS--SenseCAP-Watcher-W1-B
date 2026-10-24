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

    static mx_model_t m;
    memset(&m, 0, sizeof m);
    m.time_valid = true;
    m.now.tm_hour = 10; m.now.tm_min = 24; m.now.tm_mday = 26; m.now.tm_mon = 7; m.now.tm_year = 126; m.now.tm_wday = 3;
    mktime(&m.now);
    m.battery = 92; m.wifi = true; m.relay_ok = true; m.brightness = 80; m.volume = 50;
    snprintf(m.ip, sizeof m.ip, "192.168.1.42");
    snprintf(m.pair_code, sizeof m.pair_code, "483920");
    snprintf(m.price_src, sizeof m.price_src, "COINBASE");
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

    static mx_actions_t a;
    ui_init(&m, &a);
    settle(400);

    static const char *names[MX_PAGE_COUNT] = { "01_home", "02_market", "03_assist", "04_portfolio", "05_journal", "06_modes", "07_notes", "08_settings", "09_security" };
    for (int p = 0; p < MX_PAGE_COUNT; p++) {
        ui_goto((mx_page_t)p, false);
        if (p == MX_PAGE_ASSIST) { m.assist = MX_ASSIST_LISTENING; m.mic_level = 70; }
        if (p == MX_PAGE_HOME) ui_ring_color(0x2F7BFF);
        ui_refresh();
        settle(500);
        dump(dir, names[p]);
    }
    /* extra states */
    ui_goto(MX_PAGE_ASSIST, false);
    m.assist = MX_ASSIST_REPLY; ui_refresh(); settle(500); dump(dir, "10_assist_reply");
    m.assist = MX_ASSIST_THINKING; ui_refresh(); settle(500); dump(dir, "11_assist_thinking");
    ui_goto(MX_PAGE_SETTINGS, false);
    ui_input(MX_IN_CONFIRM); ui_input(MX_IN_RIGHT); ui_input(MX_IN_RIGHT); ui_refresh(); settle(300); dump(dir, "12_settings_sel");
    ui_goto(MX_PAGE_MODES, false);
    ui_input(MX_IN_CONFIRM); ui_input(MX_IN_RIGHT); ui_refresh(); settle(300); dump(dir, "13_modes_sel");
    ui_goto(MX_PAGE_HOME, false);
    ui_show_alert("BTC/USDT", "Stop Loss Triggered", "$61,230.00", true); settle(300); dump(dir, "14_alert");
    ui_input(MX_IN_CONFIRM);
    ui_set_hold(6500); settle(300); dump(dir, "15_hold");
    ui_set_hold(0);
    puts("ok");
    return 0;
}
