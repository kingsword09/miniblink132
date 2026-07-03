#!/usr/bin/env bash
set -euo pipefail

config="${1:-${MAC_CONFIG:-mac_release_arm64}}"
rebuild_opt="${REBUILD_OPT:-3}"
src_path="${SRC_PATH:-.}"
fastmake="${FASTMAKE:-./out/mac_tools/fastmake}"
out_dir="out/$config"

run_fastmake() {
  local cfg="$1"
  local expected="$2"
  shift 2

  echo "::group::Build $cfg"
  "$fastmake" "fastmake/buildcfg/$cfg" "$config" "$rebuild_opt" "$src_path" "$@"
  echo "::endgroup::"

  if [[ ! -e "$expected" ]]; then
    echo "Expected build output was not produced: $expected" >&2
    return 1
  fi
}

test -x "$fastmake"

run_fastmake freetype_build.js "$out_dir/libfreetype.a"
run_fastmake openssl_build.js "$out_dir/libopenssl.a"
run_fastmake util_build.js "$out_dir/libthird_party_util.a"
run_fastmake icu_build.js "$out_dir/libthird_party_icu.a"
run_fastmake v8_108_build.js "$out_dir/libv8_108.a"
run_fastmake v8_108_gen_build.js "$out_dir/libv8_108_gen.a"
run_fastmake skia_build.js "$out_dir/libskia.a"
run_fastmake gen_build.js "$out_dir/libgen.a"
run_fastmake chromium_build.js "$out_dir/libchromium.a"
run_fastmake electron_build.js "$out_dir/libelectron.a"
run_fastmake node_build.js "$out_dir/libnodejs.a"
run_fastmake blink_build.js "$out_dir/miniblink" electron

test -x "$out_dir/miniblink"
