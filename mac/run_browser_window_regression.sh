#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-mac_release_arm64}"
OUT_DIR="$ROOT_DIR/out/$CONFIG"
DYLIB="$OUT_DIR/miniblink.dylib"
TEST_BIN="$OUT_DIR/browser_window_like_test"
LOG_DIR="$OUT_DIR/regression_logs/$(date +%Y%m%d_%H%M%S)"

mkdir -p "$LOG_DIR"
cd "$ROOT_DIR"

run_step() {
    local name="$1"
    shift
    echo "== $name =="
    "$@" >"$LOG_DIR/$name.log" 2>&1
    echo "ok $name"
}

if [[ ! -f "$DYLIB" ]]; then
    echo "missing $DYLIB" >&2
    echo "build it first, for example:" >&2
    echo "./out/mac_tools/fastmake fastmake/buildcfg/blink_build.js $CONFIG 2 ." >&2
    exit 2
fi

echo "logs: $LOG_DIR"

run_step export_check bash -c '
set -euo pipefail
def_file="mbvip/mbvip.def"
dylib="'"$DYLIB"'"
def_symbols="$(mktemp)"
nm_symbols="$(mktemp)"
check_c="$(mktemp).c"
check_bin="$(mktemp)"
trap "rm -f \"$def_symbols\" \"$nm_symbols\" \"$check_c\" \"$check_bin\"" EXIT

perl -ne '"'"'print "$1\n" if /^\s*(mb\w+)\b/'"'"' "$def_file" | sort -u > "$def_symbols"
nm -gU "$dylib" | perl -ne '"'"'print "$1\n" if /\b_(mb\w+)$/'"'"' | sort -u > "$nm_symbols"
static_missing="$(comm -23 "$def_symbols" "$nm_symbols" | wc -l | tr -d " ")"

{
  echo "#include <dlfcn.h>"
  echo "#include <stdio.h>"
  echo "int main(){"
  echo "void* h=dlopen(\"$dylib\",RTLD_NOW);"
  echo "if(!h){printf(\"DLOPEN:%s\\n\",dlerror()); return 2;}"
  echo "int miss=0;"
  while IFS= read -r symbol; do
    echo "if(!dlsym(h,\"$symbol\")){printf(\"%s\\n\",\"$symbol\"); ++miss;}"
  done < "$def_symbols"
  echo "printf(\"runtime_missing=%d\\n\",miss);"
  echo "return miss?1:0;}"
} > "$check_c"

clang "$check_c" -o "$check_bin"
runtime_output="$("$check_bin")"
runtime_missing="$(printf "%s\n" "$runtime_output" | awk -F= "/runtime_missing=/{print \$2}")"

def_count="$(wc -l < "$def_symbols" | tr -d " ")"
echo "def_count=$def_count static_missing=$static_missing runtime_missing=$runtime_missing"
test "$static_missing" = "0"
test "$runtime_missing" = "0"
'

run_step dlopen_miniblink_test "$OUT_DIR/dlopen_miniblink_test"
run_step mbapi_smoke_test "$OUT_DIR/mbapi_smoke_test"
run_step mbapi_extra_smoke "$OUT_DIR/mbapi_extra_smoke"

run_step build_browser_window_like_test clang++ -std=c++17 -ObjC++ -DENABLE_MB=1 -Imac -I. \
    mac/browser_window_like_test.mm "$DYLIB" -framework Cocoa -o "$TEST_BIN"

run_step browser_window_like_test "$TEST_BIN"

echo "summary:"
grep -hE "^(PASS|FAIL) |browser_window_like_test failures=" "$LOG_DIR/browser_window_like_test.log" || true
echo "all regression steps passed"
