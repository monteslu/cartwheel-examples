// Writes bomberdude's arenas (assets/scenes/arena1..8.cwscene and the
// title backdrop title.cwscene) from the layouts and moods below. The output
// is ordinary .cwscene text; rerun after editing:
//
//   node games/bomberdude/tools/make-arenas.mjs
//
// A layout is 13 rows of 15 tiles (sim.c reads it):
//   #  wall          +  crate             .  open (never a crate)
//   ?  maybe a crate (crate_pct)          1-4  where each player starts
//   i  ice  I  ice that may hold a crate  x  forge vent
//   t  teleporter (pairs in reading order: 1st-2nd, 3rd-4th)
//   E W N S  belts running east / west / north (up the screen) / south
//   ~  sand (slow)  :  sand that may hold a crate
//   c  a rim wall with a cannon firing along its row, k along its column
// Moods are physical: linear colours (picked as screen colours, converted),
// the sky drives the sun, energies in three.js r186 units.
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const D = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'scenes');
mkdirSync(D, { recursive: true });
const f = (x) => { const s = (Math.round(x * 10000) / 10000).toString(); return s === '-0' ? '0' : s; };
const toLin = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
const col = (r, g, b, a = 1) => `color(${[toLin(r), toLin(g), toLin(b)].map(f).join(', ')}, ${f(a)})`;
const str = (s) => `"${s}"`;

// point lights for blast flashes (three.c's table has this game's l1301 states)
const FLASHES = 3;
const THEMES = ['MEADOW', 'FROST', 'FORGE', 'TEMPLE', 'CANDY', 'NEON', 'PIRATE', 'DESERT'];

