#include "mx_net.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "mx_config.h"

static const char *TAG = "net";
static volatile bool s_wifi, s_relay_ok, s_started;
static char s_ip[16];
static esp_netif_t *s_netif;
static int s_retry;
static esp_timer_handle_t s_retry_timer;

static void retry_cb(void *arg)
{
    if (mx_cfg()->ssid[0]) esp_wifi_connect();
}

void mx_net_apply_tz(void)
{
    setenv("TZ", mx_cfg()->tz[0] ? mx_cfg()->tz : "UTC0", 1);
    tzset();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (mx_cfg()->ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi = false;
        s_ip[0] = 0;
        if (mx_cfg()->ssid[0]) {
            /* back off to 30 s so a wrong password does not hammer the AP */
            int delay = s_retry < 5 ? 1000 * (1 << s_retry) : 30000;
            s_retry++;
            esp_timer_stop(s_retry_timer);
            esp_timer_start_once(s_retry_timer, (uint64_t)delay * 1000);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
        s_wifi = true;
        s_retry = 0;
        ESP_LOGI(TAG, "got ip %s", s_ip);
    }
}

static void sntp_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "time synced");
}

void mx_net_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t el = esp_event_loop_create_default();
    if (el != ESP_OK && el != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(el);
    const esp_timer_create_args_t ta = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_retry_timer));
    s_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, mx_cfg()->ssid, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, mx_cfg()->pass, sizeof wc.sta.password);
    wc.sta.threshold.authmode = mx_cfg()->pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    mx_net_apply_tz();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_set_time_sync_notification_cb(sntp_cb);
    esp_sntp_init();
    s_started = true;
}

void mx_net_reconfigure(void)
{
    if (!s_started) return;
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, mx_cfg()->ssid, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, mx_cfg()->pass, sizeof wc.sta.password);
    wc.sta.threshold.authmode = mx_cfg()->pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    esp_timer_stop(s_retry_timer);
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    mx_net_apply_tz();
    s_retry = 0;
    if (wc.sta.ssid[0]) esp_wifi_connect();
}

void mx_net_status(mx_net_status_t *out)
{
    out->wifi = s_wifi;
    out->relay_ok = s_relay_ok;
    out->time_valid = time(NULL) > 1700000000;
    strlcpy(out->ip, s_ip, sizeof out->ip);
}

void mx_net_set_relay_ok(bool ok) { s_relay_ok = ok; }

/* ---- HTTP ---------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t len, cap;
} resp_t;

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    resp_t *r = (resp_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && r) {
        if (r->len + e->data_len + 1 > r->cap) {
            size_t ncap = (r->cap ? r->cap * 2 : 2048);
            while (ncap < r->len + e->data_len + 1) ncap *= 2;
            if (ncap > 256 * 1024) return ESP_FAIL;
            char *nb = heap_caps_realloc(r->buf, ncap, MALLOC_CAP_SPIRAM);
            if (!nb) return ESP_FAIL;
            r->buf = nb;
            r->cap = ncap;
        }
        memcpy(r->buf + r->len, e->data, e->data_len);
        r->len += e->data_len;
        r->buf[r->len] = 0;
    }
    return ESP_OK;
}

int mx_http(const char *method, const char *path, const char *content_type, const void *body, size_t body_len, char **out, size_t *out_len)
{
    mx_cfg_t *c = mx_cfg();
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!s_wifi || !c->relay[0]) return -1;

    char url[256];
    snprintf(url, sizeof url, "%s%s", c->relay, path);
    resp_t r = { 0 };
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = http_evt,
        .user_data = &r,
        .timeout_ms = 25000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return -1;
    esp_http_client_set_method(h, !strcmp(method, "POST") ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    char auth[100];
    snprintf(auth, sizeof auth, "Bearer %s", c->key);
    esp_http_client_set_header(h, "Authorization", auth);
    esp_http_client_set_header(h, "User-Agent", "MaximusOS/" MX_FW_VERSION);
    if (body) {
        esp_http_client_set_header(h, "Content-Type", content_type ? content_type : "application/json");
        esp_http_client_set_post_field(h, body, (int)body_len);
    }
    esp_err_t err = esp_http_client_perform(h);
    int status = err == ESP_OK ? esp_http_client_get_status_code(h) : -1;
    esp_http_client_cleanup(h);
    if (err != ESP_OK) ESP_LOGW(TAG, "%s %s: %s", method, path, esp_err_to_name(err));
    if (out) *out = r.buf; else free(r.buf);
    if (out_len) *out_len = r.len;
    return status;
}
