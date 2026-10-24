# Cloudflare relay (Telegram bot + AI)

The relay is one Worker (`worker/`). The watch talks to it with a shared **device key**; the Telegram **bot token** lives only in the Worker.

```
Telegram ──webhook──▶ Worker ◀──HTTPS (Bearer DEVICE_KEY)── Watch
TradingView ──POST /v1/signal──▶ │
                                 ├─ Workers AI (Whisper speech-to-text, Llama chat)   or any OpenAI-compatible API
                                 └─ KV (chat memory, watch inbox, pairing)
```

## Easiest: Deploy button

1. Create a bot with [@BotFather](https://t.me/BotFather) (`/newbot`) and copy the token.
2. Open `https://deploy.workers.cloudflare.com/?url=https://github.com/drfxai/Maximus-OS--SenseCAP-Watcher-W1-B/tree/main/worker`.
3. Paste the token as `TELEGRAM_BOT_TOKEN`, choose a long random `DEVICE_KEY`, deploy.
4. In the installer page, enter the Worker URL and the same device key, then press **Register Telegram webhook**.
5. In Telegram, send `/start <pairing code>` to your bot. The first chat to pair owns the bot; everyone else is ignored.

## Command line

```bash
cd worker && npm install
npx wrangler login
npx wrangler secret put TELEGRAM_BOT_TOKEN
npx wrangler secret put DEVICE_KEY
npx wrangler deploy                     # also creates the KV namespace (Wrangler 4)
curl -X POST https://<worker>.workers.dev/admin/setup -H "Authorization: Bearer <DEVICE_KEY>"
```

`/admin/setup` registers the webhook and returns the bot name and pairing code. It is safe to call again.

## Using another AI provider

Workers AI (free tier) is the default. To use OpenRouter, OpenAI, Groq or anything OpenAI-compatible:

```bash
npx wrangler secret put AI_API_KEY
# in wrangler.toml [vars]:  AI_BASE_URL = "https://openrouter.ai/api/v1"   AI_MODEL = "openai/gpt-4o-mini"
```

Voice transcription always uses Workers AI (Whisper).

## TradingView

Alert URL: `https://<worker>/v1/signal?key=<DEVICE_KEY>` (or set a separate `WEBHOOK_KEY` secret). Body is the JSON from GeekMagic-Ultra's `tradingview/alert-message.json` (`symbol`, `side`, `entry`, `tp1`..`tp3`, `sl`, `note`) or plain text such as `BUY BTCUSDT @ 64000 SL 63000`. A `note` containing "stop" raises the red critical alert on the watch.

## API (for reference)

| Route | Auth | Purpose |
|---|---|---|
| `POST /telegram/<secret>` | secret path | Telegram webhook |
| `POST /admin/setup` | Bearer | register webhook, return pairing code |
| `GET /v1/ping` | Bearer | health, pairing state |
| `GET /v1/crypto?symbols=` | Bearer | prices + sparklines |
| `GET /v1/poll?sig=` | Bearer | Telegram `/watch` messages and newest signal |
| `POST /v1/chat` | Bearer | text to assistant |
| `POST /v1/voice` | Bearer | 16 kHz mono PCM or WAV to transcript + answer (`journal=1` saves instead) |
| `POST /v1/photo`, `/v1/notify` | Bearer | forward to Telegram |
| `POST /v1/signal` | key | TradingView alerts |

## Privacy

Voice clips go to your own Worker and Cloudflare Workers AI (or the provider you configure) for transcription. Nothing is stored except short chat memory (24 h) and the inbox in your KV namespace.
