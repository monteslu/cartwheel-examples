#!/usr/bin/env bash
# games/racer/tools/trial_ref.sh [map ...]: the reference lap of each map for the time trial's medals. The Ace CPU driver
# (the bot, kart 0) drives one lap from the grid at the Standard class; this prints the time in centiseconds, one per map,
# in the form to paste into TRIAL_REF_CS (ui_modes.inc). Re-run it when the physics or a track changes.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
MAPS=("$@")
[ ${#MAPS[@]} -eq 0 ] && MAPS=(0 1 2 3 4 5 6 7)
for m in "${MAPS[@]}"; do
  "$D/tools/run.sh" "ref$m" "${REF_FRAMES:-9000}" RACER_MAP="$m" RACER_MODE=trial RACER_BOT=1 RACER_RUNS=0 >/dev/null 2>&1
  node -e '
    const d = JSON.parse(require("fs").readFileSync(process.argv[1] + "/out/racer-ref" + process.argv[2] + ".json", "utf8"));
    console.log("map", process.argv[2], "last_cs", d["trial.last_cs"], "runs", d["trial.runs"]);
  ' "$R" "$m"
done
