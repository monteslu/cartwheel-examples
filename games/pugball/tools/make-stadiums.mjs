// Writes pugball's stadiums (assets/scenes/stadium1..N.cwscene and the title
// backdrop title.cwscene) from the moods below. The output is ordinary
// .cwscene text; rerun after editing:
//
//   node games/pugball/tools/make-stadiums.mjs
//
// Moods are physical: linear colours (picked as screen colours, converted),
// the sky drives the sun (or the moon, at night), energies in three.js r186
// units. Every stadium carries the same three point lights (goal flashes and
// floodlight fill): a light that came and went would change every material's
// program.
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const D = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'scenes');
mkdirSync(D, { recursive: true });
const f = (x) => { const s = (Math.round(x * 10000) / 10000).toString(); return s === '-0' ? '0' : s; };
const toLin = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
const col = (r, g, b, a = 1) => `color(${[toLin(r), toLin(g), toLin(b)].map(f).join(', ')}, ${f(a)})`;
const str = (s) => `"${s}"`;

const FLASHES = 3;
export const THEMES = ['PARK', 'NEON', 'BEACH', 'SNOW', 'GARDEN'];

export const STADIUMS = [
  {
    title: 'Barkley Park', theme: 'PARK', blurb: 'Sunshine, picnic blankets and a very grumpy cat.', seed: 11, cpu: 0, secs: 300,
    mood: { time: 15.2, lat: 40, dec: 14, north: 25, sun: 6, sky: 1, mie: 1, fog: 0.0025, exposure: 0.85, bloom: 0.04,
      ground: [0.32, 0.45, 0.2], ambient: [1, 0.93, 0.82], hemi: 0.25, grade: 'park' },
  },
  {
    title: 'Neon Kennel Bowl', theme: 'NEON', blurb: 'Floodlights, neon and a crowd that never sleeps.', seed: 23, cpu: 1, secs: 300,
    mood: { time: 22.5, lat: 40, dec: 10, north: 60, sun: 0.8, sky: 0.55, mie: 0.5, fog: 0.004, exposure: 1.2, bloom: 0.1,
      ground: [0.18, 0.18, 0.26], ambient: [0.7, 0.75, 1], hemi: 0.9, grade: 'neon', night: [0.03, 0.04, 0.11], moon: 4.2,
      flood: 1 },
  },
  {
    title: 'Sunset Sands', theme: 'BEACH', blurb: 'The sun goes down, the cat stays up.', seed: 37, cpu: 1, secs: 300,
    mood: { time: 17.3, lat: 24, dec: 18, north: 200, sun: 7, sky: 1.1, mie: 2.2, fog: 0.004, exposure: 0.95, bloom: 0.07,
      ground: [0.8, 0.62, 0.42], ambient: [1, 0.78, 0.6], hemi: 0.28, grade: 'beach' },
  },
  {
    title: 'Frosty Backyard', theme: 'SNOW', blurb: 'Snow on the fence, steam on your breath.', seed: 53, cpu: 2, secs: 300,
    mood: { time: 12.6, lat: 46, dec: -6, north: 40, sun: 6, sky: 1.15, mie: 0.8, fog: 0.004, exposure: 0.88, bloom: 0.05,
      ground: [0.85, 0.9, 0.98], ambient: [0.8, 0.88, 1], hemi: 0.32, grade: 'snow' },
  },
  {
    title: 'Royal Rose Garden', theme: 'GARDEN', blurb: 'Hedges, roses and a fountain. Mind the topiary.', seed: 71, cpu: 2, secs: 300,
    mood: { time: 10.2, lat: 50, dec: 16, north: 110, sun: 6, sky: 1, mie: 1.2, fog: 0.003, exposure: 0.88, bloom: 0.05,
      ground: [0.28, 0.42, 0.2], ambient: [1, 0.95, 0.88], hemi: 0.26, grade: 'garden' },
  },
];

function scene(index, a) {
  const m = a.mood;
  const out = ['[scene format=1]', ''];
  const node = (name, parent, type, props) => {
    out.push(parent ? `[node name="${name}" parent="${parent}" type="${type}"]` : `[node name="${name}" type="${type}"]`);
    for (const [k, v] of Object.entries(props)) out.push(`${k} = ${v}`);
    out.push('');
  };
  node('Stadium', null, 'Stadium', {
    index, title: str(a.title), blurb: str(a.blurb), theme: a.theme, seed: a.seed, cpu_level: a.cpu, match_secs: a.secs,
  });
  node('Sky', '.', 'ProceduralSky', {
    sun: m.moon ? str('') : str('../Sun'), time_of_day: f(m.time), latitude: f(m.lat), declination: f(m.dec), north: f(m.north),
    sun_energy: f(m.sun), sky_energy: f(m.sky), mie: f(m.mie), sun_disk_intensity: f(1.2), ground_color: col(...m.ground),
    ...(m.night ? { night_color: `color(${m.night.map(f).join(', ')}, 1)` } : {}), env_update_angle: 2,
  });
  node('Sun', '.', 'DirectionalLight3D', {
    ...(m.moon ? { position: 'vec3(-14, 30, 18)', rotation: 'vec3(-1.05, -0.5, 0)', color: col(0.75, 0.8, 1), energy: f(m.moon) } : {}),
    shadow: true, shadow_size: 2048, shadow_extent: 28, shadow_far: 140, shadow_bias: f(-0.0003), shadow_normal_bias: f(0.03),
  });
  node('Env', '.', 'WorldEnvironment', {
    background: col(0.3, 0.45, 0.7), ambient_color: col(...m.ambient), ambient_energy: f(0.05), sky_color: col(...m.ambient),
    ground_color: col(...m.ground), hemi_energy: f(m.hemi),
  });
  node('Fog', '.', 'HeightFog', {
    density: f(m.fog), height_falloff: f(0.12), base_height: 0, use_sky_color: true, sun_scatter: f(0.35), sun_g: f(0.8),
    max_opacity: f(0.6), start_distance: 40, sky_distance: 400,
  });
  node('Post', '.', 'PostProcess', {
    tonemap: 'AGX', exposure: f(m.exposure), bloom: true, bloom_intensity: f(m.bloom), bloom_threshold: f(1.1), bloom_knee: f(0.6),
    ssao: true, ssao_radius: f(0.8), ssao_intensity: f(0.9), aa: 'SMAA', vignette: true, vignette_intensity: f(0.25),
    vignette_smoothness: f(0.6), grade: true, lut: str(`grades/${m.grade}.cube`), grade_intensity: f(0.8),
  });
  node('Camera', '.', 'Camera3D', { position: 'vec3(0, 12, -40)', fov: 62, near: f(0.2), far: 600 });
  for (let i = 0; i < FLASHES; i++)
    node(`Flash${i}`, '.', 'OmniLight3D', { position: 'vec3(0, -50, 0)', color: col(1, 0.8, 0.5), energy: 0, range: 18, decay: 2 });
  return out.join('\n');
}

STADIUMS.forEach((a, i) => writeFileSync(join(D, `stadium${i + 1}.cwscene`), scene(i + 1, a) + '\n'));
const title = { ...STADIUMS[0], title: 'Pugball', seed: 5,
  mood: { ...STADIUMS[0].mood, time: 17.6, sun: 6.5, mie: 1.4, fog: 0.003, exposure: 0.9, bloom: 0.06 } };
writeFileSync(join(D, 'title.cwscene'), scene(0, title) + '\n');
console.log(`wrote ${STADIUMS.length} stadiums and the title to ${D}`);
