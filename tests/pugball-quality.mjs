// pugball has a program for everything it draws at every graphics preset on both backends (tests/lib/quality-sweep.mjs):
// the title, the team sheet, a kickoff with its replay, the demo, a Cup match, split screen for three and four, and an
// exhibition in each of the five stadiums.
//   node tests/pugball-quality.mjs [--gpu gl|webgpu] [--quality verylow,high] [--scene <regex>] [--no-control]
import { sweep } from './lib/quality-sweep.mjs';
import { gateScene } from './lib/gate-scene.mjs';
const stadium = (n) => ({ name: `stadium ${n}`, app: 'pugball_auto', frames: 700, env: { PUG_STADIUM: String(n) }, presets: ['verylow', 'low', 'medium', 'ultra'],
  reach: { 'game.state': 3, 'game.stadium': (v) => v === n || v === n - 1 }, show: ['game.stadium', 'game.state', 'match.clock'] });
sweep({
  game: 'pugball', tables: ['pugball'],
  scenes: [gateScene('pugball-kickoff'), gateScene('pugball-title'), gateScene('pugball-sheet'), gateScene('pugball-demo'), gateScene('pugball-cup1'), gateScene('pugball-split3'), gateScene('pugball-split4'),
    ...[1, 2, 3, 4, 5].map(stadium)],
  control: { state: 'physical+map+rep+nmap+ao+rmap+senv+sheen+l0301+xcsm_plain', scene: 'pugball-kickoff', quality: 'verylow' },
});
