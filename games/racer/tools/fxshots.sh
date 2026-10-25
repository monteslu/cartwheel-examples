#!/usr/bin/env bash
# games/racer/tools/fxshots.sh <frame> <name>...: one run per forced effect (RACER_FX, see fx.c) on a human kart that
# sits on the proving ground's grid line; writes out/racer-fx_<name>.png. The effect fires at tick 30 mod 60, so
# frame 306 shows it 6 ticks old; pass 312 or 330 to see it later.
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
F="$1"; shift
for n in "$@"; do
  "$D/run.sh" "fx_$n" "$F" RACER_MAP=-1 RACER_PLAYERS=1 RACER_FX="$n" | grep -E "^wrote" | sed 's/.*\.\.\/\.\.\///' | cut -d' ' -f1
done
