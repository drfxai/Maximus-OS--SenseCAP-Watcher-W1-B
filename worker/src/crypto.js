// Price chain ported from GeekMagic-Ultra bridge/worker.js. Coinbase gives
// 24h range + hourly candles; Kraken and CoinGecko are price-only fallbacks.
// Output matches the device's CryptoSet wire format: {src, ts, tickers:[{s,d,p,c,h,l,k}]}.

const SPARK_POINTS = 24;
const TTL = 20;

export function priceDecimals(v) {
  if (v >= 1000) return 2;
  if (v >= 1) return 3;
  if (v >= 0.01) return 5;
  return 8;
}
export function formatPrice(raw) {
  const n = Number(raw);
  return isFinite(n) ? n.toFixed(priceDecimals(Math.abs(n))) : "0";
}
export function shortName(symbol) {
  for (const q of ["USDT", "FDUSD", "BUSD", "USDC", "TUSD", "BTC", "ETH", "EUR", "GBP"]) {
    if (symbol.length > q.length && symbol.endsWith(q)) return symbol.slice(0, -q.length);
  }
  return symbol;
}
export function scaleSpark(closes) {
  const v = closes.map(Number).filter((n) => isFinite(n));
  if (v.length < 2) return [];
  const lo = Math.min(...v), hi = Math.max(...v), span = hi - lo;
  return span <= 0 ? v.map(() => 50) : v.map((x) => Math.round(((x - lo) / span) * 100));
}

async function upstream(url) {
  const r = await fetch(url, {
    cf: { cacheTtl: TTL, cacheEverything: true },
    headers: { accept: "application/json", "user-agent": "MaximusOS/1.0 (+https://github.com/drfxai/Maximus-OS--SenseCAP-Watcher-W1-B)" },
  });
  if (!r.ok) throw new Error(String(r.status));
  return r.json();
}

async function fromCoinbase(symbols) {
  const failures = [];
  const res = await Promise.all(symbols.map(async (s) => {
    const product = `${shortName(s)}-USD`;
    let stats;
    try { stats = await upstream(`https://api.exchange.coinbase.com/products/${product}/stats`); }
    catch (e) { if (e.message !== "404") failures.push(e.message); return null; }
    const last = Number(stats.last), open = Number(stats.open);
    if (!isFinite(last) || last <= 0) return null;
    let k = [];
    try {
      const hour = 3600e3, end = Math.floor(Date.now() / hour) * hour, start = end - SPARK_POINTS * hour;
      const c = await upstream(`https://api.exchange.coinbase.com/products/${product}/candles?granularity=3600&start=${new Date(start).toISOString()}&end=${new Date(end).toISOString()}`);
      k = scaleSpark((c || []).slice(0, SPARK_POINTS).reverse().map((r) => r[4]));
    } catch { /* price without chart */ }
    return { s, d: shortName(s), p: formatPrice(last), c: Number((open > 0 ? ((last - open) / open) * 100 : 0).toFixed(2)), h: formatPrice(stats.high), l: formatPrice(stats.low), k };
  }));
  const out = res.filter(Boolean);
  if (!out.length && failures.length) throw new Error(failures[0]);
  return out;
}

async function fromKraken(symbols) {
  const pairs = symbols.map((s) => { const b = shortName(s); return `${b === "BTC" ? "XBT" : b}USD`; });
  const res = await upstream(`https://api.kraken.com/0/public/Ticker?pair=${pairs.join(",")}`);
  const entries = Object.entries((res && res.result) || {});
  const out = [];
  symbols.forEach((s, i) => {
    const base = pairs[i].replace(/USD$/, "");
    const hit = entries.find(([k]) => k.includes(base) && k.includes("USD"));
    if (!hit) return;
    const t = hit[1], last = Number(t.c && t.c[0]), open = Number(t.o);
    if (!isFinite(last) || last <= 0) return;
    out.push({ s, d: shortName(s), p: formatPrice(last), c: Number((open > 0 ? ((last - open) / open) * 100 : 0).toFixed(2)), h: formatPrice(t.h && t.h[1]), l: formatPrice(t.l && t.l[1]), k: [] });
  });
  return out;
}

const GECKO_IDS = { BTC: "bitcoin", ETH: "ethereum", SOL: "solana", BNB: "binancecoin", XRP: "ripple", DOGE: "dogecoin", ADA: "cardano", AVAX: "avalanche-2", LINK: "chainlink", TON: "the-open-network", TRX: "tron", DOT: "polkadot" };

async function fromCoinGecko(symbols) {
  const want = symbols.map((s) => [s, GECKO_IDS[shortName(s)]]).filter(([, id]) => id);
  if (!want.length) return [];
  const r = await upstream(`https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&ids=${want.map(([, id]) => id).join(",")}&sparkline=true&price_change_percentage=24h`);
  return want.map(([s, id]) => {
    const c = r.find((x) => x.id === id);
    if (!c) return null;
    const sp = (c.sparkline_in_7d && c.sparkline_in_7d.price) || [];
    return { s, d: shortName(s), p: formatPrice(c.current_price), c: Number((c.price_change_percentage_24h || 0).toFixed(2)), h: formatPrice(c.high_24h), l: formatPrice(c.low_24h), k: scaleSpark(sp.slice(-SPARK_POINTS)) };
  }).filter(Boolean);
}

export const SOURCES = [
  { name: "coinbase", fn: fromCoinbase },
  { name: "kraken", fn: fromKraken },
  { name: "coingecko", fn: fromCoinGecko },
];

export async function fetchCrypto(symbols, sources = SOURCES) {
  const tried = [];
  for (const s of sources) {
    try {
      const tickers = await s.fn(symbols);
      if (tickers.length) return { v: "1", src: s.name, ts: Date.now(), tickers };
      tried.push(`${s.name}: no pairs`);
    } catch (e) { tried.push(`${s.name}: ${e.message || e}`); }
  }
  return { error: `no source had ${symbols.join(",")}`, tried };
}
