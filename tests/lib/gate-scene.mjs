// A quality sweep scene (tests/lib/quality-sweep.mjs) made from one of the gates in tests/gates.json: the gate's app,
// frames, input script, start commands and environment, and its expected debug state as the proof the run got where the
// gate goes (the simulation never sees the graphics preset, so a gate's expected state holds at every preset). Fields
// the engine sets per host or per preset (cw.*, render.*) are left out of the proof.
//
//   gateScene('spout-climb')                            the gate as it is
//   gateScene('racer-salt', { frames: 1500, presets: ['verylow', 'low', 'ultra'] })
//   gateScene('x', { name, frames, cmds (added), env (added), reach (instead of the gate's), presets, swap, show })
import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const GATES = JSON.parse(readFileSync(join(ROOT, 'tests/gates.json'), 'utf8'));

/** the gate's expected state as a reach: arrays compared whole */
export function reachOf(expect = {}) {
  const out = {};
  for (const [k, want] of Object.entries(expect)) {
    if (/^(cw|render)\./.test(k)) continue;
    out[k] = Array.isArray(want) ? (_, s) => JSON.stringify(s[k]) === JSON.stringify(want) : want;
  }
  return out;
}

export function gateScene(gate, o = {}) {
  const g = GATES.find((x) => x.name === gate);
  if (!g || !g.app) throw new Error(`no app gate ${gate} in tests/gates.json`);
  const capped = o.frames && o.frames !== g.frames;
  if (capped && !o.reach) throw new Error(`${gate}: a scene cut short needs its own reach (the gate's expected state is for frame ${g.frames})`);
  return {
    name: o.name || gate,
    app: o.app || g.app,
    frames: o.frames || g.frames,
    cmds: [...(g.startCmds || []), ...(o.cmds || [])],
    env: { ...(g.env || {}), ...(o.env || {}) },
    ...(g.input ? { input: JSON.parse(readFileSync(join(ROOT, 'tests', g.input), 'utf8')) } : {}),
    reach: o.reach || reachOf(g.expect),
    ...(o.presets ? { presets: o.presets } : {}),
    ...(o.swap ? { swap: true } : {}),
    spread: false, // the gate's input script is timed for the warm-up in frame 1
    show: o.show || Object.keys(g.expect || {}).filter((k) => !/^(cw|render)\./.test(k)).slice(0, 6),
  };
}
