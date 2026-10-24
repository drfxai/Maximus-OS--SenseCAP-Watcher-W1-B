#include "mx_media.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mx_audio.h"
#include "mx_avi.h"
#include "mx_camera.h"
#include "mx_config.h"
#include "mx_jpeg.h"
#include "mx_sd.h"
#include "ui/ui.h"

static const char *TAG = "media";

#define PREVIEW_W 416
#define PREVIEW_H 416
#define MAX_ITEMS 500
#define MAX_VIDEO_S 600
#define MIN_FREE_MB 8

typedef enum {
    CMD_CAM_OPEN, CMD_CAM_CLOSE, CMD_SHUTTER, CMD_SET_VIDEO,
    CMD_GAL_OPEN, CMD_GAL_SHOW, CMD_GAL_PLAY, CMD_GAL_DELETE, CMD_GAL_CLOSE,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    int arg;
} cmd_t;

static mx_model_t *M;
static QueueHandle_t s_q;
static volatile bool s_cam_open, s_rec, s_play, s_stop_play;

/* camera */
static uint8_t *s_cam_buf[2];
static int s_cam_front;
static mx_avi_t *s_avi;
static int64_t s_rec_start_ms, s_msg_until_ms, s_last_frame_ms;
static char s_rec_path[48];

/* gallery */
typedef struct { char name[16]; int key; } item_t;
static item_t *s_items;
static int s_n_items;
static uint16_t *s_gal_buf[2];
static int s_gal_front;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void post(cmd_type_t t, int arg)
{
    cmd_t c = { t, arg };
    xQueueSend(s_q, &c, 0);
}

bool mx_media_camera_active(void) { return s_cam_open; }
bool mx_media_busy(void) { return s_cam_open || s_play; }

void mx_media_camera_open(void) { post(CMD_CAM_OPEN, 0); }
void mx_media_camera_close(void) { post(CMD_CAM_CLOSE, 0); }
void mx_media_camera_shutter(void) { post(CMD_SHUTTER, 0); }
void mx_media_camera_set_video(bool v) { post(CMD_SET_VIDEO, v); }
void mx_media_gallery_open(void) { post(CMD_GAL_OPEN, 0); }
void mx_media_gallery_show(int idx) { s_stop_play = true; post(CMD_GAL_SHOW, idx); }
void mx_media_gallery_close(void) { s_stop_play = true; post(CMD_GAL_CLOSE, 0); }
bool mx_media_gallery_delete(int idx) { s_stop_play = true; post(CMD_GAL_DELETE, idx); return true; }

void mx_media_gallery_play(void)
{
    if (s_play) s_stop_play = true;
    else post(CMD_GAL_PLAY, 0);
}

/* ---- small helpers ---------------------------------------------------- */

