// Regenerates rider's audio, all synthesized here (no third-party content):
// sound effects as WAV, a driving music loop encoded to Ogg Vorbis with
// ffmpeg. Everything is band-limited (sines, filtered noise, gentle
// saturation) with short fades at both ends: no clicks, no buzz.
//
//   node games/bikemesa/tools/make-audio.mjs
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { rng } from '../../../deps/cartwheel-engine/tools/lib/rng.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const D = join(HERE, '..', 'assets', 'audio');
const OUT = join(HERE, '..', '..', '..', 'out', 'bikemesa-audio');
mkdirSync(D, { recursive: true });
mkdirSync(OUT, { recursive: true });
const SR = 48000;
const noise = rng(0x51de).noise;

function wav(path, L, R) {
  const n = L.length, ch = R ? 2 : 1;
  const b = Buffer.alloc(44 + n * ch * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * ch * 2, 4); b.write('WAVE', 8); b.write('fmt ', 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(ch, 22); b.writeUInt32LE(SR, 24);
  b.writeUInt32LE(SR * ch * 2, 28); b.writeUInt16LE(ch * 2, 32); b.writeUInt16LE(16, 34); b.write('data', 36);
  b.writeUInt32LE(n * ch * 2, 40);
  for (let i = 0; i < n; i++)
    for (let c = 0; c < ch; c++) b.writeInt16LE(Math.round(Math.max(-1, Math.min(1, (c ? R : L)[i])) * 32767), 44 + (i * ch + c) * 2);
  writeFileSync(path, b);
}
const buf = (sec) => new Float32Array(Math.ceil(sec * SR));
const norm = (a, peak = 0.85) => { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); for (let i = 0; i < a.length; i++) a[i] *= peak / (m || 1); return a; };
const lowpass = (a, hz) => { const k = 1 - Math.exp(-2 * Math.PI * hz / SR); let y = 0; for (let i = 0; i < a.length; i++) { y += k * (a[i] - y); a[i] = y; } return a; };
const highpass = (a, hz) => { const k = 1 - Math.exp(-2 * Math.PI * hz / SR); let y = 0; for (let i = 0; i < a.length; i++) { y += k * (a[i] - y); a[i] -= y; } return a; };
// 5 ms fades: every sound starts and ends at zero
const fade = (a) => { const f = Math.min(240, a.length >> 2); for (let i = 0; i < f; i++) { a[i] *= i / f; a[a.length - 1 - i] *= i / f; } return a; };
const save = (name, a, peak) => wav(join(D, name + '.wav'), fade(norm(highpass(a, 25), peak)));

