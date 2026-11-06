// Nothing floats on Very Low: each game's autopilot in play keeps the shadow High draws at the base of each caster big
// enough to read on screen (the engine's tests/quality-grounding.mjs: CW_SHADOW_CONTACTS and CW_TEST_NO_SHADOWS,
// natively at 1080p on the test GPU), on GL and WebGPU. Control (in the engine's tool, per app): the frame with no
// shadows must fail.
//   racer: RACER_THIN on both runs (below High the racer builds thin scenery, no tree shadows: the casters must be
//     the same on both frames to compare them).
//   chromite: 80% (one conduit pipe lying on the deck takes its shadow from a lamp; Very Low keeps the sun's shadows only).
//   derby: the cars' contact blobs (world_contact_shadows) are what Very Low has under them: the floodlights cast none.
//   BikeMesa is not measured here (its frame reports no caster bases: the rider's shadow falls on the canyon wall far
//   behind, past Very Low's shadow distance, while the wheels sit on the track); it is judged by eye on its Very Low shots.
//   node tests/quality-grounding.mjs [--gpu gl|webgpu] [--apps derby_auto,...]
import { spawnSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const APPS = [
  { app: 'racer_auto', frames: 900, env: { RACER_THIN: '1' } },
  { app: 'derby_auto', frames: 600 },
  { app: 'pugball_auto', frames: 600 },
  { app: 'bomberdude_auto', frames: 600 },
  { app: 'chromite_auto', frames: 600, share: 0.8 },
  { app: 'spout_auto', frames: 600 },
];
const only = opt('apps') ? opt('apps').split(',') : null;
let fail = 0;
for (const gpu of opt('gpu', 'gl,webgpu').split(','))
  for (const a of APPS.filter((x) => !only || only.includes(x.app))) {
    const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tests/quality-grounding.mjs'), '--project', ROOT, '--apps', `${a.app}:${a.frames}`, '--gpu', gpu, ...(a.share ? ['--share', String(a.share)] : [])],
      { stdio: 'inherit', env: { ...process.env, ...(a.env || {}) } });
    if (r.status !== 0) fail++;
  }
process.exit(fail ? 1 : 0);
