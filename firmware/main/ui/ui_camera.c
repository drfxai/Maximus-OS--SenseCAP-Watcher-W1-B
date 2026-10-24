/*
 * Camera, Gallery and the picture / video viewer.
 *
 * The app owns the pixel buffers (a decoded preview frame, a decoded photo);
 * the model hands this file a pointer, width and height and these pages show
 * it through an lv_img. Nothing here touches the camera or the card.
 */
#include "ui_priv.h"

#define M ui_M
#define A ui_A
#define P ui_P
#define cur ui_cur

#define SHEET_OPA LV_OPA_60

static lv_obj_t *dim_label(lv_obj_t *scr, const lv_font_t *f, lv_color_t col, lv_align_t al, int x, int y, int w)
{
    lv_obj_t *l = ui_lbl(scr, f, col, "", al, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(l, C_BG, 0);
    lv_obj_set_style_bg_opa(l, SHEET_OPA, 0);
    lv_obj_set_style_radius(l, 10, 0);
    lv_obj_set_style_pad_all(l, 4, 0);
    return l;
}

/* The app's pixel buffer as an LVGL image. */
static void dsc_from(lv_img_dsc_t *d, const void *pix, int w, int h)
{
    memset(d, 0, sizeof *d);
    d->header.cf = LV_IMG_CF_TRUE_COLOR;
    d->header.w = w;
    d->header.h = h;
    d->data_size = (uint32_t)w * h * (LV_COLOR_DEPTH / 8);
    d->data = pix;
}

/* ------------------------------------------------------------------ */
/* CAMERA                                                              */
/* ------------------------------------------------------------------ */

static lv_obj_t *c_img, *c_msg, *c_toast, *c_rec_dot, *c_rec_lbl, *c_flash;
static lv_obj_t *c_mode_photo, *c_mode_video, *c_lbl_photo, *c_lbl_video, *c_ring, *c_core;
static lv_img_dsc_t c_dsc;
static uint32_t c_flash_seen;

static void flash_anim_cb(void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

static void cam_check_flash(void)
{
    if (M->cam_flash == c_flash_seen) return;
    c_flash_seen = M->cam_flash;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c_flash);
    lv_anim_set_exec_cb(&a, flash_anim_cb);
    lv_anim_set_values(&a, 230, 0);
    lv_anim_set_time(&a, 280);
    lv_anim_start(&a);
}

void ui_camera_frame(void)
{
    if (!c_img) return;
    if (!M->cam_pix || M->cam_w <= 0) {
        lv_obj_add_flag(c_img, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    dsc_from(&c_dsc, M->cam_pix, M->cam_w, M->cam_h);
    lv_img_set_src(c_img, &c_dsc);
    lv_obj_align(c_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(c_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(c_img);
    cam_check_flash();
}

static void mode_draw(void)
{
    bool v = M->cam_video;
    ui_text_color(c_lbl_photo, v ? C_DIM : C_TEXT);
    ui_text_color(c_lbl_video, v ? C_TEXT : C_DIM);
    ui_bg_color(c_mode_photo, v ? C_BG : C_BLUE);
    ui_bg_color(c_mode_video, v ? C_RED : C_BG);
    lv_obj_set_style_bg_opa(c_mode_photo, v ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(c_mode_video, v ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void camera_refresh(void)
{
    mode_draw();
    const char *msg = "";
    switch (M->cam_state) {
    case MX_CAM_STARTING: msg = "Starting camera..."; break;
    case MX_CAM_NOCHIP: msg = "Camera chip not responding.\nSettings > Camera chip"; break;
    case MX_CAM_STOPPED: msg = "Camera off"; break;
    default: break;
    }
    ui_set_text(c_msg, msg);
    if (msg[0]) lv_obj_clear_flag(c_msg, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(c_msg, LV_OBJ_FLAG_HIDDEN);

    char b[64];
    if (M->cam_msg[0]) snprintf(b, sizeof b, "%s", M->cam_msg);
    else if (M->sd_state == MX_SD_ST_NONE) snprintf(b, sizeof b, "Insert a memory card to save");
    else if (M->sd_state == MX_SD_ST_ERROR) snprintf(b, sizeof b, "Card unreadable, use FAT32");
    else if (M->sd_total_mb >= 1024) snprintf(b, sizeof b, "%.1f GB free", (double)M->sd_free_mb / 1024.0);
    else snprintf(b, sizeof b, "%u MB free", (unsigned)M->sd_free_mb);
    ui_set_text(c_toast, b);
    ui_text_color(c_toast, M->cam_msg[0] ? C_TEXT : (M->sd_state == MX_SD_ST_OK ? C_DIM : C_ORANGE));

    if (M->cam_rec) {
        snprintf(b, sizeof b, "%02d:%02d", M->cam_rec_s / 60, M->cam_rec_s % 60);
        ui_set_text(c_rec_lbl, b);
        lv_obj_clear_flag(c_rec_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(c_rec_lbl, LV_OBJ_FLAG_HIDDEN);
        /* the shutter becomes a stop square */
        lv_obj_set_size(c_core, 26, 26);
        lv_obj_set_style_radius(c_core, 6, 0);
    } else {
        lv_obj_add_flag(c_rec_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c_rec_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(c_core, M->cam_video ? 38 : 44, M->cam_video ? 38 : 44);
        lv_obj_set_style_radius(c_core, LV_RADIUS_CIRCLE, 0);
    }
    ui_bg_color(c_core, M->cam_video ? C_RED : lv_color_white());
    lv_obj_center(c_core);
    cam_check_flash();
}

static void camera_enter(void)
{
    if (A->camera_open) A->camera_open();
}

static void camera_leave(void)
{
    if (A->camera_close) A->camera_close();
    lv_obj_add_flag(c_img, LV_OBJ_FLAG_HIDDEN);
}

static void camera_confirm(void)
{
    if (A->camera_shutter) A->camera_shutter();
}

static void mode_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    if (!ui_is_tap()) return;
    bool video = (intptr_t)lv_event_get_user_data(e) == 1;
    if (M->cam_rec || video == M->cam_video) return;
    M->cam_video = video;
    if (A->camera_set_video) A->camera_set_video(video);
    camera_refresh();
}

static void shutter_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    if (!ui_is_tap()) return;
    camera_confirm();
}

static lv_obj_t *mode_btn(lv_obj_t *pill, const char *txt, int x, int id, lv_obj_t **lbl)
{
    lv_obj_t *b = ui_box(pill, 70, 26, C_BLUE, 13);
    lv_obj_align(b, LV_ALIGN_CENTER, x, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, ui_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    *lbl = ui_lbl(b, &lv_font_montserrat_14, C_TEXT, txt, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(*lbl, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static void build_camera(void)
{
    page_t *pg = &P[MX_PAGE_CAMERA];
    pg->scr = ui_scr();

    c_img = lv_img_create(pg->scr);
    lv_obj_add_flag(c_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(c_img, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *pill = ui_box(pg->scr, 132, 26, C_BG, 13);
    lv_obj_set_style_bg_opa(pill, SHEET_OPA, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 10);
    ui_chrome(pg, false);

    c_rec_dot = ui_circle(pg->scr, 10, C_RED);
    lv_obj_align(c_rec_dot, LV_ALIGN_TOP_MID, -28, 50);
    c_rec_lbl = ui_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "00:00", LV_ALIGN_TOP_MID, 8, 45);
    lv_obj_add_flag(c_rec_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c_rec_lbl, LV_OBJ_FLAG_HIDDEN);

    c_msg = dim_label(pg->scr, &lv_font_montserrat_16, C_TEXT, LV_ALIGN_CENTER, 0, -20, 270);
    lv_label_set_long_mode(c_msg, LV_LABEL_LONG_WRAP);
    c_toast = dim_label(pg->scr, &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_MID, 0, 244, 240);

    lv_obj_t *mode = ui_box(pg->scr, 150, 30, C_BG, 15);
    lv_obj_set_style_bg_opa(mode, SHEET_OPA, 0);
    lv_obj_align(mode, LV_ALIGN_TOP_MID, 0, 276);
    c_mode_photo = mode_btn(mode, "PHOTO", -37, 0, &c_lbl_photo);
    c_mode_video = mode_btn(mode, "VIDEO", 37, 1, &c_lbl_video);

    c_ring = ui_circle(pg->scr, 62, C_BG);
    lv_obj_set_style_bg_opa(c_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c_ring, 4, 0);
    lv_obj_set_style_border_color(c_ring, lv_color_white(), 0);
    lv_obj_align(c_ring, LV_ALIGN_TOP_MID, 0, 314);
    lv_obj_add_flag(c_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c_ring, ui_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(c_ring, shutter_cb, LV_EVENT_CLICKED, NULL);
    c_core = ui_circle(c_ring, 44, lv_color_white());
    lv_obj_center(c_core);
    lv_obj_clear_flag(c_core, LV_OBJ_FLAG_CLICKABLE);

    c_flash = ui_box(pg->scr, W, H, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(c_flash, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(c_flash, LV_OBJ_FLAG_CLICKABLE);

    pg->refresh = camera_refresh;
    pg->enter = camera_enter;
    pg->leave = camera_leave;
    pg->confirm = camera_confirm;
}

/* ------------------------------------------------------------------ */
/* GALLERY (carousel page: a preview and the way in)                    */
/* ------------------------------------------------------------------ */

static lv_obj_t *g_img, *g_icon, *g_line1, *g_line2, *g_hint;
static lv_img_dsc_t g_dsc;

static void gallery_refresh(void)
{
    char b[64];
    bool have = M->sd_state == MX_SD_ST_OK && M->gal_count > 0;
    if (M->sd_state == MX_SD_ST_NONE) {
        ui_set_text(g_line1, "No memory card");
        ui_set_text(g_line2, "Insert a card to see photos and videos");
        ui_set_text(g_hint, "");
    } else if (M->sd_state == MX_SD_ST_ERROR) {
        ui_set_text(g_line1, "Card unreadable");
        ui_set_text(g_line2, "Format it as FAT32 and try again");
        ui_set_text(g_hint, "");
    } else if (!have) {
        ui_set_text(g_line1, "Nothing here yet");
        ui_set_text(g_line2, "Photos and videos you take appear here");
        ui_set_text(g_hint, "");
    } else {
        snprintf(b, sizeof b, "%d item%s", M->gal_count, M->gal_count == 1 ? "" : "s");
        ui_set_text(g_line1, b);
        ui_set_text(g_line2, M->gal_name);
        ui_set_text(g_hint, "Tap to open");
    }
    if (have && M->gal_pix) {
        lv_obj_clear_flag(g_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

static void gallery_enter(void)
{
    if (A->gallery_open) A->gallery_open();
}

static void gallery_confirm(void)
{
    if (M->sd_state == MX_SD_ST_OK && M->gal_count > 0) ui_goto(MX_PAGE_VIEWER, true);
    else if (A->sd_refresh) A->sd_refresh();
}

static void build_gallery(void)
{
    page_t *pg = &P[MX_PAGE_GALLERY];
    pg->scr = ui_scr();
    ui_chrome(pg, true);
    ui_lbl(pg->scr, &lv_font_montserrat_20, C_TEXT, "Gallery", LV_ALIGN_TOP_MID, 0, 56);

    g_icon = ui_circle(pg->scr, 110, C_PANEL);
    lv_obj_align(g_icon, LV_ALIGN_TOP_MID, 0, 108);
    ui_lbl(g_icon, &lv_font_montserrat_40, C_DIM, LV_SYMBOL_IMAGE, LV_ALIGN_CENTER, 0, 0);

    g_img = lv_img_create(pg->scr);
    lv_obj_add_flag(g_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_img, LV_OBJ_FLAG_CLICKABLE);

    g_line1 = ui_lbl(pg->scr, &lv_font_montserrat_16, C_TEXT, "", LV_ALIGN_TOP_MID, 0, 270);
    lv_obj_set_width(g_line1, 260);
    lv_obj_set_style_text_align(g_line1, LV_TEXT_ALIGN_CENTER, 0);
    g_line2 = ui_lbl(pg->scr, &lv_font_montserrat_12, C_DIM, "", LV_ALIGN_TOP_MID, 0, 296);
    lv_obj_set_width(g_line2, 240);
    lv_label_set_long_mode(g_line2, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g_line2, LV_TEXT_ALIGN_CENTER, 0);
    g_hint = ui_lbl(pg->scr, &lv_font_montserrat_12, C_BLUE, "", LV_ALIGN_TOP_MID, 0, 320);

    pg->refresh = gallery_refresh;
    pg->enter = gallery_enter;
    pg->confirm = gallery_confirm;
    pg->tap = gallery_confirm;
}

/* ------------------------------------------------------------------ */
/* VIEWER                                                              */
/* ------------------------------------------------------------------ */

static lv_obj_t *v_img, *v_name, *v_info, *v_play, *v_busy, *v_sheet;
static lv_img_dsc_t v_dsc;
static ui_list_t L_vm;
static bool v_menu, v_armed;
enum { VM_PLAY, VM_DELETE, VM_GALLERY, VM_CLOSE, VM_N };

/* shared between the gallery preview and the viewer */
void ui_gallery_frame(void)
{
    if (!v_img) return;
    if (!M->gal_pix || M->gal_w <= 0) {
        lv_obj_add_flag(v_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_img, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    dsc_from(&v_dsc, M->gal_pix, M->gal_w, M->gal_h);
    lv_img_set_src(v_img, &v_dsc);
    lv_obj_align(v_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(v_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(v_img);

    dsc_from(&g_dsc, M->gal_pix, M->gal_w, M->gal_h);
    lv_img_set_src(g_img, &g_dsc);
    lv_img_set_zoom(g_img, 128);
    lv_obj_align(g_img, LV_ALIGN_TOP_MID, 0, 180 - M->gal_h / 2);
    if (cur == MX_PAGE_GALLERY && M->gal_count > 0) lv_obj_clear_flag(g_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(g_img);
}

static void menu_close(void)
{
    v_menu = false;
    v_armed = false;
    lv_obj_add_flag(v_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void viewer_refresh(void)
{
    char b[96];
    snprintf(b, sizeof b, "%s", M->gal_name);
    ui_set_text(v_name, b);
    snprintf(b, sizeof b, "%d / %d   %s", M->gal_idx + 1, M->gal_count, M->gal_info);
    ui_set_text(v_info, b);
    if (M->gal_video && !M->gal_playing && !M->gal_loading) lv_obj_clear_flag(v_play, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(v_play, LV_OBJ_FLAG_HIDDEN);
    if (M->gal_loading) lv_obj_clear_flag(v_busy, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(v_busy, LV_OBJ_FLAG_HIDDEN);

    if (v_menu) {
        L_vm.skip[VM_PLAY] = !M->gal_video;
        ui_list_set(&L_vm, VM_PLAY, M->gal_playing ? "Stop video" : "Play video", NULL, NULL);
        ui_list_set(&L_vm, VM_DELETE, v_armed ? "Tap again to delete" : "Delete", v_armed ? "This cannot be undone" : M->gal_name, NULL);
        ui_list_layout(&L_vm);
    }
}

static void viewer_show(int idx)
{
    if (M->gal_count <= 0) return;
    if (idx < 0) idx = M->gal_count - 1;
    if (idx >= M->gal_count) idx = 0;
    if (A->gallery_show) A->gallery_show(idx);
}

static void viewer_enter(void)
{
    menu_close();
    if (A->gallery_show) A->gallery_show(M->gal_idx);
}

static void viewer_leave(void)
{
    menu_close();
    if (M->gal_playing && A->gallery_play) A->gallery_play();
    if (A->gallery_close) A->gallery_close();
}

static bool viewer_rotate(int dir)
{
    if (v_menu) { v_armed = false; return ui_list_rotate(&L_vm, dir); }
    if (M->gal_playing && A->gallery_play) { A->gallery_play(); return true; }     /* any input stops the video */
    viewer_show(M->gal_idx + dir);
    return true;
}

static void viewer_confirm(void)
{
    if (M->gal_playing) { if (A->gallery_play) A->gallery_play(); return; }
    if (!v_menu) {
        v_menu = true;
        v_armed = false;
        L_vm.sel = M->gal_video ? VM_PLAY : VM_DELETE;
        L_vm.sig_ok = false;
        lv_obj_clear_flag(v_sheet, LV_OBJ_FLAG_HIDDEN);
        viewer_refresh();
        return;
    }
    int row = -1;
    for (int i = 0; i < L_vm.n; i++) if (L_vm.ord[i] == L_vm.sel) row = i;
    switch (row) {
    case VM_PLAY:
        menu_close();
        if (A->gallery_play) A->gallery_play();
        break;
    case VM_DELETE:
        if (!v_armed) { v_armed = true; break; }
        menu_close();
        if (A->gallery_delete) A->gallery_delete(M->gal_idx);
        if (M->gal_count <= 1) ui_back();
        break;
    case VM_GALLERY:
        menu_close();
        ui_back();
        return;
    default:
        menu_close();
        break;
    }
}

/* a tap on the picture opens the menu (or stops a video); a tap beside the
   menu rows closes it */
static void viewer_tap(void)
{
    if (v_menu) { menu_close(); return; }
    viewer_confirm();
}

static bool viewer_gesture(lv_dir_t d)
{
    if (v_menu) return true;
    if (d == LV_DIR_LEFT) { viewer_show(M->gal_idx + 1); return true; }
    if (d == LV_DIR_RIGHT) { viewer_show(M->gal_idx - 1); return true; }
    if (d == LV_DIR_BOTTOM) { ui_back(); return true; }
    return true;
}

static void vm_click(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_stop_bubbling(e);
    if (!ui_is_tap()) return;
    L_vm.sel = L_vm.ord[i] >= 0 ? L_vm.ord[i] : 0;
    viewer_confirm();
}

static void build_viewer(void)
{
    page_t *pg = &P[MX_PAGE_VIEWER];
    pg->scr = ui_scr();
    ui_chrome(pg, false);
    lv_obj_add_flag(pg->batt.cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pg->batt.pct, LV_OBJ_FLAG_HIDDEN);

    v_img = lv_img_create(pg->scr);
    lv_obj_add_flag(v_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(v_img, LV_OBJ_FLAG_CLICKABLE);

    v_name = dim_label(pg->scr, &lv_font_montserrat_14, C_TEXT, LV_ALIGN_TOP_MID, 0, 22, 190);
    v_info = dim_label(pg->scr, &lv_font_montserrat_12, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -30, 230);

    v_play = ui_circle(pg->scr, 76, C_BG);
    lv_obj_set_style_bg_opa(v_play, LV_OPA_50, 0);
    lv_obj_set_style_border_width(v_play, 3, 0);
    lv_obj_set_style_border_color(v_play, lv_color_white(), 0);
    lv_obj_center(v_play);
    lv_obj_clear_flag(v_play, LV_OBJ_FLAG_CLICKABLE);
    ui_lbl(v_play, &lv_font_montserrat_28, lv_color_white(), LV_SYMBOL_PLAY, LV_ALIGN_CENTER, 3, 0);
    lv_obj_add_flag(v_play, LV_OBJ_FLAG_HIDDEN);

    v_busy = dim_label(pg->scr, &lv_font_montserrat_16, C_TEXT, LV_ALIGN_CENTER, 0, 0, 150);
    lv_label_set_text(v_busy, "Loading...");
    lv_obj_add_flag(v_busy, LV_OBJ_FLAG_HIDDEN);

    /* options sheet */
    v_sheet = ui_box(pg->scr, W, H, C_BG, 0);
    lv_obj_set_style_bg_opa(v_sheet, LV_OPA_80, 0);
    lv_obj_add_flag(v_sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(v_sheet, LV_OBJ_FLAG_CLICKABLE);
    L_vm.parent = v_sheet;
    L_vm.top = 96;
    ui_list_add(&L_vm, lv_color_hex(0x2ED573), LV_SYMBOL_PLAY, "Play video", "", vm_click);
    ui_list_add(&L_vm, lv_color_hex(0xFF4757), LV_SYMBOL_TRASH, "Delete", "", vm_click);
    ui_list_add(&L_vm, lv_color_hex(0x3A4254), LV_SYMBOL_LEFT, "Back to gallery", NULL, vm_click);
    ui_list_add(&L_vm, lv_color_hex(0x3A4254), LV_SYMBOL_CLOSE, "Close menu", NULL, vm_click);
    ui_list_layout(&L_vm);

    pg->refresh = viewer_refresh;
    pg->enter = viewer_enter;
    pg->leave = viewer_leave;
    pg->rotate = viewer_rotate;
    pg->confirm = viewer_confirm;
    pg->tap = viewer_tap;
    pg->gesture = viewer_gesture;
    ui_register_hidden(MX_PAGE_VIEWER, MX_PAGE_GALLERY);
}

void ui_build_camera_pages(void)
{
    build_camera();
    build_gallery();
    build_viewer();
}
