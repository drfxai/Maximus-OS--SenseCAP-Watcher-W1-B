/*
 * On-screen keyboards that type like a phone's.
 *
 * The keys are plain boxes inside one touch area that does its own hit
 * testing: the finger always lands on the nearest key of the nearest row
 * (no dead gaps between keys), sliding corrects the choice, a bubble shows
 * the letter under the finger and the key is typed on release. Backspace
 * repeats while held. The knob walks the keys and its click types one.
 *
 * Text layout: letters / shift (tap = one capital, double tap = caps lock) /
 * numbers / symbols. PIN layout: 3x4 digits with backspace and OK.
 */
#include "ui_priv.h"

#define KB_POOL 32
#define KB_ROWS 4

typedef struct {
    int kind;
    const char *txt;       /* character typed, or label of a special key */
    float units;           /* width in letter widths */
} key_def_t;

struct ui_kb {
    lv_obj_t *area;
    lv_obj_t *box[KB_POOL], *lbl[KB_POOL];
    lv_obj_t *pop, *pop_lbl;
    lv_timer_t *rep;
    ui_kb_cb_t cb;
    int type, layer;        /* 0 letters, 1 numbers, 2 symbols */
    bool shift, caps;
    uint32_t shift_t;
    int n;
    int kx[KB_POOL], ky[KB_POOL], kw[KB_POOL], krow[KB_POOL];
    key_def_t def[KB_POOL];
    int row_first[KB_ROWS + 1], rows;
    int key_h, row_step;
    int down;               /* key under the finger, -1 = none */
    int sel;                /* knob highlight */
    bool rep_fired;
    uint32_t up_t;          /* when the last key was released */
    bool bounce;            /* this press is being ignored */
};

#define K(c) { UI_KEY_CHAR, c, 1 }
#define END { -1, NULL, 0 }

static const key_def_t L_LETTERS[KB_ROWS][12] = {
    { K("q"), K("w"), K("e"), K("r"), K("t"), K("y"), K("u"), K("i"), K("o"), K("p"), END },
    { K("a"), K("s"), K("d"), K("f"), K("g"), K("h"), K("j"), K("k"), K("l"), END },
    { { UI_KEY_SHIFT, LV_SYMBOL_UP, 1.5f }, K("z"), K("x"), K("c"), K("v"), K("b"), K("n"), K("m"),
      { UI_KEY_BKSP, LV_SYMBOL_BACKSPACE, 1.5f }, END },
    { { UI_KEY_LAYER, "?123", 2.2f }, { UI_KEY_SPACE, "space", 4.4f }, { UI_KEY_OK, "OK", 2.2f }, END },
};
static const key_def_t L_NUMBERS[KB_ROWS][12] = {
    { K("1"), K("2"), K("3"), K("4"), K("5"), K("6"), K("7"), K("8"), K("9"), K("0"), END },
    { K("-"), K("/"), K(":"), K(";"), K("("), K(")"), K("$"), K("&"), K("@"), K("\""), END },
    { { UI_KEY_LAYER, "#+=", 1.5f }, K("."), K(","), K("?"), K("!"), K("'"), K("_"),
      { UI_KEY_BKSP, LV_SYMBOL_BACKSPACE, 1.5f }, END },
    { { UI_KEY_LAYER, "ABC", 2.2f }, { UI_KEY_SPACE, "space", 4.4f }, { UI_KEY_OK, "OK", 2.2f }, END },
};
static const key_def_t L_SYMBOLS[KB_ROWS][12] = {
    { K("["), K("]"), K("{"), K("}"), K("#"), K("%"), K("^"), K("*"), K("+"), K("="), END },
    { K("_"), K("\\"), K("|"), K("~"), K("<"), K(">"), K("`"), K("."), K(","), K("?"), END },
    { { UI_KEY_LAYER, "123", 1.5f }, K("!"), K("'"), K("\""), K("-"), K("/"), K("@"),
      { UI_KEY_BKSP, LV_SYMBOL_BACKSPACE, 1.5f }, END },
    { { UI_KEY_LAYER, "ABC", 2.2f }, { UI_KEY_SPACE, "space", 4.4f }, { UI_KEY_OK, "OK", 2.2f }, END },
};
static const key_def_t L_PIN[KB_ROWS][12] = {
    { K("1"), K("2"), K("3"), END },
    { K("4"), K("5"), K("6"), END },
    { K("7"), K("8"), K("9"), END },
    { { UI_KEY_BKSP, LV_SYMBOL_BACKSPACE, 1 }, K("0"), { UI_KEY_OK, LV_SYMBOL_OK, 1 }, END },
};

