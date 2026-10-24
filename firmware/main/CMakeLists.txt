# The Seeed board support package (and LVGL, the knob driver, ...) comes in
# through idf_component.yml; run tools/fetch-deps.sh before building.
idf_component_register(
    SRCS
        "main.c"
        "mx_app.c"
        "mx_audio.c"
        "mx_avi.c"
        "mx_ble.c"
        "mx_camera.c"
        "mx_config.c"
        "mx_himax.c"
        "mx_jpeg.c"
        "mx_media.c"
        "mx_knob.c"
        "mx_net.c"
        "mx_relay.c"
        "mx_rgb.c"
        "mx_sd.c"
        "ui/ui.c"
        "ui/ui_camera.c"
        "ui/ui_settings.c"
        "ui/ui_shell.c"
        "ui/ui_kb.c"
        "ui/assets.c"
    INCLUDE_DIRS "." "ui"
    PRIV_REQUIRES sensecap-watcher esp_lvgl_port lvgl sscma_client esp_jpeg_simd bt fatfs sdmmc nvs_flash esp_wifi esp_netif esp_event esp_http_client esp-tls mbedtls json
                  spiffs driver esp_timer esp_hw_support esp_system vfs
)

# The camera chip's firmware travels inside the flash image: everything in
# firmware/spiffs (himax.img, put there by tools/fetch-deps.sh) becomes the
# "storage" partition, so one flash writes the ESP32 and stages the camera chip.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../spiffs)
    spiffs_create_partition_image(storage ${CMAKE_CURRENT_SOURCE_DIR}/../spiffs FLASH_IN_PROJECT)
endif()
