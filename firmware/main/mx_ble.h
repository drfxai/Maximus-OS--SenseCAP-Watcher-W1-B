#pragma once

#include <stdbool.h>

/*
 * Bluetooth LE switch. When on, the watch advertises as "Maximus OS" so a
 * phone can see it; the radio is fully released when off.
 */
bool mx_ble_set(bool on);       /* false if the stack could not start */
bool mx_ble_on(void);
