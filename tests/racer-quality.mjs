// racer has a program for everything it draws at every graphics preset on both backends (tests/lib/quality-sweep.mjs):
// a race on each of the eight maps, the battle arenas and their modes, a time trial, the title, and split screen for two
// and four players (Very Low split screen crashed the engine before v0.33.1). Every map's race also runs with the preset
// switched mid-race, High to Very Low and back and the reverse (swapdown, swapup): a race built at High and drawn at Very
// Low kept a light set the Very Low terrain had no program for, and frost, temple and orbit lost their ground.
//   node tests/racer-quality.mjs [--gpu gl|webgpu] [--quality verylow,high|swapdown,swapup] [--scene <regex>] [--no-control] [--shard i/n]
import { sweep } from './lib/quality-sweep.mjs';
import { gateScene } from './lib/gate-scene.mjs';
const racing = { 'race.loaded': 1, 'race.phase': 2 };
const MAPS = ['salt', 'drowned', 'fair', 'refinery', 'dam', 'frost', 'temple', 'orbit'];
const race = (m) => gateScene(`racer-${m}`, { frames: 1500, presets: ['verylow', 'low', 'medium', 'ultra'], swap: true, reach: racing, show: ['race.loaded', 'race.phase'] });
const battle = (g) => gateScene(g, { frames: 1500, presets: ['verylow', 'low', 'ultra'], reach: { 'race.loaded': 1, 'race.phase': (v) => v >= 2 }, show: ['race.loaded', 'race.phase', 'battle.mode'] });
const split = (n, m) => ({ name: `split ${n} ${MAPS[m]}`, app: 'racer', frames: 1200, cmds: ['bot 1', `race ${m} ${n} 3`], presets: ['verylow', 'low', 'high'],
  reach: { ...racing, views: n }, show: ['race.loaded', 'race.phase', 'views'] });
sweep({
  game: 'racer', tables: ['racer'],
  scenes: [race('salt'), ...MAPS.slice(1).map(race), battle('racer-battle'), battle('racer-ctf'), battle('racer-ctf-spires'), battle('racer-assault'), battle('racer-koth'),
    gateScene('racer-trial', { frames: 1500, presets: ['verylow', 'low', 'ultra'], reach: { 'race.loaded': 1 } }),
    { name: 'title', app: 'racer', frames: 300, reach: { ticks: (v) => v >= 0 }, show: ['ui.mode'] },
    split(2, 0), split(4, 0), split(2, 2), split(4, 6)],
  // the control: the terrain's Very Low state out of the table (it covers the fewer-light state a Very Low build draws): frost
  // built at High and switched to Very Low must report it and skip its ground, as the table before the switch runs did
  control: { state: 'standard+instbig+shadow+senv+l3001+s100+xterrainmin_cluster_plain', scene: 'racer-frost', quality: 'swapdown' },
});
