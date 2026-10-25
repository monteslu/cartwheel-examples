#!/usr/bin/env bash
# games/racer/tools/lapcheck.sh <map 0..7 | -1> [laps] [frames]: an all-CPU race on that map, then what happened:
# laps done, falls (respawns), spin-outs, the best lap and the finish. A track that CPU drivers cannot
# drive is not ready: expect every kart to finish with a few falls at most.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
M="${1:-0}"; L="${2:-2}"; F="${3:-9000}"
"$D/tools/run.sh" "lap$M" "$F" RACER_MAP="$M" RACER_AUTO=1 RACER_LAPS="$L" | grep -E "skipped|warn|error" | head -3
node -e '
const d = JSON.parse(require("fs").readFileSync(process.argv[1] + "/out/racer-lap" + process.argv[2] + ".json", "utf8"));
const f = (k) => d[k] || [];
console.log("tick", d.ticks, "phase", d["race.phase"], "(2 racing, 3 done)");
console.log("kart  place lap  prog_m  falls hits  finished best_lap_s  top_speed");
for (let i = 0; i < 8; i++) console.log(i, "    ", f("karts.place")[i], "    ", f("karts.lap")[i], "  ", (f("karts.prog")[i] / 10).toFixed(0).padStart(6), "  ", f("karts.falls")[i], "   ", f("karts.hits")[i], "   ", f("karts.finished")[i], "       ", (f("karts.best_lap")[i] / 100).toFixed(1), "      ", (f("karts.top_speed")[i] / 10).toFixed(1));
const fin = f("karts.finished").filter(Boolean).length, falls = f("karts.falls").reduce((a, b) => a + b, 0);
console.log(fin === 8 && falls <= 16 ? "PASS" : "CHECK", "finished", fin, "of 8, total falls", falls);
' "$D/../.." "$M"
