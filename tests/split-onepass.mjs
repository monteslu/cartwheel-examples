// Split screen in one pass: BikeMesa, Derby and Pugball say what each view draws with view masks
// (cw_object_set_views) instead of a view hook, so the engine draws every view in one pass. This plays each game's
// split screen on the built cart through romdev (GL or WebGPU), with pads driving the way players do, and checks:
//   - one pass: the cart's `pipeline` reply has views_array true and views_array_why empty, at frames through play;
//   - the picture: with --ref (the cart built with the view hook), the same deterministic run's frames differ in at
//     most 0.3% of their pixels by more than 16 levels (the engine's views-array / views-masks bound);
//   - the control: the same frames against the reference run 20 frames later must fail that bound (so the compare
//     can fail at all), and `pipeline views_array 0` must report views_array false (so the one-pass check can fail).
//
//   node tests/split-onepass.mjs --game bikemesa|derby|pugball --players 2|3|4 [--gpu gl|webgpu]
//        [--cart build/cart-wgpu/carts/<game>.wasc] [--ref <old cart>] [--scene <n>] [--quality <preset>]
//        [--frames 900] [--shots out/split-onepass] [--json <file>]
//
// Scenes: bikemesa a track (`track n`), derby an arena (`bout n`), pugball a stadium (the team sheet, then the
// stadium's exhibition; scene 1 only: the split scripts play stadium 1).
import { request as httpRequest } from 'node:http';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { ROMDEV_URL } from './lib/romdev-url.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const { readImage, diffImages } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/png.mjs'));
const { wgpuPin } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/wgpu-pin.mjs'));
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const GAME = opt('game');
const PLAYERS = Number(opt('players', '2'));
const GPU = opt('gpu', 'gl');
const SCENE = Number(opt('scene', '1'));
const QUALITY = opt('quality', '');
const FRAMES = Number(opt('frames', '900'));
const CART = resolve(opt('cart', join(ROOT, `build/cart-wgpu/carts/${GAME}.wasc`)));
const REF = opt('ref') ? resolve(opt('ref')) : null;
const SHOTS = resolve(opt('shots', join(ROOT, 'out/split-onepass')));
const BOUND = 0.003;
const BASE = new URL(ROMDEV_URL);
if (!['bikemesa', 'derby', 'pugball'].includes(GAME)) throw new Error('--game bikemesa|derby|pugball');
if (!existsSync(CART)) throw new Error(`no cart at ${CART}`);
mkdirSync(SHOTS, { recursive: true });

const post = (tool, args, session) => new Promise((ok, fail) => {
  const body = JSON.stringify(args);
  const req = httpRequest({ host: BASE.hostname, port: BASE.port || 80, path: `/tool/${tool}`, method: 'POST', agent: false,
    headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session,
      'x-romdev-agent': 'cartwheel-examples-split-onepass' } }, (res) => {
    let t = '';
    res.setEncoding('utf8');
    res.on('data', (c) => { t += c; });
    res.on('end', () => {
      let b;
      try { b = JSON.parse(t); } catch { b = t; }
      if (res.statusCode >= 300 || (b && b.isError)) fail(new Error(`romdev ${tool}: ${JSON.stringify(b).slice(0, 1500)}`));
      else ok(b);
    });
  });
  req.on('error', (e) => fail(new Error(`romdev at ${BASE.origin} ${tool}: ${e.message}`)));
  req.end(body);
});

// a gate input script (tests/<game>-*.json: {frame, buttons, axes, pad2..pad4}, each held to the next) as steps
function scriptSteps(file) {
  const ev = JSON.parse(readFileSync(join(ROOT, 'tests', file), 'utf8'));
  const port = (e) => { const p = {}; for (const b of e?.buttons || []) p[b] = true; if (e?.axes) p.axes = e.axes; return p; };
  const steps = [];
  let at = 0, ports = [{}, {}, {}, {}];
  for (const e of ev) {
    if (e.frame > at) steps.push({ input: { ports }, frames: e.frame - at });
    at = e.frame;
    ports = [port(e), port(e.pad2), port(e.pad3), port(e.pad4)];
  }
  steps.push({ input: { ports }, frames: 1 });
  return { steps, frames: at + 1 };
}
// play after the start: each pad drives its own way, steering in turns of a second or so, as players do
function playSteps(frames, n) {
  const steps = [];
  for (let f = 0, k = 0; f < frames; k++) {
    const len = GAME === 'bikemesa' ? 10 : 36;
    const ports = [];
    for (let p = 0; p < 4; p++) {
      if (p >= n) { ports.push({}); continue; }
      const turn = Math.sin((k + p * 3) * 0.7);
      if (GAME === 'bikemesa') ports.push(k % 2 === p % 2 ? { south: true } : {}); /* pedal strokes */
      else if (GAME === 'derby') ports.push({ r: true, axes: { rt: 1, lx: Math.max(-1, Math.min(1, turn)) } });
      else ports.push({ r: true, ...(turn > 0.3 ? { right: true } : turn < -0.3 ? { left: true } : {}), ...(k % 7 === p ? { east: true } : {}) });
    }
    const fr = Math.min(len, frames - f);
    steps.push({ input: { ports }, frames: fr });
    f += fr;
  }
  return steps;
}