#define KEY_BG    lv_color_hex(0x2A3346)
#define KEY_FN    lv_color_hex(0x1A2131)
#define KEY_DOWN  lv_color_hex(0x55627E)

static const char *upper(const char *t)
{
    static char b[2];
    b[0] = (t[0] >= 'a' && t[0] <= 'z') ? (char)(t[0] - 32) : t[0];
    b[1] = 0;
    return b;
}

static const char *key_text(ui_kb_t *kb, int i)
{
    const key_def_t *d = &kb->def[i];
    if (d->kind == UI_KEY_CHAR && kb->type == UI_KB_TEXT && kb->layer == 0 && (kb->shift || kb->caps)) return upper(d->txt);
    return d->txt;
}

static lv_color_t key_bg(ui_kb_t *kb, int i)
{
    const key_def_t *d = &kb->def[i];
    if (i == kb->down) return KEY_DOWN;
    if (d->kind == UI_KEY_OK) return C_BLUE;
    if (d->kind == UI_KEY_SHIFT && (kb->shift || kb->caps)) return kb->caps ? C_BLUE : lv_color_hex(0x3B4A6A);
    return (d->kind == UI_KEY_CHAR || d->kind == UI_KEY_SPACE) ? KEY_BG : KEY_FN;
}

static void key_paint(ui_kb_t *kb, int i)
{
    ui_bg_color(kb->box[i], key_bg(kb, i));
    bool hl = ui_knob_mode() && i == kb->sel;
    lv_obj_set_style_border_width(kb->box[i], hl ? 2 : 0, 0);
}

static void paint_all(ui_kb_t *kb)
{
    for (int i = 0; i < kb->n; i++) {
        ui_set_text(kb->lbl[i], key_text(kb, i));
        key_paint(kb, i);
    }
}

