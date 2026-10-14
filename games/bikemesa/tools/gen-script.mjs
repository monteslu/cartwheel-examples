// The gate's input script (tests/bikemesa-ride.json) as the C table the
// _auto build compiles in (games/bikemesa/script.inc). Rerun after
// editing the JSON:  node games/bikemesa/tools/gen-script.mjs
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..', '..', '..');
const BITS = { south: 0, east: 1, west: 2, north: 3, back: 4, guide: 5, start: 6, l3: 7, r3: 8, l: 9, r: 10, up: 11, down: 12, left: 13, right: 14 };
const script = JSON.parse(readFileSync(join(ROOT, 'tests', 'bikemesa-ride.json'), 'utf8'));
const lines = script.map((s) => {
  const mask = (s.buttons || []).reduce((m, b) => m | (1 << BITS[b]), 0);
  const ax = s.axes || {};
  const fl = (v) => { const t = String(v || 0); return (/[.e]/.test(t) ? t : t + '.0') + 'f'; };
  if (s.keys || s.mouse || s.touches) throw new Error('the autopilot carries pad input only');
  return `  { ${s.frame}, ${mask}u, ${fl(ax.lx)}, ${fl(ax.ly)} },`;
});
writeFileSync(join(HERE, '..', 'script.inc'), `/* generated from tests/bikemesa-ride.json by tools/gen-script.mjs */\n${lines.join('\n')}\n`);
console.log(`script.inc: ${lines.length} entries`);
