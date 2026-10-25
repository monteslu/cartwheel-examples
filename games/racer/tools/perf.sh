#!/usr/bin/env bash
# games/racer/tools/perf.sh <map 0..7> <players 1..4>: wall milliseconds per frame of an uncapped headless run
# (frames 600 to 1800 of the race: 375 s of start, countdown and the first stretch), the cost of the scene as
# a whole (CPU and GPU together). Players above 1 run that many split views with parked karts.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
M="$1"; P="${2:-1}"
t() { local s=$(date +%s.%N); "$D/tools/run.sh" "perf$M" "$1" RACER_MAP="$M" RACER_PLAYERS="$P" > /dev/null; echo "$(date +%s.%N) - $s" | bc; }
a=$(t 600); b=$(t 1800)
echo "map $M players $P: $(echo "($b - $a) / 1200 * 1000" | bc -l | cut -c1-5) ms/frame"
