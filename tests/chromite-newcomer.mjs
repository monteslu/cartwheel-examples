// chromite on the cart through romdev, GL and WebGPU, by the pad: a newcomer powers on, reads the title's prompt
// ({fire} : launch), launches, and plays level 1 by what the screen shows: it holds fire, slides under the lowest
// enemy (the big bright chrome shapes on the screen), and sidesteps enemy rounds it sees coming down at it (the
// saturated magenta and cyan dots), as slowly as a first-timer reacts (a look every 8 frames). Only its own ship's
// place is read from the game (it is on the screen anyway). It must reach level 2 within six minutes of play.
// The second leg is the idle seat: chromite is one player; a second pad that presses Start on the title and then
// nothing must not hold anything up (the game launches and the first pad plays it through).
// Control: sw rules 1 (classic, the original's rule: every enemy that gets past costs a ship) leaves the same
// newcomer short of level 2.
//   node tests/chromite-newcomer.mjs [--gl] [--webgpu] [--cart <path>] [--no-control]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readImage } from '../deps/cartwheel-engine/tools/lib/png.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.CHROMITE_SESSION || 'chromite-newcomer';
const OUT = join(ROOT, 'out/chromite-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/chromite.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const LIMIT = +arg('--frames', 21600); /* six minutes */
mkdirSync(OUT, { recursive: true });
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

/* the play field to the screen, at 1920 x 1080 (the camera frames field y -8.5..8.5 over the height) */
const SX = (x) => 960 + 60 * x, SY = (y) => 550 - 60 * y;

async function play(backend, tag, { pre, idle } = {}) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(150);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  if (idle) { await pads({}, { start: true }); await step(4); await pads({}, {}); await step(30); }
  const titleState = await rd('game.state');
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-title.png`) });
  await pads({ south: true }); await step(4); await pads({}); await step(20); /* the prompt: {fire} : launch */
  const started = await rd('game.state');
  let frames = 0, tx = 0, reached = false, over = false, shots = 0, lastLog = 0;
  const path = join(OUT, `${backend}-${tag}-bot.png`);
  while (frames < LIMIT) {
    const st = await rd('game.state');
    const level = await rd('level');
    if (level >= 2) { reached = true; break; }
    if (st === 4) { over = true; break; }
    if (st === 2) { await pads({ start: true }); await step(4); await pads({}); await step(4); continue; } /* never pauses on purpose */
    const hp = await rd('hero.pos');
    const hx = SX(hp[0]), hy = SY(hp[1]);
    /* look at the screen */
    await tool('frame', { op: 'stepAndShot', frames: 1, path });
    frames++;
    const img = readImage(path), W = img.w, px = img.rgba;
    const at = (x, y) => (y * W + x) * 4;
    /* enemies: blobs of bright, unsaturated chrome; the lowest is the target */
    const C = 24;
    let best = null;
    for (let cy = 60; cy < 1040; cy += C) for (let cx = 360; cx < 1560; cx += C) {
      if (Math.abs(cx - hx) < 70 && Math.abs(cy - hy) < 90) continue;
      let n = 0;
      for (let y = cy; y < cy + C; y += 3) for (let x = cx; x < cx + C; x += 3) {
        const i = at(x, y), r = px[i], g = px[i + 1], b = px[i + 2];
        if (r > 175 && g > 175 && b > 175 && Math.max(r, g, b) - Math.min(r, g, b) < 45) n++;
      }
      if (n >= 30 && (!best || cy > best.cy)) best = { cx, cy };
    }
    if (best) tx = (best.cx + C / 2 - 960) / 60;
    /* danger: saturated magenta or cyan pixels in the column above the ship, weighted by nearness */
    let push = 0;
    for (let y = Math.max(0, hy - 300); y < Math.min(1079, hy + 20); y += 4) for (let x = Math.max(0, hx - 110); x < Math.min(1919, hx + 110); x += 4) {
      const i = at(x, y), r = px[i], g = px[i + 1], b = px[i + 2];
      const magenta = r > 190 && b > 120 && g < 110, cyan = g > 170 && b > 190 && r < 110;
      if (!magenta && !cyan) continue;
      const w = (1 - Math.abs(x - hx) / 110) * (1 - (hy - y) / 320);
      push += (x < hx ? 1 : -1) * w;
    }
    const steer = Math.abs(push) > 0.5 ? Math.sign(push) : Math.max(-1, Math.min(1, (tx - hp[0]) * 0.8));
    await pads({ south: true, axes: { lx: steer, ly: Math.max(-1, Math.min(1, (hp[1] - (-4.5)) * 0.5)) } });
    await step(7);
    frames += 7;
    if (frames - lastLog >= 1800) { lastLog = frames; console.log(`  ${backend} ${tag} f ${frames} lives ${await rd('lives')} kills ${await rd('kills')} escapes ${await rd('escapes')} losses ${await rd('losses')} score ${await rd('score')}`); }
    if (++shots % 450 === 0) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-${frames}.png`) });
  }
  await pads({});
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-end.png`) });
  return { titleState, started, reached, over, frames, lives: await rd('lives'), kills: await rd('kills'), losses: await rd('losses'), escapes: await rd('escapes') };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const n = await play(backend, 'newcomer', { idle: true });
  check('newcomer (a second pad pressed Start on the title, then idled): launches from the prompt and reaches level 2', n.started === 1 && n.reached,
    `title ${n.titleState}, launched ${n.started}, level 2 ${n.reached} at ${n.frames} frames, game over ${n.over}, lives ${n.lives}, kills ${n.kills}, escapes ${n.escapes}, losses ${n.losses}`);
  if (process.argv.includes('--no-control')) continue;
  const c = await play(backend, 'control', { pre: 'sw rules 1' });
  check('control (sw rules 1, classic): the same newcomer falls short of level 2', !c.reached,
    `level 2 ${c.reached} at ${c.frames} frames, game over ${c.over}, lives ${c.lives}, losses ${c.losses}`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
