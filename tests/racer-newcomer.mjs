// racer on the cart through romdev, GL and WebGPU, by the pad and the screen's own prompts: a race is started from the
// title and P1 drives off, alone (RACE) and in SPLIT SCREEN with a second pad that pressed (A) to join and was never
// played again. On the ride pick P1 readies; once the other seat has pressed nothing for five seconds, P1's READY!
// button reads "(A) START NOW"; P1 presses it, the idle seat sits the race out, and the race starts on one screen.
// Control: sw idle_wait 1 (the ride pick waits for every seat, as the game once did) must leave P1 on the ride pick.
//   node tests/racer-newcomer.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.RACER_SESSION || 'racer-newcomer', OUT = join(ROOT, 'out/racer-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/racer.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(ROMDEV_URL+'/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
/* ranges, not loops: the pad held for a stretch of frames and those frames stepped, in one request */
const hold = (frames, p0, p1 = {}) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, p1] }, frames }] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await hold(4, ...a); await hold(26, {}, {}); };
const M = { JOIN: 1, VEHICLE: 2, MAP: 3, RACE: 6 }, PH_RACE = 2;
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(180);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(0, 'south');                                   /* "PRESS (A)" */
  if (idle) {
    for (let i = 0; i < 4; i++) await press(0, 'down');      /* SPLIT SCREEN */
    await press(0, 'south');
    await press(1, 'south');                                 /* "Press (A) to join", then never touched again */
  }
  await press(0, 'south');                                   /* RACE, or CONTINUE */
  const humans = +(await rd('ui.humans'));
  await press(0, 'south');                                   /* "(A) READY" */
  await step(idle ? 330 : 30);                               /* P1 waits on the idle seat */
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-ready.png`) });
  if (+(await rd('ui.mode')) === M.VEHICLE) await press(0, 'south'); /* "(A) START NOW" */
  await step(30);
  const picked = +(await rd('ui.mode'));
  if (picked === M.MAP) await press(0, 'south');             /* "START RACE" ("(A) go") */
  let phase = 0, mode = picked; /* the menus' demo race runs behind them: only the race's own GO counts */
  for (let f = 0; f < 1200 && !(mode === M.RACE && phase === PH_RACE); f += 30) { await step(30); mode = +(await rd('ui.mode')); phase = +(await rd('race.phase')); }
  const prog0 = (await rd('karts.prog'))[0];
  await hold(300, { south: true }, {});          /* floor it */
  await pads({}, {});
  const prog1 = (await rd('karts.prog'))[0];
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-race.png`) });
  return { humans, picked, phase: mode === M.RACE ? phase : -1, racers: +(await rd('ui.humans')), views: +(await rd('views')), gained: prog1 - prog0 };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('one seat: a race starts and P1 drives off', one.humans === 1 && one.picked === M.MAP && one.phase === PH_RACE && one.views === 1 && one.gained > 300,
    `${one.humans} seat, after READY mode ${one.picked}, race phase ${one.phase}, ${one.views} view, P1 gained ${one.gained} on the road`);
  const two = await run(backend, 'idle-seat', true);
  check('a second seat that joined and idles: P1 starts now and drives off', two.humans === 2 && two.picked === M.MAP && two.phase === PH_RACE && two.racers === 1 && two.views === 1 && two.gained > 300,
    `${two.humans} seats on the pick, after "START NOW" mode ${two.picked}, race phase ${two.phase}, ${two.racers} racing on ${two.views} view, P1 gained ${two.gained}`);
  const ctl = await run(backend, 'control', true, 'sw idle_wait 1');
  check('control: sw idle_wait 1, the idle seat holds P1 on the ride pick', ctl.humans === 2 && ctl.picked === M.VEHICLE && ctl.phase !== PH_RACE,
    `${ctl.humans} seats, mode ${ctl.picked} after P1's press, race phase ${ctl.phase}`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
