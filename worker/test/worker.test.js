import test from "node:test";
import assert from "node:assert/strict";
import { parseSignal } from "../src/signal.js";
import { scaleSpark, formatPrice, shortName, fetchCrypto } from "../src/crypto.js";
import { pcmToWav } from "../src/index.js";

test("parseSignal reads JSON alerts", () => {
  const s = parseSignal('{"symbol":"btcusdt","side":"buy","score":140,"entry":"64000","sl":"63000"}');
  assert.equal(s.symbol, "BTCUSDT");
  assert.equal(s.side, "BUY");
  assert.equal(s.score, 100);
});
test("parseSignal reads free text", () => {
  const s = parseSignal("BUY BTCUSDT @ 64000 SL 63000");
  assert.equal(s.side, "BUY");
  assert.equal(s.sl, "63000");
});
test("parseSignal rejects junk", () => assert.equal(parseSignal("hello"), null));
test("crypto helpers", () => {
  assert.equal(shortName("BTCUSDT"), "BTC");
  assert.equal(formatPrice("63782.414"), "63782.41");
  assert.deepEqual(scaleSpark([1, 2, 3]), [0, 50, 100]);
  assert.deepEqual(scaleSpark([5, 5]), [50, 50]);
});
test("fetchCrypto falls through sources", async () => {
  const r = await fetchCrypto(["BTCUSDT"], [
    { name: "a", fn: async () => { throw new Error("403"); } },
    { name: "b", fn: async () => [{ s: "BTCUSDT", d: "BTC", p: "1", c: 0, h: "1", l: "1", k: [] }] },
  ]);
  assert.equal(r.src, "b");
});
test("pcmToWav writes a RIFF header", () => {
  const w = pcmToWav(new Uint8Array(10), 16000);
  assert.equal(w.length, 54);
  assert.equal(String.fromCharCode(...w.slice(0, 4)), "RIFF");
});

/* ---- end-to-end through the real handler with in-memory bindings ---- */
import worker from "../src/index.js";

function kv() {
  const m = new Map();
  return { get: async (k) => (m.has(k) ? m.get(k) : null), put: async (k, v) => void m.set(k, String(v)), delete: async (k) => void m.delete(k) };
}
function env(extra = {}) {
  return { MAXIMUS: kv(), DEVICE_KEY: "k", TELEGRAM_BOT_TOKEN: "T", AI_MODEL: "m",
    AI: { run: async (model, input) => (model.includes("whisper") ? { text: "what is btc" } : { response: "BTC looks fine." }) }, ...extra };
}
const ctx = () => { const p = []; return { waitUntil: (x) => p.push(x), done: () => Promise.all(p) }; };
const call = (e, path, init = {}, c = ctx()) => worker.fetch(new Request("https://w.test" + path, init), e, c);

test("device endpoints reject missing key and answer CORS preflight", async () => {
  const e = env();
  assert.equal((await call(e, "/v1/ping")).status, 401);
  const pre = await call(e, "/v1/ping", { method: "OPTIONS" });
  assert.equal(pre.status, 204);
  assert.equal(pre.headers.get("access-control-allow-origin"), "*");
});

test("ping exposes the pairing code until a chat pairs, then hides it", async () => {
  const sent = [];
  globalThis.fetch = async (u, init) => { sent.push([String(u), init && init.body]); return new Response(JSON.stringify({ ok: true, result: {} })); };
  const e = env();
  const auth = { headers: { authorization: "Bearer k" } };
  const j1 = await (await call(e, "/v1/ping", auth)).json();
  assert.match(j1.pair_code, /^\d{6}$/);
  assert.equal(j1.telegram, false);

  const setup = await (await call(e, "/admin/setup", { method: "POST", ...auth })).json();
  assert.equal(setup.pairing_code, j1.pair_code);
  const hook = sent.find(([u]) => u.endsWith("/setWebhook"));
  const hookUrl = JSON.parse(hook[1]).url;

  // a stranger cannot pair, the right code can
  const c1 = ctx();
  await call(e, new URL(hookUrl).pathname, { method: "POST", body: JSON.stringify({ message: { chat: { id: 5 }, text: "/start 000000" } }) }, c1);
  await c1.done();
  assert.equal((await (await call(e, "/v1/ping", auth)).json()).telegram, false);
  const c2 = ctx();
  await call(e, new URL(hookUrl).pathname, { method: "POST", body: JSON.stringify({ message: { chat: { id: 5 }, text: "/start " + j1.pair_code } }) }, c2);
  await c2.done();
  const j2 = await (await call(e, "/v1/ping", auth)).json();
  assert.equal(j2.telegram, true);
  assert.equal(j2.pair_code, undefined);
});

test("paired owner chats, other chats are ignored, /watch reaches the device inbox", async () => {
  const sent = [];
  globalThis.fetch = async (u, init) => { sent.push([String(u), init && init.body]); return new Response(JSON.stringify({ ok: true, result: {} })); };
  const e = env({ OWNER_CHAT_ID: "7" });
  const hookPath = "/telegram/" + (await (async () => { await call(e, "/admin/setup", { method: "POST", headers: { authorization: "Bearer k" } }); return e.MAXIMUS.get("hooksecret"); })());
  const post = async (chat, text) => { const c = ctx(); await call(e, hookPath, { method: "POST", body: JSON.stringify({ message: { chat: { id: chat }, text } }) }, c); await c.done(); };

  sent.length = 0;
  await post(99, "hello");
  assert.equal(sent.filter(([u]) => u.endsWith("/sendMessage")).length, 0, "stranger gets no reply");

  await post(7, "hello");
  const reply = sent.find(([u]) => u.endsWith("/sendMessage"));
  assert.match(JSON.parse(reply[1]).text, /BTC looks fine/);

  await post(7, "/watch buy the dip");
  const poll = await (await call(e, "/v1/poll", { headers: { authorization: "Bearer k" } })).json();
  assert.equal(poll.inbox[0].text, "buy the dip");
  const again = await (await call(e, "/v1/poll", { headers: { authorization: "Bearer k" } })).json();
  assert.equal(again.inbox.length, 0, "inbox is cleared once read");
});

test("signal webhook needs the key, stores the signal and surfaces it to the device", async () => {
  globalThis.fetch = async () => new Response(JSON.stringify({ ok: true, result: {} }));
  const e = env({ OWNER_CHAT_ID: "7" });
  const bad = await call(e, "/v1/signal?key=nope", { method: "POST", body: '{"symbol":"BTCUSDT","side":"buy"}' });
  assert.equal(bad.status, 403);
  const ok = await call(e, "/v1/signal?key=k", { method: "POST", body: '{"symbol":"BTCUSDT","side":"buy","entry":"64000","note":"Stop loss hit"}' });
  assert.equal(ok.status, 200);
  const poll = await (await call(e, "/v1/poll?sig=0", { headers: { authorization: "Bearer k" } })).json();
  assert.equal(poll.signal.symbol, "BTCUSDT");
  assert.equal(poll.signal.side, "BUY");
});

test("voice endpoint transcribes then answers", async () => {
  globalThis.fetch = async () => new Response(JSON.stringify({ ok: true, result: {} }));
  const e = env({ OWNER_CHAT_ID: "7" });
  const r = await call(e, "/v1/voice?mode=trading", { method: "POST", headers: { authorization: "Bearer k" }, body: new Uint8Array(8000) });
  const j = await r.json();
  assert.equal(j.heard, "what is btc");
  assert.match(j.reply, /BTC/);
});
