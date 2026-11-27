// racer natively, GL and WebGPU: temple's bird flocks and butterfly swarms (m7_temple.c build_canopy_life) are off where
// no view can see them. Each of those seven emitters ran its simulation on every step of every race wherever the views
// were. On GL a simulation step is a transform feedback pass, and radeonsi on GFX11 flushes the command buffer each time
// one ends, so split 4 temple ran about 22 of those passes a frame where split 4 salt runs 9.
// Now each one is off (not simulated, not drawn) beyond the distance where it still shows, and faded in over the last
// 40 percent of that range, as the maps' other world emitters are (fx_ambient_fade, fx_amb.c).
//   run      split 4 temple at Very Low, 1500 frames with the bot: the race reached with four views, and at least
//            FADE_MIN of the seven emitters off on the last frame (debug field fx.fade_off).
//   control  the same run with sw amb_fade 0 (the emitters on at any distance, as before): none of them may be off.
// Each run prints fx.psys and fx.psys_hidden (the particle systems, and those neither simulated nor drawn) to read.
//   node tests/racer-psys-fade.mjs [--gpu gl|webgpu] [--frames N]
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ENGINE = join(ROOT, 'deps/cartwheel-engine');
const OUT = join(ROOT, 'out/racer-psys-fade');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const GPUS = arg('--gpu', 'gl,webgpu').split(',');
const FRAMES = Number(arg('--frames', '1500'));
const FADE_MIN = 3; // four views sit at four places on the course: frame 1500 has 4 of the 7 off on GL; the control has none
const exe = JSON.parse(readFileSync(join(ROOT, 'build/native/apps/racer.json'), 'utf8')).binary;

function run(gpu, fade) {
  const dir = join(OUT, `${gpu}-${fade ? 'fade' : 'control'}`);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const state = join(dir, 's.json');
  const r = spawnSync('node', [join(ENGINE, 'tools/run-cart.mjs'), '--host', 'native', '--gpu', gpu, '--app', 'racer', '--exe', exe,
    '--frames', String(FRAMES), '--allow-skipped', '--shot', join(dir, 's.png'), '--state', state, '--log', join(dir, 's.log'),
    '--start-cmds', `bot 1;race 6 4 3;sw amb_fade ${fade ? 1 : 0}`],
  { cwd: ENGINE, encoding: 'utf8', maxBuffer: 1 << 26, env: { CW_RUN_TIMEOUT_MS: '1800000', ...process.env, CW_PROJECT_ROOT: ROOT, SDL_AUDIODRIVER: 'dummy', CW_QUALITY: 'verylow' } });
  if (r.status !== 0) return { failed: (r.stderr || r.stdout || '').slice(-600) };
  return JSON.parse(readFileSync(state, 'utf8'));
}

let bad = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) bad++; };
const show = (s) => ['race.phase', 'views', 'fx.fade_off', 'fx.psys', 'fx.psys_hidden'].map((k) => `${k}=${s[k]}`).join(' ');
for (const gpu of GPUS) {
  const on = run(gpu, true), off = run(gpu, false);
  for (const [name, s] of [['run', on], ['control', off]]) if (s.failed) check(false, `${gpu} ${name}: run failed: ${s.failed}`);
  if (on.failed || off.failed) continue;
  const raced = (s) => s['race.phase'] === 2 && s.views === 4;
  check(raced(on) && on['fx.fade_off'] >= FADE_MIN, `${gpu} split 4 temple: at least ${FADE_MIN} of the canopy emitters off where no view sees them (${show(on)})`);
  check(raced(off) && off['fx.fade_off'] === 0, `${gpu} control, sw amb_fade 0: none off (${show(off)})`);
}
console.log(bad ? `${bad} check(s) failed` : 'all passed');
process.exit(bad ? 1 : 0);
