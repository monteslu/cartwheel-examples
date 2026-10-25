// Writes assets/grades/frost.cube: the colour grade of Frostbite Pass. Shadows lean deep teal-blue, highlights
// lean pink-white (the last of the light on the snow), colours a little richer, a soft S-curve and lifted blacks.
// Deterministic (no random numbers). Run: node games/racer/tools/frost_grade.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const outDir = join(here, '..', 'assets', 'grades');
mkdirSync(outDir, { recursive: true });
const N = 17;
const clamp = (x) => Math.max(0, Math.min(1, x));
const sm = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
const lines = ['TITLE "frostbite twilight"', `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) {
  let c = [r / (N - 1), g / (N - 1), b / (N - 1)];
  const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  // richer colour, but keep the snow from going neon
  const sat = 1.14 - 0.12 * sm(0.7, 1.0, l);
  c = c.map((v) => l + (v - l) * sat);
  // cold shadows, pink-white highlights
  const sh = 1 - sm(0.0, 0.5, l), hi = sm(0.5, 1.0, l);
  c[0] += -0.018 * sh + 0.04 * hi;
  c[1] += 0.012 * sh - 0.004 * hi;
  c[2] += 0.05 * sh + 0.012 * hi;
  // a soft S-curve and lifted blacks
  c = c.map((v) => { v = clamp(v); const s = v * v * (3 - 2 * v); return 0.012 + (v * 0.65 + s * 0.35) * 0.988; });
  lines.push(c.map((v) => clamp(v).toFixed(5)).join(' '));
}
writeFileSync(join(outDir, 'frost.cube'), lines.join('\n') + '\n');
console.log('wrote', join(outDir, 'frost.cube'));
