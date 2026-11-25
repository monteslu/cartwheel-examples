// Bikemesa's verge grass sways in the wind, drawn on the GPU by the engine's foliage wind (cartwheel/foliage.h): the
// tufts' instance matrices are their rest poses, written once. On the built cart through romdev, GL and WebGPU, on
// track 1 with the rider waiting at the start (nothing else moves on screen):
//   1. two frames 15 frames apart differ (luminance step over 6) in at least 2 % of the verge band's pixels (the band under the clock and
//      the start flag, the rider left out): the grass moves;
//   2. no program is missing (cw.three.missing 0).
// CONTROL: sw still_grass 1 (the wind's strength 0) must leave the frames all but equal (under 0.3 %), so check 1 can
// fail. Frames for a look are kept in out/bikemesa-grass-wind.
//   node tests/bikemesa-grass-wind.mjs [--gl] [--webgpu]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = 'bikemesa-grass-wind', OUT = join(ROOT, 'out/bikemesa-grass-wind');
const { readImage } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/bikemesa.wasc')));
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
const lum = (img, i) => 0.2126 * img.rgba[4 * i] + 0.7152 * img.rgba[4 * i + 1] + 0.0722 * img.rgba[4 * i + 2];
// the verge band (rows 340 to 1000 of 1080: under the clock and the start flag, over the prompts), the rider left
// out (he breathes), as a share of its pixels
function changed(a, b) {
  const k = a.h / 1080, y0 = Math.round(340 * k), y1 = Math.round(1000 * k), rx0 = Math.round(830 * k), rx1 = Math.round(1060 * k);
  const ry0 = Math.round(430 * k), ry1 = Math.round(700 * k);
  let n = 0, t = 0;
  for (let y = y0; y < y1; y++)
    for (let x = 0; x < a.w; x++) {
      if (x >= rx0 && x < rx1 && y >= ry0 && y < ry1) continue;
      const i = y * a.w + x;
      t++, n += Math.abs(lum(a, i) - lum(b, i)) > 6;
    }
  return n / t;
}

async function measure(backend, still) {
  await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  await tool('frame', { op: 'step', frames: 400 });
  await cmd('track 1', 600);
  await tool('frame', { op: 'step', frames: 600 });
  await cmd(`sw still_grass ${still ? 1 : 0}`);
  await tool('frame', { op: 'step', frames: 2 });
  const tag = `${backend}-${still ? 'still' : 'wind'}`;
  const a = await shot(`${tag}-a.png`);
  await tool('frame', { op: 'step', frames: 15 });
  const b = await shot(`${tag}-b.png`);
  const missing = (await tool('wasm', { op: 'read', name: 'cw.three.missing' }))?.value;
  return { moved: changed(a, b), missing };
}

let fails = 0;
const say = (ok, line) => { if (!ok) fails++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${line}`); };
for (const backend of BACKENDS) {
  const w = await measure(backend, false), s = await measure(backend, true);
  const p = (v) => (v * 100).toFixed(2) + ' %';
  say(s.moved < 0.003, `${backend}: control (sw still_grass 1) holds the grass still (${p(s.moved)} of the verge band changed, under 0.3 %)`);
  say(w.moved >= 0.02, `${backend}: the grass sways in the wind (${p(w.moved)} of the verge band changed in 15 frames, at least 2 %)`);
  say(w.missing === 0, `${backend}: no program missing (cw.three.missing ${w.missing})`);
}
await bye(fails ? 1 : 0);
