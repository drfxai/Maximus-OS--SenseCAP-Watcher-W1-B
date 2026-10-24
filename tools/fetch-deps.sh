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
# Upstream bug: the *_ALLOC_EXTERNAL guards in sscma_client_types.h and
# sscma_client_ops.c are mismatched between monitor_task and process_task, so
# the build breaks whichever option is set. Always declare the static task/stack
# members so every branch compiles.
hdr="$dest/components/sscma_client/include/sscma_client_types.h"
sed -i -e '/^#ifdef CONFIG_SSCMA_\(PROCESS\|MONITOR\)_TASK_STACK_ALLOC_EXTERNAL$/{N;N;N;s/^#ifdef[^\n]*\n\(.*\)\n\(.*\)\n#endif$/\1\n\2/}' "$hdr"
echo "deps ready in $dest"
