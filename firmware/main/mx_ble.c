#include <string.h>
#include "mx_ble.h"
#include "mx_net.h"

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble";
#define BLE_NAME "Maximus OS"

static bool s_on;
static uint8_t s_addr_type;

static void advertise(void);

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_on) advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (const uint8_t *)BLE_NAME;
    f.name_len = sizeof BLE_NAME - 1;
    f.name_is_complete = 1;
    if (ble_gap_adv_set_fields(&f) != 0) return;
    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    int rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "advertise failed: %d", rc);
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0) return;
    if (ble_hs_id_infer_auto(0, &s_addr_type) != 0) return;
    advertise();
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool mx_ble_on(void) { return s_on; }

bool mx_ble_set(bool on)
{
    if (on == s_on) return true;
    if (on) {
        esp_err_t r = nimble_port_init();
        if (r != ESP_OK) {
            mx_radio_failed("bluetooth", r);
            return false;
        }
        ble_hs_cfg.sync_cb = on_sync;
        ble_svc_gap_init();
        ble_svc_gatt_init();
        ble_svc_gap_device_name_set(BLE_NAME);
        s_on = true;
        nimble_port_freertos_init(host_task);
    } else {
        s_on = false;
        ble_gap_adv_stop();
        nimble_port_stop();
        nimble_port_deinit();
    }
    ESP_LOGI(TAG, "bluetooth %s", s_on ? "on" : "off");
    return true;
}
