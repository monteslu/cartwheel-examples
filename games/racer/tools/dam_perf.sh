#!/usr/bin/env bash
# tools/dam_perf.sh <name> <s metres> [players] [VAR=value ...]: the dam map's GPU time at one place: kart 0 dropped at s, 30 m/s, the pipeline's
# per-pass milliseconds read 60 frames later (CW_GPU_TIMERS). Prints the total and the passes over 0.15 ms.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
N="$1"; S="$2"; P="${3:-1}"; shift $(( $# > 3 ? 3 : $# ))
for kv in "$@"; do export "$kv"; done
cd "$R"
CW_GPU_TIMERS=1 RACER_MAP=${PERF_MAP:-4} RACER_AUTO=1 RACER_VIEW=0 RACER_PLAYERS="$P" RACER_TEST_AT="$S:30" timeout 200 node deps/cartwheel-engine/tools/run-cart.mjs --host native --app racer \
  --exe "${RACER_EXE:-build/native/games/racer/racer}" --frames 330 --shot "out/racer-$N.png" --state "out/racer-$N.json" --log "out/racer-$N.log" --start-cmds "@320 pipeline" > /dev/null 2>&1
grep "^start cmd: pipeline" "out/racer-$N.log" | sed 's/^start cmd: pipeline -> //' | node -e '
const d = JSON.parse(require("fs").readFileSync(0, "utf8"));
const top = d.passes.filter((p) => p.gpu_ms >= 0.15).map((p) => p.name + " " + p.gpu_ms.toFixed(2)).join(", ");
console.log(process.argv[1] + ": gpu " + d.gpu_ms.toFixed(2) + " ms  (" + top + ")");' "$N s=$S players=$P"
