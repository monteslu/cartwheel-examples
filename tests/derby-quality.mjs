// derby has a program for everything it draws at every graphics preset on both backends (tests/lib/quality-sweep.mjs):
// the title, a quick bout by real input, split screen for three and four, a wreck, the autopilot's bout to its results,
// and a bout in each arena.
//   node tests/derby-quality.mjs [--gpu gl|webgpu] [--quality verylow,high] [--scene <regex>] [--no-control]
import { sweep } from './lib/quality-sweep.mjs';
import { gateScene } from './lib/gate-scene.mjs';
const bout = (n) => ({ name: `bout ${n}`, app: 'derby_auto', frames: 700, env: { DERBY_BOUT: String(n) }, presets: ['verylow', 'low', 'medium', 'ultra'],
  reach: { 'game.state': 1 }, show: ['game.state', 'bout.phase', 'bout.alive'] });
sweep({
  game: 'derby', tables: ['derby'],
  scenes: [gateScene('derby-quick'), gateScene('derby-title'), gateScene('derby-split3'), gateScene('derby-split4'), gateScene('derby-ram'), gateScene('derby-auto'), bout(1), bout(2)],
  control: { state: 'physical+map+rep+nmap+ao+rmap+shadow+senv+l1001+xcluster_plain', scene: 'derby-quick', quality: 'verylow' },
});
