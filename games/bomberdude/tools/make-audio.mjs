// Regenerates bomberdude's audio, all synthesized here (no third-party
// content): the sound effects as WAV, and a music loop per arena mood (plus
// the title's) from a small step sequencer, encoded to Ogg Vorbis with ffmpeg.
//
//   node games/bomberdude/tools/make-audio.mjs
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { rng } from '../../../deps/cartwheel-engine/tools/lib/rng.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const D = join(HERE, '..', 'assets', 'audio');
const TMP = join(HERE, '..', '..', '..', 'out', 'bomberdude-audio');
mkdirSync(D, { recursive: true });
mkdirSync(TMP, { recursive: true });
const SR = 48000;
// seeded noise from tools/lib/rng.mjs (exact 32-bit integer math: an LCG in
// doubles once fell into a 419-sample cycle, a 114.6 Hz buzz in every noise)
const noise = rng(0xb0b).noise;

// band-limited oscillators (PolyBLEP): the naive square / saw alias, folding
// harmonics past Nyquist back down as inharmonic buzz. Stateless: phase in
// cycles, dt = f / SR.
const blep = (t, dt) => {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
};
const sawB = (ph, dt) => { ph -= Math.floor(ph); return 2 * ph - 1 - blep(ph, dt); };
const sqrB = (ph, dt) => { ph -= Math.floor(ph); return (ph < 0.5 ? 1 : -1) + blep(ph, dt) - blep((ph + 0.5) % 1, dt); };
const sqr = (f, t) => sqrB(f * t, f / SR);
const TAU = 2 * Math.PI;

function wav(path, a) {
  const n = a.length;
  const b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVE', 8); b.write('fmt ', 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(SR, 24);
  b.writeUInt32LE(SR * 2, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(Math.max(-1, Math.min(1, a[i])) * 32767), 44 + i * 2);
  writeFileSync(path, b);
}
const buf = (sec) => new Float32Array(Math.ceil(sec * SR));
const norm = (a, peak = 0.9) => { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); for (let i = 0; i < a.length; i++) a[i] *= peak / (m || 1); return a; };
const lowpass = (a, hz) => { const k = 1 - Math.exp(-TAU * hz / SR); let y = 0; for (let i = 0; i < a.length; i++) { y += k * (a[i] - y); a[i] = y; } return a; };
const highpass = (a, hz) => { const k = 1 - Math.exp(-TAU * hz / SR); let y = 0; for (let i = 0; i < a.length; i++) { y += k * (a[i] - y); a[i] -= y; } return a; };
/* every sound: DC removed, a 2 ms attack ramp, a fade to exactly 0 over the
 * last 12 ms (no truncation click), peaks at 0.7 so overlapping voices leave
 * the mix headroom */
function finish(a, peak = 0.7) {
  highpass(a, 20);
  const att = Math.floor(0.002 * SR), rel = Math.min(Math.floor(0.012 * SR), a.length >> 1);
  for (let i = 0; i < att && i < a.length; i++) a[i] *= i / att;
  for (let i = 0; i < rel; i++) a[a.length - 1 - i] *= i / rel;
  return norm(a, peak);
}
/* a one-pole lowpass whose cutoff glides from f0 to f1 over the sound (noise bursts that dull as they die) */
const lpSweep = (a, f0, f1) => {
  let y = 0;
  for (let i = 0; i < a.length; i++) {
    const hz = f0 * Math.pow(f1 / f0, i / a.length), k = 1 - Math.exp(-TAU * hz / SR);
    y += k * (a[i] - y);
    a[i] = y;
  }
  return a;
};
function sfx(name, sec, fn, post) {
  const a = buf(sec);
  for (let i = 0; i < a.length; i++) a[i] = fn(i / SR, i);
  wav(join(D, name + '.wav'), finish(post ? post(a) : a));
}
const env = (t, a, d) => (t < a ? t / a : Math.exp(-(t - a) * d));

