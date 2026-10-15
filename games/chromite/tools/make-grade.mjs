// Writes assets/grade_<deck>.cube: the colour grade each deck's PostProcess
// applies after tone mapping (33^3 3D LUTs, display values in and out). All
// share a hard industrial base (colour pulled down toward steel, deep but
// not clipped blacks, a firm S curve) and differ in their cast:
//   steel   cold blue-grey shadows, clean white highlights
//   rust    umber shadows, sodium-amber highlights
//   toxic   green-teal shadows, sickly yellow highlights
//
//   node games/chromite/tools/make-grade.mjs
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const DIR = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets');
const N = 33;
const clamp = (x) => Math.max(0, Math.min(1, x));
const GRADES = {
  steel: { sat: 0.72, sh: [-0.012, 0.0, 0.03], hi: [0.01, 0.012, 0.02], toe: 0.012, contrast: 0.55 },
  rust: { sat: 0.78, sh: [0.025, 0.006, -0.02], hi: [0.05, 0.02, -0.04], toe: 0.014, contrast: 0.55 },
  toxic: { sat: 0.74, sh: [-0.02, 0.02, 0.008], hi: [0.035, 0.04, -0.03], toe: 0.012, contrast: 0.55 },
};
for (const [name, t] of Object.entries(GRADES)) {
  const lines = [`TITLE "chromite ${name}"`, `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
  for (let b = 0; b < N; b++)
    for (let g = 0; g < N; g++)
      for (let r = 0; r < N; r++) {
        let c = [r / (N - 1), g / (N - 1), b / (N - 1)];
        const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
        // saturated colours (the glows, the fire) keep more of their colour than the greys
        const chroma = Math.max(...c) - Math.min(...c);
        const sat = t.sat + (1 - t.sat) * Math.min(1, chroma * 1.4);
        c = c.map((v) => l + (v - l) * sat);
        const sh = Math.pow(1 - l, 3), hi = Math.pow(l, 2);
        c = c.map((v, k) => v + t.sh[k] * sh + t.hi[k] * hi);
        c = c.map((v) => {
          v = clamp(v);
          const s = v * v * (3 - 2 * v);
          return t.toe + (v * (1 - t.contrast) + s * t.contrast) * (1 - t.toe);
        });
        lines.push(c.map((v) => clamp(v).toFixed(6)).join(' '));
      }
  writeFileSync(join(DIR, `grade_${name}.cube`), lines.join('\n') + '\n');
}
console.log('wrote', Object.keys(GRADES).map((n) => `grade_${n}.cube`).join(' '));
