// Builds bomberdude's gate input scripts (tests/bomberdude-*.json) and the C
// table the _auto builds compile in (games/bomberdude/script*.inc).
//
//   node games/bomberdude/tools/make-gate-script.mjs
//
// The two-player round (tests/bomberdude-round.json): pads 1 and 2 go
// through the menus (pad 2 joins, the other slots are switched off, arena
// 1), then play a round whose moves were recorded from two hard CPUs
// (BOMBERDUDE_RECORD=out/bb/rec.txt BOMBERDUDE_PLAYERS=2 BOMBERDUDE_CPU_ONLY=1 BOMBERDUDE_ARENA=1);
// it has a chain reaction and power-up pickups in its first 1700 ticks.
// PLAY_FRAME is the engine frame that runs the round's first tick (measured:
// run the menu prefix and read round.tick).
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..', '..', '..');
const T = join(ROOT, 'tests');
const BITS = { south: 0, east: 1, west: 2, north: 3, back: 4, guide: 5, start: 6, l3: 7, r3: 8, l: 9, r: 10, up: 11, down: 12, left: 13, right: 14 };

const press = (frame, buttons, pad = 1) => [{ frame, pad, buttons }, { frame: frame + 4, pad, buttons: [] }];
// the menus, pad 1 (pad: 1) and pad 2 (pad: 2)
// the players screen: the cursor starts on FIGHT; left / right on P2-P4 picks
// who plays it (CPU Normal -> Hard -> off -> pad player ...); A goes on
const MENU_2P = [
  ...press(30, ['right']), ...press(40, ['south']),  // title: BATTLE
  ...press(50, ['south'], 2),                        // pad 2 joins as player 2
  ...press(60, ['up']),                              // P4
  ...press(68, ['right']), ...press(76, ['right']),  // CPU Normal -> Hard -> Off
  ...press(84, ['up']),                              // P3
  ...press(92, ['right']), ...press(100, ['right']),
  ...press(120, ['south']),                          // go -> arena select
  ...press(140, ['south']),                          // arena 1
];
const PLAY_FRAME_2P = 251;                           // frame of round tick 1 (see above)
const MENU_CPU = [
  ...press(30, ['right']), ...press(40, ['south']),  // title: BATTLE
  ...press(90, ['south']),                           // go (the _auto build plays player 1 itself)
  ...press(110, ['south']),                          // arena 1
];
// a player pressing A on the players screen goes on and is still player 1
const CONFIRM = [...press(30, ['right']), ...press(40, ['south']), ...press(60, ['south']), ...press(80, ['south'])];
// merge per-pad change lists into run-cart entries: each entry is the whole state of both pads
function merge(events, twoPads) {
  const frames = [...new Set(events.map((e) => e.frame))].sort((a, b) => a - b);
  const st = { 1: { buttons: [] }, 2: { buttons: [] } };
  const out = [];
  if (twoPads && frames[0] > 1) out.push({ frame: 1, pad2: {} });
  for (const f of frames) {
    for (const e of events.filter((x) => x.frame === f)) st[e.pad] = { buttons: e.buttons };
    const entry = { frame: f };
    if (st[1].buttons.length) entry.buttons = st[1].buttons;
    if (twoPads) entry.pad2 = st[2].buttons.length ? { buttons: st[2].buttons } : {};
    out.push(entry);
  }
  return out;
}

const BIT_BUTTONS = (v) => [v & 1 ? 'left' : null, v & 2 ? 'right' : null, v & 4 ? 'up' : null, v & 8 ? 'down' : null,
  v & 16 ? 'south' : null, v & 32 ? 'east' : null].filter(Boolean);

function recorded(file, maxTicks) {
  const ev = [];
  let prev = [-1, -1];
  for (const line of readFileSync(file, 'utf8').trim().split('\n')) {
    const [tick, a, b] = line.split(' ').map(Number);
    if (tick > maxTicks) break;
    [a, b].forEach((v, p) => {
      if (v !== prev[p]) ev.push({ frame: PLAY_FRAME_2P + tick - 1, pad: p + 1, buttons: BIT_BUTTONS(v) });
      prev[p] = v;
    });
  }
  return ev;
}

const save = (name, script) => writeFileSync(join(T, name), '[\n' + script.map((s) => '  ' + JSON.stringify(s)).join(',\n') + '\n]\n');
save('bomberdude-start.json', merge([...press(30, ['right']), ...press(40, ['south'])], false)); // title -> players
save('bomberdude-quick.json', merge(press(30, ['south']), false)); // QUICK BATTLE: one press
save('bomberdude-confirm.json', merge(CONFIRM, false));
save('bomberdude-idle.json', []);
save('bomberdude-menu2p.json', merge(MENU_2P, true));
const rec = join(ROOT, 'out', 'bb', 'rec.txt');
if (existsSync(rec)) save('bomberdude-round.json', merge([...MENU_2P, ...recorded(rec, 1700)], true));
save('bomberdude-cpu.json', merge(MENU_CPU, false));
// the round with player 2 idle from the start (the control: different outcome)
if (existsSync(rec)) save('bomberdude-round-p2idle.json', merge([...MENU_2P, ...recorded(rec, 1700).filter((e) => e.pad === 1)], true));

// the twist: CPUs on Neon Nights (the unlocked build), the belts carrying them
const TWIST = [...MENU_CPU.slice(0, -2), ...press(100, ['right']), ...press(108, ['right']), ...press(116, ['right']),
  ...press(124, ['right']), ...press(132, ['right']), ...press(150, ['south'])];
save('bomberdude-neon.json', merge(TWIST, false));
const ICE = [...MENU_CPU.slice(0, -2), ...press(100, ['right']), ...press(150, ['south'])];
save('bomberdude-frost.json', merge(ICE, false));

// the _auto builds: the CPU round (input-free hosts)
function inc(name, script) {
  const lines = [];
  for (const s of script) {
    const m1 = (s.buttons || []).reduce((m, b) => m | (1 << BITS[b]), 0);
    lines.push(`  { ${s.frame}, 0, ${m1}u, 0.0f, 0.0f },`);
    if (s.pad2) lines.push(`  { ${s.frame}, 1, ${(s.pad2.buttons || []).reduce((m, b) => m | (1 << BITS[b]), 0)}u, 0.0f, 0.0f },`);
  }
  writeFileSync(join(HERE, '..', name), `/* generated by tools/make-gate-script.mjs */\n${lines.join('\n')}\n`);
}
inc('script.inc', merge(MENU_CPU, false));
console.log('wrote the bomberdude gate scripts and script.inc');
