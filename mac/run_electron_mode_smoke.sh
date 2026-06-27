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

"$BIN" --electron-native-theme-smoke --electron-power-monitor-smoke --electron-global-shortcut-smoke --electron-power-save-blocker-smoke --electron-screen-smoke
"$BIN" --electron-native-theme-appearance-smoke
"$BIN" --electron-global-shortcut-dispatch-smoke
"$BIN" --electron-menu-smoke
"$BIN" --electron-native-image-smoke
"$BIN" --electron-clipboard-smoke
"$BIN" --electron-shell-smoke
"$BIN" --electron-dialog-smoke
"$BIN" --electron-tray-smoke
"$BIN" --electron-protocol-smoke
"$BIN" --electron-base-api-smoke
"$BIN" --electron-power-monitor-event-smoke
"$BIN" --electron-power-save-blocker-lifecycle-smoke
"$BIN" --electron-app-smoke
"$BIN" --electron-app-single-instance-smoke
"$BIN" --electron-linked-binding-runtime-smoke
"$BIN" --electron-v8-typed-array-smoke
"$BIN" --electron-v8-shared-array-buffer-smoke
"$BIN" --electron-node-bootstrap-smoke

symbols="$(nm -gU "$BIN")"
grep -q "__register_electron_browser_native_theme" <<<"$symbols"
grep -q "__register_electron_browser_app" <<<"$symbols"
grep -q "__register_electron_browser_powermonitor" <<<"$symbols"
grep -q "__register_electron_browser_global_shortcut" <<<"$symbols"
grep -q "__register_electron_browser_menu" <<<"$symbols"
grep -q "__register_electron_browser_power_save_blocker" <<<"$symbols"
grep -q "__register_electron_common_nativeImage" <<<"$symbols"
grep -q "__register_electron_common_clipboard" <<<"$symbols"
grep -q "__register_electron_common_screen" <<<"$symbols"
grep -q "__register_electron_common_shell" <<<"$symbols"
grep -q "__register_electron_browser_dialog" <<<"$symbols"
grep -q "__register_electron_browser_tray" <<<"$symbols"
grep -q "__register_electron_browser_protocol" <<<"$symbols"
grep -q "__register_electron_browser_commandline" <<<"$symbols"
grep -q "__register_electron_browser_safe_storage" <<<"$symbols"
grep -q "_nodeModuleInitRegister" <<<"$symbols"
grep -q "_electronMacNodeBridgeGetLinkedBinding" <<<"$symbols"
grep -q "__register_electron_common_features" <<<"$symbols"
grep -q "__register_electron_common_v8_util" <<<"$symbols"
grep -q "__register_electron_common_original_fs" <<<"$symbols"
grep -q "__register_electron_common_intl_collator" <<<"$symbols"

echo "ok electron_mode_smoke"
