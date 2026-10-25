#!/usr/bin/env bash
# tools/refinery_perf.sh <name> [VAR=value ...]: a headless refinery run with the profiler on (GPU time per pass) and
# its numbers, e.g.  tools/refinery_perf.sh one RACER_TEST_AT=700:22   or  tools/refinery_perf.sh four RACER_PLAYERS=4
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
N="$1"; shift
T="$D/../../out/racer-perf-$N.trace.json"
"$D/tools/run.sh" "perf-$N" 420 RACER_MAP=3 RACER_AUTO=1 CW_PROFILE_TRACE="$T" CW_PROFILE_GPU=1 CW_PROFILE_TRACE_START=330 CW_PROFILE_TRACE_FRAMES=60 "$@" | grep -E "skipped|error"
node "$D/tools/refinery_perf.mjs" "$T"
node -e 'const d=JSON.parse(require("fs").readFileSync(process.argv[1],"utf8"));console.log("draw calls",d["render.draw_calls"])' "$D/../../out/racer-perf-$N.json" 2>/dev/null || true
