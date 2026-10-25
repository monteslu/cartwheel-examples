#!/usr/bin/env bash
# games/racer/tools/build.sh: build racer (native) and print only errors and warnings. Several people build in
# one tree, so builds take a lock; it also keeps the program tables in step (see programs.sh).
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
R="$D/../.."
mkdir -p "$R/out"
exec 9>"$R/out/racer-build.lock"
flock 9
cd "$R"
TARGETS="${TARGETS:-native}" scripts/build.sh racer racer_auto 2>&1 | grep -E "error|warning|FAILED|undefined reference|Error" | head -${LINES_MAX:-40}
exit "${PIPESTATUS[0]}"
