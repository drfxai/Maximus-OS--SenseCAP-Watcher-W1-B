#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sensecap-watcher.h"

/*
 * The Watcher's camera is wired to a Himax WE2 chip, not to the ESP32-S3. The
 * ESP talks to it over SPI with the SSCMA protocol; a frame arrives as a base64
 * JPEG inside an event. This file is the thin layer that turns that into a
 * stream of JPEG frames and into single still pictures.
 */

#define MX_CAM_PREVIEW_OPT 1        /* sensor option 1 = 416 x 416 */

typedef struct {
    uint8_t *jpeg;                  /* PSRAM; the receiver frees it with free() */
    size_t len;
} mx_frame_t;

void mx_camera_init(void);
bool mx_camera_ready(void);                         /* the chip answered */
bool mx_camera_probing(void);                       /* still waiting for it at boot */
sscma_client_handle_t mx_camera_client(void);

/* Continuous preview. Both block for up to a few seconds; call from a task. */
bool mx_camera_stream(bool on);
bool mx_camera_streaming(void);
bool mx_camera_next(mx_frame_t *f, int timeout_ms);

/* One high resolution picture (640 x 480 where the chip allows). Stops the stream. */
bool mx_camera_still(uint8_t **jpeg, size_t *len);

/* Convenience used by the Telegram snapshot. */
bool mx_camera_snapshot(uint8_t **jpeg, size_t *len);

/* Exclusive access, for flashing the chip. */
void mx_camera_lock(void);
void mx_camera_unlock(void);
