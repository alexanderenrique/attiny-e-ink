#!/usr/bin/env bash
# Host-side unit tests for pure logic headers (no PlatformIO native platform required).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/.pio/native_test_build"
mkdir -p "$BUILD"

UNITY_SRC=""
for candidate in \
  "$HOME/.platformio/lib/Unity"*/src/unity.c \
  "$ROOT/.pio/libdeps/native_test"/Unity*/src/unity.c; do
  if [[ -f "$candidate" ]]; then
    UNITY_SRC="$candidate"
    UNITY_INC="$(dirname "$candidate")"
    break
  fi
done

if [[ -z "$UNITY_SRC" ]]; then
  if [[ ! -f "$BUILD/unity/unity.c" ]]; then
    mkdir -p "$BUILD/unity"
    curl -fsSL "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.0/src/unity.c" -o "$BUILD/unity/unity.c"
    curl -fsSL "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.0/src/unity.h" -o "$BUILD/unity/unity.h"
    curl -fsSL "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.0/src/unity_internals.h" -o "$BUILD/unity/unity_internals.h"
  fi
  UNITY_SRC="$BUILD/unity/unity.c"
  UNITY_INC="$BUILD/unity"
fi

CXX="${CXX:-c++}"
CC="${CC:-cc}"
"$CC" -std=c11 -Wall -Wextra -I"$UNITY_INC" -c "$UNITY_SRC" -o "$BUILD/unity.o"
"$CXX" -std=c++17 -Wall -Wextra -DHOST_TEST=1 \
  -I"$ROOT/include" -I"$ROOT/test/host_include" -I"$UNITY_INC" \
  "$ROOT/test/test_logic/test_main.cpp" \
  "$ROOT/src/font.cpp" \
  "$ROOT/src/font_serif.cpp" \
  "$ROOT/src/display_raster.cpp" \
  "$BUILD/unity.o" \
  -o "$BUILD/test_logic"
"$BUILD/test_logic"
echo "All host logic tests passed."
