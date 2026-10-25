#!/usr/bin/env bash
# games/racer/tools/prof.sh <map 0..7> <players 1..4>: the profiler's view of a 120-frame stretch (frames 600 on): the CPU frame, the render
# passes' CPU submission and the GPU lane, in ms per frame, plus the draw and triangle counts. Steadier than perf.sh's wall clock.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
M="$1"; P="${2:-1}"
"$D/tools/run.sh" "prof$M$P" 900 RACER_MAP="$M" RACER_PLAYERS="$P" CW_PROFILE=1 CW_PROFILE_GPU=1 CW_PROFILE_TRACE="$R/out/prof$M$P.json" CW_PROFILE_TRACE_FRAMES=120 CW_PROFILE_TRACE_START=600 > /dev/null 2>&1
node -e "
const fs=require('fs');const t=JSON.parse(fs.readFileSync('$R/out/prof$M$P.json'));const ev=(t.traceEvents||t).filter(e=>e.ph==='X');
const sum=(re)=>ev.filter(e=>re.test(e.name+'@'+e.tid)).reduce((a,e)=>a+e.dur,0)/120/1000;
const gpu=ev.filter(e=>e.tid===1000).reduce((a,e)=>a+e.dur,0)/120/1000;
const d=JSON.parse(fs.readFileSync('$R/out/racer-prof$M$P.json'));
console.log('map $M p$P: cpu frame',sum(/^frame@0$/).toFixed(2),'ms  depth_prepass',sum(/^pass depth_prepass@0$/).toFixed(2),' scene',sum(/^pass scene@0$/).toFixed(2),' lights',sum(/^pass lights@0$/).toFixed(2),' | gpu lane',gpu.toFixed(2),'ms | draws',d['render.draws'],'tris',d['render.tris']);"
