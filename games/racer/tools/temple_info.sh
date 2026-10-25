#!/usr/bin/env bash
# tools/temple_info.sh [every n samples]: where each piece of the temple course starts (for placing set pieces)
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="$D/../../deps/cartwheel-sdk/linux-x64"
OUT="$D/../../out/temple_info"
cc -O1 -DMAPFILE="\"$D/maps/m7_temple.c\"" -I"$SDK/include" -I"$SDK/deps/three.c/include" -I"$SDK/deps/box3d/include" -I"$SDK/deps/box2d/include" \
  -Wl,--unresolved-symbols=ignore-all -o "$OUT.bin" "$D/tools/temple_info.c" "$D/track.c" -lm 2>&1 | grep -E "error" || true
"$OUT.bin" "$@"
