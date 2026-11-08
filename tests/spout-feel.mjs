// spout on the cart through romdev, GL and WebGPU: the climb asks something, and the HUD leaves the climb clear.
//
//   hazards   study hazards 1 (games/spout/study.c, three seeds, without drawing): the climber that only holds up
//             and hops is washed down at least once on every seed of the first spout (the rain pushes a stream
//             across its line and the bird marks where it will be), while the one that braces for the waves it can
//             see clears every seed with no wash and as fast as the holder or within 15%. Control: sw old_hazards 1
//             (streams that only wander) lets the holder up untouched on some seed.
//   hud       through a whole climb of the first spout, the time card, the forecast and the banners ("rain's coming",
//             "washed down!") never cover the pipe above the spider (hud.over_pipe, sampled every 10 frames, banners
//             included while they show). Control: sw old_hud 1 (the card in the middle, banners over the climb).
//   medal     a clean climb of the first spout wins a paper medal (win.medal 0 gold or 1 silver).
//   node tests/spout-feel.mjs [--gl] [--webgpu] [--cart <path>]
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.SPOUT_SESSION || 'spout-feel';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/spout.wasc')));
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
const cmd = async (text) => { const j = await tool('wasm', { op: 'command', text }); const v = j.reply ?? j.result ?? j.value ?? j; return typeof v === 'string' ? JSON.parse(v) : v; };
const state = async () => { const v = await tool('wasm', { op: 'debugState' }), o = {}; for (const f of v.fields) o[f.name] = f.values ?? f.value; return o; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0) => tool('input', { op: 'set', ports: [p0, {}] });
const press = async (b) => { await pads({ [b]: true }); await step(4); await pads({}); await step(20); };
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const load = async (backend) => {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(60);
};
const hazards = (h) => {
  const holdWashed = h.hold.every((c) => c.washes + c.pecks >= 1), tellClean = h.tell.every((c) => c.ticks > 0 && c.washes === 0);
  const fair = h.tell.every((c, i) => h.hold[i].ticks > 0 && c.ticks <= h.hold[i].ticks * 1.15);
  return { ok: holdWashed && tellClean && fair, holdWashed,
    text: `hold ${h.hold.map((c) => `${(c.ticks / 60).toFixed(1)} s ${c.washes}w ${c.pecks}p`).join(', ')}; tell ${h.tell.map((c) => `${(c.ticks / 60).toFixed(1)} s ${c.washes}w`).join(', ')}` };
};
async function climb(backend, pre) {
  await load(backend);
  if (pre) await cmd(pre);
  for (let n = 0; n < 10 && (await state())['game.state'] !== 2; n++) await press('south');
  let k = await state(), over = 0, samples = 0, banner = 0, still = 0, best = 0;
  for (let f = 0; f < 60 * 60 && k['game.state'] === 2; f += 10) {
    const y = k['spider.pos'][1];
    if (y > best + 0.05) best = y, still = 0; else still += 10;
    const brace = !!k['hud.wave_near'], hop = !brace && still >= 60;
    if (hop) still = 0;
    await pads({ up: !brace, east: brace, south: hop });
    await step(10);
    k = await state();
    over += +k['hud.over_pipe'] > 0, samples++;
    banner += +k['weather'] === 1;
  }
  await pads({});
  const r = { over, samples, banner, state: k['game.state'], medal: k['win.medal'] };
  await tool('host', { op: 'shutdown' });
  return r;
}

const seen = {};
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  await load(backend);
  const h = await cmd('study hazards 1'), hz = hazards(h);
  check('the first spout asks something: the holder is washed, the reader of waves is not', hz.ok, hz.text);
  await cmd('sw old_hazards 1');
  const ho = hazards(await cmd('study hazards 1'));
  check('control: sw old_hazards 1 lets the holder up untouched', !ho.holdWashed, ho.text);
  await cmd('sw old_hazards 0');
  await tool('host', { op: 'shutdown' });
  seen[backend] = JSON.stringify(h);
  const c = await climb(backend);
  check('the HUD never covers the pipe above the spider', c.samples > 30 && c.banner > 0 && c.over === 0, `${c.over} of ${c.samples} samples over the pipe (${c.banner} with the rain warning up), game.state ${c.state}`);
  check('a clean climb wins a paper medal', c.state === 4 && (c.medal === 0 || c.medal === 1), `win.medal ${c.medal}`);
  const cc = await climb(backend, 'sw old_hud 1');
  check('control: sw old_hud 1 covers it', cc.over > 0, `${cc.over} of ${cc.samples} samples over the pipe`);
}
if (seen.gl && seen.webgpu) check('GL and WebGPU agree', seen.gl === seen.webgpu, seen.gl === seen.webgpu ? 'identical studies' : 'the studies differ');
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
