/*
 * Maximus OS - screens.
 *
 * Ten pages on a carousel, laid out for the round 412x412 panel: everything
 * important sits inside a ~340 px circle. Settings opens sub-pages that sit
 * off the carousel and return to their parent.
 *
 * Touch, like a phone:
 *   swipe right-to-left / left-to-right  next / previous page (Back on a sub-page)
 *   swipe top-to-bottom                  quick panel (Wi-Fi, Bluetooth, brightness, volume)
 *   swipe bottom-to-top                  app grid
 *   drag a list                          scroll it; tap a row to open it
 * Knob: rotate = move, click (under 2 s) = select, hold 2 s = Home, hold 5 s
 * = power off. Lists on the carousel take the knob after a first click and
 * give it back through their "Back" row.
 *
 * This file is the core (widgets, lists, gestures, navigation) and the AI
 * pages; ui_shell.c holds the app grid, quick panel and dialogs, ui_kb.c the
 * keyboards, ui_settings.c and ui_camera.c the rest.
 */
#include "ui_priv.h"

LV_IMG_DECLARE(mx_img_helmet);
LV_IMG_DECLARE(mx_img_shield);
LV_IMG_DECLARE(mx_img_mic);

mx_model_t *ui_M;
const mx_actions_t *ui_A;
page_t ui_P[MX_PAGE_COUNT];
mx_page_t ui_cur = MX_PAGE_HOME;
ui_list_t *ui_list_of[MX_PAGE_COUNT];

#define M ui_M
#define A ui_A
#define P ui_P
#define cur ui_cur

const mx_mode_info_t mx_modes[4] = {
    { "trading", "Trading Assistant" },
    { "analysis", "Market Analysis" },
    { "news", "News & Sentiment" },
    { "general", "General Assistant" },
};

static lv_obj_t *ring_arc;           /* mirrors the RGB ring on the home page */
static bool knob_mode;               /* the knob was used last: show its highlight */

/* overlays */
static lv_obj_t *hold_dim, *hold_box, *hold_arc, *hold_lbl, *blank_box;
static lv_obj_t *alert_box, *alert_t, *alert_s, *alert_p, *alert_ic;
static lv_timer_t *alert_timer;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

lv_obj_t *ui_scr(void)
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

lv_obj_t *ui_lbl(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *t, lv_align_t al, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, t);
    lv_obj_align(l, al, x, y);
    return l;
}

lv_obj_t *ui_box(lv_obj_t *p, int w, int h, lv_color_t bg, int radius)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    /* decoration only: taps fall through to the page unless a caller opts in */
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *ui_circle(lv_obj_t *p, int d, lv_color_t bg)
{
    return ui_box(p, d, d, bg, LV_RADIUS_CIRCLE);
}

