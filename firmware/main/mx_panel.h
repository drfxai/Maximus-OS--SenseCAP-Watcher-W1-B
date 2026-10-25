#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t draws;         /* pieces of picture sent since boot */
    uint32_t errors;        /* of those, how many the driver refused */
    esp_err_t last_err;
    uint32_t idle_s;        /* seconds since the last one */
} mx_panel_stats_t;

void mx_panel_stats(mx_panel_stats_t *out);

/* Resends the picture whenever the panel has had nothing for 5 s. */
void mx_panel_keepalive_start(void);

/* Sends the panel its power-on setup again and redraws everything. */
bool mx_panel_restart(void);
