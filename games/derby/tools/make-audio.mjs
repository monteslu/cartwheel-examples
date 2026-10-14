// Regenerates derby's audio, all synthesized here (no third-party content):
// crunches and thuds of sheet steel, scrapes, breaking glass, a wreck going
// up, horns, the countdown, the crowd; an engine loop (pitched live by the
// game) and a tyre squeal loop; and a grimy rock loop for the title,
// encoded to Ogg Vorbis with ffmpeg.
//
// Everything is band-limited (no raw square or sawtooth), DC-free, faded at
// its ends, and the loops have no seams (integer cycles, crossfaded), so
// nothing buzzes or clicks when the game restarts or re-pitches them.
//
//   node games/derby/tools/make-audio.mjs
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { lcg } from '../../../deps/cartwheel-engine/tools/lib/rng.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const D = join(HERE, '..', 'assets', 'audio');
const TMP = join(HERE, '..', '..', '..', 'out', 'derby-audio');
mkdirSync(D, { recursive: true });
mkdirSync(TMP, { recursive: true });
const SR = 48000;
const noise = lcg(0xde7b).noise; /* exact 32-bit maths (tools/lib/rng.mjs): a float LCG decays into a buzz */
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
/* a two-pole resonator: the ring of a struck panel */
function resonate(a, f, bw, gain = 1) {
  const r = Math.exp(-Math.PI * bw / SR), c = 2 * r * Math.cos(TAU * f / SR);
  let y1 = 0, y2 = 0;
  const out = new Float32Array(a.length);
  for (let i = 0; i < a.length; i++) { const y = a[i] + c * y1 - r * r * y2; y2 = y1; y1 = y; out[i] = y * (1 - r) * gain; }
  return out;
}
const mix = (...xs) => { const n = Math.max(...xs.map((x) => x.length)), o = new Float32Array(n); for (const x of xs) for (let i = 0; i < x.length; i++) o[i] += x[i]; return o; };
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
const save = (name, a, peak = 0.85) => wav(join(D, name + '.wav'), norm(edges(a), peak));
const env = (t, a, d) => (t < a ? t / a : Math.exp(-(t - a) * d));
function shaped(sec, fn) { const a = buf(sec); for (let i = 0; i < a.length; i++) a[i] = fn(i / SR, i); return a; }
/* a loop without a seam: crossfade the tail into the head */
function loopify(a, xsec) {
  const n = a.length, x = Math.floor(xsec * SR);
  for (let i = 0; i < x; i++) { const k = i / x; a[i] = a[i] * k + a[n - x + i] * (1 - k); }
  return a.slice(0, n - x);
}

// ── metal ──────────────────────────────────────────────────────────────

// a crunch: a noise hit through a handful of panel resonances, a low body
// thump under it, and a rattle of debris trailing off
function crunch(name, sec, size) {
  const hit = shaped(sec, (t) => noise() * env(t, 0.0015, 9 / size));
  const panels = [180, 310, 470, 690, 1150, 1730].map((f, i) => resonate(hit, f * (1.1 - size * 0.12) * (1 + 0.03 * i), 60 + 25 * i, 3));
  const body = shaped(sec, (t) => Math.sin(TAU * (48 + 30 * Math.exp(-t * 20)) * t) * env(t, 0.002, 7 / size) * 0.9);
  const rattle = shaped(sec, (t) => (t > 0.05 ? noise() * (noise() > 0.992 ? 1 : 0.08) * env(t - 0.05, 0.01, 4 / size) : 0));
  const a = mix(...panels, body, highpass(lowpass(rattle, 5000), 900));
  save(name, lowpass(highpass(a, 35), 7000));
}
crunch('crunch', 0.7, 0.8);
crunch('crunch_big', 1.3, 1.5);
// a thud: a dull push of tyre and bumper
save('thud', lowpass(shaped(0.35, (t) => (Math.sin(TAU * (70 + 40 * Math.exp(-t * 30)) * t) + noise() * 0.3) * env(t, 0.003, 14)), 900), 0.7);
// a scrape: grinding noise through a wobbling band, sparks of ring on top
{
  const a = shaped(0.6, (t) => noise() * (0.6 + 0.4 * Math.sin(TAU * 23 * t)) * Math.min(1, t * 30) * Math.min(1, (0.6 - t) * 6));
  const b = resonate(a, 2400, 500, 1.5), c = resonate(a, 3700, 700, 1);
  save('scrape', mix(highpass(lowpass(a, 3500), 600), b, c), 0.6);
}
// glass: bright tinkles scattering
save('glass', highpass(shaped(0.9, (t) => {
  let v = noise() * env(t, 0.001, 30) * 0.5;
  for (let k = 0; k < 9; k++) { const t0 = 0.02 + k * 0.07 + (k % 3) * 0.013; if (t > t0) v += Math.sin(TAU * (3200 + k * 410) * (t - t0)) * env(t - t0, 0.001, 25) * 0.35; }
  return v;
}), 1500), 0.6);
// a wreck going up: a deep whump, a crackle of fire after
{
  const whump = shaped(2.2, (t) => (Math.sin(TAU * (40 + 50 * Math.exp(-t * 6)) * t) * 0.8 + noise() * 0.6) * env(t, 0.004, 2.2));
  const crackle = shaped(2.2, (t) => (t > 0.25 ? noise() * (noise() > 0.96 ? 1 : 0.05) * env(t - 0.25, 0.05, 1.5) : 0));
  save('boom', mix(lowpass(whump, 700), highpass(crackle, 2000)), 0.95);
}

