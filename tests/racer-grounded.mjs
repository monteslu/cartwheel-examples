// racer on the cart through romdev, GL and WebGPU: the karts sit on the road. Two measures, each with a control that must
// fail:
//   shadow  A race built below High (Very Low builds it thin, as Auto does on a slow machine and three or four views do),
//           paused a few seconds in: the same frame drawn with the player's kart throwing its shadow and without
//           (kshadow 0 0|1), at each preset. What goes darker is the shadow the kart throws on the road; it must cover at
//           least SHADOW_MIN_PX pixels of the 1080p frame (a kart with nothing under it reads as hovering). Control:
//           sw sun_short 1 (the sun's shadow camera cut to 70 m deep below High, as it was) leaves no shadow.
//   gap     One lap of every map with the bot at the wheel. Every frame the cart measures each kart's four tyres as drawn
//           against the road under them (kartgfx.c ground_probe, the gap.* fields; a frame over 30 mm is counted): while
//           the physics has any of its wheels on the road no kart, the player's or a computer one, is drawn more than
//           GAP_MAX_MM above it. A kart's drawn flip, hop or trick, a jump's flight and a kart in the air off a crest are
//           not hovering (its wheels hang off the road in the physics too): they are counted apart and printed.
//           Control: sw wheel_lag 1 (the wheels drawn from the vehicle controller's casts, a tick behind the drawn
//           chassis, as they were) must put a kart more than GAP_MAX_MM above the road with its wheels on it.
//   node tests/racer-grounded.mjs [--gl] [--webgpu] [--maps 0,1,...] [--legs shadow,gap,control] [--cart <path>]
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.RACER_SESSION || 'racer-grounded', OUT = join(ROOT, 'out/racer-grounded');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/racer.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const MAPS = arg('--maps', '0,1,2,3,4,5,6,7').split(',').map(Number);
const LEGS = arg('--legs', 'shadow,gap,control').split(',');
const PRESETS = ['verylow', 'low', 'medium', 'high'];
const SHADOW_MIN_PX = 1500, GAP_MAX_MM = 50, CLASSES = ['all', 'some', 'air', 'jump', 'anim'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
// (node:http, not fetch: fetch gives up on a reply that takes five minutes, and loading the cart on a busy GPU can)
const post = (name, body) => new Promise((ok, no) => {
  const q = request(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION } }, (res) => {
    let t = '';
    res.setEncoding('utf8');
    res.on('data', (c) => (t += c));
    res.on('end', () => ok({ status: res.statusCode, text: t }));
  });
  q.on('error', no);
  q.end(body);
});
const tool = async (name, args) => {
  let r;
  try { r = await post(name, JSON.stringify(args)); }
  catch (e) { console.error(`romdev ${name}: ${e.message} (the server is down)`); await bye(3); }
  const t = r.text;
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (r.status !== 200 || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = (text) => tool('wasm', { op: 'command', text });
const step = (frames) => tool('frame', { op: 'step', frames });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

/** an 8-bit RGB or RGBA PNG (what romdev writes) as its luma per pixel */
function luma(path) {
  const b = readFileSync(path);
  let w = 0, h = 0, ct = 0;
  const idat = [];
  for (let o = 8; o < b.length;) {
    const len = b.readUInt32BE(o), type = b.toString('latin1', o + 4, o + 8), d = b.subarray(o + 8, o + 8 + len);
    if (type === 'IHDR') { w = d.readUInt32BE(0), h = d.readUInt32BE(4), ct = d[9]; if (d[8] !== 8 || d[12]) throw new Error(`${path}: not an 8-bit non-interlaced PNG`); }
    else if (type === 'IDAT') idat.push(d);
    o += 12 + len;
  }
  const ch = ct === 6 ? 4 : ct === 2 ? 3 : 0;
  if (!ch) throw new Error(`${path}: colour type ${ct}`);
  const raw = inflateSync(Buffer.concat(idat)), stride = w * ch, px = Buffer.alloc(stride * h), Y = new Float32Array(w * h);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], src = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)), row = y * stride;
    for (let x = 0; x < stride; x++) {
      const a = x >= ch ? px[row + x - ch] : 0, up = y ? px[row - stride + x] : 0, c = y && x >= ch ? px[row - stride + x - ch] : 0;
      let v = src[x];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const p = a + up - c, pa = Math.abs(p - a), pb = Math.abs(p - up), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      px[row + x] = v & 255;
    }
  }
  for (let i = 0; i < w * h; i++) Y[i] = 0.2126 * px[i * ch] + 0.7152 * px[i * ch + 1] + 0.0722 * px[i * ch + 2];
  return { w, h, Y };
}
/** pixels darker by 12 or more with the kart's shadow than without it, less those lighter, in the chase camera's middle (a
 * quarter of the width either side, from 45% to 92% of the height: the kart and the road round it, not the map, the
 * speedometer or the scenery far off). Without it is drawn before and after: what still moves while paused (rippling
 * water, a spinning wheel) differs between those two and is left out */
