// Regenerates otium's audio, all synthesized here (no third-party content):
// the sound effects as WAV, and one calm music loop (a plucked lyre over a
// soft drone in D dorian, with a frame drum) encoded to Ogg Vorbis with ffmpeg.
//
//   node games/otium/tools/make-audio.mjs
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { rng } from '../../../deps/cartwheel-engine/tools/lib/rng.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const D = join(HERE, '..', 'assets', 'audio');
const TMP = join(HERE, '..', '..', '..', 'out', 'otium-audio');
mkdirSync(D, { recursive: true });
mkdirSync(TMP, { recursive: true });
const SR = 48000;
const TAU = 2 * Math.PI;
const noise = rng(0x07105).noise;

function wav(path, a, channels = 1) {
  const n = a.length;
  const b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVE', 8); b.write('fmt ', 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(channels, 22); b.writeUInt32LE(SR, 24);
  b.writeUInt32LE(SR * 2 * channels, 28); b.writeUInt16LE(2 * channels, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(Math.max(-1, Math.min(1, a[i])) * 32767), 44 + i * 2);
  writeFileSync(path, b);
}
const buf = (sec) => new Float32Array(Math.ceil(sec * SR));
const norm = (a, peak) => { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); for (let i = 0; i < a.length; i++) a[i] *= peak / (m || 1); return a; };
const lowpass = (a, hz) => { const k = 1 - Math.exp(-TAU * hz / SR); let y = 0; for (let i = 0; i < a.length; i++) { y += k * (a[i] - y); a[i] = y; } return a; };
const highpass = (a, hz) => { const k = 1 - Math.exp(-TAU * hz / SR); let y = 0; for (let i = 0; i < a.length; i++) { y += k * (a[i] - y); a[i] -= y; } return a; };
function finish(a, peak = 0.7) {
  highpass(a, 25);
  const att = Math.floor(0.002 * SR), rel = Math.min(Math.floor(0.015 * SR), a.length >> 1);
  for (let i = 0; i < att && i < a.length; i++) a[i] *= i / att;
  for (let i = 0; i < rel; i++) a[a.length - 1 - i] *= i / rel;
  return norm(a, peak);
}
const env = (t, a, d) => (t < a ? t / a : Math.exp(-(t - a) * d));
const mtof = (m) => 440 * Math.pow(2, (m - 69) / 12);

/* a plucked string (Karplus-Strong), the lyre's voice */
function pluck(freq, sec, bright = 0.5, decay = 0.996) {
  const a = buf(sec), N = Math.max(2, Math.round(SR / freq)), line = new Float32Array(N);
  for (let i = 0; i < N; i++) line[i] = noise() * 2 - 1;
  let lp = 0; /* soften the excitation */
  for (let i = 0; i < N; i++) { lp += (line[i] - lp) * (0.2 + bright * 0.6); line[i] = lp; }
  let idx = 0;
  for (let i = 0; i < a.length; i++) {
    const nxt = (idx + 1) % N;
    const v = line[idx];
    line[idx] = decay * (0.5 * (v + line[nxt]));
    a[i] = v;
    idx = nxt;
  }
  return a;
}
function mixInto(dst, src, at, gain) {
  const o = Math.floor(at * SR);
  for (let i = 0; i < src.length && o + i < dst.length; i++) dst[o + i] += src[i] * gain;
}
function sfx(name, sec, fn, post) {
  const a = buf(sec);
  for (let i = 0; i < a.length; i++) a[i] = fn(i / SR, i);
  wav(join(D, name + '.wav'), finish(post ? post(a) : a));
}

