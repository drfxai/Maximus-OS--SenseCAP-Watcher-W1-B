/*
 * Maximus OS - screens.
 *
 * Nine pages on a carousel (knob rotation or swipe), laid out for the round
 * 412x412 panel: everything important sits inside a ~330 px circle. Lists
 * (modes, notifications, settings) capture the knob once confirmed and give
 * it back through their "Back" row.
 */
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

LV_IMG_DECLARE(mx_img_helmet);
LV_IMG_DECLARE(mx_img_shield);
LV_IMG_DECLARE(mx_img_mic);

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

const mx_mode_info_t mx_modes[4] = {
    { "trading", "Trading Assistant" },
    { "analysis", "Market Analysis" },
    { "news", "News & Sentiment" },
    { "general", "General Assistant" },
};

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *time;
    lv_obj_t *batt;
    void (*refresh)(void);
    bool (*rotate)(int dir);  /* true = consumed */
    void (*confirm)(void);
    bool capture;
} page_t;

static mx_model_t *M;
static const mx_actions_t *A;
static page_t P[MX_PAGE_COUNT];
static mx_page_t cur = MX_PAGE_HOME;
static lv_obj_t *dots[MX_PAGE_COUNT];
static lv_obj_t *ring_arc;           /* mirrors the RGB ring on the home page */

/* overlays */
static lv_obj_t *hold_dim, *hold_box, *hold_arc, *hold_lbl;
static lv_obj_t *alert_box, *alert_t, *alert_s, *alert_p, *alert_ic;
static lv_timer_t *alert_timer;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static lv_obj_t *mk_scr(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, C_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_radius(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static lv_obj_t *mk_lbl(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *t, lv_align_t al, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, t);
    lv_obj_align(l, al, x, y);
    return l;
}

static lv_obj_t *mk_box(lv_obj_t *p, int w, int h, lv_color_t bg, int radius)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mk_circle(lv_obj_t *p, int d, lv_color_t bg)
{
    return mk_box(p, d, d, bg, LV_RADIUS_CIRCLE);
}

static void glow(lv_obj_t *o, lv_color_t c, int spread)
{
    lv_obj_set_style_shadow_color(o, c, 0);
    lv_obj_set_style_shadow_width(o, spread, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_60, 0);
}

static lv_color_t chg_color(float c) { return c >= 0 ? C_GREEN : C_RED; }

static const char *fmt_chg(char *buf, size_t n, float c)
{
    snprintf(buf, n, "%s%.2f%%", c >= 0 ? "+" : "", (double)c);
    return buf;
}

/* "63782.41" -> "$63,782.41" (the Montserrat faces carry ',' and '$') */
static const char *fmt_usd(char *out, size_t n, const char *price)
{
    const char *dot = strchr(price, '.');
    int ip = dot ? (int)(dot - price) : (int)strlen(price);
    size_t o = 0;
    if (o < n - 1) out[o++] = '$';
    for (int i = 0; i < ip && o < n - 1; i++) {
        if (i && (ip - i) % 3 == 0 && o < n - 1) out[o++] = ',';
        if (o < n - 1) out[o++] = price[i];
    }
    if (dot) {
        for (const char *d = dot; *d && o < n - 1; d++) out[o++] = *d;
    }
    out[o] = 0;
    return out;
}

static void time_str(char *buf, size_t n)
{
    if (!M->time_valid) { snprintf(buf, n, "--:--"); return; }
    snprintf(buf, n, "%02d:%02d", M->now.tm_hour, M->now.tm_min);
}

static void date_str(char *buf, size_t n)
{
    if (!M->time_valid) { buf[0] = 0; return; }
    strftime(buf, n, "%a, %b %d", &M->now);
}

static void goto_dir(int dir);

static void on_scr_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ui_input(MX_IN_CONFIRM);
    } else if (code == LV_EVENT_GESTURE) {
        lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (d == LV_DIR_LEFT) ui_input(MX_IN_RIGHT);
        else if (d == LV_DIR_RIGHT) ui_input(MX_IN_LEFT);
    }
}

/* Status chrome: time + battery on top, page dots at the bottom. */
static void chrome(page_t *pg, mx_page_t idx, bool with_time)
{
    lv_obj_add_event_cb(pg->scr, on_scr_event, LV_EVENT_ALL, NULL);
    if (with_time) {
        pg->time = mk_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "--:--", LV_ALIGN_TOP_MID, 0, 22);
    }
    pg->batt = mk_lbl(pg->scr, &lv_font_montserrat_14, C_GREEN, "", LV_ALIGN_TOP_MID, 62, 28);
    (void)idx;
}

static void dots_build(lv_obj_t *scr, lv_obj_t **arr)
{
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 160, 12);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < MX_PAGE_COUNT; i++) {
        arr[i] = mk_circle(row, 7, lv_color_hex(0x3A4254));
        lv_obj_clear_flag(arr[i], LV_OBJ_FLAG_CLICKABLE);
    }
}

static lv_obj_t *page_dots[MX_PAGE_COUNT][MX_PAGE_COUNT];

static void dots_update(void)
{
    for (int pg = 0; pg < MX_PAGE_COUNT; pg++) {
        for (int i = 0; i < MX_PAGE_COUNT; i++) {
            bool on = (i == pg);
            lv_obj_set_style_bg_color(page_dots[pg][i], on ? C_BLUE : lv_color_hex(0x3A4254), 0);
            lv_obj_set_size(page_dots[pg][i], on ? 9 : 7, on ? 9 : 7);
        }
    }
}

/* ------------------------------------------------------------------ */
/* generic list (modes, notifications, settings)                       */
/* ------------------------------------------------------------------ */

#define LIST_MAX 10
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
} list_t;

