// pugball on the cart through romdev, GL and WebGPU: the party game's floor. Each part has its control, the old
// behaviour brought back by a `sw` switch, which must fail where the part passes.
//
//   exhibition  a fresh save's Exhibition opens every stadium: stadium 2 kicks off from the select (control:
//               sw exhibition_locked 1, the padlock bumps and the select stays)
//   kickoff     the countdown and "kick off the cat!" never cover the cat: at each second of the countdown the
//               block (hud.kick_rect) keeps clear of the cat's circle on screen (hud.kick_cat) (control:
//               sw kick_dodge 0, the block in the middle over the cat)
//   goal mouth  P1's pug parked in its own goal mouth facing out, and in each corner of it: the chase camera keeps
//               the pug in the picture with nothing of the posts or the crossbar in front of it (cam.pug_seen)
//               (control: sw cam_posts 0, the walls alone, as the clearance test once was)
//   fov kick    boosting from rest to supersonic opens P1's view 6 degrees or more (cam.fov) (control: sw fov_kick 0)
//   moon cat    the party rules MOON CAT, picked on the team sheet with the KICK OFF row's {m_alt}: the cat dropped
//               from 8 m stays up at least half as long again as under NORMAL (game.rules)
//   node tests/pugball-feel.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.PUGBALL_SESSION || 'pugball-feel', OUT = join(ROOT, 'out/pugball-feel');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/pugball.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
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
const cmd = (text) => tool('wasm', { op: 'command', text });
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0) => tool('input', { op: 'set', ports: [p0, {}] });
const press = async (b, after = 14) => { await pads({ [b]: true }); await step(4); await pads({}); await step(after); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const load = async (backend, pre = []) => {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(30);
  await cmd('progress preset 0'); // a fresh save
  for (const p of pre) await cmd(p);
};
const until = async (state, b) => {
  for (let n = 0; n < 12 && (await rd('game.state')) !== state; n++) { await press(b); await step(16); }
  return (await rd('game.state')) === state;
};
// title -> EXHIBITION -> the team sheet; returns the CPUs' levels
const toSheet = async () => { if (!(await until(1, 'south'))) { console.error('no team sheet'); await bye(3); } return rd('players.slot'); };

async function exhibition(backend, ctl) {
  await load(backend, ctl ? ['sw exhibition_locked 1'] : []);
  const slots = await toSheet();
  if (!(await until(2, 'start'))) { console.error('no stadium select'); await bye(3); }
  await press('right'); // stadium 2
  await press('south', 30);
  await step(30);
  const r = { slots, state: +(await rd('game.state')), stadium: +(await rd('game.stadium')) };
  await tool('host', { op: 'shutdown' });
  return r;
}

async function match(backend, pre, stadium = 1) {
  await load(backend, pre);
  await cmd(`stadium ${stadium}`);
  for (let f = 0; f < 120 && +(await rd('game.state')) !== 3; f += 10) await step(10);
}

async function kickoff(backend, ctl) {
  await match(backend, ctl ? ['sw kick_dodge 0'] : []);
  let worst = Infinity, n = 0;
  for (let f = 0; f < 200; f += 20) {
    if (+(await rd('match.phase')) !== 0) break;
    await step(20);
    const r = await rd('hud.kick_rect'), c = await rd('hud.kick_cat');
    if (!c[2]) continue;
    const nx = Math.max(r[0], Math.min(c[0], r[0] + r[2])), ny = Math.max(r[1], Math.min(c[1], r[1] + r[3]));
    worst = Math.min(worst, Math.hypot(c[0] - nx, c[1] - ny) - c[2]);
    n++;
    if (f === 100) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-kickoff${ctl ? '-control' : ''}.png`) });
  }
  await tool('host', { op: 'shutdown' });
  return { worst, n };
}

const SPOTS = [[0, -32.5, 0], [-4.6, -32.5, 0], [4.6, -32.5, 0], [0, -34, 0]];
async function goalMouth(backend, ctl) {
  await match(backend, ctl ? ['sw cam_posts 0'] : []);
  for (let f = 0; f < 300 && +(await rd('match.phase')) === 0; f += 10) await step(10);
  const seen = [];
  for (const [x, z, yaw] of SPOTS) {
    await cmd(`place ${x} ${z} ${yaw}`);
    let ok = 0, all = 0;
    for (let f = 0; f < 60; f += 6) { await step(6); all++; ok += +(await rd('cam.pug_seen')); }
    seen.push(`${ok}/${all}`);
    if (x === 0 && z === -32.5) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-goalmouth${ctl ? '-control' : ''}.png`) });
  }
  await tool('host', { op: 'shutdown' });
  return seen;
}