// ── sound effects ──────────────────────────────────────────────────────
// fuse: a hiss with crackle
{ /* fuse: a soft hiss band-passed to 3-8 kHz, crackles as smoothed bumps, a gentle attack */
  const a = buf(0.5);
  let crack = 0, target = 0;
  for (let i = 0; i < a.length; i++) {
    const t = i / SR;
    if (i % 240 === 0) target = noise() > 0.7 ? 1 : 0;
    crack += (target - crack) * 0.02; /* ~1 ms smoothing */
    a[i] = noise() * env(t, 0.03, 4) * (0.55 + 0.45 * crack);
  }
  highpass(highpass(a, 3000), 3000);
  lowpass(lowpass(a, 8000), 8000);
  wav(join(D, 'fuse.wav'), finish(a, 0.45));
}
// boom: a low thump, a noise body and a rumble tail
for (const [name, len, f0] of [['boom', 1.3, 70], ['boom_big', 2.0, 52]]) {
  const a = buf(len);
  let ph = 0;
  for (let i = 0; i < a.length; i++) {
    const t = i / SR, f = f0 * (1 + 2.5 * Math.exp(-t * 18));
    ph += TAU * f / SR;
    a[i] = Math.sin(ph) * env(t, 0.004, 5) * 1.1;
  }
  const crack = buf(len);
  for (let i = 0; i < crack.length; i++) crack[i] = noise() * env(i / SR, 0.003, 7) * 0.9;
  lpSweep(crack, 5000, 300);
  for (let i = 0; i < a.length; i++) a[i] += crack[i];
  const n = buf(len);
  for (let i = 0; i < n.length; i++) n[i] = noise() * env(i / SR, 0.01, 2.2) * 0.6;
  lowpass(n, 300);
  for (let i = 0; i < a.length; i++) a[i] += n[i];
  wav(join(D, name + '.wav'), finish(lowpass(a, 2600), 0.8));
}
// crate: woody crack and clatter
sfx('crate', 0.6, (t) => {
  let v = noise() * env(t, 0.0015, 30) * 0.6;
  for (const [ft, f] of [[0, 380], [0.06, 520], [0.13, 300], [0.2, 450]]) if (t > ft) v += Math.sin(TAU * f * (t - ft)) * env(t - ft, 0.001, 35) * 0.5;
  return v;
}, (a) => lpSweep(a, 4500, 900));
// pick-up: a bright rising arpeggio
sfx('pick', 0.45, (t) => {
  const notes = [784, 988, 1175, 1568], k = Math.min(3, Math.floor(t / 0.06));
  const f = notes[k], tt = t - k * 0.06;
  return (Math.sin(TAU * f * tt) + 0.3 * Math.sin(TAU * f * 2 * tt)) * env(tt, 0.004, 9) * 0.6;
});
// bubble pop
sfx('pop', 0.3, (t) => Math.sin(TAU * (900 - 2000 * t) * t) * env(t, 0.002, 22) + noise() * env(t, 0.0015, 60) * 0.2, (a) => lpSweep(a, 6000, 1500));
// knockout: a cartoon boing down and a little "oof"
sfx('ko', 0.9, (t) => {
  const boing = Math.sin(TAU * (520 * Math.exp(-t * 2.2)) * t + 3 * Math.sin(TAU * 9 * t)) * env(t, 0.005, 3) * 0.7;
  const oof = t > 0.05 && t < 0.35 ? Math.sin(TAU * 180 * t + Math.sin(TAU * 360 * t) * 2) * Math.sin(Math.PI * (t - 0.05) / 0.3) * 0.5 : 0;
  return boing + oof;
});
sfx('kick', 0.25, (t) => Math.sin(TAU * 150 * t * (1 - t)) * env(t, 0.002, 25) + noise() * env(t, 0.0015, 70) * 0.25, (a) => lpSweep(a, 4000, 600));
sfx('tele', 0.7, (t) => Math.sin(TAU * (400 + 1400 * t) * t) * Math.sin(TAU * 7 * t) * env(t, 0.05, 4) * 0.7);
sfx('vent', 1.0, (t) => noise() * env(t, 0.08, 3) * (0.6 + 0.4 * Math.sin(TAU * 13 * t)), (a) => lpSweep(lowpass(a, 2000), 1600, 500));
sfx('cannon', 1.2, (t) => Math.sin(TAU * 60 * (1 + 3 * Math.exp(-t * 30)) * t) * env(t, 0.003, 6) + noise() * env(t, 0.002, 9) * 0.8,
  (a) => lowpass(a, 1800));
