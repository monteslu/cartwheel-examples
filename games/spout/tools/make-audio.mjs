// Generates spout's sound effects and music (synthesized here: no recorded or
// third-party audio). Clean by construction:
//   - oscillators are sines summed into band-limited partials (no naive
//     square or saw), with phase accumulators so pitch glides are continuous;
//   - noise comes from tools/lib/rng.mjs (exact 32-bit), shaped by filters;
//   - every sound has a 2 ms attack and ends on a release to exactly 0;
//   - peaks are normalized to 0.7, loops are crossfaded so they repeat
//     without a seam.
// Effects are 16-bit mono WAV; the music and the rain bed are Ogg Vorbis (ffmpeg).
//   node games/spout/tools/make-audio.mjs
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { rng } from '../../../deps/cartwheel-engine/tools/lib/rng.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'audio');
const TMP = join(resolve(HERE, '../../..'), 'out', 'spout-audio');
const RATE = 48000;
const TAU = Math.PI * 2;
mkdirSync(OUT, { recursive: true });
mkdirSync(TMP, { recursive: true });

function wav(path, data, rate = RATE) {
  const n = data.length, buf = Buffer.alloc(44 + n * 2);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + n * 2, 4); buf.write('WAVE', 8);
  buf.write('fmt ', 12); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(1, 22);
  buf.writeUInt32LE(rate, 24); buf.writeUInt32LE(rate * 2, 28); buf.writeUInt16LE(2, 32); buf.writeUInt16LE(16, 34);
  buf.write('data', 36); buf.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) buf.writeInt16LE(Math.max(-32767, Math.min(32767, Math.round(data[i] * 32767))), 44 + i * 2);
  writeFileSync(path, buf);
}
// peak to `peak`, DC removed, a 2 ms fade in and a fade to 0 at the end
function finish(x, peak = 0.7, tailMs = 12) {
  let mean = 0;
  for (const v of x) mean += v;
  mean /= x.length;
  let m = 0;
  for (let i = 0; i < x.length; i++) { x[i] -= mean; m = Math.max(m, Math.abs(x[i])); }
  const g = m > 0 ? peak / m : 1, a = Math.round(RATE * 0.002), r = Math.round(RATE * tailMs / 1000);
  for (let i = 0; i < x.length; i++) {
    let e = 1;
    if (i < a) e = i / a;
    if (i > x.length - r) e = Math.min(e, (x.length - i) / r);
    x[i] *= g * e;
  }
  return x;
}
const env = (t, a, d) => (t < a ? t / a : Math.exp(-(t - a) / d));
// one-pole low and high pass, in place
function lowpass(x, fc) { const k = 1 - Math.exp(-TAU * fc / RATE); let y = 0; for (let i = 0; i < x.length; i++) x[i] = y += k * (x[i] - y); return x; }
function highpass(x, fc) { const k = Math.exp(-TAU * fc / RATE); let px = 0, y = 0; for (let i = 0; i < x.length; i++) { y = k * (y + x[i] - px); px = x[i]; x[i] = y; } return x; }
function bandpass(x, lo, hi) { return lowpass(lowpass(highpass(x, lo), hi), hi); } // two poles on top: soft, not hissy
// a voice: additive partials over a phase accumulator (band-limited, glides stay continuous)
function tone(secs, freqAt, partials, ampAt) {
  const n = Math.round(secs * RATE), x = new Float64Array(n);
  let ph = 0;
  for (let i = 0; i < n; i++) {
    const t = i / RATE, f = freqAt(t);
    ph += f / RATE;
    let s = 0;
    for (const [mult, amp] of partials) if (f * mult < RATE * 0.45) s += amp * Math.sin(TAU * ph * mult);
    x[i] = s * ampAt(t);
  }
  return x;
}
function noise(secs, seed) { const r = rng(seed), n = Math.round(secs * RATE), x = new Float64Array(n); for (let i = 0; i < n; i++) x[i] = r.noise(); return x; }
function mix(...parts) { const n = Math.max(...parts.map((p) => p.length)), x = new Float64Array(n); for (const p of parts) for (let i = 0; i < p.length; i++) x[i] += p[i]; return x; }
function at(x, secs, len) { const o = Math.round(secs * RATE), y = new Float64Array(Math.max(len, o + x.length)); y.set(x, o); return y; }

