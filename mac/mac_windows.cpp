#include "mac/windows.h"
#include "mac/mac_window.h"
#include "mac/shlwapi.h"

#include <CoreGraphics/CoreGraphics.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <execinfo.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

extern char** environ;

namespace {

thread_local DWORD g_lastError = 0;
BOOL g_isLinuxOpenglDraw = FALSE;

enum MacHandleKind {
    kMacHandleThread = 1,
    kMacHandleFile,
    kMacHandleEvent,
    kMacHandleFind,
};

struct MacHandle {
    uint32_t magic = 0x6d626d68;
    MacHandleKind kind;
};

struct MacThreadHandle : MacHandle {
    pthread_t thread = 0;
    bool joined = false;
};

struct MacFileHandle : MacHandle {
    int fd = -1;
};

struct MacEventHandle : MacHandle {
    std::mutex mutex;
    std::condition_variable condition;
    bool manualReset = false;
    bool signaled = false;
};

struct MacFindHandle : MacHandle {
    std::vector<std::string> paths;
    size_t nextIndex = 0;
};

struct GlobalMemoryHeader {
    size_t size;
};

static MacHandle* asHandle(HANDLE handle)
{
    MacHandle* macHandle = (MacHandle*)handle;
    if (!macHandle || macHandle == (MacHandle*)INVALID_HANDLE_VALUE || macHandle->magic != 0x6d626d68)
        return nullptr;
    return macHandle;
}

static size_t wideLen(LPCWSTR value)
{
    size_t len = 0;
    if (!value)
        return 0;
    while (value[len])
        ++len;
    return len;
}

static std::string wideToUtf8(LPCWSTR value, int cchWideChar = -1)
{
    std::string result;
    if (!value)
        return result;
    int index = 0;
    while ((cchWideChar < 0 && value[index]) || (cchWideChar >= 0 && index < cchWideChar)) {
        uint32_t cp = value[index++];
        if (cp == 0 && cchWideChar < 0)
            break;
        if (0xd800 <= cp && cp <= 0xdbff && ((cchWideChar < 0 && value[index]) || (cchWideChar >= 0 && index < cchWideChar))) {
            uint32_t low = value[index];
            if (0xdc00 <= low && low <= 0xdfff) {
                ++index;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            }
        }
        if (cp < 0x80) {
            result.push_back((char)cp);
        } else if (cp < 0x800) {
            result.push_back((char)(0xc0 | (cp >> 6)));
            result.push_back((char)(0x80 | (cp & 0x3f)));
        } else if (cp < 0x10000) {
            result.push_back((char)(0xe0 | (cp >> 12)));
            result.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
            result.push_back((char)(0x80 | (cp & 0x3f)));
        } else {
            result.push_back((char)(0xf0 | (cp >> 18)));
            result.push_back((char)(0x80 | ((cp >> 12) & 0x3f)));
            result.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
            result.push_back((char)(0x80 | (cp & 0x3f)));
        }
    }
    return result;
}

static std::u16string utf8ToWide(const char* value, int cbMultiByte = -1)
{
    std::u16string result;
    if (!value)
        return result;

    int len = cbMultiByte < 0 ? (int)strlen(value) : cbMultiByte;
    for (int i = 0; i < len;) {
        unsigned char c = (unsigned char)value[i++];
        if (c == 0 && cbMultiByte < 0)
            break;

        uint32_t cp = 0xfffd;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i < len) {
            cp = ((c & 0x1f) << 6) | ((unsigned char)value[i++] & 0x3f);
        } else if ((c >> 4) == 0xe && i + 1 < len) {
            cp = ((c & 0x0f) << 12) | (((unsigned char)value[i++] & 0x3f) << 6);
            cp |= ((unsigned char)value[i++] & 0x3f);
        } else if ((c >> 3) == 0x1e && i + 2 < len) {
            cp = ((c & 0x07) << 18) | (((unsigned char)value[i++] & 0x3f) << 12);
            cp |= (((unsigned char)value[i++] & 0x3f) << 6);
            cp |= ((unsigned char)value[i++] & 0x3f);
        }

        if (cp < 0x10000) {
            result.push_back((char16_t)cp);
        } else {
            cp -= 0x10000;
            result.push_back((char16_t)(0xd800 + (cp >> 10)));
            result.push_back((char16_t)(0xdc00 + (cp & 0x3ff)));
        }
    }
    return result;
}

static DWORD copyWideToBuffer(const std::u16string& value, LPWSTR buffer, DWORD cchBuffer)
{
    DWORD required = (DWORD)value.size();
    if (!buffer || cchBuffer == 0)
        return required;
    DWORD copied = std::min<DWORD>(required, cchBuffer - 1);
    for (DWORD i = 0; i < copied; ++i)
        buffer[i] = (WCHAR)value[i];
    buffer[copied] = 0;
    return required;
}

static DWORD errnoToWinError(int value)
{
    switch (value) {
    case ENOENT:
        return ERROR_FILE_NOT_FOUND;
    case ENOTDIR:
        return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
        return ERROR_ACCESS_DENIED;
    case EBADF:
        return ERROR_INVALID_HANDLE;
    default:
        return (DWORD)value;
    }
}

static void fillFileTime(time_t seconds, FILETIME* fileTime)
{
    if (!fileTime)
        return;
    uint64_t ticks = ((uint64_t)seconds + 11644473600ULL) * 10000000ULL;
    fileTime->dwLowDateTime = (DWORD)(ticks & 0xffffffff);
    fileTime->dwHighDateTime = (DWORD)(ticks >> 32);
}

static void fillFindData(const std::string& path, const struct stat& st, LPWIN32_FIND_DATAW data)
{
    memset(data, 0, sizeof(*data));
    data->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    fillFileTime(st.st_ctime, &data->ftCreationTime);
    fillFileTime(st.st_atime, &data->ftLastAccessTime);
    fillFileTime(st.st_mtime, &data->ftLastWriteTime);
    data->nFileSizeHigh = (DWORD)(((uint64_t)st.st_size) >> 32);
    data->nFileSizeLow = (DWORD)(((uint64_t)st.st_size) & 0xffffffff);
    std::string name = path;
    size_t slash = name.find_last_of('/');
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    std::u16string wideName = utf8ToWide(name.c_str(), -1);
    DWORD copied = std::min<DWORD>((DWORD)wideName.size(), MAX_PATH - 1);
    for (DWORD i = 0; i < copied; ++i)
        data->cFileName[i] = (WCHAR)wideName[i];
    data->cFileName[copied] = 0;
}

static bool wildcardMatch(const char* pattern, const char* text)
{
    if (!pattern || !text)
        return false;
    if (strcmp(pattern, "*.*") == 0 || strcmp(pattern, "*") == 0)
        return true;
    while (*pattern) {
        if (*pattern == '*') {
            ++pattern;
            if (!*pattern)
                return true;
            while (*text) {
                if (wildcardMatch(pattern, text))
                    return true;
                ++text;
            }
            return false;
        }
        if (*pattern == '?') {
            if (!*text)
                return false;
            ++pattern;
            ++text;
            continue;
        }
        if (*pattern != *text)
            return false;
        ++pattern;
        ++text;
    }
    return *text == 0;
}

static void splitFindPattern(const std::string& pattern, std::string* directory, std::string* leafPattern)
{
    size_t slash = pattern.find_last_of("/\\");
    if (slash == std::string::npos) {
        *directory = ".";
        *leafPattern = pattern;
        return;
    }
    *directory = pattern.substr(0, slash);
    if (directory->empty())
        *directory = "/";
    *leafPattern = pattern.substr(slash + 1);
    if (leafPattern->empty())
        *leafPattern = "*";
}

static int openFlagsFromDisposition(DWORD desiredAccess, DWORD creationDisposition)
{
    int flags = 0;
    bool read = (desiredAccess & GENERIC_READ) != 0;
    bool write = (desiredAccess & (GENERIC_WRITE | FILE_APPEND_DATA)) != 0;
    if (read && write)
        flags |= O_RDWR;
    else if (write)
        flags |= O_WRONLY;
    else
        flags |= O_RDONLY;

    switch (creationDisposition) {
    case CREATE_NEW:
        flags |= O_CREAT | O_EXCL;
        break;
    case CREATE_ALWAYS:
        flags |= O_CREAT | O_TRUNC;
        break;
    case OPEN_ALWAYS:
        flags |= O_CREAT;
        break;
    case TRUNCATE_EXISTING:
        flags |= O_TRUNC;
        break;
    case OPEN_EXISTING:
    default:
        break;
    }
    if (desiredAccess & FILE_APPEND_DATA)
        flags |= O_APPEND;
    return flags;
}

static void fillSystemTime(const tm& tmValue, int milliseconds, LPSYSTEMTIME systemTime)
{
    if (!systemTime)
        return;
    systemTime->wYear = tmValue.tm_year + 1900;
    systemTime->wMonth = tmValue.tm_mon + 1;
    systemTime->wDayOfWeek = tmValue.tm_wday;
    systemTime->wDay = tmValue.tm_mday;
    systemTime->wHour = tmValue.tm_hour;
    systemTime->wMinute = tmValue.tm_min;
    systemTime->wSecond = tmValue.tm_sec;
    systemTime->wMilliseconds = milliseconds;
}

} // namespace

extern "C" LONG MB_InterlockedCompareExchange(LONG volatile* destination, LONG exchange, LONG comparand)
{
    return __sync_val_compare_and_swap(destination, comparand, exchange);
}

