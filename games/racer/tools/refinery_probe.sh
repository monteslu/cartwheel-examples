#!/usr/bin/env bash
# tools/refinery_probe.sh: build the refinery's track on the host and print where its set pieces fall
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="$D/../../deps/cartwheel-sdk/linux-x64"
OUT="$D/../../out/racer-refinery-probe"
cc -O1 -Wall -Wno-unused -I"$SDK/include" -I"$SDK/deps/three.c/include" -I"$SDK/deps/box3d/include" -I"$SDK/deps/box2d/include" \
  -Wl,--unresolved-symbols=ignore-all -o "$OUT.bin" "$D/tools/refinery_probe.c" "$D/track.c" -lm
"$OUT.bin"
