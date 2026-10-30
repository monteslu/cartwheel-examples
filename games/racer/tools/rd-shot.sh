#!/usr/bin/env bash
# games/racer/tools/rd-shot.sh <name> <frames> [debug command ...]: run the racer CART on romdev (the dual GL + WebGPU
# cart, build/cart-wgpu/carts/racer.wasc, WebGPU) for N frames and write out/racer-rd-<name>.png, .json and .log.
# Debug commands go in at load (a cart has no environment): "race 4 0 1" is map 4, no humans, so the camera follows
# the leader. Through the machine-wide GPU suite lock, like tools/run.sh.
#   tools/rd-shot.sh dam 900 "race 4 0 1"
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
N="$1"; F="$2"; shift 2
CMDS="$(IFS=';'; echo "$*")"
cd "$R"
export CW_RUN_TIMEOUT_MS="${CW_RUN_TIMEOUT_MS:-900000}"
timeout "${RUN_TIMEOUT:-1000}" node deps/cartwheel-engine/tools/with-gpu-suite.mjs racer-rd \
  node deps/cartwheel-engine/tools/run-cart.mjs --host romdev --gpu webgpu --app racer --cart build/cart-wgpu/carts/racer.wasc --frames "$F" \
  ${CMDS:+--start-cmds "$CMDS"} --shot "out/racer-rd-$N.png" --state "out/racer-rd-$N.json" --log "out/racer-rd-$N.log" 2>&1 | tail -3
grep -E "skipped|NOT DRAWN|\[error\]" "$R/out/racer-rd-$N.log" | head -8
echo "wrote $R/out/racer-rd-$N.png"
