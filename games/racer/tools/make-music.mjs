// racer's music: ten original pieces composed in code (no samples, no
// third-party music). A menu theme, one track per map and a results theme.
// Each is a seamless loop: the tail (reverb, held notes) is folded onto the
// head, so the end flows into the start. The game plays the final lap of a
// race a little faster (a pitch shift of the same file), so no second copy.
//
//   node games/racer/tools/make-music.mjs [name ...]    (all when none named)
//
// Writes games/racer/assets/audio/music_<name>.ogg and prints each file's length, size, level and loop seam.
import { mkdirSync, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import * as L from './audiolib.mjs';
import { useSR, db, rmsOf, peakOf, centroid } from './audiolib.mjs';
import { Song, synth, pluck, mallet, calliope, organ, flute, SCALES, CHORD, finish } from './musiclib.mjs';

useSR(32000);
const SR = 32000;
const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'audio');
const TMP = join(HERE, '..', '..', '..', 'out', 'racer-audio');
mkdirSync(OUT, { recursive: true });

SCALES.mpent = [0, 3, 5, 7, 10];
/* a melody of whole bars: each string must be exactly sixteen steps */
const M = (...bars) => bars.map((b, i) => { const n = b.trim().split(/\s+/).length; if (n !== 16) throw new Error(`bar ${i} has ${n} steps: ${b}`); return b.trim(); }).join(' ');
const OFF = { 1: 0, 8: 12, 5: 7, 4: 5, b: 10, 2: 2, o: -12, 6: 9, 7: 11, a: -5, f: 1, c: 3, d: 8, g: -2 };
function bassLine(S, bus, inst, bar, root, q, pat, vel = 1) {
  const tones = CHORD[q], list = [];
  let last = null;
  for (let i = 0; i < pat.length; i++) {
    const c = pat[i];
    if (c === '.') { last = null; continue; }
    if (c === '-') { if (last) last.len++; continue; }
    last = { step: i, len: 1, midi: root + (c === '3' ? tones[1] : OFF[c]) };
    list.push(last);
  }
  for (const n of list) S.n(bus, inst, bar, n.step, n.len, n.midi, vel);
}
/* the whole chord on the marked steps */
function chordPat(S, bus, inst, bar, root, q, pat, { vel = 1, len = 1, oct = 0 } = {}) {
  for (let i = 0; i < pat.length; i++) if (pat[i] !== '.') for (const m of CHORD[q]) S.n(bus, inst, bar, i, len, root + m + 12 * oct, vel * (pat[i] === 'o' ? 0.6 : 1));
}
/* digits pick chord tones, wrapping up the octaves */
function arpPat(S, bus, inst, bar, root, q, pat, { vel = 1, len = 1, oct = 0 } = {}) {
  const t = CHORD[q];
  for (let i = 0; i < pat.length; i++) {
    if (pat[i] === '.') continue;
    const k = +pat[i];
    S.n(bus, inst, bar, i, len, root + t[k % t.length] + 12 * Math.floor(k / t.length) + 12 * oct, vel);
  }
}
function padHold(S, bus, inst, bar, bars, root, q, { vel = 1, oct = 0 } = {}) {
  for (const m of CHORD[q]) S.n(bus, inst, bar, 0, bars * 16, root + m + 12 * oct, vel);
}
const each = (a, b, fn) => { for (let i = a; i < b; i++) fn(i); };
/* a drum fill: toms falling over the last beat(s) of a bar */
function tomFill(S, bus, bar, from = 8, f = [190, 160, 135, 110], vel = 0.8) {
  for (let i = from, k = 0; i < 16; i++, k++) if ((i - from) % 2 === 0 || from >= 12) S.drum(bus, 'tom', bar, i, vel * (0.7 + 0.3 * i / 16), f[Math.min(f.length - 1, Math.floor(k * f.length / (16 - from)))]);
}

