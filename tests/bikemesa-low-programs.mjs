// BikeMesa's cart has a program for everything a newcomer's first minutes draw at Very Low and Low, on GL and WebGPU
// through romdev: the title, the rider pick alone and with a second pad joined (split screen), and the first seconds
// of track 1. Auto steps a busy machine down to these presets, where a missing state is a red "no program" banner over
// the rider pick and the grass left undrawn (seen on the v0.19.7 cart: 2 states at Low on WebGPU). The pipeline's own
// report is read every second (cw.three.missing, cw.three.missing_states) and must stay empty.
// Control: --control-cart <a cart built before the fix> must report the missing states on WebGPU.
//   node tests/bikemesa-low-programs.mjs [--gl] [--webgpu] [--cart <path>] [--control-cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.BIKEMESA_SESSION || 'bikemesa-low-programs', OUT = join(ROOT, 'out/bikemesa-low-programs');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/bikemesa.wasc')));
const CONTROL = arg('--control-cart', null);
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  let r;
  try { r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) }); }
  catch (e) { console.error(`romdev ${name}: ${e.cause?.message || e.message} (the server is down)`); await bye(3); }
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
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

/* the most states the pipeline reported missing at once, and their names */
async function survey(acc) {
  const n = +(await rd('cw.three.missing'));
  if (n > 0) {
    const t = (await tool('wasm', { op: 'read', name: 'cw.three.missing_states' })).text ?? '';
    for (const s of String(t).split('\n').filter(Boolean)) acc.states.add(s);
  }
  acc.max = Math.max(acc.max, n);
  acc.quality.add(+(await rd('cw.quality')));
}

async function run(cart, backend, quality, two, tag) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(20);
  await tool('wasm', { op: 'command', text: `quality ${quality}` });
  await tool('wasm', { op: 'command', text: 'quality lock' });
  const acc = { max: 0, states: new Set(), quality: new Set() };
  await step(60); await survey(acc);                         /* the title */
  await press(0, 'south');
  if (two) await press(1, 'south');                          /* a second pad joins: the pick in split screen */
  for (let i = 0; i < 3; i++) { await press(0, 'south'); await survey(acc); }
  for (let f = 0; f < 360; f += 60) { await step(60); await survey(acc); }
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-pick.png`) });
  if (two && +(await rd('game.state')) === 7) await press(0, 'south'); /* "(A) start now" */
  await step(60);
  if (+(await rd('game.state')) === 1) await press(0, 'south');        /* track 1 */
  for (let f = 0; f < 600; f += 60) { await hold(60, { south: true }, {}); await survey(acc); }
  await pads({}, {});
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-ride.png`) });
  return { ...acc, state: +(await rd('game.state')) };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  for (const q of ['verylow', 'low']) for (const two of [false, true]) {
    const tag = `${q}-${two ? 'two' : 'one'}`;
    const r = await run(CART, backend, q, two, tag);
    check(`${q}, ${two ? 'a second pad joined (split pick)' : 'one seat'}: every state has a program`, r.max === 0 && r.state === 3,
      `most missing at once ${r.max}${r.states.size ? ' (' + [...r.states].join(', ') + ')' : ''}, presets seen ${[...r.quality].join(',')}, riding (state ${r.state})`);
  }
  if (CONTROL && backend === 'webgpu') {
    const c = await run(resolve(CONTROL), backend, 'low', true, 'control');
    check('control: the cart from before the fix reports missing states at Low', c.max > 0, `most missing at once ${c.max} (${[...c.states].join(', ')})`);
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
