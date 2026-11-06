// bomberdude has a program for everything it draws at every graphics preset on both backends (tests/lib/quality-sweep.mjs):
// the title, the players screen, a round by real input, the autopilot's match, and a Battle round in each of the eight arenas.
//   node tests/bomberdude-quality.mjs [--gpu gl|webgpu] [--quality verylow,high] [--scene <regex>] [--no-control]
import { sweep } from './lib/quality-sweep.mjs';
import { gateScene } from './lib/gate-scene.mjs';
const arena = (n) => ({ name: `arena ${n}`, app: 'bomberdude_auto', frames: 700, env: { BOMBERDUDE_ARENA: String(n) }, presets: ['verylow', 'low', 'medium', 'ultra'],
  reach: { 'game.arena': n, 'game.state': 4 }, show: ['game.arena', 'game.state', 'round.timer'] });
sweep({
  game: 'bomberdude', tables: ['bomberdude'],
  scenes: [gateScene('bomberdude-round'), gateScene('bomberdude-title'), gateScene('bomberdude-players'), gateScene('bomberdude-auto'), gateScene('bomberdude-neon'), gateScene('bomberdude-frost'),
    ...[1, 2, 3, 4, 5, 6, 7, 8].map(arena)],
  control: { state: 'physical+map+rep+nmap+ao+rmap+shadow+senv+sheen+l1001+xcluster_plain', scene: 'bomberdude-round', quality: 'verylow' },
});
