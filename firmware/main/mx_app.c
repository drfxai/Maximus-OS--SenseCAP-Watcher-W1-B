/*
 * Application glue: owns the UI model, turns knob events into UI input, and
 * runs the background work (prices, Telegram inbox, voice, camera, journal).
 *
 * Threading: LVGL objects and the model are touched only under lvgl_port_lock
 * or from the LVGL timer. Network calls happen in app_task with the lock
 * released, then results are copied in.
 */
#include "mx_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/ledc.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sensecap-watcher.h"

#include "mx_audio.h"
#include "mx_ble.h"
#include "mx_camera.h"
#include "mx_config.h"
#include "mx_himax.h"
#include "mx_knob.h"
#include "mx_media.h"
#include "mx_net.h"
#include "mx_relay.h"
#include "mx_rgb.h"
#include "mx_sd.h"
#include "ui/ui.h"

static const char *TAG = "app";

#define JOURNAL_FILE "/spiffs/journal.tsv"

typedef enum {
    CMD_LISTEN, CMD_JOURNAL, CMD_SNAPSHOT, CMD_PRICES,
    CMD_WIFI_SCAN, CMD_WIFI_JOIN, CMD_WIFI_FORGET, CMD_WIFI_ENABLE,
    CMD_BT, CMD_CHIP, CMD_SD,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    int arg;
    char s1[33];
    char s2[65];
} cmd_t;

static mx_model_t M;
static QueueHandle_t s_cmdq;
static volatile bool s_stop_rec, s_recording, s_chip_busy, s_rtc_written;
static volatile int64_t s_last_activity_ms;
static bool s_dimmed, s_off;
static bool s_usb_was;          /* USB power seen at the last check */
static int s_bat_avg = -1;      /* smoothed battery percentage */
static uint64_t s_last_sig_ts;

void mx_activity(void)
{
    s_last_activity_ms = esp_timer_get_time() / 1000;
}

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void send_cmd_arg(cmd_type_t t, int arg, const char *s1, const char *s2)
{
    cmd_t c;
    memset(&c, 0, sizeof c);
    c.type = t;
    c.arg = arg;
    if (s1) strlcpy(c.s1, s1, sizeof c.s1);
    if (s2) strlcpy(c.s2, s2, sizeof c.s2);
    xQueueSend(s_cmdq, &c, 0);
}

static void send_cmd(cmd_type_t t) { send_cmd_arg(t, 0, NULL, NULL); }

/* ---- actions (called from the LVGL thread) ----------------------- */

static void act_assistant(void)
{
    if (s_recording) s_stop_rec = true;
    else send_cmd(CMD_LISTEN);
}

static void act_journal(void)
{
    if (s_recording) s_stop_rec = true;
    else send_cmd(CMD_JOURNAL);
}

static void act_set_mode(int i)
{
    mx_cfg()->mode = (uint8_t)i;
    mx_cfg_save();
}

static void act_brightness(int pct)
{
    mx_cfg()->brightness = (uint8_t)pct;
    mx_cfg_save();
    bsp_lcd_brightness_set(pct);
}

/* while a slider moves: light only, the setting is saved on release */
static void act_preview_brightness(int pct)
{
    bsp_lcd_brightness_set(pct);
}

/* The BSP drives the backlight from LEDC timer 1 at 5 kHz. A faster PWM
   does not change what the eye sees but removes the dark bands a phone
   camera records when it photographs the screen. */
static void backlight_freq_apply(int idx)
{
    static const uint32_t hz[3] = { 5000, 20000, 40000 };
    if (idx < 0 || idx > 2) idx = 0;
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, hz[idx]);
}

static void act_bl_freq(int idx)
{
    mx_cfg()->bl_freq = (uint8_t)idx;
    mx_cfg_save();
    backlight_freq_apply(idx);
}

static void act_volume(int pct)
{
    mx_cfg()->volume = (uint8_t)pct;
    mx_cfg_save();
    mx_audio_set_volume(pct);
    mx_audio_chime(0);
}

static void act_snapshot(void) { send_cmd(CMD_SNAPSHOT); }

