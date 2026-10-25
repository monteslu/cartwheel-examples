// Writes assets/grades/fair.cube: the fairgrounds' sunset grade. Shadows lean violet, mid tones rose, highlights gold,
// a gentle S curve and a saturation lift so the candy colours survive the haze. Deterministic (no random numbers).
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, '..', 'assets', 'grades', 'fair.cube');
const N = 17;
const lines = ['TITLE "fair sunset"', `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
const sm = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const curve = (v) => v + 0.12 * Math.sin((v - 0.5) * Math.PI) * (1 - Math.abs(v - 0.5) * 0.5) * 0.5;
for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) {
  let R = r / (N - 1), G = g / (N - 1), B = b / (N - 1);
  R = curve(R); G = curve(G); B = curve(B);
  const l = 0.2126 * R + 0.7152 * G + 0.0722 * B;
  const sat = 1.22;
  R = l + (R - l) * sat; G = l + (G - l) * sat; B = l + (B - l) * sat;
  const sh = 1 - sm(0.0, 0.5, l), mid = sm(0.1, 0.5, l) * (1 - sm(0.5, 0.9, l)), hi = sm(0.5, 1.0, l);
  R += 0.03 * sh + 0.02 * mid + 0.035 * hi;
  G += -0.01 * sh - 0.012 * mid + 0.012 * hi;
  B += 0.065 * sh + 0.012 * mid - 0.05 * hi;
  R = 0.012 + R * 0.988; G = 0.008 + G * 0.992; B = 0.02 + B * 0.98;
  const c = (v) => Math.min(1, Math.max(0, v)).toFixed(5);
  lines.push(`${c(R)} ${c(G)} ${c(B)}`);
}
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, lines.join('\n') + '\n');
console.log('wrote', out);
