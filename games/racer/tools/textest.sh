#!/usr/bin/env bash
# tools/textest.sh <kind number> [map 0..4] [tiles]: build racer's texture generator on the host and write
# out/racer-tex-<kind>-<map>.png (map: 0 albedo, 1 normal, 2 roughness, 3 occlusion, 4 emissive). Kinds: textures.h order.
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="$D/../../deps/cartwheel-sdk/linux-x64"
mkdir -p "$D/../../out"
O="$D/../../out/racer-tex-${1}-${2:-0}"
BIN="$D/../../out/racer-textest.bin"
cc -O2 -Wall -Wno-unused -I"$SDK/include" -I"$SDK/deps/three.c/include" -Wl,--unresolved-symbols=ignore-all -o "$BIN" "$D/tools/textest.c" -lm
"$BIN" "$1" "${2:-0}" "$O.ppm" "${3:-1}"
convert "$O.ppm" "$O.png"; rm -f "$O.ppm"
echo "wrote $O.png"
