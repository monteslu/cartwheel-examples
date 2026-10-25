// racer's audio toolbox: the small DSP kit the generators share (oscillators,
// filters, envelopes, a reverb, a delay, a chorus, wav and ogg writers and the
// numbers the offline checks print). Deterministic: every random draw comes
// from the seeded generators of the engine's tools/lib/rng.mjs.
import { writeFileSync, mkdirSync, readFileSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { rng } from '../../../deps/cartwheel-engine/tools/lib/rng.mjs';

export let SR = 32000;
export const useSR = (n) => { SR = n; };
export const TAU = Math.PI * 2;
export { rng };

export const buf = (sec) => new Float32Array(Math.ceil(sec * SR));
export const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
export const mtof = (m) => 440 * Math.pow(2, (m - 69) / 12);
export const db = (v) => 20 * Math.log10(Math.max(v, 1e-9));
export const fromdb = (d) => Math.pow(10, d / 20);

export function shaped(sec, fn) { const a = buf(sec); for (let i = 0; i < a.length; i++) a[i] = fn(i / SR, i); return a; }
export const env = (t, a, d) => (t < a ? t / a : Math.exp(-(t - a) * d));
export const mixArrays = (...xs) => { const n = Math.max(...xs.map((x) => x.length)), o = new Float32Array(n); for (const x of xs) for (let i = 0; i < x.length; i++) o[i] += x[i]; return o; };
export const scale = (a, k) => { for (let i = 0; i < a.length; i++) a[i] *= k; return a; };
export function addInto(dst, src, at, gain = 1) {
  const s0 = Math.max(0, -at);
  for (let i = s0; i < src.length; i++) { const j = at + i; if (j >= dst.length) break; dst[j] += src[i] * gain; }
}

/* ── filters: RBJ biquads, in place ─────────────────────────────────── */
function biquad(a, type, f, q = 0.7071) {
  const w = TAU * clamp(f, 10, SR * 0.45) / SR, c = Math.cos(w), al = Math.sin(w) / (2 * q);
  let b0, b1, b2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
  if (type === 'lp') { b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; }
  else if (type === 'hp') { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; }
  else if (type === 'bp') { b0 = al; b1 = 0; b2 = -al; }
  else { b0 = 1; b1 = -2 * c; b2 = 1; } /* notch */
  b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (let i = 0; i < a.length; i++) {
    const x = a[i], y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y; a[i] = y;
  }
  return a;
}
export const lp = (a, f, q) => biquad(a, 'lp', f, q);
export const hp = (a, f, q) => biquad(a, 'hp', f, q);
export const bp = (a, f, q) => biquad(a, 'bp', f, q);
export const notch = (a, f, q) => biquad(a, 'notch', f, q);
/* a loop filtered round the seam: two passes over the loop, keep the second */
export function circ(a, fn) {
  const n = a.length, d = new Float32Array(n * 2);
  d.set(a, 0); d.set(a, n);
  fn(d);
  a.set(d.subarray(n), 0);
  return a;
}
/* a two-pole resonator: the ring of something struck */
export function resonate(a, f, bw, gain = 1) {
  const r = Math.exp(-Math.PI * bw / SR), c = 2 * r * Math.cos(TAU * f / SR);
  let y1 = 0, y2 = 0;
  const out = new Float32Array(a.length);
  for (let i = 0; i < a.length; i++) { const y = a[i] + c * y1 - r * r * y2; y2 = y1; y1 = y; out[i] = y * (1 - r) * gain; }
  return out;
}
export const softclip = (a, drive = 1) => { const k = 1 / Math.tanh(drive); for (let i = 0; i < a.length; i++) a[i] = Math.tanh(a[i] * drive) * k; return a; };
export function removeDC(a) { let m = 0; for (const v of a) m += v; m /= a.length || 1; for (let i = 0; i < a.length; i++) a[i] -= m; return a; }
export function peakOf(a) { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); return m; }
export function norm(a, peak = 0.9) { const m = peakOf(a) || 1; for (let i = 0; i < a.length; i++) a[i] *= peak / m; return a; }
export function rmsOf(a) { let s = 0; for (const v of a) s += v * v; return Math.sqrt(s / (a.length || 1)); }
/* a short fade at both ends and no DC: a one-shot never clicks */
export function edges(a, fi = 0.002, fo = 0.01) {
  removeDC(a);
  const n1 = Math.floor(fi * SR), n2 = Math.floor(fo * SR);
  for (let i = 0; i < a.length; i++) {
    if (i < n1) a[i] *= i / n1;
    if (i > a.length - n2) a[i] *= (a.length - i) / n2;
  }
  return a;
}

