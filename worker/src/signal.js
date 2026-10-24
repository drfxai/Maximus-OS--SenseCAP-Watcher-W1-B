// Parses a TradingView alert body into the device's signal record. Accepts the
// JSON template from GeekMagic-Ultra (symbol, side, score, conf, entry, tp1-3,
// sl, tf, note) or free text such as "BUY BTCUSDT @ 64000 SL 63000".
const NUM = /-?\d+(?:\.\d+)?/;

export function parseSignal(raw) {
  let o = null;
  try { o = JSON.parse(raw); } catch { /* free text */ }
  if (o && typeof o === "object") {
    const side = String(o.side || o.action || "").toUpperCase();
    const symbol = String(o.symbol || o.ticker || "").toUpperCase();
    if (!side && !symbol) return null;
    const s = (v) => (v === undefined || v === null ? "" : String(v));
    return {
      symbol, side,
      score: clamp(o.score), conf: clamp(o.conf),
      entry: s(o.entry), tp1: s(o.tp1), tp2: s(o.tp2), tp3: s(o.tp3), sl: s(o.sl),
      tf: s(o.tf), note: s(o.note).slice(0, 200),
    };
  }
  const t = String(raw || "").trim();
  if (!t) return null;
  const side = (t.match(/\b(BUY|SELL|LONG|SHORT|FLAT)\b/i) || [])[1];
  const symbol = (t.match(/\b([A-Z]{2,10}(?:USDT|USD|BTC|EUR)?)\b/) || [])[1];
  const lv = (k) => { const m = t.match(new RegExp(`${k}\\s*[:=@]?\\s*(${NUM.source})`, "i")); return m ? m[1] : ""; };
  if (!side) return null;
  return { symbol: (symbol || "").toUpperCase(), side: side.toUpperCase(), score: 0, conf: 0, entry: lv("(?:entry|@)"), tp1: lv("tp1?"), tp2: lv("tp2"), tp3: lv("tp3"), sl: lv("sl"), tf: "", note: t.slice(0, 200) };
}

function clamp(v) {
  const n = Math.round(Number(v) || 0);
  return Math.max(0, Math.min(100, n));
}
