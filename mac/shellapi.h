#ifndef MAC_SHELLAPI_H
#define MAC_SHELLAPI_H

#include "windows.h"

EXTERN_C HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd);
EXTERN_C BOOL Shell_NotifyIconW(DWORD dwMessage, PNOTIFYICONDATAW lpData);
EXTERN_C BOOL Shell_NotifyIconA(DWORD dwMessage, PNOTIFYICONDATAA lpData);

#endif // MAC_SHELLAPI_H
