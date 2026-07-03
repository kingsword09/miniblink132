#ifndef WINDOWS_FOR_MAC_H
#define WINDOWS_FOR_MAC_H

#ifndef __int64
#define __int64 long long
#endif

// Carbon/ApplicationServices declare legacy GetCurrentProcess/GetCurrentThread
// symbols. Include the full framework umbrella before mapping Win32 calls to
// the miniblink shim names, otherwise later macOS framework headers can have
// their declarations rewritten by these macros.
typedef bool OBJC_BOOL;
#define BOOL OBJC_BOOL
#include <ApplicationServices/ApplicationServices.h>
#undef BOOL

#define GetCurrentProcess MbGetCurrentProcess
#define GetCurrentThread MbGetCurrentThread

#include "../linux/windows.h"

#ifndef CAPTUREBLT
#define CAPTUREBLT (DWORD)0x40000000
#endif

EXTERN_C HDC GetWindowDC(HWND hWnd);

#undef GetCurrentProcess
#undef GetCurrentThread

#include <string>

typedef CHAR* LPSTR;
typedef WORD LANGID;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES;

typedef STARTUPINFOW STARTUPINFO;
typedef LPSTARTUPINFOW LPSTARTUPINFO;

typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

typedef struct _RTL_SRWLOCK {
    pthread_rwlock_t lock;
    BOOL initialized;
} SRWLOCK, *PSRWLOCK;

#ifndef sprintf_s
#define sprintf_s snprintf
#endif

#ifndef WM_IME_ENDCOMPOSITION
#define WM_IME_ENDCOMPOSITION 0x010E
#endif

#ifndef __cdecl
#define __cdecl
#endif

#ifndef __stdcall
#define __stdcall
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

#ifndef STDMETHODIMP
#define STDMETHODIMP HRESULT
#endif

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif

#ifndef S_OK
#define S_OK ((HRESULT)0L)
#endif

#ifndef S_FALSE
#define S_FALSE ((HRESULT)1L)
#endif

#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0x00000000L
#endif

#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT 0x00000102L
#endif

#ifndef WAIT_FAILED
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)
#endif

#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#endif

#ifndef FILE_ATTRIBUTE_READONLY
#define FILE_ATTRIBUTE_READONLY 0x00000001
#endif

#ifndef FILE_ATTRIBUTE_ARCHIVE
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#endif

#ifndef FILE_FLAG_BACKUP_SEMANTICS
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#endif

#ifndef OPEN_ALWAYS
#define OPEN_ALWAYS 4
#endif

#ifndef TRUNCATE_EXISTING
#define TRUNCATE_EXISTING 5
#endif

#ifndef FILE_SHARE_DELETE
#define FILE_SHARE_DELETE 0x00000004
#endif

#ifndef FILE_APPEND_DATA
#define FILE_APPEND_DATA 0x00000004
#endif

#ifndef FILE_FLAG_SEQUENTIAL_SCAN
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#endif

#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND 2L
#endif

#ifndef ERROR_PATH_NOT_FOUND
#define ERROR_PATH_NOT_FOUND 3L
#endif

#ifndef ERROR_ACCESS_DENIED
#define ERROR_ACCESS_DENIED 5L
#endif

#ifndef ERROR_INVALID_HANDLE
#define ERROR_INVALID_HANDLE 6L
#endif

#ifndef ERROR_NO_MORE_FILES
#define ERROR_NO_MORE_FILES 18L
#endif

#ifndef ERROR_CALL_NOT_IMPLEMENTED
#define ERROR_CALL_NOT_IMPLEMENTED 120L
#endif

#ifndef E_NOTIMPL
#define E_NOTIMPL ((HRESULT)0x80004001L)
#endif

#ifndef E_UNEXPECTED
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#endif

#ifndef CO_E_SERVER_START_TIMEOUT
#define CO_E_SERVER_START_TIMEOUT ((HRESULT)0x80080005L)
#endif

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

#ifndef OFN_SHOWHELP
#define OFN_SHOWHELP 0x00000010
#endif

#ifndef MONITORINFOF_PRIMARY
#define MONITORINFOF_PRIMARY 0x00000001
#endif

#ifndef GWLP_WNDPROC
#define GWLP_WNDPROC (-4)
#endif

#ifndef GWL_WNDPROC
#define GWL_WNDPROC GWLP_WNDPROC
#endif

#ifndef GWL_USERDATA
#define GWL_USERDATA GWLP_USERDATA
#endif

#ifndef GetWindowLong
#define GetWindowLong GetWindowLongW
#endif

#ifndef SetWindowLong
#define SetWindowLong SetWindowLongW
#endif

#ifndef GetWindowLongPtr
#define GetWindowLongPtr GetWindowLongPtrW
#endif

