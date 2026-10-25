#!/usr/bin/env bash
# games/racer/tools/frost_perf.sh <name> <frame> [VAR=value ...]: frame cost of a Frostbite Pass run (CPU ms per frame, GPU ms of the passes, draw calls) over 60 frames from `frame`
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
O="$D/../../out"
N="$1"; F="$2"; shift 2
"$D/tools/run.sh" "fperf-$N" "$((F + 70))" RACER_MAP=5 RACER_AUTO=1 CW_PROFILE_TRACE="$O/frost-trace-$N.json" CW_PROFILE_TRACE_START="$F" CW_PROFILE_TRACE_FRAMES=60 CW_PROFILE_GPU=1 "$@" >/dev/null 2>&1
node -e '
const fs=require("fs"); const j=JSON.parse(fs.readFileSync(process.argv[1],"utf8")); const ev=j.traceEvents||j;
let cpu=[],gpu=0,n=0; const per={};
for (const e of ev) { if (e.ph==="C" && e.name==="frame cpu ms") cpu.push(e.args.ms); if (e.ph==="X" && e.tid===1000) { (per[e.name]=per[e.name]||[]).push(e.dur/1000); } }
for (const a of Object.values(per)) gpu += a.reduce((x,y)=>x+y,0)/a.length;
const st=JSON.parse(fs.readFileSync(process.argv[2],"utf8"));
console.log(process.argv[3], "cpu ms/frame", (cpu.reduce((x,y)=>x+y,0)/cpu.length).toFixed(2), " gpu ms", gpu.toFixed(2), " draw calls", st["render.draw_calls"], " views", st["views"]);
' "$O/frost-trace-$N.json" "$O/racer-fperf-$N.json" "$N"
