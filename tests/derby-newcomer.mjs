// derby on the cart through romdev, GL and WebGPU, by the pad: a newcomer starts a quick match from the title and is
// shown which buttons drive. The controls hint ({thr_fwd} gas, {thr_back} brake / reverse, steer, handbrake, look
// back) stays up from the countdown until that player gives it gas (or ten seconds into the bout), on a single screen
// and in every split view, on that player's own device. The run follows it: P1 waits a second past the flag, reads
// the hint, holds the gas it names and drives off; the hint then leaves P1's view. With a second pad that joined on
// the title and is never played, both views draw the hint and P1 drives off the same way.
// Control: sw hint_countdown 1 (the hint only through the countdown and never in a split view, as the game once drew
// it) must leave both split views without the controls once the flag drops.
//   node tests/derby-newcomer.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.DERBY_SESSION || 'derby-newcomer', OUT = join(ROOT, 'out/derby-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/derby.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); process.exit(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await pads(...a); await step(4); await pads({}, {}); await step(20); };
const PH_GO = 1;
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); process.exit(3); }
  await step(240);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  if (idle) await press(1, 'south');                        /* "P2 (A) JOIN", then never touched again */
  await press(0, 'south');                                  /* "(A) drive": QUICK MATCH */
  for (let f = 0; f < 600 && +(await rd('bout.phase')) !== PH_GO; f += 10) await step(10);
  const views = +(await rd('views'));
  const atFlag = await rd('hint.frames');
  await step(60);                                           /* a newcomer looks for the gas */
  const read = await rd('hint.frames');
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-hint.png`) });
  const pos0 = (await rd('cars.pos')).slice(0, 2);
  await pads({ r: true }, {}); await step(90);              /* the gas the hint names */
  const afterGas = await rd('hint.frames');
  await step(60);
  const later = await rd('hint.frames');
  await pads({}, {});
  const pos1 = (await rd('cars.pos')).slice(0, 2);
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-drive.png`) });
  const moved = Math.hypot(pos1[0] - pos0[0], pos1[1] - pos0[1]);
  return { views, past: [0, 1].map((p) => read[p] - atFlag[p]), p1Gone: later[0] === afterGas[0], p2Still: later[1] > afterGas[1], moved };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('one seat: the controls stay up past the flag until P1 gives it gas', one.views === 1 && one.past[0] > 30 && one.p1Gone && one.moved > 300,
    `${one.views} view, ${one.past[0]} hint frames in the second after the flag, gone after the gas ${one.p1Gone}, P1 moved ${one.moved.toFixed(0)}`);
  const two = await run(backend, 'idle-seat', true);
  check('a second seat that joined and idles: both views show the controls, P1 drives off', two.views === 2 && two.past[0] > 30 && two.past[1] > 30 && two.p1Gone && two.p2Still && two.moved > 300,
    `${two.views} views, hint frames after the flag P1 ${two.past[0]} P2 ${two.past[1]}, P1's gone after the gas ${two.p1Gone}, P2's still up ${two.p2Still}, P1 moved ${two.moved.toFixed(0)}`);
  const ctl = await run(backend, 'control', true, 'sw hint_countdown 1');
  check('control: sw hint_countdown 1, the split views show no controls past the flag', ctl.views === 2 && ctl.past[0] === 0 && ctl.past[1] === 0,
    `${ctl.views} views, hint frames after the flag P1 ${ctl.past[0]} P2 ${ctl.past[1]}`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
