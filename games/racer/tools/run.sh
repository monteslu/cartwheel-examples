#!/usr/bin/env bash
# games/racer/tools/run.sh <name> <frames> [VAR=value ...]: run racer headless on the test GPU for N frames (60 per
# second of game time) and write out/racer-<name>.png (the last frame), .json (the named debug state) and .log.
# Examples:
#   tools/run.sh a 700 RACER_MAP=2 RACER_AUTO=1            map 2, CPUs only, 700 frames in
#   tools/run.sh b 500 RACER_MAP=0 RACER_PLAYERS=4         four views
#   tools/run.sh c 900 RACER_MAP=1 RACER_AUTO=1 RACER_VIEW=3 RACER_CAM=top     (see the env list at the top of main.c)
# It prints the path of what it wrote and any "skipped draws" warning (a material state missing from the program
# table: run tools/programs.sh <name>.log).
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
N="$1"; F="$2"; shift 2
for kv in "$@"; do export "$kv"; done
cd "$R"
BIN="build/native/games/racer/racer"
[ -n "${RACER_AUTOBUILD:-}" ] && BIN="build/native/games/racer/racer_auto"
timeout "${RUN_TIMEOUT:-300}" node deps/cartwheel-engine/tools/run-cart.mjs --host native --app racer --exe "$BIN" --frames "$F" \
  --shot "out/racer-$N.png" --state "out/racer-$N.json" --log "out/racer-$N.log" ${GPU:+--gpu $GPU} 2>&1 | tail -3
grep -E "skipped|\[warn\]|\[error\]" "$R/out/racer-$N.log" | head -8
echo "wrote $R/out/racer-$N.png $R/out/racer-$N.json $R/out/racer-$N.log"
