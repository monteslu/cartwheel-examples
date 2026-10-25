#!/usr/bin/env bash
# games/racer/tools/fair_tile.sh <out name> <cam> <cam> ...: several held-camera stills of the fair map, centre-cropped and tiled 2 wide
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
O="$D/../../out"
N="$1"; shift
i=0; files=()
for cam in "$@"; do
  "$D/tools/run.sh" "fair-t$i" 700 RACER_MAP=2 RACER_AUTO=1 RACER_FAIR_CAM="$cam" ${FAIR_ENV:-} 2>&1 | grep -E "error|skipped" | head -3
  convert "$O/racer-fair-t$i.png" -crop 1280x720+320+120 -resize 800x450 "$O/fair-t$i.png"
  files+=("$O/fair-t$i.png"); i=$((i+1))
done
montage "${files[@]}" -tile 2x -geometry +2+2 "$O/fair-$N.png"
echo "$O/fair-$N.png"