/* ═══ 1. Salt Flats Run: dawn desert surf rock ═════════════════════════ */
function salt() {
  const S = new Song({ bpm: 148, bars: 32, seed: 11 });
  S.addBus('dr', { gain: 0.9, rev: 0.07 }).addBus('bass', { gain: 0.8 }).addBus('lead', { gain: 0.8, pan: 0.1, rev: 0.5, dly: 0.25 })
    .addBus('rhy', { gain: 0.5, pan: -0.45, rev: 0.2 }).addBus('org', { gain: 0.35, pan: 0.4, rev: 0.4, chorusOn: true });
  const surf = pluck({ bright: 0.75, damp: 0.9988, ring: 0.5 }), chop = pluck({ bright: 0.4, damp: 0.985, ring: 0.1 });
  const bass = synth({ osc: [['saw', 0, 1], ['square', 0, 0.4, 0.4]], cut: 900, envAmt: 2, envDec: 0.08, a: 0.004, d: 0.1, s: 0.6, r: 0.05, sub: 0.5, drive: 1.4 });
  const pad = synth({ osc: [['saw', -8, 1], ['saw', 8, 1]], a: 0.3, d: 1, s: 0.8, r: 0.6, cut: 1500 });
  const sc = SCALES.phrygdom, root = 57;
  const A = [45, 45, 46, 45, 45, 45, 46, 40], B = [38, 38, 45, 45, 46, 46, 40, 40], C = [45, 46, 45, 46, 38, 40, 45, 45];
  const Aq = ['m', 'm', 'M', 'm', 'm', 'm', 'M', '7'], Bq = ['m', 'm', 'm', 'm', 'M', 'M', '7', '7'];
  const phA = M('5\' - - - - - 4\' - 3\' - - - 2\' - 1\' -', '2\' - 1\' - 7 - 5 - 4 - - - - - - -', '5\' - - - - - 4\' - 3\' - 2\' - 1\' - 7 -', '5 - - - 6 - 5 - 4 - 3 - - - 5 -');
  const phB = M('1\' - 3\' - 5\' - 3\' - 1\' - 3\' - 5\' - 3\' -', '7 - 2\' - 4\' - 2\' - 7 - 2\' - 4\' - 2\' -', '5 - 7 - 2\' - 7 - 5 - 7 - 2\' - 7 -', '3\' - - - 2\' - - - 1\' - - - 7 - 5 -');
  each(0, 32, (b) => {
    const sec = Math.floor(b / 8), i = b % 8, roots = sec === 1 ? B : sec === 3 ? C : A, qs = sec === 1 ? Bq : Aq, r = roots[i], q = qs[i];
    S.grid('dr', b, { kick: sec === 1 ? 'x.......x.x.....' : 'x.....x.x.x.....', snare: '....x.......x...', hat: 'x.x.x.x.x.x.x.x.' });
    if (i === 7) tomFill(S, 'dr', b);
    if (b % 8 === 0 && b > 0) S.drum('dr', 'crash', b, 0, 0.8);
    bassLine(S, 'bass', bass, b, r, q, sec === 1 ? '1..1..1.1..1..8.' : '1.1.8.1.1.1.8.1.');
    chordPat(S, 'rhy', chop, b, r + 24, q === 'm' ? 'm' : q === '7' ? 'M' : 'M', 'x.xxx.x.x.xxx.x.', { vel: 0.5 });
    if (sec !== 0) padHold(S, 'org', pad, b, 1, r + 24, q === '7' ? 'M' : q, { vel: 0.35 });
  });
  S.line('lead', surf, 0, phA, sc, root, { trem: 2, vel: 0.9 });
  S.line('lead', surf, 8 * 1, phB, sc, root, { trem: 2, vel: 0.9 });
  S.line('lead', surf, 12, phA, sc, root, { trem: 2, vel: 0.9 });
  S.line('lead', surf, 16, phA, sc, root, { trem: 2, vel: 0.9, octave: 0 });
  S.line('lead', surf, 20, phB, sc, root, { trem: 2, vel: 0.9 });
  S.line('lead', surf, 24, phB, sc, root, { trem: 2, vel: 0.85, octave: 1 });
  S.line('lead', surf, 28, phA, sc, root, { trem: 2, vel: 0.9 });
  return S.mix({ revSize: 0.88, revDamp: 0.3, dlyTime: 0.75 });
}

/* ═══ 2. Drowned Downtown: neon synthwave drive ════════════════════════ */
function drowned() {
  const S = new Song({ bpm: 108, bars: 32, seed: 22 });
  S.addBus('dr', { gain: 0.95, rev: 0.12 }).addBus('bass', { gain: 0.8, duck: 0.6 }).addBus('arp', { gain: 0.45, pan: 0.2, rev: 0.2, dly: 0.35, duck: 0.3 })
    .addBus('pad', { gain: 0.5, rev: 0.3, chorusOn: true, duck: 0.6 }).addBus('lead', { gain: 0.55, pan: -0.1, rev: 0.3, dly: 0.45 });
  const bass = synth({ osc: [['saw', 0, 1], ['saw', -8, 0.6]], cut: 700, envAmt: 2.5, envDec: 0.12, a: 0.003, d: 0.15, s: 0.5, r: 0.05, sub: 0.6, drive: 1.4, q: 1.2 });
  const arp = synth({ osc: [['square', 0, 0.7, 0.35], ['saw', 7, 0.4]], cut: 1800, envAmt: 2, envDec: 0.09, d: 0.1, s: 0.2, r: 0.04 });
  const pad = synth({ osc: [['saw', -9, 1], ['saw', 9, 1], ['saw', 0, 0.6]], a: 0.4, d: 1, s: 0.8, r: 0.8, cut: 1400 });
  const lead = synth({ osc: [['saw', -6, 1], ['saw', 6, 1], ['square', 0, 0.4]], vib: [5.5, 0.006, 0.25], cut: 3200, envAmt: 0.6, envDec: 0.3, a: 0.01, d: 0.3, s: 0.7, r: 0.2, q: 1 });
  const prog = [[41, 'm'], [37, 'M'], [44, 'M'], [39, 'M']], sc = SCALES.minor, root = 65;
  const l1 = M('5 - - - 3 - 1 - 3 - 5 - 8 - - -'.replace('8', '1\''), '6 - - - 5 - 3 - 1 - - - 3 - 5 -'.replace('6', '6'), '5 - - - 3 - 5 - 7 - - - 5 - 3 -', '2\' - - - 7 - - - 5 - 4 - 3 - 2 -');
  const l2 = M('1\' - . 1\' 7 - 5 - 1\' - . 1\' 3\' - 1\' -', '6 - . 6 5 - 3 - 6 - . 6 1\' - 6 -', '3\' - . 3\' 2\' - 1\' - 7 - . 7 5 - 7 -', '2\' - . 2\' 4\' - 2\' - 7 - 5 - 4 - 2 -');
  each(0, 32, (b) => {
    const [r, q] = prog[b % 4], groove = (b >= 4 && b < 16) || (b >= 20 && b < 28), hatsOnly = b < 4 || (b >= 16 && b < 20) || b >= 28;
    padHold(S, 'pad', pad, b, 1, r + 24, q, { vel: 0.6 });
    arpPat(S, 'arp', arp, b, r + 24, q, b < 16 || b >= 20 ? '0120210102101201'.split('').map((c) => (c === '0' ? '0' : c)).join('') : '0.1.2.1.0.1.2.1.', { vel: 0.6 });
    if (groove) {
      S.grid('dr', b, { kick: 'x...x...x...x...', snare: '....x.......x...', hat: 'x.x.x.x.x.x.x.xo', open: '..o...o...o...o.' }, { open: ['hat', true] });
      bassLine(S, 'bass', bass, b, r, q, '1.1.8.1.1.1.8.1.');
      if (b % 8 === 7) tomFill(S, 'dr', b, 12, [150, 130, 110, 90]);
      if (b % 8 === 0) S.drum('dr', 'crash', b, 0, 0.6);
    } else if (hatsOnly) {
      S.grid('dr', b, { hat: 'x...x...x...x...' });
      if (b === 30) S.grid('dr', b, { kick: 'x.......x.......' });
      if (b === 19 || b === 3) S.drum('dr', 'noise_up', b - 0, 0, 0.5, 2);
    }
  });
  S.line('lead', lead, 8, l1, sc, root, { vel: 0.7 });
  S.line('lead', lead, 12, l1, sc, root, { vel: 0.7, octave: 0 });
  S.line('lead', lead, 20, l2, sc, root, { vel: 0.7 });
  S.line('lead', lead, 24, l2, sc, root, { vel: 0.7, octave: 1 });
  return S.mix({ revSize: 0.9, revDamp: 0.4, dlyTime: 0.75, duckDepth: 0.55 });
}

