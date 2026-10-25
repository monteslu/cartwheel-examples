#!/usr/bin/env bash
# games/racer/tools/sheet.sh <map 0..7> <frame> [frame ...]: an all-CPU race on that map, a screenshot at each frame
# (the camera follows the leader), shrunk and joined into out/racer-sheet-<map>.png (two columns).
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
M="$1"; shift
parts=()
for f in "$@"; do
  "$D/tools/run.sh" "sh${M}_$f" "$f" RACER_MAP="$M" RACER_AUTO=1 ${SHEET_ENV:-} > /dev/null
  convert "$R/out/racer-sh${M}_$f.png" -resize 800x "$R/out/racer-sh${M}_$f.s.png"
  parts+=("$R/out/racer-sh${M}_$f.s.png")
done
montage "${parts[@]}" -tile 2x -geometry +4+4 -background '#101014' "$R/out/racer-sheet-$M.png"
echo "$R/out/racer-sheet-$M.png"
