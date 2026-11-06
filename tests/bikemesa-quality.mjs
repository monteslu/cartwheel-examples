// BikeMesa has a program for everything it draws at every graphics preset on both backends (tests/lib/quality-sweep.mjs):
// the title, a ride by real input, split screen for two, three and four, and every
// track ridden.
//   node tests/bikemesa-quality.mjs [--gpu gl|webgpu] [--quality verylow,high] [--scene <regex>] [--no-control]
import { sweep } from './lib/quality-sweep.mjs';
import { gateScene } from './lib/gate-scene.mjs';
const track = (n) => gateScene('bikemesa-wreck', { name: `track ${n}`, frames: 700, cmds: [], presets: ['verylow', 'low', 'ultra'],
  reach: { 'game.state': (v) => v >= 2 }, show: ['game.state', 'game.track', 'checkpoint'] });
sweep({
  game: 'bikemesa', tables: ['bikemesa'],
  scenes: [gateScene('bikemesa-ride', { reach: { 'game.state': (v) => v >= 3, checkpoint: (v) => v >= 2 } }), gateScene('bikemesa-title'),
    ...[2, 3, 4].map((n) => ({ ...gateScene('bikemesa-loop-lanes', { name: `split ${n}`, frames: 900, presets: ['verylow', 'low', 'high'], reach: { players: n, 'game.state': (v) => v >= 3 } }), cmds: [`players ${n}`, 'track 1'] })),
    ...Array.from({ length: 19 }, (_, i) => ({ ...track(i + 1), cmds: [`track ${i + 1}`] }))],
  control: { state: 'standard+vcol+shadow+skin+senv+l1001+xplain', scene: 'bikemesa-ride', quality: 'verylow' },
});
