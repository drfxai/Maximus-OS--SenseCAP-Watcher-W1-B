/**
 * Maximus OS cloud relay (Cloudflare Worker).
 *
 * One Worker serves three clients:
 *   - Telegram            POST /telegram/<WEBHOOK_PATH_SECRET>   (webhook)
 *   - the watch           /v1/*                                  (Bearer DEVICE_KEY)
 *   - TradingView alerts  POST /v1/signal                        (key in query or body)
 *
 * Secrets: TELEGRAM_BOT_TOKEN, DEVICE_KEY. Optional: AI_API_KEY (external
 * OpenAI-compatible provider), WEBHOOK_KEY (TradingView), OWNER_CHAT_ID.
 * The bot token never leaves the Worker - the watch only holds DEVICE_KEY.
 */

import { MODES, DEFAULT_MODE } from "./modes.js";
import { fetchCrypto } from "./crypto.js";
import { parseSignal } from "./signal.js";

const VERSION = "1.0.0";
const MAX_HISTORY = 12;
const MAX_INBOX = 20;

export default {
  async fetch(request, env, ctx) {
    const res = await route(request, env, ctx);
    // The installer page calls /admin/setup and /v1/ping from the browser.
    // Auth is a bearer token, never a cookie, so a wildcard origin is safe.
    const out = new Response(res.body, res);
    out.headers.set("access-control-allow-origin", "*");
    return out;
  },
};

async function route(request, env, ctx) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, "") || "/";
    if (request.method === "OPTIONS") {
      return new Response(null, {
        status: 204,
        headers: {
          "access-control-allow-methods": "GET,POST,OPTIONS",
          "access-control-allow-headers": "authorization,content-type",
          "access-control-max-age": "86400",
        },
      });
    }
    try {
      if (path === "/") return json({ name: "maximus-os", version: VERSION, ok: true });
      if (path.startsWith("/telegram/") && request.method === "POST") {
        return await handleTelegram(request, env, ctx, path);
      }
      if (path === "/admin/setup" && request.method === "POST") return await adminSetup(request, env, url);
      if (path === "/v1/signal" && request.method === "POST") return await handleSignalIn(request, env, url);
      if (path.startsWith("/v1/")) {
        if (!deviceAuthed(request, url, env)) return text("unauthorised", 401);
        return await handleDevice(request, env, url, path);
      }
      return text("not found", 404);
    } catch (err) {
      return json({ error: String((err && err.message) || err) }, 500);
    }
}

/* ------------------------------------------------------------------ */
/* Device API                                                          */
/* ------------------------------------------------------------------ */

