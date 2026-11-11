// bikemesa on the cart through romdev, GL and WebGPU, by the pad and the screen's own prompts: from the title the
// first track is ridden to the results, alone and with a second pad that joined on the rider pick and was never
// played. Once a seat still choosing has not pressed anything for five seconds, P1's READY panel offers "(A) start
// now"; P1 presses it, the idle seat sits the race out and P1 rides Red Rock Run to the finish. (A seat still picking
// is waited for.)
// Control: sw idle_wait 1 (a joined seat that never readies holds the start, as the game once did) must leave P1
// on the pick with the race never started.
//   node tests/bikemesa-newcomer.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.BIKEMESA_SESSION || 'bikemesa-newcomer', OUT = join(ROOT, 'out/bikemesa-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/bikemesa.wasc')));
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
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await hold(4, ...a); await hold(20, {}, {}); };
const GS = { TITLE: 0, SELECT: 1, RIDE: 3, FINISH: 5, CHARACTER: 7 };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press(0, 'south');                                  /* title: "press (A) to ride" */
  if (idle) await press(1, 'south');                        /* "P2: (A) to join", then never touched again */
  const joined = +(await rd('players'));
  for (let i = 0; i < 3; i++) await press(0, 'south');      /* rider, bike, paint: READY! */
  await step(idle ? 300 : 40);                             /* P1 waits on the idle seat: after 5 s the panel offers "start now" */
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-ready.png`) });
  if (+(await rd('game.state')) === GS.CHARACTER) await press(0, 'south'); /* "(A) start now" */
  await step(60);
  const picked = +(await rd('game.state'));
  if (picked === GS.SELECT) await press(0, 'south');        /* track 1: "(A) ride" */
  let state = +(await rd('game.state'));
  for (let f = 0; f < 60 * 120 && state !== GS.FINISH && state !== GS.CHARACTER && state !== GS.SELECT; f += 120) {
    await hold(120, { south: true }, {});         /* pedal */
    state = +(await rd('game.state'));
  }
  await pads({}, {});
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-end.png`) });
  return { joined, picked, state, finished: +(await rd('finished')), checkpoint: +(await rd('checkpoint')), ticks: +(await rd('ride.ticks')), riders: +(await rd('players')) };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('one seat: track 1 ridden to the results', one.joined === 1 && one.state === GS.FINISH && one.finished === 1, `state ${one.state}, finished ${one.finished}, checkpoint ${one.checkpoint}, ${one.ticks} ride ticks`);
  const two = await run(backend, 'idle-seat', true);
  check('a second seat that joined and idles: P1 starts now and finishes', two.joined === 2 && two.picked === GS.SELECT && two.state === GS.FINISH && two.finished === 1 && two.riders === 1,
    `joined ${two.joined}, after "start now" state ${two.picked}, end state ${two.state}, finished ${two.finished}, ${two.riders} rider(s) raced`);
  const ctl = await run(backend, 'control', true, 'sw idle_wait 1');
  check('control: sw idle_wait 1, the idle seat holds P1 on the pick', ctl.joined === 2 && ctl.picked === GS.CHARACTER && ctl.finished === 0, `joined ${ctl.joined}, state ${ctl.picked} after P1's press, finished ${ctl.finished}`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
