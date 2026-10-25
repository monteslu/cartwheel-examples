#!/usr/bin/env bash
# tools/refinery_shots.sh <s[:speed]> [...]: one headless run per place on the refinery course (map 3). The CPU kart
# 0 is put on the road a little before s (speed m/s, default 22) when the lights go out, and the shot is taken
# when it gets there (frame 420, after the GO banner has gone). Writes out/racer-sh-<s>.png. Variables such as
# RACER_PLAYERS=n or RACER_REFINERY_CAM pass through the environment.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for at in "$@"; do
  s="${at%%:*}"
  v="${at#*:}"; [ "$v" = "$at" ] && v=22
  from=$(node -e "console.log(Math.max(5, Math.round($s - 3.2 * $v)))")
  "$D/tools/run.sh" "sh-$s" 420 RACER_MAP=3 RACER_AUTO=1 RACER_VIEW=0 RACER_TEST_AT="$from:$v" | grep -E "skipped|error" | head -2
done
