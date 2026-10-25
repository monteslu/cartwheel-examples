#!/usr/bin/env bash
# games/racer/tools/fxseries.sh <name> <frame>...: the forced effect (RACER_FX, see fx.c) at several frames, cropped
# round the middle of the screen at native scale, in one contact sheet out/racer-fxs_<name>.png. The effect fires at
# tick 330 (frame 330 is its first), so 334 shows it 4 ticks old.
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$D/../../../out"
N="$1"; shift
files=()
for f in "$@"; do
  "$D/run.sh" "fxs_${N}_$f" "$f" RACER_MAP=-1 RACER_PLAYERS=1 RACER_FX="$N" >/dev/null
  convert "$R/racer-fxs_${N}_$f.png" -crop ${CROP:-960x540+480+270} +repage -gravity NorthWest -fill white -pointsize 22 -annotate +8+8 "$f" "$R/racer-fxs_${N}_$f.crop.png"
  files+=("$R/racer-fxs_${N}_$f.crop.png")
done
montage "${files[@]}" -tile 2x -geometry +2+2 "$R/racer-fxs_$N.png"
echo "$R/racer-fxs_$N.png"
