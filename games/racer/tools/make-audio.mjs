// Regenerates racer's sound effects, engines and loops (the music is made by
// make-music.mjs, which this script runs last). Everything is synthesised here
// by our own deterministic code: no third-party sound.
//
//   node games/racer/tools/make-audio.mjs [--no-music]
//
// Output goes to games/racer/assets/audio/:
//   engine_0..5.wav   six vehicle engines, 1 s loops with an integer number of firing pulses (the game pitches them)
//   turbo.wav         the whistle of the Longhaul's turbo
//   skid / slide / wind / water / road_* / missile loops (seamless: filtered round the seam)
//   *.ogg             one-shots (small, decoded to memory at load)
// and prints, for every file, its size, length, peak, loudness, brightness and the loop seam, then an
// offline check of the engines across their pitch range.
import { mkdirSync, statSync, readdirSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import * as L from './audiolib.mjs';
import { rng, TAU, buf, shaped, env, mixArrays, lp, hp, bp, resonate, circ, softclip, removeDC, norm, edges, peakOf, rmsOf, db, centroid, seam, mtof, clamp } from './audiolib.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'audio');
const TMP = join(HERE, '..', '..', '..', 'out', 'racer-audio');
mkdirSync(OUT, { recursive: true });
mkdirSync(TMP, { recursive: true });

const report = [];
const SR_ONE = 32000, SR_LOOP = 22050;
let sr = SR_ONE;
function use(n) { L.useSR(n); sr = n; }

function saveLoop(name, a, peak = 0.8, rate = sr) {
  /* loops are exactly periodic: no fades, no DC, normalised */
  removeDC(a); norm(a, peak);
  L.writeWav(join(OUT, name + '.wav'), a, rate);
  report.push({ name: name + '.wav', a, loop: true, rate });
}
function saveShot(name, a, peak = 0.85) {
  edges(a); norm(a, peak);
  L.writeOgg(join(OUT, name + '.ogg'), a, TMP, 4, sr);
  report.push({ name: name + '.ogg', a, loop: false, rate: sr });
}

/* ── helpers ──────────────────────────────────────────────────────────── */
/* a sine that follows f(t) (phase integrated), with an amplitude env */
function chirp(sec, f, amp = (t) => 1) {
  let ph = 0;
  return shaped(sec, (t) => { ph += TAU * f(t) / sr; return Math.sin(ph) * amp(t); });
}
/* a bell: inharmonic partials each ringing out */
function bell(f, sec, decay = 6, ratios = [1, 2.76, 5.4, 8.9], amps = [1, 0.5, 0.25, 0.12]) {
  return shaped(sec, (t) => { let v = 0; for (let k = 0; k < ratios.length; k++) v += Math.sin(TAU * f * ratios[k] * t) * amps[k] * Math.exp(-t * decay * (1 + k * 0.6)); return v * Math.min(1, t * 400); });
}
/* a plain tone with a few harmonics, gated */
function tone(f, sec, { harm = [1, 0.3, 0.1], a = 0.004, rel = 0.03, decay = 0 } = {}) {
  return shaped(sec, (t) => { let v = 0; for (let k = 0; k < harm.length; k++) v += Math.sin(TAU * f * (k + 1) * t) * harm[k]; return v * Math.min(1, t / a) * Math.min(1, (sec - t) / rel) * Math.exp(-t * decay); });
}
function brass(f, sec, bright = 1) {
  /* detuned saws through a filter that opens as it blows */
  const o1 = L.oscillator('saw', f * 1.003), o2 = L.oscillator('saw', f * 0.997), o3 = L.oscillator('square', f, 0.4);
  const a = shaped(sec, (t, i) => (o1(i) + o2(i) + o3(i) * 0.5) * Math.min(1, t / 0.02) * Math.min(1, (sec - t) / 0.06));
  return L.svfRun(a, (i) => 500 + 2500 * bright * Math.min(1, i / sr / 0.12), 1.2);
}
const at = (dst, src, t, g = 1) => L.addInto(dst, src, Math.round(t * sr), g);

// ═══════════════════════════ engines ═══════════════════════════════════
use(SR_ONE);
/* one engine: `fire` pulses per second (integer, so the 1 s loop is exact), each pulse a few damped resonances plus a
 * burst of noise, placed with the vehicle's firing order (offsets and strengths), a steady hum of harmonics, and a
 * drive stage for the snarl. The game pitches the loop from about 0.6x to 1.9x. */
function engine(i, spec) {
  const r = rng(0xe96 + i * 77), n = sr, a = new Float32Array(n);
  for (let p = 0; p < spec.fire; p++) {
    const [off, amp] = spec.order(p, r);
    if (amp <= 0) continue;
    const t0 = (p + off) / spec.fire, s0 = Math.floor(t0 * n) % n, len = Math.floor(sr * spec.ring);
    for (let k = 0; k < len; k++) {
      const t = k / sr;
      let v = 0;
      for (const [f, dec, g] of spec.formants) v += Math.sin(TAU * f * t) * g * Math.exp(-t * dec);
      v += r.noise() * spec.noise * Math.exp(-t * spec.noiseDecay);
      a[(s0 + k) % n] += v * amp;
    }
  }
  for (const [h, g] of spec.hum) for (let k = 0; k < n; k++) a[k] += Math.sin(TAU * h * k / sr) * g;
  if (spec.growl) for (let k = 0; k < n; k++) a[k] *= 1 + spec.growl[1] * Math.sin(TAU * spec.growl[0] * k / sr);
  circ(a, (d) => { hp(d, spec.hp, 0.7); lp(d, spec.lp, 0.8); });
  norm(a, 0.9);
  softclip(a, spec.drive);
  circ(a, (d) => lp(d, spec.lp * 1.1, 0.7));
  saveLoop('engine_' + i, a, 0.8);
}
const even = (p) => [0, 1];
const ENG = [
  /* 0 Scrapper: an honest four-cylinder, balanced */
  { fire: 56, ring: 0.045, formants: [[110, 60, 1], [230, 80, 0.5], [520, 120, 0.2]], noise: 0.1, noiseDecay: 120, hum: [[28, 0.2], [56, 0.15], [112, 0.07]], hp: 40, lp: 3000, drive: 1.6,
    order: (p, r) => [(r() - 0.5) * 0.06, 0.85 + 0.3 * r()] },
  /* 1 Rhino: big V8 burble, lumpy, deep */
  { fire: 34, ring: 0.09, formants: [[62, 35, 1], [124, 45, 0.7], [310, 90, 0.25]], noise: 0.07, noiseDecay: 80, hum: [[17, 0.3], [34, 0.2], [68, 0.12]], hp: 28, lp: 1300, drive: 1.8,
    order: (p, r) => [[0, 0.2, 0.1, 0.32][p % 4] * 0.5 + (r() - 0.5) * 0.04, p % 8 < 4 ? 1.0 : 0.75 + 0.2 * r()] },
  /* 2 Gnat: a high two-stroke buzz */
  { fire: 118, ring: 0.022, formants: [[620, 160, 0.8], [1250, 220, 0.5], [2400, 280, 0.3], [310, 110, 0.6]], noise: 0.35, noiseDecay: 260, hum: [[118, 0.18], [236, 0.1], [354, 0.1], [472, 0.06]], hp: 120, lp: 6500, drive: 2.4,
    order: (p, r) => [(r() - 0.5) * 0.04, 0.7 + 0.3 * r()] },
  /* 3 Sidewinder: a flat four with a snarl */
  { fire: 66, ring: 0.05, formants: [[150, 55, 1], [300, 70, 0.8], [700, 90, 0.4], [1500, 140, 0.15]], noise: 0.14, noiseDecay: 100, hum: [[33, 0.2], [66, 0.18], [132, 0.12]], hp: 55, lp: 3800, drive: 3.4, growl: [11, 0.25],
    order: (p, r) => [(p & 1) * 0.16 + (r() - 0.5) * 0.02, (p & 1) ? 0.8 : 1.0] },
  /* 4 Longhaul: a diesel clatter */
  { fire: 30, ring: 0.06, formants: [[58, 30, 1], [116, 40, 0.6], [1900, 180, 0.35], [2800, 220, 0.25]], noise: 0.45, noiseDecay: 160, hum: [[15, 0.35], [30, 0.2], [60, 0.1]], hp: 25, lp: 2400, drive: 1.5,
    order: (p, r) => [(r() - 0.5) * 0.03, 0.9 + 0.2 * r()] },
  /* 5 Pogo: a sputtering little two-stroke that misses and pops */
  { fire: 82, ring: 0.035, formants: [[260, 55, 1], [520, 75, 0.6], [1100, 100, 0.3]], noise: 0.25, noiseDecay: 150, hum: [[41, 0.12], [82, 0.1]], hp: 70, lp: 4200, drive: 2.2, growl: [4, 0.3],
    order: (p, r) => { const u = r(); return u < 0.2 ? [0, 0] : u < 0.3 ? [(r() - 0.5) * 0.3, 1.5] : [(r() - 0.5) * 0.12, 0.6 + 0.5 * r()]; } },
];
ENG.forEach((e, i) => engine(i, e));
/* the turbo whistle: two sines and a little air, a flutter at an integer rate */
{
  const a = shaped(1, (t) => (Math.sin(TAU * 2400 * t) + 0.35 * Math.sin(TAU * 4800 * t) + 0.15 * Math.sin(TAU * 3600 * t)) * (1 + 0.15 * Math.sin(TAU * 7 * t)));
  const air = shaped(1, () => 0); const r = rng(55); for (let i = 0; i < air.length; i++) air[i] = r.noise() * 0.25;
  circ(air, (d) => bp(d, 2400, 2));
  for (let i = 0; i < a.length; i++) a[i] += air[i];
  saveLoop('turbo', a, 0.6);
}

// ═══════════════════════════ loops (tyres, wind, water) ════════════════
use(SR_LOOP);
function noiseLoop(sec, seed) { const r = rng(seed), a = buf(sec); for (let i = 0; i < a.length; i++) a[i] = r.noise(); return a; }
const pulsesAt = (a, hz, fn) => { const per = sr / hz; for (let p = 0; p * per < a.length; p++) fn(Math.floor(p * per), p); };
{ /* road: a tyre hum on tarmac with the tick of the surface joins */
  const a = noiseLoop(1.5, 1);
  circ(a, (d) => { bp(d, 300, 0.9); lp(d, 1400); });
  const hum = shaped(1.5, (t) => Math.sin(TAU * 96 * t) * 0.25 + Math.sin(TAU * 144 * t) * 0.12);
  const j = buf(1.5); pulsesAt(j, 12, (s) => { if (s < j.length) j[s] += 0.8; });
  circ(j, (d) => { lp(d, 220); });
  saveLoop('road_road', mixArrays(a, hum, j), 0.8);
}
{ /* dirt and gravel: a rumble and the sparse crackle of stones */
  const r = rng(2), a = buf(1.5);
  for (let i = 0; i < a.length; i++) a[i] = r.noise() * (r() > 0.93 ? 1 : 0.12);
  circ(a, (d) => { bp(d, 1500, 0.6); });
  const rum = noiseLoop(1.5, 3); circ(rum, (d) => lp(d, 260));
  saveLoop('road_dirt', mixArrays(a, scaleOf(rum, 3)), 0.8);
}
{ /* ice: a thin hiss and nothing underneath */
  const a = noiseLoop(1.5, 4); circ(a, (d) => { hp(d, 2500); lp(d, 6500); });
  const t = shaped(1.5, (tt) => Math.sin(TAU * 3000 * tt) * 0.05 * (1 + Math.sin(TAU * 2 * tt)));
  saveLoop('road_ice', mixArrays(a, t), 0.6);
}
{ /* metal: a hollow drone with a clank at every plate join */
  const a = noiseLoop(1.5, 5);
  const m = mixArrays(resonate(a, 140, 25, 8), resonate(a, 335, 40, 5), resonate(a, 640, 70, 3));
  const j = buf(1.5); pulsesAt(j, 10, (s, p) => { if (s < j.length) j[s] += (p % 2 ? 1 : 0.7); });
  const clank = mixArrays(resonate(j, 480, 120, 4), resonate(j, 1130, 200, 2));
  const out = mixArrays(m, clank); circ(out, (d) => lp(d, 3200));
  saveLoop('road_metal', out, 0.8);
}
{ /* wood: planks, a knock at every board */
  const r = rng(6), j = buf(1.5);
  pulsesAt(j, 14, (s, p) => { const o = s + Math.floor(r() * 150); if (o < j.length) j[o] += 0.7 + 0.5 * r(); });
  const knock = mixArrays(resonate(j, 190, 70, 6), resonate(j, 420, 110, 3));
  const hiss = noiseLoop(1.5, 7); circ(hiss, (d) => { bp(d, 700, 0.8); });
  const out = mixArrays(knock, scaleOf(hiss, 0.18)); circ(out, (d) => lp(d, 2500));
  saveLoop('road_wood', out, 0.8);
}
{ /* glass: a smooth, slightly singing hiss */
  const a = noiseLoop(1.5, 8); circ(a, (d) => { bp(d, 3300, 1.4); });
  const s = shaped(1.5, (t) => Math.sin(TAU * 1800 * t) * 0.06);
  saveLoop('road_glass', mixArrays(a, s), 0.6);
}
{ /* wind: air going past, with a slow breathing */
  const a = noiseLoop(2, 9);
  circ(a, (d) => { hp(d, 160); lp(d, 2800, 0.6); });
  for (let i = 0; i < a.length; i++) a[i] *= 0.75 + 0.25 * Math.sin(TAU * 1.5 * i / sr) * Math.sin(TAU * 0.5 * i / sr + 1);
  saveLoop('wind', a, 0.8);
}
{ /* water through the wheels: a soft wash that swells slowly, with a few small bubbles. (It was chopped by an 18 Hz
   * envelope scaled 66 times, a sputter that read as an engine blowing out.) */
  const a = noiseLoop(1.5, 10);
  circ(a, (d) => { hp(d, 250); lp(d, 2400, 0.6); });
  const b = noiseLoop(1.5, 11); circ(b, (d) => lp(d, 3));
  let bm = 1e-9; for (let i = 0; i < b.length; i++) bm = Math.max(bm, Math.abs(b[i]));
  for (let i = 0; i < a.length; i++) a[i] *= 0.7 + 0.3 * (b[i] / bm);
  const bub = shaped(1.5, () => 0); const r = rng(12);
  for (let k = 0; k < 12; k++) { const t0 = r() * 1.4, f = 600 + r() * 900; for (let i = 0; i < 0.04 * sr; i++) { const j = Math.floor(t0 * sr) + i; if (j < bub.length) bub[j] += Math.sin(TAU * f * (1 + 4 * i / sr) * i / sr) * Math.exp(-i / sr * 90) * 0.15; } }
  saveLoop('water', mixArrays(a, bub), 0.6);
}
{ /* a tyre sliding on loose ground or ice: broad rough hiss */
  const a = noiseLoop(1.5, 13);
  circ(a, (d) => { bp(d, 1800, 0.5); });
  for (let i = 0; i < a.length; i++) a[i] *= 0.7 + 0.3 * Math.sin(TAU * 9 * i / sr);
  saveLoop('slide', a, 0.8);
}
{ /* the tyre squeal: narrow bands that waver, looping seamlessly (2 s, integer cycles) */
  const a = noiseLoop(2, 14), n = a.length, d2 = new Float32Array(n * 2);
  d2.set(a, 0); d2.set(a, n);
  const q2 = mixArrays(resonate(d2, 1150, 90, 6), resonate(d2, 1720, 140, 3), resonate(d2, 2300, 200, 1.5));
  const q = q2.slice(n);
  for (let i = 0; i < q.length; i++) q[i] *= 0.75 + 0.25 * Math.sin(TAU * 3 * i / sr);
  circ(q, (d) => hp(d, 500));
  saveLoop('skid', q, 0.7);
}
use(SR_ONE);
{ /* a missile in flight: a rocket whine and hiss (1 s, integer cycles) */
  const a = shaped(1, (t) => (Math.sin(TAU * 420 * t) + 0.5 * Math.sin(TAU * 840 * t + 1) + 0.3 * Math.sin(TAU * 1260 * t)) * (1 + 0.3 * Math.sin(TAU * 31 * t)));
  const h = noiseLoop(1, 15); circ(h, (d) => { hp(d, 900); lp(d, 5000); });
  for (let i = 0; i < a.length; i++) a[i] = a[i] * 0.5 + h[i] * 0.6;
  saveLoop('missile', a, 0.7);
}

// ═══════════════════════════ one-shots ═════════════════════════════════
const NS = rng(0x5eed).noise;
const noiseSec = (sec) => shaped(sec, () => NS());

/* countdown and race */
saveShot('count', tone(880, 0.3, { harm: [1, 0.25, 0.1], decay: 4 }), 0.8);
saveShot('go', mixArrays(tone(1760, 0.75, { harm: [1, 0.3, 0.15], decay: 3 }), tone(2217, 0.75, { harm: [1, 0.3], decay: 3 }), tone(1319, 0.75, { harm: [1, 0.3], decay: 3 })), 0.85);
saveShot('move', tone(1500, 0.05, { harm: [1, 0.2], a: 0.002, rel: 0.02, decay: 20 }), 0.5);
saveShot('select', mixArrays(tone(990, 0.12, { harm: [1, 0.3] }), (() => { const x = buf(0.2); at(x, tone(1480, 0.14, { harm: [1, 0.3], decay: 6 }), 0.06); return x; })()), 0.7);
saveShot('back', mixArrays(tone(740, 0.1, { harm: [1, 0.3] }), (() => { const x = buf(0.2); at(x, tone(494, 0.14, { harm: [1, 0.3], decay: 6 }), 0.06); return x; })()), 0.7);
{ /* a lap: two bell dings */
  const a = buf(1.1); at(a, bell(1319, 0.7, 5), 0); at(a, bell(1760, 0.8, 5), 0.17); saveShot('lap', a, 0.8);
}
{ /* the last lap: a short brass fanfare, rhythm 1-1-1-3 */
  const a = buf(1.5), n = [[523, 0, 0.12], [659, 0.14, 0.12], [784, 0.28, 0.12], [1047, 0.44, 0.7]];
  for (const [f, t, d] of n) at(a, brass(f, d, 1), t);
  const cym = noiseSec(1); circ(cym, (x) => hp(x, 6000)); for (let i = 0; i < cym.length; i++) cym[i] *= Math.exp(-i / sr * 5) * 0.25;
  at(a, cym, 0.44);
  saveShot('final_lap', a, 0.85);
}
{ /* the finish: a bigger fanfare with a held chord */
  const a = buf(2.6), n = [[523, 0, 0.14], [659, 0.16, 0.14], [784, 0.32, 0.14], [1047, 0.5, 0.2], [784, 0.74, 0.14], [1047, 0.9, 1.5]];
  for (const [f, t, d] of n) at(a, brass(f, d, 1), t);
  at(a, brass(1319, 1.5, 1), 0.9, 0.7); at(a, brass(1568, 1.5, 1), 0.9, 0.6); at(a, brass(523, 1.5, 0.6), 0.9, 0.6);
  const cym = noiseSec(2); circ(cym, (x) => hp(x, 5000)); for (let i = 0; i < cym.length; i++) cym[i] *= Math.exp(-i / sr * 2.5) * 0.3;
  at(a, cym, 0.9);
  saveShot('finish', a, 0.85);
}
{ /* wrong way: two sharp falling beeps, twice */
  const a = buf(0.8);
  for (let k = 0; k < 2; k++) { at(a, tone(740, 0.12, { harm: [1, 0.5, 0.3, 0.2], decay: 2 }), k * 0.4); at(a, tone(520, 0.2, { harm: [1, 0.5, 0.3, 0.2], decay: 2 }), k * 0.4 + 0.14); }
  saveShot('wrongway', a, 0.8);
}
{ /* an overtake: a quick rising sparkle */
  const a = buf(0.4); at(a, tone(1047, 0.1, { harm: [1, 0.3] }), 0); at(a, tone(1568, 0.25, { harm: [1, 0.3], decay: 8 }), 0.08); saveShot('overtake', a, 0.6);
}

/* items */
{ /* picking up a box: a bright rising arpeggio with a shimmer */
  const a = buf(0.6);
  [784, 988, 1175, 1568].forEach((f, i) => at(a, bell(f, 0.4, 7, [1, 2, 3], [1, 0.3, 0.1]), i * 0.055));
  saveShot('item_get', a, 0.75);
}
saveShot('tick', tone(1900, 0.03, { harm: [1, 0.4], a: 0.001, rel: 0.01, decay: 60 }), 0.5);
{ /* using an item: a pop and a rising swish */
  const sw = chirp(0.22, (t) => 500 + 2500 * t * 4.5, (t) => Math.exp(-t * 12) * 0.7);
  const pop = shaped(0.22, (t) => Math.sin(TAU * (180 - 80 * t * 4) * t) * Math.exp(-t * 40));
  saveShot('item_use', mixArrays(sw, pop), 0.75);
}
{ /* a boost pad: a quick rising whoosh */
  const w = noiseSec(0.7); circ(w, (d) => bp(d, 1200, 0.6));
  const sw = shaped(0.7, (t, i) => w[i] * Math.sin(Math.PI * Math.min(1, t / 0.7)) * (0.3 + t));
  const up = chirp(0.7, (t) => 300 + 900 * t, (t) => Math.sin(Math.PI * Math.min(1, t / 0.7)) * 0.4);
  saveShot('boost_pad', mixArrays(sw, up), 0.8);
}
{ /* a turbo item: a rocket kick, a flame roar */
  const w = noiseSec(1.0); L.svfRun(w, (i) => 3500 - 3000 * Math.min(1, i / sr), 0.9);
  const kick = shaped(1.0, (t) => Math.sin(TAU * (130 - 60 * t) * t) * Math.exp(-t * 7));
  const sw = chirp(1.0, (t) => 400 + 700 * Math.min(1, t * 2), (t) => Math.exp(-t * 3) * 0.3);
  saveShot('boost_item', mixArrays(shaped(1.0, (t, i) => w[i] * Math.min(1, t * 30) * Math.exp(-t * 2.2)), kick, sw), 0.85);
}
for (let lv = 1; lv <= 3; lv++) { /* a drift boost: more layers and a brighter top at each level */
  const len = 0.35 + 0.1 * lv, a = buf(len + 0.1), w = noiseSec(len + 0.1);
  circ(w, (d) => bp(d, 900 + 500 * lv, 0.7));
  const body = shaped(len + 0.1, (t, i) => w[i] * Math.min(1, t * 30) * Math.exp(-t * 4) * 0.8);
  const root = [0, 500, 560, 660][lv];
  const sw = chirp(len + 0.1, (t) => root * (1 + 0.8 * Math.min(1, t * 4)), (t) => Math.exp(-t * 5) * 0.45);
  for (let k = 0; k < lv; k++) at(a, chirp(0.12, (t) => root * (1.5 + k * 0.5) * (1 + 2 * t), (t) => Math.exp(-t * 12) * 0.35), k * 0.06);
  saveShot('mini' + lv, mixArrays(a, body, sw), 0.8);
}
{ /* a trick: a spinning sparkle */
  const a = buf(0.6), r = rng(77);
  for (let k = 0; k < 8; k++) at(a, bell(1200 + k * 230 + r() * 100, 0.3, 10, [1, 2.4], [1, 0.3]), k * 0.045, 0.6);
  saveShot('trick', a, 0.7);
}
saveShot('drift_chime', bell(1760, 0.35, 9, [1, 2, 3.01], [1, 0.4, 0.15]), 0.7);
{ /* landings: a thump, harder ones with a crunch of springs */
  const soft = shaped(0.3, (t) => Math.sin(TAU * (95 - 40 * Math.min(1, t * 5)) * t) * Math.exp(-t * 16) + NS() * 0.3 * Math.exp(-t * 60));
  saveShot('land_soft', lp(soft, 900), 0.8);
  const hit = shaped(0.5, (t) => NS() * Math.exp(-t * 40));
  const hard = mixArrays(shaped(0.5, (t) => Math.sin(TAU * (80 - 45 * Math.min(1, t * 4)) * t) * Math.exp(-t * 9) * 1.1), resonate(hit, 210, 90, 3), resonate(hit, 560, 140, 1.5));
  saveShot('land_hard', lp(hard, 1800), 0.9);
}
{ /* a wall scrape: grinding noise through a wobbling band with ringing sparks */
  const a = shaped(0.5, (t) => NS() * (0.6 + 0.4 * Math.sin(TAU * 29 * t)) * Math.min(1, t * 40) * Math.min(1, (0.5 - t) * 8));
  saveShot('scrape', mixArrays(hp(lp(a.slice(), 3800), 500), resonate(a, 2300, 450, 1.5), resonate(a, 3300, 600, 1)), 0.7);
}
{ /* a kart bump: a clunk of two shells */
  const hit = shaped(0.35, (t) => NS() * Math.exp(-t * 80));
  const b = mixArrays(resonate(hit, 240, 80, 3), resonate(hit, 510, 120, 2), resonate(hit, 980, 200, 1), shaped(0.35, (t) => Math.sin(TAU * (110 - 50 * t) * t) * Math.exp(-t * 22) * 0.9));
  saveShot('bump', lp(b, 2800), 0.85);
}
{ /* a missile leaving: a thump, a rising whoosh */
  const w = noiseSec(0.9); circ(w, (d) => bp(d, 1500, 0.5));
  const sh = shaped(0.9, (t, i) => w[i] * Math.min(1, t * 20) * Math.exp(-t * 2.2));
  const th = shaped(0.9, (t) => Math.sin(TAU * (160 - 90 * Math.min(1, t * 4)) * t) * Math.exp(-t * 12));
  const wh = chirp(0.9, (t) => 400 + 800 * t, (t) => Math.exp(-t * 3) * 0.3);
  saveShot('missile_launch', mixArrays(sh, th, wh), 0.85);
}
{ /* an explosion: a huge low whump, a roar falling away, then debris */
  const sec = 2.4, rumble = shaped(sec, (t) => Math.sin(TAU * (75 - 45 * Math.min(1, t * 1.2)) * t) * Math.exp(-t * 2.4) * Math.min(1, t * 200));
  const roar = noiseSec(sec); L.svfRun(roar, (i) => 4500 * Math.exp(-i / sr * 2.6) + 120, 0.8);
  const roarE = shaped(sec, (t, i) => roar[i] * Math.exp(-t * 2.2) * Math.min(1, t * 400));
  const crack = shaped(sec, (t) => NS() * Math.exp(-t * 55) * 0.9);
  const deb = shaped(sec, (t) => (t > 0.25 ? NS() * (NS() > 0.97 ? 1 : 0.04) * Math.exp(-(t - 0.25) * 2.2) : 0));
  const body = mixArrays(scaleOf(rumble, 1.6), roarE, crack, hp(deb, 1800));
  saveShot('explosion', softclip(body, 1.3), 0.8);
}
{ /* a mine arming: three quick beeps */
  const a = buf(0.42); for (let k = 0; k < 3; k++) at(a, tone(1500, 0.07, { harm: [1, 0.3], decay: 8 }), k * 0.13);
  saveShot('mine_arm', a, 0.65);
}
{ /* oil: a wet splat with a few bubbles */
  const sp = noiseSec(0.55); L.svfRun(sp, (i) => 2500 * Math.exp(-i / sr * 9) + 200, 1);
  const splat = shaped(0.55, (t, i) => sp[i] * Math.exp(-t * 7) * Math.min(1, t * 400));
  const bub = buf(0.55); const r = rng(88);
  for (let k = 0; k < 5; k++) at(bub, chirp(0.06, (t) => 300 + r() * 0 + 2200 * t, (t) => Math.exp(-t * 50) * 0.5), 0.05 + k * 0.07 + r() * 0.04, 0.8);
  saveShot('oil', mixArrays(splat, bub), 0.8);
}
{ /* a shockwave: a crackling electric burst under a falling buzz */
  const sec = 1.3, z = noiseSec(sec); circ(z, (d) => hp(d, 800));
  const crack = shaped(sec, (t, i) => z[i] * (NS() > 0.4 ? 1 : 0.2) * Math.exp(-t * 3) * (0.6 + 0.4 * Math.sin(TAU * 120 * t)));
  const buzz = chirp(sec, (t) => 900 * Math.exp(-t * 2.4) + 90, (t) => Math.exp(-t * 3) * 0.6);
  const thump = shaped(sec, (t) => Math.sin(TAU * (100 - 50 * Math.min(1, t * 3)) * t) * Math.exp(-t * 10));
  saveShot('shock', softclip(mixArrays(crack, scaleOf(buzz, 0.8), thump), 1.6), 0.8);
}
{ /* a shield coming on: a rising hum with glassy partials; popping: a bright shatter */
  const on = chirp(0.7, (t) => 220 + 700 * Math.min(1, t * 1.4), (t) => Math.min(1, t * 8) * Math.min(1, (0.7 - t) * 6) * 0.6);
  const sh = shaped(0.7, (t) => Math.sin(TAU * 1760 * t) * Math.sin(TAU * 11 * t) * 0.15 * Math.min(1, t * 3) * Math.min(1, (0.7 - t) * 6));
  saveShot('shield_on', mixArrays(on, sh), 0.7);
  const burst = noiseSec(0.5); circ(burst, (d) => hp(d, 3000));
  const pop = shaped(0.5, (t, i) => burst[i] * Math.exp(-t * 14) * Math.min(1, t * 500));
  const ping = chirp(0.5, (t) => 2600 * Math.exp(-t * 4), (t) => Math.exp(-t * 9) * 0.5);
  saveShot('shield_pop', mixArrays(pop, ping, bell(3300, 0.4, 12, [1, 1.9], [0.4, 0.2])), 0.85);
}
{ /* a spin-out: a cartoon spiral down with a wobble */
  const a = chirp(0.9, (t) => (1500 * Math.exp(-t * 1.6) + 250) * (1 + 0.05 * Math.sin(TAU * 11 * t)), (t) => Math.exp(-t * 1.5) * Math.min(1, (0.9 - t) * 8) * 0.6);
  const sq = shaped(0.9, (t) => NS() * 0.15 * Math.exp(-t * 2));
  circ(sq, (d) => bp(d, 1400, 1));
  saveShot('spin', mixArrays(a, sq), 0.75);
}
{ /* falling off the world: a long whistle down; coming back: a bright rising blip */
  saveShot('fall', chirp(1.3, (t) => 1600 * Math.exp(-t * 1.9) + 90, (t) => Math.min(1, t * 20) * Math.exp(-t * 1.1) * 0.7), 0.7);
  const rp = chirp(0.5, (t) => 300 + 1300 * Math.min(1, t * 2.4) ** 2, (t) => Math.min(1, t * 30) * Math.exp(-t * 4) * 0.6);
  const sp = buf(0.5); [1568, 2093, 2637].forEach((f, i) => at(sp, bell(f, 0.25, 12, [1, 2.2], [1, 0.3]), 0.15 + i * 0.05, 0.4));
  saveShot('respawn', mixArrays(rp, sp), 0.75);
}
{ /* the world dropping away on a jump: a soft falling swoosh */
  const w = noiseSec(1.0); L.svfRun(w, (i) => 2800 * Math.exp(-i / sr * 3) + 200, 0.8);
  const s = chirp(1.0, (t) => 420 * Math.exp(-t * 1.8) + 70, (t) => Math.min(1, t * 6) * Math.exp(-t * 2) * 0.35);
  saveShot('drop', mixArrays(shaped(1.0, (t, i) => w[i] * Math.min(1, t * 8) * Math.exp(-t * 2.4) * 0.8), s), 0.6);
}
{ /* a splash: a burst of water */
  const w = noiseSec(0.7); L.svfRun(w, (i) => 4500 * Math.exp(-i / sr * 6) + 300, 0.9);
  const sp = shaped(0.7, (t, i) => w[i] * Math.min(1, t * 200) * Math.exp(-t * 5));
  const bub = buf(0.7), r = rng(99);
  for (let k = 0; k < 9; k++) at(bub, chirp(0.05, (t) => 400 + r() * 700 + 3000 * t, (t) => Math.exp(-t * 60) * 0.3), 0.04 + r() * 0.4);
  saveShot('splash', mixArrays(sp, bub), 0.8);
}

// ═══════════════════════════ the report ════════════════════════════════
function scaleOf(a, k) { const o = new Float32Array(a.length); for (let i = 0; i < a.length; i++) o[i] = a[i] * k; return o; }

console.log('file                     bytes   sec   peak   rms   centroid  seam');
let total = 0;
const issues = [];
for (const f of report) {
  const path = join(OUT, f.name), bytes = statSync(path).size;
  total += bytes;
  /* check what the file DECODES to (the ogg too), at its own rate */
  const d = f.name.endsWith('.ogg') ? L.decode(path, TMP, f.rate, 1)[0] : f.a;
  L.useSR(f.rate);
  const pk = peakOf(d), rm = rmsOf(d), ce = centroid(d, f.rate), sm = f.loop ? seam(d) : 0;
  console.log(`${f.name.padEnd(22)} ${String(bytes).padStart(7)} ${(d.length / f.rate).toFixed(2).padStart(5)} ${pk.toFixed(2).padStart(6)} ${db(rm).toFixed(1).padStart(6)} ${ce.toFixed(0).padStart(8)} ${f.loop ? sm.toFixed(2).padStart(6) : '     -'}`);
  if (pk > 0.98) issues.push(f.name + ' clips');
  if (rm < 0.005) issues.push(f.name + ' is nearly silent');
  if (f.loop && sm > 6) issues.push(f.name + ' loop seam jump ' + sm.toFixed(1));
  if (f.name.endsWith('.ogg') && Math.abs(d[0]) > 0.05) issues.push(f.name + ' starts with a click');
  if (!isFinite(pk)) issues.push(f.name + ' has NaN');
}
console.log(`sound effects total ${(total / 1024).toFixed(0)} KB, ${report.length} files`);

/* the engines as the game will play them: resampled by linear interpolation at several pitches */
use(SR_ONE);
console.log('\nengines across the pitch range (rms dB / centroid Hz at pitch 0.6, 1.0, 1.9):');
for (let i = 0; i < 6; i++) {
  const e = report.find((x) => x.name === 'engine_' + i + '.wav').a;
  const row = [0.6, 1.0, 1.9].map((p) => {
    const n = 32000 * 2, o = new Float32Array(n);
    for (let k = 0; k < n; k++) { const x = k * p, i0 = Math.floor(x) % e.length, i1 = (i0 + 1) % e.length, fr = x - Math.floor(x); o[k] = e[i0] * (1 - fr) + e[i1] * fr; }
    return `${db(rmsOf(o)).toFixed(1)}dB/${centroid(o, 32000).toFixed(0)}Hz`;
  });
  console.log('  engine_' + i, row.join('  '));
}
if (issues.length) { console.log('\nISSUES:\n  ' + issues.join('\n  ')); }
else console.log('\nno clipping, silence, NaN or loop-seam problems in the effects');

if (!process.argv.includes('--no-music')) {
  const r = spawnSync('node', [join(HERE, 'make-music.mjs')], { stdio: 'inherit' });
  if (r.status !== 0) process.exit(r.status || 1);
}
if (issues.length) process.exitCode = 1;
