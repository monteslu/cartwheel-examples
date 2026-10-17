// Writes derby's arena scenes (assets/scenes/arena<n>.cwscene): the Arena
// root, a low dusk sun through haze, clustered lighting (the floodlights and
// fires, their shadows in the atlas), the environment, dust fog (volumetric
// where the game turns it on: main.c apply_fog), the post stack (AgX, bloom,
// GTAO, SMAA, a warm worn grade, a vignette), the camera and three
// crash-flash lights the effects borrow.
//
//   node games/derby/tools/make-arenas.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const D = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'scenes');
mkdirSync(D, { recursive: true });
const f = (x) => (Number.isInteger(x) ? String(x) : String(+x.toFixed(4)));
const col = (r, g, b, a = 1) => `color(${f(r)}, ${f(g)}, ${f(b)}, ${f(a)})`;
const str = (s) => JSON.stringify(s);

const ARENAS = [
  {
    index: 1, title: 'County Fairground Pit', blurb: 'Dirt, diesel and a crowd that came for the wrecks.', theme: 'DUSK', seed: 19,
    time: 18.98, lat: 38, dec: 16, north: 230, sun: 3.2, sky: 0.95, mie: 2.2, ground: [0.16, 0.13, 0.1],
    ambient: [0.55, 0.64, 0.85], hemi: 0.42, fog: 0.0075, grade: 'dusk', exposure: 1.25,
  },
  /* a neutral daylight turntable (tools/turntable.mjs: the cars under plain IBL) */
  {
    index: 2, title: 'Studio', blurb: 'Daylight, no haze: for looking at the cars.', theme: 'DUSK', seed: 3,
    time: 13.5, lat: 38, dec: 16, north: 230, sun: 4.5, sky: 1.0, mie: 0.8, ground: [0.3, 0.3, 0.3],
    ambient: [0.85, 0.88, 0.95], hemi: 0.5, fog: 0, grade: 'neutral', exposure: 1.0, nograin: true,
  },
];

for (const m of ARENAS) {
  const out = ['[scene format=1]', ''];
  const node = (name, parent, type, props) => {
    out.push(`[node name="${name}"${parent ? ` parent="${parent}"` : ''} type="${type}"]`);
    for (const [k, v] of Object.entries(props)) out.push(`${k} = ${v}`);
    out.push('');
  };
  node('Arena', null, 'Arena', { index: m.index, title: str(m.title), blurb: str(m.blurb), theme: m.theme, seed: m.seed });
  node('Sky', '.', 'ProceduralSky', {
    sun: str('../Sun'), time_of_day: f(m.time), latitude: f(m.lat), declination: f(m.dec), north: f(m.north),
    sun_energy: f(m.sun), sky_energy: f(m.sky), mie: f(m.mie), sun_disk_intensity: f(1.4), ground_color: col(...m.ground), env_update_angle: 2,
  });
  node('Sun', '.', 'DirectionalLight3D', {
    shadow: true, shadow_size: 2048, shadow_extent: 26, shadow_far: 140, shadow_bias: f(-0.0003), shadow_normal_bias: f(0.03),
  });
  node('Lighting', '.', 'LightingSettings', { mode: str('CLUSTERED') });
  node('Env', '.', 'WorldEnvironment', {
    background: col(0.05, 0.07, 0.12), ambient_color: col(...m.ambient), ambient_energy: f(0.05), sky_color: col(...m.ambient),
    ground_color: col(...m.ground), hemi_energy: f(m.hemi),
  });
  node('Fog', '.', 'HeightFog', {
    density: f(m.fog), height_falloff: f(0.22), base_height: 0, use_sky_color: true, sun_scatter: f(0.55), sun_g: f(0.82),
    max_opacity: f(0.7), start_distance: 12, sky_distance: 300,
    /* the volumetric grid over the bowl: dark ambient, so the floods' beams carry it */
    ...(m.fog > 0 ? { volumetric_distance: 70, volumetric_ambient: f(0.2), volumetric_albedo: col(0.92, 0.84, 0.74) } : {}),
  });
  node('Post', '.', 'PostProcess', {
    tonemap: 'AGX', exposure: f(m.exposure), bloom: true, bloom_intensity: f(0.05), bloom_threshold: f(1.0), bloom_knee: f(0.6),
    ssao: true, ssao_radius: f(0.7), ssao_intensity: f(1.1), aa: 'SMAA', vignette: true, vignette_intensity: f(0.38),
    vignette_smoothness: f(0.55), grade: true, lut: str(`grades/${m.grade}.cube`), grade_intensity: f(0.9),
    grain: !m.nograin, grain_intensity: f(0.035),
  });
  node('Camera', '.', 'Camera3D', { position: 'vec3(0, 12, -40)', fov: 64, near: f(0.2), far: 600 });
  for (let i = 0; i < 3; i++)
    node(`Flash${i}`, '.', 'OmniLight3D', { position: 'vec3(0, -50, 0)', color: col(1, 0.62, 0.3), energy: 0, range: 16, decay: 2 });
  writeFileSync(join(D, `arena${m.index}.cwscene`), out.join('\n'));
}
console.log('wrote', ARENAS.length, 'arenas to', D);