static void act_wifi_enable(bool on) { send_cmd_arg(CMD_WIFI_ENABLE, on, NULL, NULL); }
static void act_wifi_scan(void)
{
    if (!M.wifi_scanning) {
        M.wifi_scanning = true;
        send_cmd(CMD_WIFI_SCAN);
    }
}
static void act_wifi_join(const char *ssid, const char *pass) { send_cmd_arg(CMD_WIFI_JOIN, 0, ssid, pass); }
static void act_wifi_forget(void) { send_cmd(CMD_WIFI_FORGET); }
static void act_bt(bool on) { send_cmd_arg(CMD_BT, on, NULL, NULL); }

static void act_timeout(int idx)
{
    mx_cfg()->timeout_idx = (uint8_t)idx;
    mx_cfg_save();
}

static void act_disp_speed(int idx)
{
    mx_cfg()->disp_speed = (uint8_t)idx;
    mx_cfg_save();
}

static void act_set_time(const struct tm *lt)
{
    struct tm t = *lt;
    t.tm_isdst = -1;
    time_t e = mktime(&t);
    struct timeval tv = { .tv_sec = e };
    settimeofday(&tv, NULL);
    struct tm u;
    gmtime_r(&e, &u);
    bsp_rtc_set_time(&u);
    mx_cfg()->auto_time = false;
    mx_cfg_save();
    mx_net_set_auto_time(false);
    localtime_r(&e, &M.now);
    M.time_valid = true;
    M.auto_time = false;
}

static void act_set_tz(int idx)
{
    if (idx < 0 || idx >= mx_tz_n) return;
    strlcpy(mx_cfg()->tz, mx_tz[idx].rule, sizeof mx_cfg()->tz);
    mx_cfg_save();
    mx_net_apply_tz();
    M.tz_idx = idx;
    time_t n = time(NULL);
    localtime_r(&n, &M.now);
}

static void act_auto_time(bool on)
{
    mx_cfg()->auto_time = on;
    mx_cfg_save();
    mx_net_set_auto_time(on);
    if (on) s_rtc_written = false;
}

static void act_cam_open(void) { mx_media_camera_open(); }
static void act_cam_close(void) { mx_media_camera_close(); }
static void act_cam_shutter(void) { mx_media_camera_shutter(); }
static void act_cam_video(bool v) { mx_media_camera_set_video(v); }
static void act_gal_open(void) { mx_media_gallery_open(); }
static void act_gal_show(int i) { mx_media_gallery_show(i); }
static void act_gal_play(void) { mx_media_gallery_play(); }
static bool act_gal_delete(int i) { return mx_media_gallery_delete(i); }
static void act_gal_close(void) { mx_media_gallery_close(); }
static void act_chip(void) { send_cmd(CMD_CHIP); }
static void act_sd_refresh(void) { send_cmd(CMD_SD); }
static void act_prices(void) { send_cmd(CMD_PRICES); }
static void act_reboot(void) { esp_restart(); }

static void power_off(void)
{
    ESP_LOGI(TAG, "power off");
    mx_rgb_set(MX_RGB_OFF);
    mx_audio_chime(3);
    bsp_lcd_brightness_set(0);
    vTaskDelay(pdMS_TO_TICKS(400));
    bsp_system_shutdown();
    vTaskDelay(pdMS_TO_TICKS(1500));
    /* Still running means USB power is holding the rail up: sleep until the
       knob is touched, which is the closest thing to "off" on a cable. */
    bsp_system_deep_sleep(0);
}

static bool act_pin_verify(const char *p) { return mx_pin_verify(p); }
static bool act_pin_store(const char *p) { return mx_pin_store(p); }
static void act_pin_clear(void) { mx_pin_clear(); }

static const mx_actions_t ACTIONS = {
    .assistant_toggle = act_assistant,
    .journal_toggle = act_journal,
    .set_mode = act_set_mode,
    .set_brightness = act_brightness,
    .preview_brightness = act_preview_brightness,
    .set_volume = act_volume,
    .snapshot = act_snapshot,
    .wifi_set_enabled = act_wifi_enable,
    .wifi_scan = act_wifi_scan,
    .wifi_join = act_wifi_join,
    .wifi_forget = act_wifi_forget,
    .bt_set_enabled = act_bt,
    .set_timeout = act_timeout,
    .set_disp_speed = act_disp_speed,
    .set_bl_freq = act_bl_freq,
    .set_time = act_set_time,
    .set_tz = act_set_tz,
    .set_auto_time = act_auto_time,
    .camera_open = act_cam_open,
    .camera_close = act_cam_close,
    .camera_shutter = act_cam_shutter,
    .camera_set_video = act_cam_video,
    .gallery_open = act_gal_open,
    .gallery_show = act_gal_show,
    .gallery_play = act_gal_play,
    .gallery_delete = act_gal_delete,
    .gallery_close = act_gal_close,
    .chip_reinstall = act_chip,
    .sd_refresh = act_sd_refresh,
    .refresh_prices = act_prices,
    .reboot = act_reboot,
    .power_off = power_off,
    .pin_verify = act_pin_verify,
    .pin_store = act_pin_store,
    .pin_clear = act_pin_clear,
};

