#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Keeps the recent log in a ring buffer for the web console. The buffer lives
 * in PSRAM that a restart does not clear, so after a crash the log from just
 * before it is still there ("previous run"). The crash itself (task, address,
 * backtrace) comes from the core dump partition.
 */
void mx_log_init(void);                         /* call first thing in app_main */

/* Copies log bytes from absolute position *pos into dst; moves *pos on.
   Returns the bytes copied. A *pos older than the ring jumps to the oldest. */
size_t mx_log_read(uint32_t *pos, char *dst, size_t cap);
uint32_t mx_log_head(void);

/* The previous run's log tail, or NULL (power-on). */
const char *mx_log_prev(size_t *len);

/* A one-paragraph description of the last crash from the core dump, or NULL. */
const char *mx_log_crash(void);
