
#ifndef browser_api_WindowInterface_h
#define browser_api_WindowInterface_h

#include <v8.h>
#if defined(WINDOWS_FOR_LINUX_H) || defined(WINDOWS_FOR_MAC_H) || defined(_WIN32)
#include <windows.h>
#else
typedef struct HWND__* HWND;
#endif

namespace atom {

class WebContents;

class WindowInterface {
public:
    virtual bool isClosed() = 0;
    virtual void close() = 0;
    virtual v8::Local<v8::Object> getWrapper() = 0;
    virtual int getId() const = 0;
    virtual WebContents* getWebContents() const = 0;
    virtual HWND getHWND() const = 0;
    static v8::Local<v8::Value> getFocusedWindow(v8::Isolate* isolate);
    static v8::Local<v8::Value> getFocusedContents(v8::Isolate* isolate);

    static WebContents* onCreateNewWebview(v8::Local<v8::Object>);

    static const wchar_t kElectronClassName[];
    static const int kSingleInstanceMessage = 0x410;
};

} // atom

#endif // browser_api_WindowInterface_h
