# macOS Electron API Coverage

Updated: 2026-06-24

This matrix tracks Electron-like API coverage that is backed by macOS code and regression tests. It is intentionally scoped to APIs that are either implemented or are the next practical compatibility targets.

| Module / API | Windows behavior in this tree | macOS implementation | E2E coverage | Gap / next step |
| --- | --- | --- | --- | --- |
| BrowserWindow lifecycle | Win32 HWND create/show/hide/minimize/maximize/focus/close flow | `mac_window` plus Win32-compatible HWND shims | Yes, `browserwindow-*` checks | Add app-level quit/window-all-closed semantics |
| WebContents navigation | Load URL/file, history, stop, title/url/load callbacks | miniblink callbacks and mac host window | Yes, navigation/load/script/source checks | Keep expanding Electron callback parity |
| Session storage/cookies | Cookie API, localStorage/sessionStorage partition behavior | Cookie jar/path shims and per-view storage paths | Yes, session partition checks | Add more session permission/cache APIs |
| webRequest | begin/end, redirect, cancel, post body | miniblink load URL callbacks | Yes, `session-webrequest-*` | Add auth/proxy/header edge cases |
| Dialog | MessageBox, open/save file, open directory | Deterministic Win32-compatible shim | Yes, `dialog-*` | Optional real `NSOpenPanel`/`NSSavePanel` behind non-CI path |
| Menu/context menu | Create/append/insert/state/track/system menu | In-process menu model plus command dispatch | Yes, `menu-*` | Add accelerator and richer role behavior |
| Tray | `Shell_NotifyIcon` add/modify/delete/version/focus | In-memory status item model | Yes, `tray-*` | Add native status item rendering/events |
| NativeImage/bitmap | DIB section, masks, memory DC, clipboard DIB, icon lifecycle | GDI-compatible bitmap/DC/icon shims | Yes, `nativeimage-*` | Add more image decode/scale variants |
| Shell openPath/openExternal | `ShellExecuteW` and `ShellExecuteExW` | `/usr/bin/open`, quoted URL cleanup, deterministic E2E log hook | Yes, `shell-open-*` | Add async JS binding smoke once Electron runtime path is enabled |
| Shell showItemInFolder | `SHOpenFolderAndSelectItems` optional path with `ShellExecuteExW` fallback | Direct PIDL reveal API plus fallback-compatible shell32 probing | Yes, `shell-show-item-*` | Replace fallback with direct platform_util path when COM branch is removed |
| Shell trashItem | Move item to Recycle Bin through shell file operation | `SHFileOperationW(FO_DELETE | FOF_ALLOWUNDO)` moves to `~/.Trash` | Yes, `shell-trash-item-*` | Add multi-item and directory trash cases |
| Shell beep | `MessageBeep(MB_OK)` | `MessageBeep`/`Beep` shim with test suppress switch | Yes, `shell-beep-*` | Optional native sound API if needed |
| App path/locale | SHGetFolderPath, known folder, locale APIs used by Electron app | macOS folder mapping, known Downloads folder, dynamic locale shim | Yes, `app-path-*` and `app-locale-*` | Add full JS Electron runtime smoke for `app.getPath/getLocale` |
| App name/version | Electron app metadata setters/getters | Existing C++ fields in `ApiApp` | No | Add JS binding smoke when Electron runtime path is enabled |
| App lifecycle | quit, before-quit, window-all-closed | `app.quit()` now closes windows and posts `WM_QUIT`; `app.exit()` remains immediate exit; window-all-closed hook exists in `ApiBrowserWindow` | Partial, `app-lifecycle-*` covers Win32 message-loop quit path | Add full JS Electron runtime smoke for `before-quit/window-all-closed` event ordering |
| Single instance lock | Process-level named mutex | Win32-compatible named mutex shim | Yes, `app-single-instance-mutex` | Add cross-process callback/argv forwarding case |
| globalShortcut | Register/unregister accelerators | Not covered yet | No | Later system integration batch |
| screen | Display list/primary display/cursor point | NSScreen-backed Win32 monitor metrics, monitor lookup, monitor info, display enumeration, cursor point | Yes, `screen-*` covers monitor metrics/enumeration/lookup; cursor point already used by menu/input paths | Add full JS Electron runtime smoke for `screen.getPrimaryDisplay/getAllDisplays/getCursorScreenPoint` |
| nativeTheme | Theme/dark mode/high contrast | `SystemParametersInfoW` supports work area, animation, deterministic high contrast; JS `nativeTheme` exposes basic read-only state | Partial, `native-theme-*` covers deterministic native queries | Add full JS Electron runtime smoke for `nativeTheme.shouldUse*` and `themeSource` |
| powerMonitor | Suspend/resume/power events | Not covered yet | No | Add event source or deterministic test hook |
