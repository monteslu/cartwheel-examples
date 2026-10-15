// Generates chromite's sound effects and music (synthesized here: no recorded
// or third-party audio). Clean by construction, as spout's generator:
//   - oscillators are sums of sines (band-limited partials) on phase
//     accumulators, so pitch sweeps stay continuous;
//   - noise comes from tools/lib/rng.mjs (exact 32-bit), shaped by filters;
//   - every sound has a 2 ms attack and ends on a release to exactly 0;
//   - peaks are normalized, loops are crossfaded so they repeat without a seam.
// Effects are 16-bit mono WAV; the two music loops are Ogg Vorbis (ffmpeg).
//   node games/chromite/tools/make-audio.mjs
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { rng } from '../../../deps/cartwheel-engine/tools/lib/rng.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'audio');
const TMP = join(resolve(HERE, '../../..'), 'out', 'chromite-audio');
const RATE = 48000;
const TAU = Math.PI * 2;
mkdirSync(OUT, { recursive: true });
mkdirSync(TMP, { recursive: true });

function wav(path, data, channels = 1) {
  const n = data.length, buf = Buffer.alloc(44 + n * 2);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + n * 2, 4); buf.write('WAVE', 8);
  buf.write('fmt ', 12); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(channels, 22);
  buf.writeUInt32LE(RATE, 24); buf.writeUInt32LE(RATE * 2 * channels, 28); buf.writeUInt16LE(2 * channels, 32); buf.writeUInt16LE(16, 34);
  buf.write('data', 36); buf.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) buf.writeInt16LE(Math.max(-32767, Math.min(32767, Math.round(data[i] * 32767))), 44 + i * 2);
  writeFileSync(path, buf);
}
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
function lowpass(x, fc) { const k = 1 - Math.exp(-TAU * fc / RATE); let y = 0; for (let i = 0; i < x.length; i++) x[i] = y += k * (x[i] - y); return x; }
function highpass(x, fc) { const k = Math.exp(-TAU * fc / RATE); let px = 0, y = 0; for (let i = 0; i < x.length; i++) { y = k * (y + x[i] - px); px = x[i]; x[i] = y; } return x; }
function bandpass(x, lo, hi) { return lowpass(lowpass(highpass(x, lo), hi), hi); }
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
function at(x, secs) { const o = Math.round(secs * RATE), y = new Float64Array(o + x.length); y.set(x, o); return y; }
function gain(x, g) { return x.map((v) => v * g); }
function shaped(x, f) { return x.map((v, i) => v * f(i / RATE)); }
function drive(x, k) { return x.map((v) => Math.tanh(v * k)); }

const note = (m) => 440 * Math.pow(2, (m - 69) / 12);
const SAW = Array.from({ length: 14 }, (_, i) => [i + 1, 1 / (i + 1)]);
const SQR = Array.from({ length: 8 }, (_, i) => [2 * i + 1, 1 / (2 * i + 1)]);
const BELL = [[1, 1], [2.76, 0.35], [5.4, 0.15], [8.9, 0.05]];

// ── effects ──
// the default gun: a soft, short zap (the original had no gun sounds; this
// one sits low in the mix)
wav(join(OUT, 'shot.wav'), finish(mix(tone(0.07, (t) => 1800 - 9000 * t, [[1, 1], [2, 0.3]], (t) => env(t, 0.002, 0.018)),
  gain(bandpass(shaped(noise(0.05, 3), (t) => env(t, 0.001, 0.01)), 3000, 8000), 0.3)), 0.5));
// the green lance: a rising sizzle
wav(join(OUT, 'lance.wav'), finish(mix(tone(0.35, (t) => 400 + 1500 * t, SQR.slice(0, 4), (t) => env(t, 0.005, 0.12)),
  gain(bandpass(shaped(noise(0.35, 5), (t) => env(t, 0.005, 0.1)), 2000, 6000), 0.35)), 0.55, 30));
// the purple hammer: a thump and a buzz
wav(join(OUT, 'hammer.wav'), finish(mix(tone(0.18, (t) => 220 * Math.exp(-t * 9) + 60, [[1, 1], [2, 0.4]], (t) => env(t, 0.002, 0.05)),
  tone(0.12, (t) => 900 - 3000 * t, SAW.slice(0, 6), (t) => env(t, 0.002, 0.03))), 0.6, 15));
