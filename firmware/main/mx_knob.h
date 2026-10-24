#pragma once

#include <stdbool.h>

typedef enum {
    MX_K_LEFT,
    MX_K_RIGHT,
    MX_K_CONFIRM,   /* released after < 5 s */
    MX_K_HOME,      /* released after >= 5 s and < 10 s */
    MX_K_POWEROFF,  /* held for 10 s */
    MX_K_HOLD,      /* progress: .ms is how long it has been held */
} mx_knob_type_t;

typedef struct {
    mx_knob_type_t type;
    int ms;
} mx_knob_evt_t;

#define MX_HOLD_HOME_MS 5000
#define MX_HOLD_OFF_MS 10000

/* Starts the rotation + button handling. Events are delivered on a queue. */
void mx_knob_start(bool invert);
bool mx_knob_poll(mx_knob_evt_t *out);      /* non-blocking */
