#!/usr/bin/env bash
# tools/refinery_shots.sh <s> [<s> ...]: one headless run per place on the refinery course (map 3). The CPU kart
# 0 is put on the road 70 m before s at 22 m/s when the lights go out and the shot is taken when it gets there,
# after the GO banner has gone. Writes out/racer-sh-<s>.png. RACER_PLAYERS=n and the like pass through the environment.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for s in "$@"; do
  from=$(( s > 75 ? s - 70 : 5 ))
  "$D/tools/run.sh" "sh-$s" 420 RACER_MAP=3 RACER_AUTO=1 RACER_VIEW=0 RACER_TEST_AT="$from:22" | grep -E "skipped|error" | head -2
done
