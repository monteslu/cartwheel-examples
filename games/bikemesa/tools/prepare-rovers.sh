#!/usr/bin/env bash
# Run from cartwheel-examples. Requires glTF Transform CLI 4.5.1.
# These are review candidates, not automatically shipped assets.
set -euo pipefail
rover_cli=${ROVER_GLTF_CLI:-gltf-transform}
rover_dir=games/bikemesa/references/rovers
mkdir -p "$rover_dir/prepared"
for rover_name in curiosity spirit-opportunity perseverance; do
  "$rover_cli" optimize "$rover_dir/$rover_name.glb" "$rover_dir/prepared/$rover_name.glb" \
    --compress false --instance false --simplify-ratio .12 \
    --simplify-error .002 --texture-size 512
done
# Reviewed MER runtime candidate: shared by Spirit and Opportunity.
mkdir -p games/bikemesa/assets/models
"$rover_cli" optimize "$rover_dir/spirit-opportunity.glb" games/bikemesa/assets/models/mer.glb \
  --compress false --instance false --simplify-ratio .18 \
  --simplify-error .01 --texture-size 256

"$rover_cli" optimize "$rover_dir/curiosity.glb" games/bikemesa/assets/models/curiosity.glb \
  --compress false --instance false --simplify-ratio .18 \
  --simplify-error .01 --texture-size 256

"$rover_cli" optimize "$rover_dir/perseverance.glb" "$rover_dir/prepared/perseverance-runtime.glb" \
  --compress false --instance false --simplify-ratio .1 \
  --simplify-error .01 --texture-size 256
node games/bikemesa/tools/finish-perseverance.mjs "$rover_dir/prepared/perseverance-runtime.glb" games/bikemesa/assets/models/perseverance.glb
