// tools/refinery_perf.mjs <trace.json>: per-frame CPU and GPU time from a CW_PROFILE_TRACE file
// (run through tools/refinery_perf.sh, which sets the trace variables for a headless run)
import fs from "node:fs";
const tr = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const ev = Array.isArray(tr) ? tr : tr.traceEvents;
const frames = new Set(), gpu = new Map();
let cpu = 0, ncpu = 0, gpuTotal = 0, ngpu = 0;
const tids = new Map();
for (const e of ev) if (e.ph === "M" && e.name === "thread_name") tids.set(e.tid, e.args.name);
for (const e of ev) {
  if (e.ph === "C" && e.name === "frame cpu ms") { cpu += Object.values(e.args)[0]; ncpu++; }
  if (e.ph === "X" && tids.get(e.tid) === "GPU") {
    gpu.set(e.name, (gpu.get(e.name) || 0) + e.dur / 1000);
    gpuTotal += e.dur / 1000;
    if (e.name === "scene") ngpu++;
  }
}
const n = Math.max(ngpu, 1);
console.log(`cpu ${(cpu / Math.max(ncpu, 1)).toFixed(2)} ms/frame over ${ncpu} frames; gpu ${(gpuTotal / n).toFixed(2)} ms/frame over ${n} frames`);
for (const [k, v] of [...gpu].sort((a, b) => b[1] - a[1]).slice(0, 8)) console.log(`  ${k.padEnd(18)} ${(v / n).toFixed(2)} ms`);
// main-thread zones: inclusive time per frame
const zones = new Map();
let nframe = 0;
for (const e of ev) {
  if (e.ph !== "X" || tids.get(e.tid) !== "main") continue;
  if (e.name === "frame") nframe++;
  const z = zones.get(e.name) || { t: 0, n: 0 };
  z.t += e.dur / 1000, z.n++;
  zones.set(e.name, z);
}
console.log("main thread, inclusive ms per frame:");
for (const [k, v] of [...zones].sort((a, b) => b[1].t - a[1].t).slice(0, 14)) console.log(`  ${k.padEnd(22)} ${(v.t / Math.max(nframe, 1)).toFixed(2)} ms  x${(v.n / Math.max(nframe, 1)).toFixed(1)}`);