/* ═══ 3. Rustwheel Fairgrounds: a calliope gone minor and menacing ═════ */
function fair() {
  const S = new Song({ bpm: 132, bars: 32, seed: 33 });
  S.addBus('dr', { gain: 0.85, rev: 0.1 }).addBus('bass', { gain: 0.9 }).addBus('org', { gain: 0.5, pan: -0.3, rev: 0.2 }).addBus('cal', { gain: 0.55, pan: 0.15, rev: 0.35, dly: 0.15 })
    .addBus('xy', { gain: 0.45, pan: 0.4, rev: 0.4, dly: 0.2 });
  const tuba = synth({ osc: [['square', 0, 1, 0.3], ['saw', 0, 0.5]], cut: 600, envAmt: 1.5, envDec: 0.08, a: 0.008, d: 0.15, s: 0.5, r: 0.05, sub: 0.6 });
  const cal = calliope(), org = organ({ bars: [1, 0.5, 0.7, 0.2, 0.3, 0.1] }), xy = mallet({ ratios: [1, 3, 6.3], amps: [1, 0.4, 0.2], dec: 9, bell: 1 });
  const prog = [[38, 'm'], [45, '7'], [38, 'm'], [43, 'm'], [38, 'm'], [45, '7'], [46, 'M'], [45, '7']], sc = SCALES.harmonic, root = 62;
  const mA = M('1\' . 1\' . 3\' . 5\' . 3\' . 1\' . 7 . 5 .', '5 . 7 . 2\' . 4\' . 2\' . 7 . 5 . 4 .', '1\' . 1\' . 3\' . 5\' . 6\' . 5\' . 3\' . 1\' .', '4\' . 3\' . 2\' . 1\' . 7 . 6 . 5 . - -');
  const mB = M('5 . 6 . 5 . 4 . 3 . 4 . 5 . 6 .', '7 . 1\' . 2\' . 1\' . 7 . 5 . 7 . - -', '6 . 5 . 4 . 3 . 4 . 3 . 2 . 1 .', '2 . 4 . 6 . 5 . 7 - - - - - - -');
  const mC = M('1\' - - 7 6 - - 5 4 - - 3 2 - - 1', '7 - - 6 5 - - 4 3 - - 2 1 - - 7,', '1\' - - 7 6 - - 5 4 - - 3 2 - - 1', '7, - - - - - - - - - - - - - - -');
  each(0, 32, (b) => {
    const [r, q] = prog[b % 8], sec = Math.floor(b / 8), calm = sec === 2;
    bassLine(S, 'bass', tuba, b, r, q, calm ? '1...............' : '1...5...1...5...', 0.9);
    if (!calm) chordPat(S, 'org', org, b, r + 24, q, '....x.......x...', { vel: 0.8, len: 2 });
    else chordPat(S, 'org', org, b, r + 24, q, 'x...............', { vel: 0.7, len: 16 });
    if (!calm) S.grid('dr', b, { kick: 'x.......x.......', snare: '....x.......x.x.', shk: 'x.x.x.x.x.x.x.x.' }, { shk: ['shaker'], snare: ['snare', { tone: 220, bright: 1.2 }] });
    else S.grid('dr', b, { kick: 'x...............', rim: '....o.......o...' });
    if (b % 4 === 3 && !calm) { for (let i = 12; i < 16; i++) S.drum('dr', 'snare', b, i, 0.5 + i * 0.03); }
    if (b % 8 === 0) S.drum('dr', 'crash', b, 0, 0.7);
  });
  S.line('cal', cal, 0, mA, sc, root, { vel: 0.9 }); S.line('cal', cal, 4, mA, sc, root, { vel: 0.9 });
  S.line('cal', cal, 8, mB, sc, root, { vel: 0.85 }); S.line('cal', cal, 12, mB, sc, root, { vel: 0.85, octave: 0 });
  S.line('xy', xy, 16, mC, sc, root, { vel: 0.9, octave: 1 }); S.line('xy', xy, 20, mC, sc, root, { vel: 0.8, octave: 1 });
  S.line('cal', cal, 24, mA, sc, root, { vel: 0.9 }); S.line('cal', cal, 28, mB, sc, root, { vel: 0.95 });
  S.line('xy', xy, 0, mB, sc, root, { vel: 0.5, octave: 1 }); S.line('xy', xy, 24, mC, sc, root, { vel: 0.5, octave: 1 });
  return S.mix({ revSize: 0.8, revDamp: 0.5, dlyTime: 0.5 });
}

