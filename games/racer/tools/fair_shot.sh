#!/usr/bin/env bash
# games/racer/tools/fair_shot.sh <name> <x,y,z,tx,ty,tz[,fov]> [frames] [VAR=value ...]: a still of the fair map from a held camera
# (the map's RACER_FAIR_CAM dev aid), cropped of its HUD margins into out/fair-<name>.png
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
N="$1"; CAM="$2"; F="${3:-700}"; shift 3 2>/dev/null || shift $#
"$D/tools/run.sh" "fair-$N" "$F" RACER_MAP=2 RACER_AUTO=1 RACER_FAIR_CAM="$CAM" "$@" | grep -E "skipped|error" | head -3
echo "$D/../../out/racer-fair-$N.png"
