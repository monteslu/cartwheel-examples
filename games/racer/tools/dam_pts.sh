#!/usr/bin/env bash
# tools/dam_pts.sh [step]: the dam course's road positions every `step` metres (default 25)
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="$D/../../deps/cartwheel-sdk/linux-x64"
mkdir -p "$D/../../out"
cc -O1 -Wno-unused -DMAPFILE="\"$D/maps/m5_dam.c\"" -I"$SDK/include" -I"$SDK/deps/three.c/include" -I"$SDK/deps/box3d/include" -I"$SDK/deps/box2d/include" \
  -Wl,--unresolved-symbols=ignore-all -o "$D/../../out/racer-dam-pts.bin" "$D/tools/dam_pts.c" "$D/track.c" -lm
"$D/../../out/racer-dam-pts.bin" "${1:-25}"
