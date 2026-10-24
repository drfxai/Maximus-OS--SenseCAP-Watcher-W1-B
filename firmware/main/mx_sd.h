#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MX_SD_ROOT "/sdcard"
#define MX_SD_DCIM "/sdcard/DCIM"

typedef enum {
    MX_SD_NONE,     /* nothing in the slot */
    MX_SD_ERROR,    /* a card is there but could not be mounted (use FAT32) */
    MX_SD_OK,
} mx_sd_state_t;

/* Watches the card slot: mounts on insert, unmounts on removal. */
void mx_sd_start(void);
mx_sd_state_t mx_sd_state(void);
bool mx_sd_ready(void);                                 /* mounted and DCIM exists */
bool mx_sd_space(uint32_t *total_mb, uint32_t *free_mb);
/* Next free file name such as /sdcard/DCIM/IMG_0007.JPG (prefix "IMG", ext "JPG"). */
bool mx_sd_next_name(const char *prefix, const char *ext, char *path, unsigned cap);
