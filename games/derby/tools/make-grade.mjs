// Writes derby's colour grade (assets/grades/<mood>.cube): a 17^3 3D LUT
// applied after tone mapping. Dusk: desaturated a touch, warm highlights,
// teal-ish shadows, crushed a little at the bottom, a soft S curve: worn
// film, not a postcard.
//
//   node games/derby/tools/make-grade.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const D = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'grades');
mkdirSync(D, { recursive: true });
const N = 17;
const clamp = (x) => Math.max(0, Math.min(1, x));
const MOODS = {
  /* sodium against blue hour: teal-cold shadows, warm highlights, a lot less colour, filmic contrast */
  dusk: { sat: 0.74, sh: [-0.03, 0.008, 0.04], hi: [0.05, 0.02, -0.035], s: 0.62, lift: 0.02, gain: 1.03 },
  neutral: { sat: 1, sh: [0, 0, 0], hi: [0, 0, 0], s: 0, lift: 0, gain: 1 },
  night: { sat: 0.8, sh: [-0.02, 0.0, 0.045], hi: [0.04, 0.02, -0.02], s: 0.5, lift: 0.012, gain: 1.0 },
};
for (const [name, m] of Object.entries(MOODS)) {
  const lines = [`TITLE "derby ${name}"`, `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
  for (let b = 0; b < N; b++)
    for (let g = 0; g < N; g++)
      for (let r = 0; r < N; r++) {
        let c = [r / (N - 1), g / (N - 1), b / (N - 1)];
        const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
        c = c.map((v) => l + (v - l) * m.sat);
        const sh = Math.pow(1 - l, 3), hi = Math.pow(l, 2);
        c = c.map((v, i) => (v + m.sh[i] * sh + m.hi[i] * hi) * m.gain);
        c = c.map((v) => { v = clamp(v); const s = v * v * (3 - 2 * v); return m.lift + (v * (1 - m.s) + s * m.s) * (1 - m.lift); });
        lines.push(c.map((v) => clamp(v).toFixed(6)).join(' '));
      }
  writeFileSync(join(D, `${name}.cube`), lines.join('\n') + '\n');
}
console.log('wrote', Object.keys(MOODS).length, 'grades to', D);
