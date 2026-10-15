// Generates chromite's particle sprites (made here: no third-party images):
//   fx/fireball.png  an 8 x 8 flipbook of a fireball's life, 64 px frames: a
//                    white-hot core boiling out through orange and red to
//                    dim embers (for ADD blending: colour is light)
//   fx/smoke.png     a 4 x 4 flipbook of a rolling smoke puff, 64 px frames
//                    (ALPHA blending: grey with alpha, tinted by the ramp)
//   fx/spark.png     a hot streak: a bright line with a soft falloff
//   fx/flare.png     a star flare: a core, four long spikes, a soft halo
// Noise is value noise on 32-bit integer hashing (Math.imul), so the bytes
// are the same on every machine.
//   node games/chromite/tools/make-sprites.mjs
import { mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { writePng } from '../../../deps/cartwheel-engine/tools/lib/png.mjs';

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'fx');
mkdirSync(OUT, { recursive: true });

function hash3(x, y, z, seed) {
  let h = Math.imul(x, 374761393) ^ Math.imul(y, 668265263) ^ Math.imul(z, 2147483647) ^ Math.imul(seed, 1274126177);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}
const sm = (t) => t * t * (3 - 2 * t);
function vnoise3(x, y, z, seed) {
  const x0 = Math.floor(x), y0 = Math.floor(y), z0 = Math.floor(z);
  const fx = sm(x - x0), fy = sm(y - y0), fz = sm(z - z0);
  let v = 0;
  for (let k = 0; k < 8; k++) {
    const dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
    const w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
    v += w * hash3(x0 + dx, y0 + dy, z0 + dz, seed);
  }
  return v;
}
function fbm3(x, y, z, oct, seed) {
  let s = 0, a = 0.5, n = 0;
  for (let o = 0; o < oct; o++) { s += a * vnoise3(x, y, z, seed + o * 31); n += a; a *= 0.5; x *= 2.03; y *= 2.03; z *= 2.03; }
  return s / n;
}
const clamp = (x) => Math.max(0, Math.min(1, x));
const lerp = (a, b, t) => a + (b - a) * t;

function sheet(cols, rows, fs, frame) {
  const W = cols * fs, H = rows * fs, px = new Uint8Array(W * H * 4);
  for (let f = 0; f < cols * rows; f++) {
    const ox = (f % cols) * fs, oy = Math.floor(f / cols) * fs, t = f / (cols * rows - 1);
    for (let y = 0; y < fs; y++)
      for (let x = 0; x < fs; x++) {
        const [r, g, b, a] = frame((x + 0.5) / fs * 2 - 1, (y + 0.5) / fs * 2 - 1, t);
        const i = ((oy + y) * W + ox + x) * 4;
        px[i] = Math.round(clamp(r) * 255), px[i + 1] = Math.round(clamp(g) * 255), px[i + 2] = Math.round(clamp(b) * 255), px[i + 3] = Math.round(clamp(a) * 255);
      }
  }
  return { W, H, px };
}

// the fireball: a boiling ball of noise, its edge eaten away as it cools
{
  const { W, H, px } = sheet(8, 8, 64, (x, y, t) => {
    const d = Math.hypot(x, y);
    const R = lerp(0.62, 0.98, Math.pow(t, 0.5));
    const warp = fbm3(x * 2.2, y * 2.2, t * 3, 3, 7) - 0.5;
    const n = fbm3(x * 3 + warp * 1.5, y * 3 + warp * 1.5, t * 4, 4, 11);
    let dens = clamp((1 - d / R) * 1.9 + (n - 0.5) * 1.1);
    dens *= clamp(1.5 - t * 1.25 + (n - 0.5) * 0.7);
    const heat = clamp(dens * (1.25 - t * 1.1) + (n - 0.5) * 0.3);
    // white -> yellow -> orange -> red -> ember
    const r = clamp(heat * 2.2), g = clamp(heat * heat * 1.9), b = clamp(Math.pow(heat, 4) * 1.6);
    const edge = clamp(d / R);
    return [r, g, b, dens * (1 - edge * edge * 0.6)];
  });
  writePng(join(OUT, 'fireball.png'), W, H, px);
}
// smoke: a lumpy puff that rolls and thins
{
  const { W, H, px } = sheet(4, 4, 64, (x, y, t) => {
    const d = Math.hypot(x, y);
    const n = fbm3(x * 2.5 + t * 0.8, y * 2.5 - t * 0.5, t * 2, 4, 23);
    const R = lerp(0.55, 0.92, t);
    const dens = clamp((1 - d / R) * 1.6 + (n - 0.55) * 1.4) * lerp(1, 0.55, t);
    const shade = lerp(0.55, 0.9, n);
    return [shade, shade, shade, dens];
  });
  writePng(join(OUT, 'smoke.png'), W, H, px);
}
// a spark streak: long along y
{
  const S = 64, px = new Uint8Array(S * S * 4);
  for (let y = 0; y < S; y++)
    for (let x = 0; x < S; x++) {
      const u = (x + 0.5) / S * 2 - 1, v = (y + 0.5) / S * 2 - 1;
      const a = Math.exp(-u * u * 40) * clamp(1 - Math.abs(v)) ** 1.5;
      const i = (y * S + x) * 4;
      px[i] = px[i + 1] = px[i + 2] = 255, px[i + 3] = Math.round(clamp(a) * 255);
    }
  writePng(join(OUT, 'spark.png'), S, S, px);
}
// a star flare: core, four spikes, halo
{
  const S = 128, px = new Uint8Array(S * S * 4);
  for (let y = 0; y < S; y++)
    for (let x = 0; x < S; x++) {
      const u = (x + 0.5) / S * 2 - 1, v = (y + 0.5) / S * 2 - 1, d = Math.hypot(u, v);
      const core = Math.exp(-d * d * 60), halo = Math.exp(-d * d * 6) * 0.35;
      const spikes = (Math.exp(-u * u * 900) * clamp(1 - Math.abs(v)) + Math.exp(-v * v * 900) * clamp(1 - Math.abs(u))) * 0.7;
      const a = clamp(core + halo + spikes) * clamp(1 - d);
      const i = (y * S + x) * 4;
      px[i] = px[i + 1] = px[i + 2] = 255, px[i + 3] = Math.round(a * 255);
    }
  writePng(join(OUT, 'flare.png'), S, S, px);
}
console.log('sprites written to', OUT);
