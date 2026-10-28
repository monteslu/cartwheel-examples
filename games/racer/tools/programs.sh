#!/usr/bin/env bash
# games/racer/tools/programs.sh [states-log...]: regenerate racer's three.c program tables (programs/racer_{gl,wgpu}.c)
# from programs/racer.txt plus the states a run reported missing (lines like "physical+senv+l1001+xcluster_rim" in a
# run's log). Goes through the engine's tools/programs.mjs, which composes the engine's shading parts (the rim light
# on the clustered lights is one program, xcluster_rim: three.c's generator alone cannot build it).
# Runs in the engine's GPU sandbox (the test GPU only, never the desktop).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
exec 8>"$ROOT/out/racer-programs.lock"
flock 8
LOGS="$ROOT/out/racer-programs-logs"
rm -rf "$LOGS" && mkdir -p "$LOGS"
for l in "$@"; do
  grep -E '^[a-z]+(\+[a-z0-9_]+)+$' "$l" >> "$LOGS/racer.log" || true
done
touch "$LOGS/racer.log"
cd "$ROOT"
CW_PROJECT_ROOT="$ROOT" WASMCART="${WASMCART:-$ROOT/../wasmcart}" \
  node deps/cartwheel-engine/tools/programs.mjs racer --logs "$LOGS" --no-run --build "$ROOT/build/native"
