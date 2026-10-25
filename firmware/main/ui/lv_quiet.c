/*
 * Setters that skip what would not change anything.
 *
 * LVGL redraws an object on every text, style or flag write, even when the
 * value is the one already set, and the pages rewrite their whole state five
 * times a second. On the SPD2010 panel every transfer leaves faint vertical
 * streaks that take seconds to fade, so an idle page has to send nothing.
 * The linker routes these calls here (-Wl,--wrap=..., see CMakeLists.txt and
 * sim/Makefile); a real change goes on to LVGL as before.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

void __real_lv_obj_set_local_style_prop(lv_obj_t *obj, lv_style_prop_t prop, lv_style_value_t value,
                                        lv_style_selector_t selector);
void __real_lv_label_set_text(lv_obj_t *obj, const char *text);
void __real_lv_obj_add_flag(lv_obj_t *obj, lv_obj_flag_t f);
void __real_lv_obj_clear_flag(lv_obj_t *obj, lv_obj_flag_t f);
void __real_lv_chart_set_value_by_id(lv_obj_t *obj, lv_chart_series_t *ser, uint16_t id, lv_coord_t value);

static bool is_color(lv_style_prop_t p)
{
    switch (p) {
    case LV_STYLE_BG_COLOR: case LV_STYLE_BG_GRAD_COLOR: case LV_STYLE_BORDER_COLOR:
    case LV_STYLE_OUTLINE_COLOR: case LV_STYLE_SHADOW_COLOR: case LV_STYLE_IMG_RECOLOR:
    case LV_STYLE_LINE_COLOR: case LV_STYLE_ARC_COLOR: case LV_STYLE_TEXT_COLOR:
        return true;
    default:
        return false;
    }
}

void __wrap_lv_obj_set_local_style_prop(lv_obj_t *obj, lv_style_prop_t prop, lv_style_value_t value,
                                        lv_style_selector_t selector)
{
    lv_style_value_t cur;
    if (lv_obj_get_local_style_prop(obj, prop, &cur, selector) == LV_RES_OK) {
        bool same = is_color(prop) ? cur.color.full == value.color.full
                                   : cur.num == value.num && cur.ptr == value.ptr;
        if (same) return;
    }
    __real_lv_obj_set_local_style_prop(obj, prop, value, selector);
}

/* A label in dot mode keeps "Long tex..." in its buffer, so its text never
   equals the one it was given. Those remember what they were last given:
   label, buffer and a hash of the text, so a new label at a freed address
   does not pass for the old one. */
typedef struct {
    const lv_obj_t *obj;
    const char *buf;
    uint32_t hash;
} dot_seen_t;
static dot_seen_t s_dots[64];

static uint32_t hash_str(const char *t)
{
    uint32_t h = 2166136261u;
    while (*t) h = (h ^ (uint8_t)*t++) * 16777619u;
    return h;
}

void __wrap_lv_label_set_text(lv_obj_t *obj, const char *text)
{
    /* NULL means "re-lay the current text out", which callers ask for on purpose */
    if (!text) {
        __real_lv_label_set_text(obj, text);
        return;
    }
    lv_label_t *lb = (lv_label_t *)obj;
    const char *cur = lv_label_get_text(obj);
    if (cur && cur != text && strcmp(cur, text) == 0) return;
    uint32_t h = hash_str(text);
    dot_seen_t *d = &s_dots[((uintptr_t)obj >> 3) % (sizeof s_dots / sizeof *s_dots)];
    if (lb->long_mode == LV_LABEL_LONG_DOT && lb->dot_end != 0xFFFFFFFF /* LV_LABEL_DOT_END_INV, private to lv_label.c */ && d->obj == obj &&
        d->buf == lb->text && d->hash == h)
        return;
    __real_lv_label_set_text(obj, text);
    if (lb->long_mode == LV_LABEL_LONG_DOT) {
        d->obj = obj;
        d->buf = lb->text;
        d->hash = h;
    }
}

void __wrap_lv_label_set_text_fmt(lv_obj_t *obj, const char *fmt, ...)
{
    char small[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof small) {
        __wrap_lv_label_set_text(obj, small);
        return;
    }
    char *big = malloc((size_t)n + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    __wrap_lv_label_set_text(obj, big);
    free(big);
}

void __wrap_lv_obj_add_flag(lv_obj_t *obj, lv_obj_flag_t f)
{
    if (lv_obj_has_flag(obj, f)) return;
    __real_lv_obj_add_flag(obj, f);
}

void __wrap_lv_obj_clear_flag(lv_obj_t *obj, lv_obj_flag_t f)
{
    if (!lv_obj_has_flag_any(obj, f)) return;
    __real_lv_obj_clear_flag(obj, f);
}

void __wrap_lv_chart_set_value_by_id(lv_obj_t *obj, lv_chart_series_t *ser, uint16_t id, lv_coord_t value)
{
    if (id < lv_chart_get_point_count(obj) && ser->y_points[id] == value) return;
    __real_lv_chart_set_value_by_id(obj, ser, id, value);
}
