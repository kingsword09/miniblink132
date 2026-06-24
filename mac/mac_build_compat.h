#ifndef MINIBLINK_MAC_BUILD_COMPAT_H_
#define MINIBLINK_MAC_BUILD_COMPAT_H_

#if defined(OS_MAC)

#include <stdio.h>

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

#endif // defined(OS_MAC)

#endif // MINIBLINK_MAC_BUILD_COMPAT_H_
