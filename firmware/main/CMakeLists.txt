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
        "mx_log.c"
        "mx_net.c"
        "mx_panel.c"
        "mx_relay.c"
        "mx_rgb.c"
        "mx_sd.c"
        "mx_web.c"
        "ui/ui.c"
        "ui/ui_camera.c"
        "ui/ui_settings.c"
        "ui/ui_shell.c"
        "ui/ui_kb.c"
        "ui/assets.c"
        "ui/lv_quiet.c"
    INCLUDE_DIRS "." "ui"
    PRIV_REQUIRES sensecap-watcher esp_lvgl_port esp_lcd lvgl sscma_client esp_jpeg_simd bt fatfs sdmmc nvs_flash esp_wifi esp_netif esp_event esp_http_client esp-tls mbedtls json
                  spiffs driver esp_timer esp_hw_support esp_system vfs esp_http_server espcoredump
)

# LVGL setters that skip writes which change nothing (ui/lv_quiet.c): an idle
# page then sends nothing to the panel, whose transfers show as faint streaks.
foreach(fn lv_obj_set_local_style_prop lv_label_set_text lv_label_set_text_fmt
           lv_obj_add_flag lv_obj_clear_flag lv_chart_set_value_by_id)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${fn}")
endforeach()

# Counts what is sent to the panel (mx_panel.c) for the status line.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_lcd_panel_draw_bitmap")

# The camera chip's firmware travels inside the flash image: everything in
# firmware/spiffs (himax.img, put there by tools/fetch-deps.sh) becomes the
# "storage" partition, so one flash writes the ESP32 and stages the camera chip.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../spiffs)
    spiffs_create_partition_image(storage ${CMAKE_CURRENT_SOURCE_DIR}/../spiffs FLASH_IN_PROJECT)
endif()
