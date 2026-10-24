# The Seeed board support package (and LVGL, the knob driver, ...) comes in
# through idf_component.yml; run tools/fetch-deps.sh before building.
idf_component_register(
    SRCS
        "main.c"
        "mx_app.c"
        "mx_audio.c"
        "mx_camera.c"
        "mx_config.c"
        "mx_knob.c"
        "mx_net.c"
        "mx_relay.c"
        "mx_rgb.c"
        "ui/ui.c"
        "ui/assets.c"
    INCLUDE_DIRS "." "ui"
    PRIV_REQUIRES sensecap-watcher esp_lvgl_port lvgl sscma_client nvs_flash esp_wifi esp_netif esp_event esp_http_client esp-tls mbedtls json
                  spiffs driver esp_timer esp_hw_support esp_system vfs
)
