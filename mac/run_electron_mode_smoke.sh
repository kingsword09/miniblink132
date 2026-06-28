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
"$BIN" --electron-asar-smoke
"$BIN" --electron-power-monitor-event-smoke
"$BIN" --electron-power-save-blocker-lifecycle-smoke
"$BIN" --electron-app-smoke
"$BIN" --electron-app-single-instance-smoke
"$BIN" --electron-linked-binding-runtime-smoke
"$BIN" --electron-v8-typed-array-smoke
"$BIN" --electron-v8-shared-array-buffer-smoke
"$BIN" --electron-node-bootstrap-smoke

require_symbol() {
    nm -gU "$BIN" | grep "$1" >/dev/null
}

require_symbol "__register_electron_browser_native_theme"
require_symbol "__register_electron_browser_app"
require_symbol "__register_electron_browser_powermonitor"
require_symbol "__register_electron_browser_global_shortcut"
require_symbol "__register_electron_browser_menu"
require_symbol "__register_electron_browser_power_save_blocker"
require_symbol "__register_electron_common_nativeImage"
require_symbol "__register_electron_common_clipboard"
require_symbol "__register_electron_common_screen"
require_symbol "__register_electron_common_shell"
require_symbol "__register_electron_browser_dialog"
require_symbol "__register_electron_browser_tray"
require_symbol "__register_electron_browser_protocol"
require_symbol "__register_electron_browser_commandline"
require_symbol "__register_electron_browser_safe_storage"
require_symbol "_nodeModuleInitRegister"
require_symbol "_electronMacNodeBridgeGetLinkedBinding"
require_symbol "__register_electron_common_features"
require_symbol "__register_electron_common_v8_util"
require_symbol "__register_electron_common_original_fs"
require_symbol "__register_electron_common_intl_collator"
require_symbol "__register_electron_common_asar"

echo "ok electron_mode_smoke"
