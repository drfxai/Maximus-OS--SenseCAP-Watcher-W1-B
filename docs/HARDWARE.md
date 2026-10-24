# Hardware notes

Pin map and drivers come from Seeed's board support package (`components/sensecap-watcher`).

| Feature | Part | How Maximus uses it |
|---|---|---|
| Display | 412x412 round, SPD2010 (QSPI) | LVGL 8, double-buffered in PSRAM |
| Touch | SPD2010 touch (I2C) | tap = confirm, swipe = next/previous |
| Knob | Rotary encoder on GPIO41/42, button on the IO expander | rotation via the `knob` driver; the button is polled every 20 ms for the 5 s and 10 s holds |
| Mic + speaker | ES7243 ADC / ES8311 DAC, 16 kHz mono | voice capture with end-of-speech detection, UI sounds |
| RGB ring | WS2812 on GPIO40 | status colours |
| Camera | Himax AI chip over SPI (SSCMA protocol) | snapshot to Telegram |
| RTC | PCF8563 | keeps time without Wi-Fi |
| Battery | ADC + charge detect | level and charging icon |
| NFC | not exposed by Seeed's BSP | the mockup shows "NFC + PIN"; only the PIN is implemented |

## Power off

Holding the knob for 10 s cuts the board's power rail. When the USB cable is connected the rail stays up, so the watch goes into deep sleep with the screen off and wakes on the next knob press.

## Wiped device

The ESP32-S3's first-stage bootloader is in ROM, so USB flashing always works. If the device does not show up as a serial port, hold the knob while plugging in the cable.

The camera runs on a **separate Himax chip with its own firmware**. Installing Maximus OS does not touch it. If that chip was also erased, Settings → Camera reports "Camera module not responding"; restore it with Seeed's factory firmware first (Seeed's Watcher docs describe flashing the Himax image).

## Known gaps

- Spoken replies: the speaker is used for chimes only; answers are shown on screen. Text-to-speech needs an audio decoder on the device.
- Live camera preview is not shown on the display.
- The firmware has been written against Seeed's BSP API and compiled in CI, but not yet run on hardware by the author.