async function handleDevice(request, env, url, path) {
  switch (path) {
    case "/v1/ping":
      const paired = (await ownerOf(env)) !== null;
      // The pairing code is only shown to the authenticated watch, and only
      // until a chat has claimed the bot.
      return json({ ok: true, v: VERSION, ts: Date.now(), telegram: paired, pair_code: paired ? undefined : await pairCode(env) });

    case "/v1/time":
      return json({ ts: Date.now() });

    case "/v1/crypto": {
      const symbols = (url.searchParams.get("symbols") || "BTCUSDT,ETHUSDT,SOLUSDT,BNBUSDT")
        .split(",").map((s) => s.trim().toUpperCase().replace(/[^A-Z0-9]/g, "")).filter(Boolean).slice(0, 8);
      const res = await fetchCrypto(symbols);
      return json(res, res.error ? 502 : 200);
    }

    case "/v1/poll": {
      // Everything waiting for the watch, then cleared. At-most-once is fine
      // for notifications; the watch keeps its own history.
      const inbox = (await kvGet(env, "inbox")) || [];
      const signal = await kvGet(env, "signal");
      const sinceSignal = Number(url.searchParams.get("sig") || 0);
      await env.MAXIMUS.put("inbox", "[]");
      return json({
        ts: Date.now(),
        inbox,
        signal: signal && signal.ts > sinceSignal ? signal : null,
      });
    }

    case "/v1/chat": {
      if (request.method !== "POST") return text("POST only", 405);
      const body = await request.json();
      const reply = await converse(env, "device", String(body.text || ""), body.mode);
      if (body.mirror !== false) await tgMirror(env, `🗣 ${body.text}\n\n🤖 ${reply}`);
      return json({ reply });
    }

    case "/v1/voice": {
      // Raw 16 kHz mono 16-bit PCM or WAV body. Transcribe, answer, return both.
      if (request.method !== "POST") return text("POST only", 405);
      const audio = new Uint8Array(await request.arrayBuffer());
      if (audio.length < 3200) return json({ error: "audio too short" }, 400);
      const wav = isWav(audio) ? audio : pcmToWav(audio, 16000);
      const heard = await transcribe(env, wav);
      if (!heard) return json({ heard: "", reply: "I did not catch that." });
      if (url.searchParams.get("journal")) {
        await tgMirror(env, `📓 Journal\n${heard}`);
        return json({ heard, reply: "Saved." });
      }
      const reply = await converse(env, "device", heard, url.searchParams.get("mode"));
      await tgMirror(env, `🎙 ${heard}\n\n🤖 ${reply}`);
      return json({ heard, reply });
    }

    case "/v1/notify": {
      // The watch asks the Worker to message the owner on Telegram.
      const body = await request.json();
      const ok = await tgMirror(env, String(body.text || "").slice(0, 3500));
      return json({ ok });
    }

    case "/v1/photo": {
      if (request.method !== "POST") return text("POST only", 405);
      const jpeg = await request.arrayBuffer();
      const ok = await tgSendPhoto(env, jpeg, url.searchParams.get("caption") || "Maximus camera");
      return json({ ok });
    }
  }
  return text("not found", 404);
}

/* ------------------------------------------------------------------ */
/* Telegram                                                            */
/* ------------------------------------------------------------------ */

async function handleTelegram(request, env, ctx, path) {
  const secret = await webhookSecret(env);
  if (path !== `/telegram/${secret}`) return text("forbidden", 403);
  const update = await request.json();
  const msg = update.message || update.edited_message;
  if (!msg || !msg.chat) return json({ ok: true });
  // Reply after acknowledging so Telegram never retries a slow AI call.
  ctx.waitUntil(onTelegramMessage(env, msg).catch((e) => tgSend(env, msg.chat.id, `⚠️ ${e.message || e}`)));
  return json({ ok: true });
}

async function onTelegramMessage(env, msg) {
  const chat = msg.chat.id;
  const raw = (msg.text || msg.caption || "").trim();

  // First person to send /start <PAIR_CODE> owns the bot. Everyone else is ignored.
  let owner = await ownerOf(env);
  if (owner === null) {
    const code = (raw.split(/\s+/)[1] || "").trim();
    const expect = await pairCode(env);
    if (raw.startsWith("/start") && code === expect) {
      await env.MAXIMUS.put("owner", String(chat));
      owner = chat;
      await tgSend(env, chat, "✅ Paired. I am Maximus. Send me anything, or /help.");
    } else {
      await tgSend(env, chat, "This bot is not paired yet. Send /start <pairing code> (shown on the watch or in the setup page).");
    }
    return;
  }
  if (chat !== owner) return; // silently ignore strangers

  if (msg.voice || msg.audio) {
    const file = msg.voice || msg.audio;
    const heard = await transcribe(env, await tgDownload(env, file.file_id));
    await tgSend(env, chat, `🎙 ${heard}`);
    return tgSend(env, chat, await converse(env, "tg", heard));
  }
  if (!raw) return;

  if (raw.startsWith("/")) return onCommand(env, chat, raw);
  await tgSend(env, chat, await converse(env, "tg", raw));
}

