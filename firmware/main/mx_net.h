#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MX_FW_VERSION "1.0.0"

typedef struct {
    bool wifi;
    bool relay_ok;
    bool time_valid;
    char ip[16];
} mx_net_status_t;

void mx_net_start(void);
void mx_net_reconfigure(void);               /* config changed: reconnect */
void mx_net_status(mx_net_status_t *out);
void mx_net_set_relay_ok(bool ok);
void mx_net_apply_tz(void);

/* HTTPS request to the relay. Returns the HTTP status (or -1) and a
   heap-allocated NUL-terminated body in *out (caller frees). */
int mx_http(const char *method, const char *path, const char *content_type, const void *body, size_t body_len, char **out, size_t *out_len);
