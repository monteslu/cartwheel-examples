// Nothing floats on Very Low: each game's autopilot in play keeps the shadow High draws at the base of each caster big
// enough to read on screen (the engine's tests/quality-grounding.mjs: CW_SHADOW_CONTACTS and CW_TEST_NO_SHADOWS,
// natively at 1080p on the test GPU), on GL and WebGPU. Control (in the engine's tool, per app): the frame with no
// shadows must fail.
//   racer: RACER_THIN on both runs (below High the racer builds thin scenery, no tree shadows: the casters must be
//     the same on both frames to compare them).
//   chromite: 80% (one conduit pipe lying on the deck takes its shadow from a lamp; Very Low keeps the sun's shadows only).
//   derby: at night Very Low and Low draw two floods as directional lights that cast (scenery_flood_sun), with the cars'
//     contact blobs (world_contact_shadows) under them. A bout changes from frame to frame (cars pile up, smoke and dust
//     rise), so derby is judged on a sweep, every 50 frames from 300 to 900, on Very Low and on Low (the engine's
//     tool judges a wall or a row of stands at the foot nearest the camera and leaves out what stands around the
//     camera: the wall ring, the floor). The sweep passes when it keeps 90% of the shadows High draws over all its
//     frames and at least 80% in each frame of 5 bases or more (one panel of a crumpled car shaded from another
//     flood's side is a tenth of a frame's bases; a frame of fewer is too few to judge alone and counts toward the
//     sweep), with at least 10 frames holding a base.
//   BikeMesa is not measured here (its frame reports no caster bases: the rider's shadow falls on the canyon wall far
//   behind, past Very Low's shadow distance, while the wheels sit on the track); it is judged by eye on its Very Low shots.
//     Its sweep runs as four gates of its own (tests/quality-grounding-derby-<n>.mjs: Very Low and Low, GL and WebGPU), each
//     inside the time a GPU suite may hold the lock; the gate quality-grounding runs the other games.
//   node tests/quality-grounding.mjs [--gpu gl|webgpu] [--apps derby_auto,...] [--preset verylow|low] (a swept app's presets)
import { spawnSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes('--' + k) ? argv[argv.indexOf('--' + k) + 1] : d);
const APPS = [
  { app: 'racer_auto', frames: 900, env: { RACER_THIN: '1' } },
  { app: 'derby_auto', frames: [300, 350, 400, 450, 500, 550, 600, 650, 700, 750, 800, 850, 900], presets: ['verylow', 'low'], frameShare: 0.8, frameMin: 5, minFrames: 10 },
  { app: 'pugball_auto', frames: 600 },
  { app: 'bomberdude_auto', frames: 600 },
  { app: 'chromite_auto', frames: 600, share: 0.8 },
  { app: 'spout_auto', frames: 600 },
];
const only = opt('apps') ? opt('apps').split(',') : null;
let fail = 0;
for (const gpu of opt('gpu', 'gl,webgpu').split(','))
  for (const a of APPS.filter((x) => (only ? only.includes(x.app) : !Array.isArray(x.frames)))) {
    if (!Array.isArray(a.frames)) {
      const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tests/quality-grounding.mjs'), '--project', ROOT, '--apps', `${a.app}:${a.frames}`, '--gpu', gpu, ...(a.share ? ['--share', String(a.share)] : [])],
        { stdio: 'inherit', env: { ...process.env, ...(a.env || {}) } });
      if (r.status !== 0) fail++;
      continue;
    }
    for (const preset of a.presets.filter((p) => !opt('preset') || opt('preset').split(',').includes(p))) fail += sweep(a, gpu, preset);
  }
/** a sweep of frames at one preset: the engine's tool on each frame (its control included), each frame's verdict taken
 * again by the sweep's rules: the sweep needs a.share over all its frames,
 * a.frameShare in each frame of a.frameMin bases or more, and a.minFrames frames holding a base */
function sweep(a, gpu, preset) {
  const share = a.share || 0.9;
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tests/quality-grounding.mjs'), '--project', ROOT, '--apps', a.frames.map((f) => `${a.app}:${f}`).join(','),
    '--gpu', gpu, '--preset', preset, '--share', String(share)], { encoding: 'utf8', maxBuffer: 1 << 26, env: { ...process.env, ...(a.env || {}) } });
  const lines = (r.stdout || '').split('\n');
  let bad = 0, frames = 0, k = 0, judged = 0, allGot = 0, allN = 0, ctlGot = 0, ctlN = 0;
  for (let i = 0; i < lines.length; i++) {
    const m = lines[i].match(/^(ok  |FAIL) \S+ \(\S+, \S+\): (\d+) of the (\d+) /);
    if (m) {
      const frame = a.frames[k++];
      const n = +m[3], got = +m[2], skip = n < a.frameMin, ok = skip || got / n >= a.frameShare;
      frames++;
      if (n > 0) judged++, allGot += got, allN += n;
      if (!ok) bad++;
      console.log(`${skip ? 'few ' : ok ? 'ok  ' : 'FAIL'} ${a.app} frame ${frame} (${preset}, ${gpu}): ${got} of ${n}`);
      if (!ok && lines[i + 1] && lines[i + 1].startsWith('       none:')) console.log(lines[i + 1]);
    } else if (/^CONTROL PASSED|^ok   control/.test(lines[i])) {
      console.log(lines[i]);
      const c = lines[i].match(/no shadows, (\d+) of (\d+) pass/);
      if (c) {
        ctlGot += +c[1], ctlN += +c[2];
        if (+c[2] >= 2 && +c[1] / +c[2] >= a.frameShare) { console.log(`CONTROL PASSED (${a.app}): a frame with no shadows reached ${a.frameShare * 100}%`); bad++; }
      }
    }
    else if (/^FAIL .* casters on High/.test(lines[i])) { console.log(`${lines[i]} (frame ${a.frames[k++]})`); frames++; bad++; }
    else if (/^FAIL /.test(lines[i]) || /Error/.test(lines[i])) { console.log(lines[i]); bad++; }
  }
  if (r.status !== 0 && !frames) { console.log(`FAIL ${a.app} (${preset}, ${gpu}): the tool failed: ${(r.stderr || '').slice(-400)}`); return 1; }
  if (frames !== a.frames.length) { console.log(`FAIL ${a.app} (${preset}, ${gpu}): ${frames} of ${a.frames.length} frames judged`); bad++; }
  if (lines.some((l) => l.startsWith('CONTROL PASSED'))) bad++;
  const all = allN ? allGot / allN : 0, sweepOk = judged >= a.minFrames && all >= share;
  if (!sweepOk) bad++;
  console.log(`${sweepOk ? 'ok  ' : 'FAIL'} ${a.app} sweep (${preset}, ${gpu}): ${allGot} of the ${allN} shadows High draws at a base over ${judged} frames (${(all * 100).toFixed(0)}%, at least ${share * 100}%; at least ${a.minFrames} frames)`);
  /* the control: the sweep's frames with no shadows must fail it */
  const ctlOk = ctlN > 0 && ctlGot / ctlN < share;
  if (!ctlOk) bad++;
  console.log(`${ctlOk ? 'ok  ' : 'FAIL'} control ${a.app} sweep (${preset}, ${gpu}): no shadows, ${ctlGot} of ${ctlN} (must stay under ${share * 100}%)`);
  return bad;
}
process.exit(fail ? 1 : 0);
