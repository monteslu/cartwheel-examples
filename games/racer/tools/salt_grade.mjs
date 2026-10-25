// Writes assets/grades/salt.cube: a dawn grade for the salt flats. Highlights lean warm pink, shadows lean cool
// blue-violet, a touch of lifted blacks and a gentle saturation boost. Deterministic (no random numbers).
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, '..', 'assets', 'grades', 'salt.cube');
const N = 17;
const lines = ['TITLE "salt dawn"', `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
const sm = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) {
  let R = r / (N - 1), G = g / (N - 1), B = b / (N - 1);
  const l = 0.2126 * R + 0.7152 * G + 0.0722 * B;
  // saturation up a little
  const sat = 1.12;
  R = l + (R - l) * sat; G = l + (G - l) * sat; B = l + (B - l) * sat;
  // shadows cool, highlights warm
  const sh = 1 - sm(0.0, 0.55, l), hi = sm(0.45, 1.0, l);
  R += -0.03 * sh + 0.045 * hi;
  G += -0.005 * sh + 0.008 * hi;
  B += 0.05 * sh - 0.04 * hi;
  // lifted blacks, soft contrast
  R = 0.015 + R * 0.985; G = 0.015 + G * 0.985; B = 0.02 + B * 0.98;
  const c = (v) => Math.min(1, Math.max(0, v)).toFixed(5);
  lines.push(`${c(R)} ${c(G)} ${c(B)}`);
}
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, lines.join('\n') + '\n');
console.log('wrote', out);
