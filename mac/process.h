#ifndef MAC_PROCESS_H
#define MAC_PROCESS_H

#include "windows.h"

typedef unsigned int(CALLBACK* _beginthreadex_proc_type)(void*);
extern "C" {
uintptr_t _beginthreadex(void* security, unsigned stackSize, _beginthreadex_proc_type startAddress, void* argList, unsigned initFlag, unsigned* threadId);
}

#endif // MAC_PROCESS_H