static void list_layout(list_t *L)
{
    int ord = 0;
    for (int i = 0; i < L->n; i++) L->ord[i] = L->skip[i] ? -1 : ord++;
    L->count = ord;
    if (L->sel >= L->count) L->sel = L->count ? L->count - 1 : 0;
    if (L->sel < L->first) L->first = L->sel;
    if (L->sel >= L->first + LIST_VIS) L->first = L->sel - LIST_VIS + 1;
    if (L->first < 0) L->first = 0;
    for (int i = 0; i < L->n; i++) {
        int o = L->ord[i];
        if (o < 0 || o < L->first || o >= L->first + LIST_VIS) { lv_obj_add_flag(L->row[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(L->row[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(L->row[i], (W - 300) / 2, LIST_TOP + (o - L->first) * (ROW_H + ROW_GAP));
        bool sel = (o == L->sel);
        lv_obj_set_style_bg_color(L->row[i], sel ? C_PANEL_SEL : C_PANEL, 0);
        lv_obj_set_style_border_width(L->row[i], sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(L->row[i], C_BLUE, 0);
    }
}

static lv_obj_t *list_add(list_t *L, lv_color_t ic_bg, const char *ic_txt, const char *title, const char *sub, lv_event_cb_t cb)
{
    int i = L->n++;
    lv_obj_t *r = mk_box(L->parent, 300, ROW_H, C_PANEL, 18);
    L->row[i] = r;
    lv_obj_set_style_border_color(r, C_BLUE, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *c = mk_circle(r, 36, ic_bg);
    lv_obj_align(c, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    L->ic[i] = mk_lbl(c, &lv_font_montserrat_16, C_TEXT, ic_txt, LV_ALIGN_CENTER, 0, 0);

    L->tx[i] = mk_lbl(r, &lv_font_montserrat_16, C_TEXT, title, LV_ALIGN_TOP_LEFT, 56, sub ? 8 : 18);
    lv_obj_set_width(L->tx[i], 170);
    lv_label_set_long_mode(L->tx[i], LV_LABEL_LONG_DOT);
    L->sub[i] = NULL;
    if (sub) {
        L->sub[i] = mk_lbl(r, &lv_font_montserrat_12, C_DIM, sub, LV_ALIGN_TOP_LEFT, 56, 32);
        lv_obj_set_width(L->sub[i], 190);
        lv_label_set_long_mode(L->sub[i], LV_LABEL_LONG_DOT);
    }
    L->val[i] = mk_lbl(r, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_RIGHT_MID, -12, 0);
    return r;
}

static void list_set(list_t *L, int i, const char *title, const char *sub, const char *val)
{
    if (title) lv_label_set_text(L->tx[i], title);
    if (sub && L->sub[i]) lv_label_set_text(L->sub[i], sub);
    if (val) lv_label_set_text(L->val[i], val);
}

static bool list_rotate(list_t *L, int dir)
{
    int n = L->sel + dir;
    if (n < 0) n = 0;
    if (n >= L->count) n = L->count - 1;
    L->sel = n;
    list_layout(L);
    return true;
}

/* ------------------------------------------------------------------ */
/* HOME                                                                */
/* ------------------------------------------------------------------ */

static lv_obj_t *h_date;

static void home_refresh(void)
{
    char b[24];
    date_str(b, sizeof b);
    lv_label_set_text(h_date, b);
}

static void build_home(void)
{
    page_t *pg = &P[MX_PAGE_HOME];
    pg->scr = mk_scr();
    chrome(pg, MX_PAGE_HOME, true);
    lv_obj_align(pg->time, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_text_font(pg->time, &lv_font_montserrat_28, 0);
    h_date = mk_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_TOP_MID, 0, 64);

    /* status ring: same colour as the RGB LEDs */
    ring_arc = lv_arc_create(pg->scr);
    lv_obj_set_size(ring_arc, 406, 406);
    lv_obj_center(ring_arc);
    lv_arc_set_bg_angles(ring_arc, 0, 360);
    lv_arc_set_angles(ring_arc, 0, 0);
    lv_obj_remove_style(ring_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ring_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ring_arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring_arc, lv_color_hex(0x1B2438), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring_arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring_arc, C_BLUE, LV_PART_INDICATOR);

    lv_obj_t *halo = mk_circle(pg->scr, 200, lv_color_hex(0x050A18));
    lv_obj_align(halo, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_border_width(halo, 2, 0);
    lv_obj_set_style_border_color(halo, C_BLUE, 0);
    glow(halo, C_BLUE, 36);
    lv_obj_t *im = lv_img_create(halo);
    lv_img_set_src(im, &mx_img_helmet);
    lv_obj_center(im);

    mk_lbl(pg->scr, &lv_font_montserrat_28, C_TEXT, "MAXIMUS", LV_ALIGN_CENTER, 0, 124);
    mk_lbl(pg->scr, &lv_font_montserrat_12, C_DIM, "AI COMPANION", LV_ALIGN_CENTER, 0, 150);
    pg->refresh = home_refresh;
}

/* ------------------------------------------------------------------ */
/* MARKET                                                              */
/* ------------------------------------------------------------------ */

static lv_obj_t *m_coin, *m_pair, *m_price, *m_chg, *m_chart, *m_hl, *m_src;
static lv_chart_series_t *m_ser;

static lv_color_t coin_color(const char *name)
{
    if (!strcmp(name, "BTC")) return lv_color_hex(0xF7931A);
    if (!strcmp(name, "ETH")) return lv_color_hex(0x627EEA);
    if (!strcmp(name, "SOL")) return lv_color_hex(0x9945FF);
    if (!strcmp(name, "BNB")) return lv_color_hex(0xF3BA2F);
    return lv_color_hex(0x4A5568);
}

static void spark_to_chart(lv_obj_t *chart, lv_chart_series_t *ser, const mx_ticker_t *t, lv_color_t col)
{
    if (t->spark_n < 2) {
        lv_obj_add_flag(chart, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_HIDDEN);
    lv_chart_set_point_count(chart, t->spark_n);
    ser->color = col;
    for (int i = 0; i < t->spark_n; i++) lv_chart_set_value_by_id(chart, ser, i, t->spark[i]);
    lv_chart_refresh(chart);
}

static void market_refresh(void)
{
    if (M->n_tk == 0) {
        lv_label_set_text(m_pair, "Prices");
        lv_label_set_text(m_price, M->wifi ? "Loading" : "No Wi-Fi");
        lv_obj_set_style_text_color(m_price, C_DIM, 0);
        lv_label_set_text(m_chg, "");
        lv_label_set_text(m_hl, "");
        lv_obj_add_flag(m_chart, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (M->sel_tk >= M->n_tk) M->sel_tk = 0;
    const mx_ticker_t *t = &M->tk[M->sel_tk];
    char b[64], c[24];
    lv_color_t col = chg_color(t->change);
    lv_obj_set_style_bg_color(m_coin, coin_color(t->name), 0);
    lv_label_set_text_fmt(m_pair, "%.*s/USDT", 8, t->name);
    lv_label_set_text(m_price, fmt_usd(b, sizeof b, t->price));
    lv_obj_set_style_text_color(m_price, col, 0);
    lv_label_set_text_fmt(m_chg, "%s (24h)", fmt_chg(c, sizeof c, t->change));
    lv_obj_set_style_text_color(m_chg, col, 0);
    spark_to_chart(m_chart, m_ser, t, col);
    if (t->hi[0] && t->lo[0]) lv_label_set_text_fmt(m_hl, "H %s   L %s", t->hi, t->lo);
    else lv_label_set_text(m_hl, "");
    lv_label_set_text(m_src, M->price_src);
}

static void market_confirm(void)
{
    if (M->n_tk > 1) M->sel_tk = (M->sel_tk + 1) % M->n_tk;
    market_refresh();
}

static void build_market(void)
{
    page_t *pg = &P[MX_PAGE_MARKET];
    pg->scr = mk_scr();
    chrome(pg, MX_PAGE_MARKET, true);
    m_coin = mk_circle(pg->scr, 34, coin_color("BTC"));
    lv_obj_align(m_coin, LV_ALIGN_TOP_MID, -72, 66);
    mk_lbl(m_coin, &lv_font_montserrat_16, lv_color_white(), "B", LV_ALIGN_CENTER, 0, 0);
    m_pair = mk_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "BTC/USDT", LV_ALIGN_TOP_MID, 14, 72);
    m_price = mk_lbl(pg->scr, &lv_font_montserrat_40, C_GREEN, "--", LV_ALIGN_TOP_MID, 0, 112);
    m_chg = mk_lbl(pg->scr, &lv_font_montserrat_16, C_GREEN, "", LV_ALIGN_TOP_MID, 0, 164);

    m_chart = lv_chart_create(pg->scr);
    lv_obj_set_size(m_chart, 270, 110);
    lv_obj_align(m_chart, LV_ALIGN_TOP_MID, 0, 200);
    lv_chart_set_type(m_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(m_chart, LV_CHART_AXIS_PRIMARY_Y, -5, 105);
    lv_chart_set_div_line_count(m_chart, 0, 0);
    lv_obj_set_style_bg_opa(m_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_chart, 0, 0);
    lv_obj_set_style_pad_all(m_chart, 2, 0);
    lv_obj_set_style_size(m_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(m_chart, 3, LV_PART_ITEMS);
    lv_obj_clear_flag(m_chart, LV_OBJ_FLAG_CLICKABLE);
    m_ser = lv_chart_add_series(m_chart, C_GREEN, LV_CHART_AXIS_PRIMARY_Y);

    m_hl = mk_lbl(pg->scr, &lv_font_montserrat_12, C_DIM, "", LV_ALIGN_TOP_MID, 0, 322);
    m_src = mk_lbl(pg->scr, &lv_font_montserrat_12, lv_color_hex(0x4A5568), "", LV_ALIGN_TOP_MID, 0, 342);
    pg->refresh = market_refresh;
    pg->confirm = market_confirm;
}

/* ------------------------------------------------------------------ */
/* ASSISTANT                                                           */
/* ------------------------------------------------------------------ */

static lv_obj_t *a_orb, *a_title, *a_sub, *a_wave, *a_spin, *a_reply_box, *a_reply, *a_heard;
static lv_chart_series_t *a_ser;
static mx_assist_state_t a_last = (mx_assist_state_t)-1;
static char a_reply_cache[MX_REPLY_MAX];

static void reply_anim_cb(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, v); }

static void assist_refresh(void)
{
    mx_assist_state_t s = M->assist;
    bool listening = s == MX_ASSIST_LISTENING;
    bool reply = s == MX_ASSIST_REPLY || s == MX_ASSIST_ERROR;

    if (s != a_last) {
        a_last = s;
        lv_color_t col = listening ? C_GREEN : s == MX_ASSIST_THINKING ? C_BLUE : s == MX_ASSIST_ERROR ? C_RED : C_BLUE;
        lv_obj_set_style_border_color(a_orb, col, 0);
        glow(a_orb, col, listening ? 40 : 28);
        switch (s) {
        case MX_ASSIST_LISTENING:
            lv_label_set_text(a_title, "Listening...");
            lv_label_set_text(a_sub, "How can I help you?");
            break;
        case MX_ASSIST_THINKING:
            lv_label_set_text(a_title, "Thinking...");
            lv_label_set_text(a_sub, M->heard[0] ? M->heard : "");
            break;
        case MX_ASSIST_ERROR:
            lv_label_set_text(a_title, "Something went wrong");
            lv_label_set_text(a_sub, "");
            break;
        default:
            lv_label_set_text(a_title, reply ? mx_modes[M->mode].title : "Assistant");
            lv_label_set_text(a_sub, reply ? "" : "Press the knob and speak");
            break;
        }
        if (listening) lv_obj_clear_flag(a_wave, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(a_wave, LV_OBJ_FLAG_HIDDEN);
        if (s == MX_ASSIST_THINKING) lv_obj_clear_flag(a_spin, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(a_spin, LV_OBJ_FLAG_HIDDEN);
        if (reply) lv_obj_clear_flag(a_reply_box, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(a_reply_box, LV_OBJ_FLAG_HIDDEN);
        a_reply_cache[0] = 0;
    }
    if (reply && strcmp(a_reply_cache, M->reply) != 0) {
        snprintf(a_reply_cache, sizeof a_reply_cache, "%s", M->reply);
        lv_label_set_text(a_reply, M->reply);
        lv_obj_set_y(a_reply, 0);
        lv_anim_del(a_reply, reply_anim_cb);
        lv_obj_update_layout(a_reply);
        int over = lv_obj_get_height(a_reply) - lv_obj_get_height(a_reply_box);
        if (over > 0) {
            lv_anim_t an;
            lv_anim_init(&an);
            lv_anim_set_var(&an, a_reply);
            lv_anim_set_exec_cb(&an, reply_anim_cb);
            lv_anim_set_values(&an, 0, -over);
            lv_anim_set_delay(&an, 2500);
            lv_anim_set_time(&an, over * 60);
            lv_anim_set_playback_time(&an, 800);
            lv_anim_set_playback_delay(&an, 2500);
            lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&an);
        }
    }
    if (listening) {
        /* a lively bar chart from the live mic level */
        static const int shape[16] = { 25, 45, 70, 55, 90, 60, 100, 75, 85, 50, 95, 60, 70, 40, 55, 30 };
        static int phase;
        phase++;
        for (int i = 0; i < 16; i++) {
            int wob = shape[(i + phase) % 16];
            int v = 8 + (wob * (M->mic_level < 12 ? 12 : M->mic_level)) / 100;
            lv_chart_set_value_by_id(a_wave, a_ser, i, v > 100 ? 100 : v);
        }
        lv_chart_refresh(a_wave);
    }
}

static void assist_confirm(void)
{
    if (A->assistant_toggle) A->assistant_toggle();
}

static void build_assist(void)
{
    page_t *pg = &P[MX_PAGE_ASSIST];
    pg->scr = mk_scr();
    chrome(pg, MX_PAGE_ASSIST, true);

    a_orb = mk_circle(pg->scr, 128, lv_color_hex(0x07102A));
    lv_obj_align(a_orb, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_border_width(a_orb, 3, 0);
    lv_obj_set_style_border_color(a_orb, C_BLUE, 0);
    glow(a_orb, C_BLUE, 28);
    lv_obj_t *mic = lv_img_create(a_orb);
    lv_img_set_src(mic, &mx_img_mic);
    lv_obj_center(mic);

    a_spin = lv_spinner_create(pg->scr, 1200, 70);
    lv_obj_set_size(a_spin, 148, 148);
    lv_obj_align(a_spin, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_arc_width(a_spin, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a_spin, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a_spin, lv_color_hex(0x0B1530), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a_spin, C_BLUE, LV_PART_INDICATOR);
    lv_obj_add_flag(a_spin, LV_OBJ_FLAG_HIDDEN);

    a_title = mk_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "Assistant", LV_ALIGN_TOP_MID, 0, 214);
    a_sub = mk_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_TOP_MID, 0, 244);
    lv_obj_set_width(a_sub, 250);
    lv_label_set_long_mode(a_sub, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(a_sub, LV_TEXT_ALIGN_CENTER, 0);

    a_wave = lv_chart_create(pg->scr);
    lv_obj_set_size(a_wave, 170, 44);
    lv_obj_align(a_wave, LV_ALIGN_TOP_MID, 0, 290);
    lv_chart_set_type(a_wave, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(a_wave, 16);
    lv_chart_set_range(a_wave, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(a_wave, 0, 0);
    lv_obj_set_style_bg_opa(a_wave, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(a_wave, 0, 0);
    lv_obj_set_style_pad_all(a_wave, 0, 0);
    lv_obj_set_style_pad_column(a_wave, 3, LV_PART_ITEMS);
    lv_obj_set_style_radius(a_wave, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_column(a_wave, 2, 0);
    a_ser = lv_chart_add_series(a_wave, C_BLUE, LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < 16; i++) lv_chart_set_value_by_id(a_wave, a_ser, i, 20);
    lv_obj_add_flag(a_wave, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(a_wave, LV_OBJ_FLAG_CLICKABLE);

    a_reply_box = mk_box(pg->scr, 280, 116, C_BG, 0);
    lv_obj_align(a_reply_box, LV_ALIGN_TOP_MID, 0, 244);
    lv_obj_add_flag(a_reply_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(a_reply_box, LV_OBJ_FLAG_CLICKABLE);
    a_reply = lv_label_create(a_reply_box);
    lv_obj_set_width(a_reply, 280);
    lv_label_set_long_mode(a_reply, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(a_reply, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(a_reply, C_TEXT, 0);
    lv_obj_set_style_text_align(a_reply, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(a_reply, "");
    a_heard = NULL;

    pg->refresh = assist_refresh;
    pg->confirm = assist_confirm;
}

/* ------------------------------------------------------------------ */
/* PORTFOLIO                                                           */
/* ------------------------------------------------------------------ */

static lv_obj_t *p_total, *p_chg, *p_title, *p_rows[MX_MAX_TICKERS][4], *p_ic[MX_MAX_TICKERS];

static void portfolio_refresh(void)
{
    double total = 0, prev = 0;
    bool have_qty = false;
    for (int i = 0; i < M->n_tk; i++) {
        double p = atof(M->tk[i].price);
        if (M->tk[i].qty > 0) {
            have_qty = true;
            total += p * M->tk[i].qty;
            double base = 1.0 + M->tk[i].change / 100.0;
            prev += (base > 0.01) ? p * M->tk[i].qty / base : p * M->tk[i].qty;
        }
    }
    char b[40], c[24];
    lv_label_set_text(p_title, have_qty ? "Portfolio" : "Watchlist");
    if (have_qty) {
        snprintf(b, sizeof b, "%.2f", total);
        lv_label_set_text(p_total, fmt_usd(c, sizeof c, b));
        float pc = prev > 0 ? (float)((total - prev) / prev * 100.0) : 0;
        lv_label_set_text_fmt(p_chg, "%s (24h)", fmt_chg(c, sizeof c, pc));
        lv_obj_set_style_text_color(p_chg, chg_color(pc), 0);
    } else {
        lv_label_set_text(p_total, M->n_tk ? "Prices" : "--");
        lv_label_set_text(p_chg, M->n_tk ? "add holdings in setup" : "");
        lv_obj_set_style_text_color(p_chg, C_DIM, 0);
    }
    for (int i = 0; i < MX_MAX_TICKERS; i++) {
        bool on = i < M->n_tk;
        for (int k = 0; k < 4; k++) {
            if (on) lv_obj_clear_flag(p_rows[i][k], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(p_rows[i][k], LV_OBJ_FLAG_HIDDEN);
        }
        if (!on) { lv_obj_add_flag(p_ic[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(p_ic[i], LV_OBJ_FLAG_HIDDEN);
        const mx_ticker_t *t = &M->tk[i];
        lv_obj_set_style_bg_color(p_ic[i], coin_color(t->name), 0);
        lv_label_set_text(p_rows[i][0], t->name);
        char pr[24];
        snprintf(pr, sizeof pr, "%s", t->price);
        char *dot = strchr(pr, '.');
        if (dot && (dot - pr) >= 5) *dot = 0;           /* 63782.41 -> 63782 */
        lv_label_set_text(p_rows[i][1], pr);
        lv_label_set_text(p_rows[i][2], fmt_chg(c, sizeof c, t->change));
        lv_obj_set_style_text_color(p_rows[i][2], chg_color(t->change), 0);
    }
}

static void build_portfolio(void)
{
    page_t *pg = &P[MX_PAGE_PORTFOLIO];
    pg->scr = mk_scr();
    chrome(pg, MX_PAGE_PORTFOLIO, true);
    p_title = mk_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "Portfolio", LV_ALIGN_TOP_MID, 0, 62);
    p_total = mk_lbl(pg->scr, &lv_font_montserrat_40, C_TEXT, "--", LV_ALIGN_TOP_MID, 0, 88);
    p_chg = mk_lbl(pg->scr, &lv_font_montserrat_14, C_GREEN, "", LV_ALIGN_TOP_MID, 0, 138);
    for (int i = 0; i < MX_MAX_TICKERS; i++) {
        int y = 172 + i * 42;
        p_ic[i] = mk_circle(pg->scr, 28, coin_color("?"));
        lv_obj_set_pos(p_ic[i], 62, y);
        p_rows[i][3] = mk_lbl(p_ic[i], &lv_font_montserrat_14, lv_color_white(), "", LV_ALIGN_CENTER, 0, 0);
        p_rows[i][0] = mk_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_LEFT, 100, y + 5);
        p_rows[i][1] = mk_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_LEFT, 170, y + 5);
        p_rows[i][2] = mk_lbl(pg->scr, &lv_font_montserrat_14, C_GREEN, "", LV_ALIGN_TOP_RIGHT, -62, y + 7);
    }
    pg->refresh = portfolio_refresh;
}

/* ------------------------------------------------------------------ */
/* JOURNAL                                                             */
/* ------------------------------------------------------------------ */

static lv_obj_t *j_wave, *j_text, *j_meta, *j_hint;
static lv_chart_series_t *j_ser;

static void journal_refresh(void)
{
    if (M->journal_rec) {
        lv_label_set_text(j_text, "Recording... press to save");
        lv_obj_set_style_text_color(j_text, C_RED, 0);
    } else if (M->journal_last[0]) {
        lv_label_set_text(j_text, M->journal_last);
        lv_obj_set_style_text_color(j_text, C_TEXT, 0);
    } else {
        lv_label_set_text(j_text, "No entries yet. Press the knob and speak your thoughts.");
        lv_obj_set_style_text_color(j_text, C_DIM, 0);
    }
    char b[40] = "";
    if (M->journal_ts) {
        struct tm tmv;
        time_t t = M->journal_ts;
        gmtime_r(&t, &tmv);
        strftime(b, sizeof b, "%b %d, %H:%M", &tmv);
    }
    lv_label_set_text_fmt(j_meta, "%s%s%d saved", b, b[0] ? "  -  " : "", M->journal_count);
    static int phase;
    phase++;
    for (int i = 0; i < 28; i++) {
        int amp = M->journal_rec ? (M->mic_level < 25 ? 25 : M->mic_level) : 55;
        int v = 50 + ((i * 37 + phase * 11) % 41 - 20) * amp / 40;
        lv_chart_set_value_by_id(j_wave, j_ser, i, v < 3 ? 3 : v);
    }
    lv_chart_refresh(j_wave);
    lv_chart_set_type(j_wave, LV_CHART_TYPE_LINE);
    ((lv_chart_series_t *)j_ser)->color = M->journal_rec ? C_RED : C_BLUE;
}

static void journal_confirm(void)
{
    if (A->journal_toggle) A->journal_toggle();
}

static void build_journal(void)
{
    page_t *pg = &P[MX_PAGE_JOURNAL];
    pg->scr = mk_scr();
    chrome(pg, MX_PAGE_JOURNAL, true);
    mk_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "Journal", LV_ALIGN_TOP_MID, 0, 62);
    j_wave = lv_chart_create(pg->scr);
    lv_obj_set_size(j_wave, 250, 50);
    lv_obj_align(j_wave, LV_ALIGN_TOP_MID, 0, 100);
    lv_chart_set_type(j_wave, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(j_wave, 28);
    lv_chart_set_range(j_wave, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(j_wave, 0, 0);
    lv_obj_set_style_bg_opa(j_wave, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(j_wave, 0, 0);
    lv_obj_set_style_size(j_wave, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(j_wave, 2, LV_PART_ITEMS);
    lv_obj_clear_flag(j_wave, LV_OBJ_FLAG_CLICKABLE);
    j_ser = lv_chart_add_series(j_wave, C_BLUE, LV_CHART_AXIS_PRIMARY_Y);

    j_text = mk_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 166);
    lv_obj_set_width(j_text, 270);
    lv_obj_set_height(j_text, 110);
    lv_label_set_long_mode(j_text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(j_text, LV_TEXT_ALIGN_LEFT, 0);
    j_meta = mk_lbl(pg->scr, &lv_font_montserrat_12, C_GREEN, "", LV_ALIGN_TOP_MID, 0, 300);
    j_hint = mk_lbl(pg->scr, &lv_font_montserrat_12, C_DIM, "", LV_ALIGN_TOP_MID, 0, 324);
    pg->refresh = journal_refresh;
    pg->confirm = journal_confirm;
}

/* ------------------------------------------------------------------ */
/* AI MODES                                                            */
/* ------------------------------------------------------------------ */

static list_t L_modes;
static const uint32_t mode_cols[4] = { 0xF5B301, 0x7C4DFF, 0xE040A0, 0x1E90FF };
static const char *mode_ic[4] = { LV_SYMBOL_CHARGE, LV_SYMBOL_LIST, LV_SYMBOL_FILE, LV_SYMBOL_EDIT };
static const char *mode_sub[4] = { "Signals, risk, levels", "Charts and structure", "Headlines and mood", "Anything else" };

static void modes_pick(int i)
{
    if (i >= 4) { P[MX_PAGE_MODES].capture = false; list_layout(&L_modes); return; }
    M->mode = i;
    if (A->set_mode) A->set_mode(i);
}

static void modes_refresh(void)
{
    for (int i = 0; i < 4; i++) {
        list_set(&L_modes, i, NULL, NULL, i == M->mode ? LV_SYMBOL_OK : "");
        lv_obj_set_style_text_color(L_modes.val[i], C_GREEN, 0);
    }
}

static bool modes_rotate(int dir)
{
    if (!P[MX_PAGE_MODES].capture) return false;
    return list_rotate(&L_modes, dir);
}

static void modes_confirm(void)
{
    page_t *pg = &P[MX_PAGE_MODES];
    if (!pg->capture) { pg->capture = true; L_modes.sel = M->mode; list_layout(&L_modes); return; }
    modes_pick(L_modes.sel);
    modes_refresh();
}

static list_t *list_of[MX_PAGE_COUNT];

static void row_click(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_stop_bubbling(e);
    if (!list_of[cur]) return;
    P[cur].capture = true;
    list_of[cur]->sel = list_of[cur]->ord[i] >= 0 ? list_of[cur]->ord[i] : 0;
    ui_input(MX_IN_CONFIRM);
}

static void build_modes(void)
{
    page_t *pg = &P[MX_PAGE_MODES];
    pg->scr = mk_scr();
    chrome(pg, MX_PAGE_MODES, true);
    mk_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "AI Mode", LV_ALIGN_TOP_MID, 0, 56);
    L_modes.parent = pg->scr;
    list_of[MX_PAGE_MODES] = &L_modes;
    for (int i = 0; i < 4; i++) list_add(&L_modes, lv_color_hex(mode_cols[i]), mode_ic[i], mx_modes[i].title, mode_sub[i], row_click);
    list_add(&L_modes, lv_color_hex(0x3A4254), LV_SYMBOL_LEFT, "Back", NULL, row_click);
    list_layout(&L_modes);
    pg->refresh = modes_refresh;
    pg->rotate = modes_rotate;
    pg->confirm = modes_confirm;
}

/* ------------------------------------------------------------------ */
/* NOTIFICATIONS                                                       */
/* ------------------------------------------------------------------ */

static list_t L_notes;
static lv_obj_t *n_empty;

static void note_style(mx_note_kind_t k, lv_color_t *c, const char **ic)
{
    switch (k) {
    case MX_NOTE_PRICE:   *c = lv_color_hex(0xF7931A); *ic = LV_SYMBOL_BELL; break;
    case MX_NOTE_NEWS:    *c = C_PURPLE;               *ic = LV_SYMBOL_FILE; break;
    case MX_NOTE_SIGNAL:  *c = C_GREEN;                *ic = LV_SYMBOL_CHARGE; break;
    case MX_NOTE_MESSAGE: *c = C_BLUE;                 *ic = LV_SYMBOL_ENVELOPE; break;
    default:              *c = lv_color_hex(0x1E90FF); *ic = LV_SYMBOL_SETTINGS; break;
    }
}

static void notes_refresh(void)
{
    int n = M->n_notes;
    for (int i = 0; i < MX_MAX_NOTES; i++) {
        L_notes.skip[i] = i >= n;
        if (i >= n) continue;
        lv_color_t col;
        const char *ic;
        note_style(M->notes[i].kind, &col, &ic);
        lv_obj_set_style_bg_color(lv_obj_get_child(L_notes.row[i], 0), col, 0);
        lv_label_set_text(L_notes.ic[i], ic);
        list_set(&L_notes, i, M->notes[i].title, M->notes[i].body, "");
        char tb[8] = "";
        if (M->notes[i].ts) {
            struct tm tmv;
            time_t t = M->notes[i].ts;
            gmtime_r(&t, &tmv);
            snprintf(tb, sizeof tb, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        }
        lv_label_set_text(L_notes.val[i], tb);
        lv_obj_align(L_notes.val[i], LV_ALIGN_TOP_RIGHT, -12, 8);
    }
    L_notes.skip[MX_MAX_NOTES] = !P[MX_PAGE_NOTES].capture;   /* Back only while browsing */
    list_layout(&L_notes);
    if (n == 0) lv_obj_clear_flag(n_empty, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(n_empty, LV_OBJ_FLAG_HIDDEN);
}

static bool notes_rotate(int dir)
{
    if (!P[MX_PAGE_NOTES].capture) return false;
    return list_rotate(&L_notes, dir);
}

static void notes_confirm(void)
{
    page_t *pg = &P[MX_PAGE_NOTES];
    if (!pg->capture) {
        if (M->n_notes) { pg->capture = true; L_notes.sel = 0; }
        return;
    }
    if (L_notes.sel >= M->n_notes) { pg->capture = false; L_notes.sel = 0; return; }   /* Back */
    /* confirming a note dismisses it */
    int i = L_notes.sel;
    memmove(&M->notes[i], &M->notes[i + 1], sizeof(mx_note_t) * (MX_MAX_NOTES - i - 1));
    M->n_notes--;
    if (L_notes.sel > M->n_notes) L_notes.sel = M->n_notes;
}

static void build_notes(void)
{
    page_t *pg = &P[MX_PAGE_NOTES];
    pg->scr = mk_scr();
    chrome(pg, MX_PAGE_NOTES, true);
    mk_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "Notifications", LV_ALIGN_TOP_MID, 0, 56);
    n_empty = mk_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "All clear", LV_ALIGN_CENTER, 0, 0);
    L_notes.parent = pg->scr;
    list_of[MX_PAGE_NOTES] = &L_notes;
    for (int i = 0; i < MX_MAX_NOTES; i++) list_add(&L_notes, C_BLUE, LV_SYMBOL_BELL, "", "", row_click);
    list_add(&L_notes, lv_color_hex(0x3A4254), LV_SYMBOL_LEFT, "Back", NULL, row_click);
    pg->refresh = notes_refresh;
    pg->rotate = notes_rotate;
    pg->confirm = notes_confirm;
}

/* ------------------------------------------------------------------ */
/* SETTINGS                                                            */
/* ------------------------------------------------------------------ */

enum { S_WIFI, S_BRIGHT, S_VOL, S_TG, S_CAM, S_SEC, S_REBOOT, S_OFF, S_BACK, S_N };
static list_t L_set;

static void settings_refresh(void)
{
    char b[24];
    list_set(&L_set, S_WIFI, NULL, M->wifi ? (M->relay_ok ? "Relay connected" : "Relay unreachable") : "Not connected", M->wifi ? M->ip : "off");
    snprintf(b, sizeof b, "%d%%", M->brightness);
    list_set(&L_set, S_BRIGHT, NULL, NULL, b);
    snprintf(b, sizeof b, "%d%%", M->volume);
    list_set(&L_set, S_VOL, NULL, NULL, b);
    if (M->telegram_paired) list_set(&L_set, S_TG, NULL, "Bot paired", LV_SYMBOL_OK);
    else {
        char s[40];
        snprintf(s, sizeof s, "Send /start %s", M->pair_code[0] ? M->pair_code : "<code>");
        list_set(&L_set, S_TG, NULL, s, "");
    }
    list_set(&L_set, S_SEC, NULL, M->pin_set ? "Lock the watch now" : "Create a 4-digit PIN", "");
}

static bool settings_rotate(int dir)
{
    if (!P[MX_PAGE_SETTINGS].capture) return false;
    return list_rotate(&L_set, dir);
}

static int step_pct(int cur_v, const int *steps, int n)
{
    for (int i = 0; i < n; i++) if (cur_v < steps[i]) return steps[i];
    return steps[0];
}

static void settings_confirm(void)
{
    page_t *pg = &P[MX_PAGE_SETTINGS];
    if (!pg->capture) { pg->capture = true; list_layout(&L_set); return; }
    static const int bs[] = { 20, 40, 60, 80, 100 };
    static const int vs[] = { 0, 25, 50, 75, 100 };
    switch (L_set.sel) {
    case S_BRIGHT:
        M->brightness = step_pct(M->brightness, bs, 5);
        if (A->set_brightness) A->set_brightness(M->brightness);
        break;
    case S_VOL:
        M->volume = step_pct(M->volume, vs, 5);
        if (A->set_volume) A->set_volume(M->volume);
        break;
    case S_CAM:
        if (A->snapshot) A->snapshot();
        break;
    case S_SEC:
        pg->capture = false;
        if (M->pin_set) { M->locked = true; }
        ui_goto(MX_PAGE_SECURITY, true);
        break;
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
    pg->scr = mk_scr();
    chrome(pg, MX_PAGE_SETTINGS, true);
    mk_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "Settings", LV_ALIGN_TOP_MID, 0, 56);
    L_set.parent = pg->scr;
    list_of[MX_PAGE_SETTINGS] = &L_set;
    list_add(&L_set, lv_color_hex(0x1E90FF), LV_SYMBOL_WIFI, "Wi-Fi", "", row_click);
    list_add(&L_set, lv_color_hex(0xF5B301), LV_SYMBOL_EYE_OPEN, "Display", "Brightness", row_click);
    list_add(&L_set, lv_color_hex(0x7C4DFF), LV_SYMBOL_VOLUME_MAX, "Sound", "Volume", row_click);
    list_add(&L_set, lv_color_hex(0x229ED9), LV_SYMBOL_ENVELOPE, "Telegram", "", row_click);
    list_add(&L_set, lv_color_hex(0xE040A0), LV_SYMBOL_IMAGE, "Camera", "Send a photo to Telegram", row_click);
    list_add(&L_set, lv_color_hex(0x2ED573), LV_SYMBOL_EYE_CLOSE, "Security", "", row_click);
    list_add(&L_set, lv_color_hex(0xFFA502), LV_SYMBOL_REFRESH, "Restart", NULL, row_click);
    list_add(&L_set, C_RED, LV_SYMBOL_POWER, "Power off", NULL, row_click);
    list_add(&L_set, lv_color_hex(0x3A4254), LV_SYMBOL_LEFT, "Back", NULL, row_click);
    list_layout(&L_set);
    pg->refresh = settings_refresh;
    pg->rotate = settings_rotate;
    pg->confirm = settings_confirm;
}

/* ------------------------------------------------------------------ */
/* SECURITY (PIN)                                                      */
/* ------------------------------------------------------------------ */

#define PIN_LEN 4
static lv_obj_t *s_title, *s_sub, *s_digit, *s_dots[PIN_LEN];
static char pin_buf[PIN_LEN + 1], pin_first[PIN_LEN + 1];
static int pin_n, pin_digit;
static bool pin_confirming;

static void pin_draw(void)
{
    for (int i = 0; i < PIN_LEN; i++) {
        bool on = i < pin_n;
        lv_obj_set_style_bg_color(s_dots[i], on ? C_BLUE : C_BG, 0);
        lv_obj_set_style_border_color(s_dots[i], on ? C_BLUE : lv_color_hex(0x5B6B8C), 0);
    }
    lv_label_set_text_fmt(s_digit, "< %d >", pin_digit);
    if (M->pin_set) {
        lv_label_set_text(s_title, M->locked ? "Locked" : "Security Mode");
        lv_label_set_text(s_sub, "Enter PIN");
    } else {
        lv_label_set_text(s_title, "Create PIN");
        lv_label_set_text(s_sub, pin_confirming ? "Repeat the PIN" : "Choose 4 digits");
    }
}

static void pin_reset(void)
{
    pin_n = 0;
    pin_digit = 0;
    memset(pin_buf, 0, sizeof pin_buf);
    pin_draw();
}

static void security_refresh(void)
{
    pin_draw();
}

static bool security_rotate(int dir)
{
    pin_digit = (pin_digit + dir + 10) % 10;
    pin_draw();
    return true;
}

static void security_confirm(void)
{
    pin_buf[pin_n++] = (char)('0' + pin_digit);
    pin_digit = 0;
    if (pin_n < PIN_LEN) { pin_draw(); return; }
    pin_buf[PIN_LEN] = 0;
    if (M->pin_set) {
        if (A->pin_verify && A->pin_verify(pin_buf)) {
            M->locked = false;
            pin_reset();
            ui_goto(MX_PAGE_HOME, true);
            return;
        }
        lv_label_set_text(s_sub, "Wrong PIN");
        pin_reset();
        lv_label_set_text(s_sub, "Wrong PIN, try again");
        return;
    }
    if (!pin_confirming) {
        memcpy(pin_first, pin_buf, sizeof pin_first);
        pin_confirming = true;
        pin_reset();
        return;
    }
    if (!strcmp(pin_first, pin_buf) && A->pin_store && A->pin_store(pin_buf)) {
        M->pin_set = true;
        M->locked = false;
        pin_confirming = false;
        pin_reset();
        ui_goto(MX_PAGE_HOME, true);
        return;
    }
    pin_confirming = false;
    pin_reset();
    lv_label_set_text(s_sub, "PINs differ, start again");
}

static void build_security(void)
{
    page_t *pg = &P[MX_PAGE_SECURITY];
    pg->scr = mk_scr();
    chrome(pg, MX_PAGE_SECURITY, true);
    lv_obj_t *sh = lv_img_create(pg->scr);
    lv_img_set_src(sh, &mx_img_shield);
    lv_obj_align(sh, LV_ALIGN_TOP_MID, 0, 74);
    s_title = mk_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 214);
    s_sub = mk_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_TOP_MID, 0, 244);
    for (int i = 0; i < PIN_LEN; i++) {
        s_dots[i] = mk_circle(pg->scr, 16, C_BG);
        lv_obj_set_style_border_width(s_dots[i], 2, 0);
        lv_obj_align(s_dots[i], LV_ALIGN_TOP_MID, (i - (PIN_LEN - 1) / 2.0f) * 30, 278);
    }
    s_digit = mk_lbl(pg->scr, &lv_font_montserrat_28, C_TEXT, "< 0 >", LV_ALIGN_TOP_MID, 0, 312);
    pg->refresh = security_refresh;
    pg->rotate = security_rotate;
    pg->confirm = security_confirm;
    pg->capture = true;
}

/* ------------------------------------------------------------------ */
/* overlays                                                            */
/* ------------------------------------------------------------------ */

static void build_overlays(void)
{
    lv_obj_t *top = lv_layer_top();

    hold_dim = mk_box(top, W, H, C_BG, 0);
    lv_obj_set_style_bg_opa(hold_dim, LV_OPA_80, 0);
    lv_obj_add_flag(hold_dim, LV_OBJ_FLAG_HIDDEN);
    hold_box = mk_circle(top, 340, lv_color_hex(0x05070D));
    lv_obj_center(hold_box);
    hold_arc = lv_arc_create(hold_box);
    lv_obj_set_size(hold_arc, 320, 320);
    lv_obj_center(hold_arc);
    lv_arc_set_rotation(hold_arc, 270);
    lv_arc_set_bg_angles(hold_arc, 0, 360);
    lv_arc_set_range(hold_arc, 0, 100);
    lv_obj_remove_style(hold_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(hold_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(hold_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(hold_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(hold_arc, lv_color_hex(0x1B2438), LV_PART_MAIN);
    hold_lbl = mk_lbl(hold_box, &lv_font_montserrat_24, C_TEXT, "", LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(hold_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(hold_box, LV_OBJ_FLAG_HIDDEN);

    alert_box = mk_box(top, W, H, lv_color_hex(0x1A0507), 0);
    lv_obj_add_flag(alert_box, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE);
    alert_ic = mk_lbl(alert_box, &lv_font_montserrat_40, C_RED, LV_SYMBOL_WARNING, LV_ALIGN_TOP_MID, 0, 62);
    alert_t = mk_lbl(alert_box, &lv_font_montserrat_20, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 126);
    alert_s = mk_lbl(alert_box, &lv_font_montserrat_20, C_RED, "", LV_ALIGN_TOP_MID, 0, 162);
    lv_obj_set_width(alert_s, 280);
    lv_obj_set_style_text_align(alert_s, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(alert_s, LV_LABEL_LONG_DOT);
    alert_p = mk_lbl(alert_box, &lv_font_montserrat_28, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 214);
}

static void alert_hide_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_add_flag(alert_box, LV_OBJ_FLAG_HIDDEN);
    alert_timer = NULL;
}

void ui_show_alert(const char *title, const char *sub, const char *price, bool critical)
{
    lv_obj_set_style_bg_color(alert_box, critical ? lv_color_hex(0x1A0507) : lv_color_hex(0x1A1105), 0);
    lv_obj_set_style_text_color(alert_ic, critical ? C_RED : C_ORANGE, 0);
    lv_obj_set_style_text_color(alert_s, critical ? C_RED : C_ORANGE, 0);
    lv_label_set_text(alert_t, title ? title : "");
    lv_label_set_text(alert_s, sub ? sub : "");
    lv_label_set_text(alert_p, price ? price : "");
    lv_obj_align(alert_t, LV_ALIGN_TOP_MID, 0, 126);
    lv_obj_clear_flag(alert_box, LV_OBJ_FLAG_HIDDEN);
    if (alert_timer) lv_timer_del(alert_timer);
    alert_timer = lv_timer_create(alert_hide_cb, 9000, NULL);
    lv_timer_set_repeat_count(alert_timer, 1);
}

void ui_set_hold(int ms)
{
    if (ms < 600) {
        lv_obj_add_flag(hold_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hold_dim, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(hold_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hold_dim, LV_OBJ_FLAG_HIDDEN);
    int pct = ms * 100 / 10000;
    if (pct > 100) pct = 100;
    lv_arc_set_value(hold_arc, pct);
    lv_color_t col;
    const char *msg;
    if (ms >= 10000) { col = C_RED; msg = "Powering off..."; }
    else if (ms >= 5000) { col = C_ORANGE; msg = "Release: Home\nKeep holding:\npower off"; }
    else { col = C_BLUE; msg = "Hold 5 s: Home\nHold 10 s:\npower off"; }
    lv_obj_set_style_arc_color(hold_arc, col, LV_PART_INDICATOR);
    lv_label_set_text(hold_lbl, msg);
}

/* ------------------------------------------------------------------ */
/* core                                                                */
/* ------------------------------------------------------------------ */

void ui_ring_color(uint32_t rgb)
{
    if (!ring_arc) return;
    if (rgb == 0) {
        lv_arc_set_angles(ring_arc, 0, 0);
    } else {
        lv_arc_set_angles(ring_arc, 0, 359);
        lv_obj_set_style_arc_color(ring_arc, lv_color_hex(rgb), LV_PART_INDICATOR);
    }
}

mx_page_t ui_page(void) { return cur; }

static void chrome_refresh(void)
{
    char t[16], b[24];
    time_str(t, sizeof t);
    snprintf(b, sizeof b, "%s%d%%", M->charging ? LV_SYMBOL_CHARGE " " : "", M->battery);
    lv_color_t bc = M->battery <= 15 ? C_RED : M->battery <= 30 ? C_ORANGE : C_GREEN;
    for (int i = 0; i < MX_PAGE_COUNT; i++) {
        if (!P[i].scr) continue;
        if (P[i].time) lv_label_set_text(P[i].time, t);
        if (P[i].batt) {
            lv_label_set_text(P[i].batt, b);
            lv_obj_set_style_text_color(P[i].batt, bc, 0);
            lv_obj_align_to(P[i].batt, P[i].time, LV_ALIGN_OUT_RIGHT_MID, 10, 2);
        }
    }
}

void ui_refresh(void)
{
    chrome_refresh();
    if (P[cur].refresh) P[cur].refresh();
}

void ui_goto(mx_page_t page, bool animate)
{
    if (page < 0 || page >= MX_PAGE_COUNT) return;
    bool fwd = page >= cur;
    P[cur].capture = (cur == MX_PAGE_SECURITY) ? true : false;
    cur = page;
    if (page != MX_PAGE_SECURITY) P[page].capture = false;
    dots_update();
    if (page == MX_PAGE_SECURITY) pin_reset();
    ui_refresh();
    lv_scr_load_anim(P[page].scr, animate ? (fwd ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT) : LV_SCR_LOAD_ANIM_NONE,
                     animate ? 220 : 0, 0, false);
}

static void goto_dir(int dir)
{
    int n = ((int)cur + dir + MX_PAGE_COUNT) % MX_PAGE_COUNT;
    if (n == MX_PAGE_SECURITY) n = (n + dir + MX_PAGE_COUNT) % MX_PAGE_COUNT; /* only reachable from Settings */
    ui_goto((mx_page_t)n, true);
}

void ui_input(mx_input_t in)
{
    mx_activity();
    /* an alert swallows the first input and just closes */
    if (alert_box && !lv_obj_has_flag(alert_box, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(alert_box, LV_OBJ_FLAG_HIDDEN);
        if (alert_timer) { lv_timer_del(alert_timer); alert_timer = NULL; }
        return;
    }
    if (M->locked && cur != MX_PAGE_SECURITY) ui_goto(MX_PAGE_SECURITY, false);
    page_t *pg = &P[cur];
    switch (in) {
    case MX_IN_LEFT:
    case MX_IN_RIGHT: {
        int dir = in == MX_IN_RIGHT ? 1 : -1;
        if (pg->rotate && pg->rotate(dir)) break;
        if (M->locked) break;
        goto_dir(dir);
        break;
    }
    case MX_IN_CONFIRM:
        if (pg->confirm) pg->confirm();
        else if (cur == MX_PAGE_HOME) ui_goto(MX_PAGE_ASSIST, true);
        break;
    case MX_IN_HOME:
        if (!M->locked) ui_goto(MX_PAGE_HOME, true);
        break;
    }
    ui_refresh();
}

void ui_init(mx_model_t *model, const mx_actions_t *actions)
{
    M = model;
    A = actions;
    lv_obj_t *top = lv_layer_top();
    (void)top;
    build_home();
    build_market();
    build_assist();
    build_portfolio();
    build_journal();
    build_modes();
    build_notes();
    build_settings();
    build_security();
    for (int i = 0; i < MX_PAGE_COUNT; i++) dots_build(P[i].scr, page_dots[i]);
    build_overlays();
    cur = MX_PAGE_HOME;
    dots_update();
    ui_refresh();
    lv_scr_load(P[MX_PAGE_HOME].scr);
    if (M->locked) ui_goto(MX_PAGE_SECURITY, false);
}