/* ═══ 4. Cinder Refinery: an industrial stomp ══════════════════════════ */
function refinery() {
  const S = new Song({ bpm: 116, bars: 32, seed: 44 });
  S.addBus('dr', { gain: 0.95, rev: 0.1 }).addBus('bass', { gain: 0.9, duck: 0.25 }).addBus('stab', { gain: 0.5, pan: -0.25, rev: 0.2, dly: 0.2 })
    .addBus('ld', { gain: 0.5, pan: 0.25, rev: 0.25, dly: 0.3 }).addBus('drone', { gain: 0.4, rev: 0.3, chorusOn: true });
  const bass = synth({ osc: [['saw', 0, 1], ['saw', -12, 0.8], ['square', 0, 0.5, 0.45]], cut: 650, envAmt: 3, envDec: 0.1, a: 0.003, d: 0.12, s: 0.6, r: 0.04, sub: 0.5, drive: 3, q: 1.4 });
  const stab = synth({ osc: [['saw', -10, 1], ['saw', 10, 1]], cut: 1600, envAmt: 3, envDec: 0.1, a: 0.004, d: 0.2, s: 0.3, r: 0.08, drive: 1.6 });
  const ld = synth({ osc: [['saw', -5, 1], ['square', 5, 0.6, 0.3]], cut: 2200, envAmt: 1.6, envDec: 0.15, a: 0.006, d: 0.2, s: 0.6, r: 0.1, drive: 2, q: 2 });
  const drone = synth({ osc: [['saw', -14, 1], ['saw', 14, 1]], a: 1.5, d: 2, s: 0.9, r: 1.5, cut: 500, q: 1.5 });
  const sc = SCALES.phrygian, root = 52;
  const rf = ['1.1.1.f.1.1.4.c.', '1.1.1.f.1.1.c.f.', '1.1.1.f.1.1.4.5.', '1.1.d.5.4.c.f.1.'];
  const prog = [40, 40, 40, 40, 40, 40, 36, 38];
  const lA = M('1\' . . 1\' . . 2\' . 1\' . . 7 . . 1\' .', '. . 5 . . 4 . . 3 . . 4 . 5 . .', '1\' . . 1\' . . 2\' . 3\' . . 2\' . . 1\' .', '7 . 6 . 5 . 4 . 3 . 2 . 1 . . .');
  each(0, 32, (b) => {
    const sec = Math.floor(b / 8), i = b % 8, r = prog[i], full = sec !== 0 || i >= 4, kickOnly = sec === 3 && i >= 6;
    bassLine(S, 'bass', bass, b, r, 'm', rf[i % 4], 0.9);
    if (full) {
      S.grid('dr', b, { kick: kickOnly ? 'x...............' : 'x...x...x...x...', clap: '....x.......x...', hat: '..x...x...x...x.', clank: '..X..........x..' }, { clap: ['clap'], clank: ['clank', 520] });
      if (sec >= 1) S.grid('dr', b, { hat2: 'x.x.x.x.x.x.x.x.' }, { hat2: ['hat'] });
      if (i === 7) { tomFill(S, 'dr', b, 12, [110, 95, 80, 65]); S.drum('dr', 'noise_up', b, 0, 0.5, 2); }
    } else S.grid('dr', b, { kick: 'x.......x.......', clank: '..............x.' }, { clank: ['clank', 380] });
    if (sec >= 1 && i % 2 === 0) chordPat(S, 'stab', stab, b, r + 24, '5', i % 4 === 0 ? 'x..x..x.........' : 'x..x..x...x.....', { vel: 0.55, len: 1 });
    if (b % 8 === 0) padHold(S, 'drone', drone, b, 8, r + 12, '5', { vel: 0.4 });
    if (b % 8 === 0 && b > 0) S.drum('dr', 'crash', b, 0, 0.7);
  });
  S.line('ld', ld, 8, lA, sc, root, { vel: 0.7 }); S.line('ld', ld, 12, lA, sc, root, { vel: 0.7, octave: 1 });
  S.line('ld', ld, 20, lA, sc, root, { vel: 0.7 }); S.line('ld', ld, 24, lA, sc, root, { vel: 0.75, octave: 1 });
  return S.mix({ revSize: 0.75, revDamp: 0.55, dlyTime: 0.75 });
}

