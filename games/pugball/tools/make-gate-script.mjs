// Builds pugball's gate input scripts (tests/pugball-*.json) and the C table
// the _auto build compiles in (games/pugball/script.inc).
//
//   node games/pugball/tools/make-gate-script.mjs
//
//   pugball-start.json      (A) on the title: the team sheet
//   pugball-idle.json       nothing pressed (controls)
//   pugball-kickoff.json    1v1 against a CPU Rookie: from GO, throttle (R1)
//                           and boost (B) held, a jump (A) 45 ticks in: the
//                           cat goes in off the kickoff (found by
//                           tools/search-goal.mjs)
//   pugball-kickoff-nojump.json  the same without the jump (its control)
//   pugball-demo.json       pugball_demo: throttle and boost at the parked red
//                           pug, a supersonic demolition (control: no boost)
//   pugball-stadium2.json   Exhibition -> stadium 2 (locked on a fresh save,
//                           open on pugball_unlocked); -stadium1: its control
//   pugball-cup1.json       pugball_cup: Pug Cup fixture 1 won off the kickoff
//                           (stadium 2 unlocks); -nojump: its control
//   pugball-split1.json, pugball-split2.json  the same 2v2 with one and two
//                           players (the CPUs fill the rest): with split3 and
//                           split4, tests/pugball-split-bench.mjs's runs
//   pugball-split3.json     three players: pads 2 and 3 join on the team sheet
//                           and pad 3 switches sides (P1 and a CPU against P2
//                           and P3), then everyone drives at the cat; -idle:
//                           its control (nobody drives)
//   pugball-split4.json     four players: pads 2-4 join (2v2, no CPUs) and
//                           drive; P2 and P4 turn ball cam off; -idle: control.
//                           In both, each pad ends holding its own buttons:
//                           P1 R1 + (B), P2 R1, P3 R1 + (B) + (X), P4 (B)
//   script.inc              the _auto build's menus: blue 1 becomes a CPU
//                           All-Star, so the match is four CPUs
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { press, MENU_CPU } from './menu.mjs';
import { play, PLAY } from './search-goal.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const T = join(HERE, '..', '..', '..', 'tests');
const BITS = { south: 0, east: 1, west: 2, north: 3, back: 4, guide: 5, start: 6, l3: 7, r3: 8, l: 9, r: 10, up: 11, down: 12, left: 13, right: 14 };
const put = (name, v) => writeFileSync(join(T, name), JSON.stringify(v, null, 1) + '\n');

put('pugball-start.json', press(30, ['south']));
put('pugball-idle.json', [{ frame: 1, buttons: [] }]);
put('pugball-kickoff.json', play({ steer: null, steerFor: 0, jump: 45, dodge: 0 }));
put('pugball-kickoff-nojump.json', play({ steer: null, steerFor: 0, jump: 0, dodge: 0 }));
/* the demolition (pugball_demo): throttle and boost straight at the parked pug; the control never boosts */
put('pugball-demo.json', play({ steer: null, steerFor: 0, jump: 0, dodge: 0 }));
put('pugball-demo-noboost.json', play({ steer: null, steerFor: 0, jump: 0, dodge: 0 }).map((e) => ({ ...e, buttons: (e.buttons || []).filter((b) => b !== 'east') })));

/* the stadium select: Exhibition, KICK OFF, one step right (stadium 2), (A).
 * Locked on a fresh save (a bump, still on the select); open on _unlocked */
const STADIUM2 = [...press(30, ['south']), ...press(50, ['start']), ...press(70, ['right']), ...press(80, ['south'])];
put('pugball-stadium2.json', STADIUM2);
put('pugball-stadium1.json', [...press(30, ['south']), ...press(50, ['start']), ...press(80, ['south'])]);

/* the Pug Cup's first fixture (pugball_cup: 6 s matches): PUG CUP on the
 * title, fixture 1 (1v1 against a Rookie in stadium 1, the same match as the
 * kickoff gate), the same kickoff goal, then hands off to hold the lead.
 * The control plays the fixture without the jump: no goal, no unlock */