static void toast(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void toast(const char *fmt, ...)
{
    char b[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (lvgl_port_lock(0)) {
        snprintf(M->cam_msg, sizeof M->cam_msg, "%s", b);
        lvgl_port_unlock();
    }
    s_msg_until_ms = now_ms() + 2800;
}

static void set_cam_state(mx_cam_state_t s)
{
    if (lvgl_port_lock(0)) {
        M->cam_state = s;
        lvgl_port_unlock();
    }
}

/* ---- camera ----------------------------------------------------------- */

static void cam_free_buffers(void)
{
    if (lvgl_port_lock(0)) {
        M->cam_pix = NULL;
        ui_camera_frame();
        lvgl_port_unlock();
    }
    for (int i = 0; i < 2; i++) {
        mx_jpeg_free(s_cam_buf[i]);
        s_cam_buf[i] = NULL;
    }
}

static void show_frame(const mx_frame_t *f)
{
    if (!s_cam_buf[0]) {
        for (int i = 0; i < 2; i++) s_cam_buf[i] = mx_jpeg_alloc((size_t)PREVIEW_W * PREVIEW_H * 2);
        if (!s_cam_buf[0] || !s_cam_buf[1]) return;
    }
    int back = !s_cam_front, w = 0, h = 0;
    if (!mx_jpeg_decode(f->jpeg, f->len, s_cam_buf[back], (size_t)PREVIEW_W * PREVIEW_H * 2, &w, &h)) return;
    if (lvgl_port_lock(0)) {
        M->cam_pix = s_cam_buf[back];
        M->cam_w = w;
        M->cam_h = h;
        ui_camera_frame();
        lvgl_port_unlock();
    }
    s_cam_front = back;
}

static void rec_stop(bool announce)
{
    if (!s_avi) return;
    uint32_t elapsed = (uint32_t)(now_ms() - s_rec_start_ms);
    uint32_t frames = mx_avi_frames(s_avi);
    size_t bytes = mx_avi_bytes(s_avi);
    bool ok = mx_avi_close(s_avi, elapsed);
    s_avi = NULL;
    s_rec = false;
    if (lvgl_port_lock(0)) {
        M->cam_rec = false;
        lvgl_port_unlock();
    }
    mx_audio_beep(700, 60);
    if (announce) {
        if (ok && frames) toast("Saved %s", strrchr(s_rec_path, '/') + 1);
        else toast("Video could not be saved");
    }
    ESP_LOGI(TAG, "video: %u frames, %u ms, %u KB", (unsigned)frames, (unsigned)elapsed, (unsigned)(bytes / 1024));
}

static void rec_start(void)
{
    if (!mx_sd_ready()) {
        toast("Insert a memory card first");
        mx_audio_chime(2);
        return;
    }
    if (!mx_sd_next_name("VID", "AVI", s_rec_path, sizeof s_rec_path)) return;
    s_avi = mx_avi_open(s_rec_path, PREVIEW_W, PREVIEW_H);
    if (!s_avi) {
        toast("Cannot write to the card");
        mx_audio_chime(2);
        return;
    }
    s_rec_start_ms = now_ms();
    s_rec = true;
    mx_audio_beep(1400, 60);
    if (lvgl_port_lock(0)) {
        M->cam_rec = true;
        M->cam_rec_s = 0;
        lvgl_port_unlock();
    }
}

static void take_photo(void)
{
    if (!mx_sd_ready()) {
        toast("Insert a memory card first");
        mx_audio_chime(2);
        return;
    }
    char path[48];
    if (!mx_sd_next_name("IMG", "JPG", path, sizeof path)) return;
    set_cam_state(MX_CAM_BUSY);
    mx_audio_beep(1800, 40);
    if (lvgl_port_lock(0)) {
        M->cam_flash++;
        lvgl_port_unlock();
    }
    uint8_t *jpeg = NULL;
    size_t len = 0;
    bool ok = mx_camera_still(&jpeg, &len);
    if (ok) {
        FILE *f = fopen(path, "wb");
        ok = f && fwrite(jpeg, 1, len, f) == len;
        if (f) ok = fclose(f) == 0 && ok;
    }
    free(jpeg);
    if (ok) toast("Saved %s", strrchr(path, '/') + 1);
    else {
        toast("Photo failed, try again");
        mx_audio_chime(2);
    }
    s_last_frame_ms = now_ms();
    set_cam_state(mx_camera_streaming() ? MX_CAM_LIVE : MX_CAM_NOCHIP);
}

static void cam_open(void)
{
    s_cam_open = true;
    if (lvgl_port_lock(0)) {
        M->cam_state = MX_CAM_STARTING;
        M->cam_video = mx_cfg()->cam_video;
        M->cam_msg[0] = 0;
        lvgl_port_unlock();
    }
    for (int i = 0; i < 30 && !mx_camera_ready() && mx_camera_probing(); i++) vTaskDelay(pdMS_TO_TICKS(200));
    if (!mx_camera_ready() || !mx_camera_stream(true)) {
        set_cam_state(MX_CAM_NOCHIP);
        return;
    }
    s_last_frame_ms = now_ms();
    set_cam_state(MX_CAM_LIVE);
}

static void cam_close(void)
{
    if (s_rec) rec_stop(false);
    s_cam_open = false;
    mx_camera_stream(false);
    cam_free_buffers();
    set_cam_state(MX_CAM_STOPPED);
}

static void preview_step(void)
{
    mx_frame_t f;
    int64_t t = now_ms();
    if (M->cam_state != MX_CAM_LIVE) { vTaskDelay(pdMS_TO_TICKS(50)); return; }
    if (mx_camera_next(&f, 120)) {
        s_last_frame_ms = t;
        if (s_rec && s_avi && !mx_avi_add(s_avi, f.jpeg, f.len)) {
            rec_stop(true);
            toast("Card full or removed");
        }
        /* while recording, decoding every frame would make the queue drop some */
        static int64_t last_show;
        if (!s_rec || t - last_show >= 120) {
            show_frame(&f);
            last_show = t;
        }
        free(f.jpeg);
    } else if (t - s_last_frame_ms > 4000) {
        ESP_LOGW(TAG, "no frames for 4 s, restarting the stream");
        if (s_rec) rec_stop(true);
        mx_camera_stream(false);
        if (mx_camera_stream(true)) s_last_frame_ms = now_ms();
        else set_cam_state(MX_CAM_NOCHIP);
    }
    if (s_rec) {
        int secs = (int)((t - s_rec_start_ms) / 1000);
        uint32_t free_mb = 0;
        if (secs != M->cam_rec_s && lvgl_port_lock(0)) {
            M->cam_rec_s = secs;
            lvgl_port_unlock();
        }
        if (secs >= MAX_VIDEO_S) { rec_stop(true); }
        else if (secs % 5 == 4 && mx_sd_space(NULL, &free_mb) && free_mb < MIN_FREE_MB) {
            rec_stop(true);
            toast("Card is full");
        }
    }
    if (M->cam_msg[0] && t > s_msg_until_ms) {
        if (lvgl_port_lock(0)) {
            M->cam_msg[0] = 0;
            lvgl_port_unlock();
        }
    }
}

/* ---- gallery ---------------------------------------------------------- */

static int item_cmp(const void *a, const void *b)
{
    return ((const item_t *)b)->key - ((const item_t *)a)->key;    /* newest first */
}

static void scan_items(void)
{
    if (!s_items) s_items = heap_caps_malloc(sizeof(item_t) * MAX_ITEMS, MALLOC_CAP_SPIRAM);
    s_n_items = 0;
    if (!s_items || !mx_sd_ready()) return;
    DIR *d = opendir(MX_SD_DCIM);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && s_n_items < MAX_ITEMS) {
        const char *n = e->d_name;
        size_t L = strlen(n);
        if (L < 9 || L > 15) continue;
        bool jpg = !strncasecmp(n, "IMG_", 4) && !strcasecmp(n + L - 4, ".JPG");
        bool avi = !strncasecmp(n, "VID_", 4) && !strcasecmp(n + L - 4, ".AVI");
        if (!jpg && !avi) continue;
        item_t *it = &s_items[s_n_items++];
        strlcpy(it->name, n, sizeof it->name);
        it->key = atoi(n + 4);
    }
    closedir(d);
    qsort(s_items, s_n_items, sizeof *s_items, item_cmp);
}

static bool is_video(const char *name)
{
    size_t L = strlen(name);
    return L > 4 && !strcasecmp(name + L - 4, ".AVI");
}

static void gal_publish(const uint16_t *pix, int w, int h, int idx, const char *name, const char *info, bool video, bool playing)
{
    if (!lvgl_port_lock(0)) return;
    M->gal_count = s_n_items;
    M->gal_idx = idx;
    M->gal_video = video;
    M->gal_playing = playing;
    M->gal_loading = false;
    snprintf(M->gal_name, sizeof M->gal_name, "%s", name);
    snprintf(M->gal_info, sizeof M->gal_info, "%s", info);
    M->gal_pix = pix;
    M->gal_w = w;
    M->gal_h = h;
    ui_gallery_frame();
    lvgl_port_unlock();
}

static void gal_free_old(int keep)
{
    for (int i = 0; i < 2; i++) {
        if (i != keep && s_gal_buf[i]) {
            mx_jpeg_free(s_gal_buf[i]);
            s_gal_buf[i] = NULL;
        }
    }
}

/* Loads the picture, or the first frame of a video, for item idx. */
static void gal_load(int idx)
{
    if (s_n_items == 0) {
        if (lvgl_port_lock(0)) {
            M->gal_count = 0;
            M->gal_idx = 0;
            M->gal_pix = NULL;
            M->gal_name[0] = 0;
            M->gal_info[0] = 0;
            M->gal_loading = false;
            ui_gallery_frame();
            lvgl_port_unlock();
        }
        gal_free_old(-1);
        return;
    }
    if (idx < 0) idx = s_n_items - 1;
    if (idx >= s_n_items) idx = 0;
    if (lvgl_port_lock(0)) {
        M->gal_loading = true;
        M->gal_idx = idx;
        M->gal_count = s_n_items;
        lvgl_port_unlock();
    }
    const item_t *it = &s_items[idx];
    char path[48];
    snprintf(path, sizeof path, MX_SD_DCIM "/%s", it->name);
    struct stat st;
    stat(path, &st);
    char info[48];
    uint8_t *jpeg = NULL;
    size_t len = 0;
    bool video = is_video(it->name);
    int w = 0, h = 0;
    uint32_t secs = 0;
    if (video) {
        uint32_t us = 100000, frames = 0;
        mx_avi_rd_t *r = mx_avi_rd_open(path, &w, &h, &us, &frames);
        if (r) {
            size_t cap = 0;
            len = mx_avi_rd_next(r, &jpeg, &cap);
            mx_avi_rd_close(r);
            secs = (uint32_t)((uint64_t)frames * us / 1000000);
        }
    } else {
        FILE *f = fopen(path, "rb");
        if (f) {
            len = (size_t)st.st_size;
            jpeg = heap_caps_malloc(len + 16, MALLOC_CAP_SPIRAM);
            if (jpeg && fread(jpeg, 1, len, f) != len) len = 0;
            fclose(f);
        }
    }
    uint16_t *pix = NULL;
    int pw = 0, ph = 0;
    bool ok = jpeg && len > 100 && mx_jpeg_load_fit(jpeg, len, 416, 416, &pix, &pw, &ph);
    free(jpeg);
    if (video) snprintf(info, sizeof info, "%dx%d  %us  %.1f MB", w, h, (unsigned)secs, (double)st.st_size / 1048576.0);
    else if (ok) snprintf(info, sizeof info, "%dx%d  %u KB", pw, ph, (unsigned)(st.st_size / 1024));
    else snprintf(info, sizeof info, "unreadable");
    int slot = s_gal_front == 0 ? 1 : 0;
    gal_free_old(s_gal_front);
    s_gal_buf[slot] = pix;
    gal_publish(ok ? pix : NULL, pw, ph, idx, it->name, info, video, false);
    s_gal_front = slot;
    gal_free_old(slot);
}

static void gal_play(void)
{
    if (s_n_items == 0) return;
    int idx = M->gal_idx;
    const item_t *it = &s_items[idx];
    if (!is_video(it->name)) return;
    char path[48];
    snprintf(path, sizeof path, MX_SD_DCIM "/%s", it->name);
    int w = 0, h = 0;
    uint32_t us = 100000, frames = 0;
    mx_avi_rd_t *r = mx_avi_rd_open(path, &w, &h, &us, &frames);
    if (!r) return;
    uint8_t *jpeg = NULL;
    size_t cap = 0, len;
    uint16_t *buf[2] = { mx_jpeg_alloc((size_t)w * h * 2), mx_jpeg_alloc((size_t)w * h * 2) };
    s_play = true;
    s_stop_play = false;
    if (lvgl_port_lock(0)) { M->gal_playing = true; lvgl_port_unlock(); }
    int cur = 0;
    int64_t next = now_ms();
    while (!s_stop_play && buf[0] && buf[1] && (len = mx_avi_rd_next(r, &jpeg, &cap)) > 0) {
        int dw, dh;
        if (!mx_jpeg_decode(jpeg, len, (uint8_t *)buf[cur], (size_t)w * h * 2, &dw, &dh)) continue;
        if (lvgl_port_lock(0)) {
            M->gal_pix = buf[cur];
            M->gal_w = dw;
            M->gal_h = dh;
            ui_gallery_frame();
            lvgl_port_unlock();
        }
        cur = !cur;
        next += us / 1000;
        int64_t wait = next - now_ms();
        if (wait > 0) vTaskDelay(pdMS_TO_TICKS(wait));
        else next = now_ms();                     /* behind: do not try to catch up */
    }
    mx_avi_rd_close(r);
    free(jpeg);
    /* back to the still first frame, which also releases the playback buffers */
    gal_load(idx);
    mx_jpeg_free(buf[0]);
    mx_jpeg_free(buf[1]);
    s_play = false;
    s_stop_play = false;
}

static void gal_delete(int idx)
{
    if (idx < 0 || idx >= s_n_items) return;
    char path[48];
    snprintf(path, sizeof path, MX_SD_DCIM "/%s", s_items[idx].name);
    unlink(path);
    scan_items();
    if (idx >= s_n_items) idx = s_n_items - 1;
    gal_load(idx < 0 ? 0 : idx);
}

/* ---- the task --------------------------------------------------------- */

static void media_task(void *arg)
{
    for (;;) {
        cmd_t c;
        if (xQueueReceive(s_q, &c, s_cam_open ? 0 : portMAX_DELAY) == pdTRUE) {
            switch (c.type) {
            case CMD_CAM_OPEN: if (!s_cam_open) cam_open(); break;
            case CMD_CAM_CLOSE: if (s_cam_open) cam_close(); break;
            case CMD_SHUTTER:
                if (!s_cam_open || M->cam_state != MX_CAM_LIVE) break;
                if (M->cam_video) { if (s_rec) rec_stop(true); else rec_start(); }
                else take_photo();
                break;
            case CMD_SET_VIDEO:
                if (lvgl_port_lock(0)) { M->cam_video = c.arg; lvgl_port_unlock(); }
                mx_cfg()->cam_video = c.arg;
                mx_cfg_save();
                break;
            case CMD_GAL_OPEN:
                scan_items();
                gal_load(M->gal_idx < s_n_items ? M->gal_idx : 0);
                break;
            case CMD_GAL_SHOW:
                if (s_n_items == 0) scan_items();
                gal_load(c.arg);
                break;
            case CMD_GAL_PLAY: gal_play(); break;
            case CMD_GAL_DELETE: gal_delete(c.arg); break;
            case CMD_GAL_CLOSE: break;
            }
            continue;
        }
        if (s_cam_open) preview_step();
    }
}

void mx_media_start(mx_model_t *model)
{
    M = model;
    s_q = xQueueCreate(10, sizeof(cmd_t));
    xTaskCreatePinnedToCore(media_task, "mx_media", 10240, NULL, 5, NULL, 0);
}
