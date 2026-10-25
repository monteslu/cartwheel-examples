#!/usr/bin/env bash
# games/racer/tools/programs.sh [states-log...]: regenerate racer's three.c program tables (programs/racer_{gl,wgpu}.c)
# from programs/racer.txt plus the states a run reported missing (lines like "physical+senv+l1001+xcluster" in a run's log).
# Runs in the engine's GPU sandbox (the test GPU only, never the desktop).
set -euo pipefail
exec 8>"$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/out/racer-programs.lock"
flock 8
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
E="$D/../../deps/cartwheel-engine"
EXT=()
for f in "$E"/src/*/extensions/[a-z0-9]*.mjs; do EXT+=(--extend "$f"); done
MISS=()
for l in "$@"; do
  grep -E '^[a-z]+(\+[a-z0-9]+)+$' "$l" >> "$D/programs/racer.txt" || true
done
awk '!seen[$0]++' "$D/programs/racer.txt" > "$D/programs/racer.txt.new" && mv "$D/programs/racer.txt.new" "$D/programs/racer.txt"
WASMCART="${WASMCART:-$D/../../../wasmcart}" node "$E/tools/with-gpu-suite.mjs" racer-programs \
  node "${CW_THREE_DIR:-$D/../../../three.c}/tools/gen-project-table.mjs" --name racer --out "$D/programs" --states "$D/programs/racer.txt" \
  --cache "${CW_PROGRAMS_CACHE:-$E/build/native/programs-cache/racer-3c}" --minimize "${EXT[@]}"