/* ---- UI tick (LVGL context) -------------------------------------- */

static void wake_screen(void)
{
    if (!s_off && !s_dimmed) return;
    s_off = false;
    s_dimmed = false;
    ui_screen_blank(false);
    bsp_lcd_brightness_set(mx_cfg()->brightness);
}

static void ui_tick(lv_timer_t *t)
{
    static int n;
    mx_knob_evt_t e;
    while (mx_knob_poll(&e)) {
        if (e.type == MX_K_POWEROFF) {
            ui_set_hold(MX_HOLD_OFF_MS);
            power_off();
            continue;
        }
        if (s_off) {
            /* the first knob action only wakes the screen */
            if (e.type == MX_K_LEFT || e.type == MX_K_RIGHT || e.type == MX_K_CONFIRM || e.type == MX_K_HOME) {
                mx_activity();
                wake_screen();
            }
            continue;
        }
        switch (e.type) {
        case MX_K_LEFT: mx_activity(); ui_input(MX_IN_LEFT); break;
        case MX_K_RIGHT: mx_activity(); ui_input(MX_IN_RIGHT); break;
        case MX_K_CONFIRM: ui_input(MX_IN_CONFIRM); break;
        case MX_K_HOME: ui_set_hold(0); ui_input(MX_IN_HOME); break;
        case MX_K_HOLD: mx_activity(); ui_set_hold(e.ms); break;
        default: break;
        }
    }
    int64_t idle = now_ms() - s_last_activity_ms;
    int64_t limit = (int64_t)mx_timeout_s[mx_cfg()->timeout_idx] * 1000;
    bool busy = s_recording || s_chip_busy || M.assist == MX_ASSIST_THINKING || mx_media_busy();
    if (busy && idle > limit / 2) mx_activity();         /* a camera preview or a flash keeps the screen on */
    if (!s_off && idle > limit) {
        s_off = true;
        ui_screen_blank(true);
        bsp_lcd_brightness_set(0);
        if (mx_cfg()->pin_set && !M.locked) {
            M.locked = true;
            ui_goto(MX_PAGE_SECURITY, false);
        }
    } else if (s_off && idle < 1500) {
        wake_screen();
    } else if (!s_off && !s_dimmed && idle > limit * 7 / 10) {
        s_dimmed = true;                                  /* a warning dim before the screen goes off */
        bsp_lcd_brightness_set(12);
    } else if (!s_off && s_dimmed && idle < limit * 7 / 10) {
        wake_screen();
    }
    if (!s_off && ++n % 4 == 0) ui_refresh();
}

/* ---- helpers for app_task --------------------------------------- */

static void level_cb(int level) { M.mic_level = level; }

static void add_note(mx_note_kind_t k, const char *title, const char *body)
{
    memmove(&M.notes[1], &M.notes[0], sizeof(mx_note_t) * (MX_MAX_NOTES - 1));
    mx_note_t *n = &M.notes[0];
    memset(n, 0, sizeof *n);
    n->kind = k;
    strlcpy(n->title, title, sizeof n->title);
    strlcpy(n->body, body, sizeof n->body);
    n->ts = time(NULL);
    if (M.n_notes < MX_MAX_NOTES) M.n_notes++;
}

static void set_assist(mx_assist_state_t s, mx_rgb_state_t rgb)
{
    if (!lvgl_port_lock(0)) return;
    M.assist = s;
    ui_ring_color(mx_rgb_color(rgb));
    lvgl_port_unlock();
    if (rgb == MX_RGB_OFF) mx_rgb_set(MX_RGB_OFF); else mx_rgb_set(rgb);
}

static void journal_append(const char *text)
{
    FILE *f = fopen(JOURNAL_FILE, "a");
    if (!f) return;
    char clean[320];
    strlcpy(clean, text, sizeof clean);
    for (char *p = clean; *p; p++) if (*p == '\n' || *p == '\t') *p = ' ';
    fprintf(f, "%lld\t%s\n", (long long)time(NULL), clean);
    fclose(f);
}

