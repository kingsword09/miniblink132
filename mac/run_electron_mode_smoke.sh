#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-mac_release_arm64}"
OUT_DIR="$ROOT_DIR/out/$CONFIG"
BIN="$OUT_DIR/miniblink"

if [[ ! -x "$BIN" ]]; then
    echo "missing $BIN" >&2
    echo "build it first, for example:" >&2
    echo "./out/mac_tools/fastmake fastmake/buildcfg/blink_build.js $CONFIG 2 . electron" >&2
    exit 2
fi

"$BIN" --electron-native-theme-smoke --electron-power-monitor-smoke --electron-global-shortcut-smoke --electron-power-save-blocker-smoke --electron-screen-smoke --electron-linked-binding-runtime-smoke

symbols="$(nm -gU "$BIN")"
grep -q "__register_electron_browser_native_theme" <<<"$symbols"
grep -q "__register_electron_browser_powermonitor" <<<"$symbols"
grep -q "__register_electron_browser_global_shortcut" <<<"$symbols"
grep -q "__register_electron_browser_power_save_blocker" <<<"$symbols"
grep -q "__register_electron_common_screen" <<<"$symbols"
grep -q "_nodeModuleInitRegister" <<<"$symbols"
grep -q "_electronMacNodeBridgeGetLinkedBinding" <<<"$symbols"

echo "ok electron_mode_smoke"