/* ═══ 5. Spillway Dam: an epic anthem ══════════════════════════════════ */
function dam() {
  const S = new Song({ bpm: 118, bars: 32, seed: 55 });
  S.addBus('dr', { gain: 0.95, rev: 0.25 }).addBus('bass', { gain: 0.75 }).addBus('str', { gain: 0.55, pan: -0.3, rev: 0.4, chorusOn: true })
    .addBus('brs', { gain: 0.6, pan: 0.2, rev: 0.4 }).addBus('pad', { gain: 0.4, rev: 0.5, chorusOn: true });
  const bass = synth({ osc: [['saw', 0, 1]], cut: 700, envAmt: 1.5, envDec: 0.1, a: 0.01, d: 0.2, s: 0.7, r: 0.08, sub: 0.7 });
  const str = synth({ osc: [['saw', -10, 1], ['saw', 10, 1], ['saw', 0, 0.7]], a: 0.06, d: 0.3, s: 0.8, r: 0.15, cut: 2400, envAmt: 0.5, envDec: 0.2 });
  const brs = synth({ osc: [['saw', -4, 1], ['saw', 4, 1], ['square', 0, 0.5, 0.4]], a: 0.04, d: 0.3, s: 0.8, r: 0.1, cut: 1700, envAmt: 1.5, envDec: 0.15, vib: [5, 0.004, 0.3], drive: 1.3 });
  const pad = synth({ osc: [['saw', -12, 1], ['saw', 12, 1]], a: 0.9, d: 1, s: 0.9, r: 1.2, cut: 1200 });
  const prog = [[38, 'M'], [45, 'M'], [47, 'm'], [43, 'M'], [38, 'M'], [45, 'M'], [43, 'M'], [45, 'M']], sc = SCALES.major, root = 62;
  const pA = M('5 - - - 1\' - - - 3\' - 2\' - 1\' - 7 -', '5 - - - 7 - - - 2\' - 1\' - 7 - 6 -', '3\' - - - 2\' - 1\' - 7 - - - 6 - 5 -', '4 - - - 6 - 1\' - 3\' - - - 2\' - 1\' -');
  const pB = M('1\' - - - - - - - 5\' - - - 3\' - - -', '2\' - - - - - - - 7 - - - 5 - - -', '3\' - - 3\' 4\' - 3\' - 2\' - - - 1\' - 7 -', '6 - 7 - 1\' - 2\' - 3\' - - - - - - -');
  const ost = ['0.1.2.1.0.1.2.1.', '0.1.2.1.0.1.2.3.'];
  each(0, 32, (b) => {
    const [r, q] = prog[b % 8], sec = Math.floor(b / 8), mid = sec % 2 === 1;
    padHold(S, 'pad', pad, b, 1, r + 24, q, { vel: 0.6 });
    bassLine(S, 'bass', bass, b, r, q, sec === 0 && b < 4 ? '1...............' : '1.8.1.8.1.8.1.8.', 0.8);
    if (sec > 0 || b >= 4) arpPat(S, 'str', str, b, r + 24, q, ost[b % 2], { vel: 0.55, len: 2 });
    S.grid('dr', b, { deep: 'X.....x.x.......', snare: '....X.......X...' }, { deep: ['deep'], snare: ['snare', { tone: 180, dec: 14, ndec: 9, len: 0.5, body: 0.8 }] });
    if (mid) S.grid('dr', b, { tm: '..........x.x.x.' }, { tm: ['tom', 100] });
    if (b % 4 === 3) { for (let i = 12; i < 16; i++) S.drum('dr', 'deep', b, i, 0.4 + 0.1 * (i - 12)); }
    if (b % 8 === 0) S.drum('dr', 'crash', b, 0, 0.9);
    if (b % 2 === 0) S.drum('dr', 'ride', b, 8, 0.4);
  });
  S.line('brs', brs, 8, pA, sc, root, { vel: 0.8 }); S.line('brs', brs, 12, pA, sc, root, { vel: 0.8, octave: 0 });
  S.line('brs', brs, 16, pB, sc, root, { vel: 0.8 }); S.line('brs', brs, 20, pB, sc, root, { vel: 0.85, octave: 0 });
  S.line('brs', brs, 24, pA, sc, root, { vel: 0.9 }); S.line('brs', brs, 28, pB, sc, root, { vel: 0.9 });
  return S.mix({ revSize: 0.9, revDamp: 0.35, dlyTime: 0.75 });
}

/* ═══ 6. Frostbite Pass: ambient, then driving, then back to ambient ═══ */
function frost() {
  const S = new Song({ bpm: 126, bars: 32, seed: 66 });
  S.addBus('dr', { gain: 0.8, rev: 0.12 }).addBus('bass', { gain: 0.8, duck: 0.3 }).addBus('bell', { gain: 0.5, pan: 0.25, rev: 0.5, dly: 0.5 })
    .addBus('pad', { gain: 0.5, rev: 0.5, chorusOn: true, duck: 0.35 }).addBus('lead', { gain: 0.5, pan: -0.15, rev: 0.45, dly: 0.4 });
  const bell = mallet({ ratios: [1, 2.76, 5.4], amps: [1, 0.4, 0.2], dec: 3.5, bell: 1, knock: 0.05 });
  const bass = synth({ osc: [['saw', 0, 0.8], ['sin', 0, 1]], cut: 500, envAmt: 1.5, envDec: 0.1, a: 0.005, d: 0.2, s: 0.6, r: 0.06, sub: 0.7 });
  const pad = synth({ osc: [['saw', -12, 1], ['saw', 12, 1], ['tri', 0, 0.8]], a: 1.2, d: 1.5, s: 0.9, r: 1.5, cut: 1800 });
  const lead = synth({ osc: [['tri', 0, 1], ['sin', 1200, 0.3]], cut: 4000, a: 0.02, d: 0.4, s: 0.6, r: 0.4, vib: [4.5, 0.007, 0.3] });
  const prog = [[45, 'm'], [41, 'M'], [48, 'M'], [43, 'M']], sc = SCALES.minor, root = 69;
  const ld = M('5 - - - - - 3 - 5 - - - 1\' - - -', '4 - - - - - 3 - 1 - - - 3 - - -', '5 - - - 1\' - - - 3\' - - - 2\' - 1\' -', '7 - - - 2\' - - - 5 - - - - - - -');
  each(0, 32, (b) => {
    const [r, q] = prog[b % 4];
    const drive = (b >= 16 && b < 28), build = b >= 8 && b < 16, soft = b >= 24 && b < 28;
    padHold(S, 'pad', pad, b, 1, r + 24, q, { vel: b < 8 ? 0.55 : 0.7 });
    arpPat(S, 'bell', bell, b, r + 36, q, b >= 16 && b < 24 ? '0.1.2.1.0.1.2.3.' : b < 28 || true ? '0...2...1...3...' : '', { vel: 0.5 });
    if (b >= 4 && b < 28) bassLine(S, 'bass', bass, b, r, q, drive || b >= 12 ? '1.1.1.1.1.1.1.1.' : '1...............', 0.8);
    if (build) { S.grid('dr', b, { hat: b >= 12 ? 'x.x.x.x.x.x.x.x.' : '..x...x...x...x.' }); if (b >= 12) S.grid('dr', b, { kick: 'x...x...x...x...' }, { kick: ['kick', 0.7, { f0: 110, dec: 16 }] }); if (b === 15) { for (let i = 8; i < 16; i++) S.drum('dr', 'snare', b, i, 0.3 + 0.07 * (i - 8)); } }
    if (drive) { S.grid('dr', b, { kick: 'x...x...x...x...', snare: '....x.......x...', hat: 'x.x.x.x.x.x.x.x.', op: '..o...o...o...o.' }, { op: ['hat', true] }); if (b % 8 === 7) tomFill(S, 'dr', b, 12, [140, 120, 100, 80]); if (b % 8 === 0) S.drum('dr', 'crash', b, 0, 0.7); }
    if (b >= 24 && b < 28) S.grid('dr', b, { hat: '..x...x...x...x.' });
    if (soft && b === 27) S.drum('dr', 'noise_up', b, 0, 0.3, 2);
  });
  S.line('lead', lead, 8, ld, sc, root, { vel: 0.5 }); S.line('lead', lead, 16, ld, sc, root, { vel: 0.6 }); S.line('lead', lead, 20, ld, sc, root, { vel: 0.65, octave: 1 });
  return S.mix({ revSize: 0.92, revDamp: 0.4, dlyTime: 0.75 });
}

