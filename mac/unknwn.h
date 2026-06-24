#ifndef MAC_UNKNWN_H
#define MAC_UNKNWN_H

#include "windows.h"

typedef struct _GUID {
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;

typedef GUID IID;
typedef const IID& REFIID;
typedef const IID& REFCLSID;

static const IID IID_IUnknown = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

class IUnknown {
public:
    virtual HRESULT QueryInterface(REFIID riid, void** ppvObject) = 0;
    virtual ULONG AddRef(void) = 0;
    virtual ULONG Release(void) = 0;
};

#endif // MAC_UNKNWN_H