// ── effects ────────────────────────────────────────────────────────────
/* hop: a dry wooden tick */
sfx('hop', 0.06, (t) => Math.sin(TAU * 1800 * t) * env(t, 0.001, 90) + (noise() - 0.5) * env(t, 0.0005, 300) * 0.5, (a) => lowpass(a, 5000));
/* open: two lyre notes upward */
{ const a = buf(0.5); mixInto(a, pluck(mtof(69), 0.5, 0.6), 0, 0.8); mixInto(a, pluck(mtof(74), 0.45, 0.6), 0.06, 0.8); wav(join(D, 'open.wav'), finish(a, 0.55)); }
/* ok: a bright pluck and its fifth */
{ const a = buf(0.6); mixInto(a, pluck(mtof(74), 0.6, 0.7), 0, 0.8); mixInto(a, pluck(mtof(81), 0.55, 0.7), 0.0, 0.5); wav(join(D, 'ok.wav'), finish(a, 0.6)); }
/* deny: a dull low thud on a muted string */
{ const a = pluck(mtof(45), 0.3, 0.1, 0.98); for (let i = 0; i < a.length; i++) a[i] += Math.sin(TAU * 90 * i / SR) * env(i / SR, 0.002, 25) * 0.6; wav(join(D, 'deny.wav'), finish(lowpass(a, 1200), 0.6)); }
/* build: knocks of a mallet on timber, then a chime */
{
  const a = buf(0.9);
  for (let k = 0; k < 3; k++) {
    const o = k * 0.11;
    for (let i = 0; i < 0.12 * SR; i++) { const t = i / SR; a[Math.floor(o * SR) + i] += (Math.sin(TAU * (220 - k * 20) * t) * 0.8 + (noise() - 0.5) * 0.6) * env(t, 0.001, 40); }
  }
  mixInto(a, pluck(mtof(86), 0.5, 0.8), 0.36, 0.5);
  wav(join(D, 'build.wav'), finish(lowpass(a, 6000), 0.7));
}
/* harvest: a swish of grain, a rustle */
sfx('harvest', 0.7, (t) => (noise() - 0.5) * env(t, 0.12, 6) * (0.6 + 0.4 * Math.sin(TAU * 7 * t)), (a) => highpass(lowpass(a, 3500), 600));
/* coins: little metal clinks */
{
  const a = buf(0.6);
  const hits = [0, 0.05, 0.13, 0.2, 0.31];
  for (const h of hits) for (let i = 0; i < 0.25 * SR; i++) { const t = i / SR, f = 3200 + noise() * 1400; a[Math.floor(h * SR) + i] += (Math.sin(TAU * f * t) * 0.4 + Math.sin(TAU * f * 1.51 * t) * 0.3) * env(t, 0.0005, 30); }
  wav(join(D, 'coins.wav'), finish(a, 0.5));
}
/* recruit: a short horn call (cornu) */
sfx('recruit', 0.8, (t) => {
  const f = t < 0.25 ? mtof(57) : mtof(64);
  let s = 0; for (let h = 1; h <= 6; h++) s += Math.sin(TAU * f * h * t) / (h * 1.2);
  return s * Math.min(1, t * 30) * (t < 0.25 ? 1 : Math.exp(-(t - 0.25) * 4)) * (t > 0.23 && t < 0.27 ? 0.3 : 1);
}, (a) => lowpass(a, 2500));
/* temple: a struck bowl */
sfx('temple', 2.0, (t) => (Math.sin(TAU * 523 * t) + 0.6 * Math.sin(TAU * 523 * 2.76 * t) * Math.exp(-t * 2) + 0.3 * Math.sin(TAU * 523 * 5.4 * t) * Math.exp(-t * 5)) * env(t, 0.002, 1.6));
/* rest: a soft open chord on the lyre */
{ const a = buf(1.6); [62, 69, 74, 77].forEach((m, i) => mixInto(a, pluck(mtof(m), 1.5, 0.35, 0.998), i * 0.07, 0.5)); wav(join(D, 'rest.wav'), finish(lowpass(a, 4000), 0.55)); }
/* season: a low bell */
sfx('season', 3.0, (t) => (Math.sin(TAU * 196 * t) + 0.5 * Math.sin(TAU * 196 * 2.4 * t) * Math.exp(-t) + 0.35 * Math.sin(TAU * 196 * 3.9 * t) * Math.exp(-t * 2)) * env(t, 0.003, 1.1));
/* end: a rising lyre run and a bell */
{
  const a = buf(3.2);
  [62, 64, 65, 67, 69, 71, 72, 74].forEach((m, i) => mixInto(a, pluck(mtof(m), 1.6, 0.6, 0.998), i * 0.09, 0.6));
  [62, 69, 74, 78].forEach((m) => mixInto(a, pluck(mtof(m), 2.4, 0.5, 0.999), 0.85, 0.5));
  wav(join(D, 'end.wav'), finish(a, 0.7));
}
/* arrive: a muffled cart bump */
sfx('arrive', 0.2, (t) => (Math.sin(TAU * 140 * t) + (noise() - 0.5) * 0.4) * env(t, 0.002, 30), (a) => lowpass(a, 1500));