extern "C" LONG MB_InterlockedExchange(LONG volatile* target, LONG value)
{
    return __sync_lock_test_and_set(target, value);
}

extern "C" LONG MB_InterlockedExchangeAdd(LONG volatile* addend, LONG value)
{
    return __sync_fetch_and_add(addend, value);
}

extern "C" LONG MB_InterlockedIncrement(LONG volatile* addend)
{
    return __sync_add_and_fetch(addend, 1);
}

extern "C" LONG MB_InterlockedDecrement(LONG volatile* addend)
{
    return __sync_sub_and_fetch(addend, 1);
}

extern "C" void DebugBreak(void)
{
    void* frames[64];
    int frameCount = backtrace(frames, 64);
    backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);
    raise(SIGTRAP);
}

extern "C" VOID OutputDebugStringA(LPCSTR lpOutputString)
{
    if (lpOutputString)
        fputs(lpOutputString, stderr);
}

extern "C" VOID OutputDebugStringW(LPCWSTR lpOutputString)
{
    std::string utf8 = wideToUtf8(lpOutputString);
    OutputDebugStringA(utf8.c_str());
}

extern "C" void* CoTaskMemAlloc(SIZE_T cb)
{
    return malloc(cb);
}

extern "C" void CoTaskMemFree(void* pv)
{
    free(pv);
}

extern "C" int MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
{
    if (!lpMultiByteStr)
        return 0;
    std::u16string wide = utf8ToWide(lpMultiByteStr, cbMultiByte);
    bool includeNull = cbMultiByte < 0;
    int required = (int)wide.size() + (includeNull ? 1 : 0);
    if (!lpWideCharStr || cchWideChar == 0)
        return required;
    int copied = std::min<int>((int)wide.size(), cchWideChar - (includeNull ? 1 : 0));
    for (int i = 0; i < copied; ++i)
        lpWideCharStr[i] = (WCHAR)wide[i];
    if (includeNull && copied < cchWideChar)
        lpWideCharStr[copied] = 0;
    return copied + (includeNull && copied < cchWideChar ? 1 : 0);
}

extern "C" int WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte,
    LPCSTR lpDefaultChar, BOOL* lpUsedDefaultChar)
{
    if (lpUsedDefaultChar)
        *lpUsedDefaultChar = FALSE;
    if (!lpWideCharStr)
        return 0;
    std::string utf8 = wideToUtf8(lpWideCharStr, cchWideChar);
    bool includeNull = cchWideChar < 0;
    int required = (int)utf8.size() + (includeNull ? 1 : 0);
    if (!lpMultiByteStr || cbMultiByte == 0)
        return required;
    int copied = std::min<int>((int)utf8.size(), cbMultiByte - (includeNull ? 1 : 0));
    memcpy(lpMultiByteStr, utf8.data(), copied);
    if (includeNull && copied < cbMultiByte)
        lpMultiByteStr[copied] = '\0';
    return copied + (includeNull && copied < cbMultiByte ? 1 : 0);
}

typedef unsigned int (*MacThreadStart)(void*);

struct ThreadStartInfo {
    MacThreadStart start = nullptr;
    void* argument = nullptr;
};

static void* threadStartThunk(void* argument)
{
    std::unique_ptr<ThreadStartInfo> info((ThreadStartInfo*)argument);
    if (info->start)
        info->start(info->argument);
    return nullptr;
}

extern "C" uintptr_t _beginthreadex(void* security, unsigned int stackSize, unsigned int (*startAddress)(void*), void* arglist, unsigned int initflag, unsigned int* thrdaddr)
{
    MacThreadHandle* handle = new MacThreadHandle();
    handle->kind = kMacHandleThread;
    ThreadStartInfo* info = new ThreadStartInfo();
    info->start = (MacThreadStart)startAddress;
    info->argument = arglist;
    int result = pthread_create(&handle->thread, nullptr, threadStartThunk, info);
    if (result != 0) {
        delete info;
        delete handle;
        g_lastError = errnoToWinError(result);
        return 0;
    }
    if (thrdaddr)
        *thrdaddr = (unsigned int)(uintptr_t)handle->thread;
    return (uintptr_t)handle;
}

extern "C" HANDLE CreateThread(void* lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId)
{
    return (HANDLE)_beginthreadex(lpThreadAttributes, (unsigned int)dwStackSize, (MacThreadStart)lpStartAddress, lpParameter, dwCreationFlags, lpThreadId);
}

extern "C" BOOL CloseHandle(HANDLE hObject)
{
    MacHandle* handle = asHandle(hObject);
    if (!handle)
        return FALSE;

    if (handle->kind == kMacHandleFile) {
        MacFileHandle* file = (MacFileHandle*)handle;
        if (file->fd >= 0)
            close(file->fd);
        delete file;
        return TRUE;
    }
    if (handle->kind == kMacHandleThread) {
        MacThreadHandle* thread = (MacThreadHandle*)handle;
        if (!thread->joined)
            pthread_detach(thread->thread);
        delete thread;
        return TRUE;
    }
    if (handle->kind == kMacHandleEvent) {
        delete (MacEventHandle*)handle;
        return TRUE;
    }
    if (handle->kind == kMacHandleFind) {
        delete (MacFindHandle*)handle;
        return TRUE;
    }
    return FALSE;
}

extern "C" VOID Sleep(DWORD dwMilliseconds)
{
    usleep((useconds_t)dwMilliseconds * 1000);
}

extern "C" HANDLE CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName)
{
    MacEventHandle* event = new MacEventHandle();
    event->kind = kMacHandleEvent;
    event->manualReset = !!bManualReset;
    event->signaled = !!bInitialState;
    return event;
}

extern "C" BOOL SetEvent(HANDLE hEvent)
{
    MacHandle* handle = asHandle(hEvent);
    if (!handle || handle->kind != kMacHandleEvent)
        return FALSE;
    MacEventHandle* event = (MacEventHandle*)handle;
    {
        std::lock_guard<std::mutex> lock(event->mutex);
        event->signaled = true;
    }
    if (event->manualReset)
        event->condition.notify_all();
    else
        event->condition.notify_one();
    return TRUE;
}

extern "C" BOOL ResetEvent(HANDLE hEvent)
{
    MacHandle* handle = asHandle(hEvent);
    if (!handle || handle->kind != kMacHandleEvent)
        return FALSE;
    MacEventHandle* event = (MacEventHandle*)handle;
    std::lock_guard<std::mutex> lock(event->mutex);
    event->signaled = false;
    return TRUE;
}

extern "C" DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
    MacHandle* handle = asHandle(hHandle);
    if (!handle)
        return WAIT_FAILED;

    if (handle->kind == kMacHandleEvent) {
        MacEventHandle* event = (MacEventHandle*)handle;
        std::unique_lock<std::mutex> lock(event->mutex);
        bool signaled = false;
        if (dwMilliseconds == INFINITE) {
            event->condition.wait(lock, [&] { return event->signaled; });
            signaled = true;
        } else {
            signaled = event->condition.wait_for(lock, std::chrono::milliseconds(dwMilliseconds), [&] { return event->signaled; });
        }
        if (!signaled)
            return WAIT_TIMEOUT;
        if (!event->manualReset)
            event->signaled = false;
        return WAIT_OBJECT_0;
    }

    if (handle->kind == kMacHandleThread) {
        MacThreadHandle* thread = (MacThreadHandle*)handle;
        if (dwMilliseconds != INFINITE && dwMilliseconds != 0)
            return WAIT_TIMEOUT;
        if (dwMilliseconds == 0)
            return WAIT_TIMEOUT;
        if (!thread->joined) {
            pthread_join(thread->thread, nullptr);
            thread->joined = true;
        }
        return WAIT_OBJECT_0;
    }

    return WAIT_FAILED;
}

extern "C" DWORD TlsAlloc(void)
{
    pthread_key_t key;
    if (pthread_key_create(&key, nullptr) != 0)
        return (DWORD)-1;
    return (DWORD)key;
}

extern "C" LPVOID TlsGetValue(DWORD dwTlsIndex)
{
    return pthread_getspecific((pthread_key_t)dwTlsIndex);
}

extern "C" BOOL TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue)
{
    return pthread_setspecific((pthread_key_t)dwTlsIndex, lpTlsValue) == 0;
}

extern "C" BOOL TlsFree(DWORD dwTlsIndex)
{
    return pthread_key_delete((pthread_key_t)dwTlsIndex) == 0;
}

extern "C" uintptr_t MbTlsAlloc(void)
{
    return (uintptr_t)TlsAlloc();
}

extern "C" LPVOID MbTlsGetValue(uintptr_t dwTlsIndex)
{
    return TlsGetValue((DWORD)dwTlsIndex);
}

extern "C" BOOL MbTlsSetValue(uintptr_t dwTlsIndex, LPVOID lpTlsValue)
{
    return TlsSetValue((DWORD)dwTlsIndex, lpTlsValue);
}

extern "C" BOOL MbTlsFree(uintptr_t dwTlsIndex)
{
    return TlsFree((DWORD)dwTlsIndex);
}

extern "C" DWORD GetCurrentThreadId(void)
{
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return (DWORD)tid;
}

extern "C" DWORD GetCurrentProcessId(void)
{
    return (DWORD)getpid();
}

extern "C" HANDLE MbGetCurrentProcess(void)
{
    return (HANDLE)(intptr_t)-1;
}

extern "C" HANDLE MbGetCurrentThread(void)
{
    return (HANDLE)(uintptr_t)GetCurrentThreadId();
}