sfx('thud', 0.5, (t) => Math.sin(TAU * 55 * t) * env(t, 0.002, 12) + noise() * env(t, 0.0015, 40) * 0.3, (a) => lowpass(a, 700));
sfx('move', 0.08, (t) => Math.sin(TAU * 1300 * t) * env(t, 0.002, 60) * 0.5);
sfx('select', 0.3, (t) => (Math.sin(TAU * 880 * t) + Math.sin(TAU * 1320 * t) * 0.6) * env(t, 0.003, 12) * 0.6);
sfx('locked', 0.3, (t) => sqr(140, t) * env(t, 0.003, 14) * 0.4, (a) => lowpass(a, 1500));
sfx('unlock', 0.9, (t) => {
  const notes = [523, 659, 784, 1047], k = Math.min(3, Math.floor(t / 0.09));
  const tt = t - k * 0.09;
  return (Math.sin(TAU * notes[k] * tt) + 0.4 * Math.sin(TAU * notes[k] * 3 * tt)) * env(tt, 0.004, 5) * 0.6;
});
// ready: two soft blips; go: a punchy stab
sfx('ready', 0.5, (t) => Math.sin(TAU * 660 * t) * (env(t, 0.005, 14) + (t > 0.2 ? env(t - 0.2, 0.005, 14) : 0)) * 0.6);
sfx('go', 0.9, (t) => {
  let v = 0;
  for (const f of [262, 330, 392, 523]) v += sqr(f, t) * 0.2;
  return v * env(t, 0.004, 4) + Math.sin(TAU * 60 * t) * env(t, 0.002, 10) * 0.8 + noise() * env(t, 0.001, 30) * 0.4;
}, (a) => lowpass(a, 3000));
// fanfares
function fanfare(name, notes, step) {
  sfx(name, notes.length * step + 0.8, (t) => {
    let v = 0;
    notes.forEach((f, k) => {
      const tt = t - k * step;
      if (tt < 0) return;
      const last = k === notes.length - 1;
      const e = env(tt, 0.01, last ? 1.6 : 6);
      v += (Math.sin(TAU * f * tt) + 0.5 * Math.sin(TAU * f * 2 * tt) + 0.25 * sqr(f, tt)) * e * 0.35;
    });
    return v;
  }, (a) => lowpass(a, 5000));
}
fanfare('round_win', [523, 659, 784, 1047], 0.11);
fanfare('match_win', [392, 523, 659, 784, 659, 784, 1047, 1319], 0.13);
{ /* alarm: two pitches, a continuous phase that glides between them over 4 ms, band-limited */
  const a = buf(1.2);
  let ph = 0, f = 660;
  for (let i = 0; i < a.length; i++) {
    const t = i / SR, target = Math.floor(t * 6) % 2 ? 880 : 660;
    f += (target - f) * (1 - Math.exp(-1 / (0.004 * SR)));
    ph += f / SR;
    a[i] = sqrB(ph, f / SR) * 0.35 * env(t, 0.004, 0) * Math.min(1, (1.2 - t) / 0.08);
  }
  wav(join(D, 'alarm.wav'), finish(lowpass(a, 3000)));
}

