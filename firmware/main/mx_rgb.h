#pragma once

#include <stdint.h>

typedef enum {
    MX_RGB_OFF,
    MX_RGB_LISTENING,   /* green  */
    MX_RGB_THINKING,    /* blue   */
    MX_RGB_PROCESSING,  /* purple */
    MX_RGB_WARNING,     /* orange */
    MX_RGB_CRITICAL,    /* red    */
} mx_rgb_state_t;

void mx_rgb_start(void);
void mx_rgb_set(mx_rgb_state_t s);
uint32_t mx_rgb_color(mx_rgb_state_t s);
void mx_rgb_flash(mx_rgb_state_t s, int ms);   /* show s for ms, then off */