// ── music: 32 bars of 4/4 at 72 bpm (106.7 s), seamless ────────────────
{
  const BPM = 72, beat = 60 / BPM, bars = 32, sec = bars * 4 * beat;
  const L = buf(sec), R = buf(sec);
  /* D dorian: the chords' roots, two bars each */
  const prog = [[50, 57, 62, 65], [48, 55, 60, 64], [46, 53, 58, 62], [48, 55, 60, 67], [50, 57, 62, 69], [53, 60, 65, 69], [55, 62, 67, 71], [57, 64, 69, 72]];
  const scale = [62, 64, 65, 67, 69, 71, 72, 74, 76, 77];
  const r = rng(0x1a7e);
  for (let bar = 0; bar < bars; bar++) {
    const ch = prog[Math.floor(bar / 2) % prog.length];
    const t0 = bar * 4 * beat;
    /* the drone: the chord's root and fifth, a slow swell */
    for (let i = 0; i < 4 * beat * SR; i++) {
      const t = i / SR, tt = t0 + t;
      const sw = 0.5 - 0.5 * Math.cos(TAU * (i / (4 * beat * SR)));
      const f1 = mtof(ch[0] - 12), f2 = mtof(ch[1] - 12);
      const v = (Math.sin(TAU * f1 * tt) + 0.6 * Math.sin(TAU * f2 * tt) + 0.25 * Math.sin(TAU * f1 * 2 * tt + 0.3)) * (0.06 + 0.03 * sw);
      const k = Math.floor(t0 * SR) + i;
      if (k < L.length) { L[k] += v; R[k] += v * 0.95; }
    }
    /* the lyre: arpeggios of the chord, a gentle melody on top every other bar */
    for (let s = 0; s < 8; s++) {
      const n = ch[[0, 1, 2, 3, 2, 1, 2, 3][s]];
      const p = pluck(mtof(n), 2.0, 0.45, 0.9975);
      const at = t0 + s * beat / 2 + (s % 2 ? 0.012 : 0);
      const g = s % 2 ? 0.16 : 0.22, pan = (s % 4) / 3;
      mixInto(L, p, at, g * (1 - pan * 0.4));
      mixInto(R, p, at, g * (0.6 + pan * 0.4));
    }
    if (bar % 2 === 1 || bar >= 16) {
      for (let s = 0; s < 4; s++) {
        if (r.noise() < 0.3) continue;
        const n = scale[Math.floor(r.noise() * scale.length)];
        const p = pluck(mtof(n + 12), 2.4, 0.7, 0.998);
        mixInto(L, p, t0 + s * beat, 0.16);
        mixInto(R, p, t0 + s * beat + 0.006, 0.18);
      }
    }
    /* a frame drum on 1 and the and of 3, after the first eight bars */
    if (bar >= 8) for (const b of [0, 2.5]) {
      const at = t0 + b * beat;
      for (let i = 0; i < 0.3 * SR; i++) {
        const t = i / SR, k = Math.floor(at * SR) + i;
        const v = (Math.sin(TAU * (95 - 30 * t) * t) * 0.6 + (noise() - 0.5) * 0.15) * env(t, 0.002, 14) * 0.35;
        if (k < L.length) { L[k] += v; R[k] += v; }
      }
    }
  }
  /* wrap the tails past the end onto the start, so the loop is seamless */
  lowpass(L, 7000), lowpass(R, 7000);
  norm(L, 0.8), norm(R, 0.8);
  const st = new Float32Array(L.length * 2);
  for (let i = 0; i < L.length; i++) { st[i * 2] = L[i]; st[i * 2 + 1] = R[i]; }
  const tmp = join(TMP, 'music.wav');
  wav(tmp, st, 2);
  const res = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', tmp, '-c:a', 'libvorbis', '-q:a', '4', join(D, 'music.ogg')]);
  if (res.status !== 0) throw new Error('ffmpeg failed: ' + res.stderr);
  rmSync(tmp);
}
console.log('wrote', D);
