#!/usr/bin/env bash
# games/racer/tools/fair_drive.sh <out name> <s:speed:frames> ...: the normal chase view of kart 0 put at s on the fair course when the lights go
# out (frame 225) and left to drive until `frames`, tiled 2 wide (every kart is a CPU driver)
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
O="$D/../../out"
N="$1"; shift
i=0; files=()
for spec in "$@"; do
  IFS=: read -r s sp fr <<< "$spec"
  "$D/tools/run.sh" "fair-d$i" "${fr:-300}" RACER_MAP=2 RACER_AUTO=1 RACER_VIEW=0 RACER_TEST_AT="$s:${sp:-30}" 2>&1 | grep -E "error|skipped" | head -3
  convert "$O/racer-fair-d$i.png" -crop 1280x720+320+130 -resize 800x450 "$O/fair-d$i.png"
  files+=("$O/fair-d$i.png"); i=$((i+1))
done
montage "${files[@]}" -tile 2x -geometry +2+2 "$O/fair-$N.png"
echo "$O/fair-$N.png"
