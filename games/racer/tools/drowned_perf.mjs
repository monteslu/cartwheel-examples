// Prints the frame cost from a profiler trace (CW_PROFILE_TRACE): mean CPU frame ms and mean GPU ms per frame, and the
// busiest GPU passes. Usage: tools/drowned_perf.mjs out/trace.json
import { readFileSync } from 'node:fs';
const d = JSON.parse(readFileSync(process.argv[2], 'utf8'));
const ev = d.traceEvents || d;
const names = {};
for (const e of ev) if (e.ph === 'M' && e.name === 'thread_name') names[`${e.pid}:${e.tid}`] = e.args.name;
let frames = 0, cpu = 0;
const gpu = {};
let gpuTotal = 0;
for (const e of ev) {
  if (e.ph !== 'X') continue;
  const th = names[`${e.pid}:${e.tid}`];
  if (e.name === 'frame' && th === 'main') frames++, cpu += e.dur / 1000;
  if (th === 'GPU') gpu[e.name] = (gpu[e.name] || 0) + e.dur / 1000, gpuTotal += e.dur / 1000;
}
console.log(`frames ${frames}  cpu ${(cpu / frames).toFixed(2)} ms/frame  gpu ${(gpuTotal / frames).toFixed(2)} ms/frame`);
for (const [k, v] of Object.entries(gpu).sort((a, b) => b[1] - a[1]).slice(0, 8)) console.log(`  ${k.padEnd(28)} ${(v / frames).toFixed(2)}`);
