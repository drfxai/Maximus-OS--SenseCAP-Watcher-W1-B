#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Installs firmware on the Himax WE2 camera chip. The image is Seeed's stock
 * SSCMA firmware (Apache-2.0); it travels in the "storage" flash partition as
 * /spiffs/himax.img and is written to the chip over the same SPI link the
 * camera uses.
 */
#define MX_HIMAX_IMAGE "/spiffs/himax.img"

typedef void (*mx_himax_progress_t)(int percent);

bool mx_himax_image_present(size_t *bytes);
/* Blocking, about a minute. Reboot afterwards so the camera starts clean. */
bool mx_himax_flash(mx_himax_progress_t progress);
/* Firmware version string reported by the chip, "" if it does not answer. */
bool mx_himax_version(char *out, size_t cap);