async function fovKick(backend, ctl) {
  await match(backend, ctl ? ['sw fov_kick 0'] : []);
  for (let f = 0; f < 300 && +(await rd('match.phase')) === 0; f += 10) await step(10);
  const f0 = +(await rd('cam.fov'));
  await pads({ r: true, east: true });
  let f1 = f0;
  for (let f = 0; f < 150; f += 10) { await step(10); f1 = Math.max(f1, +(await rd('cam.fov'))); }
  await pads({});
  await tool('host', { op: 'shutdown' });
  return { f0, f1 };
}

async function hang(backend, moon) {
  await load(backend);
  await toSheet();
  if (moon) {
    await press('up', 6); // the KICK OFF row (up from the first pug wraps to it)
    await press('west'); // {m_alt}: the next party rules
  }
  const rules = +(await rd('game.rules'));
  if (!(await until(2, 'start'))) { console.error('no stadium select'); await bye(3); }
  await press('south', 30);
  for (let f = 0; f < 300 && +(await rd('match.phase')) === 0; f += 10) await step(10);
  await cmd('dropball 14 8 18'); // (out of the CPUs' way)
  let frames = 0;
  for (; frames < 400; frames += 2) { await step(2); if ((await rd('ball.pos'))[1] < 160) break; }
  await tool('host', { op: 'shutdown' });
  return { rules, frames };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const ex = await exhibition(backend, false), exc = await exhibition(backend, true);
  check('the Exhibition plays stadium 2 on a fresh save', ex.state === 3 && ex.stadium === 2, `game.state ${ex.state}, stadium ${ex.stadium}`);
  check('control: sw exhibition_locked 1 keeps the padlock', exc.state === 2, `game.state ${exc.state}`);
  const k = await kickoff(backend, false), kc = await kickoff(backend, true);
  check('the kickoff countdown keeps off the cat', k.n >= 3 && k.worst > 0, `${k.n} countdown frames, nearest ${k.worst.toFixed(0)} px clear of the cat`);
  check('control: sw kick_dodge 0 covers the cat', kc.n >= 3 && kc.worst <= 0, `nearest ${kc.worst.toFixed(0)} px`);
  const g = await goalMouth(backend, false), gc = await goalMouth(backend, true);
  const full = (a) => a.every((x) => { const [p, q] = x.split('/').map(Number); return p === q; });
  check('in the goal mouth the camera keeps the pug in the picture, clear of the frame', full(g), g.join(', '));
  check('control: sw cam_posts 0 loses it', !full(gc), gc.join(', '));
  const fk = await fovKick(backend, false), fkc = await fovKick(backend, true);
  check('boosting to supersonic opens the view', fk.f1 - fk.f0 >= 6, `${fk.f0.toFixed(1)} -> ${fk.f1.toFixed(1)} degrees`);
  check('control: sw fov_kick 0 holds it', fkc.f1 - fkc.f0 < 0.5, `${fkc.f0.toFixed(1)} -> ${fkc.f1.toFixed(1)} degrees`);
  const hn = await hang(backend, false), hm = await hang(backend, true);
  check('MOON CAT keeps the cat up half as long again', hm.rules === 1 && hn.rules === 0 && hm.frames >= hn.frames * 1.5, `NORMAL ${hn.frames} frames to fall 8 m, MOON CAT ${hm.frames}`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
