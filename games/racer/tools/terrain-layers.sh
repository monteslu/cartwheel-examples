#!/usr/bin/env bash
# games/racer/tools/terrain-layers.sh <ambientcg dir>: the Dam's Terrain3D layers from ambientCG material sets (CC0,
# https://ambientcg.com/license), packed the way Terrain3D reads them (cartwheel/terrain.h): <n>_albedo.png is
# albedo RGB + height A, <n>_normal.png is normal RGB (OpenGL, +Y) + roughness A, 512 x 512. Some sets are colour
# graded here so they belong in a red sandstone canyon. The sources are the 1K-JPG downloads, unzipped one set per
# folder:  curl -L -o Rock029.zip 'https://ambientcg.com/get?file=Rock029_1K-JPG.zip' && unzip Rock029.zip -d Rock029
# Writes games/racer/assets/terrain/dam/. Credits: games/racer/CREDITS.
set -euo pipefail
SRC="${1:?ambientcg dir}"
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$D/assets/terrain/dam"
mkdir -p "$OUT"

# layer <out name> <set> <grade: ImageMagick options for the albedo, or "">
layer() {
  local name="$1" set="$2" grade="$3" p="$SRC/$2/$2_1K-JPG"
  # shellcheck disable=SC2086
  convert "${p}_Color.jpg" -resize 512x512! $grade \( "${p}_Displacement.jpg" -resize 512x512! -colorspace gray \) \
    -alpha off -compose CopyOpacity -composite -define png:color-type=6 "$OUT/${name}_albedo.png"
  # roughness lifted into 0.8..1 (dry ground is matte: as scanned, 0.46..0.55 turned a sunlit slope into a white sheen)
  convert "${p}_NormalGL.jpg" -resize 512x512! \( "${p}_Roughness.jpg" -resize 512x512! -colorspace gray -fx '0.8+0.2*u' \) \
    -alpha off -compose CopyOpacity -composite -define png:color-type=6 "$OUT/${name}_normal.png"
  echo "$name <- $set"
}

# 0 cliff: red sandstone as it comes
layer 0_cliff Rock029 ""
# 1 cliff band: the same rock, a darker, deeper red stratum
layer 1_band Rock029 "-modulate 78,125,98"
# 2 floor: cracked desert earth, regraded from grey to the canyon's red dust
layer 2_floor Ground031 "-colorspace gray -sigmoidal-contrast 3,50% -fill #8a3c1c -tint 100 -modulate 78,100,100"
# 3 dirt: dusty brown earth, warmed toward red
layer 3_dirt Ground105 "-modulate 95,85,94"
# 4 scrub: grass and dirt for the patches of brush
layer 4_scrub Ground037 "-modulate 82,62,102"
# 5 sand: river banks and washes
layer 5_sand Ground080 "-modulate 100,90,97"
# 6 gravel: the road's verges
layer 6_gravel Ground110 "-modulate 105,60,100"
# 7 concrete: the dam's faces and the spillway's channel
layer 7_concrete Concrete034 "-modulate 78,40,100"