function shadowPx(withShadow, before, after) {
  const a = luma(withShadow), b = luma(before), c = luma(after);
  let dark = 0, light = 0, moving = 0;
  for (let y = Math.floor(a.h * 0.45); y < Math.floor(a.h * 0.92); y++)
    for (let x = Math.floor(a.w * 0.25); x < Math.floor(a.w * 0.75); x++) {
      const i = y * a.w + x;
      if (Math.abs(b.Y[i] - c.Y[i]) >= 6) { moving++; continue; }
      const d = (b.Y[i] + c.Y[i]) / 2 - a.Y[i];
      if (d >= 12) dark++; else if (d <= -12) light++;
    }
  return { dark, light, moving, net: dark - light };
}

async function load(backend) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(120);
}
async function race(map, pre) {
  await cmd(`sw sun_short ${pre === 'sun_short' ? 1 : 0}`);
  await cmd(`sw wheel_lag ${pre === 'wheel_lag' ? 1 : 0}`);
  await cmd(`race ${map} 1 1`);
  await cmd('bot 1');
  let phase = 0;
  for (let f = 0; f < 1500 && phase !== 2; f += 30) { await step(30); phase = +(await rd('race.phase')); }
  if (phase !== 2) { console.error(`map ${map}: no race started (phase ${phase})`); await bye(3); }
}
/** the player's shadow at each preset on a race built at Very Low: paused at three moments (3, 7 and 11 s past the green),
 * the best of them per preset (a kart in the shade of a stand or a bridge throws none on any preset) */
