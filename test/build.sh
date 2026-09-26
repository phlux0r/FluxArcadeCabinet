#!/usr/bin/env bash
# Builds the host harness (see README.md) and runs it unless --build-only.
#
#   test/build.sh                  # build, then run all three scenarios
#   test/build.sh god 30000        # build, then run one scenario
#   test/build.sh --build-only
#
# Needs a host g++ with C++17 and Jet's sources. Jet is found automatically
# once `pio run` has fetched it; otherwise set JET_SRC to a checkout:
#   JET_SRC=~/src/Jet/src test/build.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
OUT="$HERE/.build"

if [ -z "${JET_SRC:-}" ]; then
  for c in "$ROOT"/.pio/libdeps/*/Jet/src "$ROOT"/../Jet/src "$ROOT"/../jet/src; do
    [ -f "$c/Scene.cpp" ] && JET_SRC="$c" && break
  done
fi
if [ -z "${JET_SRC:-}" ] || [ ! -f "$JET_SRC/Scene.cpp" ]; then
  echo "Jet sources not found. Run 'pio run' once to fetch them, or set" >&2
  echo "JET_SRC to a checkout of https://github.com/CubeCoders/Jet" >&2
  exit 1
fi
JET_SRC="$(cd "$JET_SRC" && pwd)"

# ASan+UBSan: the harness is also how memory errors and leaks get caught.
CXXFLAGS=(-std=gnu++17 -O1 -g -fsanitize=address,undefined)
mkdir -p "$OUT"

# Jet rarely changes, so its objects are cached. Delete test/.build to force
# a rebuild (or after changing JET_SRC).
if [ ! -f "$OUT/libjet.a" ]; then
  echo "building Jet from $JET_SRC"
  for f in "$JET_SRC"/*.cpp; do
    case "$(basename "$f")" in Example.cpp|Sample.cpp) continue;; esac
    g++ "${CXXFLAGS[@]}" -I"$ROOT/include" -I"$JET_SRC" \
        -c "$f" -o "$OUT/$(basename "$f" .cpp).o"
  done
  ar rcs "$OUT/libjet.a" "$OUT"/*.o
fi

echo "building harness"
# -Itest/stub comes first so its Arduino.h/GFX/AudioEngine shadow the real ones.
g++ "${CXXFLAGS[@]}" -Wall -Wno-unused-variable \
    -I"$HERE/stub" -I"$ROOT/src" -I"$ROOT/include" -I"$JET_SRC" \
    "$HERE/tankflux_harness.cpp" "$ROOT"/src/games/TankFlux/*.cpp \
    "$OUT/libjet.a" -o "$OUT/tankflux_harness"

[ "${1:-}" = "--build-only" ] && exit 0

cd "$OUT"
if [ $# -gt 0 ]; then
  ./tankflux_harness "$@"
else
  for s in "play 20000" "god 30000" "menus 12000"; do
    echo "=== $s"
    # shellcheck disable=SC2086
    ./tankflux_harness $s | tail -3
  done
fi
