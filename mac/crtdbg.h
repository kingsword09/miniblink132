#ifndef MAC_CRTDBG_H
#define MAC_CRTDBG_H

#define _CRTDBG_MAP_ALLOC
#define _CrtDumpMemoryLeaks() ((void)0)
#define _CrtSetDbgFlag(flag) (flag)
#define _CrtSetBreakAlloc(alloc) ((long)(alloc))

#endif // MAC_CRTDBG_H