/* ═══ 7. Overgrowth Temple: tribal drums and marimba ═══════════════════ */
function temple() {
  const S = new Song({ bpm: 136, bars: 32, seed: 77 });
  S.addBus('dr', { gain: 0.9, rev: 0.18 }).addBus('mar', { gain: 0.65, pan: -0.25, rev: 0.25 }).addBus('mar2', { gain: 0.55, pan: 0.3, rev: 0.25 })
    .addBus('fl', { gain: 0.5, pan: 0.1, rev: 0.5, dly: 0.3 }).addBus('drone', { gain: 0.5, rev: 0.3 }).addBus('bass', { gain: 0.8 });
  const mar = mallet({ dec: 6 }), fl = flute();
  const dj = synth({ osc: [['saw', -6, 1], ['saw', 6, 1], ['square', 0, 0.5, 0.2]], cut: 380, q: 3, a: 0.15, d: 0.5, s: 0.9, r: 0.3, drive: 1.3 });
  const bass = synth({ osc: [['sin', 0, 1], ['tri', 0, 0.4]], cut: 450, a: 0.005, d: 0.2, s: 0.5, r: 0.1 });
  const sc = SCALES.mpent, root = 62;
  const m1 = M('1 . 3 . 5 . 3 . 1 . 3 . 4 . 2 .', '1 . 3 . 5 . 3 . 1 . 3 . 5 . 4 .', '5 . 3 . 4 . 2 . 5 . 3 . 4 . 2 .', '1 . 3 . 5 . 3 . 4 . 3 . 2 . 1 .');
  const m2 = M('. 5 . 1\' . 5 . 3 . 5 . 1\' . 3 . 4', '. 5 . 1\' . 5 . 3 . 5 . 1\' . 3 . 4', '. 3 . 5 . 4 . 2 . 3 . 5 . 4 . 2', '. 5 . 1\' . 3 . 5 . 4 . 3 . 2 . 1');
  const fA = M('5 - - - 3 - 5 - 1\' - - - - - - -', '4 - - - 3 - 1 - 3 - - - - - - -', '5 - - - 1\' - - - 3\' - - 2\' 1\' - - -', '5 - 4 - 3 - 2 - 1 - - - - - - -');
  each(0, 32, (b) => {
    const break1 = b >= 16 && b < 20, intro = b < 4, full = b >= 20 || (b >= 8 && b < 16);
    S.grid('dr', b, { lo: 'x.....x...x.....', hi: '..x.x.....x.x.x.', sl: '........x.....x.' }, { lo: ['skin', 0.9, 190], hi: ['skin', 0.7, 330], sl: ['skin', 0.8, 420, true] });
    if (!break1) S.grid('dr', b, { shk: intro ? '....x.......x...' : 'x.xxx.xxx.xxx.xx' }, { shk: ['shaker'] });
    if (!intro && !break1) S.grid('dr', b, { tk: 'X.......x.x.....' }, { tk: ['deep'] });
    if (b % 4 === 3 && !break1) { for (let i = 12; i < 16; i++) S.drum('dr', 'tom', b, i, 0.6 + 0.08 * (i - 12), 130 - (i - 12) * 10); }
    if (full && b % 8 === 0) S.drum('dr', 'crash', b, 0, 0.5);
    if (!intro) padHold(S, 'drone', dj, b, 1, 38, '5', { vel: 0.35 });
    if (!break1) bassLine(S, 'bass', bass, b, 38, 'm', '1.....1...1.....', 0.7);
  });
  S.line('mar', mar, 0, m1, sc, root, { vel: 0.75 }); S.line('mar', mar, 4, m1, sc, root, { vel: 0.75 });
  S.line('mar2', mar, 4, m2, sc, root, { vel: 0.65, octave: 1 }); S.line('mar', mar, 8, m1, sc, root, { vel: 0.75 }); S.line('mar', mar, 12, m1, sc, root, { vel: 0.75 });
  S.line('mar2', mar, 8, m2, sc, root, { vel: 0.65, octave: 1 }); S.line('mar2', mar, 12, m2, sc, root, { vel: 0.65, octave: 1 });
  S.line('mar', mar, 16, m1, sc, root, { vel: 0.6, octave: -1 });
  for (const b0 of [20, 24, 28]) { S.line('mar', mar, b0, m1, sc, root, { vel: 0.75 }); S.line('mar2', mar, b0, m2, sc, root, { vel: 0.65, octave: 1 }); }
  S.line('fl', fl, 8, fA, sc, root, { vel: 0.7, octave: 1 }); S.line('fl', fl, 12, fA, sc, root, { vel: 0.7, octave: 1 });
  S.line('fl', fl, 24, fA, sc, root, { vel: 0.75, octave: 1 });
  return S.mix({ revSize: 0.85, revDamp: 0.45, dlyTime: 0.75 });
}