extern "C" LANGID GetUserDefaultUILanguage(void)
{
    return LANG_USER_DEFAULT;
}

extern "C" HMODULE GetModuleHandleW(LPCWSTR lpModuleName)
{
    return nullptr;
}

extern "C" DWORD GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
    char path[4096] = { 0 };
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
        return 0;
    char resolved[4096] = { 0 };
    const char* finalPath = realpath(path, resolved) ? resolved : path;
    std::u16string wide = utf8ToWide(finalPath, -1);
    DWORD required = copyWideToBuffer(wide, lpFilename, nSize);
    return std::min<DWORD>(required, nSize ? nSize - 1 : 0);
}

extern "C" HMODULE LoadLibraryW(LPCWSTR lpLibFileName)
{
    std::string path = wideToUtf8(lpLibFileName);
    void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
        g_lastError = ERROR_FILE_NOT_FOUND;
    return (HMODULE)handle;
}

extern "C" void* GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
    return dlsym(hModule ? hModule : RTLD_DEFAULT, lpProcName);
}

extern "C" BOOL FreeLibrary(HMODULE hLibModule)
{
    return hLibModule ? (dlclose(hLibModule) == 0) : FALSE;
}

extern "C" HANDLE CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    if (!lpFileName)
        return INVALID_HANDLE_VALUE;
    int fd = open(lpFileName, openFlagsFromDisposition(dwDesiredAccess, dwCreationDisposition), 0666);
    if (fd < 0) {
        g_lastError = errnoToWinError(errno);
        return INVALID_HANDLE_VALUE;
    }
    MacFileHandle* file = new MacFileHandle();
    file->kind = kMacHandleFile;
    file->fd = fd;
    return file;
}

extern "C" HANDLE CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    std::string path = wideToUtf8(lpFileName);
    return CreateFileA(path.c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

extern "C" BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, void* lpOverlapped)
{
    MacHandle* handle = asHandle(hFile);
    if (!handle || handle->kind != kMacHandleFile)
        return FALSE;
    ssize_t readSize = read(((MacFileHandle*)handle)->fd, lpBuffer, nNumberOfBytesToRead);
    if (readSize < 0) {
        g_lastError = errnoToWinError(errno);
        return FALSE;
    }
    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = (DWORD)readSize;
    return TRUE;
}

extern "C" BOOL WriteFile(HANDLE hFile, const VOID* lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, void* lpOverlapped)
{
    MacHandle* handle = asHandle(hFile);
    if (!handle || handle->kind != kMacHandleFile)
        return FALSE;
    ssize_t writtenSize = write(((MacFileHandle*)handle)->fd, lpBuffer, nNumberOfBytesToWrite);
    if (writtenSize < 0) {
        g_lastError = errnoToWinError(errno);
        return FALSE;
    }
    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = (DWORD)writtenSize;
    return TRUE;
}

extern "C" DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
    MacHandle* handle = asHandle(hFile);
    if (!handle || handle->kind != kMacHandleFile)
        return (DWORD)-1;
    struct stat st;
    if (fstat(((MacFileHandle*)handle)->fd, &st) != 0)
        return (DWORD)-1;
    if (lpFileSizeHigh)
        *lpFileSizeHigh = (DWORD)(((uint64_t)st.st_size) >> 32);
    return (DWORD)(((uint64_t)st.st_size) & 0xffffffff);
}

extern "C" DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, LONG* lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
    MacHandle* handle = asHandle(hFile);
    if (!handle || handle->kind != kMacHandleFile)
        return INVALID_SET_FILE_POINTER;
    int whence = SEEK_SET;
    if (dwMoveMethod == FILE_CURRENT)
        whence = SEEK_CUR;
    else if (dwMoveMethod == FILE_END)
        whence = SEEK_END;
    int64_t distance = lDistanceToMove;
    if (lpDistanceToMoveHigh)
        distance |= ((int64_t)*lpDistanceToMoveHigh) << 32;
    off_t result = lseek(((MacFileHandle*)handle)->fd, (off_t)distance, whence);
    if (result < 0)
        return INVALID_SET_FILE_POINTER;
    if (lpDistanceToMoveHigh)
        *lpDistanceToMoveHigh = (LONG)(((uint64_t)result) >> 32);
    return (DWORD)(((uint64_t)result) & 0xffffffff);
}

extern "C" BOOL GetFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation)
{
    MacHandle* handle = asHandle(hFile);
    if (!handle || handle->kind != kMacHandleFile || !lpFileInformation)
        return FALSE;
    struct stat st;
    if (fstat(((MacFileHandle*)handle)->fd, &st) != 0)
        return FALSE;
    memset(lpFileInformation, 0, sizeof(*lpFileInformation));
    lpFileInformation->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    fillFileTime(st.st_ctime, &lpFileInformation->ftCreationTime);
    fillFileTime(st.st_atime, &lpFileInformation->ftLastAccessTime);
    fillFileTime(st.st_mtime, &lpFileInformation->ftLastWriteTime);
    lpFileInformation->nFileSizeHigh = (DWORD)(((uint64_t)st.st_size) >> 32);
    lpFileInformation->nFileSizeLow = (DWORD)(((uint64_t)st.st_size) & 0xffffffff);
    lpFileInformation->nNumberOfLinks = (DWORD)st.st_nlink;
    lpFileInformation->nFileIndexLow = (DWORD)st.st_ino;
    return TRUE;
}

extern "C" BOOL DeleteFileW(LPCWSTR lpFileName)
{
    std::string path = wideToUtf8(lpFileName);
    return unlink(path.c_str()) == 0;
}

extern "C" BOOL DeleteFileA(LPCSTR lpFileName)
{
    return lpFileName && unlink(lpFileName) == 0;
}

extern "C" BOOL MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName)
{
    std::string from = wideToUtf8(lpExistingFileName);
    std::string to = wideToUtf8(lpNewFileName);
    if (rename(from.c_str(), to.c_str()) == 0)
        return TRUE;
    g_lastError = errnoToWinError(errno);
    return FALSE;
}

extern "C" HANDLE FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
    if (!lpFileName || !lpFindFileData)
        return INVALID_HANDLE_VALUE;

    std::string pattern = wideToUtf8(lpFileName);
    std::string directory;
    std::string leafPattern;
    splitFindPattern(pattern, &directory, &leafPattern);

    MacFindHandle* findHandle = new MacFindHandle();
    findHandle->kind = kMacHandleFind;

    DIR* dir = opendir(directory.c_str());
    if (dir) {
        while (dirent* entry = readdir(dir)) {
            if (!wildcardMatch(leafPattern.c_str(), entry->d_name))
                continue;
            std::string child = directory;
            if (!child.empty() && child.back() != '/')
                child.push_back('/');
            child += entry->d_name;
            findHandle->paths.push_back(child);
        }
        closedir(dir);
    } else if (leafPattern.find('*') == std::string::npos && leafPattern.find('?') == std::string::npos) {
        struct stat st;
        if (stat(pattern.c_str(), &st) == 0)
            findHandle->paths.push_back(pattern);
    }

    if (findHandle->paths.empty()) {
        delete findHandle;
        g_lastError = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }

    struct stat st;
    if (stat(findHandle->paths[0].c_str(), &st) != 0) {
        delete findHandle;
        g_lastError = errnoToWinError(errno);
        return INVALID_HANDLE_VALUE;
    }
    fillFindData(findHandle->paths[0], st, lpFindFileData);
    findHandle->nextIndex = 1;
    return findHandle;
}

extern "C" BOOL FindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData)
{
    MacHandle* handle = asHandle(hFindFile);
    if (!handle || handle->kind != kMacHandleFind || !lpFindFileData) {
        g_lastError = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    MacFindHandle* findHandle = (MacFindHandle*)handle;
    while (findHandle->nextIndex < findHandle->paths.size()) {
        const std::string& path = findHandle->paths[findHandle->nextIndex++];
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            fillFindData(path, st, lpFindFileData);
            return TRUE;
        }
    }
    g_lastError = ERROR_NO_MORE_FILES;
    return FALSE;
}

extern "C" BOOL FindClose(HANDLE hFindFile)
{
    return CloseHandle(hFindFile);
}

extern "C" BOOL GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime)
{
    MacHandle* handle = asHandle(hFile);
    if (!handle || handle->kind != kMacHandleFile)
        return FALSE;
    struct stat st;
    if (fstat(((MacFileHandle*)handle)->fd, &st) != 0)
        return FALSE;
    fillFileTime(st.st_ctime, lpCreationTime);
    fillFileTime(st.st_atime, lpLastAccessTime);
    fillFileTime(st.st_mtime, lpLastWriteTime);
    return TRUE;
}

extern "C" BOOL FileTimeToSystemTime(const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime)
{
    if (!lpFileTime || !lpSystemTime)
        return FALSE;
    uint64_t ticks = ((uint64_t)lpFileTime->dwHighDateTime << 32) | lpFileTime->dwLowDateTime;
    time_t seconds = (time_t)(ticks / 10000000ULL - 11644473600ULL);
    tm value;
    if (!gmtime_r(&seconds, &value))
        return FALSE;
    fillSystemTime(value, 0, lpSystemTime);
    return TRUE;
}

extern "C" DWORD GetFileAttributesW(LPCWSTR lpFileName)
{
    std::string path = wideToUtf8(lpFileName);
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        g_lastError = errnoToWinError(errno);
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD attributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(st.st_mode & S_IWUSR))
        attributes |= FILE_ATTRIBUTE_READONLY;
    return attributes;
}

