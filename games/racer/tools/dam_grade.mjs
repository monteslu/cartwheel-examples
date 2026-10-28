// Writes assets/grades/dam.cube: the dam's bright midday grade. The tone mapper leaves a clear sky a dull slate and
// the canyon a little brown; this pulls the sky to a clean azure (lighter, bluer), gives the sandstone its red and the
// water its turquoise, and keeps neutrals (the concrete, the road) neutral. Deterministic (no random numbers).
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, '..', 'assets', 'grades', 'dam.cube');
const N = 17;
const lines = ['TITLE "dam midday"', `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
const sm = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
for (let b = 0; b < N; b++) for (let g = 0; g < N; g++) for (let r = 0; r < N; r++) {
  let R = r / (N - 1), G = g / (N - 1), B = b / (N - 1);
  const l = 0.2126 * R + 0.7152 * G + 0.0722 * B;
  const chroma = Math.max(R, G, B) - Math.min(R, G, B);
  // saturation up, more for colours that already have some (the greys of the dam stay grey)
  const sat = 1.0 + 0.28 * sm(0.03, 0.2, chroma);
  R = l + (R - l) * sat; G = l + (G - l) * sat; B = l + (B - l) * sat;
  // sky: blue over red, not too dark. Push it toward azure and lift it
  const sky = sm(0.02, 0.16, B - R) * sm(0.15, 0.4, l);
  R += -0.05 * sky; G += 0.015 * sky; B += 0.11 * sky;
  R += 0.05 * sky; G += 0.05 * sky; B += 0.05 * sky;
  // sandstone: red over blue gets a touch warmer and redder
  const rock = sm(0.05, 0.25, R - B);
  R += 0.03 * rock; G -= 0.01 * rock;
  // a gentle S curve for midday contrast
  const s = (v) => v + 0.06 * Math.sin((v - 0.5) * Math.PI) * (1 - Math.abs(2 * v - 1));
  R = s(R); G = s(G); B = s(B);
  const c = (v) => Math.min(1, Math.max(0, v)).toFixed(5);
  lines.push(`${c(R)} ${c(G)} ${c(B)}`);
}
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, lines.join('\n') + '\n');
console.log('wrote', out);
