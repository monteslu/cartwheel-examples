#!/usr/bin/env bash
# games/racer/tools/frost_shot.sh <name> <s> [speed] [frames] [VAR=value ...]: a one-player view of Frostbite Pass with
# kart 0 put on the road at distance <s> metres, moving, when the lights go out (RACER_TEST_AT). Writes
# out/racer-<name>.png through tools/run.sh. Frame 225 is the lights going out, so the default 240 shows
# the kart about a quarter of a second after it was placed.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
N="$1"; S="$2"; SP="${3:-30}"; F="${4:-240}"; shift $(( $# < 4 ? $# : 4 ))
exec "$D/tools/run.sh" "$N" "$F" RACER_MAP=5 RACER_VIEW=0 RACER_AUTO=1 RACER_TEST_AT="$S:$SP" "$@"
