#include "mx_relay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include <ctype.h>
#include <strings.h>
#include "esp_log.h"

#include "mx_config.h"
#include "mx_net.h"

static const char *TAG = "relay";

static void copy_str(char *dst, size_t cap, cJSON *j, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(j, key);
    if (cJSON_IsString(v) && v->valuestring) strlcpy(dst, v->valuestring, cap);
    else if (cJSON_IsNumber(v)) snprintf(dst, cap, "%g", v->valuedouble);
}

bool mx_relay_ping(mx_ping_t *out)
{
    char *body = NULL;
    int st = mx_http("GET", "/v1/ping", NULL, NULL, 0, &body, NULL);
    memset(out, 0, sizeof *out);
    if (st == 200 && body) {
        cJSON *j = cJSON_Parse(body);
        if (j) {
            out->ok = true;
            cJSON *t = cJSON_GetObjectItem(j, "telegram");
            out->paired = cJSON_IsTrue(t);
            copy_str(out->pair_code, sizeof out->pair_code, j, "pair_code");
            cJSON_Delete(j);
        }
    }
    free(body);
    mx_net_set_relay_ok(out->ok);
    return out->ok;
}

void mx_relay_apply_holdings(mx_model_t *m)
{
    /* "BTC=0.12,ETH=2.5" */
    for (int i = 0; i < m->n_tk; i++) m->tk[i].qty = 0;
    char buf[96];
    strlcpy(buf, mx_cfg()->holdings, sizeof buf);
    for (char *tok = strtok(buf, ", "); tok; tok = strtok(NULL, ", ")) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        for (int i = 0; i < m->n_tk; i++) {
            if (strcasecmp(m->tk[i].name, tok) == 0) m->tk[i].qty = atof(eq + 1);
        }
    }
}

bool mx_relay_crypto(mx_model_t *m)
{
    char path[160];
    snprintf(path, sizeof path, "/v1/crypto?symbols=%s", mx_cfg()->symbols);
    char *body = NULL;
    int st = mx_http("GET", path, NULL, NULL, 0, &body, NULL);
    bool ok = false;
    if (st == 200 && body) {
        cJSON *j = cJSON_Parse(body);
        cJSON *arr = j ? cJSON_GetObjectItem(j, "tickers") : NULL;
        if (cJSON_IsArray(arr)) {
            int n = 0;
            cJSON *o;
            cJSON_ArrayForEach(o, arr) {
                if (n >= MX_MAX_TICKERS) break;
                mx_ticker_t *t = &m->tk[n];
                memset(t, 0, sizeof *t);
                copy_str(t->sym, sizeof t->sym, o, "s");
                copy_str(t->name, sizeof t->name, o, "d");
                copy_str(t->price, sizeof t->price, o, "p");
                copy_str(t->hi, sizeof t->hi, o, "h");
                copy_str(t->lo, sizeof t->lo, o, "l");
                cJSON *c = cJSON_GetObjectItem(o, "c");
                t->change = cJSON_IsNumber(c) ? (float)c->valuedouble : 0;
                cJSON *k = cJSON_GetObjectItem(o, "k");
                cJSON *v;
                cJSON_ArrayForEach(v, k) {
                    if (t->spark_n >= MX_SPARK_N) break;
                    int x = cJSON_IsNumber(v) ? v->valueint : 0;
                    t->spark[t->spark_n++] = (uint8_t)(x < 0 ? 0 : x > 100 ? 100 : x);
                }
                if (t->name[0] && t->price[0]) n++;
            }
            if (n) {
                m->n_tk = n;
                cJSON *src = cJSON_GetObjectItem(j, "src");
                if (cJSON_IsString(src)) {
                    strlcpy(m->price_src, src->valuestring, sizeof m->price_src);
                    for (char *p = m->price_src; *p; p++) *p = (char)toupper((unsigned char)*p);
                }
                mx_relay_apply_holdings(m);
                ok = true;
            }
        }
        cJSON_Delete(j);
    } else {
        ESP_LOGW(TAG, "crypto status %d", st);
    }
    free(body);
    return ok;
}

static void note_add(mx_model_t *m, mx_note_kind_t kind, const char *title, const char *body, time_t ts)
{
    /* newest first */
    memmove(&m->notes[1], &m->notes[0], sizeof(mx_note_t) * (MX_MAX_NOTES - 1));
    mx_note_t *n = &m->notes[0];
    memset(n, 0, sizeof *n);
    n->kind = kind;
    strlcpy(n->title, title, sizeof n->title);
    strlcpy(n->body, body, sizeof n->body);
    n->ts = ts;
    if (m->n_notes < MX_MAX_NOTES) m->n_notes++;
}

