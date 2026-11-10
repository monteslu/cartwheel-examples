// spout on the cart through romdev, GL and WebGPU, by the pad: a newcomer climbs the first spout by what the screen
// shows, alone and with a second pad that pressed (A) on the title and then idles (spout is one player: the second
// pad must change nothing). The climber holds up on the stick, as anyone would; braces ({brace}, the controls line)
// when the screen shows a white wave front coming down the stream it is in (hud.wave_near is that front, drawn);
// hops ({jump}) when it has not gained height for a second (a slick band). It must reach the top within 60 s with no
// wash. The rain now pushes a stream across the climber's line, so the waves must be read.
// Control: sw hide_waves 1 (the wave fronts undrawn) gets the same newcomer washed down.
//   node tests/spout-newcomer.mjs [--gl] [--webgpu] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.SPOUT_SESSION || 'spout-newcomer', OUT = join(ROOT, 'out/spout-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/spout.wasc')));
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
const state = async () => { const v = await tool('wasm', { op: 'debugState' }), o = {}; for (const f of v.fields) o[f.name] = f.values ?? f.value; return o; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
/* ranges, not loops: the pad held for a stretch of frames and those frames stepped, in one request */
const hold = (frames, p0, p1 = {}) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, p1] }, frames }] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await hold(4, ...a); await hold(20, {}, {}); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  if (idle) await press(1, 'south'); // a second pad, then never again
  for (let n = 0; n < 10 && (await state())['game.state'] !== 2; n++) await press(0, 'south'); // title, then the first spout
  let k = await state(), still = 0, best = 0, f = 0;
  for (; f < 60 * 60 && k['game.state'] === 2; f += 6) {
    const y = k['spider.pos'][1];
    if (y > best + 0.05) best = y, still = 0; else still += 6;
    const brace = !!k['hud.wave_near'], hop = !brace && still >= 60;
    if (hop) still = 0;
    await pads({ up: !brace, east: brace, south: hop });
    await step(6);
    k = await state();
    if (f === 900) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-climb.png`) });
  }
  await pads({}, {});
  const r = { state: k['game.state'], washes: k['washes'], knocks: k['knocks'], secs: f / 60, height: best };
  await tool('host', { op: 'shutdown' });
  return r;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const a = await run(backend, 'alone', false);
  check('a newcomer climbs the first spout by the screen', a.state === 4 && a.washes === 0 && a.secs <= 60, `game.state ${a.state} after ${a.secs.toFixed(1)} s, ${a.washes} wash(es), ${a.knocks} knock(s)`);
  const b = await run(backend, 'idle-seat', true);
  check('a second pad that pressed once changes nothing', b.state === 4 && b.washes === a.washes && Math.abs(b.secs - a.secs) < 1, `game.state ${b.state} after ${b.secs.toFixed(1)} s, ${b.washes} wash(es)`);
  const c = await run(backend, 'control', false, 'sw hide_waves 1');
  check('control: sw hide_waves 1 (no wave fronts to read) gets the newcomer washed down', c.washes >= 1, `${c.washes} wash(es), game.state ${c.state} after ${c.secs.toFixed(1)} s`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