async function onCommand(env, chat, raw) {
  const [cmdFull, ...rest] = raw.split(/\s+/);
  const cmd = cmdFull.split("@")[0].toLowerCase();
  const arg = rest.join(" ").trim();

  switch (cmd) {
    case "/start":
    case "/help":
      return tgSend(env, chat,
        "Maximus commands\n" +
        "/price BTC ETH - live prices\n" +
        "/mode - list or set AI mode (trading, analysis, news, general)\n" +
        "/watch <text> - push a notification to the watch\n" +
        "/reset - clear chat memory\n" +
        "/status - relay status\n" +
        "Any other text is sent to the AI assistant; voice notes are transcribed.");
    case "/price": {
      const syms = (arg || "BTC ETH").split(/[\s,]+/).filter(Boolean).slice(0, 6).map((s) => s.toUpperCase().replace(/(USDT|USD)$/, "") + "USDT");
      const r = await fetchCrypto(syms);
      if (r.error) return tgSend(env, chat, `Prices unavailable: ${r.error}`);
      return tgSend(env, chat, r.tickers.map((t) => `${t.d}  $${t.p}  ${t.c >= 0 ? "+" : ""}${t.c}%`).join("\n"));
    }
    case "/mode": {
      if (!arg) {
        const cur = (await kvGet(env, "mode")) || DEFAULT_MODE;
        return tgSend(env, chat, Object.entries(MODES).map(([k, m]) => `${k === cur ? "▶" : "•"} ${k} - ${m.title}`).join("\n"));
      }
      if (!MODES[arg.toLowerCase()]) return tgSend(env, chat, "Unknown mode.");
      await env.MAXIMUS.put("mode", arg.toLowerCase());
      return tgSend(env, chat, `Mode set: ${MODES[arg.toLowerCase()].title}`);
    }
    case "/watch": {
      if (!arg) return tgSend(env, chat, "Usage: /watch <message for the watch>");
      await pushInbox(env, { kind: "msg", from: "Telegram", text: arg.slice(0, 400), ts: Date.now() });
      return tgSend(env, chat, "Sent to the watch (it picks it up within a few seconds).");
    }
    case "/reset":
      await env.MAXIMUS.delete("hist");
      return tgSend(env, chat, "Memory cleared.");
    case "/status": {
      const sig = await kvGet(env, "signal");
      return tgSend(env, chat, `Maximus relay v${VERSION}\nAI: ${env.AI_BASE_URL ? "external" : "Workers AI"} (${env.AI_MODEL})\nLast signal: ${sig ? `${sig.symbol} ${sig.side}` : "none"}`);
    }
  }
  return tgSend(env, chat, "Unknown command. /help");
}

async function tgApi(env, method, body, isForm) {
  if (!env.TELEGRAM_BOT_TOKEN) return null;
  const r = await fetch(`https://api.telegram.org/bot${env.TELEGRAM_BOT_TOKEN}/${method}`, {
    method: "POST",
    headers: isForm ? undefined : { "content-type": "application/json" },
    body: isForm ? body : JSON.stringify(body),
  });
  return r.json().catch(() => null);
}

async function tgSend(env, chat, textMsg) {
  const t = String(textMsg || "");
  for (let i = 0; i < Math.max(1, t.length); i += 3900) {
    await tgApi(env, "sendMessage", { chat_id: chat, text: t.slice(i, i + 3900) });
  }
  return true;
}

async function tgMirror(env, textMsg) {
  const owner = await ownerOf(env);
  if (owner === null) return false;
  return tgSend(env, owner, textMsg);
}

async function tgSendPhoto(env, jpeg, caption) {
  const owner = await ownerOf(env);
  if (owner === null) return false;
  const form = new FormData();
  form.append("chat_id", String(owner));
  form.append("caption", caption);
  form.append("photo", new Blob([jpeg], { type: "image/jpeg" }), "maximus.jpg");
  const r = await tgApi(env, "sendPhoto", form, true);
  return !!(r && r.ok);
}

