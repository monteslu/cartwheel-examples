// Writes assets/grades/drowned.cube: a night-in-the-rain grade for the drowned city. Shadows lean deep teal-blue,
// highlights lean hot pink, saturation is pushed up so neon stays neon after the tone curve, blacks are lifted a
// touch so the dark water still shows its colour. Deterministic (no random numbers).
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, '..', 'assets', 'grades', 'drowned.cube');
const N = 17;
const lines = ['TITLE "drowned city night"', `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
const sm = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) {
  let R = r / (N - 1), G = g / (N - 1), B = b / (N - 1);
  const l = 0.2126 * R + 0.7152 * G + 0.0722 * B;
  const sat = 1.28;
  R = l + (R - l) * sat; G = l + (G - l) * sat; B = l + (B - l) * sat;
  const sh = 1 - sm(0.0, 0.5, l), hi = sm(0.5, 1.0, l);
  R += -0.035 * sh + 0.05 * hi;
  G += 0.012 * sh - 0.025 * hi;
  B += 0.06 * sh - 0.012 * hi;
  // an s-curve for punch, then lifted blacks
  const curve = (v) => v + 0.18 * (v - 0.5) * (1 - Math.abs(2 * v - 1)) * 2 * 0.5;
  R = curve(R); G = curve(G); B = curve(B);
  R = 0.012 + R * 0.988; G = 0.016 + G * 0.984; B = 0.028 + B * 0.972;
  const c = (v) => Math.min(1, Math.max(0, v)).toFixed(5);
  lines.push(`${c(R)} ${c(G)} ${c(B)}`);
}
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, lines.join('\n') + '\n');
console.log('wrote', out);
