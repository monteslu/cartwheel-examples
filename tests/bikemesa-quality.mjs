// BikeMesa has a program for everything it draws at every graphics preset on both backends (tests/lib/quality-sweep.mjs):
// the title, a ride by real input, split screen for two, three and four, and every
// track ridden.
//   node tests/bikemesa-quality.mjs [--gpu gl|webgpu] [--quality verylow,high] [--scene <regex>] [--no-control]
import { sweep } from './lib/quality-sweep.mjs';
import { gateScene } from './lib/gate-scene.mjs';
// a ride on bikemesa_unlocked from a start request, every seat pedalling from frame 190 (the countdown is over by then)
const pedal = (players) => [{ frame: 190, buttons: ['south'], ...Object.fromEntries(Array.from({ length: players - 1 }, (_, i) => [`pad${i + 2}`, { buttons: ['south'] }])) }];
const unlocked = (o) => ({ app: 'bikemesa_unlocked', env: {}, spread: false, ...o });
const track = (n) => unlocked({ name: `track ${n}`, frames: 700, cmds: [`track ${n}`], input: pedal(1), presets: ['verylow', 'low', 'ultra'],
  reach: { 'game.state': (v) => v >= 2 }, show: ['game.state', 'game.track', 'checkpoint'] });
sweep({
  game: 'bikemesa', tables: ['bikemesa'],
  scenes: [gateScene('bikemesa-ride', { reach: { 'game.state': (v) => v >= 3, checkpoint: (v) => v >= 2 } }), gateScene('bikemesa-title'),
    ...[2, 3, 4].map((n) => unlocked({ name: `split ${n}`, frames: 900, cmds: [`players ${n}`, 'track 1'], input: pedal(2), presets: ['verylow', 'low', 'high'],
      reach: { players: n, 'game.state': (v) => v >= 3 }, show: ['players', 'game.state', 'loops'] })),
    ...Array.from({ length: 19 }, (_, i) => track(i + 1))],
  control: { state: 'physical+shadow+senv+l1001+xplain', scene: 'track 16', quality: 'verylow' },
});