async function tgDownload(env, fileId) {
  const info = await tgApi(env, "getFile", { file_id: fileId });
  const p = info && info.result && info.result.file_path;
  if (!p) throw new Error("telegram file not found");
  const r = await fetch(`https://api.telegram.org/file/bot${env.TELEGRAM_BOT_TOKEN}/${p}`);
  return new Uint8Array(await r.arrayBuffer());
}

/** Registers the webhook and returns the pairing code. Idempotent. */
async function adminSetup(request, env, url) {
  if (!deviceAuthed(request, url, env)) return text("unauthorised", 401);
  if (!env.TELEGRAM_BOT_TOKEN) return json({ error: "TELEGRAM_BOT_TOKEN secret is not set" }, 400);
  const secret = await webhookSecret(env);
  const hook = `${url.origin}/telegram/${secret}`;
  const r = await tgApi(env, "setWebhook", { url: hook, allowed_updates: ["message", "edited_message"], drop_pending_updates: true });
  const me = await tgApi(env, "getMe", {});
  return json({
    ok: !!(r && r.ok),
    webhook: r && r.description,
    bot: me && me.result && me.result.username,
    pairing_code: await pairCode(env),
    paired: (await ownerOf(env)) !== null,
  });
}

/* ------------------------------------------------------------------ */
/* AI                                                                  */
/* ------------------------------------------------------------------ */

async function converse(env, channel, userText, modeOverride) {
  const modeKey = MODES[modeOverride] ? modeOverride : (await kvGet(env, "mode")) || DEFAULT_MODE;
  const mode = MODES[modeKey];
  const hist = (await kvGet(env, "hist")) || [];
  const messages = [{ role: "system", content: mode.prompt }, ...hist.slice(-MAX_HISTORY), { role: "user", content: userText }];
  let reply = await complete(env, messages, channel === "device" ? 220 : 700);
  reply = reply.trim() || "…";
  hist.push({ role: "user", content: userText }, { role: "assistant", content: reply });
  await env.MAXIMUS.put("hist", JSON.stringify(hist.slice(-MAX_HISTORY * 2)), { expirationTtl: 86400 });
  return reply;
}

async function complete(env, messages, maxTokens) {
  if (env.AI_BASE_URL && env.AI_API_KEY) {
    const r = await fetch(`${env.AI_BASE_URL.replace(/\/+$/, "")}/chat/completions`, {
      method: "POST",
      headers: { "content-type": "application/json", authorization: `Bearer ${env.AI_API_KEY}` },
      body: JSON.stringify({ model: env.AI_MODEL, messages, max_tokens: maxTokens }),
    });
    if (!r.ok) throw new Error(`AI provider ${r.status}`);
    const j = await r.json();
    return (j.choices && j.choices[0] && j.choices[0].message.content) || "";
  }
  if (!env.AI) throw new Error("no AI configured (add the [ai] binding or AI_BASE_URL/AI_API_KEY)");
  const out = await env.AI.run(env.AI_MODEL || "@cf/meta/llama-3.1-8b-instruct", { messages, max_tokens: maxTokens });
  return out.response || "";
}

async function transcribe(env, wavBytes) {
  if (!env.AI) throw new Error("voice needs the Workers AI binding");
  const out = await env.AI.run("@cf/openai/whisper-large-v3-turbo", { audio: b64(wavBytes) });
  return (out.text || "").trim();
}

/* ------------------------------------------------------------------ */
/* TradingView / signals                                               */
/* ------------------------------------------------------------------ */