const scripts = { pugball: { 2: 'pugball-split2.json', 3: 'pugball-split3.json', 4: 'pugball-split4.json' } };
function setup() {
  if (GAME === 'pugball') {
    const s = scriptSteps(scripts.pugball[PLAYERS]);
    return { commands: [], steps: s.steps, startFrames: s.frames };
  }
  if (GAME === 'derby') return { commands: [`players ${PLAYERS}`, `bout ${SCENE}`], steps: [{ input: { ports: [{}, {}, {}, {}] }, frames: 240 }], startFrames: 240 };
  return { commands: [`players ${PLAYERS}`, `track ${SCENE}`], steps: [{ input: { ports: [{}, {}, {}, {}] }, frames: 240 }], startFrames: 240 };
}

// one run: load, set up, play to each check frame, read the pipeline and keep the frame
async function run(cart, tag, { offset = 0 } = {}) {
  const session = `split-onepass-${process.pid}-${tag}`;
  const pin = wgpuPin();
  const loaded = await post('loadMedia', { platform: 'wasmcart', path: cart, deterministicSeed: 1, gpuPower: pin.power,
    wgpu: GPU === 'webgpu' }, session);
  const out = { checks: [] };
  try {
    if (loaded.gpu && loaded.gpu !== GPU) throw new Error(`the cart runs on ${loaded.gpu}, not ${GPU}`);
    await post('frame', { op: 'step', frames: 30 }, session); /* the warm-up (a deterministic load does it in the first frame) */
    if (QUALITY) await post('wasm', { op: 'command', text: `quality ${QUALITY}`, maxFrames: 8 }, session);
    const s = setup();
    for (const c of s.commands) await post('wasm', { op: 'command', text: c, maxFrames: 8 }, session);
    await post('input', { op: 'sequence', steps: s.steps }, session);
    const play = playSteps(FRAMES + offset, PLAYERS);
    const checks = [Math.round(FRAMES * 0.25), Math.round(FRAMES * 0.6), FRAMES];
    let done = 0, si = 0, carry = null;
    for (const at of checks) {
      const want = at + offset - done, seg = [];
      let got = 0;
      while (got < want) {
        const st = carry || play[si++] || { input: { ports: [{}, {}, {}, {}] }, frames: want - got };
        carry = null;
        const take = Math.min(st.frames, want - got);
        seg.push({ input: st.input, frames: take });
        if (take < st.frames) carry = { input: st.input, frames: st.frames - take };
        got += take;
      }
      if (seg.length) await post('input', { op: 'sequence', steps: seg }, session);
      done += want;
      const pl = await post('wasm', { op: 'command', text: 'pipeline', maxFrames: 8 }, session);
      const reply = pl.reply || pl;
      const shot = join(SHOTS, `${GAME}-${PLAYERS}p-s${SCENE}${QUALITY ? '-' + QUALITY : ''}-${GPU}-${tag}-f${at}.png`);
      await post('frame', { op: 'screenshot', path: shot }, session);
      out.checks.push({ frame: at, views_array: reply.views_array, why: reply.views_array_why, views: reply.views ?? reply.nviews,
        view_hook: reply.view_hook, view_masks: reply.view_masks, shot });
    }
    // the one-pass check's control: the one pass switched off must say so
    if (!offset) {
      await post('wasm', { op: 'command', text: 'pipeline views_array 0', maxFrames: 8 }, session);
      await post('frame', { op: 'step', frames: 2 }, session);
      const pl = await post('wasm', { op: 'command', text: 'pipeline', maxFrames: 8 }, session);
      out.control_views_array = (pl.reply || pl).views_array;
    }
  } finally {
    await post('host', { op: 'shutdown' }, session).catch(() => {});
  }
  return out;
}

let fail = 0;
const check = (what, ok, detail) => { if (!ok) fail++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}: ${detail}`); };
const name = `${GAME} ${PLAYERS}p scene ${SCENE}${QUALITY ? ' ' + QUALITY : ''} ${GPU}`;
const a = await run(CART, 'new');
for (const c of a.checks) check(`${name} f${c.frame}: one pass`, c.views_array === true && !c.why, `views_array ${c.views_array} why "${c.why || ''}" hook ${c.view_hook} masks ${c.view_masks}`);
check(`${name}: control (pipeline views_array 0) reports per view`, a.control_views_array === false, `views_array ${a.control_views_array}`);
const result = { game: GAME, players: PLAYERS, scene: SCENE, quality: QUALITY, gpu: GPU, new: a };
if (REF) {
  const r = await run(REF, 'old');
  const late = await run(REF, 'old-late', { offset: 20 });
  result.old = r;
  result.diff = [];
  for (let i = 0; i < a.checks.length; i++) {
    const d = diffImages(readImage(a.checks[i].shot), readImage(r.checks[i].shot)).gt16;
    const dc = diffImages(readImage(a.checks[i].shot), readImage(late.checks[i].shot)).gt16;
    result.diff.push({ frame: a.checks[i].frame, share: d, control: dc, old_why: r.checks[i].why });
    check(`${name} f${a.checks[i].frame}: picture vs the view hook build (<= ${BOUND})`, d <= BOUND, `${(d * 100).toFixed(3)}% (old: one pass ${r.checks[i].views_array}, "${r.checks[i].why || ''}")`);
    check(`${name} f${a.checks[i].frame}: control (the hook build 20 frames later) must fail`, dc > BOUND, `${(dc * 100).toFixed(3)}%`);
  }
}
if (opt('json')) writeFileSync(resolve(opt('json')), JSON.stringify(result, null, 1));
console.log(fail ? `split-onepass ${name}: ${fail} check(s) failed` : `split-onepass ${name}: ok`);
process.exit(fail ? 1 : 0);
