// The band: drum and instrument synthesisers and a small song sequencer, all
// offline and deterministic. make-music.mjs writes the songs with it.
import { SR, TAU, buf, shaped, env, clamp, mtof, lp, hp, bp, resonate, circ, svfRun, softclip, oscillator, addInto, rng, reverb, pingpong, chorus, mixArrays, removeDC, peakOf, rmsOf, db } from './audiolib.mjs';

const NR = rng(0xba5e);
const noise = () => NR.noise();

/* ── drums ──────────────────────────────────────────────────────────── */
function sweepSine(len, f0, f1, k, dec) {
  let ph = 0;
  return shaped(len, (t) => { ph += TAU * (f1 + (f0 - f1) * Math.exp(-t * k)) / SR; return Math.sin(ph) * Math.exp(-t * dec); });
}
const nbuf = (len) => { const a = buf(len); for (let i = 0; i < a.length; i++) a[i] = noise(); return a; };
const metal = (len, dec, lo = 3000) => {
  /* six inharmonic squares: the hush of a cymbal */
  const fs = [205.3, 304.4, 369.6, 522.7, 540, 800].map((f) => f * (lo / 3000 + 0.5));
  const os = fs.map((f) => oscillator('square', f * 3));
  return shaped(len, (t, i) => { let v = 0; for (const o of os) v += o(i); return v * Math.exp(-t * dec) * 0.25; });
};
export const DRUM = {
  kick: (v = 1, o = {}) => {
    const { f0 = 150, f1 = 46, k = 32, dec = 12, len = 0.4, click = 0.25 } = o;
    const a = sweepSine(len, f0, f1, k, dec);
    for (let i = 0; i < 160; i++) a[i] += noise() * click * (1 - i / 160);
    return softclip(a.map((x) => x * v * 1.2), 1.2);
  },
  /* a huge soft drum: taiko, a dam's timpani */
  deep: (v = 1, o = {}) => {
    const { f0 = 120, f1 = 52, dec = 5.5, len = 0.9 } = o;
    const a = sweepSine(len, f0, f1, 18, dec), n = nbuf(len);
    lp(n, 700);
    for (let i = 0; i < a.length; i++) a[i] += n[i] * Math.exp(-i / SR * 30) * 0.5;
    return a.map((x) => x * v);
  },
  snare: (v = 1, o = {}) => {
    const { tone = 190, dec = 22, ndec = 18, len = 0.3, bright = 1, body = 0.6 } = o;
    const n = nbuf(len); hp(n, 1200 * bright); lp(n, 9000);
    const t1 = sweepSine(len, tone * 1.5, tone, 60, dec);
    return shaped(len, (t, i) => (n[i] * Math.exp(-t * ndec) * 0.9 + t1[i] * body) * v);
  },
  clap: (v = 1) => {
    const n = nbuf(0.35); bp(n, 1500, 0.9);
    return shaped(0.35, (t, i) => { let e = 0; for (const t0 of [0, 0.011, 0.023]) if (t >= t0) e += Math.exp(-(t - t0) * 300) * 0.5; e += (t >= 0.035 ? Math.exp(-(t - 0.035) * 22) : 0); return n[i] * e * v * 1.4; });
  },
  rim: (v = 1) => shaped(0.08, (t) => (Math.sin(TAU * 1700 * t) * 0.6 + noise() * 0.5) * Math.exp(-t * 80) * v),
  hat: (v = 1, open = false) => { const len = open ? 0.35 : 0.06, n = nbuf(len); hp(n, 7000); const m = metal(len, open ? 9 : 70); return shaped(len, (t, i) => (n[i] * 0.5 + m[i]) * Math.exp(-t * (open ? 8 : 55)) * v * 0.7); },
  tom: (v = 1, f = 120) => {
    const a = sweepSine(0.45, f * 1.6, f, 20, 8), n = nbuf(0.45); lp(n, 1500);
    return shaped(0.45, (t, i) => (a[i] + n[i] * Math.exp(-t * 90) * 0.4) * v);
  },
  crash: (v = 1) => { const n = nbuf(2.2); hp(n, 4500); const m = metal(2.2, 2.2); return shaped(2.2, (t, i) => (n[i] * 0.6 + m[i]) * Math.exp(-t * 2.4) * Math.min(1, t * 500) * v * 0.55); },
  ride: (v = 1) => { const m = metal(0.7, 7, 4000), n = nbuf(0.7); hp(n, 6500); return shaped(0.7, (t, i) => (m[i] + n[i] * 0.1) * v * Math.exp(-t * 3)); },
  cowbell: (v = 1) => { const o1 = oscillator('square', 562), o2 = oscillator('square', 845); const a = shaped(0.3, (t, i) => (o1(i) + o2(i)) * Math.exp(-t * 14)); return bp(a, 700, 1.5).map((x) => x * v * 1.4); },
  shaker: (v = 1) => { const n = nbuf(0.07); bp(n, 7000, 1.2); return shaped(0.07, (t, i) => n[i] * Math.sin(Math.PI * Math.min(1, t / 0.07)) * v * 0.9); },
  block: (v = 1, f = 1700) => shaped(0.08, (t) => Math.sin(TAU * f * t) * Math.exp(-t * 60) * v),
  /* a skin drum: conga, djembe. f low, hi, or a slap */
  skin: (v = 1, f = 200, slap = false) => {
    const a = sweepSine(0.4, f * 1.25, f, 40, slap ? 18 : 9), n = nbuf(0.4); bp(n, slap ? 3000 : 900, 1);
    return shaped(0.4, (t, i) => (a[i] + n[i] * Math.exp(-t * (slap ? 60 : 120)) * (slap ? 1.2 : 0.5)) * v);
  },
  /* industrial: a struck sheet of steel */
  clank: (v = 1, f = 480) => { const n = nbuf(1.2); const h = shaped(1.2, (t, i) => n[i] * Math.exp(-t * 200)); return mixArrays(resonate(h, f, 40, 4), resonate(h, f * 2.43, 60, 3), resonate(h, f * 4.1, 90, 2), resonate(h, f * 1.37, 50, 3)).map((x) => x * v * 1.3); },
  noise_up: (len = 2, v = 1) => { const n = nbuf(len); svfRun(n, (i) => 300 + 9000 * (i / SR / len) ** 2, 1.5, 'bp'); return shaped(len, (t, i) => n[i] * (t / len) ** 1.5 * v * 0.3); },
};

