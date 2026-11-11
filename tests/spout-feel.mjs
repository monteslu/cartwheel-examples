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
//   silk      study silk (games/spout/study.c, scripted, without drawing): a web spun 1 m above the first bracket
//             ({brace} + {silk}) stops the next wash there and is spent; its control (no web) is washed down to the
//             bracket. A line let down at 5 m ({down} + {silk}) goes down 2 m and swings round, and {silk} reels it home
//             to the anchor in under 2.5 s with no silk spent.
//   ghost     a climb of the first spout, then the same spout again from the select: the best climb's ghost is kept
//             (ghost.n samples, its hash the recorded climb's) and climbs beside the spider (ghost.shown). Control:
//             sw no_ghost 1 keeps none.
//   ribbons   the win card's ribbons are the climb's (win.ribbons: 1 every fly, 2 no wash and no knock).
//   hands     study hands (study.c, three seeds): the climber that braces for the waves it sees, on the stick and
//             hand over hand: a steady five pulls a second beats the stick by 5% or more, a ragged rhythm is slower than
//             the stick. Control: one hand pressed every time (and no hop) never pulls: no climb to the top.
//   node tests/spout-feel.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.SPOUT_SESSION || 'spout-feel';
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/spout.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const bye = async (code) => {
  try { await fetch(ROMDEV_URL+'/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': process.env.ROMDEV_AGENT || 'spout-feel' }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
let requests = 0; /* romdev requests, printed at the end */
const tool = async (name, args) => {
  requests++;
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': process.env.ROMDEV_AGENT || 'spout-feel' }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const cmd = async (text) => { const j = await tool('wasm', { op: 'command', text }); const v = j.reply ?? j.result ?? j.value ?? j; return typeof v === 'string' ? JSON.parse(v) : v; };
const state = async () => { const v = await tool('wasm', { op: 'debugState' }), o = {}; for (const f of v.fields) o[f.name] = f.values ?? f.value; return o; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0) => tool('input', { op: 'set', ports: [p0, {}] });
/* ranges, not loops: a press is one request (held 4 frames, let go for 20), a decision of the climb one request (the pad
   held for its 10 frames) and one read */
const hold = (p0, frames) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, {}] }, frames }] });
const press = (b) => tool('input', { op: 'sequence', steps: [{ input: { ports: [{ [b]: true }, {}] }, frames: 4 }, { input: { ports: [{}, {}] }, frames: 20 }] });
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
async function climb(backend, pre, keep) {
  if (!keep) await load(backend);
  if (pre) await cmd(pre);
  for (let n = 0; n < 10 && (await state())['game.state'] !== 2; n++) await press('south');
  let k = await state(), over = 0, samples = 0, banner = 0, still = 0, best = 0;
  for (let f = 0; f < 60 * 60 && k['game.state'] === 2; f += 10) {
    const y = k['spider.pos'][1];
    if (y > best + 0.05) best = y, still = 0; else still += 10;
    const brace = !!k['hud.wave_near'], hop = !brace && still >= 60;
    if (hop) still = 0;
    await hold({ up: !brace, east: brace, south: hop }, 10);
    k = await state();
    over += +k['hud.over_pipe'] > 0, samples++;
    banner += +k['weather'] === 1;
  }
  await pads({});
  const r = { over, samples, banner, state: k['game.state'], medal: k['win.medal'], ribbons: k['win.ribbons'], rec: k['rec.hash'], washes: k['washes'] + k['knocks'], flies: k['flies'] };
  if (!keep) await tool('host', { op: 'shutdown' });
  return r;
}
/* a climb, then the select ({jump} on the win card), back to the first spout and a second second of climbing */
async function ghost(backend, pre) {
  await load(backend);
  await cmd('sw fresh 1'); /* the host keeps saves between loads: start as a new player, so this climb is the best */
  if (pre) await cmd(pre);
  const first = await climb(backend, null, true);
  await step(60); /* the win card takes a press after two thirds of a second */
  await press('south'); /* the win card: back to the select (the cursor on the next spout) */
  await press('left');
  await press('south');
  await pads({ up: true });
  await step(60);
  const k = await state();
  await pads({});
  await tool('host', { op: 'shutdown' });
  return { first, state: k['game.state'], n: k['ghost.n'], hash: k['ghost.hash'], shown: k['ghost.shown'] };
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
  check('the win card\'s ribbons are the climb\'s: every fly (bit 1), no wash or knock (bit 2)', c.ribbons === ((c.flies === 3 ? 1 : 0) | (c.washes === 0 ? 2 : 0)), `flies ${c.flies}, washed or knocked ${c.washes}, win.ribbons ${c.ribbons}`);
  const cc = await climb(backend, 'sw old_hud 1');
  check('control: sw old_hud 1 covers it', cc.over > 0, `${cc.over} of ${cc.samples} samples over the pipe`);
  await load(backend);
  const sk = await cmd('study silk');
  await tool('host', { op: 'shutdown' });
  const w = sk.web, nw = sk.none, ln = sk.line;
  check('a web stops the next wash where it was spun, and is spent', w.washed && w.caught === 1 && !w.web_left && Math.abs(w.after - w.web_y) < 0.05 && w.after > nw.after + 0.5,
    `web at ${w.web_y.toFixed(2)} m: washed from ${w.before.toFixed(2)} to ${w.after.toFixed(2)} m`);
  check('control: no web, the wash takes it to the bracket', nw.washed && nw.after < w.web_y - 0.5, `washed from ${nw.before.toFixed(2)} to ${nw.after.toFixed(2)} m`);
  check('a line down from 5 m and {silk} home: back at the anchor, no silk spent, under 2.5 s', ln.started && ln.lowest < ln.anchor - 1.5 && ln.swung > 0.3 && ln.cling && Math.abs(ln.back - ln.anchor) < 0.05 && ln.silk_spent === 0 && ln.ticks < 150,
    `down to ${ln.lowest.toFixed(2)} m from ${ln.anchor.toFixed(2)}, swung ${ln.swung.toFixed(2)} rad, home at ${ln.back.toFixed(2)} m in ${(ln.ticks / 60).toFixed(2)} s, silk spent ${ln.silk_spent}`);
  seen[backend] += JSON.stringify(sk);
  await load(backend);
  const hd = await cmd('study hands');
  await tool('host', { op: 'shutdown' });
  const sum = (a) => a.reduce((x, y) => x + (y < 0 ? 1e6 : y), 0);
  check('hand over hand: a steady rhythm beats the stick, a ragged one is slower', hd.steady.every((x) => x > 0) && sum(hd.steady) <= sum(hd.stick) * 0.95 && sum(hd.ragged) > sum(hd.stick),
    `ticks to the top: stick ${hd.stick.join(', ')}; steady ${hd.steady.join(', ')}; ragged ${hd.ragged.join(', ')}`);
  check('control: one hand every time never pulls', hd.one_hand.every((x) => x < 0), `one hand ${hd.one_hand.join(', ')}`);
  seen[backend] += JSON.stringify(hd);
  const g = await ghost(backend);
  check('the best climb\'s ghost is kept and climbs beside the spider', g.first.state === 4 && g.state === 2 && g.n > 20 && g.hash === g.first.rec && g.shown === 1,
    `first climb ${g.first.state === 4 ? 'won' : 'not won'}, then game.state ${g.state}, ghost ${g.n} samples, hash ${g.hash} (recorded ${g.first.rec}), shown ${g.shown}`);
  const gc = await ghost(backend, 'sw no_ghost 1');
  check('control: sw no_ghost 1 keeps none', gc.n === 0 && gc.shown === 0, `ghost ${gc.n} samples, shown ${gc.shown}`);
}
if (seen.gl && seen.webgpu) check('GL and WebGPU agree', seen.gl === seen.webgpu, seen.gl === seen.webgpu ? 'identical studies' : 'the studies differ');
console.log(`romdev requests: ${requests}`);
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