extern "C" DWORD GetShortPathNameW(LPCWSTR lpszLongPath, LPWSTR lpszShortPath, DWORD cchBuffer)
{
    std::u16string path;
    if (lpszLongPath) {
        for (size_t i = 0; lpszLongPath[i]; ++i)
            path.push_back((char16_t)lpszLongPath[i]);
    }
    return copyWideToBuffer(path, lpszShortPath, cchBuffer);
}

extern "C" int lstrcmpW(LPCWSTR lpString1, LPCWSTR lpString2)
{
    size_t i = 0;
    for (;; ++i) {
        WCHAR a = lpString1 ? lpString1[i] : 0;
        WCHAR b = lpString2 ? lpString2[i] : 0;
        if (a != b)
            return a < b ? -1 : 1;
        if (!a)
            return 0;
    }
}

extern "C" int wsprintfW(LPWSTR lpOut, LPCWSTR lpFmt, ...)
{
    va_list args;
    va_start(args, lpFmt);
    int written = 0;
    LPWSTR out = lpOut;
    for (const WCHAR* p = lpFmt; p && *p; ++p) {
        if (*p == '%' && p[1] == 's') {
            p++;
            LPCWSTR value = va_arg(args, LPCWSTR);
            while (value && *value) {
                *out++ = *value++;
                ++written;
            }
        } else {
            *out++ = *p;
            ++written;
        }
    }
    if (out)
        *out = 0;
    va_end(args);
    return written;
}

extern "C" BOOL PathFileExistsW(LPCWSTR pszPath)
{
    std::string path = wideToUtf8(pszPath);
    return access(path.c_str(), F_OK) == 0;
}

extern "C" BOOL PathIsDirectoryW(LPCWSTR pszPath)
{
    std::string path = wideToUtf8(pszPath);
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

extern "C" BOOL PathAppendW(LPWSTR pszPath, LPCWSTR pMore)
{
    if (!pszPath || !pMore)
        return FALSE;
    size_t len = wideLen(pszPath);
    size_t moreLen = wideLen(pMore);
    if (len + moreLen + 2 >= MAX_PATH)
        return FALSE;
    if (len > 0 && pszPath[len - 1] != '/' && pszPath[len - 1] != '\\')
        pszPath[len++] = '/';
    for (size_t i = 0; i < moreLen; ++i)
        pszPath[len + i] = pMore[i];
    pszPath[len + moreLen] = 0;
    return TRUE;
}

extern "C" BOOL PathRemoveFileSpecW(LPWSTR pszPath)
{
    if (!pszPath)
        return FALSE;
    size_t len = wideLen(pszPath);
    while (len > 0 && pszPath[len - 1] != '/' && pszPath[len - 1] != '\\')
        --len;
    if (len == 0)
        return FALSE;
    pszPath[len ? len - 1 : 0] = 0;
    return TRUE;
}

extern "C" LPWSTR PathFindFileNameW(LPWSTR pszPath)
{
    if (!pszPath)
        return nullptr;
    LPWSTR result = pszPath;
    for (LPWSTR p = pszPath; *p; ++p) {
        if (*p == '/' || *p == '\\')
            result = p + 1;
    }
    return result;
}

extern "C" void PathStripPathW(LPWSTR pszPath)
{
    LPWSTR fileName = PathFindFileNameW(pszPath);
    if (fileName && fileName != pszPath) {
        while (*fileName)
            *pszPath++ = *fileName++;
        *pszPath = 0;
    }
}

extern "C" DWORD GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer)
{
    const char* tmp = getenv("TMPDIR");
    if (!tmp)
        tmp = "/tmp/";
    std::u16string wide = utf8ToWide(tmp, -1);
    if (!wide.empty() && wide.back() != '/')
        wide.push_back('/');
    return copyWideToBuffer(wide, lpBuffer, nBufferLength);
}

extern "C" DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
{
    char path[4096] = { 0 };
    if (!getcwd(path, sizeof(path)))
        return 0;
    std::u16string wide = utf8ToWide(path, -1);
    return copyWideToBuffer(wide, lpBuffer, nBufferLength);
}

extern "C" BOOL CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    std::string path = wideToUtf8(lpPathName);
    if (mkdir(path.c_str(), 0777) == 0)
        return TRUE;
    if (errno == EEXIST)
        return TRUE;
    g_lastError = errnoToWinError(errno);
    return FALSE;
}

extern "C" BOOL RemoveDirectoryW(LPCWSTR lpPathName)
{
    std::string path = wideToUtf8(lpPathName);
    return rmdir(path.c_str()) == 0;
}

extern "C" int SHCreateDirectoryExW(HWND hwnd, LPCWSTR pszPath, const void* psa)
{
    std::string path = wideToUtf8(pszPath);
    std::string current;
    for (char ch : path) {
        current.push_back(ch);
        if (ch == '/' && current.size() > 1)
            mkdir(current.c_str(), 0777);
    }
    if (mkdir(path.c_str(), 0777) == 0 || errno == EEXIST)
        return ERROR_SUCCESS;
    return errnoToWinError(errno);
}

extern "C" HRESULT SHGetFolderPathW(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath)
{
    if (!pszPath)
        return E_INVALIDARG;
    const char* home = getenv("HOME");
    if (!home)
        home = "/tmp";
    std::string path(home);
    int baseCsidl = csidl & ~CSIDL_FLAG_CREATE;
    if (baseCsidl == CSIDL_APPDATA || baseCsidl == CSIDL_LOCAL_APPDATA)
        path += "/Library/Application Support";
    if (csidl & CSIDL_FLAG_CREATE)
        SHCreateDirectoryExW(hwnd, (LPCWSTR)utf8ToWide(path.c_str(), -1).c_str(), nullptr);
    std::u16string wide = utf8ToWide(path.c_str(), -1);
    copyWideToBuffer(wide, pszPath, MAX_PATH);
    return S_OK;
}

extern "C" DWORD GetLastError(void)
{
    return g_lastError;
}

extern "C" VOID SetLastError(DWORD dwErrCode)
{
    g_lastError = dwErrCode;
}

static void ensureSRWLockInitialized(PSRWLOCK lock)
{
    if (!lock || lock->initialized)
        return;
    pthread_rwlock_init(&lock->lock, nullptr);
    lock->initialized = TRUE;
}

extern "C" void InitializeSRWLock(PSRWLOCK SRWLock)
{
    if (!SRWLock)
        return;
    if (SRWLock->initialized)
        pthread_rwlock_destroy(&SRWLock->lock);
    pthread_rwlock_init(&SRWLock->lock, nullptr);
    SRWLock->initialized = TRUE;
}

extern "C" void AcquireSRWLockShared(PSRWLOCK SRWLock)
{
    ensureSRWLockInitialized(SRWLock);
    pthread_rwlock_rdlock(&SRWLock->lock);
}

extern "C" void ReleaseSRWLockShared(PSRWLOCK SRWLock)
{
    pthread_rwlock_unlock(&SRWLock->lock);
}

extern "C" void AcquireSRWLockExclusive(PSRWLOCK SRWLock)
{
    ensureSRWLockInitialized(SRWLock);
    pthread_rwlock_wrlock(&SRWLock->lock);
}

extern "C" void ReleaseSRWLockExclusive(PSRWLOCK SRWLock)
{
    pthread_rwlock_unlock(&SRWLock->lock);
}

extern "C" void InitializeCriticalSection(CRITICAL_SECTION* lpCriticalSection)
{
    lpCriticalSection->LockCount = 0;
    lpCriticalSection->RecursionCount = 0;
    lpCriticalSection->OwningThread = 0;
    lpCriticalSection->LockSemaphore = 0;
    lpCriticalSection->SpinCount = 0;
}

static void lockCriticalSectionSpin(CRITICAL_SECTION* section)
{
    while (!__sync_bool_compare_and_swap((long volatile*)&section->SpinCount, 0, 1))
        sched_yield();
}

static void unlockCriticalSectionSpin(CRITICAL_SECTION* section)
{
    __sync_lock_release((long volatile*)&section->SpinCount);
}

extern "C" void EnterCriticalSection(CRITICAL_SECTION* lpCriticalSection)
{
    uintptr_t threadId = GetCurrentThreadId();
    for (;;) {
        lockCriticalSectionSpin(lpCriticalSection);
        if (lpCriticalSection->OwningThread == 0 || lpCriticalSection->OwningThread == threadId) {
            lpCriticalSection->OwningThread = threadId;
            ++lpCriticalSection->RecursionCount;
            unlockCriticalSectionSpin(lpCriticalSection);
            return;
        }
        unlockCriticalSectionSpin(lpCriticalSection);
        Sleep(1);
    }
}

extern "C" void LeaveCriticalSection(CRITICAL_SECTION* lpCriticalSection)
{
    uintptr_t threadId = GetCurrentThreadId();
    lockCriticalSectionSpin(lpCriticalSection);
    if (lpCriticalSection->OwningThread == threadId && lpCriticalSection->RecursionCount > 0) {
        --lpCriticalSection->RecursionCount;
        if (lpCriticalSection->RecursionCount == 0)
            lpCriticalSection->OwningThread = 0;
    }
    unlockCriticalSectionSpin(lpCriticalSection);
}

extern "C" VOID DeleteCriticalSection(CRITICAL_SECTION* lpCriticalSection)
{
    memset(lpCriticalSection, 0, sizeof(*lpCriticalSection));
}

