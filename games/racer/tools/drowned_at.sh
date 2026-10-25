#!/usr/bin/env bash
# games/racer/tools/drowned_at.sh <name> <s metres> [speed m/s] [frames after the lights] [VAR=value ...]: a one-player view of
# map 2 with kart 0 put at <s> along the course, running at <speed> (default 22); writes out/racer-<name>.png
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
N="$1"; S="$2"; V="${3:-22}"; F="${4:-30}"; shift 4 2>/dev/null || shift $#
"$D/tools/run.sh" "$N" "$((225 + F))" RACER_MAP=1 RACER_AUTO=1 RACER_VIEW=0 RACER_TEST_AT="$S:$V" "$@" | grep -v "^wrote" | grep -v "three.c skipped [0-9]* draw"
echo "image $D/../../out/racer-$N.png"