static void journal_load(void)
{
    FILE *f = fopen(JOURNAL_FILE, "r");
    if (!f) return;
    char line[400];
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        count++;
        *tab = 0;
        M.journal_ts = (time_t)atoll(line);
        strlcpy(M.journal_last, tab + 1, sizeof M.journal_last);
        size_t n = strlen(M.journal_last);
        while (n && (M.journal_last[n - 1] == '\n' || M.journal_last[n - 1] == '\r')) M.journal_last[--n] = 0;
    }
    M.journal_count = count;
    fclose(f);
}

static void do_voice(bool journal)
{
    const char *mode = mx_modes[M.mode].key;
    if (!mx_cfg_configured()) {
        if (lvgl_port_lock(0)) {
            snprintf(M.reply, sizeof M.reply, "The relay is not set up yet. Open the Maximus install page and finish the setup step.");
            M.assist = MX_ASSIST_ERROR;
            lvgl_port_unlock();
        }
        mx_audio_chime(2);
        return;
    }
    if (lvgl_port_lock(0)) {
        if (journal) M.journal_rec = true;
        else { M.assist = MX_ASSIST_LISTENING; M.heard[0] = M.reply[0] = 0; ui_ring_color(mx_rgb_color(MX_RGB_LISTENING)); }
        M.mic_level = 0;
        lvgl_port_unlock();
    }
    mx_rgb_set(MX_RGB_LISTENING);
    mx_audio_beep(1200, 60);

    s_stop_rec = false;
    s_recording = true;
    size_t n = 0;
    int16_t *pcm = mx_audio_record(journal ? 45000 : 12000, &s_stop_rec, !journal, level_cb, &n);
    s_recording = false;
    mx_audio_beep(800, 50);

    if (lvgl_port_lock(0)) {
        M.journal_rec = false;
        if (!pcm) M.assist = MX_ASSIST_IDLE;
        else if (!journal) M.assist = MX_ASSIST_THINKING;
        ui_ring_color(mx_rgb_color(pcm && !journal ? MX_RGB_THINKING : MX_RGB_OFF));
        lvgl_port_unlock();
    }
    if (!pcm) { mx_rgb_set(MX_RGB_OFF); return; }
    mx_rgb_set(journal ? MX_RGB_PROCESSING : MX_RGB_THINKING);

    static char heard[MX_REPLY_MAX / 2], reply[MX_REPLY_MAX];
    heard[0] = reply[0] = 0;
    bool ok = mx_relay_voice(pcm, n, journal, mode, heard, sizeof heard, reply, sizeof reply);
    free(pcm);

    if (lvgl_port_lock(0)) {
        ui_ring_color(0);
        if (journal) {
            if (ok && heard[0]) {
                strlcpy(M.journal_last, heard, sizeof M.journal_last);
                M.journal_ts = time(NULL);
                M.journal_count++;
                journal_append(heard);
            }
        } else if (ok) {
            strlcpy(M.heard, heard, sizeof M.heard);
            strlcpy(M.reply, reply, sizeof M.reply);
            M.assist = MX_ASSIST_REPLY;
        } else {
            snprintf(M.reply, sizeof M.reply, "I could not reach the relay. Check Wi-Fi and the Worker URL.");
            M.assist = MX_ASSIST_ERROR;
        }
        lvgl_port_unlock();
    }
    if (ok) { mx_rgb_set(MX_RGB_OFF); mx_audio_chime(0); }
    else { mx_rgb_flash(MX_RGB_WARNING, 4000); mx_audio_chime(2); }
}

static void do_snapshot(void)
{
    if (lvgl_port_lock(0)) { ui_ring_color(mx_rgb_color(MX_RGB_PROCESSING)); lvgl_port_unlock(); }
    mx_rgb_set(MX_RGB_PROCESSING);
    uint8_t *jpeg = NULL;
    size_t len = 0;
    const char *msg;
    bool ok = false;
    if (mx_media_camera_active()) msg = "Close the Camera page first";
    else if (!mx_camera_ready()) msg = "Camera module not responding";
    else if (!mx_camera_snapshot(&jpeg, &len)) msg = "Camera gave no image";
    else if (!mx_relay_photo(jpeg, len, "Maximus camera")) msg = "Photo captured, upload failed";
    else { msg = "Photo sent to Telegram"; ok = true; }
    free(jpeg);
    if (lvgl_port_lock(0)) {
        add_note(MX_NOTE_SYSTEM, "Camera", msg);
        ui_ring_color(0);
        lvgl_port_unlock();
    }
    if (ok) { mx_rgb_set(MX_RGB_OFF); mx_audio_chime(0); }
    else { mx_rgb_flash(MX_RGB_WARNING, 3000); mx_audio_chime(2); }
}

