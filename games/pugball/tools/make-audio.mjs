// Regenerates pugball's audio, all synthesized here (no third-party
// content): the sound effects as WAV (the cat's meows, yowls, hisses and
// purr from a little formant voice; barks; the crowd), and a music loop per
// stadium (plus the title's) from a small step sequencer, encoded to Ogg Vorbis with ffmpeg.
//
//   node games/pugball/tools/make-audio.mjs
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const D = join(HERE, '..', 'assets', 'audio');
const TMP = join(HERE, '..', '..', '..', 'out', 'pugball-audio');
mkdirSync(D, { recursive: true });
mkdirSync(TMP, { recursive: true });
const SR = 48000;
let seed = 0xb0b;
// exact 32-bit LCG (Math.imul): a plain multiply passes 2^53, loses its low
// bits and falls into a 419-sample cycle, a 114.6 Hz buzz in every "noise"
const noise = () => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return ((seed >>> 8) / 8388608) - 1; };
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
/* every effect: DC removed, a 3 ms fade in and an 8 ms fade out (no clicks) */
function edges(a) {
  let m = 0;
  for (const v of a) m += v;
  m /= a.length || 1;
  const fi = Math.floor(0.003 * SR), fo = Math.floor(0.008 * SR);
  for (let i = 0; i < a.length; i++) {
    a[i] -= m;
    if (i < fi) a[i] *= i / fi;
    if (i > a.length - fo) a[i] *= (a.length - i) / fo;
  }
  return a;
}
function sfx(name, sec, fn, post) {
  const a = buf(sec);
  for (let i = 0; i < a.length; i++) a[i] = fn(i / SR, i);
  wav(join(D, name + '.wav'), norm(edges(post ? post(a) : a), 0.85));
}
const env = (t, a, d) => (t < a ? t / a : Math.exp(-(t - a) * d));

// ── sound effects ──────────────────────────────────────────────────────