// ── music: a step sequencer ───────────────────────────────────────────
const midi = (n) => 440 * Math.pow(2, (n - 69) / 12);
const SCALES = { major: [0, 2, 4, 5, 7, 9, 11], minor: [0, 2, 3, 5, 7, 8, 10], dorian: [0, 2, 3, 5, 7, 9, 10], pent: [0, 2, 4, 7, 9],
  mixo: [0, 2, 4, 5, 7, 9, 10], phryg: [0, 1, 3, 5, 7, 8, 10] };
// per mood: tempo, root (midi), scale, chord degrees per bar, lead / bass voices, drum feel
const MOODS = {
  title: { bpm: 128, root: 60, scale: 'major', prog: [0, 5, 3, 4], lead: 'square', bass: 'saw', drums: 'pop', swing: 0 },
  meadow: { bpm: 120, root: 62, scale: 'major', prog: [0, 3, 4, 0, 5, 3, 1, 4], lead: 'tri', bass: 'sine', drums: 'pop', swing: 0.15 },
  frost: { bpm: 104, root: 64, scale: 'pent', prog: [0, 3, 2, 4], lead: 'bell', bass: 'sine', drums: 'soft', swing: 0 },
  forge: { bpm: 138, root: 52, scale: 'phryg', prog: [0, 0, 1, 0, 5, 6, 1, 0], lead: 'saw', bass: 'square', drums: 'heavy', swing: 0 },
  temple: { bpm: 112, root: 57, scale: 'dorian', prog: [0, 6, 3, 4], lead: 'pluck', bass: 'sine', drums: 'tribal', swing: 0.1 },
  candy: { bpm: 140, root: 65, scale: 'major', prog: [0, 4, 5, 3], lead: 'square', bass: 'tri', drums: 'pop', swing: 0.2 },
  neon: { bpm: 124, root: 57, scale: 'minor', prog: [0, 5, 2, 6], lead: 'saw', bass: 'saw', drums: 'four', swing: 0 },
  pirate: { bpm: 132, root: 62, scale: 'minor', prog: [0, 6, 5, 4], lead: 'pluck', bass: 'tri', drums: 'jig', swing: 0.33 },
  desert: { bpm: 110, root: 59, scale: 'phryg', prog: [0, 1, 0, 6], lead: 'pluck', bass: 'sine', drums: 'tribal', swing: 0.1 },
};
function voice(kind, f, t) {
  switch (kind) {
    case 'square': return sqr(f, t) * 0.5 + Math.sin(TAU * f * t) * 0.3;
    case 'saw': return sawB(f * t, f / SR);
    case 'tri': return 2 * Math.abs(2 * ((f * t) % 1) - 1) - 1;
    case 'bell': return Math.sin(TAU * f * t + 2 * Math.sin(TAU * f * 3.5 * t) * Math.exp(-t * 3));
    case 'pluck': return Math.sin(TAU * f * t + 1.5 * Math.sin(TAU * f * 2 * t) * Math.exp(-t * 8));
    default: return Math.sin(TAU * f * t);
  }
}
function song(name, m) {
  const beat = 60 / m.bpm, step = beat / 4, bars = 16, steps = bars * 16;
  const len = steps * step;
  const a = buf(len), drums = buf(len), bass = buf(len);
  const sc = SCALES[m.scale];
  const deg = (d, oct = 0) => m.root + sc[((d % sc.length) + sc.length) % sc.length] + 12 * (Math.floor(d / sc.length) + oct);
  const r = rng(0x5eed ^ name.length * 7919);
  // a 2-bar melody motif repeated with variations
  const motif = [];
  for (let i = 0; i < 32; i++) motif.push(r() < 0.62 ? Math.floor(r() * 7) : -1);
  const place = (target, start, dur, fn) => {
    const s0 = Math.floor(start * SR), n = Math.floor(dur * SR);
    const fade = Math.min(Math.floor(0.004 * SR), n >> 1);
    for (let i = 0; i < n && s0 + i < target.length; i++) target[s0 + i] += fn(i / SR) * Math.min(1, (n - i) / fade);
  };
  for (let st = 0; st < steps; st++) {
    const bar = Math.floor(st / 16), inbar = st % 16, chord = m.prog[bar % m.prog.length];
    const sw = st % 2 ? m.swing * step : 0, t0 = st * step + sw;
    // bass on the beats (and pushes)
    if (inbar % 4 === 0 || (inbar % 8 === 6 && r() < 0.5)) {
      const f = midi(deg(chord, -2));
      place(bass, t0, beat * 0.9, (t) => voice(m.bass, f, t) * env(t, 0.005, 3) * 0.55);
    }
    // chord pad on each bar
    if (inbar === 0)
      for (const k of [0, 2, 4]) {
        const f = midi(deg(chord + k, 0));
        place(a, t0, beat * 4, (t) => Math.sin(TAU * f * t) * 0.07 * Math.min(1, t * 8) * Math.exp(-t * 0.4));
      }
    // the lead: the motif shifted by the chord, the last bars of each 8 thinner
    const note = motif[(st % 32)];
    if (note >= 0 && !(bar % 8 === 7 && inbar > 8) && st % 2 === 0) {
      const f = midi(deg(chord + note, 1));
      place(a, t0, step * 1.8, (t) => voice(m.lead, f, t) * env(t, 0.004, 7) * 0.16);
    }
    // drums
    const d = m.drums;
    const kick = d === 'four' ? inbar % 4 === 0 : d === 'heavy' ? inbar % 4 === 0 || inbar === 10 : d === 'jig' ? inbar % 6 === 0 : d === 'soft' ? inbar === 0 || inbar === 8 : inbar === 0 || inbar === 8 || (d === 'pop' && inbar === 11);
    const snare = d === 'tribal' ? inbar === 6 || inbar === 14 : d === 'soft' ? inbar === 12 : inbar === 4 || inbar === 12;
    const hat = d === 'soft' ? inbar % 4 === 2 : inbar % 2 === 0;
    if (kick) place(drums, t0, 0.3, (t) => Math.sin(TAU * 55 * (1 + 2 * Math.exp(-t * 35)) * t) * env(t, 0.002, 14) * 0.9);
    if (snare) place(drums, t0, 0.25, (t) => (noise() * 0.6 + Math.sin(TAU * 190 * t) * 0.3) * env(t, 0.001, 18) * (d === 'tribal' ? 0.5 : 0.7));
    if (hat) place(drums, t0, 0.06, (t) => noise() * env(t, 0.001, 70) * 0.18);
    if (d === 'tribal' && (inbar === 3 || inbar === 11)) place(drums, t0, 0.3, (t) => Math.sin(TAU * 140 * t) * env(t, 0.002, 12) * 0.5);
  }
  highpass(drums, 30);
  lowpass(bass, 900);
  for (let i = 0; i < a.length; i++) a[i] = a[i] + drums[i] * 0.8 + bass[i];
  // a soft echo on the whole mix
  const dl = Math.floor(beat * 0.75 * SR);
  for (let i = dl; i < a.length; i++) a[i] += a[i - dl] * 0.18;
  norm(lowpass(a, 9000), 0.7);
  const lf = Math.floor(0.005 * SR); /* the loop seam: both ends meet at 0 */
  for (let i = 0; i < lf; i++) { a[i] *= i / lf; a[a.length - 1 - i] *= i / lf; }
  const tmp = join(TMP, name + '.wav');
  wav(tmp, a);
  const res = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', tmp, '-c:a', 'libvorbis', '-q:a', '3', join(D, `music_${name}.ogg`)]);
  if (res.status !== 0) throw new Error('ffmpeg failed: ' + res.stderr);
  rmSync(tmp);
}
for (const [name, m] of Object.entries(MOODS)) song(name, m);
console.log('wrote sound effects and', Object.keys(MOODS).length, 'music loops to', D);
