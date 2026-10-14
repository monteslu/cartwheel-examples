#!/usr/bin/env bash
# Fetch cartwheel-engine at the commit pinned in scripts/deps.lock into
# deps/cartwheel-engine, then run its own fetch-deps.sh, which puts the
# engine's dependencies (three.c, Box2D, Box3D, wasi-sdk, wasmcart, ...) in
# deps/cartwheel-engine/deps/.
#
#   scripts/fetch-deps.sh
#   CARTWHEEL_ENGINE=../cartwheel-engine scripts/fetch-deps.sh
#
# CARTWHEEL_ENGINE links an existing engine checkout instead (for working on
# the engine and the games together); its deps/ are fetched there. Other
# variables (WASI_SDK, NATIVE_DAWN_PREFIX, FORCE) pass through to the
# engine's script.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS="$ROOT/deps"
DST="$DEPS/cartwheel-engine"
mkdir -p "$DEPS"

read -r _ URL REV < <(grep -E '^cartwheel-engine[[:space:]]' "$ROOT/scripts/deps.lock")

if [ -n "${CARTWHEEL_ENGINE:-}" ]; then
  src="$(cd "$CARTWHEEL_ENGINE" && pwd)"
  rm -rf "$DST" && ln -sfn "$src" "$DST"
  echo "cartwheel-engine: $src (CARTWHEEL_ENGINE)"
else
  [ -L "$DST" ] && rm -f "$DST"
  if [ ! -d "$DST/.git" ]; then
    rm -rf "$DST" && git init -q "$DST"
  fi
  if [ "$(git -C "$DST" rev-parse -q --verify HEAD 2>/dev/null)" != "$REV" ]; then
    git -C "$DST" fetch -q --depth 1 "$URL" "$REV"
    git -C "$DST" -c advice.detachedHead=false checkout -q "$REV"
  fi
  echo "cartwheel-engine: $REV"
fi

"$DST/scripts/fetch-deps.sh"
