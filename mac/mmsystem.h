#ifndef MAC_MMSYSTEM_H
#define MAC_MMSYSTEM_H

#include "windows.h"

typedef struct timecaps_tag {
    UINT wPeriodMin;
    UINT wPeriodMax;
} TIMECAPS, *LPTIMECAPS;

EXTERN_C DWORD timeGetTime(void);
EXTERN_C UINT timeGetDevCaps(LPTIMECAPS ptc, UINT cbtc);

#endif // MAC_MMSYSTEM_H
