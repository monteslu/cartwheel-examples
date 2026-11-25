// Derby's arena floor does not read as one repeated tile: the dirt is a Terrain3D of two dirts tiling at 6 m and
// 7.3 m, blended by world-space noise, with the engine's near-ground macro variation (and, on High and Ultra, the tile
// turned zone by zone). On the built cart through romdev, GL and WebGPU, at the start of a bout:
//   1. top-down over the middle of the bowl (camera 32 m up, 27 px a metre), the luminance autocorrelation's peak at
//      the old tile's 6 m period (over the mean of the lags round it, along x and z, as the engine's terrain-detail
//      test measures it) is at most two thirds of the old floor's, at High and at Low;
//   2. the spread of 2 m patches' mean brightness is at least the old floor's (the floor is not flattened to hide the
//      repeat: sw old_floor 1 against the new one);
//   3. the new floor keeps its detail: the mean brightness step between neighbouring pixels on the ground is at least
//      0.85 of the old floor's (no blur as a fix).
// CONTROL: sw old_floor 1 (the floor as one 6 m tile repeated, as it was) must show the peak (at least 0.03; cars, their shadows and the puddles share the picture), so the
// check above can fail. Frames for a look are kept in out/derby-floor (top-down, and the play camera at the start grid).
//   node tests/derby-floor.mjs [--gl] [--webgpu] [--presets high,low]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = 'derby-floor', OUT = join(ROOT, 'out/derby-floor');
const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const PRESETS = arg('--presets', 'high,low').split(',');
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/derby.wasc')));
mkdirSync(OUT, { recursive: true });
let requests = 0;
const post = (name, body) => new Promise((ok, no) => {
  requests++;
  const q = request(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': SESSION } }, (res) => {
    let t = '';
    res.setEncoding('utf8');
    res.on('data', (c) => (t += c));
    res.on('end', () => ok({ status: res.statusCode, text: t }));
  });
  q.on('error', no);
  q.end(body);
});
const bye = async (code) => {
  try { await post('host', '{"op":"shutdown"}'); } catch {}
  console.log(`romdev requests: ${requests}`);
  process.exit(code);
};
const tool = async (name, args) => {
  let r;
  try { r = await post(name, JSON.stringify(args)); }
  catch (e) { console.error(`romdev ${name}: ${e.message} (the server is down)`); await bye(3); }
  const sm = /session: ([\w-]+)/.exec(r.text);
  if (sm && sm[1] !== SESSION) { console.error(`romdev ${name}: a reply for session ${sm[1]}, not ${SESSION}: discarded`); await bye(3); }
  let j; try { j = JSON.parse(r.text); } catch { j = r.text; }
  if (r.status !== 200 || j?.error || j?.isError) { console.error(`romdev ${name}: ${r.text.slice(0, 400)}`); await bye(3); }
  return j;
};
const cmd = async (text, maxFrames = 8) => (await tool('wasm', { op: 'command', text, maxFrames })).reply;
const shot = async (name) => { const p = join(OUT, name); await tool('frame', { op: 'screenshot', path: p }); return readImage(p); };

const lum = (img) => {
  const L = new Float32Array(img.w * img.h);
  for (let i = 0; i < L.length; i++) L[i] = 0.2126 * img.rgba[4 * i] + 0.7152 * img.rgba[4 * i + 1] + 0.0722 * img.rgba[4 * i + 2];
  return L;
};
// the camera 32 m over the floor looking straight down, vertical fov 64: the picture is 40 m tall
const PPM = (h) => h / (2 * 32 * Math.tan((32 * Math.PI) / 180));
function repeatPeak(img) {
  const L = lum(img), P = Math.round(PPM(img.h) * 6), S = 900, x0 = (img.w - S) >> 1, y0 = (img.h - S) >> 1, N = S - 2 * P;
  let mean = 0;
  for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) mean += L[(y0 + y) * img.w + x0 + x];
  mean /= S * S;
  const ac = (dx, dy) => {
    let s = 0, v = 0;
    for (let y = 0; y < N; y++)
      for (let x = 0; x < N; x++) {
        const a = L[(y0 + y) * img.w + x0 + x] - mean, b = L[(y0 + y + dy) * img.w + x0 + x + dx] - mean;
        s += a * b, v += a * a;
      }
    return s / v;
  };
  let peak = 0;
  for (const [ax, ay] of [[1, 0], [0, 1]]) {
    const at = (k) => ac(Math.round(ax * k * P), Math.round(ay * k * P));
    peak += at(1) - (at(0.6) + at(0.8) + at(1.2) + at(1.4)) / 4;
  }
  return peak / 2;
}
function patchSpread(img) {
  const L = lum(img), S = Math.round(PPM(img.h) * 2), m = [], x0 = (img.w - 900) >> 1, y0 = (img.h - 900) >> 1;
  for (let py = y0; py + S <= y0 + 900; py += S)
    for (let px = x0; px + S <= x0 + 900; px += S) {
      let s = 0;
      for (let y = 0; y < S; y++) for (let x = 0; x < S; x++) s += L[(py + y) * img.w + px + x];
      m.push(s / (S * S));
    }
  const mean = m.reduce((p, q) => p + q) / m.length;
  return Math.sqrt(m.reduce((p, q) => p + (q - mean) ** 2, 0) / m.length) / mean;
}
function detail(img) {
  const L = lum(img), x0 = (img.w - 900) >> 1, y0 = (img.h - 900) >> 1;
  let s = 0, n = 0;
  for (let y = y0; y < y0 + 900; y++) for (let x = x0; x < x0 + 899; x++) s += Math.abs(L[y * img.w + x + 1] - L[y * img.w + x]), n++;
  return s / n;
}

async function measure(backend, preset, old) {
  await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  await tool('frame', { op: 'step', frames: 300 });
  await cmd(`quality ${preset}`, 30);
  if (old) await cmd('sw old_floor 1');
  await cmd('bout 1', 600);
  await tool('frame', { op: 'step', frames: 30 });
  const tag = `${backend}-${preset}-${old ? 'old' : 'new'}`;
  await shot(`grid-${tag}.png`);
  await cmd('eye 0 32 0.01 0 0 0');
  await tool('frame', { op: 'step', frames: 3 });
  const img = await shot(`top-${tag}.png`);
  await cmd('eye off');
  return { peak: repeatPeak(img), spread: patchSpread(img), detail: detail(img) };
}

let fails = 0;
const say = (ok, line) => { if (!ok) fails++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${line}`); };
for (const backend of BACKENDS)
  for (const preset of PRESETS) {
    const n = await measure(backend, preset, false), o = await measure(backend, preset, true);
    const f = (v) => v.toFixed(3);
    say(o.peak >= 0.03, `${backend} ${preset}: control (sw old_floor 1) shows the 6 m repeat (peak ${f(o.peak)} >= 0.03)`);
    say(n.peak <= (o.peak * 2) / 3, `${backend} ${preset}: the repeat's peak ${f(n.peak)} is at most two thirds of the old floor's ${f(o.peak)}`);
    say(n.spread >= o.spread, `${backend} ${preset}: 2 m patches vary at least as much (${f(n.spread)} against ${f(o.spread)})`);
    say(n.detail >= o.detail * 0.85, `${backend} ${preset}: the detail holds (pixel step ${f(n.detail)} against ${f(o.detail)})`);
  }
await bye(fails ? 1 : 0);