const CUP_MENU = [...press(30, ['right']), ...press(40, ['south']), ...press(70, ['south'])];
const cupPlay = (jump) => {
  const shift = PLAY - 250; /* the cup's menu is 70 frames shorter than the 1v1's */
  const s = [...CUP_MENU, ...play({ steer: null, steerFor: 0, jump, dodge: 0 }).filter((e) => e.frame >= PLAY - 2).map((e) => ({ ...e, frame: e.frame - shift }))];
  s.push({ frame: 250 + 200, buttons: [] });
  return s;
};
put('pugball-cup1.json', cupPlay(45));
put('pugball-cup1-nojump.json', cupPlay(0));

/* split screen: the pads join on the team sheet (pad 1's script is
 * "buttons", pads 2-4 "pad2".."pad4"), pad 1 kicks off in stadium 1, and
 * from GO every pad holds throttle (R1) and boost (B), each steering its own
 * way for a moment. An entry is the whole input from its frame on, so each
 * carries every pad's state. */
const SPLIT_GO = 270; /* stadium 1 picked at frame 90: GO three seconds later */
function split(n, { switchSides = false, drive = true } = {}) {
  const pads = ['buttons', 'pad2', 'pad3', 'pad4'].slice(0, n);
  const at = new Map(); /* frame -> { pad: buttons } changes */
  const set = (f, pad, b) => at.set(f, { ...(at.get(f) || {}), [pad]: b });
  const tap = (f, pad, b) => { set(f, pad, b); set(f + 4, pad, []); };
  tap(30, 'buttons', ['south']);
  for (let k = 1; k < n; k++) tap(32 + 8 * k, pads[k], ['south']);
  if (switchSides) tap(70, 'pad3', ['right']);
  tap(80, 'buttons', ['start']);
  tap(100, 'buttons', ['south']);
  if (drive) {
    const steer = ['left', 'right', 'right', 'left'];
    pads.forEach((p, k) => {
      set(SPLIT_GO + 20 + 3 * k, p, ['r', 'east']);
      set(SPLIT_GO + 30 + 6 * k, p, ['r', 'east', steer[k]]);
      set(SPLIT_GO + 44 + 6 * k, p, ['r', 'east']);
    });
    if (n >= 4) { tap(SPLIT_GO + 90, 'pad2', ['r', 'north']); tap(SPLIT_GO + 96, 'pad4', ['r', 'north']); }
    /* the last stretch: each pad holds its own buttons (the gates read them back per pad, cw.padN) */
    const last = [['r', 'east'], ['r'], ['r', 'east', 'west'], ['east']];
    pads.forEach((p, k) => set(SPLIT_GO + 90 + 20, p, last[k]));
  }
  const cur = Object.fromEntries(pads.map((p) => [p, []]));
  return [...at.keys()].sort((a, b) => a - b).map((frame) => {
    Object.assign(cur, at.get(frame));
    const e = { frame, buttons: cur.buttons };
    for (const p of pads.slice(1)) e[p] = { buttons: cur[p] };
    return e;
  });
}
put('pugball-split1.json', split(1));
put('pugball-split2.json', split(2));
put('pugball-split3.json', split(3, { switchSides: true }));
put('pugball-split3-idle.json', split(3, { switchSides: true, drive: false }));
put('pugball-split4.json', split(4));
put('pugball-split4-idle.json', split(4, { drive: false }));

const rows = MENU_CPU.map((e) => {
  const mask = (e.buttons || []).reduce((m, b) => m | (1 << BITS[b]), 0);
  return `  { ${e.frame}, 0, ${mask}u, 0, 0, 0, 0 },`;
});
writeFileSync(join(HERE, '..', 'script.inc'),
  '/* generated by tools/make-gate-script.mjs: the _auto build\'s pad script\n * (frame, pad, buttons, lx, ly, lt, rt) */\n' + rows.join('\n') + '\n');
console.log('wrote the pugball gate scripts and script.inc');