// a tiny formant voice: a glottal-ish pulse train through two resonances,
// pitch and vowel both moving over time (the cat's meows and yowls)
function voiceSynth(len, f0At, f1At, f2At, rough, ampAt) {
  const a = buf(len);
  let ph = 0, y1 = [0, 0], y2 = [0, 0];
  const res = (state, x, f, bw) => { /* a two-pole resonator */
    const r = Math.exp(-Math.PI * bw / SR), c = 2 * r * Math.cos(TAU * f / SR);
    const y = x + c * state[0] - r * r * state[1];
    state[1] = state[0];
    state[0] = y;
    return y * (1 - r);
  };
  for (let i = 0; i < a.length; i++) {
    const t = i / SR, k = t / len;
    const f0 = f0At(k) * (1 + rough * 0.03 * noise());
    ph += f0 / SR;
    if (ph >= 1) ph -= 1;
    const src = (ph < 0.35 ? Math.sin(Math.PI * ph / 0.35) : 0) + rough * 0.15 * noise();
    const v = res(y1, src, f1At(k), 120) * 1.0 + res(y2, src, f2At(k), 200) * 0.6;
    a[i] = v * ampAt(k);
  }
  return a;
}
const lerpK = (pts) => (k) => { /* piecewise-linear over 0..1 */
  for (let i = 1; i < pts.length; i++) if (k <= pts[i][0]) { const [k0, v0] = pts[i - 1], [k1, v1] = pts[i]; return v0 + (v1 - v0) * (k - k0) / (k1 - k0); }
  return pts[pts.length - 1][1];
};
const sw = (k, a, d) => (k < a ? k / a : Math.max(0, 1 - (k - a) / d));
function meow(name, len, pitch, rough, wrap = 0.85) {
  const a = voiceSynth(len, lerpK([[0, 480 * pitch], [0.35, 760 * pitch], [1, 430 * pitch]]), lerpK([[0, 350], [0.4, 900], [1, 600]]),
    lerpK([[0, 2400], [0.4, 1500], [1, 900]]), rough, (k) => sw(k, 0.12, wrap));
  wav(join(D, name + '.wav'), norm(edges(lowpass(highpass(a, 200), 6000)), 0.85));
}
meow('meow', 0.55, 1, 0.2);
// the yowl: long, wavering, indignant
{
  const len = 1.2;
  const a = voiceSynth(len, (k) => (420 + 260 * Math.sin(Math.PI * Math.min(1, k * 1.4))) * (1 + 0.06 * Math.sin(TAU * 7 * k * len)),
    lerpK([[0, 400], [0.3, 950], [0.8, 800], [1, 500]]), lerpK([[0, 2200], [0.3, 1400], [1, 900]]), 0.8, (k) => sw(k, 0.08, 0.95));
  wav(join(D, 'yowl.wav'), norm(edges(lowpass(highpass(a, 200), 6500)), 0.9));
}
// the hiss: breathy noise, bright, with a spit at the start
sfx('hiss', 0.9, (t) => noise() * (t < 0.03 ? 1.4 : 1) * env(t, 0.02, 2.5), (a) => highpass(lowpass(a, 9000), 2200));
// the purr: a soft breathy rumble, gently modulated by a sine (a sharp
// pulse train here rang like mains hum: harmonics of 26 Hz all the way up)
sfx('purr', 2.4, (t) => {
  const pulse = 0.65 + 0.35 * Math.sin(TAU * 23 * t);
  const breath = 0.55 + 0.45 * Math.sin(TAU * t / 1.2 - 1.2);
  return noise() * pulse * breath * Math.min(1, t * 4) * Math.min(1, (2.4 - t) * 4);
}, (a) => highpass(lowpass(lowpass(a, 260), 260), 70));
// a bark: a short rough "ruff"
function bark(name, pitch) {
  const a = voiceSynth(0.28, lerpK([[0, 340 * pitch], [0.3, 420 * pitch], [1, 250 * pitch]]), lerpK([[0, 500], [0.3, 800], [1, 450]]),
    lerpK([[0, 1300], [1, 1000]]), 1.4, (k) => sw(k, 0.05, 0.9));
  for (let i = 0; i < a.length; i++) a[i] += noise() * env(i / SR, 0.002, 30) * 0.4;
  wav(join(D, name + '.wav'), norm(edges(lowpass(highpass(a, 150), 5000)), 0.85));
}
bark('bark', 1);
// touches: a soft furry thump, and a big one
sfx('hit', 0.35, (t) => Math.sin(TAU * 110 * t * (1 - t)) * env(t, 0.002, 18) + noise() * env(t, 0.001, 45) * 0.5, (a) => lowpass(a, 2200));
sfx('hit_big', 0.6, (t) => Math.sin(TAU * 70 * (1 + 2 * Math.exp(-t * 25)) * t) * env(t, 0.002, 9) + noise() * env(t, 0.001, 25) * 0.7,
  (a) => lowpass(a, 3000));