/* Lay the current layer out: rows centred, keys sized by their units. */
static void layout(ui_kb_t *kb)
{
    const key_def_t (*rows)[12] = kb->type == UI_KB_PIN ? L_PIN : kb->layer == 1 ? L_NUMBERS : kb->layer == 2 ? L_SYMBOLS : L_LETTERS;
    int unit = kb->type == UI_KB_PIN ? 80 : 32;
    int gap = kb->type == UI_KB_PIN ? 8 : 4;
    int n = 0;
    kb->rows = KB_ROWS;
    for (int r = 0; r < KB_ROWS; r++) {
        kb->row_first[r] = n;
        int cnt = 0;
        float units = 0;
        while (rows[r][cnt].kind >= 0) units += rows[r][cnt++].units;
        int rw = (int)(units * unit + 0.5f) + (cnt - 1) * gap;
        if (kb->type == UI_KB_TEXT && r == KB_ROWS - 1) rw += 2 * gap;   /* bigger gaps around space */
        int x = (W - rw) / 2;
        for (int k = 0; k < cnt && n < KB_POOL; k++, n++) {
            const key_def_t *d = &rows[r][k];
            kb->def[n] = *d;
            kb->kx[n] = x;
            kb->ky[n] = r * kb->row_step;
            kb->kw[n] = (int)(d->units * unit + 0.5f);
            kb->krow[n] = r;
            x += kb->kw[n] + gap + ((kb->type == UI_KB_TEXT && r == KB_ROWS - 1) ? gap : 0);
        }
    }
    kb->row_first[KB_ROWS] = n;
    kb->n = n;
    for (int i = 0; i < KB_POOL; i++) {
        if (i >= n) { ui_show(kb->box[i], false); continue; }
        ui_show(kb->box[i], true);
        lv_obj_set_pos(kb->box[i], kb->kx[i], kb->ky[i]);
        lv_obj_set_size(kb->box[i], kb->kw[i], kb->key_h);
        const key_def_t *d = &kb->def[i];
        bool big = d->kind == UI_KEY_CHAR;
        lv_obj_set_style_text_font(kb->lbl[i], kb->type == UI_KB_PIN ? &lv_font_montserrat_28 : big ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
    }
    if (kb->sel >= n) kb->sel = n - 1;
    paint_all(kb);
}

/* nearest row, then nearest key in it: no dead zones */
static int hit(ui_kb_t *kb, lv_point_t p)
{
    lv_area_t a;
    lv_obj_get_coords(kb->area, &a);
    int x = p.x - a.x1, y = p.y - a.y1;
    int r = (y + (kb->row_step - kb->key_h) / 2) / kb->row_step;
    if (r < 0) r = 0;
    if (r >= kb->rows) r = kb->rows - 1;
    int best = -1, bd = 1 << 30;
    for (int i = kb->row_first[r]; i < kb->row_first[r + 1]; i++) {
        int d = x < kb->kx[i] ? kb->kx[i] - x : x > kb->kx[i] + kb->kw[i] ? x - kb->kx[i] - kb->kw[i] : 0;
        if (d < bd) { bd = d; best = i; }
    }
    return bd > 40 ? -1 : best;                 /* far outside the row's ends: nothing */
}

static void pop_show(ui_kb_t *kb, int i)
{
    if (i < 0 || kb->def[i].kind != UI_KEY_CHAR || kb->type == UI_KB_PIN) { ui_show(kb->pop, false); return; }
    ui_set_text(kb->pop_lbl, key_text(kb, i));
    lv_area_t a;
    lv_obj_get_coords(kb->area, &a);
    lv_obj_t *par = lv_obj_get_parent(kb->area);
    lv_area_t pa;
    lv_obj_get_coords(par, &pa);
    int cx = a.x1 - pa.x1 + kb->kx[i] + kb->kw[i] / 2;
    int top = a.y1 - pa.y1 + kb->ky[i] - 62;
    lv_obj_set_pos(kb->pop, cx - 26, top);
    ui_show(kb->pop, true);
    lv_obj_move_foreground(kb->pop);
}

static void emit(ui_kb_t *kb, int i)
{
    if (i < 0 || i >= kb->n) return;
    const key_def_t *d = &kb->def[i];
    switch (d->kind) {
    case UI_KEY_CHAR: {
        const char *t = key_text(kb, i);
        bool was_shift = kb->shift;
        kb->shift = false;
        if (kb->cb) kb->cb(UI_KEY_CHAR, t);
        if (was_shift) paint_all(kb);
        break;
    }
    case UI_KEY_SPACE:
        if (kb->cb) kb->cb(UI_KEY_CHAR, " ");
        break;
    case UI_KEY_BKSP:
        if (kb->cb) kb->cb(UI_KEY_BKSP, NULL);
        break;
    case UI_KEY_OK:
        if (kb->cb) kb->cb(UI_KEY_OK, NULL);
        break;
    case UI_KEY_SHIFT: {
        uint32_t now = lv_tick_get();
        if (kb->caps) { kb->caps = false; kb->shift = false; }
        else if (kb->shift && now - kb->shift_t < 450) { kb->caps = true; kb->shift = false; }
        else kb->shift = !kb->shift;
        kb->shift_t = now;
        paint_all(kb);
        break;
    }
    case UI_KEY_LAYER:
        if (!strcmp(d->txt, "ABC")) kb->layer = 0;
        else if (!strcmp(d->txt, "#+=")) kb->layer = 2;
        else kb->layer = 1;
        kb->shift = false;
        kb->sel = 0;
        layout(kb);
        break;
    }
}

static void rep_cb(lv_timer_t *t)
{
    ui_kb_t *kb = t->user_data;
    if (kb->down < 0 || kb->def[kb->down].kind != UI_KEY_BKSP) { lv_timer_pause(t); return; }
    mx_activity();
    kb->rep_fired = true;
    emit(kb, kb->down);
    lv_timer_set_period(t, 80);
}

static void set_down(ui_kb_t *kb, int i)
{
    if (i == kb->down) return;
    int old = kb->down;
    kb->down = i;
    if (old >= 0) key_paint(kb, old);
    if (i >= 0) key_paint(kb, i);
    pop_show(kb, i);
}

static void area_cb(lv_event_t *e)
{
    ui_kb_t *kb = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_get_act();
    lv_point_t p = { 0, 0 };
    if (in) lv_indev_get_point(in, &p);
    if (code == LV_EVENT_PRESSED) {
        mx_activity();
        /* a press this soon after a release is the same finger bouncing */
        kb->bounce = lv_tick_elaps(kb->up_t) < 90;
        if (kb->bounce) return;
        int i = hit(kb, p);
        set_down(kb, i);
        if (i >= 0 && kb->def[i].kind == UI_KEY_BKSP) {
            /* delete at once, then repeat while held, like a phone */
            emit(kb, i);
            kb->rep_fired = false;
            lv_timer_set_period(kb->rep, 450);
            lv_timer_reset(kb->rep);
            lv_timer_resume(kb->rep);
        }
    } else if (kb->bounce) {
        if (code != LV_EVENT_PRESSING) kb->bounce = false;
    } else if (code == LV_EVENT_PRESSING) {
        int i = hit(kb, p);
        if (kb->down >= 0 && kb->def[kb->down].kind == UI_KEY_BKSP) return;   /* stay on backspace */
        set_down(kb, i);
    } else if (code == LV_EVENT_RELEASED) {
        int i = kb->down;
        lv_timer_pause(kb->rep);
        set_down(kb, -1);
        if (i >= 0) kb->up_t = lv_tick_get();
        if (i >= 0 && kb->def[i].kind != UI_KEY_BKSP) {
            kb->sel = i;
            emit(kb, i);
        }
        ui_refresh();
    } else if (code == LV_EVENT_PRESS_LOST) {
        lv_timer_pause(kb->rep);
        set_down(kb, -1);
    }
}

ui_kb_t *ui_kb_create(lv_obj_t *parent, int type, int y0, ui_kb_cb_t cb)
{
    ui_kb_t *kb = lv_mem_alloc(sizeof *kb);
    memset(kb, 0, sizeof *kb);
    kb->type = type;
    kb->cb = cb;
    kb->down = -1;
    kb->key_h = type == UI_KB_PIN ? 54 : 50;
    kb->row_step = kb->key_h + (type == UI_KB_PIN ? 6 : 6);

    kb->area = lv_obj_create(parent);
    lv_obj_remove_style_all(kb->area);
    lv_obj_set_size(kb->area, W, KB_ROWS * kb->row_step);
    lv_obj_set_pos(kb->area, 0, y0);
    lv_obj_add_flag(kb->area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(kb->area, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_ext_click_area(kb->area, 4);
    lv_obj_add_event_cb(kb->area, area_cb, LV_EVENT_PRESSED, kb);
    lv_obj_add_event_cb(kb->area, area_cb, LV_EVENT_PRESSING, kb);
    lv_obj_add_event_cb(kb->area, area_cb, LV_EVENT_RELEASED, kb);
    lv_obj_add_event_cb(kb->area, area_cb, LV_EVENT_PRESS_LOST, kb);

    for (int i = 0; i < KB_POOL; i++) {
        lv_obj_t *b = ui_box(kb->area, 32, kb->key_h, KEY_BG, type == UI_KB_PIN ? 27 : 8);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(b, C_BLUE, 0);
        kb->box[i] = b;
        kb->lbl[i] = ui_lbl(b, &lv_font_montserrat_20, C_TEXT, "", LV_ALIGN_CENTER, 0, 0);
    }

    /* the letter bubble above the finger */
    kb->pop = ui_box(parent, 52, 60, lv_color_hex(0x3A4766), 12);
    lv_obj_set_style_shadow_width(kb->pop, 16, 0);
    lv_obj_set_style_shadow_color(kb->pop, C_BG, 0);
    lv_obj_set_style_shadow_opa(kb->pop, LV_OPA_70, 0);
    lv_obj_clear_flag(kb->pop, LV_OBJ_FLAG_CLICKABLE);
    kb->pop_lbl = ui_lbl(kb->pop, &lv_font_montserrat_28, C_TEXT, "", LV_ALIGN_CENTER, 0, 0);
    ui_show(kb->pop, false);

    kb->rep = lv_timer_create(rep_cb, 450, kb);
    lv_timer_pause(kb->rep);
    layout(kb);
    return kb;
}

void ui_kb_reset(ui_kb_t *kb)
{
    if (!kb) return;
    kb->layer = 0;
    kb->shift = false;
    kb->caps = false;
    kb->sel = 0;
    set_down(kb, -1);
    layout(kb);
}

bool ui_kb_rotate(ui_kb_t *kb, int dir)
{
    if (!kb || !kb->n) return false;
    int old = kb->sel;
    kb->sel = (kb->sel + dir + kb->n) % kb->n;
    key_paint(kb, old);
    key_paint(kb, kb->sel);
    return true;
}

void ui_kb_confirm(ui_kb_t *kb)
{
    if (!kb) return;
    emit(kb, kb->sel);
    paint_all(kb);
}
