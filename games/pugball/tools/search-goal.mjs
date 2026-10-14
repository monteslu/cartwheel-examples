// Searches for a short pad script that scores from the 1v1 kickoff (the
// pugball-kickoff gate): throttle (R1) and boost held from GO, an optional
// steer at the start, a jump and a forward dodge into the cat. Each try is a
// native run; the first that puts blue ahead wins and is written to
// out/pb/search/best.json. Dev tool; the gate script itself is made by
// tools/make-gate-script.mjs from the winning parameters.
//
//   node games/pugball/tools/search-goal.mjs
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { MENU_1V1 } from './menu.mjs';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const OUT = join(ROOT, 'out', 'pb', 'search');
mkdirSync(OUT, { recursive: true });
export const PLAY = 320;
export function play({ steer, steerFor, jump, dodge }) {
  const s = [...MENU_1V1];
  const base = ['r', 'east'];
  s.push({ frame: PLAY - 2, buttons: steer && steerFor ? [...base, steer] : base });
  if (steer && steerFor) s.push({ frame: PLAY + steerFor, buttons: base });
  if (jump) {
    s.push({ frame: PLAY + jump, buttons: [...base, 'south'] });
    s.push({ frame: PLAY + jump + 6, buttons: base });
    if (dodge) {
      s.push({ frame: PLAY + jump + dodge, buttons: [...base, 'south', 'up'] });
      s.push({ frame: PLAY + jump + dodge + 4, buttons: base });
    }
  }
  s.sort((a, b) => a.frame - b.frame);
  return s;
}
function score(script, frames) {
  const inp = join(OUT, 'try.json'), st = join(OUT, 'try-state.json');
  writeFileSync(inp, JSON.stringify(script));
  const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), '--host', 'native', '--app', 'pugball', '--frames', String(frames),
    '--input', inp, '--state', st], { encoding: 'utf8', env: { ...process.env, CW_PROJECT_ROOT: ROOT, PUG_NO_AUDIO: '1' } });
  if (r.status !== 0) throw new Error(r.stderr);
  return JSON.parse(readFileSync(st, 'utf8'));
}
if (process.argv[1] === fileURLToPath(import.meta.url) && process.argv[2] === 'demo') {
  /* a demolition: no jump, steer at the CPU, boost to supersonic */
  for (const steer of ['left', 'right'])
    for (const steerFor of [2, 4, 6, 8, 10, 12, 16, 20, 26, 32]) {
      const t = { steer, steerFor, jump: 0, dodge: 0 };
      const d = score(play(t), PLAY + 240);
      console.log(JSON.stringify(t), d['count.demos'], d['match.score']);
      if (d['count.demos'] > 0) {
        writeFileSync(join(OUT, 'demo.json'), JSON.stringify(t));
        process.exit(0);
      }
    }
} else if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const tries = [];
  for (const steer of [null, 'left', 'right'])
    for (const steerFor of steer ? [4, 8, 14, 22] : [0])
      for (const jump of [0, 30, 45, 60, 75])
        for (const dodge of jump ? [0, 12] : [0]) tries.push({ steer, steerFor, jump, dodge });
  for (const t of tries) {
    const d = score(play(t), PLAY + 420);
    const ok = d['match.score'][0] > 0;
    console.log(JSON.stringify(t), d['match.score'], d['count.hits'], ok ? 'GOAL' : '');
    if (ok) {
      writeFileSync(join(OUT, 'best.json'), JSON.stringify(t));
      break;
    }
  }
}
