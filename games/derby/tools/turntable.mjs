// Turntable sheets of derby's cars: each kind from the front three-quarter,
// the rear three-quarter, the side and above, under the arena's floodlights
// and in the neutral daylight studio (arena 2). Writes
// out/derby/cars/<kind>-<scene>.png (2x2) with the native dev build.
//
//   node games/derby/tools/turntable.mjs [--exe build/native/project/games/derby/derby]
import { spawnSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readImage, writePng } from '../../../deps/cartwheel-engine/tools/lib/png.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const argv = process.argv.slice(2);
const exe = argv.includes('--exe') ? argv[argv.indexOf('--exe') + 1] : 'build/native/project/games/derby/derby';
const OUT = join(ROOT, 'out', 'derby', 'cars');
mkdirSync(OUT, { recursive: true });
const KINDS = ['sedan', 'wagon', 'pickup', 'van'];
const VIEWS = [{ a: 0.75, h: 1.6, d: 7 }, { a: 2.4, h: 1.6, d: 7 }, { a: 1.5708, h: 1.0, d: 7.5 }, { a: 0.75, h: 7.5, d: 3 }];
const SCENES = [{ name: 'floods', arena: 1 }, { name: 'studio', arena: 2 }];
for (let k = 0; k < KINDS.length; k++)
  for (const sc of SCENES) {
    const shots = [];
    for (let v = 0; v < VIEWS.length; v++) {
      const f = join(OUT, `_${k}_${sc.name}_${v}.png`);
      const r = spawnSync('node', [join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs'), '--host', 'native', '--app', 'derby', '--exe', exe, '--frames', '90', '--shot', f], {
        encoding: 'utf8',
        env: { ...process.env, CW_PROJECT_ROOT: ROOT, DERBY_BOUT: String(sc.arena), DERBY_STUDIO: String(k), DERBY_COLOUR: String([0, 2, 3, 1][k]), DERBY_NO_HUD: '1', DERBY_CAM_CAR: '0', DERBY_CAM_A: String(VIEWS[v].a),
               DERBY_CAM_H: String(VIEWS[v].h), DERBY_CAM_D: String(VIEWS[v].d) },
      });
      if (r.status !== 0) { console.error(r.stderr); process.exit(1); }
      shots.push(readImage(f));
    }
    const w = shots[0].w, h = shots[0].h, px = new Uint8Array(w * 2 * h * 2 * 4);
    shots.forEach((im, i) => {
      const ox = (i % 2) * w, oy = Math.floor(i / 2) * h;
      for (let y = 0; y < h; y++) px.set(im.rgba.subarray(y * w * 4, (y + 1) * w * 4), ((oy + y) * w * 2 + ox) * 4);
    });
    writePng(join(OUT, `${KINDS[k]}-${sc.name}.png`), w * 2, h * 2, px);
    console.log('wrote', `${KINDS[k]}-${sc.name}.png`);
  }
