// Writes assets/grades/<theme>.cube: the colour grade each track's PostProcess
// applies after tone mapping (17^3 3D LUTs, display values in and out). Each
// theme gets its own split tone, saturation and contrast.
//
//   node games/bikemesa/tools/make-grade.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const DIR = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'grades');
mkdirSync(DIR, { recursive: true });
const N = 17;
const clamp = (x) => Math.max(0, Math.min(1, x));
// sat: saturation gain; sh/hi: rgb pushed into shadows / highlights; s: S-curve
// strength; lift: black lift
const GRADES = {
  canyon: { sat: 1.18, sh: [-0.01, 0.0, 0.03], hi: [0.06, 0.02, -0.05], s: 0.5, lift: 0.012 },
  alpine: { sat: 1.08, sh: [-0.01, 0.01, 0.05], hi: [0.05, 0.02, -0.02], s: 0.4, lift: 0.015 },
  neon: { sat: 1.3, sh: [0.03, -0.02, 0.07], hi: [0.04, -0.01, 0.03], s: 0.55, lift: 0.01 },
  jungle: { sat: 1.15, sh: [-0.02, 0.025, 0.02], hi: [0.04, 0.03, -0.04], s: 0.45, lift: 0.014 },
  desert: { sat: 1.12, sh: [0.0, -0.005, 0.03], hi: [0.06, 0.03, -0.06], s: 0.5, lift: 0.01 },
  autumn: { sat: 1.2, sh: [0.0, 0.0, 0.04], hi: [0.07, 0.02, -0.05], s: 0.45, lift: 0.015 },
  coast: { sat: 0.92, sh: [-0.015, 0.01, 0.04], hi: [0.02, 0.02, 0.0], s: 0.55, lift: 0.02 },
  scary: { sat: 0.8, sh: [-0.02, 0.03, 0.05], hi: [0.05, 0.03, -0.02], s: 0.6, lift: 0.01 },
  xmas: { sat: 1.15, sh: [-0.01, 0.0, 0.05], hi: [0.07, 0.03, -0.03], s: 0.45, lift: 0.015 },
  moon: { sat: 0.6, sh: [0.0, 0.0, 0.02], hi: [0.02, 0.02, 0.02], s: 0.55, lift: 0.0 },
  beach: { sat: 1.22, sh: [-0.02, 0.01, 0.04], hi: [0.05, 0.03, -0.03], s: 0.45, lift: 0.012 },
  mars: { sat: 1.1, sh: [0.02, -0.01, 0.0], hi: [0.08, 0.03, -0.06], s: 0.5, lift: 0.012 },
  cosmic: { sat: 1.28, sh: [0.03, -0.02, 0.08], hi: [0.05, 0.0, 0.05], s: 0.5, lift: 0.012 },
};
for (const [name, g] of Object.entries(GRADES)) {
  const lines = [`TITLE "bikemesa ${name}"`, `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
  for (let b = 0; b < N; b++)
    for (let gg = 0; gg < N; gg++)
      for (let r = 0; r < N; r++) {
        let c = [r / (N - 1), gg / (N - 1), b / (N - 1)];
        const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
        c = c.map((v) => l + (v - l) * g.sat);
        const sh = Math.pow(1 - l, 3), hi = Math.pow(l, 2);
        c = c.map((v, i) => v + g.sh[i] * sh + g.hi[i] * hi);
        c = c.map((v) => { v = clamp(v); const s = v * v * (3 - 2 * v); return g.lift + (v * (1 - g.s) + s * g.s) * (1 - g.lift); });
        lines.push(c.map((v) => clamp(v).toFixed(6)).join(' '));
      }
  writeFileSync(join(DIR, name + '.cube'), lines.join('\n') + '\n');
}
console.log('wrote', Object.keys(GRADES).length, 'grades to', DIR);