// explosions: band-passed noise with a falling body, three sizes
function boom(secs, seed, lo, hi, body, decay, bodyDecay) {
  const n = lowpass(bandpass(shaped(noise(secs, seed), (t) => env(t, 0.004, decay)), lo, hi), hi);
  const b = tone(secs, (t) => body * Math.exp(-t * 2.5) + 28, [[1, 1], [2, 0.3]], (t) => env(t, 0.003, bodyDecay));
  return drive(mix(n, gain(b, 0.8)), 1.6);
}
wav(join(OUT, 'pop.wav'), finish(boom(0.4, 11, 300, 3500, 160, 0.06, 0.05), 0.6, 30));
wav(join(OUT, 'boom.wav'), finish(boom(0.9, 13, 120, 2400, 110, 0.16, 0.14), 0.7, 60));
wav(join(OUT, 'big.wav'), finish(mix(boom(2.2, 17, 60, 1500, 80, 0.45, 0.5), at(boom(1.6, 19, 200, 3000, 140, 0.2, 0.15), 0.12)), 0.75, 300));
// a round hitting armour: a short metallic tick
wav(join(OUT, 'hit.wav'), finish(mix(tone(0.06, () => 2400, [[1, 1], [2.7, 0.5], [5.1, 0.25]], (t) => env(t, 0.001, 0.012)),
  gain(bandpass(shaped(noise(0.04, 23), (t) => env(t, 0.001, 0.008)), 2000, 7000), 0.4)), 0.45));
// the shields taking a hit: a shimmering zing; the hull: a crunch
wav(join(OUT, 'shield.wav'), finish(mix(...[1, 1.012, 0.993].map((d, i) => tone(0.4, (t) => (1300 + 700 * Math.exp(-t * 12)) * d, [[1, 1], [3, 0.2]], (t) => env(t, 0.002, 0.09) * (1 + 0.4 * Math.sin(TAU * 31 * t + i))))), 0.55, 30));
wav(join(OUT, 'hull.wav'), finish(drive(mix(bandpass(shaped(noise(0.35, 29), (t) => env(t, 0.002, 0.07)), 150, 2500),
  tone(0.3, (t) => 140 - 200 * t, [[1, 1], [2, 0.5], [3, 0.3]], (t) => env(t, 0.002, 0.06))), 2.5), 0.6, 30));
// power-up: a rising arpeggio of bells; an extra life: a fanfare
wav(join(OUT, 'pickup.wav'), finish(mix(...[72, 76, 79, 84].map((m, i) => at(tone(0.6, () => note(m), BELL, (t) => env(t, 0.002, 0.15)), i * 0.05))), 0.6, 40));
wav(join(OUT, 'life.wav'), finish(mix(...[67, 72, 76, 79, 84, 88].map((m, i) => at(tone(0.9, () => note(m), SQR.slice(0, 4), (t) => env(t, 0.004, 0.25)), i * 0.08))), 0.6, 80));
// an enemy got past: a falling two-tone alarm
wav(join(OUT, 'lose.wav'), finish(mix(tone(0.35, () => note(64), SQR.slice(0, 5), (t) => env(t, 0.004, 0.2)),
  at(tone(0.6, (t) => note(57) * (1 - 0.15 * t), SQR.slice(0, 5), (t) => env(t, 0.004, 0.25)), 0.3)), 0.6, 60));
// the super bomb: a whoosh up into a deep boom
wav(join(OUT, 'bomb.wav'), finish(mix(gain(bandpass(shaped(noise(2.5, 31), (t) => (t < 0.4 ? t / 0.4 : Math.exp(-(t - 0.4) * 1.6))), 200, 4000), 0.7),
  tone(2.5, (t) => 90 * Math.exp(-t * 0.8) + 25, [[1, 1], [2, 0.4], [3, 0.15]], (t) => env(t, 0.3, 0.8)),
  at(boom(1.8, 37, 50, 1200, 70, 0.4, 0.5), 0.35)), 0.75, 300));
wav(join(OUT, 'select.wav'), finish(tone(0.08, () => 1400, [[1, 1], [2, 0.2]], (t) => env(t, 0.002, 0.02)), 0.4));
wav(join(OUT, 'armed.wav'), finish(mix(tone(0.12, () => 880, SQR.slice(0, 4), (t) => env(t, 0.002, 0.06)), at(tone(0.12, () => 660, SQR.slice(0, 4), (t) => env(t, 0.002, 0.06)), 0.14)), 0.5, 20));
wav(join(OUT, 'alarm.wav'), finish(tone(0.8, (t) => 600 + 300 * Math.sin(TAU * 3 * t), SQR.slice(0, 5), (t) => env(t, 0.01, 0.5)), 0.5, 60));

