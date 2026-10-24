/*
 * Maximus OS - UI model.
 *
 * The UI reads everything it draws from one mx_model_t and reports everything
 * the user does through mx_actions_t. It includes nothing from ESP-IDF, so the
 * same ui.c is compiled into the firmware and into the host simulator that
 * renders screenshots (sim/).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MX_MAX_TICKERS 4
#define MX_SPARK_N 24
#define MX_MAX_NOTES 8
#define MX_REPLY_MAX 512
#define MX_SHORT 40

typedef struct {
    char sym[16];     /* BTCUSDT */
    char name[12];    /* BTC */
    char price[20];   /* "63782.41" */
    float change;     /* 24h, per cent */
    char hi[20];
    char lo[20];
    uint8_t spark[MX_SPARK_N]; /* 0-100 */
    uint8_t spark_n;
    double qty;       /* holding, 0 = none */
} mx_ticker_t;

typedef enum {
    MX_ASSIST_IDLE,
    MX_ASSIST_LISTENING,
    MX_ASSIST_THINKING,
    MX_ASSIST_REPLY,
    MX_ASSIST_ERROR,
} mx_assist_state_t;

typedef enum {
    MX_NOTE_PRICE,
    MX_NOTE_NEWS,
    MX_NOTE_SYSTEM,
    MX_NOTE_MESSAGE,
    MX_NOTE_SIGNAL,
} mx_note_kind_t;

typedef struct {
    mx_note_kind_t kind;
    char title[MX_SHORT];
    char body[64];
    time_t ts;
} mx_note_t;

typedef enum {
    MX_WIFI_OFF,          /* radio switched off in Settings */
    MX_WIFI_IDLE,         /* radio on, no network chosen */
    MX_WIFI_CONNECTING,
    MX_WIFI_CONNECTED,
    MX_WIFI_FAILED,       /* wrong password or network not found */
} mx_wifi_state_t;

#define MX_MAX_APS 12
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} mx_ap_t;

typedef enum {
    MX_SD_ST_NONE,        /* no card */
    MX_SD_ST_ERROR,       /* card present but unreadable (needs FAT32) */
    MX_SD_ST_OK,
} mx_sd_st_t;

typedef enum {
    MX_CAM_STOPPED,       /* page not open */
    MX_CAM_STARTING,
    MX_CAM_LIVE,
    MX_CAM_BUSY,          /* taking a picture or closing a video */
    MX_CAM_NOCHIP,        /* the camera chip does not answer */
} mx_cam_state_t;

typedef enum {
    MX_CHIP_UNKNOWN,
    MX_CHIP_OK,
    MX_CHIP_MISSING,      /* does not answer: probably blank */
    MX_CHIP_FLASHING,
    MX_CHIP_DONE,
    MX_CHIP_FAILED,
} mx_chip_state_t;

typedef struct {
    const char *name;     /* "London" */
    const char *rule;     /* POSIX TZ string */
} mx_tz_info_t;