void ui_grad(lv_obj_t *o, lv_color_t top, lv_color_t bottom)
{
    lv_obj_set_style_bg_color(o, top, 0);
    lv_obj_set_style_bg_grad_color(o, bottom, 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
}

void ui_glow(lv_obj_t *o, lv_color_t c, int spread)
{
    lv_obj_set_style_shadow_color(o, c, 0);
    lv_obj_set_style_shadow_width(o, spread, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_60, 0);
}

void ui_show(lv_obj_t *o, bool on)
{
    if (!o) return;
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* A pill or round button that darkens while pressed and calls cb on a tap. */
lv_obj_t *ui_button(lv_obj_t *p, int w, int h, lv_color_t bg, const char *txt, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_box(p, w, h, bg, h / 2 > 26 ? 26 : h / 2);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, lv_color_lighten(bg, 40), LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(b, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(b, -2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, ui_press_cb, LV_EVENT_PRESSED, NULL);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    if (txt) {
        lv_obj_t *l = ui_lbl(b, &lv_font_montserrat_16, C_TEXT, txt, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }
    return b;
}

/* Setting identical text still invalidates the label; skip it. */
void ui_set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

void ui_text_color(lv_obj_t *o, lv_color_t c)
{
    if (lv_obj_get_style_text_color(o, LV_PART_MAIN).full != c.full) lv_obj_set_style_text_color(o, c, 0);
}

void ui_bg_color(lv_obj_t *o, lv_color_t c)
{
    if (lv_obj_get_style_bg_color(o, LV_PART_MAIN).full != c.full) lv_obj_set_style_bg_color(o, c, 0);
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
    if (!M->time_valid) { snprintf(buf, n, "Time not set"); return; }
    strftime(buf, n, "%A, %B %d", &M->now);
}

/* A finger that moved is a swipe, not a tap: LVGL still sends CLICKED after
   a swipe over a non-scrollable object, so remember where the press began. */
static lv_point_t press_pt;

void ui_press_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *i = lv_indev_get_act();
    if (i) lv_indev_get_point(i, &press_pt);
}

bool ui_is_tap(void)
{
    lv_indev_t *i = lv_indev_get_act();
    if (!i) return true;                       /* synthetic click from code */
    lv_point_t p;
    lv_indev_get_point(i, &p);
    return abs(p.x - press_pt.x) < 18 && abs(p.y - press_pt.y) < 18;
}

static void on_scr_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        ui_press_cb(e);
    } else if (code == LV_EVENT_CLICKED) {
        /* a tap on empty space: only pages that ask for it react */
        if (!ui_is_tap() || M->locked) return;
        mx_activity();
        if (P[cur].tap) {
            P[cur].tap();
            ui_refresh();
        }
    }
}

/* ---- battery icon ------------------------------------------------- */

#define BATT_IN_W 24

void ui_batt_create(batt_t *b, lv_obj_t *scr)
{
    b->cont = lv_obj_create(scr);
    lv_obj_remove_style_all(b->cont);
    lv_obj_set_size(b->cont, 52, 18);
    lv_obj_clear_flag(b->cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* the bolt sits left of the outline while USB power is in */
    b->bolt = ui_lbl(b->cont, &lv_font_montserrat_14, C_GREEN, LV_SYMBOL_CHARGE, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(b->bolt, LV_OBJ_FLAG_HIDDEN);

    b->body = ui_box(b->cont, BATT_IN_W + 6, 16, C_BG, 4);
    lv_obj_set_style_bg_opa(b->body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b->body, 2, 0);
    lv_obj_set_style_border_color(b->body, lv_color_hex(0xC9CEDA), 0);
    lv_obj_align(b->body, LV_ALIGN_LEFT_MID, 17, 0);

    b->nub = ui_box(b->cont, 3, 7, lv_color_hex(0xC9CEDA), 1);
    lv_obj_align(b->nub, LV_ALIGN_LEFT_MID, 17 + BATT_IN_W + 6, 0);

    b->fill = ui_box(b->body, BATT_IN_W, 10, C_GREEN, 2);
    lv_obj_align(b->fill, LV_ALIGN_LEFT_MID, 1, 0);

    b->pct = ui_lbl(scr, &lv_font_montserrat_14, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 0);
    b->shown_pct = -1;
    b->shown_chg = -1;
}

void ui_batt_apply(batt_t *b)
{
    if (!b->cont) return;
    int pct = M->battery < 0 ? 0 : M->battery > 100 ? 100 : M->battery;
    int chg = M->charging ? 1 : 0;
    if (pct == b->shown_pct && chg == b->shown_chg) return;
    b->shown_pct = pct;
    b->shown_chg = chg;
    lv_color_t col = chg ? C_GREEN : pct > 30 ? lv_color_hex(0xE9EDF5) : pct > 15 ? C_ORANGE : C_RED;
    int w = BATT_IN_W * pct / 100;
    if (w < 3) w = 3;
    lv_obj_set_width(b->fill, w);
    lv_obj_set_style_bg_color(b->fill, col, 0);
    ui_show(b->bolt, chg);
    char t[8];
    snprintf(t, sizeof t, "%d%%", pct);
    ui_set_text(b->pct, t);
    lv_obj_set_style_text_color(b->pct, pct <= 15 && !chg ? C_RED : C_TEXT, 0);
}

/* Time in the middle, battery to its left, percentage to its right. */
void ui_chrome_place(page_t *pg)
{
    if (!pg->time) {
        lv_obj_align(pg->batt.cont, LV_ALIGN_TOP_MID, -20, 16);
        lv_obj_align_to(pg->batt.pct, pg->batt.cont, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
        return;
    }
    lv_obj_update_layout(pg->time);
    lv_obj_align_to(pg->batt.cont, pg->time, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_obj_align_to(pg->batt.pct, pg->time, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
}

/* Status chrome: battery and clock on top, page dots at the bottom. */
void ui_chrome(page_t *pg, bool with_time)
{
    lv_obj_add_event_cb(pg->scr, on_scr_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(pg->scr, on_scr_event, LV_EVENT_CLICKED, NULL);
    if (with_time) {
        pg->time = ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "00:00", LV_ALIGN_TOP_MID, 0, 20);
    }
    ui_batt_create(&pg->batt, pg->scr);
    ui_chrome_place(pg);
}

static void dots_build(lv_obj_t *scr, lv_obj_t **arr)
{
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 170, 12);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 7, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < MX_PAGE_CAROUSEL_N; i++) {
        arr[i] = ui_box(row, 7, 7, C_FAINT, 4);
        lv_obj_clear_flag(arr[i], LV_OBJ_FLAG_CLICKABLE);
    }
}

static lv_obj_t *page_dots[MX_PAGE_CAROUSEL_N][MX_PAGE_CAROUSEL_N];

static void dots_update(void)
{
    if (cur >= MX_PAGE_CAROUSEL_N) return;
    for (int pg = 0; pg < MX_PAGE_CAROUSEL_N; pg++) {
        if (!P[pg].scr) continue;
        for (int i = 0; i < MX_PAGE_CAROUSEL_N; i++) {
            bool on = (i == pg);
            lv_obj_set_style_bg_color(page_dots[pg][i], on ? C_BLUE : C_FAINT, 0);
            lv_obj_set_size(page_dots[pg][i], on ? 18 : 7, 7);
        }
    }
}

/* ------------------------------------------------------------------ */
/* lists                                                               */
/* ------------------------------------------------------------------ */

static void list_ensure(ui_list_t *L)
{
    if (L->cont) return;
    int top = L->top ? L->top : LIST_TOP;
    int h = L->height ? L->height : LIST_BOTTOM - top;
    if (h < ROW_H + 8) h = ROW_H + 8;
    lv_obj_t *c = lv_obj_create(L->parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, ROW_W + 8, h);
    lv_obj_set_pos(c, (W - ROW_W - 8) / 2, top);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, ROW_GAP, 0);
    lv_obj_set_style_pad_ver(c, 4, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    L->cont = c;
    L->shown_sel = -1;
}

static bool list_highlight_on(const ui_list_t *L)
{
    if (!knob_mode) return false;
    if (ui_list_of[cur] == L) return P[cur].capture || P[cur].hidden;
    return true;
}

void ui_list_layout(ui_list_t *L)
{
    int ord = 0;
    for (int i = 0; i < L->n; i++) L->ord[i] = L->skip[i] ? -1 : ord++;
    L->count = ord;
    if (L->sel >= L->count) L->sel = L->count ? L->count - 1 : 0;
    if (L->sel < 0) L->sel = 0;
    bool hl = list_highlight_on(L);
    uint32_t sig = (uint32_t)L->sel * 31u + (hl ? 7u : 3u);
    for (int i = 0; i < L->n; i++) sig = sig * 33u + (L->skip[i] ? 1u : 2u);
    if (L->sig_ok && sig == L->sig) return;
    L->sig = sig;
    L->sig_ok = true;
    for (int i = 0; i < L->n; i++) {
        int o = L->ord[i];
        ui_show(L->row[i], o >= 0);
        if (o < 0) continue;
        bool sel = hl && o == L->sel;
        lv_obj_set_style_bg_color(L->row[i], sel ? C_PANEL_SEL : C_PANEL, 0);
        lv_obj_set_style_border_width(L->row[i], sel ? 2 : 0, 0);
        if (sel && L->sel != L->shown_sel) lv_obj_scroll_to_view(L->row[i], LV_ANIM_ON);
    }
    if (hl) L->shown_sel = L->sel;
}

void ui_list_reset(ui_list_t *L)
{
    L->sel = 0;
    L->first = 0;
    L->shown_sel = -1;
    L->sig_ok = false;
    if (L->cont) lv_obj_scroll_to_y(L->cont, 0, LV_ANIM_OFF);
}

lv_obj_t *ui_list_add(ui_list_t *L, lv_color_t ic_bg, const char *ic_txt, const char *title, const char *sub, lv_event_cb_t cb)
{
    if (L->n >= LIST_MAX) return NULL;
    list_ensure(L);
    int i = L->n++;
    lv_obj_t *r = ui_box(L->cont, ROW_W, ROW_H, C_PANEL, 20);
    L->row[i] = r;
    lv_obj_set_style_border_color(r, C_BLUE, 0);
    lv_obj_set_style_bg_color(r, C_PANEL_HI, LV_STATE_PRESSED);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, ui_press_cb, LV_EVENT_PRESSED, NULL);
    if (cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *c = ui_circle(r, 38, ic_bg);
    ui_grad(c, lv_color_lighten(ic_bg, 50), lv_color_darken(ic_bg, 40));
    lv_obj_align(c, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    L->ic[i] = ui_lbl(c, &lv_font_montserrat_16, C_TEXT, ic_txt, LV_ALIGN_CENTER, 0, 0);

    L->tx[i] = ui_lbl(r, &lv_font_montserrat_16, C_TEXT, title, LV_ALIGN_TOP_LEFT, 58, sub ? 9 : 19);
    lv_obj_set_width(L->tx[i], 168);
    lv_label_set_long_mode(L->tx[i], LV_LABEL_LONG_DOT);
    L->sub[i] = NULL;
    if (sub) {
        L->sub[i] = ui_lbl(r, &lv_font_montserrat_12, C_DIM, sub, LV_ALIGN_TOP_LEFT, 58, 33);
        lv_obj_set_width(L->sub[i], 186);
        lv_label_set_long_mode(L->sub[i], LV_LABEL_LONG_DOT);
    }
    L->val[i] = ui_lbl(r, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_RIGHT_MID, -14, 0);
    L->sig_ok = false;
    return r;
}

void ui_list_set(ui_list_t *L, int i, const char *title, const char *sub, const char *val)
{
    if (title) ui_set_text(L->tx[i], title);
    if (sub && L->sub[i]) ui_set_text(L->sub[i], sub);
    if (val) ui_set_text(L->val[i], val);
}

bool ui_list_rotate(ui_list_t *L, int dir)
{
    int n = L->sel + dir;
    if (n < 0) n = 0;
    if (n >= L->count) n = L->count - 1;
    L->sel = n;
    ui_list_layout(L);
    return true;
}

int ui_list_row(const ui_list_t *L)
{
    for (int i = 0; i < L->n; i++) if (L->ord[i] == L->sel) return i;
    return -1;
}

void ui_row_click(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_stop_bubbling(e);
    if (!ui_is_tap() || M->locked) return;
    ui_list_t *L = ui_list_of[cur];
    if (!L) return;
    mx_activity();
    P[cur].capture = true;
    L->sel = L->ord[i] >= 0 ? L->ord[i] : 0;
    L->sig_ok = false;
    if (P[cur].confirm) P[cur].confirm();
    ui_refresh();
}

/* ------------------------------------------------------------------ */
/* gestures                                                            */
/* ------------------------------------------------------------------ */

/* LVGL's own gesture event fires on whatever object is under the finger and
   gets lost inside scrolling lists, so the swipe is read here straight from
   the touch input device: start point, end point, and whether a list
   scrolled in between. */
#define SWIPE_MIN 56

static struct {
    lv_indev_t *indev;
    bool down, scrolled, on_screen, on_slider;   /* where the press began */
    lv_point_t p0, last;
    uint32_t t0;
} G;

static void gesture_eval(void)
{
    int dx = G.last.x - G.p0.x, dy = G.last.y - G.p0.y;
    int ax = abs(dx), ay = abs(dy);
    if (lv_tick_elaps(G.t0) > 1200) return;              /* a slow drag is not a swipe */
    lv_dir_t d = LV_DIR_NONE;
    if (ax >= SWIPE_MIN && ax * 2 > ay * 3 && !G.on_slider && (!G.scrolled || ax > ay * 3)) d = dx < 0 ? LV_DIR_LEFT : LV_DIR_RIGHT;
    else if (ay >= SWIPE_MIN && ay * 2 > ax * 3 && !G.scrolled) d = dy < 0 ? LV_DIR_TOP : LV_DIR_BOTTOM;
    if (d != LV_DIR_NONE) ui_swipe(d);
}

static void gesture_timer(lv_timer_t *t)
{
    (void)t;
    if (!G.indev) {
        for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
            if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) G.indev = i;
        if (!G.indev) return;
    }
    bool pressed = G.indev->proc.state == LV_INDEV_STATE_PRESSED;
    lv_point_t p;
    lv_indev_get_point(G.indev, &p);
    if (pressed) {
        if (!G.down) {
            G.down = true;
            G.scrolled = false;
            G.p0 = p;
            G.t0 = lv_tick_get();
            lv_obj_t *hit = lv_indev_search_obj(lv_layer_top(), &p);
            if (!hit || hit == lv_layer_top()) hit = lv_indev_search_obj(lv_scr_act(), &p);
            G.on_screen = !hit || hit == lv_scr_act();
            G.on_slider = hit && lv_obj_check_type(hit, &lv_slider_class);
            if (knob_mode) {
                knob_mode = false;                        /* a finger: hide the knob's highlight */
                if (ui_list_of[cur]) { ui_list_of[cur]->sig_ok = false; ui_list_layout(ui_list_of[cur]); }
            }
        }
        G.last = p;
        if (lv_indev_get_scroll_obj(G.indev)) G.scrolled = true;
    } else if (G.down) {
        G.down = false;
        gesture_eval();
    }
}

bool ui_knob_mode(void) { return knob_mode; }

/* ------------------------------------------------------------------ */
/* HOME                                                                */
/* ------------------------------------------------------------------ */

static lv_obj_t *h_clock, *h_date, *h_ic[4];

static void home_open_assist(void)
{
    ui_goto(MX_PAGE_ASSIST, true);
}

static void home_halo_cb(lv_event_t *e)
{
    (void)e;
    if (!ui_is_tap() || M->locked) return;
    mx_activity();
    home_open_assist();
}

static void home_refresh(void)
{
    char b[32];
    time_str(b, sizeof b);
    ui_set_text(h_clock, b);
    date_str(b, sizeof b);
    ui_set_text(h_date, b);
    bool on[4] = {
        M->wifi_state == MX_WIFI_CONNECTED,
        M->bt_on,
        M->sd_state == MX_SD_ST_OK,
        M->chip_state == MX_CHIP_OK || M->chip_state == MX_CHIP_DONE,
    };
    for (int i = 0; i < 4; i++) ui_text_color(h_ic[i], on[i] ? C_BLUE : C_FAINT);
}

static void build_home(void)
{
    page_t *pg = &P[MX_PAGE_HOME];
    pg->scr = ui_scr();
    ui_chrome(pg, false);                    /* the big clock below replaces the small one */

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
    lv_obj_move_background(ring_arc);

    h_clock = ui_lbl(pg->scr, &lv_font_montserrat_48, C_TEXT, "--:--", LV_ALIGN_TOP_MID, 0, 40);
    h_date = ui_lbl(pg->scr, &lv_font_montserrat_16, C_DIM, "", LV_ALIGN_TOP_MID, 0, 98);

    /* the helmet in a lit glass disc; tap it to talk */
    lv_obj_t *halo = ui_circle(pg->scr, 156, lv_color_hex(0x0B1A3A));
    ui_grad(halo, lv_color_hex(0x15306A), lv_color_hex(0x040814));
    lv_obj_align(halo, LV_ALIGN_TOP_MID, 0, 132);
    lv_obj_set_style_border_width(halo, 2, 0);
    lv_obj_set_style_border_color(halo, C_BLUE, 0);
    lv_obj_set_style_border_opa(halo, LV_OPA_70, 0);
    ui_glow(halo, C_BLUE, 30);
    lv_obj_add_flag(halo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(halo, lv_color_hex(0x8FC0FF), LV_STATE_PRESSED);
    lv_obj_add_event_cb(halo, ui_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(halo, home_halo_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *im = lv_img_create(halo);
    lv_img_set_src(im, &mx_img_helmet);
    lv_img_set_zoom(im, 256 * 132 / 190);
    lv_img_set_antialias(im, true);
    lv_obj_center(im);
    lv_obj_clear_flag(im, LV_OBJ_FLAG_CLICKABLE);

    ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "MAXIMUS", LV_ALIGN_TOP_MID, 0, 300);

    /* Wi-Fi, Bluetooth, memory card, camera chip: lit when working */
    static const char *ic[4] = { LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_SD_CARD, LV_SYMBOL_EYE_OPEN };
    for (int i = 0; i < 4; i++)
        h_ic[i] = ui_lbl(pg->scr, &lv_font_montserrat_16, C_FAINT, ic[i], LV_ALIGN_TOP_MID, (i * 2 - 3) * 22, 332);

    pg->refresh = home_refresh;
    pg->confirm = home_open_assist;
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
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    m_coin = ui_circle(pg->scr, 34, coin_color("BTC"));
    lv_obj_align(m_coin, LV_ALIGN_TOP_MID, -72, 66);
    ui_lbl(m_coin, &lv_font_montserrat_16, lv_color_white(), "B", LV_ALIGN_CENTER, 0, 0);
    m_pair = ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "BTC/USDT", LV_ALIGN_TOP_MID, 14, 72);
    m_price = ui_lbl(pg->scr, &lv_font_montserrat_40, C_GREEN, "--", LV_ALIGN_TOP_MID, 0, 112);
    m_chg = ui_lbl(pg->scr, &lv_font_montserrat_16, C_GREEN, "", LV_ALIGN_TOP_MID, 0, 164);

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

    m_hl = ui_lbl(pg->scr, &lv_font_montserrat_12, C_DIM, "", LV_ALIGN_TOP_MID, 0, 322);
    m_src = ui_lbl(pg->scr, &lv_font_montserrat_12, lv_color_hex(0x4A5568), "", LV_ALIGN_TOP_MID, 0, 342);
    pg->refresh = market_refresh;
    pg->confirm = market_confirm;
    pg->tap = market_confirm;           /* tap = next coin */
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
        ui_glow(a_orb, col, listening ? 40 : 28);
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
    pg->scr = ui_scr();
    ui_chrome(pg, true);

    a_orb = ui_circle(pg->scr, 128, lv_color_hex(0x07102A));
    lv_obj_align(a_orb, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_border_width(a_orb, 3, 0);
    lv_obj_set_style_border_color(a_orb, C_BLUE, 0);
    ui_glow(a_orb, C_BLUE, 28);
    lv_obj_t *mic = lv_img_create(a_orb);
    lv_img_set_src(mic, &mx_img_mic);
    lv_obj_center(mic);

    a_spin = lv_spinner_create(pg->scr, 1200, 70);
    lv_obj_clear_flag(a_spin, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a_spin, 148, 148);
    lv_obj_align(a_spin, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_arc_width(a_spin, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a_spin, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a_spin, lv_color_hex(0x0B1530), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a_spin, C_BLUE, LV_PART_INDICATOR);
    lv_obj_add_flag(a_spin, LV_OBJ_FLAG_HIDDEN);

    a_title = ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "Assistant", LV_ALIGN_TOP_MID, 0, 214);
    a_sub = ui_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "", LV_ALIGN_TOP_MID, 0, 244);
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

    a_reply_box = ui_box(pg->scr, 280, 116, C_BG, 0);
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
    pg->tap = assist_confirm;
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
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    p_title = ui_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "Portfolio", LV_ALIGN_TOP_MID, 0, 62);
    p_total = ui_lbl(pg->scr, &lv_font_montserrat_40, C_TEXT, "--", LV_ALIGN_TOP_MID, 0, 88);
    p_chg = ui_lbl(pg->scr, &lv_font_montserrat_14, C_GREEN, "", LV_ALIGN_TOP_MID, 0, 138);
    for (int i = 0; i < MX_MAX_TICKERS; i++) {
        int y = 172 + i * 42;
        p_ic[i] = ui_circle(pg->scr, 28, coin_color("?"));
        lv_obj_set_pos(p_ic[i], 62, y);
        p_rows[i][3] = ui_lbl(p_ic[i], &lv_font_montserrat_14, lv_color_white(), "", LV_ALIGN_CENTER, 0, 0);
        p_rows[i][0] = ui_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_LEFT, 100, y + 5);
        p_rows[i][1] = ui_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_LEFT, 170, y + 5);
        p_rows[i][2] = ui_lbl(pg->scr, &lv_font_montserrat_14, C_GREEN, "", LV_ALIGN_TOP_RIGHT, -62, y + 7);
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
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "Journal", LV_ALIGN_TOP_MID, 0, 62);
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

    j_text = ui_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 166);
    lv_obj_set_width(j_text, 270);
    lv_obj_set_height(j_text, 110);
    lv_label_set_long_mode(j_text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(j_text, LV_TEXT_ALIGN_LEFT, 0);
    j_meta = ui_lbl(pg->scr, &lv_font_montserrat_12, C_GREEN, "", LV_ALIGN_TOP_MID, 0, 300);
    j_hint = ui_lbl(pg->scr, &lv_font_montserrat_12, C_DIM, "", LV_ALIGN_TOP_MID, 0, 324);
    pg->refresh = journal_refresh;
    pg->confirm = journal_confirm;
    pg->tap = journal_confirm;
}

/* ------------------------------------------------------------------ */
/* AI MODES                                                            */
/* ------------------------------------------------------------------ */

static ui_list_t L_modes;
static const uint32_t mode_cols[4] = { 0xF5B301, 0x7C4DFF, 0xE040A0, 0x1E90FF };
static const char *mode_ic[4] = { LV_SYMBOL_CHARGE, LV_SYMBOL_LIST, LV_SYMBOL_FILE, LV_SYMBOL_EDIT };
static const char *mode_sub[4] = { "Signals, risk, levels", "Charts and structure", "Headlines and mood", "Anything else" };

static void modes_pick(int i)
{
    if (i >= 4) { P[MX_PAGE_MODES].capture = false; ui_list_layout(&L_modes); return; }
    M->mode = i;
    if (A->set_mode) A->set_mode(i);
}

static void modes_refresh(void)
{
    for (int i = 0; i < 4; i++) {
        ui_list_set(&L_modes, i, NULL, NULL, i == M->mode ? LV_SYMBOL_OK : "");
        lv_obj_set_style_text_color(L_modes.val[i], C_GREEN, 0);
    }
}

static bool modes_rotate(int dir)
{
    if (!P[MX_PAGE_MODES].capture) return false;
    return ui_list_rotate(&L_modes, dir);
}

static void modes_confirm(void)
{
    page_t *pg = &P[MX_PAGE_MODES];
    if (!pg->capture) { pg->capture = true; L_modes.sel = M->mode; ui_list_layout(&L_modes); return; }
    modes_pick(L_modes.sel);
    modes_refresh();
}

static void build_modes(void)
{
    page_t *pg = &P[MX_PAGE_MODES];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "AI Mode", LV_ALIGN_TOP_MID, 0, 56);
    L_modes.parent = pg->scr;
    ui_list_of[MX_PAGE_MODES] = &L_modes;
    for (int i = 0; i < 4; i++) ui_list_add(&L_modes, lv_color_hex(mode_cols[i]), mode_ic[i], mx_modes[i].title, mode_sub[i], ui_row_click);
    ui_list_add(&L_modes, lv_color_hex(0x3A4254), LV_SYMBOL_LEFT, "Back", NULL, ui_row_click);
    ui_list_layout(&L_modes);
    pg->refresh = modes_refresh;
    pg->rotate = modes_rotate;
    pg->confirm = modes_confirm;
}

/* ------------------------------------------------------------------ */
/* NOTIFICATIONS                                                       */
/* ------------------------------------------------------------------ */

static ui_list_t L_notes;
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
        ui_list_set(&L_notes, i, M->notes[i].title, M->notes[i].body, "");
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
    ui_list_layout(&L_notes);
    if (n == 0) lv_obj_clear_flag(n_empty, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(n_empty, LV_OBJ_FLAG_HIDDEN);
}

static bool notes_rotate(int dir)
{
    if (!P[MX_PAGE_NOTES].capture) return false;
    return ui_list_rotate(&L_notes, dir);
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
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "Notifications", LV_ALIGN_TOP_MID, 0, 56);
    n_empty = ui_lbl(pg->scr, &lv_font_montserrat_14, C_DIM, "All clear", LV_ALIGN_CENTER, 0, 0);
    L_notes.parent = pg->scr;
    ui_list_of[MX_PAGE_NOTES] = &L_notes;
    for (int i = 0; i < MX_MAX_NOTES; i++) ui_list_add(&L_notes, C_BLUE, LV_SYMBOL_BELL, "", "", ui_row_click);
    ui_list_add(&L_notes, lv_color_hex(0x3A4254), LV_SYMBOL_LEFT, "Back", NULL, ui_row_click);
    pg->refresh = notes_refresh;
    pg->rotate = notes_rotate;
    pg->confirm = notes_confirm;
}