// the cat bouncing: a squashy boing
sfx('boing', 0.5, (t) => Math.sin(TAU * (180 + 260 * Math.exp(-t * 9)) * t + 2 * Math.sin(TAU * 11 * t)) * env(t, 0.003, 6) * 0.8);
sfx('boost', 0.6, (t) => noise() * env(t, 0.03, 3), (a) => lowpass(highpass(a, 400), 3500));
sfx('pad', 0.3, (t) => (Math.sin(TAU * 880 * t) + Math.sin(TAU * 1320 * t) * 0.5) * env(t, 0.003, 14) * 0.6);
sfx('pad_big', 0.7, (t) => {
  const f = 440 * Math.pow(2, Math.min(1, t * 3));
  return (Math.sin(TAU * f * t) + 0.4 * Math.sin(TAU * f * 2 * t)) * env(t, 0.005, 4) * 0.6;
});
sfx('jump', 0.25, (t) => Math.sin(TAU * (300 + 500 * t) * t) * env(t, 0.004, 12) * 0.6 + noise() * env(t, 0.001, 50) * 0.2);
sfx('dodge', 0.35, (t) => noise() * env(t, 0.01, 10), (a) => highpass(lowpass(a, 5000), 900));
sfx('land', 0.3, (t) => Math.sin(TAU * 80 * t) * env(t, 0.002, 16) + noise() * env(t, 0.001, 40) * 0.3, (a) => lowpass(a, 900));
sfx('bump', 0.35, (t) => Math.sin(TAU * 140 * t * (1 - t)) * env(t, 0.002, 14) + noise() * env(t, 0.001, 35) * 0.6, (a) => lowpass(a, 2500));
// a demolition: a cartoon boom and a spring
for (const [name, len, f0] of [['demo', 1.4, 60]]) {
  const a = buf(len);
  let ph = 0;
  for (let i = 0; i < a.length; i++) {
    const t = i / SR, f = f0 * (1 + 2.5 * Math.exp(-t * 18));
    ph += TAU * f / SR;
    a[i] = Math.sin(ph) * env(t, 0.004, 5) * 1.1 + noise() * env(t, 0.002, 7) * 0.9 +
      Math.sin(TAU * (600 * Math.exp(-t * 3)) * t + 3 * Math.sin(TAU * 13 * t)) * (t > 0.15 ? env(t - 0.15, 0.01, 4) : 0) * 0.4;
  }
  wav(join(D, name + '.wav'), norm(edges(lowpass(a, 3000)), 0.95));
}
// the goal horn: a big round chord (sine partials, a slow vibrato; no raw sawtooth)
sfx('horn', 2.2, (t) => {
  let v = 0;
  const vib = 1 + 0.004 * Math.sin(TAU * 5 * t);
  for (const f of [220, 277, 330, 440]) for (let k = 1; k <= 4; k++) v += Math.sin(TAU * f * k * vib * t) * 0.18 / (k * k);
  return v * Math.min(1, t * 10) * Math.min(1, (2.2 - t) * 3);
}, (a) => lowpass(a, 3000));
// the crowd: a loop of murmur (filtered noise with slow swells; it loops without a seam)
{
  const len = 8, a = buf(len), n = a.length;
  for (let i = 0; i < n; i++) {
    const t = i / SR;
    a[i] = noise() * (0.6 + 0.25 * Math.sin(TAU * t / 4) + 0.15 * Math.sin(TAU * t / 2 + 1));
  }
  lowpass(a, 1100);
  highpass(a, 140);
  /* crossfade the last second into the first so it loops */
  const x = SR;
  for (let i = 0; i < x; i++) { const k = i / x; a[i] = a[i] * k + a[n - x + i] * (1 - k); }
  wav(join(D, 'crowd.wav'), norm(a.slice(0, n - x), 0.6));
}
// the cheer: a swell of voices, whistles on top
sfx('cheer', 3.5, (t) => {
  const sw2 = Math.min(1, t * 2) * Math.min(1, (3.5 - t) * 1.2);
  const wh = (t > 0.4 && t < 1.4 ? Math.sin(TAU * (2600 + 400 * Math.sin(TAU * 3 * t)) * t) * 0.12 : 0) +
    (t > 1.2 && t < 2 ? Math.sin(TAU * 3100 * t) * 0.1 : 0);
  return noise() * sw2 + wh;
}, (a) => lowpass(highpass(a, 300), 4500));
sfx('beep', 0.25, (t) => Math.sin(TAU * 880 * t) * (t < 0.18 ? 1 : 0) * 0.6);
sfx('go', 0.6, (t) => Math.sin(TAU * 1320 * t) * (t < 0.5 ? 1 : 0) * Math.min(1, (0.6 - t) * 10) * 0.7);
// the referee's whistle: a trilled pea whistle
sfx('whistle', 1.1, (t) => Math.sin(TAU * 2900 * t) * (0.6 + 0.4 * Math.sin(TAU * 32 * t)) * (t < 1 ? 1 : 0) * Math.min(1, t * 30) * 0.6);
sfx('move', 0.08, (t) => Math.sin(TAU * 1300 * t) * env(t, 0.002, 60) * 0.5);
sfx('select', 0.3, (t) => (Math.sin(TAU * 880 * t) + Math.sin(TAU * 1320 * t) * 0.6) * env(t, 0.003, 12) * 0.6);
sfx('locked', 0.3, (t) => (Math.sin(TAU * 180 * t) + 0.3 * Math.sin(TAU * 540 * t)) * env(t, 0.003, 14) * 0.5);
sfx('unlock', 0.9, (t) => {
  const notes = [523, 659, 784, 1047], k = Math.min(3, Math.floor(t / 0.09));
  const tt = t - k * 0.09;
  return (Math.sin(TAU * notes[k] * tt) + 0.4 * Math.sin(TAU * notes[k] * 3 * tt)) * env(tt, 0.004, 5) * 0.6;
});