/* ── oscillators (band-limited with polyBLEP) ───────────────────────── */
const blep = (t, dt) => {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
};
/* freq is a number or f(i) -> Hz; returns a function giving the next sample */
export function oscillator(kind, freq, pw = 0.5, phase0 = 0) {
  let ph = phase0;
  return (i) => {
    const f = typeof freq === 'function' ? freq(i) : freq, dt = f / SR;
    let v;
    if (kind === 'saw') v = 2 * ph - 1 - blep(ph, dt);
    else if (kind === 'square') v = (ph < pw ? 1 : -1) + blep(ph, dt) - blep((ph + 1 - pw) % 1, dt);
    else if (kind === 'tri') v = 4 * Math.abs(ph - 0.5) - 1;
    else v = Math.sin(TAU * ph);
    ph += dt; if (ph >= 1) ph -= 1;
    return v;
  };
}

/* a state variable filter with a cutoff that can move every few samples */
export function svfRun(a, cutFn, q = 0.8, mode = 'lp') {
  let ic1 = 0, ic2 = 0, g = 0, k = 1 / q, a1 = 0, a2 = 0, a3 = 0;
  for (let i = 0; i < a.length; i++) {
    if ((i & 7) === 0) {
      g = Math.tan(Math.PI * clamp(cutFn(i), 20, SR * 0.45) / SR);
      a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    const v3 = a[i] - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
    a[i] = mode === 'lp' ? v2 : mode === 'bp' ? v1 : a[i] - k * v1 - v2;
  }
  return a;
}

/* ── effects (offline, whole buffers) ───────────────────────────────── */
/* Freeverb: 8 combs and 4 allpasses a side; returns [L, R] from a mono send */
export function reverb(input, { room = 0.84, damp = 0.35, predelay = 0.012, width = 1 } = {}) {
  const sc = SR / 44100;
  const ct = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617], at = [556, 441, 341, 225];
  const n = input.length, pd = Math.floor(predelay * SR);
  const out = [new Float32Array(n), new Float32Array(n)];
  for (let ch = 0; ch < 2; ch++) {
    const spread = ch ? Math.round(23 * sc) : 0;
    const combs = ct.map((l) => ({ b: new Float32Array(Math.round(l * sc) + spread), i: 0, s: 0 }));
    const aps = at.map((l) => ({ b: new Float32Array(Math.round(l * sc) + spread), i: 0 }));
    const o = out[ch];
    for (let i = 0; i < n; i++) {
      const x = (i >= pd ? input[i - pd] : 0) * 0.015;
      let y = 0;
      for (const c of combs) {
        const v = c.b[c.i];
        c.s = v * (1 - damp) + c.s * damp;
        c.b[c.i] = x + c.s * room;
        if (++c.i >= c.b.length) c.i = 0;
        y += v;
      }
      for (const ap of aps) {
        const v = ap.b[ap.i], z = y + v * 0.5;
        ap.b[ap.i] = z;
        y = v - y;
        if (++ap.i >= ap.b.length) ap.i = 0;
      }
      o[i] = y * 3.2;
    }
  }
  if (width < 1) { for (let i = 0; i < n; i++) { const m = (out[0][i] + out[1][i]) / 2; out[0][i] = m + (out[0][i] - m) * width; out[1][i] = m + (out[1][i] - m) * width; } }
  return out;
}
/* a ping-pong echo from a mono send; returns [L, R] */
export function pingpong(input, timeSec, feedback = 0.4, tone = 3500) {
  const n = input.length, d = Math.max(1, Math.round(timeSec * SR));
  const L = new Float32Array(n), R = new Float32Array(n);
  const k = 1 - Math.exp(-TAU * tone / SR);
  let fl = 0, fr = 0;
  for (let i = 0; i < n; i++) {
    const inL = i >= d ? R[i - d] : 0, inR = i >= d ? L[i - d] : 0;
    fl += k * (inL * feedback - fl); fr += k * (inR * feedback - fr);
    L[i] = (i < n ? input[i] : 0) + fl;
    R[i] = fr;
  }
  return [L, R];
}
/* a chorus: two modulated taps, returns [L, R] from mono */
export function chorus(a, { rate = 0.5, depth = 0.004, base = 0.012 } = {}) {
  const n = a.length, L = new Float32Array(n), R = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const t = i / SR;
    for (let ch = 0; ch < 2; ch++) {
      const d = (base + depth * Math.sin(TAU * rate * t + ch * Math.PI * 0.8)) * SR, p = i - d, i0 = Math.floor(p), f = p - i0;
      const v = i0 >= 1 && i0 + 1 < n ? a[i0] * (1 - f) + a[i0 + 1] * f : 0;
      (ch ? R : L)[i] = 0.5 * a[i] + 0.6 * v;
    }
  }
  return [L, R];
}

