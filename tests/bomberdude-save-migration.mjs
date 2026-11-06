// bomberdude's save data: a version 3 save (72 bytes, before the text settings) must load under version 4 with
// everything it held kept and the text settings at their defaults; a version 4 save brings back the player's text
// size and background; a damaged one starts fresh.
//   node tests/bomberdude-save-migration.mjs [path/to/bomberdude]
// It runs the game's real save loader (bomberdude is started for a few frames with CW_SAVE_FILE and its debug state read).
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = resolve(process.argv[2] || join(ROOT, 'build/native/games/bomberdude/bomberdude'));
const dir = join(ROOT, 'out', 'bomberdude-save-test');
mkdirSync(dir, { recursive: true });
// the game's checksum: FNV-1a over everything after the checksum (offset 12), xor the version and size
const sum = (b, ver, size) => {
  let h = 2166136261;
  for (const x of b.subarray(12, size)) h = Math.imul(h ^ x, 16777619) >>> 0;
  return (h ^ (ver | (size << 16))) >>> 0;
};
const save = (ver, size, fill) => {
  const b = Buffer.alloc(size);
  b.writeUInt32LE(0x56534442, 0); b.writeUInt16LE(ver, 4); b.writeUInt16LE(size, 6);
  b[12] = 1; b[13] = 1; b[14] = 1;          // stages 1-3 beaten
  b[20] = 1; b[21] = 1; b[22] = 1; b[23] = 1; // their unlocks shown
  b.writeInt32LE(77, 28);                     // best time on stage 1
  b[68] = 5;                                  // first to 5
  if (fill) fill(b);
  b.writeUInt32LE(sum(b, ver, size), 8);
  return b;
};
const run = (name, bytes) => {
  const file = join(dir, name + '.sav'), state = join(dir, name + '.json');
  writeFileSync(file, bytes);
  const r = spawnSync(exe, [], { encoding: 'utf8', timeout: 120000, env: {
    ...process.env, SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl',
    CW_FRAMES: '5', CW_PLAYER_TIMING: '1', CW_SAVE_FILE: file, CW_DEBUG_DUMP: state,
  }});
  assert.equal(r.status, 0, r.stderr || String(r.error));
  return JSON.parse(readFileSync(state, 'utf8'));
};
const num = (v) => (Array.isArray(v) ? v[0] : v);
const bytes = (st) => Buffer.from(st['save.bytes'], 'hex'); // the debug dump writes bytes as hex

let st = run('v3', save(3, 72));
let b = bytes(st);
assert.equal(b.readUInt16LE(4), 4, 'migrated to version 4');
assert.equal(b.readUInt16LE(6), 76, 'the new size');
assert.equal(num(st['save.migrated']), 1, 'the migration ran');
assert.equal(num(st['save.cleared']), 7, 'stages kept');
assert.equal(b.readInt32LE(28), 77, 'a best time kept');
assert.equal(b[68], 5, 'first to 5 kept');
assert.equal(b[72], 0, 'no text settings yet');
assert.equal(num(st['text.scale']), 1, 'text at 100%');
assert.equal(num(st['text.backdrop']), 0, 'no text background');
console.log('PASS version 3 save carried over: stages, best time, first-to kept; text settings at their defaults');

// the text byte (cw_text_scale_saved): 200% (step 4) with the panel background (2 << 3)
st = run('v4-text', save(4, 76, (x) => { x[72] = 4 | (2 << 3); }));
assert.equal(num(st['save.migrated']) || 0, 0);
assert.equal(num(st['save.cleared']), 7);
assert.equal(num(st['text.scale']), 2, 'the saved text size comes back');
assert.equal(num(st['text.backdrop']), 2, 'the saved text background comes back');
console.log('PASS version 4 save: the text size (200%) and background (panel) come back');

const bad = save(3, 72); bad[13] ^= 3;
st = run('v3-bad', bad);
assert.equal(bytes(st).readUInt16LE(4), 4);
assert.equal(num(st['save.migrated']) || 0, 0, 'a damaged save is not carried over');
assert.equal(num(st['save.cleared']), 0, 'fresh');
console.log('PASS damaged version 3 save starts fresh');

st = run('empty', Buffer.alloc(0));
assert.equal(num(st['save.cleared']), 0);
assert.equal(num(st['text.scale']), 1);
console.log('PASS empty save starts fresh');