const MARIMBA = [[1, 1], [4, 0.25], [10, 0.06]];
const BELL = [[1, 1], [2.76, 0.35], [5.4, 0.15], [8.9, 0.05]];
const pluck = (f, secs = 0.6, d = 0.18) => tone(secs, () => f, MARIMBA, (t) => env(t, 0.003, d));
const note = (m) => 440 * Math.pow(2, (m - 69) / 12);

// ── effects ──
wav(join(OUT, 'jump.wav'), finish(tone(0.28, (t) => 340 + 520 * t, [[1, 1], [2, 0.2]], (t) => env(t, 0.004, 0.08)), 0.6));
wav(join(OUT, 'land.wav'), finish(bandpass(noise(0.12, 7).map((v, i) => v * env(i / RATE, 0.002, 0.025)), 700, 2400), 0.5));
wav(join(OUT, 'splash.wav'), finish(mix(
  bandpass(noise(0.9, 11).map((v, i) => v * env(i / RATE, 0.01, 0.22)), 400, 2800),
  tone(0.5, (t) => 520 - 300 * t, [[1, 1]], (t) => env(t, 0.005, 0.12)).map((v) => v * 0.4)), 0.7, 40));
wav(join(OUT, 'check.wav'), finish(mix(pluck(note(72)), at(pluck(note(79), 0.8, 0.25), 0.12, 0)), 0.65, 30));
wav(join(OUT, 'dew.wav'), finish(tone(0.6, () => note(91), BELL, (t) => env(t, 0.002, 0.14)), 0.5, 30));
wav(join(OUT, 'win.wav'), finish(mix(...[72, 76, 79, 84, 88].map((m, i) => at(pluck(note(m), 0.9, 0.3), i * 0.11, 0))), 0.7, 60));
wav(join(OUT, 'select.wav'), finish(bandpass(noise(0.06, 3).map((v, i) => v * env(i / RATE, 0.001, 0.012)), 1200, 3800), 0.45));
wav(join(OUT, 'locked.wav'), finish(tone(0.25, () => 110, [[1, 1], [2, 0.3], [3, 0.1]], (t) => env(t, 0.003, 0.06)), 0.55));
wav(join(OUT, 'thunder.wav'), finish(lowpass(noise(2.4, 19).map((v, i) => v * env(i / RATE, 0.25, 0.7)), 180), 0.7, 300));
wav(join(OUT, 'chime.wav'), finish(mix(...[84, 88, 91, 96].map((m, i) => at(tone(1.2, () => note(m), BELL, (t) => env(t, 0.002, 0.35)), i * 0.09, 0))), 0.6, 80));
wav(join(OUT, 'hold.wav'), finish(mix(bandpass(noise(0.1, 23).map((v, i) => v * env(i / RATE, 0.001, 0.02)), 300, 2000),
  tone(0.15, () => 200, [[1, 1]], (t) => env(t, 0.002, 0.04))), 0.5));

// silk: a rising zip; the bird: two chirps; its peck: three wooden taps
wav(join(OUT, 'zip.wav'), finish(mix(bandpass(noise(0.45, 41).map((v, i) => v * env(i / RATE, 0.01, 0.15)), 1200, 3000).map((v) => v * 0.6),
  tone(0.45, (t) => 300 + 900 * t, [[1, 1], [2, 0.15]], (t) => env(t, 0.005, 0.15))), 0.55, 30));
wav(join(OUT, 'tweet.wav'), finish(mix(tone(0.12, (t) => 2600 + 1800 * t * 8, [[1, 1]], (t) => env(t, 0.003, 0.04)),
  at(tone(0.14, (t) => 3000 - 1200 * t * 7, [[1, 1]], (t) => env(t, 0.003, 0.05)), 0.15, 0)), 0.45, 15));
wav(join(OUT, 'peck.wav'), finish(mix(...[0, 0.07, 0.14].map((d) => at(tone(0.06, () => 900, [[1, 1], [2.3, 0.4]], (t) => env(t, 0.001, 0.012)), d, 0))), 0.6, 10));

