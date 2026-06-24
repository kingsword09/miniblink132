#ifndef MAC_OBJIDL_H
#define MAC_OBJIDL_H

#include "unknwn.h"

typedef WCHAR OLECHAR;
typedef OLECHAR* LPOLESTR;
typedef WORD CLIPFORMAT;

typedef struct tagDVTARGETDEVICE {
    DWORD tdSize;
    WORD tdDriverNameOffset;
    WORD tdDeviceNameOffset;
    WORD tdPortNameOffset;
    WORD tdExtDevmodeOffset;
    BYTE tdData[1];
} DVTARGETDEVICE;

#define DVASPECT_CONTENT 1
#define TYMED_HGLOBAL 1
#define TYMED_FILE 2
#define TYMED_ISTREAM 4
#define TYMED_ISTORAGE 8
#define TYMED_GDI 16
#define TYMED_MFPICT 32
#define TYMED_ENHMF 64
#define TYMED_NULL 0

typedef struct tagFORMATETC {
    CLIPFORMAT cfFormat;
    DVTARGETDEVICE* ptd;
    DWORD dwAspect;
    LONG lindex;
    DWORD tymed;
} FORMATETC, *LPFORMATETC;

typedef struct tagSTGMEDIUM {
    DWORD tymed;
    union {
        HBITMAP hBitmap;
        HGLOBAL hGlobal;
        LPWSTR lpszFileName;
        void* pstm;
        void* pstg;
    };
    IUnknown* pUnkForRelease;
} STGMEDIUM, *LPSTGMEDIUM;

#endif // MAC_OBJIDL_H
