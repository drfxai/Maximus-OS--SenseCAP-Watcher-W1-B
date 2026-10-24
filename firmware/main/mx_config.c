#include "mx_config.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_memory_utils.h"
#include "esp_cpu.h"

#include "mx_app.h"
#include "mx_camera.h"
#include "mx_himax.h"
#include "mx_net.h"

static const char *TAG = "cfg";
static SemaphoreHandle_t s_save_go, s_save_done, s_save_lock;
static void save_task_start(void);
static mx_cfg_t C;

mx_cfg_t *mx_cfg(void) { return &C; }

static void defaults(void)
{
    memset(&C, 0, sizeof C);
    strlcpy(C.tz, "UTC0", sizeof C.tz);
    strlcpy(C.symbols, "BTCUSDT,ETHUSDT,SOLUSDT,BNBUSDT", sizeof C.symbols);
    C.brightness = 100;   /* full backlight: no PWM, so no bands (factory default) */
    C.bl_freq = 2;
    C.volume = 70;
    C.wifi_enabled = true;
    C.auto_time = true;
    C.timeout_idx = 1;
}

void mx_cfg_load(void)
{
    if (!s_save_lock) save_task_start();
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
    if (nvs_get_u8(h, "wifi_on", &b) == ESP_OK) C.wifi_enabled = b;
    if (nvs_get_u8(h, "bt_on", &b) == ESP_OK) C.bt_enabled = b;
    if (nvs_get_u8(h, "auto_time", &b) == ESP_OK) C.auto_time = b;
    if (nvs_get_u8(h, "cam_video", &b) == ESP_OK) C.cam_video = b;
    nvs_get_u8(h, "timeout", &C.timeout_idx);
    nvs_get_u8(h, "disp_speed", &C.disp_speed);
    nvs_get_u8(h, "draw", &C.draw_mode);
    if (nvs_get_u8(h, "web_on", &b) == ESP_OK) C.web_on = b;
    n = sizeof C.web_pass;
    nvs_get_str(h, "web_pass", C.web_pass, &n);
    /* v2.2 moved the backlight to 40 kHz and full brightness and the panel back
       to its normal speed; settings saved by older versions start from those */
    if (nvs_get_u8(h, "blf", &C.bl_freq) != ESP_OK) {
        C.bl_freq = 2;
        C.brightness = 100;
        C.disp_speed = 0;
    }
    n = sizeof C.pin_hash;
    if (nvs_get_blob(h, "pin", C.pin_hash, &n) == ESP_OK && n == sizeof C.pin_hash) C.pin_set = true;
    nvs_close(h);
    if (C.brightness < 10) C.brightness = 10;
    if (C.brightness > 100) C.brightness = 100;
    if (C.volume > 100) C.volume = 100;
    if (C.timeout_idx > 2) C.timeout_idx = 1;
    if (C.disp_speed > 1) C.disp_speed = 0;
    if (C.draw_mode > 1) C.draw_mode = 0;
    if (C.bl_freq > 2) C.bl_freq = 2;
}

static void cfg_write(void)
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
    nvs_set_u8(h, "wifi_on", C.wifi_enabled);
    nvs_set_u8(h, "bt_on", C.bt_enabled);
    nvs_set_u8(h, "auto_time", C.auto_time);
    nvs_set_u8(h, "cam_video", C.cam_video);
    nvs_set_u8(h, "timeout", C.timeout_idx);
    nvs_set_u8(h, "disp_speed", C.disp_speed);
    nvs_set_u8(h, "draw", C.draw_mode);
    nvs_set_u8(h, "web_on", C.web_on);
    nvs_set_str(h, "web_pass", C.web_pass);
    nvs_set_u8(h, "blf", C.bl_freq);
    if (C.pin_set) nvs_set_blob(h, "pin", C.pin_hash, sizeof C.pin_hash);
    else nvs_erase_key(h, "pin");
    nvs_commit(h);
    nvs_close(h);
}

/* Writing flash turns the cache off, and a task whose stack is in PSRAM
   (the display task is one) aborts the chip when it does that: every setting
   changed from the screen restarted the watch and landed on Home. Such
   callers hand the write to a small task with its stack in internal RAM. */

