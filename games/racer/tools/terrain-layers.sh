#!/usr/bin/env bash
# games/racer/tools/terrain-layers.sh <map> <art dir>: a map's Terrain3D layers from photo material sets, packed the way
# Terrain3D reads them (cartwheel/terrain.h): <n>_albedo.png is albedo RGB + height A, <n>_normal.png is normal RGB
# (OpenGL, +Y) + roughness A, 512 x 512. Some sets are colour graded so they belong in the map. Writes
# games/racer/assets/terrain/<map>/. Credits: games/racer/CREDITS.
#
# <art dir> holds the downloads, one set per folder:
#   ambientcg/<Set>/<Set>_1K-JPG_{Color,Displacement,NormalGL,Roughness}.jpg   (CC0, https://ambientcg.com/license)
#     curl -L -o Rock029.zip 'https://ambientcg.com/get?file=Rock029_1K-JPG.zip' && unzip Rock029.zip -d Rock029
#   polyhaven-tex/<id>/{diff,disp,nor_gl,rough}.jpg                            (CC0, https://polyhaven.com/license)
#     the 1k jpg files the API lists for the texture (https://api.polyhaven.com/files/<id>)
set -euo pipefail
MAP="${1:?map (dam, salt, roads)}"
SRC="${2:?art dir}"
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$D/assets/terrain/$MAP"
mkdir -p "$OUT"

# layer <out name> <set> <grade: ImageMagick options for the albedo, or "">
layer() {
  local name="$1" set="$2" grade="$3" col disp nrm rgh p
  if [ -d "$SRC/polyhaven-tex/$set" ]; then
    p="$SRC/polyhaven-tex/$set"
    col="$p/diff.jpg" disp="$p/disp.jpg" nrm="$p/nor_gl.jpg" rgh="$p/rough.jpg"
  else
    p="$SRC/ambientcg/$set/${set}_1K-JPG"
    col="${p}_Color.jpg" disp="${p}_Displacement.jpg" nrm="${p}_NormalGL.jpg" rgh="${p}_Roughness.jpg"
  fi
  # shellcheck disable=SC2086
  convert "$col" -resize 512x512! $grade \( "$disp" -resize 512x512! -colorspace gray \) \
    -alpha off -compose CopyOpacity -composite -define png:color-type=6 "$OUT/${name}_albedo.png"
  # roughness lifted into 0.8..1 (dry ground is matte: as scanned, 0.46..0.55 turned a sunlit slope into a white sheen)
  convert "$nrm" -resize 512x512! \( "$rgh" -resize 512x512! -colorspace gray -fx '0.8+0.2*u' \) \
    -alpha off -compose CopyOpacity -composite -define png:color-type=6 "$OUT/${name}_normal.png"
  echo "$MAP $name <- $set"
}

case "$MAP" in
dam)
  layer 0_cliff Rock029 ""                                    # red sandstone as it comes
  layer 1_band Rock029 "-modulate 78,125,98"                  # a darker, deeper red stratum
  layer 2_floor Ground031 "-colorspace gray -sigmoidal-contrast 3,50% -fill #8a3c1c -tint 100 -modulate 78,100,100" # cracked earth, red dust
  layer 3_dirt Ground105 "-modulate 95,85,94"                 # dusty brown earth, warmed
  layer 4_scrub Ground037 "-modulate 82,62,102"               # grass and dirt for the brush
  layer 5_sand Ground080 "-modulate 100,90,97"                # river banks and washes
  layer 6_gravel Ground110 "-modulate 105,60,100"             # the road's verges
  layer 7_concrete Concrete034 "-modulate 78,40,100"          # the dam's faces
  ;;
salt)
  layer 0_crust dry_ground_01 "-colorspace gray -auto-level -sigmoidal-contrast 4,35% -level 0,68% -fill #ffe6dc -colorize 9" # the crust's polygons, white
  layer 1_salt Gravel023 "-modulate 104,60,100"               # bright crystalline salt
  layer 2_plateau dry_ground_rocks "-modulate 100,80,100"     # the plateau's stony ground
  layer 3_fan gravelly_sand "-modulate 102,75,100"            # the alluvial fans below the hills
  layer 4_rock Rock051 "-modulate 88,22,120 -fill #6e5a7a -colorize 12"                  # the range: grey-violet rock
  layer 5_rock2 Rock052 "-modulate 96,70,100"                 # buff rock, the lower slopes
  layer 6_scarp Rock029 "-modulate 96,85,100"                 # the escarpment's rust rock
  layer 7_gravel Ground110 "-modulate 108,50,100"             # the road's verges
  ;;
roads)
  # photo bases for the road textures (textures.c photo_get): the surface under each generator's own paint
  layer concrete concrete_floor_worn_001 "-modulate 100,60,100"  # the Dam's concrete
  layer asphalt asphalt_track ""                               # race-track asphalt
  layer asphalt_old asphalt_02 ""                              # older, cracked asphalt
  ;;
*) echo "terrain-layers.sh: unknown map $MAP" >&2; exit 2 ;;
esac