/* ------------------------------------------------------------------ */
/* SECURITY (PIN)                                                      */
/* ------------------------------------------------------------------ */

#define PIN_LEN 4
static lv_obj_t *s_title, *s_sub, *s_dots[PIN_LEN];
static ui_kb_t *s_kb;
static char pin_buf[PIN_LEN + 1], pin_first[PIN_LEN + 1];
static int pin_n;
static bool pin_confirming;
static const char *pin_msg;           /* one-off message under the dots */

static void pin_draw(void)
{
    for (int i = 0; i < PIN_LEN; i++) {
        bool on = i < pin_n;
        ui_bg_color(s_dots[i], on ? C_BLUE : C_BG);
        lv_obj_set_style_border_color(s_dots[i], on ? C_BLUE : lv_color_hex(0x5B6B8C), 0);
    }
    if (M->pin_set) ui_set_text(s_title, M->locked ? "Locked - enter PIN" : "Enter PIN");
    else ui_set_text(s_title, pin_confirming ? "Repeat the PIN" : "Create a 4-digit PIN");
    ui_set_text(s_sub, pin_msg ? pin_msg : "");
}

static void pin_reset(void)
{
    pin_n = 0;
    memset(pin_buf, 0, sizeof pin_buf);
    pin_draw();
}

static void pin_enter(void)
{
    pin_msg = NULL;
    pin_confirming = false;
    ui_kb_reset(s_kb);
    pin_reset();
}

