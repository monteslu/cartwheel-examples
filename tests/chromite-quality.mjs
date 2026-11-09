// chromite has a program for everything it draws at every graphics preset on both backends (tests/lib/quality-sweep.mjs):
// the title and menu, a fight, the self-destruct, the autopilot's run, the first command craft (its beam's tell), and
// levels 1 to 10 in play.
//   node tests/chromite-quality.mjs [--gpu gl|webgpu] [--quality verylow,high] [--scene <regex>] [--no-control]
import { sweep } from './lib/quality-sweep.mjs';
import { gateScene } from './lib/gate-scene.mjs';
const level = (n) => ({ name: `level ${n}`, app: 'chromite_auto', frames: 900, env: { CHROMITE_START_LEVEL: String(n) }, presets: ['verylow', 'low', 'medium', 'ultra'],
  reach: { level: n, 'game.state': 1 }, show: ['level', 'ticks', 'enemies'] });
sweep({
  game: 'chromite', tables: ['chromite'],
  scenes: [gateScene('chromite-fight'), gateScene('chromite-title'), gateScene('chromite-menu'), gateScene('chromite-destruct'), gateScene('chromite-auto'),
    { name: 'boss', app: 'chromite_auto', frames: 900, env: { CHROMITE_BOSS: '1' }, presets: ['verylow', 'low', 'medium', 'high', 'ultra'], reach: { level: 1, 'game.state': 1 }, show: ['level', 'ticks', 'enemies'] },
    ...[2, 3, 4, 5, 6, 7, 8, 9, 10].map(level)],
  control: { state: 'physical+map+rep+nmap+ao+rmap+mmap+shadow+env+l1001+xcluster_plain_gi', scene: 'chromite-fight', quality: 'verylow' },
});