// ── music ──
function loop(render, secs, fadeSecs) {
  const n = Math.round(secs * RATE), f = Math.round(fadeSecs * RATE), x = render(secs + fadeSecs), y = new Float64Array(n);
  for (let i = 0; i < n; i++) y[i] = x[i];
  for (let i = 0; i < f; i++) { const a = i / f; y[i] = x[i] * a + x[n + i] * (1 - a); }
  return y;
}
function ogg(name, data) {
  const w = join(TMP, name + '.wav');
  wav(w, data);
  const r = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', w, '-c:a', 'libvorbis', '-q:a', '5', join(OUT, name + '.ogg')]);
  if (r.status !== 0) throw new Error('ffmpeg: ' + r.stderr);
}
function place(out, x, t0, g) { const o = Math.round(t0 * RATE); for (let i = 0; i < x.length && o + i < out.length; i++) out[o + i] += x[i] * g; }
// a simple resonant-ish lowpass sweep per note: a saw through a one-pole that opens and closes
function synthNote(m, secs, cutoff, decay, partials = SAW) {
  const x = tone(secs, () => note(m), partials, (t) => env(t, 0.004, decay));
  return lowpass(x, cutoff);
}
const kick = tone(0.45, (t) => 45 + 110 * Math.exp(-t * 28), [[1, 1]], (t) => env(t, 0.001, 0.14));
const snare = mix(gain(bandpass(shaped(noise(0.3, 41), (t) => env(t, 0.001, 0.07)), 900, 7000), 0.8), tone(0.2, (t) => 190 - 60 * t, [[1, 1]], (t) => env(t, 0.001, 0.05)));
const hat = gain(highpass(shaped(noise(0.08, 43), (t) => env(t, 0.001, 0.015)), 7000), 0.5);
const ohat = gain(highpass(shaped(noise(0.3, 47), (t) => env(t, 0.001, 0.08)), 6000), 0.4);

// the game: a driving minor groove at 132 bpm (A minor), 16 bars
{
  const bpm = 132, beat = 60 / bpm, bars = 16, len = beat * 4 * bars;
  const prog = [[57, 'm'], [53, 'M'], [55, 'M'], [52, 'm']]; // Am F G Em, 4 bars each
  ogg('music_game', loop((s) => {
    const out = new Float64Array(Math.round(s * RATE)), r = rng(1601);
    for (let bar = 0; bar * 4 * beat < s; bar++) {
      const [root, q] = prog[Math.floor(bar / 4) % 4], third = q === 'm' ? 3 : 4, t0 = bar * 4 * beat;
      for (let b = 0; b < 4; b++) {
        place(out, kick, t0 + b * beat, 0.9);
        if (b % 2 === 1) place(out, snare, t0 + b * beat, 0.45);
        for (let e = 0; e < 2; e++) place(out, e ? ohat : hat, t0 + (b + e * 0.5) * beat, e ? 0.18 : 0.25);
      }
      // the bass: a sixteenth-note ostinato on the root, octave jumps
      for (let k = 0; k < 16; k++) {
        const m = root - 24 + (k % 4 === 2 ? 12 : 0) + (k === 14 ? 7 : 0);
        place(out, synthNote(m, beat * 0.3, 900 + 500 * (k % 4 === 0), 0.08), t0 + k * beat / 4, 0.3);
      }
      // the pad: the chord, held, soft
      for (const iv of [0, third, 7, 12]) place(out, synthNote(root + iv, beat * 4, 1400, 1.6, SAW.slice(0, 8)), t0, 0.045);
      // the lead: an arpeggio in the second half of each 4-bar phrase
      if (bar % 4 >= 2) {
        const notes = [0, third, 7, 12, 7, third, 12, 15].map((v) => root + 12 + v);
        for (let k = 0; k < 8; k++) if (r.u32() % 6) place(out, synthNote(notes[k], beat * 0.45, 3200, 0.16, SQR.slice(0, 6)), t0 + k * beat / 2, 0.09);
      }
    }
    return finish(out, 0.6, 1);
  }, len, 0.3));
}
// the title: a slow chrome-and-glass pad over a low pulse (D minor), 8 bars at 84
{
  const bpm = 84, beat = 60 / bpm, bars = 8, len = beat * 4 * bars;
  const prog = [62, 58, 60, 57];
  ogg('music_title', loop((s) => {
    const out = new Float64Array(Math.round(s * RATE));
    for (let bar = 0; bar * 4 * beat < s; bar++) {
      const root = prog[Math.floor(bar / 2) % 4], t0 = bar * 4 * beat;
      for (const iv of [0, 3, 7, 10, 14]) place(out, synthNote(root + iv - 12, beat * 8, 1100, 3, SAW.slice(0, 10)), t0, bar % 2 ? 0 : 0.04);
      for (let b = 0; b < 4; b++) place(out, tone(beat * 0.9, () => note(root - 24), [[1, 1], [2, 0.3]], (t) => env(t, 0.01, 0.35)), t0 + b * beat, 0.25);
      for (let k = 0; k < 8; k++) {
        const m = root + 12 + [0, 7, 3, 10, 7, 14, 10, 3][k];
        place(out, tone(1.2, () => note(m), BELL, (t) => env(t, 0.002, 0.4)), t0 + k * beat / 2, 0.05);
      }
    }
    return finish(out, 0.55, 1);
  }, len, 0.5));
}
rmSync(TMP, { recursive: true, force: true });
console.log('audio written to', OUT);