extern "C" BOOL TryEnterCriticalSection(CRITICAL_SECTION* lpCriticalSection)
{
    uintptr_t threadId = GetCurrentThreadId();
    if (!__sync_bool_compare_and_swap((long volatile*)&lpCriticalSection->SpinCount, 0, 1))
        return FALSE;
    if (lpCriticalSection->OwningThread == 0 || lpCriticalSection->OwningThread == threadId) {
        lpCriticalSection->OwningThread = threadId;
        ++lpCriticalSection->RecursionCount;
        unlockCriticalSectionSpin(lpCriticalSection);
        return TRUE;
    }
    unlockCriticalSectionSpin(lpCriticalSection);
    return FALSE;
}

extern "C" BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount)
{
    if (!lpPerformanceCount)
        return FALSE;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpPerformanceCount->QuadPart = (unsigned long long)ts.tv_sec * 1000000000ULL + (unsigned long long)ts.tv_nsec;
    return TRUE;
}

extern "C" BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency)
{
    if (!lpFrequency)
        return FALSE;
    lpFrequency->QuadPart = 1000000000ULL;
    return TRUE;
}

extern "C" DWORD GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)((ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL) & 0xffffffff);
}

extern "C" DWORD timeGetTime(void)
{
    return GetTickCount();
}

extern "C" UINT timeBeginPeriod(UINT uPeriod)
{
    return 0;
}

extern "C" UINT timeEndPeriod(UINT uPeriod)
{
    return 0;
}

extern "C" BOOL SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni)
{
    if ((uiAction == SPI_GETWHEELSCROLLLINES || uiAction == SPI_GETWHEELSCROLLCHARS) && pvParam) {
        *(ULONG*)pvParam = 3;
        return TRUE;
    }
    return FALSE;
}

extern "C" VOID GetSystemTime(SYSTEMTIME* lpSystemTime)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t seconds = tv.tv_sec;
    tm result;
    gmtime_r(&seconds, &result);
    fillSystemTime(result, (int)(tv.tv_usec / 1000), lpSystemTime);
}

extern "C" VOID GetLocalTime(LPSYSTEMTIME lpSystemTime)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t seconds = tv.tv_sec;
    tm result;
    localtime_r(&seconds, &result);
    fillSystemTime(result, (int)(tv.tv_usec / 1000), lpSystemTime);
}

extern "C" HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes)
{
    GlobalMemoryHeader* header = (GlobalMemoryHeader*)calloc(1, sizeof(GlobalMemoryHeader) + dwBytes);
    if (!header)
        return nullptr;
    header->size = dwBytes;
    return (HGLOBAL)(header + 1);
}

extern "C" LPVOID GlobalLock(HGLOBAL hMem)
{
    return hMem;
}

extern "C" BOOL GlobalUnlock(HGLOBAL hMem)
{
    return TRUE;
}

extern "C" HGLOBAL GlobalFree(HGLOBAL hMem)
{
    if (!hMem)
        return nullptr;
    GlobalMemoryHeader* header = ((GlobalMemoryHeader*)hMem) - 1;
    free(header);
    return nullptr;
}

extern "C" SIZE_T GlobalSize(HGLOBAL hMem)
{
    if (!hMem)
        return 0;
    GlobalMemoryHeader* header = ((GlobalMemoryHeader*)hMem) - 1;
    return header->size;
}

static std::mutex g_clipboardMutex;
static std::vector<unsigned char> g_clipboardData;
static UINT g_clipboardFormat = 0;
static std::vector<std::pair<std::string, UINT>> g_registeredClipboardFormats;

static UINT registerClipboardFormatName(const std::string& name)
{
    if (name.empty())
        return 0;

    std::lock_guard<std::mutex> lock(g_clipboardMutex);
    for (const auto& it : g_registeredClipboardFormats) {
        if (it.first == name)
            return it.second;
    }

    const UINT format = 0xC000 + static_cast<UINT>(g_registeredClipboardFormats.size());
    g_registeredClipboardFormats.push_back(std::make_pair(name, format));
    return format;
}

extern "C" UINT RegisterClipboardFormatW(LPCWSTR lpszFormat)
{
    std::string name;
    for (size_t i = 0; lpszFormat && lpszFormat[i]; ++i)
        name.push_back(static_cast<char>(lpszFormat[i]));
    return registerClipboardFormatName(name);
}

extern "C" UINT RegisterClipboardFormatA(LPCSTR lpszFormat)
{
    return registerClipboardFormatName(lpszFormat ? lpszFormat : "");
}

extern "C" BOOL OpenClipboard(HWND hWndNewOwner)
{
    return TRUE;
}

extern "C" BOOL CloseClipboard(void)
{
    return TRUE;
}

extern "C" BOOL EmptyClipboard(void)
{
    std::lock_guard<std::mutex> lock(g_clipboardMutex);
    g_clipboardData.clear();
    g_clipboardFormat = 0;
    return TRUE;
}

extern "C" HANDLE SetClipboardData(UINT uFormat, HANDLE hMem)
{
    std::lock_guard<std::mutex> lock(g_clipboardMutex);
    g_clipboardFormat = uFormat;
    g_clipboardData.assign((unsigned char*)hMem, (unsigned char*)hMem + GlobalSize((HGLOBAL)hMem));
    return hMem;
}

extern "C" HANDLE GetClipboardData(UINT uFormat)
{
    std::lock_guard<std::mutex> lock(g_clipboardMutex);
    if (uFormat != g_clipboardFormat || g_clipboardData.empty())
        return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, g_clipboardData.size());
    memcpy(memory, g_clipboardData.data(), g_clipboardData.size());
    return memory;
}

extern "C" BOOL IsClipboardFormatAvailable(UINT format)
{
    std::lock_guard<std::mutex> lock(g_clipboardMutex);
    return format == g_clipboardFormat;
}

extern "C" DWORD GetClipboardSequenceNumber(void)
{
    return 0;
}

extern "C" cairo_surface_t* LinuxGdiCreateSurfaceByHwnd(HWND hwnd, int w, int h)
{
    return nullptr;
}

extern "C" BOOL MacGdiDrawBitmapToContext(HDC hdc, const unsigned char* bitmap, int bitmapWidth, int bitmapHeight, int destX, int destY, int width, int height, int srcX, int srcY)
{
    if (!hdc || !bitmap || bitmapWidth <= 0 || bitmapHeight <= 0 || width <= 0 || height <= 0)
        return FALSE;

    CGContextRef context = (CGContextRef)hdc;
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, bitmap, (size_t)bitmapWidth * bitmapHeight * 4, nullptr);
    CGBitmapInfo bitmapInfo = (CGBitmapInfo)((uint32_t)kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaPremultipliedFirst);
    CGImageRef image = CGImageCreate(bitmapWidth, bitmapHeight, 8, 32, bitmapWidth * 4, colorSpace,
        bitmapInfo, provider, nullptr, false, kCGRenderingIntentDefault);
    if (!image) {
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(colorSpace);
        return FALSE;
    }

    CGContextSaveGState(context);
    CGRect clip = CGRectMake(destX, destY, width, height);
    CGContextClipToRect(context, clip);
    CGRect drawRect = CGRectMake(destX - srcX, destY - srcY, bitmapWidth, bitmapHeight);
    CGContextDrawImage(context, drawRect, image);
    CGContextRestoreGState(context);

    CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
    return TRUE;
}

extern "C" HDC GetDC(HWND hWnd) { return nullptr; }
extern "C" HDC GetDCEx(HWND hWnd, HRGN hrgnClip, DWORD flags) { return nullptr; }
extern "C" int ReleaseDC(HWND hWnd, HDC hDC) { return 1; }
extern "C" HDC CreateCompatibleDC(HDC hdc) { return nullptr; }
extern "C" BOOL DeleteDC(HDC hdc) { return TRUE; }
extern "C" HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h) { return nullptr; }
extern "C" HGDIOBJ GetCurrentObject(HDC hdc, UINT type) { return nullptr; }

enum MacGdiObjectKind {
    kMacGdiBitmap = 1,
    kMacGdiIcon,
};

struct MacGdiObject {
    uint32_t magic = 0x6d626764;
    MacGdiObjectKind kind;
};

struct MacBitmapObject : MacGdiObject {
    int width = 0;
    int height = 0;
    UINT planes = 1;
    UINT bitsPerPixel = 32;
    std::vector<unsigned char> pixels;

    MacBitmapObject()
    {
        kind = kMacGdiBitmap;
    }
};

struct MacIconObject : MacGdiObject {
    BOOL isIcon = TRUE;
    DWORD xHotspot = 0;
    DWORD yHotspot = 0;
    int width = 0;
    int height = 0;

    MacIconObject()
    {
        kind = kMacGdiIcon;
    }
};

static MacGdiObject* asGdiObject(HGDIOBJ object)
{
    MacGdiObject* gdi = (MacGdiObject*)object;
    return gdi && gdi->magic == 0x6d626764 ? gdi : nullptr;
}

static size_t bitmapStrideBytes(int width, UINT planes, UINT bitsPerPixel)
{
    if (width <= 0 || planes == 0 || bitsPerPixel == 0)
        return 0;
    return ((size_t)width * planes * bitsPerPixel + 31) / 32 * 4;
}

static MacBitmapObject* asBitmap(HBITMAP bitmap)
{
    MacGdiObject* gdi = asGdiObject((HGDIOBJ)bitmap);
    return gdi && gdi->kind == kMacGdiBitmap ? (MacBitmapObject*)gdi : nullptr;
}

