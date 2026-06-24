#ifndef MAC_SHLOBJ_H
#define MAC_SHLOBJ_H

#include "objidl.h"
#include "shlwapi.h"

#define BIF_BROWSEINCLUDEFILES 0x00004000
#define BIF_RETURNONLYFSDIRS 0x00000001
#define BIF_USENEWUI 0x00000040

#define BFFM_INITIALIZED 1
#define BFFM_SETSELECTION (WM_USER + 102)

typedef enum tagCLSCTX {
    CLSCTX_INPROC_SERVER = 0x1,
    CLSCTX_INPROC_HANDLER = 0x2,
    CLSCTX_LOCAL_SERVER = 0x4,
    CLSCTX_REMOTE_SERVER = 0x10,
    CLSCTX_ALL = CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER
} CLSCTX;

#ifndef E_POINTER
#define E_POINTER ((HRESULT)0x80004003L)
#endif

class IDataObject : public IUnknown {
public:
    virtual HRESULT GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) = 0;
    virtual HRESULT GetDataHere(FORMATETC* pformatetc, STGMEDIUM* pmedium) = 0;
    virtual HRESULT QueryGetData(FORMATETC* pformatetc) = 0;
    virtual HRESULT GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) = 0;
    virtual HRESULT SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) = 0;
    virtual HRESULT EnumFormatEtc(DWORD dwDirection, void** ppenumFormatEtc) = 0;
    virtual HRESULT DAdvise(FORMATETC* pformatetc, DWORD advf, void* pAdvSink, DWORD* pdwConnection) = 0;
    virtual HRESULT DUnadvise(DWORD dwConnection) = 0;
    virtual HRESULT EnumDAdvise(void** ppenumAdvise) = 0;
};

class IDropTarget : public IUnknown {
public:
    virtual HRESULT DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) = 0;
    virtual HRESULT DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) = 0;
    virtual HRESULT DragLeave(void) = 0;
    virtual HRESULT Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) = 0;
};

class IDropTargetHelper : public IUnknown {
public:
    virtual HRESULT DragEnter(HWND hwndTarget, IDataObject* pDataObject, POINT* ppt, DWORD dwEffect) = 0;
    virtual HRESULT DragLeave(void) = 0;
    virtual HRESULT DragOver(POINT* ppt, DWORD dwEffect) = 0;
    virtual HRESULT Drop(IDataObject* pDataObject, POINT* ppt, DWORD dwEffect) = 0;
    virtual HRESULT Show(BOOL fShow) = 0;
};

typedef struct _SHITEMID {
    USHORT cb;
    BYTE abID[1];
} SHITEMID;

typedef struct _ITEMIDLIST {
    SHITEMID mkid;
} ITEMIDLIST;

typedef int (*BFFCALLBACK)(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData);
typedef const ITEMIDLIST* LPCITEMIDLIST;
typedef ITEMIDLIST* LPITEMIDLIST;

typedef struct _browseinfoW {
    HWND hwndOwner;
    const ITEMIDLIST* pidlRoot;
    LPWSTR pszDisplayName;
    LPCWSTR lpszTitle;
    UINT ulFlags;
    BFFCALLBACK lpfn;
    LPARAM lParam;
    int iImage;
} BROWSEINFOW, *PBROWSEINFOW, *LPBROWSEINFOW;

ITEMIDLIST* SHBrowseForFolderW(LPBROWSEINFOW lpbi);
BOOL SHGetPathFromIDListW(const ITEMIDLIST* pidl, LPWSTR pszPath);
EXTERN_C HRESULT SHOpenFolderAndSelectItems(LPCITEMIDLIST pidlFolder, UINT cidl, LPCITEMIDLIST* apidl, DWORD dwFlags);
HRESULT CoCreateInstance(REFCLSID rclsid, IUnknown* pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID* ppv);

#endif // MAC_SHLOBJ_H
