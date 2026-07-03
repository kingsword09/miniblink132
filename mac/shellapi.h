#ifndef MAC_SHELLAPI_H
#define MAC_SHELLAPI_H

#include "windows.h"

typedef UINT FILEOP_FUNC;
typedef WORD FILEOP_FLAGS;

#ifndef SEE_MASK_CLASSNAME
#define SEE_MASK_CLASSNAME 0x00000001
#endif

#ifndef SEE_MASK_NOCLOSEPROCESS
#define SEE_MASK_NOCLOSEPROCESS 0x00000040
#endif

#ifndef SEE_MASK_NOASYNC
#define SEE_MASK_NOASYNC 0x00000100
#endif

#ifndef SEE_MASK_FLAG_NO_UI
#define SEE_MASK_FLAG_NO_UI 0x00000400
#endif

#ifndef FO_DELETE
#define FO_DELETE 0x0003
#endif

#ifndef FOF_SILENT
#define FOF_SILENT 0x0004
#endif

#ifndef FOF_NOCONFIRMATION
#define FOF_NOCONFIRMATION 0x0010
#endif

#ifndef FOF_ALLOWUNDO
#define FOF_ALLOWUNDO 0x0040
#endif

#ifndef FOF_NOERRORUI
#define FOF_NOERRORUI 0x0400
#endif

#ifndef ERROR_INVALID_PARAMETER
#define ERROR_INVALID_PARAMETER 87L
#endif

typedef struct _SHELLEXECUTEINFOW {
    DWORD cbSize;
    ULONG fMask;
    HWND hwnd;
    LPCWSTR lpVerb;
    LPCWSTR lpFile;
    LPCWSTR lpParameters;
    LPCWSTR lpDirectory;
    INT nShow;
    HINSTANCE hInstApp;
    LPVOID lpIDList;
    LPCWSTR lpClass;
    HKEY hkeyClass;
    DWORD dwHotKey;
    HANDLE hIcon;
    HANDLE hProcess;
} SHELLEXECUTEINFOW, *LPSHELLEXECUTEINFOW;

typedef SHELLEXECUTEINFOW SHELLEXECUTEINFO;
typedef LPSHELLEXECUTEINFOW LPSHELLEXECUTEINFO;

typedef struct _SHFILEOPSTRUCTW {
    HWND hwnd;
    FILEOP_FUNC wFunc;
    LPCWSTR pFrom;
    LPCWSTR pTo;
    FILEOP_FLAGS fFlags;
    BOOL fAnyOperationsAborted;
    LPVOID hNameMappings;
    LPCWSTR lpszProgressTitle;
} SHFILEOPSTRUCTW, *LPSHFILEOPSTRUCTW;

typedef SHFILEOPSTRUCTW SHFILEOPSTRUCT;
typedef LPSHFILEOPSTRUCTW LPSHFILEOPSTRUCT;

EXTERN_C HINSTANCE ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd);
EXTERN_C HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd);
EXTERN_C BOOL ShellExecuteExW(LPSHELLEXECUTEINFOW lpExecInfo);
EXTERN_C int SHFileOperationW(LPSHFILEOPSTRUCTW lpFileOp);
EXTERN_C BOOL MessageBeep(UINT uType);
EXTERN_C BOOL Beep(DWORD dwFreq, DWORD dwDuration);
EXTERN_C BOOL Shell_NotifyIconW(DWORD dwMessage, PNOTIFYICONDATAW lpData);
EXTERN_C BOOL Shell_NotifyIconA(DWORD dwMessage, PNOTIFYICONDATAA lpData);

#ifndef ShellExecuteEx
#define ShellExecuteEx ShellExecuteExW
#endif

#ifndef SHFileOperation
#define SHFileOperation SHFileOperationW
#endif

#endif // MAC_SHELLAPI_H
