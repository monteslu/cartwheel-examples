#!/usr/bin/env bash
# games/racer/tools/fair_build.sh: tools/build.sh, retried while another map's file is mid-edit (errors outside m3_fair.c)
D="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for i in $(seq 1 12); do
  out="$("$D/tools/build.sh" 2>&1)"
  if ! echo "$out" | grep -qE "FAILED|error"; then echo "built"; exit 0; fi
  if echo "$out" | grep -q "m3_fair"; then echo "$out" | grep -A3 m3_fair | head -20; exit 1; fi
  sleep 15
done
echo "$out" | head -5; exit 1
