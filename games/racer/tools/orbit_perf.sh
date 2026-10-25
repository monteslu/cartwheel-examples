#!/usr/bin/env bash
# games/racer/tools/orbit_perf.sh [map] [s] [players]: CPU time of the main thread's frame zones over 60 frames of the Wreck Orbit
# map (or another map), from the engine's own trace, and the draw call count of the last frame.
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
MAP="${1:-7}"; S="${2:-1370}"; PL="${3:-1}"
[ "$PL" -gt 1 ] && AUTO="" || AUTO="RACER_AUTO=1"; "$D/tools/run.sh" "perf$MAP" 420 RACER_MAP="$MAP" $AUTO RACER_VIEW=0 RACER_PLAYERS="$PL" RACER_TEST_AT="$S:30" CW_GPU_TIMERS=1 CW_PROFILE_TRACE="$R/out/orbit-trace-$MAP.json" CW_PROFILE_TRACE_START=330 CW_PROFILE_TRACE_FRAMES=60 > /dev/null 2>&1
node -e '
const fs=require("fs"); const t=JSON.parse(fs.readFileSync(process.argv[1])); const ev=t.traceEvents||t; const by={};
for(const e of ev){ if(e.ph==="X"){ by[e.name]=by[e.name]||{n:0,d:0}; by[e.name].n++; by[e.name].d+=e.dur; } }
let gpu=0, gn=0; const g={};
for(const e of ev){ if(e.ph==="X"&&e.cat==="gpu"){ g[e.name]=g[e.name]||{n:0,d:0}; g[e.name].n++; g[e.name].d+=e.dur; } }
for(const [k,v] of Object.entries(g)){ gpu+=v.d/v.n/1000; }
console.log("gpu ms per frame (sum of passes):",gpu.toFixed(2),"  ",Object.entries(g).map(([k,v])=>k+" "+(v.d/v.n/1000).toFixed(2)).join(", "));
const pick=["frame","render","fixed_update"];
for(const k of pick) if(by[k]) console.log("cpu",k.padEnd(14),(by[k].d/by[k].n/1000).toFixed(2),"ms"); ' "$R/out/orbit-trace-$MAP.json"
grep -o '"render.draw_calls":[0-9]*' "$R/out/racer-perf$MAP.json"