// ── horns: two old car horns (band-limited buzz: sine partials through a horn's formant) ──
function horn(name, f1, f2) {
  const a = shaped(0.7, (t) => {
    let v = 0;
    for (const f of [f1, f2]) for (let k = 1; k <= 8; k++) v += Math.sin(TAU * f * k * t) / (k * 1.2);
    return v * Math.min(1, t * 40) * Math.min(1, (0.7 - t) * 20);
  });
  save(name, mix(resonate(a, 650, 300, 2), resonate(a, 1800, 600, 1), lowpass(a, 2500)), 0.7);
}
horn('horn', 400, 500);
horn('horn2', 330, 415);

// ── the engine: a V8-ish burble that loops at 1 s ──────────────────────
// firing pulses at 40 Hz (an integer number per loop), each a damped low
// resonance with a little uneven lope; harmonics rolled off at 2.5 kHz. The
// game re-pitches this loop between 0.55x and 1.65x.
{
  const len = 1.0, a = buf(len), n = a.length, fire = 40;
  const r = lcg(1234);
  const lope = Array.from({ length: fire }, () => 0.75 + 0.5 * r());
  for (let p = 0; p < fire; p++) {
    const t0 = p / fire, s0 = Math.floor(t0 * SR), amp = lope[p];
    for (let i = 0; i < Math.floor(0.05 * SR); i++) {
      const t = i / SR, idx = (s0 + i) % n;
      a[idx] += amp * (Math.sin(TAU * 95 * t) * 0.7 + Math.sin(TAU * 190 * t) * 0.25 + noise() * 0.12) * Math.exp(-t * 70);
    }
  }
  /* a steady low rumble at the crank (integer cycles in the loop) */
  for (let i = 0; i < n; i++) { const t = i / SR; a[i] += Math.sin(TAU * 20 * t) * 0.25 + Math.sin(TAU * 80 * t) * 0.12; }
  const b = lowpass(highpass(a, 25), 2500);
  /* the loop already wraps (pulses and sines are periodic in 1 s): just remove DC */
  let m = 0;
  for (const v of b) m += v;
  m /= n;
  for (let i = 0; i < n; i++) b[i] -= m;
  wav(join(D, 'engine.wav'), norm(b, 0.8));
}
// tyre squeal: a narrow band of noise that wavers, looping without a seam
{
  const a = shaped(3, (t) => noise());
  const q = mix(resonate(a, 1150, 90, 6), resonate(a, 1720, 140, 3));
  for (let i = 0; i < q.length; i++) q[i] *= 0.75 + 0.25 * Math.sin(TAU * 3 * i / SR); /* 3 Hz: integer cycles in 3 s */
  wav(join(D, 'skid.wav'), norm(loopify(highpass(q, 400), 0.5), 0.55));
}

// ── the crowd ──────────────────────────────────────────────────────────
{
  const len = 8, a = buf(len), n = a.length;
  for (let i = 0; i < n; i++) { const t = i / SR; a[i] = noise() * (0.6 + 0.25 * Math.sin(TAU * t / 4) + 0.15 * Math.sin(TAU * t / 2 + 1)); }
  lowpass(a, 1100);
  highpass(a, 140);
  wav(join(D, 'crowd.wav'), norm(loopify(a, 1), 0.55));
}
save('cheer', lowpass(highpass(shaped(3.5, (t) => {
  const sw = Math.min(1, t * 2) * Math.min(1, (3.5 - t) * 1.2);
  const wh = (t > 0.3 && t < 1.2 ? Math.sin(TAU * (2500 + 500 * Math.sin(TAU * 4 * t)) * t) * 0.12 : 0) + (t > 1 && t < 1.6 ? Math.sin(TAU * 2900 * t) * 0.1 : 0);
  return noise() * sw + wh;
}), 300), 4500), 0.8);

