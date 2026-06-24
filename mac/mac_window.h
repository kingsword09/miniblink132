#ifndef MAC_MAC_WINDOW_H
#define MAC_MAC_WINDOW_H

#include "windows.h"

#include <map>
#include <mutex>
#include <set>
#include <string>

class HwndMac {
public:
    HwndMac();
    ~HwndMac();

    void* m_window = nullptr;
    void* m_view = nullptr;
    WNDPROC m_wndProc = nullptr;
    LPVOID m_userdata = nullptr;
    DWORD m_style = 0;
    DWORD m_styleex = 0;
    DWORD m_threadId = 0;
    HWND m_parent = nullptr;
    bool m_autoHandleClose = false;
    bool m_isDestroying = false;
    bool m_visible = false;
    void* m_msgPtr = nullptr;

    RECT m_clientRect = { 0, 0, 0, 0 };
    RECT m_windowRect = { 0, 0, 0, 0 };
    std::map<unsigned int, void*> m_props;

    static void ensureStatics();
    static bool isValid(HWND hwnd);
    static HwndMac* from(HWND hwnd);
    static void destroy(HWND hwnd, bool forceDelete);
    static unsigned int hashString(LPCWSTR value);

    static std::map<std::u16string, WNDCLASSEXW>* s_wndClassMap;
    static std::set<HWND>* s_hwnds;
    static std::recursive_mutex* s_hwndMutex;
};

#endif // MAC_MAC_WINDOW_H