#ifndef SetWindowLongPtr
#define SetWindowLongPtr SetWindowLongPtrW
#endif

#ifndef WM_SHOWWINDOW
#define WM_SHOWWINDOW 0x0018
#endif

#ifndef WM_ACTIVATE
#define WM_ACTIVATE 0x0006
#endif

#ifndef WM_SETICON
#define WM_SETICON 0x0080
#endif

#ifndef ICON_SMALL
#define ICON_SMALL 0
#endif

#ifndef ICON_BIG
#define ICON_BIG 1
#endif

#ifndef SW_SHOWMINNOACTIVE
#define SW_SHOWMINNOACTIVE 7
#endif

#ifndef SWP_SHOWWINDOW
#define SWP_SHOWWINDOW 0x0040
#endif

#ifndef WS_EX_TOPMOST
#define WS_EX_TOPMOST 0x00000008L
#endif

#ifndef WS_EX_APPWINDOW
#define WS_EX_APPWINDOW 0x00040000L
#endif

#ifndef SendMessage
#define SendMessage SendMessageW
#endif

#ifndef SetWindowText
#define SetWindowText SetWindowTextW
#endif

#ifndef GetWindowText
#define GetWindowText GetWindowTextW
#endif

#ifndef CreateWindowEx
#define CreateWindowEx CreateWindowExW
#endif

#ifndef WNDCLASSEX
#define WNDCLASSEX WNDCLASSEXW
#endif

#ifndef PostMessage
#define PostMessage PostMessageW
#endif

#ifndef PeekMessage
#define PeekMessage PeekMessageW
#endif

#ifndef GetMessage
#define GetMessage GetMessageW
#endif

#ifndef FindFirstFile
#define FindFirstFile FindFirstFileW
#endif

#ifndef FindNextFile
#define FindNextFile FindNextFileW
#endif

#ifndef WIN32_FIND_DATA
#define WIN32_FIND_DATA WIN32_FIND_DATAW
#endif

#ifndef DispatchMessage
#define DispatchMessage DispatchMessageW
#endif

#ifndef RegisterClass
#define RegisterClass RegisterClassW
#endif

#ifndef LoadIcon
#define LoadIcon LoadIconW
#endif

#ifndef LoadCursor
#define LoadCursor LoadCursorW
#endif

#ifndef CreateWindow
#define CreateWindow CreateWindowW
#endif

#ifndef ShellExecute
#define ShellExecute ShellExecuteW
#endif

#ifndef CreateMutex
#define CreateMutex CreateMutexW
#endif

#ifndef SHGetFolderPath
#define SHGetFolderPath SHGetFolderPathW
#endif

#ifndef GetLocaleInfo
#define GetLocaleInfo GetLocaleInfoW
#endif

#ifndef GetMonitorInfo
#define GetMonitorInfo GetMonitorInfoW
#endif

EXTERN_C HMONITOR MonitorFromRect(const RECT* lprc, DWORD dwFlags);

#ifndef SHBrowseForFolder
#define SHBrowseForFolder SHBrowseForFolderW
#endif

#ifndef SHGetPathFromIDList
#define SHGetPathFromIDList SHGetPathFromIDListW
#endif

#ifndef TEXT
#define TEXT(value) u##value
#endif

#ifndef _T
#define _T(value) TEXT(value)
#endif

#ifndef ZeroMemory
#define ZeroMemory(ptr, size) memset((ptr), 0, (size))
#endif

#ifndef vsprintf_s
#define vsprintf_s(buffer, size, format, args) vsnprintf((buffer), (size), (format), (args))
#endif

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

typedef short VARIANT_BOOL;
#ifndef VARIANT_TRUE
#define VARIANT_TRUE ((VARIANT_BOOL)-1)
#endif
#ifndef VARIANT_FALSE
#define VARIANT_FALSE ((VARIANT_BOOL)0)
#endif

EXTERN_C int MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);
EXTERN_C int WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte,
    LPCSTR lpDefaultChar, BOOL* lpUsedDefaultChar);
