#!/usr/bin/env bash
# Builds the host harnesses (see README.md) and runs them unless --build-only.
#
#   test/build.sh                  # build, then run every game's scenarios
#   test/build.sh god 30000        # one Tank Flux scenario
#   test/build.sh tube god 30000   # one Tube Flux scenario
#   test/build.sh star god 12000   # one Star Flux scenario
#   test/build.sh audio            # just the audio mixer tests
#   test/build.sh games2d runner 60000   # one 2D game's attract demo checks
#   test/build.sh cabinet          # main.cpp: every game launched and quit
#   test/build.sh hiscore          # the high-score tables and name entry
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

# -Itest/stub comes first so its Arduino.h/GFX/AudioEngine shadow the real ones.
build_harness() {   # <name> <sources...>
  local name="$1"; shift
  echo "building $name"
  g++ "${CXXFLAGS[@]}" -Wall -Wno-unused-variable \
      -I"$HERE/stub" -I"$ROOT/src" -I"$ROOT/include" -I"$JET_SRC" \
      "$@" "$OUT/libjet.a" -o "$OUT/$name"
}
build_harness tankflux_harness "$HERE/tankflux_harness.cpp" "$ROOT"/src/games/TankFlux/*.cpp
build_harness tubeflux_harness "$HERE/tubeflux_harness.cpp" "$ROOT"/src/games/TubeFlux/*.cpp
build_harness starflux_harness "$HERE/starflux_harness.cpp" "$ROOT"/src/games/StarFlux/*.cpp
# The 2D games' attract demos. These games include the real (inert) audio
# engine, so src/ goes ahead of the stubs here; no Jet needed.
echo "building games2d_harness"
g++ "${CXXFLAGS[@]}" -Wall -Wno-unused-variable -Wno-sign-compare \
    -I"$ROOT/src" -I"$HERE/stub" -I"$ROOT/include" \
    "$HERE/games2d_harness.cpp" -o "$OUT/games2d_harness"
# The whole cabinet: main.cpp, launcher and every game, real audio engine
# (inert), so src/ ahead of the stubs again. Needs Jet for the 3D games.
echo "building cabinet_sim"
g++ "${CXXFLAGS[@]}" -Wall -Wno-unused-variable -Wno-sign-compare \
    -I"$ROOT/src" -I"$HERE/stub" -I"$ROOT/include" -I"$JET_SRC" \
    "$HERE/cabinet_sim.cpp" "$ROOT"/src/games/TankFlux/*.cpp "$ROOT"/src/games/TubeFlux/*.cpp \
    "$ROOT"/src/games/StarFlux/*.cpp \
    "$OUT/libjet.a" -o "$OUT/cabinet_sim"
# The high-score tables and name entry: the stubs, no Jet.
echo "building hiscore_test"
g++ "${CXXFLAGS[@]}" -Wall -Wno-unused-function -I"$HERE" -I"$HERE/stub" -I"$ROOT/src" -I"$ROOT/include" \
    "$HERE/hiscore_test.cpp" -o "$OUT/hiscore_test"
# The audio mixer and loader are plain C++; this uses no stubs at all.
echo "building audio_test"
g++ "${CXXFLAGS[@]}" -Wall -I"$ROOT/src" "$HERE/audio_test.cpp" -o "$OUT/audio_test"

[ "${1:-}" = "--build-only" ] && exit 0

cd "$OUT"
[ "${1:-}" = audio ] && exec ./audio_test
[ "${1:-}" = games2d ] && { shift; exec ./games2d_harness "$@"; }
[ "${1:-}" = cabinet ] && exec ./cabinet_sim
[ "${1:-}" = hiscore ] && exec ./hiscore_test
game=tankflux
case "${1:-}" in
  tank) game=tankflux; shift ;;
  tube) game=tubeflux; shift ;;
  star) game=starflux; shift ;;
esac
if [ $# -gt 0 ]; then
  "./${game}_harness" "$@"
else
  echo "=== audio"
  ./audio_test | tail -1
  echo "=== hiscore (tables, name entry)"
  ./hiscore_test | tail -1
  echo "=== cabinet (main.cpp: launch and quit every game, menu scrolling, high-score cycle)"
  ./cabinet_sim
  echo "=== games2d (Runner, Asteroid, Lander attract demos)"
  ./games2d_harness all 30000
  for g in tankflux tubeflux starflux; do
    scenarios=("play 20000" "god 30000" "menus 12000")
    # Every 3D game's attract screen includes a demo run: idle sits through it.
    [ "$g" = tubeflux ] && scenarios+=("idle 8000" "demoexit")
    [ "$g" = tankflux ] && scenarios+=("idle 12000" "demoexit")
    # Star Flux runs at 33ms a frame (the others 16ms), so fewer frames go as far.
    [ "$g" = starflux ] && scenarios=("play 12000" "god 12000" "menus 3000" "idle 3000" "demoexit" "passive 12000" "rapid" "loops" "reach")
    for s in "${scenarios[@]}"; do
      echo "=== $g $s"
      # shellcheck disable=SC2086
      "./${g}_harness" $s | tail -6
    done
  done
fi