const ARENAS = [
  {
    title: 'Clover Commons', theme: 'MEADOW', blurb: 'A sunny lawn. Nothing up its sleeve.', crate: 72, seed: 11, cpu: 1, cpuLevel: 0, secs: 150,
    layout: `
###############
#1.?????????.4#
#.#?#?#?#?#?#.#
#?????????????#
#?#?#?#?#?#?#?#
#?????????????#
#?#?#?#.#?#?#?#
#?????????????#
#?#?#?#?#?#?#?#
#?????????????#
#.#?#?#?#?#?#.#
#3.?????????.2#
###############`,
    mood: { time: 15.6, lat: 40, dec: 12, north: 30, sun: 6, sky: 1, mie: 1, fog: 0.004, exposure: 0.85, bloom: 0.04,
      ground: [0.3, 0.42, 0.18], ambient: [1, 0.92, 0.8], hemi: 0.25, grade: 'meadow' },
  },
  {
    title: 'Frostbite Pond', theme: 'FROST', blurb: 'Ice! Once you start sliding, you keep sliding.', crate: 64, seed: 23, cpu: 1, cpuLevel: 1, secs: 150,
    layout: `
###############
#1.??IIIII??.4#
#.#?#I#I#I#?#.#
#??IIIIIIIII??#
#?#I#I#.#I#I#?#
#?IIIIiiiIIII?#
#?#I#.#.#.#I#?#
#?IIIIiiiIIII?#
#?#I#I#.#I#I#?#
#??IIIIIIIII??#
#.#?#I#I#I#?#.#
#3.??IIIII??.2#
###############`,
    mood: { time: 15.6, lat: 62, dec: -8, north: 20, sun: 5, sky: 1.1, mie: 0.6, fog: 0.012, exposure: 0.95, bloom: 0.05,
      ground: [0.8, 0.86, 0.95], ambient: [0.75, 0.85, 1], hemi: 0.3, grade: 'frost' },
  },
  {
    title: 'Ember Forge', theme: 'FORGE', blurb: 'The vents glow, then they roar. Mind the glow.', crate: 70, seed: 37, cpu: 2, cpuLevel: 0, secs: 150,
    layout: `
###############
#1.?????????.4#
#.#?#?#?#?#?#.#
#??x???????x??#
#?#?#?#?#?#?#?#
#?????????????#
#?#?#?#x#?#?#?#
#?????????????#
#?#?#?#?#?#?#?#
#??x???????x??#
#.#?#?#?#?#?#.#
#3.?????????.2#
###############`,
    mood: { time: 17.9, lat: 35, dec: 10, north: 250, sun: 6.5, sky: 0.9, mie: 3, fog: 0.01, exposure: 1.1, bloom: 0.08,
      ground: [0.12, 0.08, 0.06], ambient: [1, 0.55, 0.3], hemi: 0.18, grade: 'forge' },
  },
  {
    title: 'Jungle Temple', theme: 'TEMPLE', blurb: 'Step on a glyph, come out of its twin.', crate: 66, seed: 41, cpu: 2, cpuLevel: 1, secs: 160,
    layout: `
###############
#1.?????????.4#
#.#?#?###?#?#.#
#??????t??????#
#?#?#?#?#?#?#?#
#?????????????#
#t#?#?#.#?#?#t#
#?????????????#
#?#?#?#?#?#?#?#
#??????t??????#
#.#?#?###?#?#.#
#3.?????????.2#
###############`,
    mood: { time: 9.8, lat: 8, dec: 5, north: 120, sun: 5.5, sky: 1, mie: 2, fog: 0.011, exposure: 0.9, bloom: 0.05,
      ground: [0.2, 0.3, 0.12], ambient: [0.85, 1, 0.8], hemi: 0.25, grade: 'temple' },
  },
  {
    title: 'Gumdrop Gardens', theme: 'CANDY', blurb: 'Jelly crates wobble off the first blast. Hit them twice.', crate: 60, seed: 53, cpu: 3, cpuLevel: 0, secs: 170,
    layout: `
###############
#1.?????????.4#
#.#?#?#?#?#?#.#
#???+?????+???#
#?#?#?#?#?#?#?#
#??+???.???+??#
#?#?#?#.#?#?#?#
#??+???.???+??#
#?#?#?#?#?#?#?#
#???+?????+???#
#.#?#?#?#?#?#.#
#3.?????????.2#
###############`,
    mood: { time: 15.2, lat: 30, dec: 18, north: 70, sun: 5.5, sky: 1.2, mie: 0.8, fog: 0.005, exposure: 0.72, bloom: 0.05,
      ground: [0.95, 0.7, 0.8], ambient: [1, 0.85, 0.95], hemi: 0.3, grade: 'candy' },
  },
  {
    title: 'Neon Nights', theme: 'NEON', blurb: 'The belts never stop. Neither does the city.', crate: 66, seed: 67, cpu: 3, cpuLevel: 1, secs: 170,
    layout: `
###############
#1.?????????.4#
#.#?#?#?#?#?#.#
#??EEEEEEEES??#
#?#N#?#?#?#S#?#
#??N???.???S??#
#?#N#?#.#?#S#?#
#??N???.???S??#
#?#N#?#?#?#S#?#
#??NWWWWWWWW??#
#.#?#?#?#?#?#.#
#3.?????????.2#
###############`,
    // night: the sky's sun is below the horizon; a cold moon light casts the shadows
    mood: { time: 23.2, lat: 40, dec: 0, north: 0, sun: 6, sky: 1, mie: 1, fog: 0.008, exposure: 1.5, bloom: 0.12,
      ground: [0.05, 0.05, 0.08], ambient: [0.6, 0.5, 1], hemi: 0.9, grade: 'neon', moon: 2.6, night: [0.012, 0.014, 0.03] },
  },
  {
    title: 'Powder Deck', theme: 'PIRATE', blurb: 'When a cannon glows, its whole row is about to burn.', crate: 68, seed: 79, cpu: 3, cpuLevel: 1, secs: 170,
    layout: `
#########k#####
#1.?????????.4#
#.#?#?#?#?#?#.#
c?????????????#
#?#?#?#?#?#?#?#
#?????????????c
#?#?#?#?#?#?#?#
c?????????????#
#?#?#?#?#?#?#?#
#?????????????c
#.#?#?#?#?#?#.#
#3.?????????.2#
#####k#########`,
    mood: { time: 16.9, lat: 20, dec: 15, north: 280, sun: 7, sky: 1, mie: 1.6, fog: 0.005, exposure: 0.95, bloom: 0.06,
      ground: [0.08, 0.2, 0.3], ambient: [1, 0.8, 0.6], hemi: 0.22, grade: 'pirate' },
  },
  {
    title: 'Sandstorm Ruins', theme: 'DESERT', blurb: 'Sand drags your feet. The storms hide the bombs.', crate: 70, seed: 97, cpu: 3, cpuLevel: 2, secs: 180,
    layout: `
###############
#1.:::???:::.4#
#.#:#?#?#?#:#.#
#?::??~~~??::?#
#?#?#~#.#~#?#?#
#??::~~~~~::??#
#?#~#.#.#.#~#?#
#??::~~~~~::??#
#?#?#~#.#~#?#?#
#?::??~~~??::?#
#.#:#?#?#?#:#.#
#3.:::???:::.2#
###############`,
    mood: { time: 16.2, lat: 26, dec: 20, north: 200, sun: 7, sky: 1, mie: 3, fog: 0.01, exposure: 0.8, bloom: 0.05,
      ground: [0.75, 0.6, 0.4], ambient: [1, 0.85, 0.65], hemi: 0.25, grade: 'desert' },
  },
];