// pedal: a soft freewheel tick
{ const a = buf(0.08); for (let i = 0; i < a.length; i++) { const t = i / SR; a[i] = noise() * Math.exp(-t * 120) * 0.6 + Math.sin(2 * Math.PI * 2400 * t) * Math.exp(-t * 90) * 0.3; } save('pedal', lowpass(a, 6000), 0.5); }
// land: a tyre thump and gravel scatter
{ const a = buf(0.5); for (let i = 0; i < a.length; i++) { const t = i / SR; a[i] = Math.sin(2 * Math.PI * (70 - 30 * t) * t) * Math.exp(-t * 14) * 0.9 + noise() * Math.exp(-t * 9) * 0.35 * (0.6 + 0.4 * Math.sin(t * 300)); } save('land', lowpass(a, 3500)); }
// crash: a body thump, a metal clatter, gravel
{
  const a = buf(1.2);
  const parts = [523, 811, 1307, 1903, 2571];
  for (let i = 0; i < a.length; i++) {
    const t = i / SR;
    let v = Math.sin(2 * Math.PI * (55 - 20 * t) * t) * Math.exp(-t * 8) * 0.9 + noise() * Math.exp(-t * 4) * 0.4;
    for (let k = 0; k < parts.length; k++) v += Math.sin(2 * Math.PI * parts[k] * t + k) * Math.exp(-t * (6 + k * 2)) * 0.12 * (t > 0.05 * k ? 1 : 0);
    a[i] = v;
  }
  save('crash', lowpass(a, 5000));
}
{ const a = buf(0.3); for (let i = 0; i < a.length; i++) { const t = i / SR; a[i] = Math.sin(2 * Math.PI * (90 - 40 * t) * t) * Math.exp(-t * 18) + noise() * Math.exp(-t * 30) * 0.3; } save('thud', lowpass(a, 1800), 0.7); }
// chimes: sine partials with a soft attack
const chime = (name, notes, dur, spacing, peak = 0.8) => {
  const a = buf(dur);
  notes.forEach((f, n) => {
    const t0 = n * spacing;
    for (let i = Math.floor(t0 * SR); i < a.length; i++) {
      const t = i / SR - t0;
      const env = Math.min(1, t * 200) * Math.exp(-t * 4);
      a[i] += (Math.sin(2 * Math.PI * f * t) + 0.3 * Math.sin(2 * Math.PI * f * 2 * t) + 0.1 * Math.sin(2 * Math.PI * f * 3 * t)) * env * 0.4;
    }
  });
  save(name, a, peak);
};
chime('check', [784, 1175], 0.7, 0.09);
chime('finish', [523, 659, 784, 1047, 1319], 1.8, 0.12, 0.9);
chime('select', [880], 0.25, 0, 0.5);
chime('star', [1319, 1568, 2093], 0.8, 0.07);
chime('count', [660], 0.3, 0, 0.6);
chime('go', [990, 1320], 0.5, 0.0, 0.8);
{ const a = buf(0.35); for (let i = 0; i < a.length; i++) { const t = i / SR; a[i] = Math.sin(2 * Math.PI * (220 - 120 * t) * t) * Math.exp(-t * 10); } save('locked', lowpass(a, 2000), 0.6); }
// trick: a rising zing
{ const a = buf(0.6); for (let i = 0; i < a.length; i++) { const t = i / SR; const f = 600 + 1400 * t * t * 4; a[i] = Math.sin(2 * Math.PI * f * t) * Math.min(1, t * 60) * Math.exp(-t * 4) * 0.6 + Math.sin(2 * Math.PI * f * 1.5 * t) * Math.exp(-t * 6) * 0.25; } save('trick', lowpass(a, 7000), 0.75); }
// whoosh / boost: filtered noise swells
const swell = (name, dur, f0, f1, peak) => {
  const a = buf(dur);
  let y = 0, y2 = 0;
  for (let i = 0; i < a.length; i++) {
    const t = i / SR, u = t / dur, hz = f0 + (f1 - f0) * u;
    const k = 1 - Math.exp(-2 * Math.PI * hz / SR);
    y += k * (noise() - y); y2 += k * (y - y2);
    a[i] = (y - y2) * Math.sin(Math.PI * u) * 3;
  }
  save(name, a, peak);
};
swell('whoosh', 0.7, 300, 1800, 0.6);
// the slow-motion wipeout: a long low whoosh into a slow, deep crunch and a rattle
{
  const a = buf(2.6), nz = rng(0x0f0f).noise;
  let y = 0, y2 = 0;
  for (let i = 0; i < a.length; i++) {
    const t = i / SR, hz = 600 - 450 * Math.min(1, t / 0.8);
    const k = 1 - Math.exp(-2 * Math.PI * hz / SR);
    y += k * (nz() - y); y2 += k * (y - y2);
    let v = (y - y2) * 3 * Math.sin(Math.PI * Math.min(1, t / 0.8)) * (t < 0.8 ? 1 : 0);
    if (t > 0.75) { const u = t - 0.75; v += Math.sin(2 * Math.PI * (34 - 10 * u) * u) * Math.exp(-u * 2.2) * 0.9 + nz() * Math.exp(-u * 3) * 0.25; }
    if (t > 0.9) { const u = t - 0.9; v += Math.sin(2 * Math.PI * (300 + 40 * Math.sin(u * 9)) * u) * Math.exp(-u * 4) * 0.12; }
    a[i] = v;
  }
  save('slowcrash', lowpass(a, 2500), 0.85);
}
swell('boost', 0.9, 200, 3000, 0.75);

