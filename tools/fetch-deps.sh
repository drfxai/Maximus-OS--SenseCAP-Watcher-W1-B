#!/usr/bin/env bash
# Fetches Seeed's SenseCAP Watcher board support package (Apache-2.0) at a
# pinned commit, plus LVGL 8.4 into its components folder, then strips the
# manifest entries that would otherwise pull from a local path or the registry
# for LVGL. Idempotent.
set -euo pipefail

SEEED_REPO="https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware.git"
SEEED_SHA="8e37f7ccd3d836bb99f483e0f6e74e8252406644"
LVGL_TAG="v8.4.0"

root="$(cd "$(dirname "$0")/.." && pwd)"
dest="$root/firmware/deps/seeed"

if [ ! -d "$dest/.git" ]; then
    mkdir -p "$dest"
    git -C "$dest" init -q
    git -C "$dest" remote add origin "$SEEED_REPO"
fi
git -C "$dest" fetch -q --depth 1 origin "$SEEED_SHA"
git -C "$dest" checkout -q -f FETCH_HEAD

# Seeed vendors a patched LVGL that drags in rlottie (an empty submodule here).
# Replace it with the stock tag; nothing in Maximus uses Lottie.
rm -rf "$dest/components/lvgl"
git clone -q --depth 1 --branch "$LVGL_TAG" https://github.com/lvgl/lvgl.git "$dest/components/lvgl"
rm -rf "$dest/components/lvgl/.git"
printf 'version: "8.4.0"\ndescription: LVGL\n' > "$dest/components/lvgl/idf_component.yml"
# LVGL's objects go to PSRAM (CONFIG_LV_MEM_CUSTOM_INCLUDE names this header).
# Each is under 1 KB, so plain malloc put all of them, about 6,700 for the
# Maximus pages, in internal RAM, and the watch ran down to 3 KB free there.
# lv_mem.c includes this after lv_conf_internal.h, so the defines replace its
# malloc/realloc/free defaults.
cat > "$dest/components/lvgl/lv_mem_psram.h" <<'HDR'
#pragma once
#include <stdlib.h>
#include "esp_heap_caps.h"
#undef LV_MEM_CUSTOM_ALLOC
#undef LV_MEM_CUSTOM_REALLOC
#undef LV_MEM_CUSTOM_FREE
#define LV_MEM_CUSTOM_ALLOC(s) heap_caps_malloc_prefer((s), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_REALLOC(p, s) heap_caps_realloc_prefer((p), (s), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE(p) heap_caps_free(p)
HDR
# Upstream bug: the *_ALLOC_EXTERNAL guards in sscma_client_types.h and
# sscma_client_ops.c are mismatched between monitor_task and process_task, so
# the build breaks whichever option is set. Always declare the static task/stack
# members so every branch compiles.
hdr="$dest/components/sscma_client/include/sscma_client_types.h"
sed -i -e '/^#ifdef CONFIG_SSCMA_\(PROCESS\|MONITOR\)_TASK_STACK_ALLOC_EXTERNAL$/{N;N;N;s/^#ifdef[^\n]*\n\(.*\)\n\(.*\)\n#endif$/\1\n\2/}' "$hdr"
# BSP tweaks (idempotent): a runtime pixel clock hook so Maximus can offer a
# display-speed setting, and a 20 kHz backlight so cameras do not see banding.
bsp="$dest/components/sensecap-watcher/sensecap-watcher.c"
if ! grep -q bsp_lcd_pclk_hz "$bsp"; then
    sed -i -e 's/^static const char \*TAG = "BSP";/static const char *TAG = "BSP";\n__attribute__((weak)) uint32_t bsp_lcd_pclk_hz(void) { return DRV_LCD_PIXEL_CLK_HZ; }/' \
           -e 's/\.pclk_hz = DRV_LCD_PIXEL_CLK_HZ,/.pclk_hz = bsp_lcd_pclk_hz(),/' \
           -e 's/\.freq_hz = 5000,/.freq_hz = 20000,/' "$bsp"
    grep -q 'pclk_hz = bsp_lcd_pclk_hz' "$bsp" || { echo "BSP patch did not apply" >&2; exit 1; }
fi

# The camera chip (Himax WE2) firmware that ships with Seeed's repo. Maximus
# stores it in the flash image and can (re)install it on request.
mkdir -p "$root/firmware/spiffs"
cp "$dest/examples/sscma_client_ota/spiffs/firmware.img" "$root/firmware/spiffs/himax.img"
echo "deps ready in $dest"