typedef struct {
    /* system */
    struct tm now;            /* local time */
    bool time_valid;
    int battery;              /* 0-100 */
    bool charging;
    bool wifi;
    bool relay_ok;            /* worker reachable */
    bool telegram_paired;
    char ip[16];
    char fw_version[16];
    char pair_code[8];

    /* connectivity */
    mx_wifi_state_t wifi_state;
    char ssid[33];            /* saved network */
    bool wifi_scanning;
    mx_ap_t aps[MX_MAX_APS];
    int n_aps;
    bool bt_on;
    bool bt_failed;

    /* market */
    mx_ticker_t tk[MX_MAX_TICKERS];
    int n_tk;
    int sel_tk;               /* ticker shown on the market page */
    char price_src[12];

    /* assistant */
    mx_assist_state_t assist;
    int mic_level;            /* 0-100, live while listening */
    char heard[MX_REPLY_MAX / 2];
    char reply[MX_REPLY_MAX];
    int mode;                 /* index into the mode list */

    /* journal */
    char journal_last[MX_REPLY_MAX / 2];
    time_t journal_ts;
    int journal_count;
    bool journal_rec;

    /* notifications */
    mx_note_t notes[MX_MAX_NOTES];
    int n_notes;

    /* settings */
    int brightness;           /* 10-100 */
    int volume;               /* 0-100 */
    int timeout_idx;          /* 0 = 30 s, 1 = 1 min, 2 = 2 min */
    int disp_speed;           /* 0 = normal, 1 = turbo (applies after a restart) */
    bool auto_time;
    int tz_idx;               /* index into mx_tz, -1 = custom rule from the setup page */

    /* memory card */
    mx_sd_st_t sd_state;
    uint32_t sd_total_mb, sd_free_mb;

    /* camera */
    mx_cam_state_t cam_state;
    bool cam_video;           /* mode: false = photo, true = video */
    bool cam_rec;
    int cam_rec_s;            /* seconds recorded so far */
    char cam_msg[48];         /* short toast, cleared by the app */
    uint32_t cam_flash;       /* bumped on every photo for the shutter flash */
    const void *cam_pix;      /* RGB565 frame owned by the app */
    int cam_w, cam_h;

    /* gallery */
    int gal_count;
    int gal_idx;              /* 0 = newest */
    char gal_name[28];
    char gal_info[48];        /* size and date */
    bool gal_video;
    bool gal_playing;
    bool gal_loading;
    const void *gal_pix;      /* RGB565 picture or video frame owned by the app */
    int gal_w, gal_h;

    /* camera chip firmware */
    mx_chip_state_t chip_state;
    int chip_pct;
    char chip_ver[24];

    /* security */
    bool pin_set;
    bool locked;
} mx_model_t;

struct tm;

typedef struct {
    void (*assistant_toggle)(void);          /* start / stop listening */
    void (*journal_toggle)(void);            /* start / stop a voice note */
    void (*set_mode)(int idx);
    void (*set_brightness)(int pct);
    void (*set_volume)(int pct);
    void (*snapshot)(void);                  /* camera -> Telegram */
    void (*wifi_set_enabled)(bool on);
    void (*wifi_scan)(void);                 /* fills aps[] when done */
    void (*wifi_join)(const char *ssid, const char *pass);
    void (*wifi_forget)(void);
    void (*bt_set_enabled)(bool on);
    void (*set_timeout)(int idx);
    void (*set_disp_speed)(int idx);
    void (*set_time)(const struct tm *local);    /* hand-set date and time */
    void (*set_tz)(int idx);
    void (*set_auto_time)(bool on);
    void (*camera_open)(void);               /* page shown: start the preview */
    void (*camera_close)(void);              /* page left: stop it */
    void (*camera_shutter)(void);            /* photo, or start/stop a video */
    void (*camera_set_video)(bool video);
    void (*gallery_open)(void);              /* list the card */
    void (*gallery_show)(int idx);           /* load item idx into the viewer */
    void (*gallery_play)(void);              /* play / stop the video shown */
    bool (*gallery_delete)(int idx);
    void (*gallery_close)(void);             /* leave the viewer: free the picture */
    void (*chip_reinstall)(void);            /* write the camera chip firmware */
    void (*sd_refresh)(void);
    void (*refresh_prices)(void);
    void (*reboot)(void);
    void (*power_off)(void);
    bool (*pin_verify)(const char *pin);     /* unlock */
    bool (*pin_store)(const char *pin);      /* first-time set */
    void (*pin_clear)(void);
} mx_actions_t;

typedef struct {
    const char *key;
    const char *title;
} mx_mode_info_t;

extern const mx_mode_info_t mx_modes[4];
extern const mx_tz_info_t mx_tz[];
extern const int mx_tz_n;
extern const int mx_timeout_s[3];
