// Pugball's split-screen cost against one player: the same 2v2 in stadium 1
// with one, two, three and four players (the CPUs fill the other pugs), each
// pad driving at the cat from GO (tests/pugball-split1..4.json), measured over
// the same window of play on the native build.
//
//   node tests/pugball-split-bench.mjs [--gpus gl,webgpu] [--players 1,2,3,4] [--window 480]
//        [--runs 3] [--json out/pugball-split-bench/report.json]
//
// Per player count and backend, each a mean ms per frame over the window:
//   sim       the fixed tick's CPU (the match, the CPUs, animation, the crowd)
//   render    the frame's render CPU (every view's culling and draw
//             submission, the post passes, the HUD)
//   (both on the main thread's CPU clock: wall time would count waits on the
//   GPU and the machine's other work)
//   wall      the render phase's wall time
//   r3d       the 3D part of render, wall (the pipeline up to the overlays)
//   gpu       the pipeline's GPU timers summed over every pass of every view
//             (CW_GPU_TIMERS=1), then the passes over 0.1 ms. The timers cost
//             the CPU (their polling), so the CPU numbers come from runs
//             without them and the GPU numbers from runs with them.
//   draws     three.c draw calls a frame (every view, shadow maps included)
// and each against one player in percent; then the CPU and GPU ms of each
// pass. The player counts take turns, --runs rounds of them; a CPU number is
// the least of its runs (the machine's other work only ever adds to it), a
// GPU number the median. Runs hold the GPU suite lock in the
// GPU sandbox on the test GPU (CW_CHROMIUM_GPU / CW_WGPU_ADAPTER, as the gates).
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const LIB = join(ROOT, 'deps/cartwheel-engine/tools/lib');
const { enterGpuSandbox } = await import(join(LIB, 'gpu-sandbox.mjs'));
await enterGpuSandbox();
const { takeSuiteLock } = await import(join(LIB, 'suite-lock.mjs'));
const { wgpuPin, glPin, scrubDisplay } = await import(join(LIB, 'wgpu-pin.mjs'));

const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 && argv[i + 1] ? argv[i + 1] : d; };
const GPUS = opt('gpus', 'gl,webgpu').split(',');
const PLAYERS = opt('players', '1,2,3,4').split(',').map(Number);
const WINDOW = +opt('window', '480');
const RUNS = +opt('runs', '5');
const GO = 270; /* the scripts' GO (games/pugball/tools/make-gate-script.mjs) */
const END = GO + 60 + WINDOW; /* a second of play first: everyone is moving */
const OUT = join(ROOT, 'out', 'pugball-split-bench');
mkdirSync(OUT, { recursive: true });

const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/pugball.json'), 'utf8')).binary;
if (!existsSync(exe)) throw new Error(`no native pugball (scripts/build.sh): ${exe}`);

// the run-cart JSON input script as the native CW_INPUT lines (pads 2-4: p= p2= p3=)
const BITS = { south: 0, east: 1, west: 2, north: 3, back: 4, guide: 5, start: 6, l3: 7, r3: 8, l: 9, r: 10, up: 11, down: 12, left: 13, right: 14 };
const mask = (b) => (b || []).reduce((m, n) => m | (1 << BITS[n]), 0);
function inputFile(n) {
  const script = JSON.parse(readFileSync(join(ROOT, `tests/pugball-split${n}.json`), 'utf8'));
  const lines = script.map((s) => {
    const parts = [s.frame - 1, mask(s.buttons), 0, 0, 0, 0, 0, 0];
    for (const [k, f] of [['pad2', 'p'], ['pad3', 'p2'], ['pad4', 'p3']])
      if (s[k]) parts.push(`${f}=${mask(s[k].buttons)},0,0`);
    return parts.join(' ');
  });
  const f = join(OUT, `input${n}.txt`);
  writeFileSync(f, lines.join('\n') + '\n');
  return f;
}

