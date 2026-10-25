#!/usr/bin/env bash
# tools/dam_cam.sh <name> <x,y,z,tx,ty,tz[,fov]> [frames] [VAR=value ...]: the dam map seen from a still camera anywhere (RACER_DAM_CAM),
# written to out/racer-<name>.png. The race runs on its own meanwhile (all CPU karts); 345 frames is past the "GO!" banner.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
N="$1"; C="$2"; F="${3:-345}"; shift $(( $# > 3 ? 3 : $# ))
"$D/tools/run.sh" "$N" "$F" RACER_MAP=4 RACER_AUTO=1 RACER_DAM_CAM="$C" "$@" | grep -v "^{" | grep -v "^wrote"
