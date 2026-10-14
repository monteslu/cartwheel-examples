// Writes assets/grade_<theme>.cube: the colour grade each spout's PostProcess
// applies after tone mapping (17^3 3D LUTs, display values in and out). All
// share the photographed-paper base (colours muted toward construction paper,
// blacks lifted like matte card, a gentle S) and differ in their cast:
//   spring  warm highlights, cool soft shadows
//   city    a night: blue-violet shadows, sodium-warm highlights
//   autumn  amber everywhere, the most saturated
//   winter  cool and bright, the least saturated
//   jungle  green-teal shadows, warm-gold highlights
//
//   node games/spout/tools/make-grade.mjs
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const DIR = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets');
const N = 17;
const clamp = (x) => Math.max(0, Math.min(1, x));
const THEMES = {
  spring: { sat: 0.85, sh: [-0.01, 0.005, 0.03], hi: [0.045, 0.02, -0.035], lift: 0.05 },
  city: { sat: 0.9, sh: [0.01, -0.005, 0.06], hi: [0.06, 0.025, -0.04], lift: 0.045 },
  autumn: { sat: 0.95, sh: [0.01, 0, -0.01], hi: [0.06, 0.025, -0.05], lift: 0.05 },
  winter: { sat: 0.75, sh: [-0.01, 0.01, 0.045], hi: [0.0, 0.01, 0.02], lift: 0.06 },
  jungle: { sat: 0.9, sh: [-0.02, 0.025, 0.02], hi: [0.04, 0.03, -0.03], lift: 0.045 },
};
for (const [name, t] of Object.entries(THEMES)) {
  const lines = [`TITLE "spout ${name}"`, `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
  for (let b = 0; b < N; b++)
    for (let g = 0; g < N; g++)
      for (let r = 0; r < N; r++) {
        let c = [r / (N - 1), g / (N - 1), b / (N - 1)];
        const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
        c = c.map((v) => l + (v - l) * t.sat);
        const sh = Math.pow(1 - l, 3), hi = Math.pow(l, 2);
        c = c.map((v, k) => v + t.sh[k] * sh + t.hi[k] * hi);
        c = c.map((v) => { v = clamp(v); const s = v * v * (3 - 2 * v); return t.lift + (v * 0.6 + s * 0.4) * (1 - t.lift - 0.02); });
        lines.push(c.map((v) => clamp(v).toFixed(6)).join(' '));
      }
  writeFileSync(join(DIR, `grade_${name}.cube`), lines.join('\n') + '\n');
}
console.log('wrote', Object.keys(THEMES).map((n) => `grade_${n}.cube`).join(' '));