extern "C" int GetObject(HANDLE h, int c, LPVOID pv)
{
    MacBitmapObject* bitmap = asBitmap((HBITMAP)h);
    if (!bitmap || !pv || c < (int)sizeof(BITMAP))
        return 0;

    BITMAP* out = (BITMAP*)pv;
    memset(out, 0, sizeof(BITMAP));
    out->bmWidth = bitmap->width;
    out->bmHeight = bitmap->height;
    out->bmWidthBytes = (LONG)bitmapStrideBytes(bitmap->width, bitmap->planes, bitmap->bitsPerPixel);
    out->bmPlanes = (WORD)bitmap->planes;
    out->bmBitsPixel = (WORD)bitmap->bitsPerPixel;
    out->bmBits = bitmap->pixels.empty() ? nullptr : bitmap->pixels.data();
    return sizeof(BITMAP);
}
extern "C" BOOL DeleteObject(HGDIOBJ ho)
{
    MacGdiObject* gdi = asGdiObject(ho);
    if (gdi) {
        if (gdi->kind == kMacGdiBitmap)
            delete (MacBitmapObject*)gdi;
        else if (gdi->kind == kMacGdiIcon)
            delete (MacIconObject*)gdi;
        return TRUE;
    }
    free(ho);
    return TRUE;
}
extern "C" HPEN CreatePen(int iStyle, int cWidth, COLORREF color) { return calloc(1, 1); }
struct MacSolidBrush {
    COLORREF color;
};
extern "C" HBRUSH CreateSolidBrush(COLORREF color)
{
    MacSolidBrush* brush = (MacSolidBrush*)calloc(1, sizeof(MacSolidBrush));
    if (brush)
        brush->color = color;
    return brush;
}
extern "C" HGDIOBJ GetStockObject(int i) { return nullptr; }
extern "C" int FillRect(HDC hDC, CONST RECT* lprc, HBRUSH hbr)
{
    if (!hDC || !lprc)
        return 0;
    COLORREF color = hbr ? ((MacSolidBrush*)hbr)->color : RGB(255, 255, 255);
    CGContextRef context = (CGContextRef)hDC;
    CGContextSaveGState(context);
    CGContextSetRGBFillColor(context, GetRValue(color) / 255.0, GetGValue(color) / 255.0, GetBValue(color) / 255.0, 1.0);
    CGContextFillRect(context, CGRectMake(lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top));
    CGContextRestoreGState(context);
    return 1;
}
extern "C" BOOL Rectangle(HDC hdc, int left, int top, int right, int bottom) { return TRUE; }
extern "C" BOOL BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop) { return FALSE; }
extern "C" BOOL AlphaBlend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, BLENDFUNCTION ftn) { return FALSE; }
extern "C" BOOL GdiAlphaBlend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, BLENDFUNCTION ftn) { return AlphaBlend(hdcDest, xoriginDest, yoriginDest, wDest, hDest, hdcSrc, xoriginSrc, yoriginSrc, wSrc, hSrc, ftn); }
extern "C" HBITMAP CreateBitmap(int nWidth, int nHeight, UINT nPlanes, UINT nBitCount, const void* lpBits)
{
    if (nWidth <= 0 || nHeight <= 0 || nPlanes == 0 || nBitCount == 0)
        return nullptr;

    MacBitmapObject* bitmap = new MacBitmapObject();
    bitmap->width = nWidth;
    bitmap->height = nHeight;
    bitmap->planes = nPlanes;
    bitmap->bitsPerPixel = nBitCount;
    size_t size = bitmapStrideBytes(nWidth, nPlanes, nBitCount) * (size_t)nHeight;
    bitmap->pixels.resize(size);
    if (lpBits && size)
        memcpy(bitmap->pixels.data(), lpBits, size);
    return bitmap;
}
extern "C" HBITMAP CreateCompatibleBitmap(HDC hdc, int cx, int cy) { return CreateBitmap(cx, cy, 1, 32, nullptr); }
extern "C" HFONT CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut,
    DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
{
    return calloc(1, 1);
}
extern "C" HBITMAP CreateDIBSection(HDC hdc, CONST BITMAPINFO* lpbmi, UINT usage, VOID** ppvBits, HANDLE hSection, DWORD offset)
{
    if (!lpbmi || !ppvBits)
        return nullptr;
    int width = std::abs(lpbmi->bmiHeader.biWidth);
    int height = std::abs(lpbmi->bmiHeader.biHeight);
    UINT bitsPerPixel = lpbmi->bmiHeader.biBitCount ? lpbmi->bmiHeader.biBitCount : 32;
    HBITMAP bitmapHandle = CreateBitmap(width, height, 1, bitsPerPixel, nullptr);
    MacBitmapObject* bitmap = asBitmap(bitmapHandle);
    *ppvBits = bitmap && !bitmap->pixels.empty() ? bitmap->pixels.data() : nullptr;
    return bitmapHandle;
}
extern "C" BOOL DestroyIcon(HICON hIcon)
{
    MacGdiObject* gdi = asGdiObject((HGDIOBJ)hIcon);
    if (!gdi || gdi->kind != kMacGdiIcon)
        return FALSE;
    delete (MacIconObject*)gdi;
    return TRUE;
}
extern "C" HICON CreateIconIndirect(PICONINFO piconinfo)
{
    if (!piconinfo || (!piconinfo->hbmColor && !piconinfo->hbmMask))
        return nullptr;

    MacIconObject* icon = new MacIconObject();
    icon->isIcon = piconinfo->fIcon;
    icon->xHotspot = piconinfo->xHotspot;
    icon->yHotspot = piconinfo->yHotspot;
    MacBitmapObject* color = asBitmap(piconinfo->hbmColor);
    MacBitmapObject* mask = asBitmap(piconinfo->hbmMask);
    MacBitmapObject* source = color ? color : mask;
    if (source) {
        icon->width = source->width;
        icon->height = source->height;
    }
    return icon;
}
extern "C" int SetBkMode(HDC hdc, int mode) { return mode; }
extern "C" COLORREF SetBkColor(HDC hdc, COLORREF color) { return color; }
extern "C" COLORREF SetTextColor(HDC hdc, COLORREF color) { return color; }
extern "C" int GetDeviceCaps(HDC hdc, int index) { return index == LOGPIXELSX || index == LOGPIXELSY ? 96 : 0; }
extern "C" BOOL GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl)
{
    if (!psizl)
        return FALSE;
    psizl->cx = c * 8;
    psizl->cy = 16;
    return TRUE;
}
extern "C" BOOL TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c) { return TRUE; }
extern "C" BOOL SetRectRgn(HRGN hrgn, int left, int top, int right, int bottom) { return TRUE; }
extern "C" int CombineRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2, int iMode) { return SIMPLEREGION; }
extern "C" HRGN CreateRectRgn(int x1, int y1, int x2, int y2) { return calloc(1, 1); }
extern "C" HRGN CreateRectRgnIndirect(CONST RECT* lprect) { return calloc(1, 1); }
extern "C" BOOL PtInRegion(HRGN hrgn, int x, int y) { return TRUE; }
extern "C" int SelectClipRgn(HDC hdc, HRGN hrgn) { return SIMPLEREGION; }
extern "C" BOOL SetWorldTransform(HDC hdc, CONST XFORM* lpxf) { return TRUE; }
extern "C" BOOL GdiFlush(void) { return TRUE; }
extern "C" BOOL SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT lppt) { return TRUE; }
extern "C" COLORREF SetDCBrushColor(HDC hdc, COLORREF color) { return color; }
extern "C" COLORREF SetDCPenColor(HDC hdc, COLORREF color) { return color; }
extern "C" int SetGraphicsMode(HDC hdc, int iMode) { return iMode; }
extern "C" int SetStretchBltMode(HDC hdc, int mode) { return mode; }
extern "C" int SetArcDirection(HDC hdc, int dir) { return dir; }
extern "C" int SetROP2(HDC hdc, int rop2) { return rop2; }
extern "C" BOOL UpdateLayeredWindow(HWND hWnd, HDC hdcDst, POINT* pptDst, SIZE* psize, HDC hdcSrc, POINT* pptSrc, COLORREF crKey, BLENDFUNCTION* pblend, DWORD dwFlags) { return FALSE; }

extern "C" BOOL PtInRect(CONST RECT* lprc, POINT pt)
{
    return lprc && pt.x >= lprc->left && pt.x < lprc->right && pt.y >= lprc->top && pt.y < lprc->bottom;
}

extern "C" BOOL IntersectRect(LPRECT dst, CONST RECT* a, CONST RECT* b)
{
    if (!dst || !a || !b)
        return FALSE;
    dst->left = std::max(a->left, b->left);
    dst->top = std::max(a->top, b->top);
    dst->right = std::min(a->right, b->right);
    dst->bottom = std::min(a->bottom, b->bottom);
    if (dst->left < dst->right && dst->top < dst->bottom)
        return TRUE;
    memset(dst, 0, sizeof(*dst));
    return FALSE;
}

extern "C" BOOL UnmapViewOfFile(const void* lpBaseAddress) { return TRUE; }
extern "C" void* MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap) { return nullptr; }