int mx_relay_poll(mx_model_t *m, uint64_t *last_sig_ts, mx_signal_t *sig, bool *got_sig)
{
    char path[64];
    snprintf(path, sizeof path, "/v1/poll?sig=%llu", (unsigned long long)*last_sig_ts);
    char *body = NULL;
    int st = mx_http("GET", path, NULL, NULL, 0, &body, NULL);
    int added = 0;
    *got_sig = false;
    if (st == 200 && body) {
        cJSON *j = cJSON_Parse(body);
        if (j) {
            cJSON *inbox = cJSON_GetObjectItem(j, "inbox"), *it;
            cJSON_ArrayForEach(it, inbox) {
                char kind[12] = "", from[MX_SHORT] = "", text[64] = "";
                copy_str(kind, sizeof kind, it, "kind");
                copy_str(from, sizeof from, it, "from");
                copy_str(text, sizeof text, it, "text");
                cJSON *ts = cJSON_GetObjectItem(it, "ts");
                time_t t = cJSON_IsNumber(ts) ? (time_t)(ts->valuedouble / 1000.0) : time(NULL);
                note_add(m, !strcmp(kind, "signal") ? MX_NOTE_SIGNAL : MX_NOTE_MESSAGE, from[0] ? from : "Telegram", text, t);
                added++;
            }
            cJSON *s = cJSON_GetObjectItem(j, "signal");
            if (cJSON_IsObject(s)) {
                memset(sig, 0, sizeof *sig);
                copy_str(sig->symbol, sizeof sig->symbol, s, "symbol");
                copy_str(sig->side, sizeof sig->side, s, "side");
                copy_str(sig->entry, sizeof sig->entry, s, "entry");
                copy_str(sig->sl, sizeof sig->sl, s, "sl");
                copy_str(sig->tp1, sizeof sig->tp1, s, "tp1");
                copy_str(sig->note, sizeof sig->note, s, "note");
                cJSON *ts = cJSON_GetObjectItem(s, "ts");
                sig->ts = cJSON_IsNumber(ts) ? (uint64_t)ts->valuedouble : 0;
                char low[96];
                strlcpy(low, sig->note, sizeof low);
                for (char *p = low; *p; p++) *p = (char)tolower((unsigned char)*p);
                sig->critical = strstr(low, "stop") || strstr(low, "sl hit") || !strcmp(sig->side, "STOP");
                if (sig->ts > *last_sig_ts) {
                    *last_sig_ts = sig->ts;
                    *got_sig = true;
                }
            }
            cJSON_Delete(j);
            mx_net_set_relay_ok(true);
        }
    } else if (st < 0 || st >= 500) {
        mx_net_set_relay_ok(false);
    }
    free(body);
    return added;
}

static bool read_reply(char *body, char *heard, size_t heard_cap, char *reply, size_t reply_cap)
{
    cJSON *j = cJSON_Parse(body);
    if (!j) return false;
    if (heard) { heard[0] = 0; copy_str(heard, heard_cap, j, "heard"); }
    if (reply) { reply[0] = 0; copy_str(reply, reply_cap, j, "reply"); }
    bool ok = !reply || reply[0];
    cJSON_Delete(j);
    return ok;
}

bool mx_relay_voice(const int16_t *pcm, size_t samples, bool journal, const char *mode, char *heard, size_t heard_cap, char *reply, size_t reply_cap)
{
    char path[96];
    snprintf(path, sizeof path, "/v1/voice?mode=%s%s", mode ? mode : "general", journal ? "&journal=1" : "");
    char *body = NULL;
    int st = mx_http("POST", path, "application/octet-stream", pcm, samples * 2, &body, NULL);
    bool ok = st == 200 && body && read_reply(body, heard, heard_cap, reply, reply_cap);
    if (st < 0) mx_net_set_relay_ok(false);
    free(body);
    return ok;
}

bool mx_relay_chat(const char *text, const char *mode, char *reply, size_t reply_cap)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "text", text);
    cJSON_AddStringToObject(j, "mode", mode ? mode : "general");
    char *payload = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    char *body = NULL;
    int st = mx_http("POST", "/v1/chat", "application/json", payload, strlen(payload), &body, NULL);
    free(payload);
    bool ok = st == 200 && body && read_reply(body, NULL, 0, reply, reply_cap);
    free(body);
    return ok;
}

bool mx_relay_notify(const char *text)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "text", text);
    char *payload = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    int st = mx_http("POST", "/v1/notify", "application/json", payload, strlen(payload), NULL, NULL);
    free(payload);
    return st == 200;
}

bool mx_relay_photo(const uint8_t *jpeg, size_t len, const char *caption)
{
    char path[96];
    snprintf(path, sizeof path, "/v1/photo?caption=%s", caption ? caption : "Maximus");
    for (char *p = path; *p; p++) if (*p == ' ') *p = '+';
    return mx_http("POST", path, "image/jpeg", jpeg, len, NULL, NULL) == 200;
}
