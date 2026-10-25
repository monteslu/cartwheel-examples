// Reads a 16-bit stereo wav that the game rendered offline (RACER_AUDIO_WAV=out/x.wav tools/run.sh ...) and prints,
// per second, the loudness, peak, brightness and left/right balance, plus a summary: the numbers you check a mix by
// when you cannot listen.   node games/racer/tools/analyze-mix.mjs out/x.wav [seconds-per-row]
import { readFileSync } from 'node:fs';
import { centroid, db, useSR } from './audiolib.mjs';
const b = readFileSync(process.argv[2]), rate = b.readUInt32LE(24), n = (b.length - 44) / 4, step = +(process.argv[3] || 1);
useSR(rate);
const L = new Float32Array(n), R = new Float32Array(n);
for (let i = 0; i < n; i++) { L[i] = b.readInt16LE(44 + i * 4) / 32768; R[i] = b.readInt16LE(46 + i * 4) / 32768; }
let clip = 0, pk = 0, tot = 0;
for (let i = 0; i < n; i++) { for (const v of [L[i], R[i]]) { const a = Math.abs(v); if (a > pk) pk = a; if (a >= 0.999) clip++; tot += v * v; } }
console.log(`${(n / rate).toFixed(1)} s at ${rate} Hz   peak ${pk.toFixed(3)}   rms ${db(Math.sqrt(tot / (2 * n))).toFixed(1)} dB   clipped samples ${clip}`);
console.log('  sec    rms dB  peak   centroid  L-R dB');
for (let t = 0; t + step <= n / rate; t += step) {
  const a = Math.floor(t * rate), e = Math.floor((t + step) * rate);
  let sl = 0, sr = 0, p = 0;
  for (let i = a; i < e; i++) { sl += L[i] * L[i]; sr += R[i] * R[i]; p = Math.max(p, Math.abs(L[i]), Math.abs(R[i])); }
  const m = new Float32Array(e - a);
  for (let i = a; i < e; i++) m[i - a] = (L[i] + R[i]) / 2;
  console.log(`  ${String(t).padStart(4)}  ${db(Math.sqrt((sl + sr) / (2 * (e - a)))).toFixed(1).padStart(7)} ${p.toFixed(2).padStart(5)} ${centroid(m, rate).toFixed(0).padStart(8)}  ${(db(Math.sqrt(sl / (e - a))) - db(Math.sqrt(sr / (e - a)))).toFixed(1).padStart(6)}`);
}
