#!/usr/bin/env bash
# tools/refinery_check.sh: compile only the refinery map for errors and warnings (no link, no shared build tree)
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="$D/../../deps/cartwheel-sdk/linux-x64"
cc -std=gnu17 -fsyntax-only -Wall -I"$SDK/include" -I"$SDK/deps/three.c/include" -I"$SDK/deps/box3d/include" -I"$SDK/deps/box2d/include" "$D/maps/m4_refinery.c"
