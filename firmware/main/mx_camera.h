#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mx_camera_init(void);
bool mx_camera_ready(void);
/* Grabs one JPEG from the camera module. Caller frees *jpeg. */
bool mx_camera_snapshot(uint8_t **jpeg, size_t *len);
