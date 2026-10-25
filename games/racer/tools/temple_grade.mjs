// Writes assets/grades/temple.cube: the Overgrowth Temple's colour grade (a 17^3 3D LUT, display values in and out,
// applied after tone mapping): deep teal greens in the shadows, golden light in the highlights, a little more colour
// and a gentle S curve.
//
//   node games/racer/tools/temple_grade.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const DIR = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'grades');
mkdirSync(DIR, { recursive: true });
const N = 17;
const clamp = (x) => Math.max(0, Math.min(1, x));
const g = { sat: 1.24, sh: [-0.035, 0.02, 0.025], hi: [0.075, 0.04, -0.06], s: 0.5, lift: 0.012 };
const lines = ['TITLE "racer temple"', `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
for (let b = 0; b < N; b++)
  for (let gg = 0; gg < N; gg++)
    for (let r = 0; r < N; r++) {
      let c = [r / (N - 1), gg / (N - 1), b / (N - 1)];
      const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
      c = c.map((v) => l + (v - l) * g.sat);
      const sh = Math.pow(1 - l, 3), hi = Math.pow(l, 2);
      c = c.map((v, i) => v + g.sh[i] * sh + g.hi[i] * hi);
      c = c.map((v) => {
        v = clamp(v);
        const s = v * v * (3 - 2 * v);
        return g.lift + (v * (1 - g.s) + s * g.s) * (1 - g.lift);
      });
      lines.push(c.map((v) => clamp(v).toFixed(6)).join(' '));
    }
writeFileSync(join(DIR, 'temple.cube'), lines.join('\n') + '\n');
console.log('wrote', join(DIR, 'temple.cube'));
