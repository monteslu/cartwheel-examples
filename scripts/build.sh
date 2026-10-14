#!/usr/bin/env bash
# Build every game: native (build/native), GL carts (build/cart) and dual GL +
# WebGPU carts (build/cart-wgpu), with cartwheel-engine from deps/ (run
# scripts/fetch-deps.sh first). The engine's CMakeLists.txt is the top of the
# build; this repository's CMakeLists.txt is its project (CW_PROJECT_DIR).
#
#   scripts/build.sh                        all three trees
#   TARGETS="native" scripts/build.sh       one tree (native, cart or cart-wgpu)
#   BUILD_TYPE=Debug scripts/build.sh
#   scripts/build.sh bikemesa derby_auto    only these apps (ninja targets)
#
# The cart trees pack assets with the native tree's cwpack, so native is
# built first. CW_CART_LINK_JOBS (default 4) caps the threads one cart link
# (ThinLTO and wasm-opt) uses; at most two carts link at once.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/deps/cartwheel-engine"
[ -f "$ENGINE/CMakeLists.txt" ] || { echo "no cartwheel-engine in deps/: run scripts/fetch-deps.sh" >&2; exit 1; }
BT="${BUILD_TYPE:-Release}"
JOBS=()
[ -n "${CW_CART_LINK_JOBS:-}" ] && JOBS=(-DCW_CART_LINK_JOBS="$CW_CART_LINK_JOBS")
COMMON=(-G Ninja -DCMAKE_BUILD_TYPE="$BT" -DCW_ENGINE_ONLY=ON -DCW_PROJECT_DIR="$ROOT")
CWPACK="$ROOT/build/native/tools/cwpack/cwpack"
for t in ${TARGETS:-native cart cart-wgpu}; do
  if [ "$t" != native ] && [ ! -x "$CWPACK" ]; then
    echo "build/native has no cwpack yet: build native first (TARGETS=\"native $t\")" >&2; exit 1
  fi
  case "$t" in
    native) cmake -S "$ENGINE" -B "$ROOT/build/native" "${COMMON[@]}" >/dev/null ;;
    cart) cmake -S "$ENGINE" -B "$ROOT/build/cart" "${COMMON[@]}" "${JOBS[@]}" \
            -DCMAKE_TOOLCHAIN_FILE="$ENGINE/cmake/cart.cmake" -DCW_CWPACK="$CWPACK" >/dev/null ;;
    cart-wgpu) cmake -S "$ENGINE" -B "$ROOT/build/cart-wgpu" "${COMMON[@]}" "${JOBS[@]}" \
                 -DCMAKE_TOOLCHAIN_FILE="$ENGINE/cmake/cart.cmake" -DCW_WGPU=ON -DCW_CWPACK="$CWPACK" >/dev/null ;;
    *) echo "unknown target tree: $t (native, cart, cart-wgpu)" >&2; exit 2 ;;
  esac
  ninja -C "$ROOT/build/$t" "$@"
done