/* ═══ 8. Wreck Orbit: a slow space-station arpeggio engine ═════════════ */
function orbit() {
  const S = new Song({ bpm: 98, bars: 32, seed: 88 });
  S.addBus('dr', { gain: 0.8, rev: 0.2 }).addBus('sub', { gain: 0.8, duck: 0.3 }).addBus('arp', { gain: 0.5, pan: 0.1, rev: 0.35, dly: 0.5, duck: 0.25 })
    .addBus('pad', { gain: 0.45, rev: 0.6, chorusOn: true, duck: 0.4 }).addBus('hi', { gain: 0.35, pan: -0.3, rev: 0.7, dly: 0.5 }).addBus('lead', { gain: 0.5, rev: 0.5, dly: 0.55 });
  const mk = (cut) => synth({ osc: [['saw', -7, 0.8], ['saw', 7, 0.8], ['square', 0, 0.3, 0.45]], cut, envAmt: 1.8, envDec: 0.12, a: 0.004, d: 0.12, s: 0.2, r: 0.06, q: 1.4 });
  const arps = [mk(900), mk(1600), mk(2800)];
  const sub = synth({ osc: [['sin', 0, 1], ['tri', 0, 0.3]], cut: 400, a: 0.02, d: 0.4, s: 0.8, r: 0.3 });
  const pad = synth({ osc: [['saw', -13, 1], ['saw', 13, 1], ['saw', 700, 0.3]], a: 1.5, d: 2, s: 0.9, r: 2, cut: 1500 });
  const bell = mallet({ ratios: [1, 2.76, 5.4], amps: [1, 0.4, 0.2], dec: 2.5, bell: 1, knock: 0.02 });
  const lead = synth({ osc: [['sin', 0, 1], ['tri', 3, 0.5]], cut: 3000, a: 0.08, d: 0.5, s: 0.7, r: 0.6, vib: [4.2, 0.01, 0.5], gliss: 0.08 });
  const prog = [[47, 'm9'], [43, 'M7'], [50, 'M7'], [45, 'sus']], sc = SCALES.minor, root = 71;
  const ld = M('5 - - - - - - - 3 - - - - - 2 -', '1 - - - - - - - 7, - - - - - - -', '5 - - - - - - - 6 - - - 5 - 3 -', '2 - - - - - - - - - - - - - - -');
  each(0, 32, (b) => {
    const [r, q] = prog[b % 4], lvl = b < 8 ? 0 : b < 24 ? 1 : 2, run = b >= 8 && b < 28;
    padHold(S, 'pad', pad, b, 1, r + 24, q, { vel: 0.55 });
    arpPat(S, 'arp', arps[b >= 16 && b < 28 ? 2 : b >= 8 ? 1 : 0], b, r + 24, q, b % 2 ? '0123432101234321' : '0123454321012345', { vel: 0.5 });
    bassLine(S, 'sub', sub, b, r, q, run ? '1...1...1...1...' : '1---------------', 0.7);
    if (b % 4 === 0) arpPat(S, 'hi', bell, b, r + 48, q, '0.......2.......', { vel: 0.5 });
    if (run) S.grid('dr', b, { kick: b >= 12 ? 'x.......x.......' : 'x...............', clap: b >= 12 ? '............x...' : '................', tick: 'x.x.x.x.x.x.x.xx' }, { clap: ['clap'], tick: ['block', 0.35, 3200] });
    if (b === 7 || b === 15 || b === 27) S.drum('dr', 'noise_up', b, 0, 0.45, 3);
    if (b === 8 || b === 16) S.drum('dr', 'crash', b, 0, 0.5);
  });
  S.line('lead', lead, 12, ld, sc, root, { vel: 0.6 }); S.line('lead', lead, 20, ld, sc, root, { vel: 0.6, octave: 1 });
  return S.mix({ revSize: 0.93, revDamp: 0.35, dlyTime: 0.75, duckDepth: 0.4 });
}

/* ═══ 9. Menu: a jaunty funk-rock theme ════════════════════════════════ */
function menu() {
  const S = new Song({ bpm: 112, bars: 24, seed: 99, swing: 0.12 });
  S.addBus('dr', { gain: 0.9, rev: 0.1 }).addBus('bass', { gain: 0.85 }).addBus('brs', { gain: 0.5, pan: 0.2, rev: 0.25 }).addBus('org', { gain: 0.4, pan: -0.3, rev: 0.2, chorusOn: true })
    .addBus('lead', { gain: 0.55, pan: 0.05, rev: 0.3, dly: 0.25 });
  const bass = synth({ osc: [['square', 0, 1, 0.4], ['saw', 0, 0.5]], cut: 900, envAmt: 4, envDec: 0.06, a: 0.003, d: 0.1, s: 0.3, r: 0.04, sub: 0.4 });
  const brs = synth({ osc: [['saw', -4, 1], ['saw', 4, 1]], a: 0.015, d: 0.2, s: 0.6, r: 0.06, cut: 1800, envAmt: 2, envDec: 0.1 });
  const org = organ(), lead = synth({ osc: [['saw', -5, 1], ['saw', 5, 1], ['square', 0, 0.3]], vib: [5.5, 0.005, 0.2], cut: 3000, envAmt: 0.8, envDec: 0.25, a: 0.01, d: 0.25, s: 0.7, r: 0.15 });
  const prog = [[43, 'M'], [48, 'M'], [50, 'M'], [48, 'M']], sc = SCALES.major, root = 67;
  const l1 = M('5 . 5 . 1\' . 5 . 3\' - - 2\' 1\' . 5 .', '5 . 5 . 1\' . 5 . 4\' - - 3\' 1\' . 3 .', '4 . 4 . 6 . 4 . 2\' - - 1\' 7 . 5 .', '5 . 5 . 1\' . 5 . 3\' - 2\' - 1\' - - -');
  each(0, 24, (b) => {
    const [r, q] = prog[b % 4], on = b >= 4 && b < 20;
    S.grid('dr', b, { kick: 'x.....x..x......', snare: '....x..o.o..x..o', hat: 'x.xxx.xxx.xxx.xx' }, { snare: ['snare', { tone: 200 }] });
    if (b % 8 === 7) tomFill(S, 'dr', b);
    if (b % 8 === 0) S.drum('dr', 'crash', b, 0, 0.6);
    bassLine(S, 'bass', bass, b, r, q, b % 2 ? '1..1..8.1.5..b1.' : '1..1..1.8..5.1.o', 0.8);
    chordPat(S, 'org', org, b, r + 24, q, '..x...x...x...x.', { vel: 0.5 });
    if (on) chordPat(S, 'brs', brs, b, r + 24, q, 'x..x..x.........', { vel: 0.7, len: 1 });
  });
  S.line('lead', lead, 4, l1, sc, root, { vel: 0.7 }); S.line('lead', lead, 12, l1, sc, root, { vel: 0.7 }); S.line('lead', lead, 20, l1, sc, root, { vel: 0.65, octave: 0 });
  return S.mix({ revSize: 0.8, revDamp: 0.5, dlyTime: 0.75 });
}