// ── the countdown and the menus ───────────────────────────────────────
save('beep', shaped(0.25, (t) => (Math.sin(TAU * 660 * t) + 0.3 * Math.sin(TAU * 1320 * t)) * (t < 0.2 ? 1 : 0) * 0.6), 0.6);
// GO: an air horn (band-limited partials, a slight slide up)
{
  const a = shaped(1.0, (t) => {
    let v = 0;
    const f = 300 * (1 + 0.04 * Math.min(1, t * 6));
    for (let k = 1; k <= 10; k++) v += Math.sin(TAU * f * k * t) / k;
    return v * Math.min(1, t * 30) * Math.min(1, (1.0 - t) * 6);
  });
  save('go', mix(resonate(a, 900, 400, 2), lowpass(a, 3000)), 0.8);
}
save('move', shaped(0.07, (t) => (Math.sin(TAU * 400 * t) + noise() * 0.3) * env(t, 0.001, 70)), 0.5);
save('select', mix(resonate(shaped(0.3, (t) => noise() * env(t, 0.001, 40)), 520, 80, 4), shaped(0.3, (t) => Math.sin(TAU * 110 * t) * env(t, 0.002, 18))), 0.7);

// ── music: a grimy rock loop (drums, a distorted bass, power chords) ──
const midi = (nn) => 440 * Math.pow(2, (nn - 69) / 12);
function music() {
  const bpm = 118, beat = 60 / bpm, step = beat / 4, bars = 16, steps = bars * 16, len = steps * step;
  const a = buf(len), drums = buf(len), bass = buf(len), gtr = buf(len);
  const prog = [0, 0, 3, 5, 0, 0, 7, 5]; /* E E G A E E B A, in semitones above E */
  const root = 40; /* E2 */
  const place = (target, start, dur, fn) => {
    const s0 = Math.floor(start * SR), n = Math.floor(dur * SR);
    for (let i = 0; i < n && s0 + i < target.length; i++) target[s0 + i] += fn(i / SR);
  };
  const saw = (f, t, k = 10) => { let v = 0; for (let h = 1; h <= k; h++) v += Math.sin(TAU * f * h * t) / h; return v; };
  for (let st = 0; st < steps; st++) {
    const bar = Math.floor(st / 16), inbar = st % 16, chord = prog[bar % prog.length], t0 = st * step;
    /* bass: driving eighths */
    if (inbar % 2 === 0) {
      const f = midi(root + chord);
      place(bass, t0, step * 1.9, (t) => saw(f, t, 6) * env(t, 0.004, 5) * 0.5);
    }
    /* guitar: chugged power chords (root + fifth + octave), palm-muted between accents */
    if (inbar % 2 === 0) {
      const accent = inbar === 0 || inbar === 6 || inbar === 10;
      const f = midi(root + 12 + chord);
      place(gtr, t0, step * (accent ? 3.5 : 1.6), (t) => (saw(f, t, 8) + saw(f * 1.5, t, 6) * 0.8 + saw(f * 2, t, 4) * 0.5) * env(t, 0.003, accent ? 2.5 : 14) * 0.35);
    }
    /* drums: a plain rock beat */
    const kick = inbar === 0 || inbar === 8 || inbar === 10, snare = inbar === 4 || inbar === 12, hat = inbar % 2 === 0;
    if (kick) place(drums, t0, 0.3, (t) => Math.sin(TAU * 50 * (1 + 2.5 * Math.exp(-t * 35)) * t) * env(t, 0.002, 12) * 1.0);
    if (snare) place(drums, t0, 0.3, (t) => (noise() * 0.7 + Math.sin(TAU * 180 * t) * 0.35) * env(t, 0.001, 14) * 0.8);
    if (hat) place(drums, t0, 0.07, (t) => noise() * env(t, 0.001, 60) * (inbar % 4 === 0 ? 0.25 : 0.15));
    if (bar % 4 === 3 && inbar >= 12) place(drums, t0, 0.2, (t) => (noise() * 0.5 + Math.sin(TAU * (140 - inbar * 4) * t) * 0.5) * env(t, 0.001, 16) * 0.7);
  }
  /* the guitar through a soft clipper, then a cabinet's roll-off */
  for (let i = 0; i < gtr.length; i++) gtr[i] = Math.tanh(gtr[i] * 3) * 0.5;
  lowpass(gtr, 3200);
  highpass(gtr, 90);
  for (let i = 0; i < bass.length; i++) bass[i] = Math.tanh(bass[i] * 2) * 0.6;
  lowpass(bass, 700);
  highpass(drums, 30);
  for (let i = 0; i < a.length; i++) a[i] = drums[i] * 0.9 + bass[i] + gtr[i] * 0.8;
  norm(lowpass(a, 9000), 0.8);
  const tmp = join(TMP, 'music.wav');
  wav(tmp, a);
  const r = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', tmp, '-c:a', 'libvorbis', '-q:a', '4', join(D, 'music.ogg')]);
  if (r.status !== 0) throw new Error('ffmpeg failed: ' + r.stderr);
  rmSync(tmp);
}
music();
console.log('wrote derby audio to', D);