async function shadows(backend, map, pre) {
  await cmd('quality verylow');
  await race(map, pre);
  const r = Object.fromEntries(PRESETS.map((q) => [q, { net: -1e9 }]));
  for (const at of [180, 420, 660]) {
    await step(at === 180 ? 180 : 240);
    await cmd('tree_pause 1');
    for (const q of PRESETS) {
      await cmd(`quality ${q}`);
      await step(12);
      const tag = `${backend}-m${map}-${at}-${q}${pre ? '-' + pre : ''}`;
      await cmd('kshadow 0 0'); await step(4);
      await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${tag}-noshadow.png`) });
      await cmd('kshadow 0 1'); await step(4);
      await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${tag}.png`) });
      await cmd('kshadow 0 0'); await step(4);
      await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${tag}-noshadow2.png`) });
      await cmd('kshadow 0 1');
      const s = shadowPx(join(OUT, `${tag}.png`), join(OUT, `${tag}-noshadow.png`), join(OUT, `${tag}-noshadow2.png`));
      /* the frames of each preset's best moment are kept to look at, the rest go (a frame is several MB) */
      const files = ['', '-noshadow', '-noshadow2'].map((x) => join(OUT, `${tag}${x}.png`));
      if (s.net > r[q].net) { for (const f of r[q].files || []) rmSync(f, { force: true }); r[q] = { ...s, at, files }; }
      else for (const f of files) rmSync(f, { force: true });
    }
    await cmd('quality verylow');
    await cmd('tree_pause 0');
  }
  await cmd('quality high');
  return r;
}
/** one lap, the cart counting every frame */
async function lap(map, pre) {
  await race(map, pre);
  await cmd('gap reset');
  let phase = 2;
  for (let i = 0; i < 600 && phase !== 3; i++) { await step(120); phase = +(await rd('race.phase')); }
  const fr = await rd('gap.frames'), n = await rd('gap.n'), mx = await rd('gap.max_mm'), at = await rd('gap.at_s');
  const k = [];
  for (let i = 0; i < 8; i++) if (fr[i]) k.push({ kart: i, frames: fr[i], ...Object.fromEntries(CLASSES.map((c, j) => [c, { n: n[i * 5 + j], max: mx[i * 5 + j], at: at[i * 5 + j] }])) });
  return { done: phase === 3, karts: k };
}
const worst = (ks, c) => ks.reduce((w, k) => (k[c].max > w.max ? { kart: k.kart, ...k[c] } : w), { kart: -1, n: 0, max: 0, at: 0 });
const total = (ks, c) => ks.reduce((s, k) => s + k[c].n, 0);
const desc = (w) => (w.kart < 0 ? 'none' : `worst kart ${w.kart}${w.kart === 0 ? ' (the player)' : ''} ${w.max} mm at ${w.at} m`);

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  await load(backend);
  for (const map of MAPS) {
    if (LEGS.includes('shadow')) {
      const s = await shadows(backend, map, '');
      for (const q of PRESETS) check(`map ${map} ${q}: the player's kart throws its shadow on the road`, s[q].net >= SHADOW_MIN_PX, `${s[q].net} px darker at its best (${s[q].at / 60} s past the green: ${s[q].dark} darker, ${s[q].light} lighter, ${s[q].moving} moving)`);
    }
    if (LEGS.includes('gap')) {
      const r = await lap(map, '');
      const all = worst(r.karts, 'all'), some = worst(r.karts, 'some');
      const frames = r.karts.reduce((s, k) => s + k.frames, 0);
      check(`map ${map}: a lap ran`, r.done && frames > 1000, `${frames} kart-frames measured`);
      check(`map ${map}: no kart drawn above the road with all its wheels on it`, all.max <= GAP_MAX_MM, `${total(r.karts, 'all')} frames over 30 mm, ${desc(all)}`);
      check(`map ${map}: no kart drawn above the road with some of its wheels on it`, some.max <= GAP_MAX_MM, `${total(r.karts, 'some')} frames over 30 mm, ${desc(some)}`);
      const air = worst(r.karts, 'air'), jump = worst(r.karts, 'jump'), anim = worst(r.karts, 'anim');
      console.log(`     (in the air off a crest or a bump ${total(r.karts, 'air')} frames, ${desc(air)}; a jump's flight ${total(r.karts, 'jump')} frames; a drawn flip, hop or trick ${total(r.karts, 'anim')} frames)`);
    }
  }
  if (LEGS.includes('control')) {
    const map = MAPS[0];
    const s = await shadows(backend, map, 'sun_short');
    check(`control: sw sun_short 1, map ${map}: no preset below High shows the kart's shadow`, PRESETS.slice(0, 3).every((q) => s[q].net < SHADOW_MIN_PX),
      PRESETS.map((q) => `${q} ${s[q].net} px`).join(', '));
    const r = await lap(map, 'wheel_lag');
    check(`control: sw wheel_lag 1, map ${map}: a kart is drawn above the road with its wheels on it`, Math.max(worst(r.karts, 'all').max, worst(r.karts, 'some').max) > GAP_MAX_MM, `all wheels down: ${total(r.karts, 'all')} frames over 30 mm, ${desc(worst(r.karts, 'all'))}; some: ${total(r.karts, 'some')} frames, ${desc(worst(r.karts, 'some'))}`);
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