static void fetch_prices(void)
{
    static mx_model_t scratch;
    scratch.n_tk = 0;
    if (!mx_relay_crypto(&scratch)) return;
    if (lvgl_port_lock(0)) {
        memcpy(M.tk, scratch.tk, sizeof M.tk);
        M.n_tk = scratch.n_tk;
        strlcpy(M.price_src, scratch.price_src, sizeof M.price_src);
        lvgl_port_unlock();
    }
}

static void poll_inbox(void)
{
    static mx_model_t scratch;
    static mx_signal_t sig;
    bool got = false;
    if (lvgl_port_lock(0)) {
        memcpy(scratch.notes, M.notes, sizeof scratch.notes);
        scratch.n_notes = M.n_notes;
        lvgl_port_unlock();
    }
    int added = mx_relay_poll(&scratch, &s_last_sig_ts, &sig, &got);
    if (!added && !got) return;
    if (lvgl_port_lock(0)) {
        memcpy(M.notes, scratch.notes, sizeof M.notes);
        M.n_notes = scratch.n_notes;
        if (got) {
            char sub[40], price[24];
            snprintf(sub, sizeof sub, "%s %s", sig.side, sig.critical ? "- stop triggered" : "signal");
            snprintf(price, sizeof price, "%s", sig.entry[0] ? sig.entry : "");
            ui_show_alert(sig.symbol, sub, price, sig.critical);
            mx_activity();
        }
        lvgl_port_unlock();
    }
    if (got) {
        mx_rgb_flash(sig.critical ? MX_RGB_CRITICAL : MX_RGB_WARNING, sig.critical ? 10000 : 5000);
        mx_audio_chime(sig.critical ? 2 : 1);
    } else {
        mx_audio_chime(1);
    }
}

static void time_fallback_from_relay(void)
{
    char *body = NULL;
    if (mx_http("GET", "/v1/time", NULL, NULL, 0, &body, NULL) == 200 && body) {
        char *p = strstr(body, "\"ts\":");
        if (p) {
            struct timeval tv = { .tv_sec = (time_t)(atoll(p + 5) / 1000) };
            settimeofday(&tv, NULL);
        }
    }
    free(body);
}