// ── loops: render one cycle plus a tail, crossfade the tail into the head ──
function loop(render, secs, fadeSecs) {
  const n = Math.round(secs * RATE), f = Math.round(fadeSecs * RATE), x = render(secs + fadeSecs), y = new Float64Array(n);
  for (let i = 0; i < n; i++) y[i] = x[i];
  for (let i = 0; i < f; i++) { const a = i / f; y[i] = x[i] * a + x[n + i] * (1 - a); }
  return y;
}
function ogg(name, data) {
  const w = join(TMP, name + '.wav');
  wav(w, data);
  const r = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', w, '-c:a', 'libvorbis', '-q:a', '4', join(OUT, name + '.ogg')]);
  if (r.status !== 0) throw new Error('ffmpeg: ' + r.stderr);
}
// the rain bed: soft pitter-patter (band-passed noise with drops) over a hush
ogg('rain', loop((s) => {
  const hush = bandpass(noise(s, 31), 400, 5000).map((v) => v * 0.35);
  const r = rng(32), drops = new Float64Array(hush.length);
  for (let i = 0; i < hush.length; i += 40 + (r.u32() % 900)) {
    const len = 600, f = 1800 + (r.u32() % 2400);
    for (let k = 0; k < len && i + k < drops.length; k++) drops[i + k] += Math.sin(TAU * f * k / RATE) * Math.exp(-k / 90) * 0.3;
  }
  return finish(mix(hush, drops), 0.6, 1);
}, 6, 0.5));
// the music, one loop per spout: the same marimba-and-bass band in a
// different key, tempo, mode and voice each
const SONGS = {
  spring: { bpm: 112, root: 60, mode: [0, 2, 4, 5, 7, 9, 11], prog: [0, 3, 4, 0, 5, 3, 4, 4], voice: MARIMBA, seed: 1 },
  city: { bpm: 92, root: 57, mode: [0, 2, 3, 5, 7, 8, 10], prog: [0, 5, 3, 4, 0, 5, 6, 4], voice: [[1, 1], [2, 0.35], [3, 0.12]], seed: 2 },
  autumn: { bpm: 100, root: 62, mode: [0, 2, 4, 5, 7, 9, 10], prog: [0, 3, 6, 0, 4, 3, 0, 4], voice: [[1, 1], [3, 0.2], [5, 0.06]], seed: 3 },
  winter: { bpm: 84, root: 64, mode: [0, 2, 4, 7, 9], prog: [0, 3, 1, 4, 0, 3, 1, 4], voice: BELL, seed: 4 },
  jungle: { bpm: 124, root: 55, mode: [0, 2, 3, 5, 7, 9, 10], prog: [0, 0, 3, 4, 0, 0, 6, 4], voice: [[1, 1], [2.5, 0.25], [4, 0.1]], seed: 5 },
};
for (const [name, S] of Object.entries(SONGS)) {
  const beat = 60 / S.bpm, bars = 8;
  ogg('music_' + name, loop((s) => {
    const len = Math.round(s * RATE), out = new Float64Array(len), r = rng(S.seed * 7919);
    const deg = (d, oct = 0) => S.root + S.mode[((d % S.mode.length) + S.mode.length) % S.mode.length] + 12 * (oct + Math.floor(d / S.mode.length));
    const add = (x, t0, gain) => { const o = Math.round(t0 * RATE); for (let i = 0; i < x.length && o + i < len; i++) out[o + i] += x[i] * gain; };
    const pl = (m, secs, d) => tone(secs, () => note(m), S.voice, (t) => env(t, 0.003, d));
    for (let bar = 0; bar * 4 * beat < s; bar++) {
      const root = S.prog[bar % bars];
      add(tone(beat * 3.8, () => note(deg(root, -2)), [[1, 1], [2, 0.25]], (t) => env(t, 0.02, 0.9)), bar * 4 * beat, 0.35);
      for (let b = 0; b < 4; b++) for (const k of [0, 2, 4]) add(pl(deg(root + k), 0.5, 0.12), (bar * 4 + b + 0.5) * beat, 0.06);
      // a tune: a stepwise walk over the chord, rests now and then
      let d = root + 4;
      for (let e = 0; e < 8; e++) {
        const step = (r.u32() % 5) - 2;
        d = Math.max(root, Math.min(root + 9, d + step));
        if (r.u32() % 5) add(pl(deg(d, 1), 0.7, 0.22), (bar * 4 + e * 0.5) * beat, 0.26);
      }
    }
    return finish(out, 0.6, 1);
  }, beat * 4 * bars, 0.4));
}
rmSync(TMP, { recursive: true, force: true });
console.log('audio written to', OUT);