// ── music: one loop per track theme, 16 bars, from a few parameters ──────
//   bpm, chords (midi triads), octave shift of the pad / lead, the lead's
//   pattern density, drums (kick / snare / shaker on or off), a timbre bend
function music(name, o) {
  const beat = 60 / o.bpm, bars = 16, len = bars * 4 * beat;
  const L = buf(len), R = buf(len);
  const nz = rng(o.seed).noise;
  const mtof = (m) => 440 * Math.pow(2, (m - 69) / 12);
  const add = (arr, i, v) => { if (i >= 0 && i < arr.length) arr[i] += v; };
  for (let bar = 0; bar < bars; bar++) {
    const ch = o.chords[bar % o.chords.length], t0 = bar * 4 * beat;
    for (const m of ch) { // pad
      const f = mtof(m + 12 + o.padShift);
      for (let i = 0; i < 4 * beat * SR; i++) {
        const t = i / SR, env = Math.min(1, t * 3) * Math.min(1, (4 * beat - t) * 6);
        const v = (Math.sin(2 * Math.PI * f * t) + Math.sin(2 * Math.PI * f * (1 + o.detune) * t + 1)) * 0.035 * env;
        add(L, Math.floor((t0 + t) * SR), v);
        add(R, Math.floor((t0 + t) * SR), v * 0.9);
      }
    }
    const bassN = o.bassEighths ? 8 : 4;
    for (let e = 0; e < bassN; e++) { // bass
      const f = mtof(ch[0] - 12 + (e === bassN - 1 ? 7 : 0)), s0 = t0 + e * 4 * beat / bassN, d = 4 * beat / bassN;
      for (let i = 0; i < d * SR; i++) {
        const t = i / SR, env = Math.exp(-t * o.bassDecay) * Math.min(1, t * 400);
        const v = (Math.sin(2 * Math.PI * f * t) + 0.35 * Math.sin(2 * Math.PI * f * 2 * t) + 0.1 * Math.sin(2 * Math.PI * f * 3 * t)) * 0.22 * env;
        add(L, Math.floor((s0 + t) * SR), v);
        add(R, Math.floor((s0 + t) * SR), v);
      }
    }
    if (bar >= 4) for (let st = 0; st < 16; st++) { // lead arpeggio
      if ((st * 7 + bar * 3) % o.leadSkip === 1) continue;
      const m = ch[st % 3] + 24 + o.leadShift + (st >= 8 ? 12 : 0) * ((bar >> 2) & 1), f = mtof(m), s0 = t0 + st * beat / 4;
      for (let i = 0; i < beat / 4 * SR * o.leadLen; i++) {
        const t = i / SR, env = Math.exp(-t * o.leadDecay) * Math.min(1, t * 300);
        const v = (Math.sin(2 * Math.PI * f * t + o.bend * Math.sin(2 * Math.PI * 5 * t)) + 0.2 * Math.sin(2 * Math.PI * f * 2 * t)) * 0.05 * env;
        add(L, Math.floor((s0 + t) * SR), v * (st & 1 ? 0.6 : 1));
        add(R, Math.floor((s0 + t) * SR), v * (st & 1 ? 1 : 0.6));
      }
    }
    if (o.drums) for (let bt = 0; bt < 4; bt++) { // kick, snare
      const s0 = t0 + bt * beat;
      for (let i = 0; i < 0.25 * SR; i++) {
        const t = i / SR, v = Math.sin(2 * Math.PI * (110 * Math.exp(-t * 18) + 42) * t) * Math.exp(-t * 11) * 0.45 * o.drums;
        add(L, Math.floor((s0 + t) * SR), v);
        add(R, Math.floor((s0 + t) * SR), v);
      }
      if (bt & 1 && bar >= 2) {
        let y = 0;
        for (let i = 0; i < 0.2 * SR; i++) {
          const t = i / SR; y += 0.35 * (nz() - y);
          const v = ((nz() - y) * Math.exp(-t * 22) * 0.2 + Math.sin(2 * Math.PI * 190 * t) * Math.exp(-t * 30) * 0.15) * o.drums;
          add(L, Math.floor((s0 + t) * SR), v);
          add(R, Math.floor((s0 + t) * SR), v);
        }
      }
    }
    if (o.shaker) for (let st = 0; st < 16; st++) {
      const s0 = t0 + st * beat / 4;
      let y = 0;
      for (let i = 0; i < 0.05 * SR; i++) {
        const t = i / SR; y += 0.5 * (nz() - y);
        const v = (nz() - y) * Math.exp(-t * 90) * (st % 4 === 2 ? 0.07 : 0.035) * o.shaker;
        add(L, Math.floor((s0 + t) * SR), v * 0.8);
        add(R, Math.floor((s0 + t) * SR), v);
      }
    }
    if (o.bells) for (let st = 0; st < 4; st++) { // sleigh-bell jingle (filtered noise bursts) and a chime
      const s0 = t0 + st * beat + beat / 2;
      for (let i = 0; i < 0.12 * SR; i++) {
        const t = i / SR, v = (Math.sin(2 * Math.PI * 5200 * t) * nz()) * Math.exp(-t * 40) * 0.05;
        add(L, Math.floor((s0 + t) * SR), v);
        add(R, Math.floor((s0 + t) * SR), v);
      }
    }
  }
  for (const a of [L, R]) for (let i = 0; i < a.length; i++) a[i] = Math.tanh(a[i] * 1.3) / 1.3;
  norm(L, 0.8); norm(R, 0.8);
  const tmp = join(OUT, name + '.wav');
  wav(tmp, L, R);
  const r = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', tmp, '-c:a', 'libvorbis', '-q:a', '5', join(D, name + '.ogg')], { stdio: 'inherit' });
  if (r.status !== 0) throw new Error('ffmpeg failed');
  rmSync(tmp);
}
const base = { bpm: 128, padShift: 0, detune: 0.003, bassEighths: true, bassDecay: 7, leadShift: 0, leadSkip: 3, leadLen: 1.6, leadDecay: 14,
  bend: 0, drums: 1, shaker: 1, bells: false, seed: 0x51de };