function scene(index, a) {
  const m = a.mood;
  const rows = a.layout.trim().split('\n').map((r) => r.trim());
  if (rows.length !== 13 || rows.some((r) => r.length !== 15)) throw new Error(`${a.title}: layout must be 13 rows of 15`);
  const out = ['[scene format=1]', ''];
  const node = (name, parent, type, props) => {
    out.push(parent ? `[node name="${name}" parent="${parent}" type="${type}"]` : `[node name="${name}" type="${type}"]`);
    for (const [k, v] of Object.entries(props)) out.push(`${k} = ${v}`);
    out.push('');
  };
  node('Arena', null, 'Arena', {
    index, title: str(a.title), blurb: str(a.blurb), theme: a.theme, layout: str(rows.join('|')), crate_pct: a.crate, seed: a.seed,
    cpu: a.cpu, cpu_level: a.cpuLevel, round_secs: a.secs,
  });
  node('Sky', '.', 'ProceduralSky', {
    sun: m.moon ? str('') : str('../Sun'), time_of_day: f(m.time), latitude: f(m.lat), declination: f(m.dec), north: f(m.north),
    sun_energy: f(m.sun), sky_energy: f(m.sky), mie: f(m.mie), sun_disk_intensity: f(1.2), ground_color: col(...m.ground),
    ...(m.night ? { night_color: `color(${m.night.map(f).join(', ')}, 1)` } : {}), env_update_angle: 2,
  });
  node('Sun', '.', 'DirectionalLight3D', {
    ...(m.moon ? { position: 'vec3(-6, 14, 9)', rotation: 'vec3(-0.95, -0.45, 0)', color: col(0.62, 0.7, 1), energy: f(m.moon) } : {}),
    shadow: true, shadow_size: 2048, shadow_extent: 12, shadow_far: 80, shadow_bias: f(-0.0004), shadow_normal_bias: f(0.025),
  });
  node('Env', '.', 'WorldEnvironment', {
    background: col(0.3, 0.45, 0.7), ambient_color: col(...m.ambient), ambient_energy: f(0.05), sky_color: col(...m.ambient),
    ground_color: col(...m.ground), hemi_energy: f(m.hemi),
  });
  node('Fog', '.', 'HeightFog', {
    density: f(m.fog), height_falloff: f(0.18), base_height: 0, use_sky_color: true, sun_scatter: f(0.35), sun_g: f(0.8),
    max_opacity: f(0.7), start_distance: 14, sky_distance: 300,
  });
  node('Post', '.', 'PostProcess', {
    tonemap: 'AGX', exposure: f(m.exposure), bloom: true, bloom_intensity: f(m.bloom), bloom_threshold: f(1.2), bloom_knee: f(0.6),
    ssao: true, ssao_radius: f(0.6), ssao_intensity: f(0.9), aa: 'SMAA', vignette: true, vignette_intensity: f(0.25),
    vignette_smoothness: f(0.6), grade: true, lut: str(`grades/${m.grade}.cube`), grade_intensity: f(0.8),
  });
  node('Camera', '.', 'Camera3D', { position: 'vec3(0, 16, 12)', fov: 38, near: f(0.3), far: 400 });
  // explosion flashes: three point lights that are always there (a light that
  // comes and goes would change every material's program), dark until a blast
  for (let i = 0; i < FLASHES; i++)
    node(`Flash${i}`, '.', 'OmniLight3D', { position: 'vec3(0, -50, 0)', color: col(1, 0.62, 0.3), energy: 0, range: 7, decay: 2 });
  return out.join('\n');
}

ARENAS.forEach((a, i) => writeFileSync(join(D, `arena${i + 1}.cwscene`), scene(i + 1, a) + '\n'));
// the title backdrop: the meadow at golden hour, crates everywhere
const title = { ...ARENAS[0], title: 'BomberDude', crate: 88, seed: 5,
  mood: { ...ARENAS[0].mood, time: 17.4, sun: 6.5, mie: 1.4, fog: 0.006, exposure: 0.9, bloom: 0.06 } };
writeFileSync(join(D, 'title.cwscene'), scene(0, title) + '\n');
console.log(`wrote ${ARENAS.length} arenas and the title to ${D}`);
export { ARENAS, THEMES };
