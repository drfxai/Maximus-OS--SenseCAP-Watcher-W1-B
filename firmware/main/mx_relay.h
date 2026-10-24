#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui/model.h"

typedef struct {
    bool ok;
    bool paired;
    char pair_code[8];
} mx_ping_t;

typedef struct {
    char symbol[16];
    char side[8];
    char entry[20], sl[20], tp1[20];
    char note[96];
    uint64_t ts;
    bool critical;
} mx_signal_t;

bool mx_relay_ping(mx_ping_t *out);
bool mx_relay_crypto(mx_model_t *m);                     /* fills tickers */
/* Fetches queued Telegram messages and the latest signal. Returns the number of
   notes appended to the model; *sig is filled when a new signal arrived. */
int mx_relay_poll(mx_model_t *m, uint64_t *last_sig_ts, mx_signal_t *sig, bool *got_sig);
bool mx_relay_voice(const int16_t *pcm, size_t samples, bool journal, const char *mode, char *heard, size_t heard_cap, char *reply, size_t reply_cap);
bool mx_relay_chat(const char *text, const char *mode, char *reply, size_t reply_cap);
bool mx_relay_notify(const char *text);
bool mx_relay_photo(const uint8_t *jpeg, size_t len, const char *caption);
void mx_relay_apply_holdings(mx_model_t *m);
