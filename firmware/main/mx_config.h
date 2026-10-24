#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char ssid[33];
    char pass[65];
    char relay[96];     /* https://maximus-os.<you>.workers.dev, no trailing slash */
    char key[72];       /* DEVICE_KEY shared with the Worker */
    char tz[48];        /* POSIX TZ rule, e.g. "GMT0BST,M3.5.0/1,M10.5.0" */
    char symbols[64];   /* BTCUSDT,ETHUSDT,... (max 4 shown) */
    char holdings[96];  /* BTC=0.12,ETH=2.5 */
    uint8_t brightness; /* 10-100 */
    uint8_t volume;     /* 0-100 */
    uint8_t mode;       /* AI mode index */
    bool knob_invert;   /* swap rotation direction */
    bool wifi_enabled;  /* radio switch in Settings */
    bool bt_enabled;    /* BLE advertising switch in Settings */
    bool auto_time;     /* follow NTP; off keeps the time set by hand */
    uint8_t timeout_idx;/* screen timeout: 0 = 30 s, 1 = 1 min, 2 = 2 min */
    uint8_t disp_speed; /* 0 = normal (40 MHz pixel clock), 1 = turbo (80 MHz) */
    bool web_on;        /* web console hotspot */
    char web_pass[12];  /* its WPA2 password, made up on first use */
    uint8_t draw_mode;  /* 0 = factory full-frame buffers in PSRAM, 1 = 32-row buffers in internal RAM */
    uint8_t bl_freq;    /* backlight PWM: 0 = 5 kHz (factory), 1 = 20 kHz, 2 = 40 kHz (default) */
    bool cam_video;     /* camera opens in video mode */
    bool pin_set;
    uint8_t pin_hash[32];
} mx_cfg_t;

void mx_cfg_load(void);
void mx_cfg_save(void);
mx_cfg_t *mx_cfg(void);
bool mx_cfg_configured(void);              /* Wi-Fi and relay both set */

bool mx_pin_store(const char *pin);
bool mx_pin_verify(const char *pin);
void mx_pin_clear(void);

/* Serial provisioning for the web installer (lines on the USB console). */
void mx_console_start(void);

/* why the chip last started: power, restart, crash, watchdog, brownout, ... */
const char *mx_reset_name(void);
