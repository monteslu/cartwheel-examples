#!/usr/bin/env bash
# games/racer/tools/fair_at.sh <out name> <s,back,up,ahead[,fov[,lat]]> ...: stills from a camera standing on the fair course
# (the map's RACER_FAIR_AT dev aid: behind s by `back` metres, `up` above the road, looking `ahead` metres on), tiled 2 wide
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
O="$D/../../out"
N="$1"; shift
i=0; files=()
for spec in "$@"; do
  "$D/tools/run.sh" "fair-a$i" "${FAIR_FRAMES:-700}" RACER_MAP=2 RACER_AUTO=1 RACER_FAIR_AT="$spec" 2>&1 | grep -E "error|skipped" | head -3
  convert "$O/racer-fair-a$i.png" -crop 1280x720+320+120 -resize 800x450 "$O/fair-a$i.png"
  files+=("$O/fair-a$i.png"); i=$((i+1))
done
montage "${files[@]}" -tile 2x -geometry +2+2 "$O/fair-$N.png"
echo "$O/fair-$N.png"
