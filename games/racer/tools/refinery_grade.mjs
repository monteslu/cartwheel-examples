// Writes assets/grades/refinery.cube: the refinery's dusk grade. Shadows lean teal, highlights lean orange (so the
// fire and the lamps keep their colour against the cool dusk), a firm S curve for contrast and a lift in saturation.
// Deterministic (no random numbers).
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, '..', 'assets', 'grades', 'refinery.cube');
const N = 17;
const lines = ['TITLE "refinery dusk"', `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
const sm = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const curve = (v) => v + 0.2 * Math.sin((v - 0.5) * Math.PI) * 0.5 * (1 - Math.abs(v - 0.5) * 0.4);
for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) {
  let R = r / (N - 1), G = g / (N - 1), B = b / (N - 1);
  R = curve(R); G = curve(G); B = curve(B);
  const l = 0.2126 * R + 0.7152 * G + 0.0722 * B;
  const sat = 1.25;
  R = l + (R - l) * sat; G = l + (G - l) * sat; B = l + (B - l) * sat;
  const sh = 1 - sm(0.0, 0.45, l), mid = sm(0.1, 0.5, l) * (1 - sm(0.5, 0.9, l)), hi = sm(0.45, 1.0, l);
  R += -0.025 * sh + 0.005 * mid + 0.045 * hi;
  G += 0.02 * sh - 0.004 * mid + 0.004 * hi;
  B += 0.05 * sh + 0.012 * mid - 0.06 * hi;
  R = 0.006 + R * 0.994; G = 0.01 + G * 0.99; B = 0.022 + B * 0.978;
  const c = (v) => Math.min(1, Math.max(0, v)).toFixed(5);
  lines.push(`${c(R)} ${c(G)} ${c(B)}`);
}
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, lines.join('\n') + '\n');
console.log('wrote', out);