async function handleSignalIn(request, env, url) {
  const raw = await request.text();
  const key = url.searchParams.get("key") || safeKey(raw);
  const want = env.WEBHOOK_KEY || env.DEVICE_KEY;
  if (!want || !timingSafeEqual(String(key || ""), want)) return text("bad key", 403);
  const sig = parseSignal(raw);
  if (!sig) return json({ error: "unparseable alert" }, 400);
  sig.ts = Date.now();
  await env.MAXIMUS.put("signal", JSON.stringify(sig));
  await pushInbox(env, { kind: "signal", from: "Signal", text: `${sig.symbol} ${sig.side}${sig.entry ? " @ " + sig.entry : ""}`, ts: sig.ts });
  await tgMirror(env, `📡 ${sig.symbol} ${sig.side}\nEntry ${sig.entry || "-"}  SL ${sig.sl || "-"}  TP ${sig.tp1 || "-"}\n${sig.note || ""}`);
  return json({ ok: true });
}

function safeKey(raw) {
  try { return JSON.parse(raw).key; } catch { return ""; }
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

function deviceAuthed(request, url, env) {
  if (!env.DEVICE_KEY) return false;
  const h = request.headers.get("authorization") || "";
  const given = h.startsWith("Bearer ") ? h.slice(7) : url.searchParams.get("key") || "";
  return timingSafeEqual(given, env.DEVICE_KEY);
}

async function ownerOf(env) {
  if (env.OWNER_CHAT_ID) return Number(env.OWNER_CHAT_ID);
  const v = await env.MAXIMUS.get("owner");
  return v ? Number(v) : null;
}

async function webhookSecret(env) {
  let s = await env.MAXIMUS.get("hooksecret");
  if (!s) {
    s = randomHex(16);
    await env.MAXIMUS.put("hooksecret", s);
  }
  return s;
}

async function pairCode(env) {
  let s = await env.MAXIMUS.get("paircode");
  if (!s) {
    s = String(100000 + (crypto.getRandomValues(new Uint32Array(1))[0] % 900000));
    await env.MAXIMUS.put("paircode", s);
  }
  return s;
}

async function pushInbox(env, item) {
  const inbox = (await kvGet(env, "inbox")) || [];
  inbox.push(item);
  await env.MAXIMUS.put("inbox", JSON.stringify(inbox.slice(-MAX_INBOX)));
}

async function kvGet(env, k) {
  const v = await env.MAXIMUS.get(k);
  if (!v) return null;
  try { return JSON.parse(v); } catch { return v; }
}

function randomHex(n) {
  return [...crypto.getRandomValues(new Uint8Array(n))].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function timingSafeEqual(a, b) {
  const x = new TextEncoder().encode(a), y = new TextEncoder().encode(b);
  let d = x.length ^ y.length;
  for (let i = 0; i < Math.max(x.length, y.length); i++) d |= (x[i] || 0) ^ (y[i] || 0);
  return d === 0;
}

function isWav(u8) {
  return u8.length > 12 && u8[0] === 0x52 && u8[1] === 0x49 && u8[2] === 0x46 && u8[3] === 0x46;
}

export function pcmToWav(pcm, rate) {
  const h = new DataView(new ArrayBuffer(44));
  const w = (o, s) => [...s].forEach((c, i) => h.setUint8(o + i, c.charCodeAt(0)));
  w(0, "RIFF"); h.setUint32(4, 36 + pcm.length, true); w(8, "WAVEfmt ");
  h.setUint32(16, 16, true); h.setUint16(20, 1, true); h.setUint16(22, 1, true);
  h.setUint32(24, rate, true); h.setUint32(28, rate * 2, true); h.setUint16(32, 2, true); h.setUint16(34, 16, true);
  w(36, "data"); h.setUint32(40, pcm.length, true);
  const out = new Uint8Array(44 + pcm.length);
  out.set(new Uint8Array(h.buffer), 0);
  out.set(pcm, 44);
  return out;
}

function b64(u8) {
  let s = "";
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
  return btoa(s);
}

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), { status, headers: { "content-type": "application/json", "cache-control": "no-store" } });
}
function text(s, status = 200) {
  return new Response(s, { status, headers: { "content-type": "text/plain" } });
}