static void pin_done(void)
{
    pin_buf[PIN_LEN] = 0;
    if (M->pin_set) {
        if (A->pin_verify && A->pin_verify(pin_buf)) {
            M->locked = false;
            pin_msg = NULL;
            pin_reset();
            ui_goto(MX_PAGE_HOME, true);
            return;
        }
        pin_msg = "Wrong PIN, try again";
        pin_reset();
        return;
    }
    if (!pin_confirming) {
        memcpy(pin_first, pin_buf, sizeof pin_first);
        pin_confirming = true;
        pin_msg = NULL;
        pin_reset();
        return;
    }
    pin_confirming = false;
    if (!strcmp(pin_first, pin_buf) && A->pin_store && A->pin_store(pin_buf)) {
        M->pin_set = true;
        M->locked = false;
        pin_msg = NULL;
        pin_reset();
        ui_goto(MX_PAGE_HOME, true);
        return;
    }
    pin_msg = "PINs differ, start again";
    pin_reset();
}

static void pin_key(int kind, const char *txt)
{
    if (kind == UI_KEY_CHAR && txt && txt[0] >= '0' && txt[0] <= '9') {
        if (pin_n < PIN_LEN) pin_buf[pin_n++] = txt[0];
        pin_msg = NULL;
        if (pin_n == PIN_LEN) { pin_done(); return; }
    } else if (kind == UI_KEY_BKSP) {
        if (pin_n > 0) pin_buf[--pin_n] = 0;
        else if (!M->locked) { ui_back(); return; }    /* empty field: backspace leaves */
    } else if (kind == UI_KEY_OK) {
        if (pin_n == PIN_LEN) { pin_done(); return; }
        if (pin_n == 0 && !M->locked) { ui_back(); return; }
    }
    pin_draw();
}

