// Scans bomberdude's audio for the things that buzz or click: the largest
// sample-to-sample step, the high-frequency share (energy of the first
// difference over the signal's), clipped samples, DC, the start / end
// samples (a click if not ~0), and the autocorrelation at lag 419 (the period
// a broken noise generator fell into).
//
//   node games/bomberdude/tools/scan-audio.mjs [--json out.json]
import { readFileSync, readdirSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const D = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'audio');
function pcm(path) {
  let buf = readFileSync(path);
  if (path.endsWith('.ogg')) {
    const t = mkdtempSync(join(tmpdir(), 'bbscan-')), w = join(t, 'a.wav');
    spawnSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', path, '-ac', '1', '-ar', '48000', w]);
    buf = readFileSync(w);
    rmSync(t, { recursive: true });
  }
  let off = 12, data = null, ch = 1;
  while (off < buf.length) {
    const id = buf.toString('ascii', off, off + 4), len = buf.readUInt32LE(off + 4);
    if (id === 'fmt ') ch = buf.readUInt16LE(off + 10);
    if (id === 'data') data = buf.subarray(off + 8, off + 8 + len);
    off += 8 + len + (len & 1);
  }
  const n = data.length / 2 / ch, a = new Float32Array(n);
  for (let i = 0; i < n; i++) a[i] = data.readInt16LE(i * 2 * ch) / 32768;
  return a;
}
function scan(a) {
  let step = 0, e = 0, ed = 0, clip = 0, dc = 0, peak = 0;
  for (let i = 0; i < a.length; i++) {
    dc += a[i];
    e += a[i] * a[i];
    peak = Math.max(peak, Math.abs(a[i]));
    if (Math.abs(a[i]) > 0.999) clip++;
    if (i) { const d = a[i] - a[i - 1]; step = Math.max(step, Math.abs(d)); ed += d * d; }
  }
  let ac = 0, a0 = 0;
  for (let i = 0; i + 419 < a.length; i++) { ac += a[i] * a[i + 419]; a0 += a[i] * a[i]; }
  return { step: +step.toFixed(3), hf: +(ed / (e || 1)).toFixed(3), clip, dc: +(dc / a.length).toFixed(4), peak: +peak.toFixed(3),
    first: +Math.abs(a[0]).toFixed(3), last: +Math.abs(a[a.length - 1]).toFixed(3), ac419: +(ac / (a0 || 1)).toFixed(3) };
}
const out = {};
for (const f of readdirSync(D).sort()) {
  if (!/\.(wav|ogg)$/.test(f)) continue;
  out[f] = scan(pcm(join(D, f)));
  console.log(f.padEnd(20), JSON.stringify(out[f]));
}
const j = process.argv.indexOf('--json');
if (j > 0) writeFileSync(process.argv[j + 1], JSON.stringify(out, null, 1));
