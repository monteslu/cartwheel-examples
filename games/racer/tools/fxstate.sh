#!/usr/bin/env bash
# games/racer/tools/fxstate.sh <frame> <state>...: a forced kart state (RACER_FX=s_dust s_snow s_mist s_stars s_flame
# s_drift3 s_smoke s_oil s_trick, see fx.c) on the player's kart, cropped round it at native scale into
# out/racer-fxstate.png (two columns).
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$D/../../../out"
F="$1"; shift
files=()
for n in "$@"; do
  "$D/run.sh" "fxst_$n" "$F" RACER_MAP=-1 RACER_PLAYERS=1 RACER_FX="$n" >/dev/null
  convert "$R/racer-fxst_$n.png" -crop ${CROP:-760x430+580+400} +repage -gravity NorthWest -fill white -pointsize 22 -annotate +8+8 "$n" "$R/racer-fxst_$n.crop.png"
  files+=("$R/racer-fxst_$n.crop.png")
done
montage "${files[@]}" -tile 2x -geometry +2+2 "$R/racer-fxstate.png"
echo "$R/racer-fxstate.png"
