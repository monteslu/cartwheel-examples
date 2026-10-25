#!/usr/bin/env bash
# games/racer/tools/drowned_run.sh <name> <frames> [VAR=value ...]: build, run map 2 (RACER_MAP=1) headless and, when
# the run reports material states missing from the program table, add them (tools/programs.sh), rebuild and run again.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
N="$1"; F="$2"; shift 2
"$D/tools/build.sh" 2>&1 | tail -20
for pass in 1 2 3; do
  "$D/tools/run.sh" "$N" "$F" RACER_MAP=1 RACER_AUTO=1 "$@" | grep -v "^wrote" | grep -v "three.c skipped [0-9]* draw"
  if grep -qE "^[a-z]+(\+[a-z0-9_]+)+$" "$R/out/racer-$N.log"; then
    "$D/tools/programs.sh" "$R/out/racer-$N.log" 2>&1 | tail -1
    "$D/tools/build.sh" 2>&1 | tail -20
  else
    break
  fi
done
echo "image $R/out/racer-$N.png"