// ── music: a step sequencer ───────────────────────────────────────────
const midi = (n) => 440 * Math.pow(2, (n - 69) / 12);
const SCALES = { major: [0, 2, 4, 5, 7, 9, 11], minor: [0, 2, 3, 5, 7, 8, 10], dorian: [0, 2, 3, 5, 7, 9, 10], pent: [0, 2, 4, 7, 9],
  mixo: [0, 2, 4, 5, 7, 9, 10], phryg: [0, 1, 3, 5, 7, 8, 10] };
// per mood: tempo, root (midi), scale, chord degrees per bar, lead / bass voices, drum feel
const MOODS = {
  title: { bpm: 132, root: 60, scale: 'major', prog: [0, 5, 3, 4], lead: 'square', bass: 'saw', drums: 'pop', swing: 0.1 },
  park: { bpm: 124, root: 62, scale: 'major', prog: [0, 3, 4, 0, 5, 3, 1, 4], lead: 'tri', bass: 'sine', drums: 'pop', swing: 0.15 },
  neon: { bpm: 126, root: 57, scale: 'minor', prog: [0, 5, 2, 6], lead: 'saw', bass: 'saw', drums: 'four', swing: 0 },
  beach: { bpm: 112, root: 64, scale: 'mixo', prog: [0, 3, 6, 4], lead: 'pluck', bass: 'tri', drums: 'tribal', swing: 0.2 },
  snow: { bpm: 104, root: 64, scale: 'pent', prog: [0, 3, 2, 4], lead: 'bell', bass: 'sine', drums: 'soft', swing: 0 },
  garden: { bpm: 116, root: 65, scale: 'major', prog: [0, 4, 5, 3], lead: 'bell', bass: 'tri', drums: 'pop', swing: 0.12 },
};
function voice(kind, f, t) {
  switch (kind) {
    case 'square': { let v = 0; for (let k = 1; k <= 7; k += 2) v += Math.sin(TAU * f * k * t) / k; return v * 0.7; } /* band-limited */
    case 'saw': { let v = 0; for (let k = 1; k <= 6; k++) v += Math.sin(TAU * f * k * t) / k; return v * 0.55; }
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
  let s2 = 0x5eed ^ name.length * 7919;
  const r = () => { s2 = (Math.imul(s2, 1103515245) + 12345) >>> 0; return (s2 >>> 8) / 16777216; };
  // a 2-bar melody motif repeated with variations
  const motif = [];
  for (let i = 0; i < 32; i++) motif.push(r() < 0.62 ? Math.floor(r() * 7) : -1);
  const place = (target, start, dur, fn) => {
    const s0 = Math.floor(start * SR), n = Math.floor(dur * SR);
    for (let i = 0; i < n && s0 + i < target.length; i++) target[s0 + i] += fn(i / SR);
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
  norm(lowpass(a, 9000), 0.8);
  const tmp = join(TMP, name + '.wav');
  wav(tmp, a);
  const res = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', tmp, '-c:a', 'libvorbis', '-q:a', '3', join(D, `music_${name}.ogg`)]);
  if (res.status !== 0) throw new Error('ffmpeg failed: ' + res.stderr);
  rmSync(tmp);
}
for (const [name, m] of Object.entries(MOODS)) song(name, m);
console.log('wrote sound effects and', Object.keys(MOODS).length, 'music loops to', D);
