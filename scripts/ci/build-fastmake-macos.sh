#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/out/mac_tools"
OBJ_DIR="$OUT_DIR/build"

rm -rf "$OBJ_DIR"
mkdir -p "$OBJ_DIR"
cd "$ROOT_DIR"

cc="${CC:-clang}"
cxx="${CXX:-clang++}"

cflags=(
  -std=c11
  -O2
  -DOS_MAC=1
  -DQUICKJS_DISABLE_ATOMICS=1
  -I.
  -Iquickjs
  -include fastmake/quickjs_mac_compat.h
  -Wno-incompatible-pointer-types
  -Wno-incompatible-function-pointer-types
  -Wno-pointer-sign
  -Wno-int-conversion
  -Wno-implicit-function-declaration
)

cxxflags=(
  -std=c++17
  -fblocks
  -O2
  -DOS_MAC=1
  -I.
  -Imac
)

c_sources=(
  quickjs/quickjs.c
  quickjs/quickjs-libc.c
  quickjs/cutils.c
  quickjs/libregexp.c
  quickjs/libunicode.c
)

cxx_sources=(
  fastmake/FastMakeMain.cpp
  fastmake/QjsFastMake.cpp
  fastmake/CmdHandler.cpp
  fastmake/StringUtilLite.cpp
  content/common/cJSON.cpp
  mac/mac_windows.cpp
)

for src in "${c_sources[@]}"; do
  "$cc" "${cflags[@]}" -c "$src" -o "$OBJ_DIR/$(basename "$src").o"
done

for src in "${cxx_sources[@]}"; do
  "$cxx" "${cxxflags[@]}" -c "$src" -o "$OBJ_DIR/$(basename "$src").o"
done

"$cxx" "${cxxflags[@]}" -ObjC++ -c mac/mac_window.mm -o "$OBJ_DIR/mac_window.mm.o"

"$cxx" "$OBJ_DIR"/*.o \
  -framework CoreFoundation \
  -framework CoreGraphics \
  -framework IOKit \
  -framework AppKit \
  -framework Cocoa \
  -framework Carbon \
  -ldl \
  -lpthread \
  -o "$OUT_DIR/fastmake"

test -x "$OUT_DIR/fastmake"