function run(n, gpu, timers) {
  const state = join(OUT, `state-${gpu}-${n}.json`);
  const env = { ...process.env, CW_FRAMES: String(END), CW_INPUT: inputFile(n), CW_DEBUG_CMDS: `perf ${WINDOW}${timers ? ';pipeline' : ''}`, CW_DEBUG_DUMP: state,
    CW_WGPU_VALIDATION: '0', SDL_AUDIODRIVER: 'dummy', PUG_NO_AUDIO: '1', CW_ASSETS: exe + '.assets' };
  if (timers) env.CW_GPU_TIMERS = '1';
  else delete env.CW_GPU_TIMERS;
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '600', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  if (r.status !== 0) throw new Error(`pugball ${n}p ${gpu} exited ${r.status}: ${(r.stderr || '').slice(-2000)}`);
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(r.stderr) || [])[1]);
  else if (gl) gl.check('native', r.stderr);
  const lines = r.stdout.split('\n').filter((l) => l.startsWith('{'));
  const perf = JSON.parse(lines.find((l) => l.includes('"frames"')));
  const pipe = timers ? JSON.parse(lines.find((l) => l.includes('"passes"'))) : { passes: [], gpu_ms: null };
  const st = JSON.parse(readFileSync(state, 'utf8'));
  /* the window must be play in split screen: no goal (its camera is one view) */
  const views = st['cam.views'];
  if (st['game.state'] !== 3 || st['match.phase'] !== 1 || views !== (n >= 2 ? n : 0))
    console.error(`warning: ${n}p ${gpu}: state ${st['game.state']} phase ${st['match.phase']} views ${views} at the end of the window`);
  const ran = pipe.passes.filter((p) => p.ran && !p.fused);
  /* the frame's GPU time: every pass's average (the reply's total is one frame's) */
  if (timers) pipe.gpu_ms = ran.reduce((t, p) => t + (p.gpu_ms > 0 ? p.gpu_ms : 0), 0);
  const passes = Object.fromEntries(ran.filter((p) => p.gpu_ms > 0).map((p) => [p.name, p.gpu_ms]));
  const cpu = Object.fromEntries(ran.filter((p) => p.cpu_ms > 0).map((p) => [p.name, p.cpu_ms]));
  const tc = perf.thread_cpu || {};
  return { players: n, gpu, sim: tc.sim ?? perf.sim.avg, render: tc.render ?? perf.render.avg, wall: perf.render.avg, r3d: perf.r3d_cpu_ms, frame: perf.frame.avg,
    gpu_ms: pipe.gpu_ms, passes, cpu, draws: perf.draw_calls, triangles: perf.triangles };
}
/* each number over the runs: the least (CPU) or the median (GPU) */
function median(rs, least) {
  const mid = (xs) => { const v = xs.filter((x) => x != null).sort((a, b) => a - b); return v.length ? v[least ? 0 : (v.length - 1) >> 1] : null; };
  const out = { ...rs[0] };
  for (const k of ['sim', 'render', 'wall', 'r3d', 'frame', 'gpu_ms', 'draws', 'triangles']) out[k] = mid(rs.map((r) => r[k]));
  for (const k of ['passes', 'cpu']) {
    out[k] = {};
    for (const p of new Set(rs.flatMap((r) => Object.keys(r[k])))) out[k][p] = mid(rs.map((r) => r[k][p]));
  }
  return out;
}

await takeSuiteLock('pugball-split-bench');
const results = [];
for (const gpu of GPUS) {
  const runs = Object.fromEntries(PLAYERS.map((n) => [n, { c: [], g: [] }]));
  for (let k = 0; k < RUNS; k++)
    for (const n of PLAYERS) runs[n].c.push(run(n, gpu, false)), runs[n].g.push(run(n, gpu, true));
  for (const n of PLAYERS) {
    const c = median(runs[n].c, true), g = median(runs[n].g, false);
    const r = { ...c, gpu_ms: g.gpu_ms, passes: g.passes, cpu: g.cpu };
    results.push(r);
    console.error(`${gpu} ${n}p: gpu ${r.gpu_ms.toFixed(2)} ms, render ${r.render.toFixed(2)} ms, draws ${r.draws}`);
  }
}

const f2 = (x) => (x == null ? '-' : x.toFixed(2));
const pct = (x, b) => (b ? `${x >= b ? '+' : ''}${Math.round((x / b - 1) * 100)}%` : '-');
for (const gpu of GPUS) {
  const rows = results.filter((r) => r.gpu === gpu);
  const base = rows.find((r) => r.players === 1) || rows[0];
  console.log(`\n${gpu}  (mean ms per frame over ${WINDOW} frames; vs 1 player)`);
  console.log('players  sim         render       wall         r3d          gpu            draws         triangles');
  for (const r of rows)
    console.log(`${String(r.players).padEnd(9)}${f2(r.sim).padEnd(12)}${(f2(r.render) + ' ' + pct(r.render, base.render)).padEnd(13)}` +
      `${(f2(r.wall) + ' ' + pct(r.wall, base.wall)).padEnd(13)}` +
      `${(f2(r.r3d) + ' ' + pct(r.r3d, base.r3d)).padEnd(13)}${(f2(r.gpu_ms) + ' ' + pct(r.gpu_ms, base.gpu_ms)).padEnd(15)}` +
      `${(r.draws + ' ' + pct(r.draws, base.draws)).padEnd(14)}${(r.triangles / 1e6).toFixed(2)}M ${pct(r.triangles, base.triangles)}`);
  const names = [...new Set(rows.flatMap((r) => Object.keys(r.passes)))].filter((p) => rows.some((r) => r.passes[p] >= 0.1));
  console.log('GPU ms by pass: ' + rows.map((r) => `${r.players}p`).join('  '));
  for (const p of names) console.log(`  ${p.padEnd(13)}${rows.map((r) => f2(r.passes[p]).padStart(6)).join('')}`);
  const cnames = [...new Set(rows.flatMap((r) => Object.keys(r.cpu)))].filter((p) => rows.some((r) => r.cpu[p] >= 0.05));
  console.log('CPU ms by pass (wall, timed runs): ' + rows.map((r) => `${r.players}p`).join('  '));
  for (const p of cnames) console.log(`  ${p.padEnd(13)}${rows.map((r) => f2(r.cpu[p]).padStart(6)).join('')}`);
}
const json = opt('json', join(OUT, 'report.json'));
writeFileSync(json, JSON.stringify({ window: WINDOW, results }, null, 1) + '\n');
console.log(`\n${json}`);
