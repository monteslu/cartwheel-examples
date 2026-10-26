// Battle mode's two sounds, synthesised here by our own deterministic code (the same toolbox as make-audio.mjs, which
// they stand beside so neither script rewrites the other's files):
//   balloon_pop.ogg   a balloon going: the rubber squeaks as it stretches, a sharp pop, a flap of torn skin
//   knockout.ogg      a kart out of the battle: a pop, then a sad falling trombone with a wobble and a thud
//
//   node games/racer/tools/make-battle-audio.mjs
import { mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import * as L from './audiolib.mjs';
import { rng, TAU, buf, shaped, mixArrays, lp, hp, bp, softclip, edges, norm, peakOf } from './audiolib.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, '..', 'assets', 'audio');
const TMP = join(HERE, '..', '..', '..', 'out', 'racer-audio');
mkdirSync(OUT, { recursive: true });
mkdirSync(TMP, { recursive: true });
const sr = 32000;
L.useSR(sr);
const NS = rng(0xba1100).noise;

function save(name, a, peak) {
  edges(a);
  norm(a, peak);
  L.writeOgg(join(OUT, name + '.ogg'), a, TMP, 4, sr);
  console.log(`${name}.ogg  ${(a.length / sr).toFixed(2)} s  peak ${peakOf(a).toFixed(2)}`);
}
function chirp(sec, f, amp) {
  let ph = 0;
  return shaped(sec, (t) => { ph += TAU * f(t) / sr; return Math.sin(ph) * amp(t); });
}
const at = (dst, src, t, g = 1) => L.addInto(dst, src, Math.round(t * sr), g);

/* the pop itself: a crack of bright noise with a hollow body ringing under it */
function pop(sec = 0.35) {
  const z = shaped(sec, () => NS());
  hp(z, 1200, 0.7);
  const crack = shaped(sec, (t, i) => z[i] * Math.exp(-t * 60) * Math.min(1, t * 3000));
  const body = shaped(sec, (t) => Math.sin(TAU * (520 - 260 * Math.min(1, t * 8)) * t) * Math.exp(-t * 28) * 0.7);
  const air = shaped(sec, () => NS());
  lp(air, 900, 0.7);
  const puff = shaped(sec, (t, i) => air[i] * Math.exp(-t * 18) * 0.5);
  return mixArrays(crack, body, puff);
}

{ /* balloon_pop: squeak, pop, flap */
  const sec = 0.55, a = buf(sec);
  const squeak = chirp(0.09, (t) => 900 + 2600 * t + 120 * Math.sin(TAU * 70 * t), (t) => Math.min(1, t * 60) * 0.25);
  bp(squeak, 1300, 1.2);
  at(a, squeak, 0);
  at(a, pop(), 0.085, 1);
  /* torn skin flapping: a few quick soft slaps, each lower */
  for (let k = 0; k < 4; k++) {
    const f = shaped(0.05, () => NS());
    bp(f, 700 - k * 110, 2);
    at(a, shaped(0.05, (t, i) => f[i] * Math.exp(-t * 90)), 0.13 + k * 0.045, 0.45 - k * 0.08);
  }
  save('balloon_pop', softclip(a, 1.4), 0.85);
}

{ /* knockout: pop, then the trombone falls (wah, wah, waaah) and lands on a thud */
  const sec = 1.9, a = buf(sec);
  at(a, pop(0.4), 0, 0.9);
  const notes = [[0.22, 392, 0.26], [0.52, 370, 0.26], [0.82, 349, 0.9]];
  for (const [t0, f0, len] of notes) {
    const last = f0 === 349;
    const o1 = L.oscillator('saw', f0 * 1.004), o2 = L.oscillator('saw', f0 * 0.996);
    let ph = 0;
    const n = shaped(len, (t, i) => {
      /* the last note sags and wobbles like a slide pulled all the way out */
      const bend = last ? 1 - 0.18 * Math.min(1, t / len) : 1;
      ph += TAU * f0 * bend * (1 + (last ? 0.02 * Math.sin(TAU * 6 * t) : 0)) / sr;
      return (o1(i) * 0.5 + o2(i) * 0.5 + Math.sin(ph) * 0.6) * Math.min(1, t / 0.03) * Math.min(1, (len - t) / 0.08);
    });
    L.svfRun(n, (i) => 380 + 1400 * Math.sin(Math.PI * Math.min(1, i / sr / len)), 1.4);
    at(a, n, t0, 0.55);
  }
  const thud = shaped(0.35, (t) => Math.sin(TAU * (90 - 40 * Math.min(1, t * 4)) * t) * Math.exp(-t * 14));
  at(a, thud, 1.68, 0.8);
  save('knockout', softclip(a, 1.3), 0.82);
}
