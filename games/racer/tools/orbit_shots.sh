#!/usr/bin/env bash
# games/racer/tools/orbit_shots.sh <name:s[:frames]> ...: run the Wreck Orbit map once per place and keep one screenshot each as
# out/racer-orbit-<name>.png. s is the distance along the lap you want to LOOK at: the kart is put 100 m earlier at 30 m/s
# (so the lights-out banner has gone by the time the frame is taken). frames defaults to 420 (the lights go out at 225).
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LEN="${LAPLEN:-3022}"
for spec in "$@"; do
  IFS=: read -r name s frames <<<"$spec"
  from=$(( (s - ${LEAD:-100} + LEN) % LEN ))
  "$D/tools/run.sh" "orbit-$name" "${frames:-420}" RACER_MAP=7 RACER_AUTO=1 RACER_VIEW=0 RACER_TEST_AT="$from:${SPEED:-30}" | grep -E "skipped|error" | head -3
done