static void save_task(void *arg)
{
    for (;;) {
        xSemaphoreTake(s_save_go, portMAX_DELAY);
        cfg_write();
        xSemaphoreGive(s_save_done);
    }
}

void mx_cfg_save(void)
{
    if (!esp_ptr_external_ram((const void *)esp_cpu_get_sp())) {
        cfg_write();
        return;
    }
    if (!s_save_lock) {
        ESP_LOGE(TAG, "settings not saved: no save task");
        return;
    }
    xSemaphoreTake(s_save_lock, portMAX_DELAY);
    xSemaphoreTake(s_save_done, 0);
    xSemaphoreGive(s_save_go);
    if (xSemaphoreTake(s_save_done, pdMS_TO_TICKS(2000)) != pdTRUE) ESP_LOGW(TAG, "settings save is slow");
    xSemaphoreGive(s_save_lock);
}

static void save_task_start(void)
{
    s_save_go = xSemaphoreCreateBinary();
    s_save_done = xSemaphoreCreateBinary();
    s_save_lock = xSemaphoreCreateMutex();
    xTaskCreate(save_task, "mx_save", 4096, NULL, 6, NULL);
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

const char *mx_reset_name(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power";
    case ESP_RST_SW:       return "restart";
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "sleep";
    case ESP_RST_EXT:      return "reset pin";
    default:               return "other";
    }
}

static void status_line(void)
{
    mx_net_status_t st;
    mx_net_status(&st);
    printf("MAXIMUS_STATUS {\"fw\":\"%s\",\"wifi\":%s,\"ip\":\"%s\",\"relay\":%s,\"configured\":%s,\"ssid\":\"%s\",\"reset\":\"%s\"}\n",
           MX_FW_VERSION, st.wifi ? "true" : "false", st.ip, st.relay_ok ? "true" : "false",
           mx_cfg_configured() ? "true" : "false", C.ssid, mx_reset_name());
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
    } else if (strncmp(line, "MAXIMUS_HIMAX_INFO", 18) == 0) {
        size_t bytes = 0;
        bool have = mx_himax_image_present(&bytes);
        char ver[24] = "";
        mx_himax_version(ver, sizeof ver);
        printf("MAXIMUS_HIMAX {\"image\":%s,\"bytes\":%u,\"ready\":%s,\"fw\":\"%s\"}\n", have ? "true" : "false", (unsigned)bytes,
               mx_camera_ready() ? "true" : "false", ver);
    } else if (strncmp(line, "MAXIMUS_HIMAX_FLASH", 19) == 0) {
        printf("MAXIMUS_HIMAX_START\n");
        fflush(stdout);
        mx_app_chip_install(true);
    } else if (strncmp(line, "MAXIMUS_FACTORY_RESET", 21) == 0) {
        nvs_flash_erase();
        printf("MAXIMUS_OK\n");
        fflush(stdout);
        esp_restart();
    }
}

/* one line buffer per port, so bytes from the two never mix */
typedef struct {
    char buf[1024];
    size_t n;
} line_t;

static void feed(line_t *l, const uint8_t *p, int len)
{
    for (int i = 0; i < len; i++) {
        uint8_t ch = p[i];
        if (ch == '\n' || ch == '\r') {
            if (l->n) {
                l->buf[l->n] = 0;
                handle_line(l->buf);
            }
            l->n = 0;
        } else if (l->n < sizeof l->buf - 1) {
            l->buf[l->n++] = (char)ch;
        } else {
            l->n = 0; /* overlong line: drop it */
        }
    }
}

/* Reads UART0 (the USB-C socket, through the CH342 bridge) and the native
   USB port. Replies go out through printf, which reaches both. */
static void console_task(void *arg)
{
    static line_t uart_l, jtag_l;
    uint8_t chunk[128];
    for (;;) {
        int n = uart_read_bytes(UART_NUM_0, chunk, sizeof chunk, pdMS_TO_TICKS(20));
        if (n > 0) feed(&uart_l, chunk, n);
        n = usb_serial_jtag_read_bytes(chunk, sizeof chunk, pdMS_TO_TICKS(20));
        if (n > 0) feed(&jtag_l, chunk, n);
    }
}

void mx_console_start(void)
{
    xTaskCreate(console_task, "mx_console", 10240, NULL, 3, NULL);
    ESP_LOGI(TAG, "serial console ready");
}
