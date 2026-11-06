// spout has a program for everything it draws at every graphics preset on both backends (tests/lib/quality-sweep.mjs):
// the title, the level select, every level in play, the wash, the sun, the silk and the bird.
//   node tests/spout-quality.mjs [--gpu gl|webgpu] [--quality verylow,high] [--scene <regex>] [--no-control]
import { sweep } from './lib/quality-sweep.mjs';
import { gateScene } from './lib/gate-scene.mjs';
const level = (n) => ({ name: `level ${n}`, app: 'spout_auto', frames: 600, env: { SPOUT_START_LEVEL: String(n) }, presets: ['verylow', 'low', 'medium', 'ultra'],
  reach: { 'game.level': n - 1, 'game.state': 2 }, show: ['game.level', 'ticks', 'checkpoint'] });
sweep({
  game: 'spout', tables: ['spout'],
  scenes: [gateScene('spout-climb'), gateScene('spout-title'), gateScene('spout-select'), gateScene('spout-wash'), gateScene('spout-sun'), gateScene('spout-silk'), gateScene('spout-bird'),
    gateScene('spout-auto'), ...[1, 2, 3, 4, 5].map(level)],
  control: { state: 'physical+map+rep+nmap+ao+rmap+shadow+senv+l1001+xplain', scene: 'spout-climb', quality: 'verylow' },
});