static void security_refresh(void) { pin_draw(); }
static bool security_rotate(int dir) { return ui_kb_rotate(s_kb, dir); }
static void security_confirm(void) { ui_kb_confirm(s_kb); }

static void build_security(void)
{
    page_t *pg = &P[MX_PAGE_SECURITY];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    s_title = ui_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 46);
    for (int i = 0; i < PIN_LEN; i++) {
        s_dots[i] = ui_circle(pg->scr, 14, C_BG);
        lv_obj_set_style_border_width(s_dots[i], 2, 0);
        lv_obj_align(s_dots[i], LV_ALIGN_TOP_MID, (int)((i - (PIN_LEN - 1) / 2.0f) * 28), 72);
    }
    s_kb = ui_kb_create(pg->scr, UI_KB_PIN, 94, pin_key);
    s_sub = ui_lbl(pg->scr, &lv_font_montserrat_14, C_RED, "", LV_ALIGN_TOP_MID, 0, 340);
    pg->refresh = security_refresh;
    pg->rotate = security_rotate;
    pg->confirm = security_confirm;
    pg->capture = true;
    pg->no_swipe = true;
    pg->enter = pin_enter;
}

/* ------------------------------------------------------------------ */
/* overlays                                                            */
/* ------------------------------------------------------------------ */