/* ── pitched instruments: (freq, durSec, vel) -> mono buffer ───────────── */
/* the general subtractive voice */
export function synth(sp) {
  const { osc = [['saw', 0, 1]], a = 0.01, d = 0.2, s = 0.7, r = 0.12, cut = 2500, envAmt = 0, envDec = 0.2, q = 0.8, vib = [0, 0, 0], sub = 0, noiseAmt = 0, drive = 0, mode = 'lp', gliss = 0 } = sp;
  return (f, dur, vel = 1) => {
    const n = Math.ceil((dur + r) * SR), out = new Float32Array(n);
    const os = osc.map(([kind, cents, lvl, pw], k) => ({ o: oscillator(kind, (i) => {
      const t = i / SR;
      let ff = f * Math.pow(2, cents / 1200);
      if (gliss) ff *= Math.pow(2, gliss * Math.exp(-t * 18));
      if (vib[1]) ff *= 1 + vib[1] * Math.sin(TAU * vib[0] * t) * Math.min(1, Math.max(0, (t - vib[2]) * 6));
      return ff;
    }, pw ?? 0.5, ((k * 0.37) % 1)), lvl }));
    const so = sub ? oscillator('sin', f / 2) : null;
    for (let i = 0; i < n; i++) {
      const t = i / SR;
      let v = 0;
      for (const x of os) v += x.o(i) * x.lvl;
      if (so) v += so(i) * sub;
      if (noiseAmt) v += noise() * noiseAmt;
      let e = t < a ? t / a : s + (1 - s) * Math.exp(-(t - a) / Math.max(d, 1e-3) * 3);
      if (t > dur) e *= Math.max(0, 1 - (t - dur) / r) ** 2;
      out[i] = v * e;
    }
    svfRun(out, (i) => cut * (1 + envAmt * Math.exp(-(i / SR) / envDec)), q, mode);
    if (drive) softclip(out, drive);
    return out.map((x) => x * vel * 0.5);
  };
}
/* a plucked string (Karplus-Strong with a fractional delay) */
export function pluck({ bright = 0.55, damp = 0.997, ring = 0.4, pick = 1 } = {}) {
  return (f, dur, vel = 1) => {
    const n = Math.ceil((dur + ring) * SR), out = new Float32Array(n), Dd = SR / f - 0.5, Nn = Math.ceil(Dd) + 2;
    let y = 0;
    for (let i = 0; i < Nn && i < n; i++) { y += bright * (noise() * pick - y); out[i] = y; }
    for (let i = Nn; i < n; i++) {
      const p = i - Dd, i0 = Math.floor(p), fr = p - i0;
      const s0 = out[i0] * (1 - fr) + out[i0 + 1] * fr, s1 = out[i0 - 1] * (1 - fr) + out[i0] * fr;
      out[i] = damp * 0.5 * (s0 + s1);
    }
    for (let i = 0; i < n; i++) { const t = i / SR; out[i] *= t > dur ? Math.max(0, 1 - (t - dur) / ring) : 1; }
    return out.map((x) => x * vel * 1.4);
  };
}
/* marimba and friends: a struck bar */
export function mallet({ ratios = [1, 3.99, 9.2], amps = [1, 0.35, 0.12], dec = 5, knock = 0.3, bell = 0 } = {}) {
  return (f, dur, vel = 1) => {
    const len = Math.min(2, 0.15 + 1.4 / (1 + f / 300) + (bell ? 0.8 : 0)), n = Math.ceil(len * SR), out = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      const t = i / SR;
      let v = 0;
      for (let k = 0; k < ratios.length; k++) v += Math.sin(TAU * f * ratios[k] * t) * amps[k] * Math.exp(-t * dec * (1 + k * (bell ? 0.4 : 1.8)) * (0.6 + f / 900));
      out[i] = v * Math.min(1, t * 800);
    }
    for (let i = 0; i < 120 && i < n; i++) out[i] += noise() * knock * (1 - i / 120);
    return out.map((x) => x * vel * 0.6);
  };
}
/* a fairground steam organ: reedy, wobbly, never quite in tune */
export function calliope({ r = 0.05 } = {}) {
  return (f, dur, vel = 1) => {
    const n = Math.ceil((dur + r) * SR), out = new Float32Array(n), cents = (noise() * 14), ff = f * Math.pow(2, cents / 1200);
    const o1 = oscillator('square', (i) => ff * (1 + 0.006 * Math.sin(TAU * 5.6 * i / SR)), 0.35), o2 = oscillator('saw', (i) => ff * 2.004 * (1 + 0.006 * Math.sin(TAU * 5.6 * i / SR)));
    for (let i = 0; i < n; i++) {
      const t = i / SR, chiff = t < 0.03 ? noise() * (1 - t / 0.03) * 0.5 : 0;
      let e = Math.min(1, t / 0.012) * (t > dur ? Math.max(0, 1 - (t - dur) / r) : 1);
      out[i] = ((o1(i) * 0.7 + o2(i) * 0.3) + chiff + noise() * 0.04) * e;
    }
    svfRun(out, () => 4200, 0.9);
    return out.map((x) => x * vel * 0.4);
  };
}
/* a drawbar organ for chord stabs */
export function organ({ bars = [1, 0.8, 0.6, 0.45, 0.2, 0.3], r = 0.04 } = {}) {
  const hs = [1, 2, 3, 4, 6, 8];
  return (f, dur, vel = 1) => {
    const n = Math.ceil((dur + r) * SR), out = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      const t = i / SR;
      let v = 0;
      for (let k = 0; k < hs.length; k++) v += Math.sin(TAU * f * hs[k] * t) * bars[k];
      v *= 1 + 0.08 * Math.sin(TAU * 6 * t);
      out[i] = v * Math.min(1, t / 0.006) * (t > dur ? Math.max(0, 1 - (t - dur) / r) : 1);
    }
    return out.map((x) => x * vel * 0.2);
  };
}
/* a breathy flute or whistle */
export function flute({ breath = 0.12, vibRate = 5.2, vibDepth = 0.008 } = {}) {
  return (f, dur, vel = 1) => {
    const r = 0.1, n = Math.ceil((dur + r) * SR), out = new Float32Array(n), nz = new Float32Array(n);
    for (let i = 0; i < n; i++) nz[i] = noise();
    bp(nz, f * 2, 2);
    let ph = 0;
    for (let i = 0; i < n; i++) {
      const t = i / SR;
      ph += TAU * f * (1 + vibDepth * Math.sin(TAU * vibRate * t) * Math.min(1, t * 4)) / SR;
      const e = Math.min(1, t / 0.05) * (t > dur ? Math.max(0, 1 - (t - dur) / r) : 1);
      out[i] = (Math.sin(ph) + 0.18 * Math.sin(2 * ph) + 0.05 * Math.sin(3 * ph) + nz[i] * breath * 4) * e;
    }
    return out.map((x) => x * vel * 0.5);
  };
}

