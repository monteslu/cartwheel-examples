// derby on the cart through romdev, GL and WebGPU: a big hit lands with a beat, a thump in the pad and the crowd, and
// the camera never shakes. derby_ram parks a car broadside 18 m ahead of yours; the run holds the gas into its door.
// The first hit is over the beat's threshold: the bout must step one tick in three for a moment (beat.count, and
// beat.held ticks held), P1's pad must rumble (count.rumbles, and the engine's cw.rumble names pad 0), the hit must
// count. Control: sw no_beat 1 must leave the same hit with no beat.
// (The camera: games/derby has no shake path left; the chase rig's eye is its spring and nothing else.)
//   node tests/derby-beat.mjs [--gl] [--webgpu] [--cart <path>]
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.DERBY_SESSION || 'derby-beat';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/derby_ram.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
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
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0) => tool('input', { op: 'set', ports: [p0, {}] });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function run(backend, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(240);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  for (let n = 0; n < 10 && +(await rd('game.state')) !== 1; n++) { await pads({ south: true }); await step(4); await pads({}); await step(20); }
  for (let f = 0; f < 600 && +(await rd('bout.phase')) !== 1; f += 10) await step(10);
  await pads({ r: true });
  let hit = 0;
  for (let f = 0; f < 300 && !hit; f += 5) { await step(5); hit = +(await rd('count.max_hit')); }
  await step(30);
  await pads({});
  const r = { maxHit: hit, beats: +(await rd('beat.count')), held: +(await rd('beat.held')), rumbles: +(await rd('count.rumbles')), rumble: await rd('cw.rumble') };
  await tool('host', { op: 'shutdown' });
  return r;
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const a = await run(backend);
  check('a big hit: the beat, the pad thumps', a.maxHit >= 9 && a.beats >= 1 && a.held >= 3 && a.rumbles >= 1,
    `hardest hit ${a.maxHit} tonne m/s, ${a.beats} beat(s), ${a.held} ticks held, ${a.rumbles} rumble(s), last ${JSON.stringify(a.rumble)}`);
  const c = await run(backend, 'sw no_beat 1');
  check('control: sw no_beat 1, the same hit runs straight on', c.maxHit >= 9 && c.beats === 0 && c.held === 0, `hardest hit ${c.maxHit}, ${c.beats} beat(s), ${c.held} ticks held`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
