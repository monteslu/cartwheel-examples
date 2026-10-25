#!/usr/bin/env bash
# tools/tracktest.sh <map file> [out prefix]: build the map's track on the host and plot it
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="$D/../../deps/cartwheel-sdk/linux-x64"
OUT="${2:-$D/../../out/racer-track}"
mkdir -p "$(dirname "$OUT")"
cc -O1 -g -Wall -Wno-unused -DMAPFILE="\"$1\"" -I"$SDK/include" -I"$SDK/deps/three.c/include" -I"$SDK/deps/box3d/include" -I"$SDK/deps/box2d/include" \
  -Wl,--unresolved-symbols=ignore-all -o "$OUT.bin" "$D/tools/tracktest.c" "$D/track.c" -lm
"$OUT.bin" "$OUT"
for k in top side; do convert "$OUT-$k.ppm" "$OUT-$k.png"; rm -f "$OUT-$k.ppm"; done
echo "wrote $OUT-top.png $OUT-side.png"
