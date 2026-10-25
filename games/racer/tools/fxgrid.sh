#!/usr/bin/env bash
# games/racer/tools/fxgrid.sh <frame> <name>...: each forced effect (RACER_FX, see fx.c) at one frame, cropped at
# native scale round the player's kart, into out/racer-fxgrid.png (two columns).
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$D/../../../out"
F="$1"; shift
files=()
for n in "$@"; do
  "$D/run.sh" "fxg_$n" "$F" RACER_MAP=-1 RACER_PLAYERS=1 RACER_FX="$n" >/dev/null
  convert "$R/racer-fxg_$n.png" -crop ${CROP:-760x430+580+400} +repage -gravity NorthWest -fill white -pointsize 22 -annotate +8+8 "$n" "$R/racer-fxg_$n.crop.png"
  files+=("$R/racer-fxg_$n.crop.png")
done
montage "${files[@]}" -tile 2x -geometry +2+2 "$R/racer-fxgrid.png"
echo "$R/racer-fxgrid.png"
