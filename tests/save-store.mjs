// The save gate: every game keeps its save in the engine's save store (cartwheel/save.h), one slot named
// "progress", and a save from before the store still loads.
//
//   node tests/save-store.mjs [game ...] [--build build/native]
//
// For each game, natively (the save file is CW_SAVE_FILE; the game's progress is its debug field save.progress,
// the bytes of its save_data after loading):
//   fresh    no save: the game's defaults
//   store    a store holding a "progress" slot of the fixture's bytes (what this build writes)
//   legacy   the same bytes in the old one-struct save at its last version (what the previous release wrote)
//   older    an older version of the old struct, a prefix of those bytes (the store's slot then the prefix)
// legacy must load exactly as store does and differ from fresh, older as its prefix slot does, and the file
// after the legacy run must be a store whose "progress" slot holds the fixture's bytes.
// Control: each game runs the legacy checks again with CW_TEST_SAVE_NO_IMPORT=1 (the old struct not imported), and
// all three must fail there, or the game fails the gate. Exit 0 only when every game passes.
import { spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..');
const args = process.argv.slice(2);
const bi = args.indexOf('--build');
const BUILD = resolve(ROOT, bi >= 0 ? args[bi + 1] : 'build/native');
const picked = args.filter((a, i) => !a.startsWith('--') && (bi < 0 || i !== bi + 1));

const fnv = (buf, h = 2166136261) => {
  for (const x of buf) h = Math.imul(h ^ x, 16777619) >>> 0;
  return h >>> 0;
};

// The old structs. head: magic, version, size and checksum in 12 bytes, then the fields, the checksum FNV-1a over
// the fields xor (version | size << 16). tail: magic, version and size in 8 bytes, the fields, the checksum in the
// last 4 bytes over the fields between. size: the struct's byte count; racer stores it in 4 byte units.
// zero: offsets the older version held as zero (spare bytes it never wrote).
const GAMES = {
  bikemesa: { magic: 0x534d4b42, kind: 'head', version: 12, size: 248, older: { version: 11, size: 244 } },
  bomberdude: { magic: 0x56534442, kind: 'head', version: 4, size: 76, older: { version: 3, size: 72 } },
  chromite: { magic: 0x4d524843, kind: 'tail', version: 3, size: 308, older: { version: 2, size: 304 } },
  derby: { magic: 0x59425244, kind: 'head', version: 3, size: 36, older: { version: 2, size: 32 } },
  pugball: { magic: 0x42475550, kind: 'head', version: 2, size: 76, older: { version: 1, size: 72 } },
  racer: { magic: 0x52435231, kind: 'head', version: 3, size: 192276, units: 4, older: { version: 2, size: 192276, zero: [30, 83] } },
  spout: { magic: 0x54554f53, kind: 'tail', version: 5, size: 22620, older: { version: 4, size: 100, zero: [94, 95] } },
};

function legacy(g, version, size, fill) {
  const b = Buffer.alloc(size);
  const body = g.kind === 'head' ? [12, size] : [8, size - 4];
  b.fill(1, body[0], body[1]);
  for (const z of fill.zero || []) b[z] = 0;
  const sizeField = g.units ? size / g.units : size;
  b.writeUInt32LE(g.magic, 0);
  b.writeUInt16LE(version, 4);
  b.writeUInt16LE(sizeField, 6);
  const sum = (fnv(b.subarray(body[0], body[1])) ^ (version | (sizeField << 16))) >>> 0;
  b.writeUInt32LE(sum, g.kind === 'head' ? 8 : size - 4);
  return { file: b, body: b.subarray(body[0], body[1]) };
}
// the engine's store format (cartwheel/save.h): "CWS1", bytes used, then each slot
function store(slots, size) {
  const b = Buffer.alloc(size);
  b.write('CWS1', 0, 'latin1');
  let o = 8;
  const pad = n => (n + 3) & ~3;
  for (const [name, version, data] of slots) {
    const nm = Buffer.from(name, 'latin1');
    b[o] = nm.length;
    b.writeUInt16LE(version, o + 2);
    b.writeUInt32LE(data.length, o + 4);
    b.writeUInt32LE(fnv(data, fnv(nm)), o + 8);
    nm.copy(b, o + 12);
    data.copy(b, o + 12 + pad(nm.length));
    o += 12 + pad(nm.length) + pad(data.length);
  }
  b.writeUInt32LE(o, 4);
  return b;
}
function readSlot(b, want) {
  if (b.toString('latin1', 0, 4) !== 'CWS1') return null;
  const used = b.readUInt32LE(4);
  for (let o = 8; o + 12 <= used;) {
    const n = b[o], size = b.readUInt32LE(o + 4);
    const name = b.toString('latin1', o + 12, o + 12 + n);
    const data = o + 12 + ((n + 3) & ~3);
    if (name === want) return { version: b.readUInt16LE(o + 2), data: b.subarray(data, data + size) };
    o = data + ((size + 3) & ~3);
  }
  return null;
}

const dir = mkdtempSync(join(ROOT, 'build', 'save-store-'));
let runs = 0;
function run(game, name, bytes, env = {}) {
  const save = join(dir, `${game}-${name}.sav`), dump = join(dir, `${game}-${name}.json`);
  if (bytes) writeFileSync(save, bytes);
  else rmSync(save, { force: true });
  const r = spawnSync(join(BUILD, 'games', game, game), [], {
    cwd: ROOT, encoding: 'utf8', timeout: 180000,
    env: { ...process.env, SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy', CW_GPU: process.env.CW_GPU || 'gl',
      CW_FRAMES: '5', CW_PLAYER_TIMING: '1', CW_SAVE_FILE: save, CW_DEBUG_DUMP: dump, ...env },
  });
  runs++;
  if (r.status !== 0) throw new Error(`${game} ${name}: exit ${r.status}\n${(r.stderr || String(r.error)).slice(-2000)}`);
  const v = JSON.parse(readFileSync(dump, 'utf8'))['save.progress'];
  if (v === undefined) throw new Error(`${game} ${name}: no debug field save.progress`);
  let file = null;
  try { file = readFileSync(save); } catch { /* never written */ }
  return { progress: Buffer.from(typeof v === 'string' ? v : v.hex, 'hex'), file };
}

const failures = [];
for (const game of picked.length ? picked : Object.keys(GAMES)) {
  const g = GAMES[game];
  if (!g) throw new Error(`no game ${game}`);
  const fails = [];
  const fresh = run(game, 'fresh', null);
  const n = fresh.progress.length; // sizeof(save_data)
  const size = g.size || n + 12; // the old struct: its 12 bytes of header and the fields
  if (size !== n + 12) fails.push(`the old struct is ${size} bytes but save_data is ${n}: save_data must only grow (fix this table)`);
  const cur = legacy(g, g.version, size, {});
  const old = legacy(g, g.older.version, g.older.size || size, g.older);
  const room = Math.max(n + 4096, size + 64);
  const st = run(game, 'store', store([['progress', 1, cur.body]], room));
  const ost = run(game, 'older-store', store([['progress', 1, old.body]], room));
  const differ = [...st.progress].filter((x, i) => x !== fresh.progress[i]).length;
  if (differ < 4) fails.push(`the fixture loads as the defaults (${differ} bytes differ): the store slot was not read`);
  // the legacy checks, with the import and (the control) without it
  const legacyFails = (env) => {
    const out = [];
    const lg = run(game, 'legacy', cur.file, env);
    if (!lg.progress.equals(st.progress)) {
      const at = [...lg.progress].findIndex((x, i) => x !== st.progress[i]);
      out.push(`legacy v${g.version} loads differently from the store (first byte ${at}: ${lg.progress[at]} against ${st.progress[at]})`);
    }
    const slot = lg.file && readSlot(lg.file, 'progress');
    if (!slot) out.push('after loading the old struct the save file is not a store with a "progress" slot');
    else if (!slot.data.equals(cur.body)) out.push('the imported "progress" slot does not hold the old struct\'s fields');
    const olg = run(game, 'older', old.file, env);
    if (!olg.progress.equals(ost.progress)) {
      const at = [...olg.progress].findIndex((x, i) => x !== ost.progress[i]);
      out.push(`legacy v${g.older.version} loads differently from its prefix slot (first byte ${at})`);
    }
    return out;
  };
  fails.push(...legacyFails({}));
  const ctl = legacyFails({ CW_TEST_SAVE_NO_IMPORT: '1' });
  if (ctl.length < 3) fails.push(`control (CW_TEST_SAVE_NO_IMPORT=1) failed only ${ctl.length} of 3 legacy checks: the gate cannot tell an import from none`);
  console.log(`${fails.length ? 'FAIL' : 'PASS'} ${game}: save_data ${n} bytes, ${differ} differ from the defaults; legacy v${g.version} (${size} bytes) and v${g.older.version} (${old.file.length}) load as the store does; control fails ${ctl.length} of 3${fails.map(f => `\n  ${f}`).join('')}`);
  if (fails.length) failures.push(game);
}
rmSync(dir, { recursive: true, force: true });
console.log(`${runs} runs${failures.length ? `; FAILED: ${failures.join(' ')}` : ''}`);
process.exit(failures.length ? 1 : 0);