/* UTC broken-down time to epoch (newlib has no declared timegm). */
static time_t utc_to_epoch(const struct tm *t)
{
    int y = t->tm_year + 1900, m = t->tm_mon + 1;
    if (m <= 2) { y--; m += 12; }
    int64_t days = 365LL * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + t->tm_mday - 719469;
    return (time_t)(days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
}

static void rtc_to_system(void)
{
    struct tm t;
    if (bsp_rtc_get_time(&t) == ESP_OK && t.tm_year >= 124) {
        struct timeval tv = { .tv_sec = utc_to_epoch(&t) };
        settimeofday(&tv, NULL);
    }
}

/* ---- camera chip firmware ---------------------------------------- */

static bool s_console_flash;

static void chip_progress(int pct)
{
    mx_activity();
    if (s_console_flash) printf("MAXIMUS_HIMAX_PROGRESS %d\n", pct);
    if (lvgl_port_lock(0)) {
        M.chip_pct = pct;
        lvgl_port_unlock();
    }
}

bool mx_app_chip_install(bool console)
{
    if (s_chip_busy) return false;
    s_console_flash = console;
    if (!mx_himax_image_present(NULL)) {
        if (console) printf("MAXIMUS_ERR the camera chip image is not in this firmware\n");
        if (lvgl_port_lock(0)) { M.chip_state = MX_CHIP_FAILED; lvgl_port_unlock(); }
        return false;
    }
    s_chip_busy = true;
    if (lvgl_port_lock(0)) {
        M.chip_state = MX_CHIP_FLASHING;
        M.chip_pct = 0;
        lvgl_port_unlock();
    }
    mx_media_camera_close();
    vTaskDelay(pdMS_TO_TICKS(500));
    mx_rgb_set(MX_RGB_PROCESSING);
    bool ok = mx_himax_flash(chip_progress);
    mx_rgb_set(MX_RGB_OFF);
    if (lvgl_port_lock(0)) {
        M.chip_state = ok ? MX_CHIP_DONE : MX_CHIP_FAILED;
        lvgl_port_unlock();
    }
    if (console) printf(ok ? "MAXIMUS_HIMAX_DONE\n" : "MAXIMUS_ERR camera chip install failed\n");
    s_chip_busy = false;
    if (ok) {
        mx_audio_chime(0);
        vTaskDelay(pdMS_TO_TICKS(2500));
        esp_restart();
    }
    mx_audio_chime(2);
    return ok;
}

static void app_task(void *arg)
{
    int64_t last_sec = 0, last_bat = -100000, last_prices = -100000, last_poll = 0, last_ping = -100000, wifi_up_at = 0, last_sd = -100000;
    bool welcomed = false, got_ver = false;
    for (;;) {
        cmd_t c;
        if (xQueueReceive(s_cmdq, &c, pdMS_TO_TICKS(200)) == pdTRUE) {
            switch (c.type) {
            case CMD_LISTEN: do_voice(false); break;
            case CMD_JOURNAL: do_voice(true); break;
            case CMD_SNAPSHOT: do_snapshot(); break;
            case CMD_PRICES: last_prices = -100000; break;
            case CMD_WIFI_ENABLE:
                mx_wifi_enable(c.arg);
                if (!c.arg && lvgl_port_lock(0)) { M.n_aps = 0; lvgl_port_unlock(); }
                break;
            case CMD_WIFI_SCAN: {
                static mx_ap_t found[MX_MAX_APS];
                int n = mx_wifi_scan(found, MX_MAX_APS);
                if (lvgl_port_lock(0)) {
                    memcpy(M.aps, found, sizeof found);
                    M.n_aps = n;
                    M.wifi_scanning = false;
                    lvgl_port_unlock();
                }
                break;
            }
            case CMD_WIFI_JOIN: mx_wifi_join(c.s1, c.s2); break;
            case CMD_WIFI_FORGET: mx_wifi_forget(); break;
            case CMD_BT: {
                bool ok = mx_ble_set(c.arg);
                mx_cfg()->bt_enabled = ok ? c.arg : mx_ble_on();
                mx_cfg_save();
                if (lvgl_port_lock(0)) {
                    M.bt_on = mx_ble_on();
                    M.bt_failed = !ok;
                    lvgl_port_unlock();
                }
                break;
            }
            case CMD_CHIP: mx_app_chip_install(false); break;
            case CMD_SD: last_sd = -100000; break;
            }
        }
        int64_t t = now_ms();
        mx_net_status_t ns;
        mx_net_status(&ns);

        if (t - last_sec >= 1000) {
            last_sec = t;
            time_t nowt = time(NULL);
            struct tm lt;
            localtime_r(&nowt, &lt);
            /* USB power and "full" come from the charger's pins every second
               (the BSP's bsp_system_is_charging() reads its pin inverted);
               the gauge is an ADC average, read every 15 s and smoothed so
               the number does not jump around */
            bool usb = bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0;
            bool full = usb && bsp_exp_io_get_level(BSP_PWR_STDBY_DET) == 0;
            bool bat_due = t - last_bat >= 15000 || usb != s_usb_was;
            s_usb_was = usb;
            int pct = M.battery;
            if (bat_due) {
                last_bat = t;
                int raw = bsp_battery_get_percent();
                pct = s_bat_avg < 0 ? raw : (s_bat_avg * 3 + raw + 2) / 4;
                if (full) pct = 100;
                s_bat_avg = pct;
            }
            bool sd_due = t - last_sd >= 4000;
            mx_sd_state_t sds = mx_sd_state();
            uint32_t sd_tot = 0, sd_free = 0;
            if (sd_due) { last_sd = t; if (sds == MX_SD_OK) mx_sd_space(&sd_tot, &sd_free); }
            mx_chip_state_t chip = M.chip_state;
            if (chip != MX_CHIP_FLASHING && chip != MX_CHIP_DONE && chip != MX_CHIP_FAILED)
                chip = mx_camera_ready() ? MX_CHIP_OK : mx_camera_probing() ? MX_CHIP_UNKNOWN : MX_CHIP_MISSING;
            char ver[24] = "";
            if (chip == MX_CHIP_OK && !got_ver) { got_ver = mx_himax_version(ver, sizeof ver); }
            if (lvgl_port_lock(0)) {
                M.now = lt;
                M.time_valid = ns.time_valid;
                M.wifi = ns.wifi;
                M.relay_ok = ns.relay_ok;
                M.wifi_state = ns.state;
                strlcpy(M.ssid, ns.ssid, sizeof M.ssid);
                strlcpy(M.ip, ns.ip, sizeof M.ip);
                M.battery = pct;
                M.charging = usb;
                M.charged = full;
                M.sd_state = sds == MX_SD_OK ? MX_SD_ST_OK : sds == MX_SD_ERROR ? MX_SD_ST_ERROR : MX_SD_ST_NONE;
                if (sd_due) { M.sd_total_mb = sd_tot; M.sd_free_mb = sd_free; }
                M.chip_state = chip;
                if (ver[0]) strlcpy(M.chip_ver, ver, sizeof M.chip_ver);
                lvgl_port_unlock();
            }
            if (ns.wifi && !wifi_up_at) wifi_up_at = t;
            if (!ns.wifi) wifi_up_at = 0;
            if (mx_cfg()->auto_time) {
                if (!ns.time_valid && ns.wifi && wifi_up_at && t - wifi_up_at > 20000 && mx_cfg_configured()) time_fallback_from_relay();
                if (ns.time_valid && !s_rtc_written && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                    struct tm u;
                    time_t nt = time(NULL);
                    gmtime_r(&nt, &u);
                    bsp_rtc_set_time(&u);
                    s_rtc_written = true;
                }
            }
        }
        if (!welcomed && t > 4000) {
            welcomed = true;
            if (!mx_cfg_configured() && lvgl_port_lock(0)) {
                add_note(MX_NOTE_SYSTEM, "Setup needed", "Open the Maximus install page");
                lvgl_port_unlock();
            }
        }
        if (!ns.wifi || !mx_cfg_configured()) continue;

        if (t - last_ping >= (M.telegram_paired ? 60000 : 12000)) {
            last_ping = t;
            mx_ping_t p;
            mx_relay_ping(&p);
            if (lvgl_port_lock(0)) {
                M.telegram_paired = p.paired;
                strlcpy(M.pair_code, p.pair_code, sizeof M.pair_code);
                M.relay_ok = p.ok;
                lvgl_port_unlock();
            }
        }
        if (t - last_prices >= 30000) {
            last_prices = t;
            fetch_prices();
        }
        if (t - last_poll >= 4000) {
            last_poll = t;
            poll_inbox();
        }
    }
}

void mx_app_start(void)
{
    mx_cfg_t *c = mx_cfg();
    memset(&M, 0, sizeof M);
    strlcpy(M.fw_version, MX_FW_VERSION, sizeof M.fw_version);
    M.mode = c->mode < 4 ? c->mode : 0;
    M.brightness = c->brightness;
    M.volume = c->volume;
    M.pin_set = c->pin_set;
    M.locked = c->pin_set;
    s_usb_was = bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0;
    M.charging = s_usb_was;
    M.battery = s_bat_avg = bsp_battery_get_percent();
    M.bl_freq = c->bl_freq;
    M.timeout_idx = c->timeout_idx;
    M.disp_speed = c->disp_speed;
    M.auto_time = c->auto_time;
    M.bt_on = false;
    M.cam_video = c->cam_video;
    M.tz_idx = -1;
    for (int i = 0; i < mx_tz_n; i++) if (!strcmp(c->tz, mx_tz[i].rule)) M.tz_idx = i;
    M.chip_state = MX_CHIP_UNKNOWN;
    journal_load();
    rtc_to_system();
    mx_activity();

    s_cmdq = xQueueCreate(8, sizeof(cmd_t));
    mx_sd_start();
    mx_media_start(&M);
    if (c->bt_enabled) send_cmd_arg(CMD_BT, 1, NULL, NULL);

    lvgl_port_lock(0);
    ui_init(&M, &ACTIONS);
    lv_timer_create(ui_tick, 50, NULL);
    lvgl_port_unlock();
    backlight_freq_apply(c->bl_freq);
    bsp_lcd_brightness_set(c->brightness);

    xTaskCreatePinnedToCore(app_task, "mx_app", 12288, NULL, 4, NULL, 0);
}
