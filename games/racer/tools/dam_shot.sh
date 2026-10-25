#!/usr/bin/env bash
# tools/dam_shot.sh <name> <s metres along the track> [speed m/s] [frames] [VAR=value ...]: one-player view of the dam map with
# kart 0 dropped on the road at s (default 30 m/s, 245 frames), written to out/racer-<name>.png (HUD and all)
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
N="$1"; S="$2"; SP="${3:-30}"; F="${4:-340}"; shift $(( $# > 4 ? 4 : $# ))
"$D/tools/run.sh" "$N" "$F" RACER_MAP=4 RACER_AUTO=1 RACER_VIEW=0 RACER_TEST_AT="$S:$SP" "$@" | grep -v "^{" | grep -v "^wrote"
