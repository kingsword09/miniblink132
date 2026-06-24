#ifndef FASTMAKE_QUICKJS_MAC_COMPAT_H
#define FASTMAKE_QUICKJS_MAC_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

typedef unsigned int DWORD;

static inline DWORD TlsAlloc(void)
{
    pthread_key_t key;
    return pthread_key_create(&key, NULL) == 0 ? (DWORD)key : (DWORD)-1;
}

static inline void* TlsGetValue(DWORD key)
{
    return pthread_getspecific((pthread_key_t)key);
}

static inline int TlsSetValue(DWORD key, void* value)
{
    return pthread_setspecific((pthread_key_t)key, value) == 0;
}

static inline void OutputDebugStringA(const char* value)
{
    if (value)
        fputs(value, stderr);
}

static inline void DebugBreak(void)
{
    abort();
}

#endif // FASTMAKE_QUICKJS_MAC_COMPAT_H