static void blank_cb(lv_event_t *e)
{
    (void)e;
    mx_activity();            /* the app notices and lights the screen again */
}

static void alert_close(void)
{
    lv_obj_add_flag(alert_box, LV_OBJ_FLAG_HIDDEN);
    if (alert_timer) { lv_timer_del(alert_timer); alert_timer = NULL; }
}

static void alert_tap_cb(lv_event_t *e)
{
    (void)e;
    mx_activity();
    alert_close();
}

static void build_overlays(void)
{
    lv_obj_t *top = lv_layer_top();

    hold_dim = ui_box(top, W, H, C_BG, 0);
    lv_obj_set_style_bg_opa(hold_dim, LV_OPA_80, 0);
    lv_obj_add_flag(hold_dim, LV_OBJ_FLAG_HIDDEN);
    hold_box = ui_circle(top, 340, lv_color_hex(0x05070D));
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
    hold_lbl = ui_lbl(hold_box, &lv_font_montserrat_24, C_TEXT, "", LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(hold_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(hold_box, LV_OBJ_FLAG_HIDDEN);

    alert_box = ui_box(top, W, H, lv_color_hex(0x1A0507), 0);
    lv_obj_add_flag(alert_box, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(alert_box, alert_tap_cb, LV_EVENT_CLICKED, NULL);
    alert_ic = ui_lbl(alert_box, &lv_font_montserrat_40, C_RED, LV_SYMBOL_WARNING, LV_ALIGN_TOP_MID, 0, 62);
    alert_t = ui_lbl(alert_box, &lv_font_montserrat_20, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 126);
    alert_s = ui_lbl(alert_box, &lv_font_montserrat_20, C_RED, "", LV_ALIGN_TOP_MID, 0, 162);
    lv_obj_set_width(alert_s, 280);
    lv_obj_set_style_text_align(alert_s, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(alert_s, LV_LABEL_LONG_DOT);
    alert_p = ui_lbl(alert_box, &lv_font_montserrat_28, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 214);
    ui_lbl(alert_box, &lv_font_montserrat_12, C_DIM, "Tap to close", LV_ALIGN_TOP_MID, 0, 300);

    /* screen timeout: a black sheet that swallows the wake-up touch */
    blank_box = ui_box(top, W, H, C_BG, 0);
    lv_obj_add_flag(blank_box, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(blank_box, blank_cb, LV_EVENT_PRESSED, NULL);
}

static bool blanked(void)
{
    return blank_box && !lv_obj_has_flag(blank_box, LV_OBJ_FLAG_HIDDEN);
}

static bool alert_up(void)
{
    return alert_box && !lv_obj_has_flag(alert_box, LV_OBJ_FLAG_HIDDEN);
}

bool ui_overlay_open(void)
{
    return ui_shell_open() || alert_up();
}

void ui_screen_blank(bool blank)
{
    if (!blank_box) return;
    if (blank) lv_obj_clear_flag(blank_box, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(blank_box, LV_OBJ_FLAG_HIDDEN);
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

/* The ring fills over 5 s: a click is under 2 s, 2-5 s goes Home, 5 s powers off. */
void ui_set_hold(int ms)
{
    if (ms < 1000) {
        lv_obj_add_flag(hold_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hold_dim, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(hold_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hold_dim, LV_OBJ_FLAG_HIDDEN);
    int pct = ms * 100 / 5000;
    if (pct > 100) pct = 100;
    lv_arc_set_value(hold_arc, pct);
    lv_color_t col;
    const char *msg;
    if (ms >= 5000) { col = C_RED; msg = "Powering off..."; }
    else if (ms >= 2000) { col = C_ORANGE; msg = "Release: Home\nKeep holding:\npower off"; }
    else { col = C_BLUE; msg = "Hold 2 s: Home\nHold 5 s:\npower off"; }
    lv_obj_set_style_arc_color(hold_arc, col, LV_PART_INDICATOR);
    lv_label_set_text(hold_lbl, msg);
}

/* ------------------------------------------------------------------ */
/* core                                                                */
/* ------------------------------------------------------------------ */

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
    page_t *pg = &P[cur];
    if (pg->time) {
        char t[16];
        time_str(t, sizeof t);
        if (strcmp(lv_label_get_text(pg->time), t)) {
            lv_label_set_text(pg->time, t);
            ui_chrome_place(pg);
        }
    }
    ui_batt_apply(&pg->batt);
}

void ui_refresh(void)
{
    chrome_refresh();
    if (P[cur].refresh) P[cur].refresh();
    if (ui_list_of[cur]) ui_list_layout(ui_list_of[cur]);
    ui_shell_refresh();
}

void ui_register_hidden(mx_page_t page, mx_page_t parent)
{
    P[page].hidden = true;
    P[page].capture = true;
    P[page].parent = parent;
}

/* dir > 0 slides the new page in from the right, < 0 from the left, 0 = cut */
static void nav_to(mx_page_t page, int dir)
{
    if (page < 0 || page >= MX_PAGE_COUNT || !P[page].scr) return;
    ui_shell_close();
    if (page == cur) {
        ui_refresh();
        return;
    }
    if (P[cur].leave) P[cur].leave();
    cur = page;
    P[page].capture = P[page].hidden;
    if (ui_list_of[page]) ui_list_of[page]->sig_ok = false;
    dots_update();
    if (P[page].enter) P[page].enter();
    ui_refresh();
    lv_scr_load_anim_t a = dir > 0 ? LV_SCR_LOAD_ANIM_MOVE_LEFT : dir < 0 ? LV_SCR_LOAD_ANIM_MOVE_RIGHT : LV_SCR_LOAD_ANIM_NONE;
    lv_scr_load_anim(P[page].scr, a, dir ? 200 : 0, 0, false);
}

void ui_goto(mx_page_t page, bool animate)
{
    if (page < 0 || page >= MX_PAGE_COUNT) return;
    /* a sub-page opens forwards; going back to its parent slides backwards */
    int dir = !animate ? 0 : (P[cur].hidden && page == P[cur].parent) ? -1 : page >= cur ? 1 : -1;
    nav_to(page, dir);
}

void ui_back(void)
{
    if (P[cur].hidden) nav_to(P[cur].parent, -1);
}

static void goto_dir(int dir)
{
    int n = ((int)cur + dir + MX_PAGE_CAROUSEL_N) % MX_PAGE_CAROUSEL_N;
    nav_to((mx_page_t)n, dir);
}

/* Finger moved: LEFT = towards the left edge (next page), RIGHT = previous
   page or Back, BOTTOM = pull the quick panel down, TOP = push the app grid up. */
void ui_swipe(lv_dir_t d)
{
    if (blanked() || M->locked) return;
    if (alert_up()) { alert_close(); return; }
    mx_activity();
    if (ui_shell_open()) {
        ui_shell_swipe(d);
        ui_refresh();
        return;
    }
    page_t *pg = &P[cur];
    if (pg->no_swipe && !G.on_screen) return;          /* the finger started on a key */
    if (pg->gesture && pg->gesture(d)) { ui_refresh(); return; }
    switch (d) {
    case LV_DIR_LEFT:
        if (!pg->hidden) { pg->capture = false; goto_dir(1); }
        break;
    case LV_DIR_RIGHT:
        if (pg->hidden) ui_back();
        else { pg->capture = false; goto_dir(-1); }
        break;
    case LV_DIR_BOTTOM:
        ui_panel_open();
        break;
    case LV_DIR_TOP:
        ui_launcher_open();
        break;
    default:
        break;
    }
    ui_refresh();
}

void ui_input(mx_input_t in)
{
    mx_activity();
    if (!knob_mode) {
        knob_mode = true;                                    /* show where the knob is */
        if (ui_list_of[cur]) ui_list_of[cur]->sig_ok = false;
    }
    /* an alert swallows the first input and just closes */
    if (alert_up()) { alert_close(); return; }
    if (M->locked && cur != MX_PAGE_SECURITY) nav_to(MX_PAGE_SECURITY, 0);
    if (in == MX_IN_HOME) {
        ui_shell_close();
        if (!M->locked) ui_goto(MX_PAGE_HOME, true);
        ui_refresh();
        return;
    }
    if (ui_shell_input(in)) { ui_refresh(); return; }
    page_t *pg = &P[cur];
    switch (in) {
    case MX_IN_LEFT:
    case MX_IN_RIGHT: {
        int dir = in == MX_IN_RIGHT ? 1 : -1;
        if (pg->rotate && pg->rotate(dir)) break;
        if (M->locked || pg->hidden) break;
        goto_dir(dir);
        break;
    }
    case MX_IN_CONFIRM:
        if (pg->confirm) pg->confirm();
        break;
    default:
        break;
    }
    ui_refresh();
}

void ui_init(mx_model_t *model, const mx_actions_t *actions)
{
    M = model;
    A = actions;
    build_home();
    build_market();
    build_assist();
    build_portfolio();
    build_journal();
    build_modes();
    build_notes();
    build_security();
    ui_build_camera_pages();
    ui_build_settings_pages();
    for (int i = 0; i < MX_PAGE_CAROUSEL_N; i++) dots_build(P[i].scr, page_dots[i]);
    ui_shell_build();
    build_overlays();
    ui_register_hidden(MX_PAGE_SECURITY, MX_PAGE_SETTINGS);
    lv_timer_create(gesture_timer, 10, NULL);
    cur = MX_PAGE_HOME;
    dots_update();
    ui_refresh();
    lv_scr_load(P[MX_PAGE_HOME].scr);
    if (M->locked) nav_to(MX_PAGE_SECURITY, 0);
}
