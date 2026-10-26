// racer's save data: a version 1 save (76 bytes, before the time trial, the cups and the assists) must load under
// version 2 with everything it held kept and the new fields empty, and a damaged one must start fresh.
//   node tests/racer-save-migration.mjs [path/to/racer]
// It runs the game's real save loader (racer is started for a few frames with CW_SAVE_FILE and its debug state read).
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = resolve(process.argv[2] || join(ROOT, 'build/native/games/racer/racer'));
const dir = join(ROOT, 'out', 'racer-save-test');
mkdirSync(dir, { recursive: true });
// the game's checksum: FNV-1a over everything from the graphics bytes (offset 12), xor the version and size
const sum = (b, ver, size) => {
  let h = 2166136261;
  for (const x of b.subarray(12, size)) h = Math.imul(h ^ x, 16777619) >>> 0;
  return (h ^ (ver | (size << 16))) >>> 0;
};
const v1 = () => {
  const b = Buffer.alloc(76);
  b.writeUInt32LE(0x52435231, 0); b.writeUInt16LE(1, 4); b.writeUInt16LE(76, 6);
  b[16] = 5; b[17] = 7;                  // music, effects
  b[18] = 4; b[19] = 1; b[20] = 0; b[21] = 3; // laps, skill, items, map
  b.writeUInt32LE(6543, 36);             // best lap on map 0
  b.writeUInt32LE(7001, 40);             // and on map 1
  b.writeUInt32LE(12, 68); b.writeUInt32LE(5, 72); // races, wins
  b.writeUInt32LE(sum(b, 1, 76), 8);
  return b;
};
const run = (name, bytes) => {
  const save = join(dir, name + '.sav'), state = join(dir, name + '.json');
  writeFileSync(save, bytes);
  const r = spawnSync(exe, [], { encoding: 'utf8', timeout: 120000, env: {
    ...process.env, SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl',
    CW_FRAMES: '5', CW_PLAYER_TIMING: '1', CW_SAVE_FILE: save, CW_DEBUG_DUMP: state,
  }});
  assert.equal(r.status, 0, r.stderr || String(r.error));
  return JSON.parse(readFileSync(state, 'utf8'));
};
const num = (v) => (Array.isArray(v) ? v[0] : v);

// a good version 1 save is carried over
let st = run('v1', v1());
assert.equal(num(st['save.version']), 2, 'migrated to version 2');
assert.equal(num(st['save.migrated']), 1, 'the migration ran');
assert.equal(num(st['save.races']), 12, 'races kept');
assert.equal(num(st['save.best_lap0']), 6543, 'a best lap kept');
assert.deepEqual([...st['save.trial_cs']].map(Number), [0, 0, 0, 0, 0, 0, 0, 0], 'no trial times yet');
console.log('PASS version 1 save carried over: races, best laps kept, trial times empty');

// a damaged version 1 (the checksum is off) starts fresh, never half loaded
const bad = v1(); bad[17] ^= 3;
st = run('v1-bad', bad);
assert.equal(num(st['save.version']), 2);
assert.equal(num(st['save.migrated']) || 0, 0, 'a damaged save is not carried over');
assert.equal(num(st['save.races']), 0, 'fresh');
console.log('PASS damaged version 1 save starts fresh');

// no save at all
st = run('empty', Buffer.alloc(0));
assert.equal(num(st['save.version']), 2);
assert.equal(num(st['save.races']), 0);
console.log('PASS empty save starts fresh');
