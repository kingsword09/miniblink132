#ifndef MAC_SHELLAPI_H
#define MAC_SHELLAPI_H

#include "windows.h"

EXTERN_C HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd);

#endif // MAC_SHELLAPI_H
