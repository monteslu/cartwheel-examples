// bomberdude has a program for everything it draws at every graphics preset on both backends (tests/lib/quality-sweep.mjs):
// the title, the players screen, a round by real input, the autopilot's match, and a Battle round in each of the eight arenas.
//   node tests/bomberdude-quality.mjs [--gpu gl|webgpu] [--quality verylow,high] [--scene <regex>] [--no-control]
import { readFileSync } from 'node:fs';
import { sweep } from './lib/quality-sweep.mjs';
import { gateScene } from './lib/gate-scene.mjs';
const arena = (n) => ({ name: `arena ${n}`, app: 'bomberdude_auto', frames: 700, env: { BOMBERDUDE_ARENA: String(n) }, presets: ['verylow', 'low', 'medium', 'ultra'],
  reach: { 'game.arena': n, 'game.state': 4 }, show: ['game.arena', 'game.state', 'round.timer'] });
sweep({
  game: 'bomberdude', tables: ['bomberdude'],
  scenes: [gateScene('bomberdude-round'), gateScene('bomberdude-title'), gateScene('bomberdude-players'), gateScene('bomberdude-auto'), gateScene('bomberdude-neon'), gateScene('bomberdude-frost'),
    ...[1, 2, 3, 4, 5, 6, 7, 8].map(arena)],
  // the control: every foliage state of the table out (the meadow grass's). The old control, a plain physical state, reported
  // nothing on v0.78 tables, and neither does the grass's Very Low state alone (physical+instbig+shadow+senv+icol+sheen+l1001+xcluster_foliage_plain) nor with its
  // fogged twin: the engine draws it with another foliage row of the closure. With none left, the sweep must report the
  // grass's state and skip or cover its draws
  control: { state: readFileSync(new URL('../games/bomberdude/programs/bomberdude.txt', import.meta.url), 'utf8').split('\n').filter((l) => /foliage/.test(l) && !l.startsWith('#')), scene: 'bomberdude-round', quality: 'verylow' },
});
