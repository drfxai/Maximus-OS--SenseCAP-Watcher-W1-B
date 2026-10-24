#include "mx_config.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mx_net.h"

static const char *TAG = "cfg";
static mx_cfg_t C;

mx_cfg_t *mx_cfg(void) { return &C; }

static void defaults(void)
{
    memset(&C, 0, sizeof C);
    strlcpy(C.tz, "UTC0", sizeof C.tz);
    strlcpy(C.symbols, "BTCUSDT,ETHUSDT,SOLUSDT,BNBUSDT", sizeof C.symbols);
    C.brightness = 80;
    C.volume = 70;
}

void mx_cfg_load(void)
{
    defaults();
    nvs_handle_t h;
    if (nvs_open("maximus", NVS_READONLY, &h) != ESP_OK) return;
    size_t n;
#define GETS(k, f) n = sizeof C.f; nvs_get_str(h, k, C.f, &n)
    GETS("ssid", ssid);
    GETS("pass", pass);
    GETS("relay", relay);
    GETS("key", key);
    GETS("tz", tz);
    GETS("symbols", symbols);
    GETS("holdings", holdings);
#undef GETS
    nvs_get_u8(h, "bright", &C.brightness);
    nvs_get_u8(h, "volume", &C.volume);
    nvs_get_u8(h, "mode", &C.mode);
    uint8_t b = 0;
    if (nvs_get_u8(h, "invert", &b) == ESP_OK) C.knob_invert = b;
    n = sizeof C.pin_hash;
    if (nvs_get_blob(h, "pin", C.pin_hash, &n) == ESP_OK && n == sizeof C.pin_hash) C.pin_set = true;
    nvs_close(h);
    if (C.brightness < 10) C.brightness = 10;
    if (C.brightness > 100) C.brightness = 100;
    if (C.volume > 100) C.volume = 100;
}

void mx_cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("maximus", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", C.ssid);
    nvs_set_str(h, "pass", C.pass);
    nvs_set_str(h, "relay", C.relay);
    nvs_set_str(h, "key", C.key);
    nvs_set_str(h, "tz", C.tz);
    nvs_set_str(h, "symbols", C.symbols);
    nvs_set_str(h, "holdings", C.holdings);
    nvs_set_u8(h, "bright", C.brightness);
    nvs_set_u8(h, "volume", C.volume);
    nvs_set_u8(h, "mode", C.mode);
    nvs_set_u8(h, "invert", C.knob_invert);
    if (C.pin_set) nvs_set_blob(h, "pin", C.pin_hash, sizeof C.pin_hash);
    else nvs_erase_key(h, "pin");
    nvs_commit(h);
    nvs_close(h);
}

bool mx_cfg_configured(void)
{
    return C.ssid[0] && C.relay[0] && C.key[0];
}

/* ---- PIN ----------------------------------------------------------- */

static void pin_digest(const char *pin, uint8_t out[32])
{
    /* The salt is per device, so identical PINs hash differently. It stops
       casual lookups; a 4-digit PIN is a lock screen, not a vault. */
    uint8_t mac[8] = { 0 };
    extern esp_err_t esp_efuse_mac_get_default(uint8_t *mac);
    esp_efuse_mac_get_default(mac);
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char *)"maximus-pin", 11);
    mbedtls_sha256_update(&ctx, mac, 6);
    mbedtls_sha256_update(&ctx, (const unsigned char *)pin, strlen(pin));
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

bool mx_pin_store(const char *pin)
{
    pin_digest(pin, C.pin_hash);
    C.pin_set = true;
    mx_cfg_save();
    return true;
}

bool mx_pin_verify(const char *pin)
{
    uint8_t d[32];
    pin_digest(pin, d);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= d[i] ^ C.pin_hash[i];
    return C.pin_set && diff == 0;
}

void mx_pin_clear(void)
{
    C.pin_set = false;
    memset(C.pin_hash, 0, sizeof C.pin_hash);
    mx_cfg_save();
}

/* ---- serial console ------------------------------------------------ */

static void sets(char *dst, size_t cap, cJSON *j, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(j, key);
    if (cJSON_IsString(v) && v->valuestring) strlcpy(dst, v->valuestring, cap);
}

static void status_line(void)
{
    mx_net_status_t st;
    mx_net_status(&st);
    printf("MAXIMUS_STATUS {\"fw\":\"%s\",\"wifi\":%s,\"ip\":\"%s\",\"relay\":%s,\"configured\":%s,\"ssid\":\"%s\"}\n",
           MX_FW_VERSION, st.wifi ? "true" : "false", st.ip, st.relay_ok ? "true" : "false",
           mx_cfg_configured() ? "true" : "false", C.ssid);
}

static void handle_line(char *line)
{
    if (strncmp(line, "MAXIMUS_HELLO", 13) == 0) {
        printf("MAXIMUS_READY %s\n", MX_FW_VERSION);
        status_line();
    } else if (strncmp(line, "MAXIMUS_STATUS", 14) == 0) {
        status_line();
    } else if (strncmp(line, "MAXIMUS_CFG ", 12) == 0) {
        cJSON *j = cJSON_Parse(line + 12);
        if (!j) { printf("MAXIMUS_ERR bad json\n"); return; }
        sets(C.ssid, sizeof C.ssid, j, "ssid");
        sets(C.pass, sizeof C.pass, j, "pass");
        sets(C.relay, sizeof C.relay, j, "relay");
        sets(C.key, sizeof C.key, j, "key");
        sets(C.tz, sizeof C.tz, j, "tz");
        sets(C.symbols, sizeof C.symbols, j, "symbols");
        sets(C.holdings, sizeof C.holdings, j, "holdings");
        cJSON *inv = cJSON_GetObjectItem(j, "knob_invert");
        if (cJSON_IsBool(inv)) C.knob_invert = cJSON_IsTrue(inv);
        cJSON_Delete(j);
        /* normalise the relay URL: https only, no trailing slash */
        size_t L = strlen(C.relay);
        while (L && C.relay[L - 1] == '/') C.relay[--L] = 0;
        if (C.relay[0] && strncmp(C.relay, "https://", 8) != 0) {
            printf("MAXIMUS_ERR relay must be an https:// URL\n");
            return;
        }
        mx_cfg_save();
        printf("MAXIMUS_OK\n");
        mx_net_reconfigure();
    } else if (strncmp(line, "MAXIMUS_FACTORY_RESET", 21) == 0) {
        nvs_flash_erase();
        printf("MAXIMUS_OK\n");
        fflush(stdout);
        esp_restart();
    }
}

static void console_task(void *arg)
{
    char buf[1024];
    for (;;) {
        if (!fgets(buf, sizeof buf, stdin)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        if (n) handle_line(buf);
    }
}

void mx_console_start(void)
{
    xTaskCreate(console_task, "mx_console", 6144, NULL, 3, NULL);
    ESP_LOGI(TAG, "serial console ready");
}