/* ── the sequencer ──────────────────────────────────────────────────── */
export const SCALES = {
  major: [0, 2, 4, 5, 7, 9, 11], minor: [0, 2, 3, 5, 7, 8, 10], dorian: [0, 2, 3, 5, 7, 9, 10], phrygian: [0, 1, 3, 5, 7, 8, 10],
  mixo: [0, 2, 4, 5, 7, 9, 10], harmonic: [0, 2, 3, 5, 7, 8, 11], phrygdom: [0, 1, 4, 5, 7, 8, 10], pent: [0, 3, 5, 7, 10, 12, 15], lydian: [0, 2, 4, 6, 7, 9, 11],
};
export const CHORD = { M: [0, 4, 7], m: [0, 3, 7], 7: [0, 4, 7, 10], m7: [0, 3, 7, 10], M7: [0, 4, 7, 11], m9: [0, 3, 7, 10, 14], sus: [0, 5, 7], dim: [0, 3, 6], 5: [0, 7, 12], M9: [0, 4, 7, 11, 14], 6: [0, 4, 7, 9] };
export const chordTones = (root, q) => CHORD[q].map((x) => root + x);
export const degree = (deg, scale, root) => { const n = scale.length, k = deg - 1, o = Math.floor(k / n); return root + scale[((k % n) + n) % n] + 12 * o; };