EXTERN_C HANDLE MbGetCurrentProcess(void);
EXTERN_C HANDLE MbGetCurrentThread(void);
EXTERN_C LANGID GetUserDefaultUILanguage(void);
EXTERN_C HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd);
EXTERN_C DWORD GetFileAttributesW(LPCWSTR lpFileName);
EXTERN_C DWORD timeGetTime(void);
EXTERN_C void* CoTaskMemAlloc(SIZE_T cb);
EXTERN_C void CoTaskMemFree(void* pv);
EXTERN_C BOOL ResetEvent(HANDLE hEvent);
EXTERN_C BOOL DeleteFileA(LPCSTR lpFileName);
EXTERN_C BOOL MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName);
EXTERN_C BOOL GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime);
EXTERN_C BOOL FileTimeToSystemTime(const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime);
EXTERN_C DWORD GetShortPathNameW(LPCWSTR lpszLongPath, LPWSTR lpszShortPath, DWORD cchBuffer);
EXTERN_C int lstrcmpW(LPCWSTR lpString1, LPCWSTR lpString2);
EXTERN_C int wsprintfW(LPWSTR lpOut, LPCWSTR lpFmt, ...);
EXTERN_C BOOL CreatePipe(HANDLE* hReadPipe, HANDLE* hWritePipe, LPSECURITY_ATTRIBUTES lpPipeAttributes, DWORD nSize);
EXTERN_C BOOL PeekNamedPipe(HANDLE hNamedPipe, LPVOID lpBuffer, DWORD nBufferSize, LPDWORD lpBytesRead, LPDWORD lpTotalBytesAvail, LPDWORD lpBytesLeftThisMessage);
EXTERN_C BOOL CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
EXTERN_C DWORD GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer);
EXTERN_C DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer);
EXTERN_C UINT RegisterClipboardFormatW(LPCWSTR lpszFormat);
EXTERN_C UINT RegisterClipboardFormatA(LPCSTR lpszFormat);
EXTERN_C void AcquireSRWLockShared(PSRWLOCK SRWLock);
EXTERN_C void ReleaseSRWLockShared(PSRWLOCK SRWLock);
EXTERN_C void AcquireSRWLockExclusive(PSRWLOCK SRWLock);
EXTERN_C void ReleaseSRWLockExclusive(PSRWLOCK SRWLock);
EXTERN_C void InitializeSRWLock(PSRWLOCK SRWLock);
EXTERN_C LONG MB_InterlockedCompareExchange(LONG volatile* destination, LONG exchange, LONG comparand);
EXTERN_C LONG MB_InterlockedExchange(LONG volatile* target, LONG value);
EXTERN_C LONG MB_InterlockedExchangeAdd(LONG volatile* addend, LONG value);
EXTERN_C LONG MB_InterlockedIncrement(LONG volatile* addend);
EXTERN_C LONG MB_InterlockedDecrement(LONG volatile* addend);

#define lstrcmp lstrcmpW
#define wsprintf wsprintfW
#define RegisterClipboardFormat RegisterClipboardFormatW
#define _InterlockedCompareExchange MB_InterlockedCompareExchange
#define _InterlockedExchange MB_InterlockedExchange
#define _InterlockedExchangeAdd MB_InterlockedExchangeAdd
#define _InterlockedIncrement MB_InterlockedIncrement
#define _InterlockedDecrement MB_InterlockedDecrement

EXTERN_C BOOL MacGdiDrawBitmapToContext(HDC hdc, const unsigned char* bitmap, int bitmapWidth, int bitmapHeight, int destX, int destY, int width, int height,
    int srcX, int srcY);
EXTERN_C void MacInitializeApplication(void);
EXTERN_C void* HwndToNSWindow(HWND hwnd);
EXTERN_C void* HwndToNSView(HWND hwnd);

#ifdef __cplusplus
inline size_t mbWideLen(const WCHAR* value)
{
    size_t len = 0;
    if (!value)
        return 0;
    while (value[len])
        ++len;
    return len;
}

inline WCHAR* mbWideCopy(WCHAR* dst, const WCHAR* src)
{
    WCHAR* out = dst;
    while (src && *src)
        *out++ = *src++;
    *out = 0;
    return dst;
}

inline WCHAR* mbWideCat(WCHAR* dst, const WCHAR* src)
{
    mbWideCopy(dst + mbWideLen(dst), src);
    return dst;
}

inline size_t mbWideLen(const wchar_t* value)
{
    size_t len = 0;
    if (!value)
        return 0;
    while (value[len])
        ++len;
    return len;
}

inline std::u16string mbWcharToUtf16(const wchar_t* value)
{
    std::u16string result;
    if (!value)
        return result;
    for (size_t i = 0; value[i]; ++i)
        result.push_back((char16_t)value[i]);
    return result;
}

inline UINT RegisterClipboardFormatW(const wchar_t* lpszFormat)
{
    std::u16string value = mbWcharToUtf16(lpszFormat);
    return RegisterClipboardFormatW((LPCWSTR)value.c_str());
}

inline VOID OutputDebugStringW(const wchar_t* lpOutputString)
{
    std::u16string value = mbWcharToUtf16(lpOutputString);
    OutputDebugStringW((LPCWSTR)value.c_str());
}

inline HANDLE GetCurrentProcess(void)
{
    return MbGetCurrentProcess();
}

inline HANDLE GetCurrentThread(void)
{
    return MbGetCurrentThread();
}

#else
#define GetCurrentProcess MbGetCurrentProcess
#define GetCurrentThread MbGetCurrentThread
#endif

#endif // WINDOWS_FOR_MAC_H
