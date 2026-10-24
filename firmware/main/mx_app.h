#pragma once

void mx_app_start(void);

#include <stdbool.h>

/* Writes the camera chip firmware (the web installer's second stage and the
   Settings > Camera chip button). With console set it reports progress as
   MAXIMUS_HIMAX_* lines. Restarts the watch on success. */
bool mx_app_chip_install(bool console);
