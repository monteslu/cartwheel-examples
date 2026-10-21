// Exercise the game's real save loader with legacy saves and a new seventeenth slot.
// node tests/bikemesa-save-migration.mjs /absolute/path/to/bikemesa
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
const exe = resolve(process.argv[2] || 'build/whimsy-native/project/games/bikemesa/bikemesa');
const dir = mkdtempSync(join(tmpdir(), 'bikemesa-save-'));
const sum = b => {
  let h = 2166136261;
  for (const x of b.subarray(12)) h = Math.imul(h ^ x, 16777619) >>> 0;
  return (h ^ (b.readUInt16LE(4) | b.length << 16)) >>> 0;
};
for (const version of [3, 4, 5, 6, 7, 8]) {
  const b = Buffer.alloc(version === 3 ? 204 : 208);
  b.writeUInt32LE(0x534d4b42); b.writeUInt16LE(version, 4); b.writeUInt16LE(b.length, 6);
  b.writeInt32LE(1234, 12); b.writeInt32LE(9000, 16); b[20] = 3; b[21] = 1; b[23] = 3;
  // Legacy last slot held graphics settings; v4 must preserve the last track's score instead.
  b.writeInt32LE(version === 3 ? 2 : 2468, 192);
  if (version >= 4) { b.writeInt32LE(12000, 196); b[201] = 1; b[203] = 3; b.writeUInt32LE(2, 204); }
  // Neon course extension invalidates old times, preserving unlocks/trick scores.
  b.writeInt32LE(1300, 36); b.writeInt32LE(10000, 40);
  b[44] = 3; b[45] = 1; b[46] = 1; b[47] = 3;
  // Track 14 changed in v8: invalidate only its time/medal, keeping tricks
  // and unlocks. v3 predates tracks 14-16 and clears those slots entirely.
  const circus = 12 + 13 * 12, neighbor = 12 + 12 * 12;
  for (const [offset, ticks, score] of [[circus, 1700, 16000], [neighbor, 1850, 14000]]) {
    b.writeInt32LE(ticks, offset); b.writeInt32LE(score, offset + 4);
    b[offset + 8] = 3; b[offset + 9] = 1; b[offset + 10] = 1; b[offset + 11] = 3;
  }
  b.writeUInt32LE(sum(b), 8);
  const save = join(dir, `v${version}.sav`), state = join(dir, `v${version}.json`);
  writeFileSync(save, b);
  const r = spawnSync(exe, [], { encoding: 'utf8', timeout: 120000, env: {
    ...process.env, SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', CW_GPU: 'gl',
    CW_FRAMES: '5', CW_PLAYER_TIMING: '1', CW_SAVE_FILE: save, CW_DEBUG_DUMP: state,
  }});
  assert.equal(r.status, 0, r.stderr || String(r.error));
  const st = JSON.parse(readFileSync(state));
  const v = st['save.bytes'];
  const out = Buffer.from(typeof v === 'string' ? v : v.hex, 'hex');
  assert.equal(out.length, 220);
  assert.equal(out.readUInt16LE(4), 9);
  assert.equal(out.readInt32LE(12), 1234);
  assert.equal(out.readInt32LE(16), 9000);
  assert.equal(out.readInt32LE(192), version === 3 ? 0 : 2468);
  assert.equal(out.readInt32LE(196), version === 3 ? 0 : 12000);
  assert.equal(out.readInt32LE(36), version < 7 ? 0 : 1300);
  assert.equal(out[44], version < 7 ? 0 : 3);
  assert.equal(out.readInt32LE(40), 10000);
  assert.deepEqual([...out.subarray(45, 48)], [1, 1, 3]);
  assert.equal(out.readInt32LE(circus), version < 8 ? 0 : 1700, `v${version}: circus time`);
  assert.equal(out[circus + 8], version < 8 ? 0 : 3, `v${version}: circus medal`);
  assert.equal(out.readInt32LE(circus + 4), version === 3 ? 0 : 16000, `v${version}: circus trick score`);
  assert.deepEqual([...out.subarray(circus + 9, circus + 12)], version === 3 ? [0, 0, 0] : [1, 1, 3],
    `v${version}: circus completion, visibility and stars`);
  assert.deepEqual(out.subarray(neighbor, neighbor + 12), b.subarray(neighbor, neighbor + 12),
    `v${version}: neighboring track must remain byte-identical`);
  assert.deepEqual(out.subarray(204, 216), Buffer.alloc(12), `v${version}: new track starts empty`);
  assert.equal(out.readUInt32LE(216), 2);
  assert.equal(out.readUInt32LE(8), sum(out));
  console.log(`PASS version ${version}: circus time/medal migration, preserved tricks/unlocks, neighboring track, and graphics settings`);
}
console.log(`Save fixtures and dumps: ${dir}`);
