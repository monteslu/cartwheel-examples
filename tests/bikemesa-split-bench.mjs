// BikeMesa's split-screen cost against one player: the self-playing build
// (bikemesa_auto) on Red Rock Run with one, two, three and four riders
// (BIKEMESA_PLAYERS), every rider pedalling the same line in its own lane and
// view, measured over the same window of the ride on the native build.
//
//   node tests/bikemesa-split-bench.mjs [--gpus gl,webgpu] [--players 1,2,3,4] [--window 600]
//        [--runs 3] [--json out/bikemesa-split-bench/report.json]
//
// Per player count and backend, each a mean ms per frame over the window:
//   sim       the fixed tick's CPU (the bikes, the crash world, the scenery)
//   render    the frame's render CPU (every view's culling and draw
//             submission, the post passes, the HUD)
//             both on the main thread's CPU clock (perf's thread_cpu), not
//             the wall clock, which counts waits on the GPU too
//   r3d       the 3D part of render (the pipeline up to the overlays)
//   gpu       the pipeline's GPU timers summed over every pass of every view
//             (CW_GPU_TIMERS=1), then the passes over 0.1 ms
//   draws     three.c draw calls a frame (every view, shadow maps included)
// and each against one player in percent; then the CPU and GPU ms of each
// pass. Each number is the median of --runs runs (the machine's other work
// moves the CPU times more than the GPU's). Runs hold the GPU suite lock in the
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
const WINDOW = +opt('window', '600');
const RUNS = +opt('runs', '3');
const END = 500 + WINDOW; /* the riders are up to speed by frame 500 whatever the count (the joins take a second) */
const OUT = join(ROOT, 'out', 'bikemesa-split-bench');
mkdirSync(OUT, { recursive: true });

const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/bikemesa_auto.json'), 'utf8')).binary;
if (!existsSync(exe)) throw new Error(`no native bikemesa_auto (scripts/build.sh): ${exe}`);

function run(n, gpu) {
  const state = join(OUT, `state-${gpu}-${n}.json`);
  const env = { ...process.env, CW_FRAMES: String(END), BIKEMESA_PLAYERS: String(n), CW_DEBUG_CMDS: `perf ${WINDOW};pipeline`, CW_DEBUG_DUMP: state,
    CW_GPU_TIMERS: '1', CW_WGPU_VALIDATION: '0', SDL_AUDIODRIVER: 'dummy', BIKEMESA_NO_AUDIO: '1' };
  const pin = gpu === 'webgpu' ? wgpuPin() : null, gl = pin ? null : glPin();
  if (pin) Object.assign(env, pin.env, { CW_GPU: 'webgpu' });
  else { env.CW_GPU = 'gl'; if (gl) Object.assign(env, gl.env); }
  scrubDisplay(env);
  const r = spawnSync('timeout', ['-s', 'KILL', '600', exe], { env, encoding: 'utf8', maxBuffer: 1 << 26 });
  if (r.status !== 0) throw new Error(`bikemesa_auto ${n}p ${gpu} exited ${r.status}: ${(r.stderr || '').slice(-2000)}`);
  if (pin) pin.check('native', (/WebGPU adapter: (.*)/.exec(r.stderr) || [])[1]);
  else if (gl) gl.check('native', r.stderr);
  const lines = r.stdout.split('\n').filter((l) => l.startsWith('{'));
  const perf = JSON.parse(lines.find((l) => l.includes('"frames"')));
  const pipe = JSON.parse(lines.find((l) => l.includes('"passes"')));
  const st = JSON.parse(readFileSync(state, 'utf8'));
  /* the window must be the race: every rider in, still riding */
  if (st['game.state'] !== 3 || st.players !== n)
    console.error(`warning: ${n}p ${gpu}: state ${st['game.state']} players ${st.players} at the end of the window`);
  const ran = pipe.passes.filter((p) => p.ran && !p.fused);
  const passes = Object.fromEntries(ran.filter((p) => p.gpu_ms > 0).map((p) => [p.name, p.gpu_ms]));
  const cpu = Object.fromEntries(ran.filter((p) => p.cpu_ms > 0).map((p) => [p.name, p.cpu_ms]));
  const tc = perf.thread_cpu || {};
  return { players: n, gpu, sim: tc.sim ?? perf.sim.avg, render: tc.render ?? perf.render.avg, render_wall: perf.render.avg, r3d: perf.r3d_cpu_ms, frame: perf.frame.avg,
    gpu_ms: pipe.gpu_ms, passes, cpu, draws: perf.draw_calls, triangles: perf.triangles };
}
/* the median of each number over the runs */
function median(rs) {
  const mid = (xs) => { const v = xs.filter((x) => x != null).sort((a, b) => a - b); return v.length ? v[(v.length - 1) >> 1] : null; };
  const out = { ...rs[0] };
  for (const k of ['sim', 'render', 'r3d', 'frame', 'gpu_ms', 'draws', 'triangles']) out[k] = mid(rs.map((r) => r[k]));
  for (const k of ['passes', 'cpu']) {
    out[k] = {};
    for (const p of new Set(rs.flatMap((r) => Object.keys(r[k])))) out[k][p] = mid(rs.map((r) => r[k][p]));
  }
  return out;
}

await takeSuiteLock('bikemesa-split-bench');
const results = [];
for (const gpu of GPUS) {
  /* the runs interleaved (1, 2, 3, 4 players, then again): the machine's
   * other work falls on every count alike */
  const runs = Object.fromEntries(PLAYERS.map((n) => [n, []]));
  for (let k = 0; k < RUNS; k++)
    for (const n of PLAYERS) runs[n].push(run(n, gpu));
  for (const n of PLAYERS) {
    const r = median(runs[n]);
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
  console.log('players  sim         render       r3d          gpu            draws         triangles');
  for (const r of rows)
    console.log(`${String(r.players).padEnd(9)}${f2(r.sim).padEnd(12)}${(f2(r.render) + ' ' + pct(r.render, base.render)).padEnd(13)}` +
      `${(f2(r.r3d) + ' ' + pct(r.r3d, base.r3d)).padEnd(13)}${(f2(r.gpu_ms) + ' ' + pct(r.gpu_ms, base.gpu_ms)).padEnd(15)}` +
      `${(r.draws + ' ' + pct(r.draws, base.draws)).padEnd(14)}${(r.triangles / 1e6).toFixed(2)}M ${pct(r.triangles, base.triangles)}`);
  const names = [...new Set(rows.flatMap((r) => Object.keys(r.passes)))].filter((p) => rows.some((r) => r.passes[p] >= 0.1));
  console.log('GPU ms by pass: ' + rows.map((r) => `${r.players}p`).join('  '));
  for (const p of names) console.log(`  ${p.padEnd(13)}${rows.map((r) => f2(r.passes[p]).padStart(6)).join('')}`);
  const cnames = [...new Set(rows.flatMap((r) => Object.keys(r.cpu)))].filter((p) => rows.some((r) => r.cpu[p] >= 0.05));
  console.log('CPU ms by pass: ' + rows.map((r) => `${r.players}p`).join('  '));
  for (const p of cnames) console.log(`  ${p.padEnd(13)}${rows.map((r) => f2(r.cpu[p]).padStart(6)).join('')}`);
}
const json = opt('json', join(OUT, 'report.json'));
writeFileSync(json, JSON.stringify({ window: WINDOW, results }, null, 1) + '\n');
console.log(`\n${json}`);