extern "C" HRESULT CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit) { return S_OK; }
extern "C" HRESULT OleInitialize(IN LPVOID pvReserved) { return S_OK; }
extern "C" void CoUninitialize(void) { }
extern "C" HRESULT RevokeDragDrop(HWND hwnd) { return S_OK; }
HRESULT RegisterDragDrop(HWND hwnd, IDropTarget* pDropTarget) { return S_OK; }
extern "C" BOOL ImpersonateAnonymousToken(HANDLE ThreadHandle) { return FALSE; }
extern "C" BOOL RevertToSelf(void) { return TRUE; }
extern "C" BOOL TerminateProcess(HANDLE hProcess, UINT uExitCode)
{
    exit((int)uExitCode);
    return TRUE;
}
extern "C" void ExitProcess(UINT uExitCode)
{
    exit((int)uExitCode);
}

extern "C" HIMC ImmGetContext(HWND hwnd) { return nullptr; }
extern "C" BOOL ImmSetCompositionWindow(HIMC himc, LPCOMPOSITIONFORM lpCompForm) { return TRUE; }
extern "C" BOOL ImmReleaseContext(HWND hwnd, HIMC himc) { return TRUE; }
extern "C" LONG ImmGetCompositionStringW(HIMC himc, DWORD index, LPVOID lpBuf, DWORD dwBufLen) { return 0; }
extern "C" HIMC ImmAssociateContext(HWND hwnd, HIMC himc) { return nullptr; }

extern "C" void DragAcceptFiles(HWND hWnd, BOOL fAccept) { }
extern "C" UINT DragQueryFileW(HDROP hDrop, UINT iFile, LPWSTR lpszFile, UINT cch) { return 0; }
extern "C" void DragFinish(HDROP hDrop) { }

struct MacMenuItem {
    UINT id = 0;
    UINT flags = 0;
    UINT type = MFT_STRING;
    UINT state = MFS_ENABLED;
    HMENU submenu = nullptr;
    ULONG_PTR data = 0;
    std::u16string text;
};

struct MacMenu {
    std::vector<MacMenuItem> items;
};

static std::mutex g_menuMutex;
static std::vector<MacMenu*> g_menus;

static MacMenu* asMenu(HMENU hMenu)
{
    MacMenu* menu = (MacMenu*)hMenu;
    std::lock_guard<std::mutex> lock(g_menuMutex);
    auto it = std::find(g_menus.begin(), g_menus.end(), menu);
    return it == g_menus.end() ? nullptr : menu;
}

static HMENU createMenuHandle()
{
    MacMenu* menu = new MacMenu();
    std::lock_guard<std::mutex> lock(g_menuMutex);
    g_menus.push_back(menu);
    return menu;
}

static MacMenuItem* findMenuItem(MacMenu* menu, UINT item, BOOL byPosition)
{
    if (!menu)
        return nullptr;
    if (byPosition)
        return item < menu->items.size() ? &menu->items[item] : nullptr;
    auto it = std::find_if(menu->items.begin(), menu->items.end(), [item](const MacMenuItem& menuItem) {
        return menuItem.id == item;
    });
    return it == menu->items.end() ? nullptr : &*it;
}

static std::vector<MacMenuItem>::iterator findMenuItemIterator(MacMenu* menu, UINT item, BOOL byPosition)
{
    if (!menu)
        return {};
    if (byPosition)
        return item < menu->items.size() ? menu->items.begin() + item : menu->items.end();
    return std::find_if(menu->items.begin(), menu->items.end(), [item](const MacMenuItem& menuItem) {
        return menuItem.id == item;
    });
}

static void applyMenuItemInfo(MacMenuItem& item, const MENUITEMINFOW* info)
{
    if (!info)
        return;
    if (info->fMask & MIIM_ID)
        item.id = info->wID;
    if (info->fMask & MIIM_STATE)
        item.state = info->fState;
    if (info->fMask & (MIIM_FTYPE | MIIM_TYPE))
        item.type = info->fType;
    if (info->fMask & MIIM_SUBMENU)
        item.submenu = info->hSubMenu;
    if (info->fMask & MIIM_DATA)
        item.data = info->dwItemData;
    if ((info->fMask & (MIIM_STRING | MIIM_TYPE)) && info->dwTypeData)
        item.text.assign((const char16_t*)info->dwTypeData, (const char16_t*)info->dwTypeData + wideLen(info->dwTypeData));
}

static void readMenuItemInfo(const MacMenuItem& item, MENUITEMINFOW* info)
{
    if (info->fMask & MIIM_ID)
        info->wID = item.id;
    if (info->fMask & MIIM_STATE)
        info->fState = item.state;
    if (info->fMask & (MIIM_FTYPE | MIIM_TYPE))
        info->fType = item.type;
    if (info->fMask & MIIM_SUBMENU)
        info->hSubMenu = item.submenu;
    if (info->fMask & MIIM_DATA)
        info->dwItemData = item.data;
    if (info->fMask & (MIIM_STRING | MIIM_TYPE)) {
        UINT textLength = (UINT)item.text.size();
        if (info->dwTypeData && info->cch > 0) {
            UINT copyLength = std::min<UINT>(textLength, info->cch - 1);
            for (UINT i = 0; i < copyLength; ++i)
                info->dwTypeData[i] = (WCHAR)item.text[i];
            info->dwTypeData[copyLength] = 0;
        }
        info->cch = textLength;
    }
}

extern "C" HMENU CreatePopupMenu(void) { return createMenuHandle(); }
extern "C" HMENU CreateMenu(void) { return createMenuHandle(); }
extern "C" BOOL DestroyMenu(HMENU hMenu)
{
    MacMenu* menu = (MacMenu*)hMenu;
    {
        std::lock_guard<std::mutex> lock(g_menuMutex);
        auto it = std::find(g_menus.begin(), g_menus.end(), menu);
        if (it == g_menus.end())
            return FALSE;
        g_menus.erase(it);
    }
    delete menu;
    return TRUE;
}
extern "C" BOOL AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem)
{
    MacMenu* menu = asMenu(hMenu);
    if (!menu)
        return FALSE;
    MacMenuItem item;
    item.id = (UINT)uIDNewItem;
    item.flags = uFlags;
    item.type = (uFlags & MF_SEPARATOR) ? MFT_SEPARATOR : MFT_STRING;
    item.state = uFlags & (MFS_DISABLED | MFS_CHECKED);
    if (lpNewItem)
        item.text.assign((const char16_t*)lpNewItem, (const char16_t*)lpNewItem + wideLen(lpNewItem));
    menu->items.push_back(item);
    return TRUE;
}
extern "C" int GetMenuItemCount(HMENU hMenu)
{
    MacMenu* menu = asMenu(hMenu);
    return menu ? (int)menu->items.size() : -1;
}
extern "C" BOOL TrackPopupMenuEx(HMENU, UINT, int, int, HWND, LPTPMPARAMS) { return FALSE; }
extern "C" BOOL TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT* prcRect) { return TrackPopupMenuEx(hMenu, uFlags, x, y, hWnd, nullptr); }
extern "C" BOOL SetMenu(HWND hWnd, HMENU hMenu) { return TRUE; }
extern "C" BOOL SetMenuItemInfoW(HMENU hmenu, UINT item, BOOL fByPositon, MENUITEMINFOW* lpmii)
{
    MacMenuItem* menuItem = findMenuItem(asMenu(hmenu), item, fByPositon);
    if (!menuItem)
        return FALSE;
    applyMenuItemInfo(*menuItem, lpmii);
    return TRUE;
}
extern "C" BOOL GetMenuItemInfoW(HMENU hmenu, UINT item, BOOL fByPosition, MENUITEMINFOW* lpmii)
{
    MacMenuItem* menuItem = findMenuItem(asMenu(hmenu), item, fByPosition);
    if (!menuItem || !lpmii)
        return FALSE;
    readMenuItemInfo(*menuItem, lpmii);
    return TRUE;
}
extern "C" BOOL InsertMenuItemW(HMENU hmenu, UINT item, BOOL fByPosition, MENUITEMINFOW* lpmi)
{
    MacMenu* menu = asMenu(hmenu);
    if (!menu || !lpmi)
        return FALSE;
    MacMenuItem menuItem;
    applyMenuItemInfo(menuItem, lpmi);
    if (fByPosition) {
        size_t index = std::min<size_t>(item, menu->items.size());
        menu->items.insert(menu->items.begin() + index, menuItem);
    } else {
        auto it = std::find_if(menu->items.begin(), menu->items.end(), [item](const MacMenuItem& existing) {
            return existing.id == item;
        });
        menu->items.insert(it, menuItem);
    }
    return TRUE;
}
extern "C" BOOL EnableMenuItem(HMENU hMenu, UINT uIDEnableItem, UINT uEnable)
{
    BOOL byPosition = (uEnable & MF_BYPOSITION) ? TRUE : FALSE;
    MacMenuItem* menuItem = findMenuItem(asMenu(hMenu), uIDEnableItem, byPosition);
    if (!menuItem)
        return FALSE;
    menuItem->state &= ~MFS_DISABLED;
    menuItem->state |= (uEnable & MFS_DISABLED);
    return TRUE;
}
extern "C" UINT CheckMenuItem(HMENU hMenu, UINT uIDCheckItem, UINT uCheck)
{
    BOOL byPosition = (uCheck & MF_BYPOSITION) ? TRUE : FALSE;
    MacMenuItem* menuItem = findMenuItem(asMenu(hMenu), uIDCheckItem, byPosition);
    if (!menuItem)
        return (UINT)-1;
    UINT previous = menuItem->state & MFS_CHECKED;
    menuItem->state &= ~MFS_CHECKED;
    menuItem->state |= (uCheck & MFS_CHECKED);
    return previous;
}
extern "C" UINT GetMenuState(HMENU hMenu, UINT uId, UINT uFlags)
{
    BOOL byPosition = (uFlags & MF_BYPOSITION) ? TRUE : FALSE;
    MacMenuItem* menuItem = findMenuItem(asMenu(hMenu), uId, byPosition);
    if (!menuItem)
        return (UINT)-1;
    return menuItem->state | menuItem->type;
}
extern "C" BOOL DeleteMenu(HMENU hMenu, UINT uPosition, UINT uFlags)
{
    MacMenu* menu = asMenu(hMenu);
    BOOL byPosition = (uFlags & MF_BYPOSITION) ? TRUE : FALSE;
    auto it = findMenuItemIterator(menu, uPosition, byPosition);
    if (!menu || it == menu->items.end())
        return FALSE;
    menu->items.erase(it);
    return TRUE;
}
extern "C" HMENU GetSystemMenu(HWND hWnd, BOOL bRevert) { return nullptr; }

