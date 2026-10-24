#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MX_RGB_OFF,
    MX_RGB_LISTENING,   /* blue, quick breathing */
    MX_RGB_THINKING,    /* blue, slow breathing */
    MX_RGB_PROCESSING,  /* purple */
    MX_RGB_WARNING,     /* orange */
    MX_RGB_CRITICAL,    /* red    */
} mx_rgb_state_t;

/*
 * What the ring shows, highest priority first:
 *   an alert flash (mx_rgb_flash), an assistant activity (mx_rgb_set),
 *   a notification (three blue blinks on battery, a blue glow on the charger),
 *   the Assistant page being open (soft blue),
 *   power: green charging, blue full, red under 30 % on battery, else off.
 */
void mx_rgb_start(void);
void mx_rgb_set(mx_rgb_state_t s);             /* MX_RGB_OFF ends the activity */
uint32_t mx_rgb_color(mx_rgb_state_t s);
void mx_rgb_flash(mx_rgb_state_t s, int ms);   /* show s for ms, then back to normal */
void mx_rgb_notify(void);
void mx_rgb_ai(bool on);                       /* Assistant page open */
void mx_rgb_power(bool usb, bool full, int pct);
