// The CI test run: the games' gates on the native host through Mesa's
// software renderers (CW_TEST_GPU=software: llvmpipe for GL, lavapipe for
// WebGPU), one share of them per CI job.
//
//   CW_TEST_GPU=software node tests/ci.mjs gl-<k>of<n>
//   CW_TEST_GPU=software node tests/ci.mjs webgpu-<k>of<n>
//
// gl runs every gate on GL; webgpu runs every gate with a "webgpu" entry on
// WebGPU (and its GL comparison run). The gates are shared out by their frame
// counts, longest first, each to the share with the fewest frames so far.
// The other hosts (Chromium, wasmcart-native, romdev) are not in CI; run
// them locally with tests/gate.mjs.
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
if (process.env.CW_TEST_GPU !== 'software') {
  console.error('tests/ci.mjs runs on software rendering only: set CW_TEST_GPU=software');
  process.exit(2);
}
const m = /^(gl|webgpu)-(\d+)of(\d+)$/.exec(process.argv[2] || '');
if (!m || +m[2] < 1 || +m[2] > +m[3]) {
  console.error('usage: ci.mjs gl-<k>of<n> | webgpu-<k>of<n>');
  process.exit(2);
}
const [, api, k, n] = [m[0], m[1], +m[2], +m[3]];
const gates = JSON.parse(readFileSync(join(ROOT, 'tests/gates.json'), 'utf8'))
  .filter((g) => g.app && (api === 'gl' ? g.gl !== false && (!g.hosts || g.hosts.includes('native')) : g.webgpu));
const load = new Array(n).fill(0), mine = new Set();
for (const g of [...gates].sort((a, b) => b.frames - a.frames || a.name.localeCompare(b.name))) {
  const j = load.indexOf(Math.min(...load));
  load[j] += g.frames;
  if (j === k - 1) mine.add(g.name);
}
const names = gates.map((g) => g.name).filter((x) => mine.has(x));
console.log(`== ${api} share ${k} of ${n}: ${names.length} gates, ${load[k - 1]} frames`);
const args = [join(ROOT, 'tests/gate.mjs'), ...names, '--hosts', 'native', ...(api === 'webgpu' ? ['--gpu', 'webgpu'] : [])];
const code = await new Promise((res) => spawn('node', args, { stdio: 'inherit' }).on('close', (c) => res(c ?? 1)));
process.exit(code);
