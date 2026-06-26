#ifndef MINIBLINK_MAC_BUILD_COMPAT_H_
#define MINIBLINK_MAC_BUILD_COMPAT_H_

#if defined(OS_MAC) && !defined(__ASSEMBLER__)

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>

#ifndef WCHAR_T_IS_32_BIT
#define WCHAR_T_IS_32_BIT 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

void DebugBreak(void);
void OutputDebugStringA(const char* lpOutputString);

#ifdef __cplusplus
}
#endif

#ifndef __debugbreak
#define __debugbreak() DebugBreak()
#endif

#ifndef sprintf_s
#define sprintf_s snprintf
#endif

#if defined(__APPLE__) && !defined(posix_fallocate)
static inline int miniblink_posix_fallocate(int fd, off_t offset, off_t len)
{
    (void)fd;
    (void)offset;
    (void)len;
    return ENOTSUP;
}
#define posix_fallocate miniblink_posix_fallocate
#endif

#endif // defined(OS_MAC)

#endif // MINIBLINK_MAC_BUILD_COMPAT_H_