/* ── files ──────────────────────────────────────────────────────────── */
export function wavBytes(chans, rate = SR) {
  const nch = chans.length, n = chans[0].length, b = Buffer.alloc(44 + n * 2 * nch);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2 * nch, 4); b.write('WAVE', 8); b.write('fmt ', 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(nch, 22); b.writeUInt32LE(rate, 24);
  b.writeUInt32LE(rate * 2 * nch, 28); b.writeUInt16LE(2 * nch, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2 * nch, 40);
  for (let i = 0; i < n; i++) for (let c = 0; c < nch; c++) b.writeInt16LE(Math.round(clamp(chans[c][i], -1, 1) * 32767), 44 + (i * nch + c) * 2);
  return b;
}
export function writeWav(path, chans, rate = SR) { writeFileSync(path, wavBytes(Array.isArray(chans) ? chans : [chans], rate)); }
/* ogg vorbis through ffmpeg; quality is libvorbis -q (0..10) */
export function writeOgg(path, chans, tmpDir, quality = 3, rate = SR) {
  mkdirSync(tmpDir, { recursive: true });
  const tmp = tmpDir + '/_enc.wav';
  writeFileSync(tmp, wavBytes(Array.isArray(chans) ? chans : [chans], rate));
  const r = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', tmp, '-c:a', 'libvorbis', '-q:a', String(quality), '-map_metadata', '-1', '-fflags', '+bitexact', '-flags:a', '+bitexact', path]);
  rmSync(tmp, { force: true });
  if (r.status !== 0) throw new Error('ffmpeg failed: ' + r.stderr);
}
/* decode any file ffmpeg reads back to float channels (for the checks) */
export function decode(path, tmpDir, rate = SR, channels = 1) {
  mkdirSync(tmpDir, { recursive: true });
  const raw = tmpDir + '/_dec.raw';
  const r = spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', path, '-f', 'f32le', '-ac', String(channels), '-ar', String(rate), raw]);
  if (r.status !== 0) throw new Error('ffmpeg decode failed: ' + r.stderr);
  const b = readFileSync(raw), f = new Float32Array(b.buffer, b.byteOffset, b.length / 4), n = f.length / channels;
  rmSync(raw, { force: true });
  const out = [];
  for (let c = 0; c < channels; c++) { const a = new Float32Array(n); for (let i = 0; i < n; i++) a[i] = f[i * channels + c]; out.push(a); }
  return out;
}

/* ── analysis ───────────────────────────────────────────────────────── */
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -TAU / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let j = 0; j < len / 2; j++) {
        const ur = re[i + j], ui = im[i + j], vr = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci, vi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr;
        re[i + j] = ur + vr; im[i + j] = ui + vi; re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi;
        const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
}
/* spectral centroid in Hz, averaged over Hann windows of 2048 */
export function centroid(a, rate = SR) {
  const N = 2048;
  let num = 0, den = 0;
  for (let p = 0; p + N <= a.length; p += N) {
    const re = new Float64Array(N), im = new Float64Array(N);
    for (let i = 0; i < N; i++) re[i] = a[p + i] * (0.5 - 0.5 * Math.cos(TAU * i / N));
    fft(re, im);
    for (let k = 1; k < N / 2; k++) { const m = Math.hypot(re[k], im[k]); num += m * k * rate / N; den += m; }
  }
  return den ? num / den : 0;
}
/* how big the jump across a loop seam is, against the typical sample step */
export function seam(a) {
  /* the jump across the wrap against the 99th percentile of ordinary steps: 1 or less is as smooth as the music itself */
  const n = a.length, st = new Float32Array(n - 1);
  for (let i = 1; i < n; i++) st[i - 1] = Math.abs(a[i] - a[i - 1]);
  const ref = st.slice().sort()[Math.floor((n - 1) * 0.99)] || 1e-9;
  return Math.abs(a[0] - a[n - 1]) / ref;
}
