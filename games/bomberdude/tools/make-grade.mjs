// Writes bomberdude's colour grades (assets/grades/<mood>.cube): one 17^3
// 3D LUT per arena mood, applied after tone mapping (display values in and
// out). Each is a saturation lift, a split tone (shadows / highlights) and a
// soft S curve.
//
//   node games/bomberdude/tools/make-grade.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const D = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'grades');
mkdirSync(D, { recursive: true });
const N = 17;
const clamp = (x) => Math.max(0, Math.min(1, x));
// sat: saturation gain; sh / hi: rgb pushed into shadows / highlights; s: S-curve share; lift: black floor
const MOODS = {
  meadow: { sat: 1.14, sh: [-0.02, 0.01, 0.03], hi: [0.04, 0.02, -0.03], s: 0.45, lift: 0.012 },
  frost: { sat: 1.05, sh: [-0.02, 0.0, 0.05], hi: [-0.01, 0.01, 0.03], s: 0.4, lift: 0.02 },
  forge: { sat: 1.15, sh: [0.03, -0.01, -0.02], hi: [0.05, 0.01, -0.05], s: 0.55, lift: 0.008 },
  temple: { sat: 1.1, sh: [-0.03, 0.02, 0.0], hi: [0.04, 0.03, -0.03], s: 0.45, lift: 0.015 },
  candy: { sat: 1.18, sh: [0.03, -0.01, 0.04], hi: [0.03, 0.0, 0.02], s: 0.35, lift: 0.02 },
  neon: { sat: 1.22, sh: [0.0, -0.01, 0.05], hi: [0.04, -0.01, 0.03], s: 0.5, lift: 0.006 },
  pirate: { sat: 1.12, sh: [-0.03, 0.01, 0.04], hi: [0.05, 0.02, -0.04], s: 0.5, lift: 0.012 },
  desert: { sat: 1.08, sh: [0.0, -0.005, 0.02], hi: [0.05, 0.025, -0.04], s: 0.45, lift: 0.015 },
};
for (const [name, m] of Object.entries(MOODS)) {
  const lines = [`TITLE "bomberdude ${name}"`, `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
  for (let b = 0; b < N; b++)
    for (let g = 0; g < N; g++)
      for (let r = 0; r < N; r++) {
        let c = [r / (N - 1), g / (N - 1), b / (N - 1)];
        const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
        c = c.map((v) => l + (v - l) * m.sat);
        const sh = Math.pow(1 - l, 3), hi = Math.pow(l, 2);
        c = c.map((v, i) => v + m.sh[i] * sh + m.hi[i] * hi);
        c = c.map((v) => { v = clamp(v); const s = v * v * (3 - 2 * v); return m.lift + (v * (1 - m.s) + s * m.s) * (1 - m.lift); });
        lines.push(c.map((v) => clamp(v).toFixed(6)).join(' '));
      }
  writeFileSync(join(D, `${name}.cube`), lines.join('\n') + '\n');
}
console.log('wrote', Object.keys(MOODS).length, 'grades to', D);
