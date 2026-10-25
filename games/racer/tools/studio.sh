#!/usr/bin/env bash
# games/racer/tools/studio.sh <vehicle 0..5> [colour 0..7] [frames]: render one vehicle from eight angles on the grid and
# write a contact sheet out/studio-<vehicle>.png (azimuths: 0 behind, 90 left, 180 front, 270 right; near and far).
# Uses RACER_STUDIO / RACER_STUDIO_CAM (see main.c); needs the racer build.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
V="${1:-0}"; C="${2:-0}"; F="${3:-200}"
OUT="$R/out/studio"
mkdir -p "$OUT"
i=0
for cam in "0,4.2,1.5" "45,4.2,1.5" "90,4.2,1.3" "135,4.2,1.5" "180,4.2,1.4" "315,4.2,1.5" "30,2.6,1.1" "160,9,2.5"; do
  "$D/tools/run.sh" "s${V}_$i" "$F" RACER_MAP=0 RACER_AUTO=1 RACER_STUDIO="$V" RACER_COLOUR="$C" RACER_VIEW=0 "RACER_STUDIO_CAM=$cam" "${@:4}" >/dev/null 2>&1
  cp "$R/out/racer-s${V}_$i.png" "$OUT/s${V}_$i.png"
  i=$((i+1))
done
montage "$OUT"/s${V}_[0-7].png -tile 4x2 -geometry 640x360+2+2 "$R/out/studio-$V.png"
echo "wrote $R/out/studio-$V.png"
