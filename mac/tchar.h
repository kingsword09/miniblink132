#ifndef MAC_TCHAR_H
#define MAC_TCHAR_H

#include "windows.h"

typedef WCHAR TCHAR;

#ifndef TEXT
#define TEXT(value) L##value
#endif

#endif // MAC_TCHAR_H
