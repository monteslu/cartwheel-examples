// Split screen wall time, the view hook build against the one-pass build: each game's split screen on two RELEASE
// carts through romdev (a host that waits for each frame), the same deterministic start and pad play, the two carts
// interleaved over rounds (A, B, A, B...), frame({op:'timing'}) over the same frames of play. Prints wall, CPU and GPU
// ms a frame (median of the rounds' means), the load average and the GPU clock.
//
//   node tests/split-onepass-timing.mjs --game bikemesa|derby|pugball --players 2|4 --gpu gl|webgpu
//        --old <view hook cart> [--new build/cart-wgpu/carts/<game>.wasc] [--rounds 3] [--frames 300] [--json <file>]
import { request as httpRequest } from 'node:http';
import { readFileSync, writeFileSync } from 'node:fs';
import { loadavg } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { ROMDEV_URL } from './lib/romdev-url.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const { wgpuPin } = await import(join(ROOT, 'deps/cartwheel-engine/tools/lib/wgpu-pin.mjs'));
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const GAME = opt('game'), N = Number(opt('players', '2')), GPU = opt('gpu', 'gl');
const ROUNDS = Number(opt('rounds', '3')), FRAMES = Number(opt('frames', '300'));
const CARTS = { old: resolve(opt('old')), new: resolve(opt('new', join(ROOT, `build/cart-wgpu/carts/${GAME}.wasc`))) };
const BASE = new URL(ROMDEV_URL);
const post = (tool, args, session) => new Promise((ok, fail) => {
  const body = JSON.stringify(args);
  const req = httpRequest({ host: BASE.hostname, port: BASE.port || 80, path: `/tool/${tool}`, method: 'POST', agent: false,
    headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session,
      'x-romdev-agent': 'cartwheel-examples-split-timing' } }, (res) => {
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
const median = (a) => { const s = [...a].sort((x, y) => x - y); return s.length % 2 ? s[(s.length - 1) / 2] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2; };
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
  return steps;
}
// the pads during the measured frames: each driving its own way (romdev's timing input: 'frame:buttons:hold')
function playInput(frames) {
  const ev = [];
  for (let f = 0, k = 0; f < frames; f += 30, k++)
    for (let p = 0; p < N; p++) {
      const turn = Math.sin((k + p * 3) * 0.7), pre = p ? `p${p + 1}.` : '';
      const b = GAME === 'bikemesa' ? ['south'] : GAME === 'derby' ? ['r', turn > 0 ? 'right' : 'left'] : ['r', turn > 0.3 ? 'right' : 'left'];
      ev.push(`${f}:${b.map((x) => pre + x).join('+')}:${GAME === 'bikemesa' ? 6 : 30}`);
    }
  return ev.join(',');
}

async function measure(which, round) {
  const session = `split-timing-${process.pid}-${which}-${round}`;
  await post('loadMedia', { platform: 'wasmcart', path: CARTS[which], deterministicSeed: 1, gpuPower: wgpuPin().power,
    wgpu: GPU === 'webgpu', ...(GPU === 'webgpu' ? { gpuTiming: true } : {}) }, session);
  try {
    await post('frame', { op: 'step', frames: 30 }, session);
    const cmd = (t) => post('wasm', { op: 'command', text: t, maxFrames: 8 }, session);
    if (GAME === 'pugball') await post('input', { op: 'sequence', steps: scriptSteps(`pugball-split${N}.json`) }, session);
    else {
      await cmd(`players ${N}`);
      await cmd(GAME === 'derby' ? 'bout 2' : 'track 1');
      await post('input', { op: 'sequence', steps: [{ input: { ports: [{}, {}, {}, {}] }, frames: 240 }] }, session);
    }
    const la = loadavg()[0];
    const t = await post('frame', { op: 'timing', frames: FRAMES, warmup: 120, window: 60, input: playInput(FRAMES + 120), maxSeconds: 600 }, session);
    const pl = await cmd('pipeline');
    const wall = t.wallMs?.mean ?? t.wallMs, cpu = t.cpuMs?.mean, gpu = t.gpuMs?.mean;
    return { wall, cpu, gpu, load: la, clock: t.gpu?.clockMhz?.mean, adapter: t.gpu?.adapter?.device || t.gpu?.glRenderer,
      noisy: t.contention?.noisy, views_array: (pl.reply || pl).views_array, why: (pl.reply || pl).views_array_why };
  } finally {
    await post('host', { op: 'shutdown' }, session).catch(() => {});
  }
}

const rows = { old: [], new: [] };
for (let r = 0; r < ROUNDS; r++)
  for (const which of r % 2 ? ['new', 'old'] : ['old', 'new']) {
    const m = await measure(which, r);
    rows[which].push(m);
    console.log(`round ${r} ${which.padEnd(3)} wall ${m.wall?.toFixed(2)} cpu ${m.cpu?.toFixed(2)} gpu ${m.gpu != null ? m.gpu.toFixed(2) : '-'} ms, one pass ${m.views_array} "${m.why || ''}", load ${m.load.toFixed(1)}, GPU clock ${m.clock ? Math.round(m.clock) + ' MHz' : '?'}${m.noisy ? ' NOISY' : ''}`);
  }
const sum = (w) => ({ wall: median(rows[w].map((m) => m.wall)), cpu: median(rows[w].map((m) => m.cpu)), gpu: rows[w][0].gpu != null ? median(rows[w].map((m) => m.gpu)) : null });
const o = sum('old'), n = sum('new');
console.log(`${GAME} ${N}p ${GPU} (${rows.new[0].adapter || '?'}): wall ${o.wall.toFixed(2)} -> ${n.wall.toFixed(2)} ms (${(100 * (n.wall / o.wall - 1)).toFixed(1)}%), cpu ${o.cpu?.toFixed(2)} -> ${n.cpu?.toFixed(2)}, gpu ${o.gpu?.toFixed(2) ?? '-'} -> ${n.gpu?.toFixed(2) ?? '-'}`);
if (opt('json')) writeFileSync(resolve(opt('json')), JSON.stringify({ game: GAME, players: N, gpu: GPU, old: rows.old, new: rows.new, summary: { old: o, new: n } }, null, 1));
