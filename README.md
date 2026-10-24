# Maximus OS for the SenseCAP Watcher W1-B

An AI companion firmware for the round-screen SenseCAP Watcher (ESP32-S3): voice assistant, live crypto prices, trading signals, voice journal and a Telegram bot, all driven by one rotary knob.

![Maximus OS screens](docs/screens.png)

*Screens rendered by the host simulator from the real UI code (`sim/`).*

## Install in one click

1. Open the installer page (GitHub Pages: `https://drfxai.github.io/Maximus-OS--SenseCAP-Watcher-W1-B/` once the first build has deployed) in **Chrome or Edge** on a computer.
2. Plug in the Watcher with a USB-C data cable and press **Install Maximus OS**. One image holds the bootloader, partition table and app, so a fully wiped device is fine.
3. Enter Wi-Fi and your relay details on the same page and press **Send to watch**.

The relay (Telegram bot + AI) is a free Cloudflare Worker, set up in about five minutes: see [docs/CLOUDFLARE.md](docs/CLOUDFLARE.md).

## Knob

| Action | Result |
|---|---|
| Turn | Navigate pages; move the highlight inside lists |
| Tap (release before 5 s) | Confirm / act (start or stop listening, open a list, pick an item) |
| Hold 5 s, release | Back to Home |
| Hold 10 s | Power off (a progress ring shows both thresholds) |

Touch also works: tap = confirm, swipe left/right = next/previous page, tap a list row to pick it.

## What's in it

| Area | Details |
|---|---|
| **AI assistant** | Press the knob, speak, get an answer on screen. Audio goes to your Worker (Whisper for speech-to-text, then an LLM). Four modes carried over from Maximus Matrix OS: Trading, Market Analysis, News & Sentiment, General. |
| **Telegram** | Chat with the same assistant from Telegram (text or voice notes), `/price`, `/mode`, `/watch <text>` to push a message to the watch, and the watch can send you camera photos and journal entries. Serverless: runs on Cloudflare Workers. |
| **Markets** | Price, 24 h change, hourly sparkline and high/low for up to four coins; portfolio value from your holdings. Price source chain (Coinbase, Kraken, CoinGecko) ported from GeekMagic-Ultra. |
| **Signals** | TradingView alerts posted to the Worker show up on the watch as an alert screen with the RGB ring flashing; notes like "stop loss hit" turn it red. Same alert JSON as GeekMagic-Ultra. |
| **Clock** | SNTP with a relay-time fallback and the on-board RTC, POSIX time zones with daylight saving. |
| **Voice journal** | Press, talk, press: transcribed and saved on the watch, mirrored to Telegram. |
| **Security** | 4-digit PIN lock (hashed with a per-device salt), auto-lock after 5 minutes idle. |
| **RGB ring** | Green listening, blue thinking, purple processing, orange warning, red critical. |
| **Camera** | Settings > Camera sends a photo to Telegram (see [docs/HARDWARE.md](docs/HARDWARE.md)). |

## Repo layout

```
firmware/   ESP-IDF 5.2 app (LVGL 8 on Seeed's Watcher board support package)
  main/ui/    all screens; no ESP-IDF includes, so it also builds on a PC
worker/     Cloudflare Worker: Telegram webhook, AI, voice, prices, signals
site/       the one-click installer page (ESP Web Tools + Web Serial setup)
sim/        host build of the UI that renders screenshots
tools/      asset generator, dependency fetch, screenshot montage
docs/       Cloudflare setup, hardware notes
```

## Building yourself

CI builds and publishes everything (`.github/workflows/build.yml`). Locally:

```bash
tools/fetch-deps.sh                          # Seeed BSP + LVGL 8.4, pinned
cd firmware && . $IDF_PATH/export.sh         # ESP-IDF 5.2.x
idf.py set-target esp32s3 && idf.py build
cd build && esptool.py --chip esp32s3 merge_bin -o ../../site/firmware/maximus-os-full.bin @flash_args
```

UI screenshots without hardware: `git clone --depth 1 -b v8.4.0 https://github.com/lvgl/lvgl .sim-deps/lvgl && make -C sim && ./sim/render out && python3 tools/sim_montage.py out screens.png`.

Worker tests: `cd worker && npm test`.

## Credits

Built on Seeed Studio's [SenseCAP Watcher firmware SDK](https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware) (Apache-2.0) for display, touch, audio, knob and camera drivers. Signal and price logic from GeekMagic-Ultra; assistant personas from Maximus Matrix OS.