// the original loop: A minor (Am F C G), driving
music('music', { ...base, chords: [[57, 60, 64], [53, 57, 60], [48, 52, 55], [55, 59, 62]] });
// scary: D minor with a diminished turn, slow and wobbly, sparse drums
music('music_scary', { ...base, bpm: 96, chords: [[50, 53, 57], [51, 54, 57], [46, 50, 53], [49, 52, 55]], detune: 0.012, bend: 0.6,
  leadSkip: 2, leadDecay: 8, bassEighths: false, drums: 0.6, shaker: 0.4, seed: 0x5ca7 });
// christmas: G major (G C D G), jingling bells
music('music_xmas', { ...base, bpm: 118, chords: [[55, 59, 62], [48, 52, 55], [50, 54, 57], [55, 59, 62]], bells: true, leadShift: 0,
  leadDecay: 10, shaker: 0.6, seed: 0x6ee7 });
// moon: E minor, spacious: slow pads, no drums, a soft lead
music('music_moon', { ...base, bpm: 84, chords: [[52, 55, 59], [48, 52, 55], [45, 48, 52], [47, 50, 54]], detune: 0.006, drums: 0, shaker: 0,
  bassEighths: false, bassDecay: 2, leadSkip: 2, leadLen: 3, leadDecay: 5, seed: 0x3007 });
// beach: C major (C Am F G), bright and bouncy
music('music_beach', { ...base, bpm: 124, chords: [[48, 52, 55], [57, 60, 64], [53, 57, 60], [55, 59, 62]], leadShift: 12, leadDecay: 18,
  shaker: 1.3, seed: 0xbeac });
// mars: F# minor, heavy and slow-driving
music('music_mars', { ...base, bpm: 110, chords: [[54, 57, 61], [50, 54, 57], [52, 56, 59], [49, 52, 56]], padShift: -12, bend: 0.25,
  leadDecay: 10, drums: 1.2, seed: 0x3a25 });
console.log('wrote BikeMesa audio to', D);
