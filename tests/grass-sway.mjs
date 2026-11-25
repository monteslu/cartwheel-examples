// The grass of bomberdude's meadow and pugball's pitch sways in the engine's foliage wind (cartwheel/foliage.h, on
// the GPU every frame), and on the pitch the pugs and the cat push the grass aside (foliage movers). On the built cart
// through romdev, GL and WebGPU, from the play camera at the start of a round or match, the game paused (tree_pause 1:
// the rules, the players and the camera hold still while the wind's clock runs on):
//   1. sway: over 20 frames at least 1% of the pixels of the grass regions change (by more than 12 of 255 in a
//      channel), and under 0.5% of a region with no grass (bomberdude's board, pugball's stands);
//      CONTROL: sw still_grass 1 (the grass built without the foliage wind) must leave the grass regions under 1%.
//   2. pugball movers: in play, before the pause, the pugs and the cat are the engine's foliage movers (debug field
//      grass.movers, at least 2) and the grass at P1's pug is pushed over (grass.push_p1, the engine's CPU copy of the
//      push the shaders apply, cw_foliage_push_at: at least 0.5 of the full bend); a crop round the pug is kept for a look;
//      CONTROL: sw grass_movers 0 must leave no mover and no push.
// Frames and native-scale crops for a look are kept in out/grass-sway.
//   node tests/grass-sway.mjs [--gl] [--webgpu] [--games bomberdude,pugball]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = 'grass-sway', OUT = join(ROOT, 'out/grass-sway');
const { readImage, writePng } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const GAMES = arg('--games', 'bomberdude,pugball').split(',');
mkdirSync(OUT, { recursive: true });

// each game: how a round starts, how long the camera takes to settle, its grass regions and a region with no grass
// (x0, y0, x1, y1 on the 1920 x 1080 picture), and for pugball the region round P1's pug kept as a crop
const GAME = {
  bomberdude: { start: 'arena 1', run: 200, settle: 2, grass: [[0, 300, 300, 1000], [1650, 350, 1920, 1000]], bare: [600, 400, 1300, 900] },
  pugball: {
    start: 'stadium 1', run: 400, settle: 400, grass: [[0, 820, 880, 1080], [1000, 800, 1600, 1080]], bare: [0, 380, 1900, 600],
    pug: [700, 820, 1220, 1080], movers: true,
  },
};

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
const cmd = async (text, maxFrames = 8) => {
  const j = await tool('wasm', { op: 'command', text, maxFrames });
  if (j.reply?.error) { console.error(`${text}: ${JSON.stringify(j.reply)}`); await bye(3); }
  return j.reply;
};
const step = (frames) => tool('frame', { op: 'step', frames });
const shot = async (name) => { const p = join(OUT, name); await tool('frame', { op: 'screenshot', path: p }); return readImage(p); };
const crop = (img, [x0, y0, x1, y1], name) => {
  const w = x1 - x0, h = y1 - y0, px = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) px.set(img.rgba.subarray(4 * ((y0 + y) * img.w + x0), 4 * ((y0 + y) * img.w + x1)), 4 * y * w);
  writePng(join(OUT, name), w, h, px);
};
// the share of a region's pixels that change by more than 12 of 255 in a channel
function changed(a, b, [x0, y0, x1, y1]) {
  let n = 0, c = 0;
  for (let y = y0; y < y1; y++)
    for (let x = x0; x < x1; x++) {
      const i = 4 * (y * a.w + x);
      n++;
      if (Math.max(Math.abs(a.rgba[i] - b.rgba[i]), Math.abs(a.rgba[i + 1] - b.rgba[i + 1]), Math.abs(a.rgba[i + 2] - b.rgba[i + 2])) > 12) c++;
    }
  return c / n;
}

// one run: the round started with the switches given, paused, two frames 20 frames apart
async function run(game, backend, sw, tag) {
  const g = GAME[game];
  await tool('loadMedia', { platform: 'wasmcart', path: join(ROOT, `build/cart-wgpu/carts/${game}.wasc`), gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  await step(300);
  for (const s of sw) await cmd(`sw ${s}`);
  await cmd(g.start, 30);
  await step(g.run);
  const read = async (name) => (await tool('wasm', { op: 'read', name })).value;
  const movers = g.movers ? { n: await read('grass.movers'), push: await read('grass.push_p1') } : null;
  await cmd('tree_pause 1', 4);
  await step(g.settle);
  const a = await shot(`${game}-${backend}-${tag}-a.png`);
  await step(20);
  const b = await shot(`${game}-${backend}-${tag}-b.png`);
  return { a, b, movers };
}

let fails = 0;
const say = (ok, line) => { if (!ok) fails++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${line}`); };
const pc = (v) => (100 * v).toFixed(2) + '%';
for (const game of GAMES)
  for (const backend of BACKENDS) {
    const g = GAME[game], t = `${game} ${backend}`;
    const wind = await run(game, backend, [], 'wind');
    const still = await run(game, backend, ['still_grass 1'], 'still');
    g.grass.forEach((r, k) => {
      crop(wind.a, r, `${game}-${backend}-grass${k}-a.png`), crop(wind.b, r, `${game}-${backend}-grass${k}-b.png`);
      const w = changed(wind.a, wind.b, r), s = changed(still.a, still.b, r);
      say(w >= 0.01, `${t}: grass region ${k} sways (${pc(w)} of its pixels change over 20 paused frames, at least 1%)`);
      say(s < 0.01, `${t}: control (sw still_grass 1): grass region ${k} holds still (${pc(s)}, under 1%), so the sway check can fail`);
    });
    const bare = changed(wind.a, wind.b, g.bare);
    say(bare < 0.005, `${t}: the region with no grass holds still (${pc(bare)}, under 0.5%): the game and its camera are paused`);
    if (g.movers) {
      const off = await run(game, backend, ['grass_movers 0'], 'nomovers');
      crop(wind.a, g.pug, `${game}-${backend}-pug-movers.png`), crop(still.a, g.pug, `${game}-${backend}-pug-still.png`);
      say(wind.movers.n >= 2 && wind.movers.push >= 500, `${t}: ${wind.movers.n} foliage movers (at least 2), the grass at P1's pug pushed ${wind.movers.push / 1000} of the full bend (at least 0.5)`);
      say(off.movers.n === 0 && off.movers.push === 0, `${t}: control (sw grass_movers 0): ${off.movers.n} movers, push ${off.movers.push / 1000}: none, so the movers check can fail`);
    }
  }
await bye(fails ? 1 : 0);
