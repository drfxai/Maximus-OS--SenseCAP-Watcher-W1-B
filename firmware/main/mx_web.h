#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Web console: the watch opens its own Wi-Fi hotspot ("Maximus-XXXX") and
 * serves a page with its status, the live log (copy or download it) and the
 * details of the last crash. The page is also reachable on the home network
 * at the watch's address while the console is on.
 */
void mx_web_init(void);                  /* after Wi-Fi is up; turns on if it was left on */
bool mx_web_set(bool on);
bool mx_web_on(void);
void mx_web_info(char *ssid, size_t ssid_n, char *pass, size_t pass_n);
