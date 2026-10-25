#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MX_FW_VERSION "2.3.4"

#include "ui/model.h"

typedef struct {
    bool wifi;
    bool relay_ok;
    bool time_valid;
    char ip[16];
    mx_wifi_state_t state;       /* off / idle / connecting / connected / failed */
    char ssid[33];               /* saved network */
} mx_net_status_t;

void mx_net_start(void);
void mx_net_reconfigure(void);               /* config changed: reconnect */
void mx_net_status(mx_net_status_t *out);
void mx_net_set_relay_ok(bool ok);
void mx_net_apply_tz(void);

/* Settings > Wi-Fi */
void mx_wifi_enable(bool on);                        /* radio switch, remembered */
bool mx_wifi_enabled(void);
int mx_wifi_scan(mx_ap_t *out, int max);             /* blocking, a few seconds */
void mx_wifi_join(const char *ssid, const char *pass); /* remember and connect */
void mx_wifi_forget(void);
void mx_net_set_auto_time(bool on);                  /* NTP on/off */
bool mx_net_ap(bool on, const char *ssid, const char *pass); /* hotspot for the web console */

/* The last Wi-Fi or Bluetooth start that failed, as "what: error", for the
   installer's status line; "" when none has. Each failure is logged with the
   free internal RAM. */
extern char mx_radio_err[48];
void mx_radio_failed(const char *what, int err);

/* HTTPS request to the relay. Returns the HTTP status (or -1) and a
   heap-allocated NUL-terminated body in *out (caller frees). */
int mx_http(const char *method, const char *path, const char *content_type, const void *body, size_t body_len, char **out, size_t *out_len);
