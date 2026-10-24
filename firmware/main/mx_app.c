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
#include "mx_camera.h"
#include "mx_config.h"
#include "mx_knob.h"
#include "mx_net.h"
#include "mx_relay.h"
#include "mx_rgb.h"
#include "ui/ui.h"

static const char *TAG = "app";

#define IDLE_DIM_MS 45000
#define IDLE_LOCK_MS 300000
#define JOURNAL_FILE "/spiffs/journal.tsv"

typedef enum { CMD_LISTEN, CMD_JOURNAL, CMD_SNAPSHOT, CMD_PRICES } cmd_t;

static mx_model_t M;
static QueueHandle_t s_cmdq;
static volatile bool s_stop_rec, s_recording;
static volatile int64_t s_last_activity_ms;
static bool s_dimmed;
static uint64_t s_last_sig_ts;

void mx_activity(void)
{
    s_last_activity_ms = esp_timer_get_time() / 1000;
}

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void send_cmd(cmd_t c)
{
    xQueueSend(s_cmdq, &c, 0);
}

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

static void act_volume(int pct)
{
    mx_cfg()->volume = (uint8_t)pct;
    mx_cfg_save();
    mx_audio_set_volume(pct);
    mx_audio_chime(0);
}

static void act_snapshot(void) { send_cmd(CMD_SNAPSHOT); }
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
    .set_volume = act_volume,
    .snapshot = act_snapshot,
    .refresh_prices = act_prices,
    .reboot = act_reboot,
    .power_off = power_off,
    .pin_verify = act_pin_verify,
    .pin_store = act_pin_store,
    .pin_clear = act_pin_clear,
};

/* ---- UI tick (LVGL context) -------------------------------------- */

static void ui_tick(lv_timer_t *t)
{
    static int n;
    mx_knob_evt_t e;
    while (mx_knob_poll(&e)) {
        switch (e.type) {
        case MX_K_LEFT: mx_activity(); ui_input(MX_IN_LEFT); break;
        case MX_K_RIGHT: mx_activity(); ui_input(MX_IN_RIGHT); break;
        case MX_K_CONFIRM: ui_input(MX_IN_CONFIRM); break;
        case MX_K_HOME: ui_set_hold(0); ui_input(MX_IN_HOME); break;
        case MX_K_HOLD: mx_activity(); ui_set_hold(e.ms); break;
        case MX_K_POWEROFF: ui_set_hold(10000); power_off(); break;
        }
    }
    int64_t idle = now_ms() - s_last_activity_ms;
    bool busy = s_recording || M.assist == MX_ASSIST_THINKING;
    if (!s_dimmed && idle > IDLE_DIM_MS && !busy) {
        s_dimmed = true;
        bsp_lcd_brightness_set(12);
    } else if (s_dimmed && idle < IDLE_DIM_MS) {
        s_dimmed = false;
        bsp_lcd_brightness_set(mx_cfg()->brightness);
    }
    if (mx_cfg()->pin_set && !M.locked && idle > IDLE_LOCK_MS) {
        M.locked = true;
        ui_goto(MX_PAGE_SECURITY, false);
    }
    if (++n % 4 == 0) ui_refresh();
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
    if (!mx_camera_ready()) msg = "Camera module not responding";
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

static void rtc_to_system(void)
{
    struct tm t;
    if (bsp_rtc_get_time(&t) == ESP_OK && t.tm_year >= 124) {
        struct timeval tv = { .tv_sec = timegm(&t) };
        settimeofday(&tv, NULL);
    }
}

static void app_task(void *arg)
{
    int64_t last_sec = 0, last_bat = -100000, last_prices = -100000, last_poll = 0, last_ping = -100000, wifi_up_at = 0;
    bool rtc_written = false, welcomed = false;
    for (;;) {
        cmd_t c;
        if (xQueueReceive(s_cmdq, &c, pdMS_TO_TICKS(200)) == pdTRUE) {
            switch (c) {
            case CMD_LISTEN: do_voice(false); break;
            case CMD_JOURNAL: do_voice(true); break;
            case CMD_SNAPSHOT: do_snapshot(); break;
            case CMD_PRICES: last_prices = -100000; break;
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
            bool bat_due = t - last_bat >= 20000;
            int pct = 0;
            bool chg = false;
            if (bat_due) { last_bat = t; pct = bsp_battery_get_percent(); chg = bsp_system_is_charging(); }
            if (lvgl_port_lock(0)) {
                M.now = lt;
                M.time_valid = ns.time_valid;
                M.wifi = ns.wifi;
                M.relay_ok = ns.relay_ok;
                strlcpy(M.ip, ns.ip, sizeof M.ip);
                if (bat_due) { M.battery = pct; M.charging = chg; }
                lvgl_port_unlock();
            }
            if (ns.wifi && !wifi_up_at) wifi_up_at = t;
            if (!ns.wifi) wifi_up_at = 0;
            if (!ns.time_valid && ns.wifi && wifi_up_at && t - wifi_up_at > 20000 && mx_cfg_configured()) time_fallback_from_relay();
            if (ns.time_valid && !rtc_written && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                struct tm u;
                time_t nt = time(NULL);
                gmtime_r(&nt, &u);
                bsp_rtc_set_time(&u);
                rtc_written = true;
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
    M.battery = 100;
    journal_load();
    rtc_to_system();
    mx_activity();

    s_cmdq = xQueueCreate(6, sizeof(cmd_t));

    lvgl_port_lock(0);
    ui_init(&M, &ACTIONS);
    lv_timer_create(ui_tick, 50, NULL);
    lvgl_port_unlock();
    bsp_lcd_brightness_set(c->brightness);

    xTaskCreatePinnedToCore(app_task, "mx_app", 10240, NULL, 4, NULL, 0);
}