extern "C" int MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
    fprintf(stderr, "%s: %s\n", lpCaption ? lpCaption : "MessageBox", lpText ? lpText : "");
    return IDOK;
}

extern "C" int MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
    std::string text = wideToUtf8(lpText);
    std::string caption = wideToUtf8(lpCaption);
    return MessageBoxA(hWnd, text.c_str(), caption.c_str(), uType);
}

extern "C" HINSTANCE ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd)
{
    if (!lpFile)
        return (HINSTANCE)0;
    pid_t pid = 0;
    const char* argv[] = { "/usr/bin/open", lpFile, nullptr };
    int result = posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, (char* const*)argv, environ);
    return result == 0 ? (HINSTANCE)33 : (HINSTANCE)0;
}

extern "C" HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd)
{
    std::string file = wideToUtf8(lpFile);
    return ShellExecuteA(hwnd, nullptr, file.c_str(), nullptr, nullptr, nShowCmd);
}

struct MacTrayItem {
    HWND hwnd = nullptr;
    UINT id = 0;
    UINT flags = 0;
    UINT callbackMessage = 0;
    HICON icon = nullptr;
    DWORD state = 0;
    UINT version = 0;
    std::u16string tip;
    std::u16string info;
    std::u16string infoTitle;
    DWORD infoFlags = 0;
    void* nativeStatusItem = nullptr;
};

static std::mutex g_trayMutex;
static std::vector<MacTrayItem> g_trayItems;

static std::vector<MacTrayItem>::iterator findTrayItem(HWND hwnd, UINT id)
{
    return std::find_if(g_trayItems.begin(), g_trayItems.end(), [hwnd, id](const MacTrayItem& item) {
        return item.hwnd == hwnd && item.id == id;
    });
}

static std::u16string fixedWideString(const WCHAR* value, size_t capacity)
{
    size_t len = 0;
    while (len < capacity && value[len])
        ++len;
    return std::u16string((const char16_t*)value, (const char16_t*)value + len);
}

static std::string trayTitleUtf8(const MacTrayItem& item)
{
    return item.tip.empty() ? std::string() : wideToUtf8((LPCWSTR)item.tip.c_str());
}

static bool isTrayItemHidden(const MacTrayItem& item)
{
    return (item.state & NIS_HIDDEN) != 0;
}

static void updateNativeTrayItem(MacTrayItem& item)
{
    if (!item.nativeStatusItem)
        return;
    std::string title = trayTitleUtf8(item);
    MacUpdateStatusItem(item.nativeStatusItem, item.callbackMessage, title.c_str(), isTrayItemHidden(item));
}

static void applyTrayData(MacTrayItem& item, const NOTIFYICONDATAW* data)
{
    if (data->uFlags & NIF_MESSAGE)
        item.callbackMessage = data->uCallbackMessage;
    if (data->uFlags & NIF_ICON)
        item.icon = data->hIcon;
    if (data->uFlags & NIF_TIP)
        item.tip = fixedWideString(data->szTip, sizeof(data->szTip) / sizeof(data->szTip[0]));
    if (data->uFlags & NIF_STATE) {
        item.state &= ~data->dwStateMask;
        item.state |= (data->dwState & data->dwStateMask);
    }
    if (data->uFlags & NIF_INFO) {
        item.info = fixedWideString(data->szInfo, sizeof(data->szInfo) / sizeof(data->szInfo[0]));
        item.infoTitle = fixedWideString(data->szInfoTitle, sizeof(data->szInfoTitle) / sizeof(data->szInfoTitle[0]));
        item.infoFlags = data->dwInfoFlags;
    }
    item.flags |= data->uFlags;
}

extern "C" BOOL Shell_NotifyIconW(DWORD dwMessage, PNOTIFYICONDATAW lpData)
{
    if (!lpData || lpData->cbSize < offsetof(NOTIFYICONDATAW, szTip))
        return FALSE;

    std::lock_guard<std::mutex> lock(g_trayMutex);
    auto it = findTrayItem(lpData->hWnd, lpData->uID);
    if (dwMessage == NIM_ADD) {
        if (it != g_trayItems.end())
            return FALSE;
        MacTrayItem item;
        item.hwnd = lpData->hWnd;
        item.id = lpData->uID;
        applyTrayData(item, lpData);
        std::string title = trayTitleUtf8(item);
        item.nativeStatusItem = MacCreateStatusItem(item.hwnd, item.id, item.callbackMessage, title.c_str(), isTrayItemHidden(item));
        g_trayItems.push_back(item);
        return TRUE;
    }
    if (it == g_trayItems.end())
        return FALSE;
    if (dwMessage == NIM_MODIFY) {
        applyTrayData(*it, lpData);
        updateNativeTrayItem(*it);
        return TRUE;
    }
    if (dwMessage == NIM_DELETE) {
        MacDestroyStatusItem(it->nativeStatusItem);
        g_trayItems.erase(it);
        return TRUE;
    }
    if (dwMessage == NIM_SETVERSION) {
        it->version = lpData->uVersion;
        return TRUE;
    }
    if (dwMessage == NIM_SETFOCUS)
        return TRUE;
    return FALSE;
}

extern "C" BOOL Shell_NotifyIconA(DWORD dwMessage, PNOTIFYICONDATAA lpData)
{
    if (!lpData)
        return FALSE;
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = lpData->hWnd;
    data.uID = lpData->uID;
    data.uFlags = lpData->uFlags;
    data.uCallbackMessage = lpData->uCallbackMessage;
    data.hIcon = lpData->hIcon;
    data.dwState = lpData->dwState;
    data.dwStateMask = lpData->dwStateMask;
    data.uVersion = lpData->uVersion;
    data.dwInfoFlags = lpData->dwInfoFlags;

    std::u16string tip = utf8ToWide(lpData->szTip, -1);
    std::u16string info = utf8ToWide(lpData->szInfo, -1);
    std::u16string title = utf8ToWide(lpData->szInfoTitle, -1);
    copyWideToBuffer(tip, data.szTip, sizeof(data.szTip) / sizeof(data.szTip[0]));
    copyWideToBuffer(info, data.szInfo, sizeof(data.szInfo) / sizeof(data.szInfo[0]));
    copyWideToBuffer(title, data.szInfoTitle, sizeof(data.szInfoTitle) / sizeof(data.szInfoTitle[0]));
    return Shell_NotifyIconW(dwMessage, &data);
}

extern "C" int GetLocaleInfoW(LCID Locale, LCTYPE LCType, LPWSTR lpLCData, int cchData)
{
    const char* value = "";
    if (LCType == LOCALE_SDECIMAL)
        value = ".";
    else if (LCType == LOCALE_STHOUSAND)
        value = ",";
    else if (LCType == LOCALE_SSHORTDATE)
        value = "M/d/yyyy";
    else if (LCType == LOCALE_STIMEFORMAT)
        value = "HH:mm:ss";
    std::u16string wide = utf8ToWide(value, -1);
    DWORD required = copyWideToBuffer(wide, lpLCData, cchData);
    return (int)required + 1;
}

extern "C" /*FILE*/ void* _wfopen(const WCHAR* fileName, const WCHAR* mode)
{
    std::string fileNameUtf8 = wideToUtf8(fileName);
    std::string modeUtf8 = wideToUtf8(mode);
    return fopen(fileNameUtf8.c_str(), modeUtf8.c_str());
}

extern "C" BOOL PostThreadMessageW(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    typedef BOOL (*PostMessageWFunc)(HWND, UINT, WPARAM, LPARAM);
    PostMessageWFunc postMessage = (PostMessageWFunc)dlsym(RTLD_DEFAULT, "PostMessageW");
    return postMessage ? postMessage(nullptr, Msg, wParam, lParam) : FALSE;
}

extern "C" UINT_PTR SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc)
{
    return nIDEvent ? nIDEvent : 1;
}

extern "C" BOOL KillTimer(HWND hWnd, UINT_PTR uIDEvent)
{
    return TRUE;
}

BOOL GetSaveFileNameW(LPOPENFILENAMEW info)
{
    return FALSE;
}

extern "C" UINT RegisterWindowMessageW(LPCWSTR lpString)
{
    unsigned int hash = 0;
    while (lpString && *lpString)
        hash = hash * 131 + *lpString++;
    return WM_USER + (hash & 0x3fff);
}

extern "C" BOOL UnregisterClassW(LPCWSTR lpClassName, HINSTANCE hInstance)
{
    return TRUE;
}

void SetLinuxOpenglDraw(BOOL b)
{
    g_isLinuxOpenglDraw = b;
}

BOOL IsLinuxOpenglDraw(void)
{
    return g_isLinuxOpenglDraw;
}
