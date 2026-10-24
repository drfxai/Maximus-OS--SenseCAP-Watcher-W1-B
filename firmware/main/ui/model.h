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

    /* security */
    bool pin_set;
    bool locked;
} mx_model_t;

typedef struct {
    void (*assistant_toggle)(void);          /* start / stop listening */
    void (*journal_toggle)(void);            /* start / stop a voice note */
    void (*set_mode)(int idx);
    void (*set_brightness)(int pct);
    void (*set_volume)(int pct);
    void (*snapshot)(void);                  /* camera -> Telegram */
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
