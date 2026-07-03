#ifndef MAC_SHLWAPI_H
#define MAC_SHLWAPI_H

#include "windows.h"

#define OFN_LONGNAMES 0x00200000
#define OFN_NOCHANGEDIR 0x00000008
#define OFN_EXPLORER 0x00080000
#define OFN_ALLOWMULTISELECT 0x00000200
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_PATHMUSTEXIST 0x00000800

#define FNERR_BUFFERTOOSMALL 0x3003

typedef UINT_PTR (*LPOFNHOOKPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagOFNW {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    LPCWSTR lpstrFilter;
    LPWSTR lpstrCustomFilter;
    DWORD nMaxCustFilter;
    DWORD nFilterIndex;
    LPWSTR lpstrFile;
    DWORD nMaxFile;
    LPWSTR lpstrFileTitle;
    DWORD nMaxFileTitle;
    LPCWSTR lpstrInitialDir;
    LPCWSTR lpstrTitle;
    DWORD Flags;
    WORD nFileOffset;
    WORD nFileExtension;
    LPCWSTR lpstrDefExt;
    LPARAM lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCWSTR lpTemplateName;
    void* pvReserved;
    DWORD dwReserved;
    DWORD FlagsEx;
} OPENFILENAMEW, *LPOPENFILENAMEW;

typedef OPENFILENAMEW OPENFILENAME;
typedef LPOPENFILENAMEW LPOPENFILENAME;

BOOL GetOpenFileNameW(LPOPENFILENAMEW info);
BOOL GetSaveFileNameW(LPOPENFILENAMEW info);
DWORD CommDlgExtendedError(void);

#endif // MAC_SHLWAPI_H