/* ═══ 10. Results: a short victory groove ══════════════════════════════ */
function results() {
  const S = new Song({ bpm: 120, bars: 12, seed: 111 });
  S.addBus('dr', { gain: 0.9, rev: 0.12 }).addBus('bass', { gain: 0.8 }).addBus('brs', { gain: 0.6, pan: 0.15, rev: 0.35 }).addBus('bell', { gain: 0.5, pan: -0.25, rev: 0.4, dly: 0.3 });
  const bass = synth({ osc: [['saw', 0, 1]], cut: 800, envAmt: 2, envDec: 0.08, a: 0.004, d: 0.12, s: 0.5, r: 0.05, sub: 0.6 });
  const brs = synth({ osc: [['saw', -5, 1], ['saw', 5, 1], ['square', 0, 0.4, 0.4]], a: 0.02, d: 0.25, s: 0.7, r: 0.1, cut: 1900, envAmt: 1.8, envDec: 0.12, drive: 1.2 });
  const bell = mallet({ ratios: [1, 2.76, 5.4], amps: [1, 0.4, 0.2], dec: 4, bell: 1 });
  const prog = [[48, 'M'], [53, 'M'], [55, 'M'], [48, 'M']], sc = SCALES.major, root = 72;
  const fan = M('5 . . 5 1\' - - - 3\' - - - 5\' - - -', '4\' - - - 3\' - - - 2\' - 1\' - 7 - 1\' -', '6 . . 6 1\' - - - 4\' - - - 6\' - - -', '5 - - - 7 - - - 2\' - 4\' - 5\' - - -');
  const mel = M('3\' - - 2\' 1\' - - 3\' 5\' - - 3\' 1\' - - -', '2\' - - 1\' 7 - - 2\' 4\' - - 2\' 7 - - -', '1\' - - 7 6 - - 1\' 3\' - - 1\' 6 - - -', '7 - - 6 5 - - 7 2\' - - 7 5 - - -');
  each(0, 12, (b) => {
    const [r, q] = prog[b % 4];
    S.grid('dr', b, { kick: 'x...x...x...x...', snare: '....x.......x...', hat: 'x.x.x.x.x.x.x.xo' });
    bassLine(S, 'bass', bass, b, r, q, '1.1.8.1.1.1.8.1.', 0.8);
    arpPat(S, 'bell', bell, b, r + 36, q, '0.1.2.1.0.1.2.3.', { vel: 0.5 });
    if (b >= 4) chordPat(S, 'brs', brs, b, r + 24, q, 'x..x..x...x.....', { vel: 0.6 });
    if (b % 4 === 0) S.drum('dr', 'crash', b, 0, 0.7);
  });
  S.line('brs', brs, 0, fan, sc, root, { vel: 0.8 }); S.line('brs', brs, 4, mel, sc, root, { vel: 0.7 }); S.line('brs', brs, 8, mel, sc, root, { vel: 0.7, octave: 0 });
  return S.mix({ revSize: 0.85, revDamp: 0.4 });
}

const SONGS = { menu, salt, drowned, fair, refinery, dam, frost, temple, orbit, results };
const want = process.argv.slice(2).filter((a) => !a.startsWith('--'));
const names = want.length ? want : Object.keys(SONGS);
console.log('\nmusic                       bytes    sec  peak   rms   centroid  seam(L/R, steps)  tempo');
let total = 0;
for (const name of names) {
  const chans = finish(SONGS[name](), { rms: 0.13, peak: 0.8 });
  const prePeak = Math.max(...chans.map((c) => peakOf(c))), nan = chans.reduce((s, c) => s + c.reduce((q, v) => q + (Number.isFinite(v) ? 0 : 1), 0), 0);
  if (nan || prePeak > 0.85) console.log('  WARNING: before encoding peak ' + prePeak.toFixed(2) + ', non-finite ' + nan);
  const path = join(OUT, 'music_' + name + '.ogg');
  L.writeOgg(path, chans, TMP, 1, SR);
  const dec = L.decode(path, TMP, SR, 2), bytes = statSync(path).size;
  total += bytes;
  /* the seam: the jump from the last sample to the first against the average step, on the decoded file */
  const sm = dec.map((c) => L.seam(c));
  const mono = dec[0].map((x, i) => (x + dec[1][i]) / 2);
  console.log(`music_${name}.ogg`.padEnd(24), String(bytes).padStart(8), (dec[0].length / SR).toFixed(1).padStart(6), peakOf(mono).toFixed(2).padStart(5), db(rmsOf(mono)).toFixed(1).padStart(6), centroid(mono, SR).toFixed(0).padStart(8), `  ${sm[0].toFixed(2)}/${sm[1].toFixed(2)}`.padEnd(18));
  if (peakOf(mono) > 0.98) console.log('  WARNING: clipping');
  if (sm[0] > 8 || sm[1] > 8) console.log('  WARNING: loop seam jump');
}
console.log(`music total ${(total / 1024).toFixed(0)} KB`);
