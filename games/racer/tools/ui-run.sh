#!/usr/bin/env bash
# games/racer/tools/ui-run.sh <name> <frames> <input.json|-> [VAR=value ...]: like run.sh, with a scripted input
# (see run-cart.mjs: [{ "frame": 30, "buttons": ["south"] }, ...]; keys, mouse, pad2.. too). The scripts of the
# front end's screens are in games/racer/tests/. Writes out/racer-<name>.png/.json/.log.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
N="$1"; F="$2"; IN="$3"; shift 3
for kv in "$@"; do export "$kv"; done
cd "$R"
BIN="build/native/games/racer/racer"
ARGS=()
[ -n "${TOUCH:-}" ] && export CW_TOUCH_PRIMARY=1
[ "$IN" != "-" ] && ARGS+=(--input "$IN")
timeout "${RUN_TIMEOUT:-300}" node deps/cartwheel-engine/tools/run-cart.mjs --host native --app racer --exe "$BIN" --frames "$F" \
  --shot "out/racer-$N.png" --state "out/racer-$N.json" --log "out/racer-$N.log" "${ARGS[@]}" ${GPU:+--gpu $GPU} 2>&1 | tail -3
grep -E "\[error\]|racer:" "$R/out/racer-$N.log" | head -8
echo "wrote $R/out/racer-$N.png"
