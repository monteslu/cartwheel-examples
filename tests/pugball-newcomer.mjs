// pugball on the cart through romdev, GL and WebGPU, by the pad: a newcomer's first kickoff, alone and with a second
// pad that joined on the team sheet and was never played (two players: split screen). The player knows only what the
// screen says. The first kickoff shows the controls line ("{thr_fwd} go ... {steer_l|steer_r} steer"); a player who
// sees it holds the throttle it shows (the right trigger) and steers at the CAT marker; one who does not pushes the
// stick forward, as anyone would try first, and steers with it. Either way the goal is to reach the cat at kickoff.
// Split screen once left the line out, so two newcomers never learned that the throttle is on the triggers.
// Control: sw split_how 0 (the line left out of split screen, as before) must leave P1 short of the cat.
//   node tests/pugball-newcomer.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.PUGBALL_SESSION || 'pugball-newcomer', OUT = join(ROOT, 'out/pugball-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/pugball.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const URL = process.env.ROMDEV_URL || ROMDEV_URL;
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(ROMDEV_URL+'/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`${URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
// several fields in one call
const rds = async (...names) => {
  const v = await tool('wasm', { op: 'debugState' }), o = {};
  for (const f of v.fields) if (names.includes(f.name)) o[f.name] = f.values ?? f.value;
  return o;
};
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
/* ranges, not loops: the pad held for a stretch of frames and those frames stepped, in one request */
const hold = (frames, p0, p1 = {}) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, p1] }, frames }] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await hold(4, ...a); await hold(10, {}, {}); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const REACH = 450; // the cat's radius and a pug's half length, with room: P1 is at the cat

// from the title: EXHIBITION, the team sheet (pad 2 joins when idle), kick off, the first stadium; then P1 plays the
// kickoff as the screen taught it (or did not)
async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(30);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  // each screen pressed until it gives way to the next (game.state: 0 title, 1 team sheet, 2 stadium, 3 match)
  const until = async (state, port, b) => {
    for (let n = 0; n < 12 && (await rd('game.state')) !== state; n++) { await press(port, b); await step(20); }
    return (await rd('game.state')) === state;
  };
  if (!(await until(1, 0, 'south'))) { console.error('the team sheet never opened'); await bye(3); } // EXHIBITION (selected already)
  // pads 2-4: A joins, B leaves (the sheet keeps the last match's players, so a seat may be taken already)
  await step(20);
  const humans = async () => (await rd('players.slot')).filter((k) => k === 1).length;
  for (let n = 0; n < 4 && (await humans()) !== (idle ? 2 : 1); n++) { await press(1, idle ? 'south' : 'east'); await step(10); }
  if ((await humans()) !== (idle ? 2 : 1)) { console.error('the team sheet would not seat', idle ? 2 : 1, 'player(s)'); await bye(3); }
  if (!(await until(2, 0, 'start'))) { console.error('Start never kicked off'); await bye(3); }     // the team sheet: Start kicks off
  if (!(await until(3, 0, 'south'))) { console.error('the stadium never opened'); await bye(3); }   // the first stadium
  const views = +(await rd('cam.views'));
  // the countdown: what the screen shows P1
  let shown = 0;
  for (let f = 0; f < 400; f += 20) {
    const k = await rds('match.phase', 'hud.how_views');
    if (k['match.phase'] !== 0) break;
    shown = Math.max(shown, +k['hud.how_views']);
    if (f === 60) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-kickoff.png`) });
    await step(20);
  }
  const taught = shown >= Math.max(1, views);
  // GO: to the cat
  let prev = (await rds('players.pos'))['players.pos'], best = Infinity, frames = 0;
  for (; frames < 60 * 6 && best > REACH; frames += 8) {
    const k = await rds('players.pos', 'ball.pos'), p = k['players.pos'], b = k['ball.pos'];
    const dx = b[0] - p[0], dz = b[2] - p[2];
    best = Math.min(best, Math.hypot(dx, dz));
    const hx = p[0] - prev[0], hz = p[2] - prev[2];
    const moving = Math.hypot(hx, hz) > 1;
    const turn = moving ? Math.sign(hx * dz - hz * dx) : 0; // steer toward the CAT marker
    prev = p;
    await pads(taught ? { axes: { rt: 1, lx: turn * 0.6 } } : { axes: { ly: -1, lx: turn * 0.6 } }, {});
    await step(8);
  }
  await pads({}, {});
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}.png`) });
  return { views, shown, taught, best: Math.round(best), frames };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('alone: the kickoff shows how to drive and P1 reaches the cat', one.views === 0 && one.taught && one.best <= REACH, `views ${one.views}, the controls line in ${one.shown} view(s), P1 ${one.best} from the cat after ${one.frames} frames`);
  const two = await run(backend, 'idle-seat', true);
  check('a second pad that joined and idles: both views show how to drive, P1 reaches the cat', two.views === 2 && two.shown === 2 && two.best <= REACH, `views ${two.views}, the controls line in ${two.shown} view(s), P1 ${two.best} from the cat after ${two.frames} frames`);
  const ctl = await run(backend, 'control', true, 'sw split_how 0');
  check('control: sw split_how 0, no controls line in split screen, P1 never reaches the cat', ctl.views === 2 && ctl.shown === 0 && ctl.best > REACH, `views ${ctl.views}, the controls line in ${ctl.shown} view(s), P1 ${ctl.best} from the cat after ${ctl.frames} frames`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
