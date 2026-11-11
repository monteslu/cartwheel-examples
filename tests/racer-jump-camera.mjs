// racer on the cart through romdev, GL and WebGPU: the chase camera follows the kart over every jump. The cart keeps a
// record of each jump the player's kart flies (main.c jcam_frame, the jcam.* fields): every drawn frame from the take-off
// to 45 frames after the landing, where the kart sits on the screen, how far it is from the eye, whether the camera was
// held still as for a fall (jcam.held), and the eye's largest change of velocity from one frame to the next (a cut or a
// shake is a spike there). Two legs on every map:
//   put   each of the map's jumps on its own: the kart put on the road 60 m before the take-off at 30 m/s, the gas held
//         and the steering assist on, one call for the whole run; frames from the race camera before, during and after
//         the first jump of each map are kept to look at (out/racer-jump-camera)
//   lap   one lap as a player drives it, the gas held and the steering assist on, the lap in ranges of 300 frames:
//         every jump on the way
//   fall  (the orbit, map 7) the kart put off the side of the road into space: the camera still holds where the fall
//         began and watches it drop away (jcam.fall_held, the hold a fall keeps), so a fall still never tumbles the view
// Over every jump flown (not one that ended in a respawn) the kart stays in the framing band: its middle within
// NDC_X of the screen's middle across and between NDC_Y_LO and NDC_Y_HI up and down, DIST_LO to DIST_HI metres from the
// eye; the camera is never held (0 frames); the eye's change of velocity in a frame stays under ACC_MAX mm per frame per
// frame. Control: sw jump_hold 1 (the flight held the camera at the lip like a fall, then cut back on the landing, as in
// v0.20.1) must fail the band on every map.
//   node tests/racer-jump-camera.mjs [--gl] [--webgpu] [--maps 0,1,...] [--legs put,lap,fall,control] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.RACER_SESSION || 'racer-jump-camera', OUT = join(ROOT, 'out/racer-jump-camera');
const AGENT = process.env.RACER_AGENT || 'racer-jump-camera';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/racer.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const MAPS = arg('--maps', '0,1,2,3,4,5,6,7').split(',').map(Number);
const LEGS = arg('--legs', 'put,lap,fall,control').split(',');
const NDC_X = 0.5, NDC_Y_LO = -0.8, NDC_Y_HI = 0.3, DIST_LO = 3, DIST_HI = 12, ACC_MAX = 250;
mkdirSync(OUT, { recursive: true });
let requests = 0;
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await post('host', '{"op":"shutdown"}'); } catch {}
  console.log(`romdev requests: ${requests}`);
  process.exit(code);
};
// (node:http, not fetch: fetch gives up on a reply that takes five minutes, and loading the cart on a busy GPU can)
const post = (name, body) => new Promise((ok, no) => {
  requests++;
  const q = request(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': AGENT } }, (res) => {
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
  let j; try { j = JSON.parse(r.text); } catch { j = r.text; }
  if (r.status !== 200 || j?.error) { console.error(`romdev ${name}: ${r.text.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
/** every debug field in one read */
const state = async () => Object.fromEntries((await tool('wasm', { op: 'debugState' })).fields.map((f) => [f.name, f.values ?? f.value]));
const cmd = (text) => tool('wasm', { op: 'command', text });
const step = (frames) => tool('frame', { op: 'step', frames });
const shot = (frames, path) => tool('frame', { op: 'stepAndShot', frames, path });
const gas = (on) => tool('input', { op: 'set', ports: [on ? { south: true } : {}, {}] });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function race(map, hold) {
  await gas(false);
  await cmd(`sw jump_hold ${hold ? 1 : 0}`);
  await cmd(`race ${map} 1 1 0 1`); /* one human, one lap, the first class, the steering assist on */
  let phase = 0;
  for (let f = 0; f < 1500 && phase !== 2; f += 30) { await step(30); phase = +(await rd('race.phase')); }
  if (phase !== 2) { console.error(`map ${map}: no race started (phase ${phase})`); await bye(3); }
  await gas(true);
  await step(30);
}
/** the records since `jcam reset` */
function records(s) {
  const out = [];
  for (let j = 0; j < +s['jcam.done']; j++)
    out.push({ s: s['jcam.s'][j], frames: s['jcam.frames'][j], air: s['jcam.air'][j], held: s['jcam.held'][j], fell: !!s['jcam.fell'][j], speed: s['jcam.speed'][j],
      x: [s['jcam.ndc'][j * 4] / 1000, s['jcam.ndc'][j * 4 + 1] / 1000], y: [s['jcam.ndc'][j * 4 + 2] / 1000, s['jcam.ndc'][j * 4 + 3] / 1000],
      dist: [s['jcam.dist'][j * 2] / 1000, s['jcam.dist'][j * 2 + 1] / 1000], acc: s['jcam.acc'][j], accAt: s['jcam.acc_at'][j], kacc: s['jcam.kacc'][j], lift: s['jcam.lift'][j] });
  return out;
}
const inBand = (r) => r.held === 0 && Math.max(-r.x[0], r.x[1]) <= NDC_X && r.y[0] >= NDC_Y_LO && r.y[1] <= NDC_Y_HI && r.dist[0] >= DIST_LO && r.dist[1] <= DIST_HI && r.acc <= ACC_MAX;
const desc = (r) => `s ${r.s} m at ${r.speed} m/s, ${r.air} ticks in the air, ${r.frames} frames: x ${r.x[0].toFixed(2)}..${r.x[1].toFixed(2)}, y ${r.y[0].toFixed(2)}..${r.y[1].toFixed(2)}, ` +
  `${r.dist[0].toFixed(1)}-${r.dist[1].toFixed(1)} m from the eye, held ${r.held} frames, eye change ${r.acc} mm/f2 (frame ${r.accAt}, the kart's own ${r.kacc}; lifted over the road ${r.lift} mm at most)${r.fell ? ', ended in a respawn' : ''}`;

/** each jump of the map from 60 m before its take-off; frames of the first one */
async function puts(backend, map, hold) {
  await race(map, hold);
  const s = await state();
  const jumps = s['jumps.s0'].slice(0, +s['jumps.n']);
  const recs = [];
  for (const [g, s0] of jumps.entries()) {
    await cmd(`put ${s0 - 60} 30`);
    await step(10);
    await cmd('jcam reset');
    if (g === 0) {
      const tag = `${backend}-m${map}${hold ? '-jump_hold' : ''}`;
      await shot(50, join(OUT, `${tag}-1before.png`)); /* about at the lip */
      await shot(40, join(OUT, `${tag}-2rise.png`));
      await shot(40, join(OUT, `${tag}-3top.png`));
      await shot(40, join(OUT, `${tag}-4fall.png`));
      await shot(50, join(OUT, `${tag}-5after.png`));
      await step(60);
    } else await step(280);
    const r = records(await state());
    if (!r.length) recs.push({ s: s0, none: true });
    else recs.push(r[0]);
  }
  return { jumps, recs };
}
/** one lap, the gas held: the records of every jump on the way */
async function lap(map) {
  await race(map, false);
  await cmd('jcam reset');
  let phase = 2;
  for (let f = 0; f < 60 * 240 && phase !== 3; f += 300) { await step(300); phase = +(await rd('race.phase')); }
  const s = await state();
  return { done: phase === 3, jumps: +s['jumps.n'], recs: records(s), road: +s['jcam.acc_road'] };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(120);
  for (const map of MAPS) {
    if (LEGS.includes('put')) {
      const p = await puts(backend, map, false);
      check(`map ${map}: the map has jumps`, p.jumps.length > 0, `${p.jumps.length} (take-offs at ${p.jumps.join(', ')} m)`);
      for (const r of p.recs) {
        if (r.none) { check(`map ${map} jump at ${r.s} m: flown`, false, 'no record (the kart never left the ground over it)'); continue; }
        check(`map ${map} jump at ${r.s} m: the kart flies it`, !r.fell, desc(r));
        if (!r.fell) check(`map ${map} jump at ${r.s} m: the chase camera keeps the kart framed and moves smoothly`, inBand(r), desc(r));
      }
    }
    if (LEGS.includes('lap')) {
      const q = await lap(map);
      const flown = q.recs.filter((r) => !r.fell);
      check(`map ${map}: a lap with the gas held flies its jumps`, q.done && flown.length >= 1, `lap done ${q.done}, ${q.recs.length} jumps recorded (${q.jumps} on the main road), ${q.recs.length - flown.length} ended in a respawn; the eye's largest change on the road ${q.road} mm/f2`);
      for (const r of flown) check(`map ${map} lap, jump at ${r.s} m: the chase camera keeps the kart framed and moves smoothly`, inBand(r), desc(r));
    }
    if (LEGS.includes('fall') && map === 7) {
      await race(map, false);
      await cmd('jcam reset');
      await cmd('put 300 30 16'); /* 16 m right of the centre line: off the road, over nothing */
      await step(150);
      const s = await state();
      check(`map ${map}: a fall off the side still holds the camera where it began`, +s['jcam.fall_held'] >= 20, `held ${s['jcam.fall_held']} frames, ${s['karts.falls'][0]} falls`);
    }
    if (LEGS.includes('control')) {
      const c = await puts(backend, map, true);
      const flown = c.recs.filter((r) => !r.none && !r.fell);
      check(`control: sw jump_hold 1, map ${map}: the band catches the camera held at the lip`, flown.length > 0 && flown.every((r) => !inBand(r)), flown.map(desc).join('; ') || 'no jump flown');
      await cmd('sw jump_hold 0');
    }
  }
}
await gas(false);
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
