// derby on the cart through romdev, GL and WebGPU: the opening a newcomer survives, and damage that drives.
//
// study opening 20 runs twenty quick matches on the first arena's field without drawing (games/derby/study.c), the
// player a newcomer in the default car (the sedan, now the light class: quick, nimble): slow off the line, part
// throttle, lagging hands, aiming at the nearest car and letting go of the gas after every hit. Each bout must leave
// the newcomer at least half an engine ten seconds after the flag, none wrecked inside 20 s, the median wreck at 45 s
// or later. Control: sw no_classes 1 (every body drives alike, as before the classes) must break it: the newcomer
// was out within seconds in some bouts.
//
// study pull drives a sedan straight with the gas held after three hits on one side: a hit beside a front wheel bends
// it (the car pulls toward that side), a battered flank's tyres grip less, a failing engine tops out lower. Hits on
// the left must turn it left by at least 8 degrees in 2.5 s, the right as far right, an undamaged car under 1 degree.
// Control: sw no_handling_damage 1 must leave every damaged car running straight.
//
// study classes 200 runs 200 bouts of six CPU pros with the four bodies rotated through the grid, and a test drive of
// each: the light sedan pulls hardest and tops out highest, the heavy van takes a door hit best, and no body's wins per
// start are more than 20% off the field's mean. Control: sw no_classes 1 (every body drives alike) must show the
// bodies' pull and toughness the same.
//
// The studies must agree bit for bit between GL and WebGPU (the simulation is deterministic on every host).
//   node tests/derby-opening.mjs [--gl] [--webgpu] [--cart <path>]
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.DERBY_SESSION || 'derby-opening';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/derby.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args), signal: AbortSignal.timeout(300000) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const cmd = async (text) => {
  const j = await tool('wasm', { op: 'command', text });
  const v = j.reply ?? j.result ?? j.value ?? j;
  return typeof v === 'string' ? JSON.parse(v) : v;
};
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

const opening = (o) => {
  const b = o.bouts, n = b.length, ticks = b.map((x) => (x.wrecked < 0 ? 180 * 60 : x.wrecked)).sort((p, q) => p - q);
  const r = {
    low: b.filter((x) => x.hp10 < 50).length, early: b.filter((x) => x.wrecked >= 0 && x.wrecked < 20 * 60).length,
    median: ticks[n >> 1] / 60, first: ticks[0] / 60, hunters: b.reduce((s, x) => s + x.hunters10, 0) / n, n,
  };
  r.ok = n === 20 && r.low === 0 && r.early === 0 && r.median >= 45;
  r.text = `${n} bouts: ${r.low} under half an engine 10 s in, ${r.early} wrecked inside 20 s (first ${r.first.toFixed(1)} s), median wreck ${r.median.toFixed(1)} s, ${r.hunters.toFixed(2)} CPU drivers after the player`;
  return r;
};
const pull = (p) => {
  const r = { left: p.left.turned, right: p.right.turned, none: p.none.turned, frontTop: p.front.top, noneTop: p.none.top, gripL: p.left.grip_l, gripR: p.left.grip_r };
  r.ok = r.left >= 8 && r.right <= -8 && Math.abs(r.none) < 1 && r.frontTop < r.noneTop - 0.3 && r.gripL < r.gripR;
  r.straight = Math.abs(r.left) < 1 && Math.abs(r.right) < 1;
  r.text = `left hits turned it ${r.left.toFixed(1)} deg, right ${r.right.toFixed(1)}, undamaged ${r.none.toFixed(1)}; top speed ${r.noneTop.toFixed(1)} undamaged, ${r.frontTop.toFixed(1)} with a battered nose; left-flank grip ${r.gripL.toFixed(2)} vs ${r.gripR.toFixed(2)}`;
  return r;
};

const classes = (c) => {
  const k = c.kinds, rate = k.map((x) => x.wins / x.starts), mean = rate.reduce((a, b) => a + b, 0) / rate.length;
  const spread = Math.max(...rate.map((r) => Math.abs(r / mean - 1)));
  const by = Object.fromEntries(k.map((x) => [x.name, x]));
  const distinct = by.sedan.speed2 > by.van.speed2 + 1 && by.sedan.top > by.van.top + 1 && by.van.hp_after_hit > by.sedan.hp_after_hit + 1;
  const same = Math.max(...k.map((x) => x.speed2)) - Math.min(...k.map((x) => x.speed2)) < 0.3 && Math.max(...k.map((x) => x.hp_after_hit)) - Math.min(...k.map((x) => x.hp_after_hit)) < 0.5;
  return { ok: spread <= 0.2 && distinct, same, text: k.map((x) => `${x.name} ${x.class} ${x.wins}/${x.starts} wins, ${x.speed2.toFixed(1)} m/s at 2 s, top ${x.top.toFixed(1)}, ${x.hp_after_hit.toFixed(1)} hp after a door hit`).join('; ') + `; the widest win rate ${(spread * 100).toFixed(0)}% off the mean` };
};
const seen = {};
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await tool('frame', { op: 'step', frames: 60 });
  const o = await cmd('study opening 20'), op = opening(o);
  check('a newcomer survives the opening', op.ok, op.text);
  const p = await cmd('study pull'), pp = pull(p);
  check('damage drives: a bent wheel pulls, a battered flank grips less, a hurt engine tops out lower', pp.ok, pp.text);
  await cmd('sw no_classes 1');
  const oc = opening(await cmd('study opening 20'));
  check('control: sw no_classes 1 (the bodies as they were) fails the opening', !oc.ok, oc.text);
  await cmd('sw no_classes 0');
  await cmd('sw no_handling_damage 1');
  const pc = pull(await cmd('study pull'));
  check('control: sw no_handling_damage 1 runs straight', pc.straight && !pc.ok, pc.text);
  await cmd('sw no_handling_damage 0');
  const c = await cmd('study classes 200'), cc = classes(c);
  check('three weight classes: the light one quick, the heavy one tough, wins within 20% of each other', cc.ok, cc.text);
  await cmd('sw no_classes 1');
  const nc = classes(await cmd('study classes 1'));
  check('control: sw no_classes 1 drives every body alike', nc.same && !nc.ok, nc.text);
  await cmd('sw no_classes 0');
  seen[backend] = JSON.stringify([o, p, c]);
  await tool('host', { op: 'shutdown' });
}
if (seen.gl && seen.webgpu) check('GL and WebGPU agree bit for bit', seen.gl === seen.webgpu, seen.gl === seen.webgpu ? 'identical studies' : 'the studies differ');
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