export class Song {
  constructor({ bpm, bars, swing = 0, tail = 4, seed = 1 }) {
    this.bpm = bpm; this.bars = bars; this.swing = swing;
    this.spb = 60 / bpm; this.step = this.spb / 4; this.barSec = this.spb * 4;
    this.N = Math.round(bars * this.barSec * SR);
    this.ext = this.N + Math.round(tail * SR);
    this.bus = {}; this.kicks = [];
    this.rnd = rng(seed);
  }
  addBus(name, { gain = 1, pan = 0, rev = 0, dly = 0, duck = 0, chorusOn = false, width = 0 } = {}) {
    this.bus[name] = { gain, pan, rev, dly, duck, chorusOn, width, L: new Float32Array(this.ext), R: new Float32Array(this.ext) };
    return this;
  }
  /* seconds of a bar and a 16th step (odd steps are pushed by the swing) */
  t(bar, step = 0) { return (bar * 16 + step) * this.step + ((step & 1) ? this.swing * this.step : 0); }
  put(name, arr, t, vel = 1, panOff = 0) {
    const b = this.bus[name];
    if (!b) throw new Error('no bus ' + name);
    const at = Math.round(t * SR);
    const p = clamp(b.pan + panOff, -1, 1), gl = Math.cos((p + 1) * Math.PI / 4), gr = Math.sin((p + 1) * Math.PI / 4);
    addInto(b.L, arr, at, vel * gl); addInto(b.R, arr, at, vel * gr);
  }
  /* one note: inst(freq, dur, vel) at bar/step for len steps */
  n(name, inst, bar, step, len, midi, vel = 1, panOff = 0) {
    this.put(name, inst(mtof(midi), len * this.step, vel), this.t(bar, step), 1, panOff);
  }
  drum(name, kind, bar, step, vel = 1, ...args) {
    const a = DRUM[kind](vel, ...args);
    this.put(name, a, this.t(bar, step));
    if (kind === 'kick' || kind === 'deep') this.kicks.push(this.t(bar, step));
  }
  /* rows of 16 characters: x full, X accent, o soft, . rest */
  grid(name, bar, rows, kit = {}) {
    for (const [kind, pat] of Object.entries(rows)) {
      for (let i = 0; i < pat.length; i++) {
        const c = pat[i];
        if (c === '.' || c === ' ') continue;
        const v = c === 'X' ? 1.2 : c === 'x' ? 0.9 : 0.5;
        const k = kit[kind] || [kind];
        this.drum(name, k[0], bar, i, v, ...k.slice(1));
      }
    }
  }
  /* a melody: tokens per 16th: '.' rest, '-' hold, degree digit with ' (octave up) or , (down); `opts.trem` re-picks long notes */
  line(name, inst, bar0, str, scale, root, { vel = 1, trem = 0, panOff = 0, minLen = 1, octave = 0 } = {}) {
    const toks = str.trim().split(/\s+/), notes = [];
    if (toks.length % 16) throw new Error('melody is ' + toks.length + ' steps, not whole bars: ' + str.slice(0, 40));
    toks.forEach((tk, i) => {
      if (tk === '.') return;
      if (tk === '-') { if (notes.length) notes[notes.length - 1].len++; return; }
      const m = /^(\d)(['\,]*)$/.exec(tk);
      if (!m) throw new Error('bad token ' + tk);
      let midi = degree(+m[1], scale, root) + 12 * octave;
      for (const c of m[2]) midi += c === "'" ? 12 : -12;
      notes.push({ i, len: 1, midi });
    });
    for (const nt of notes) {
      const bar = bar0 + Math.floor(nt.i / 16), st = nt.i % 16;
      if (trem && nt.len > 1) { const reps = nt.len * trem; for (let k = 0; k < reps; k++) this.n(name, inst, bar, st + k / trem, 1 / trem, nt.midi, vel * (k === 0 ? 1 : 0.75), panOff); }
      else this.n(name, inst, bar, st, Math.max(nt.len, minLen), nt.midi, vel, panOff);
    }
  }
  /* trem helper writes notes at fractional steps: override n for fractional start */
  mix({ revSize = 0.84, revDamp = 0.4, dlyTime = 0.75, dlyFb = 0.38, duckDepth = 0.5, duckRel = 0.14, master = 1 } = {}) {
    const N = this.N, ext = this.ext;
    const duckEnv = new Float32Array(ext).fill(1);
    for (const t0 of this.kicks) {
      const s0 = Math.round(t0 * SR);
      for (let i = 0; i < Math.round(0.5 * SR) && s0 + i < ext; i++) duckEnv[s0 + i] = Math.min(duckEnv[s0 + i], 1 - duckDepth * Math.exp(-i / SR / duckRel));
    }
    const L = new Float32Array(ext), R = new Float32Array(ext), sendR = new Float32Array(ext), sendD = new Float32Array(ext);
    for (const b of Object.values(this.bus)) {
      let bl = b.L, br = b.R;
      if (b.chorusOn) {
        const mono = bl.map((x, i) => (x + br[i]) * 0.5), [cl, cr] = chorus(mono, { rate: 0.35, depth: 0.003, base: 0.014 });
        bl = cl; br = cr;
      }
      for (let i = 0; i < ext; i++) {
        const g = b.duck ? 1 - (1 - duckEnv[i]) * b.duck : 1;
        const l = bl[i] * g * b.gain, r = br[i] * g * b.gain;
        L[i] += l; R[i] += r;
        if (b.rev) sendR[i] += (l + r) * 0.5 * b.rev;
        if (b.dly) sendD[i] += (l + r) * 0.5 * b.dly;
      }
    }
    const [rl, rr] = reverb(sendR, { room: revSize, damp: revDamp, predelay: 0.02 });
    const [dl, dr] = pingpong(sendD, this.spb * dlyTime, dlyFb, 3200);
    const [rl2, rr2] = reverb(dl.map((x, i) => x + dr[i]), { room: 0.7, damp: 0.5 });
    for (let i = 0; i < ext; i++) { L[i] += rl[i] + dl[i] + rl2[i] * 0.3; R[i] += rr[i] + dr[i] + rr2[i] * 0.3; }
    /* fold the tail onto the head: the loop's start carries the end's ring-out */
    const oL = L.slice(0, N), oR = R.slice(0, N);
    for (let i = 0; i < ext - N && i < N; i++) { oL[i] += L[N + i]; oR[i] += R[N + i]; }
    return [oL, oR];
  }
}

/* the master: a high-pass, a gentle soft limiter and a loudness target */
export function finish(chans, { rms = 0.14, peak = 0.9 } = {}) {
  for (const c of chans) { hp(c, 28, 0.7); removeDC(c); }
  /* a little air on top: the highs of the band come out dull after the reverb and the low-bitrate encode */
  for (const c of chans) { const hi = c.slice(); hp(hi, 5000, 0.7); for (let i = 0; i < c.length; i++) c[i] += hi[i] * 0.7; }
  let s = 0, n = 0;
  for (const c of chans) for (const v of c) { s += v * v; n++; }
  const k = rms / Math.sqrt(s / n);
  for (const c of chans) for (let i = 0; i < c.length; i++) c[i] = Math.tanh(c[i] * k / peak) * peak;
  return chans;
}
