#define BOOL OBJC_BOOL
#import <Cocoa/Cocoa.h>
#undef BOOL

#include "mac/mac_window.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

namespace {

std::mutex g_messageQueueMutex;
std::condition_variable g_messageQueueCondition;
std::deque<MSG> g_messageQueue;
HWND g_focusWindow = nullptr;
HWND g_captureWindow = nullptr;

struct HotKeyRegistration {
    HWND hwnd = nullptr;
    int id = 0;
    UINT modifiers = 0;
    UINT vk = 0;
};

std::mutex g_hotKeyMutex;
std::vector<HotKeyRegistration> g_hotKeys;

static std::u16string wideKey(LPCWSTR value)
{
    if (!value)
        return std::u16string();

    if (((uintptr_t)value >> 16) == 0) {
        std::u16string atomKey;
        atomKey.push_back((char16_t)0xffff);
        atomKey.push_back((char16_t)((uintptr_t)value & 0xffff));
        return atomKey;
    }

    std::u16string key;
    while (*value)
        key.push_back((char16_t)*value++);
    return key;
}

static std::string wideToUtf8(LPCWSTR value)
{
    std::string result;
    if (!value)
        return result;
    while (*value) {
        uint32_t cp = *value++;
        if (0xd800 <= cp && cp <= 0xdbff && *value) {
            uint32_t low = *value;
            if (0xdc00 <= low && low <= 0xdfff) {
                ++value;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            }
        }
        if (cp < 0x80) {
            result.push_back((char)cp);
        } else if (cp < 0x800) {
            result.push_back((char)(0xc0 | (cp >> 6)));
            result.push_back((char)(0x80 | (cp & 0x3f)));
        } else if (cp < 0x10000) {
            result.push_back((char)(0xe0 | (cp >> 12)));
            result.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
            result.push_back((char)(0x80 | (cp & 0x3f)));
        } else {
            result.push_back((char)(0xf0 | (cp >> 18)));
            result.push_back((char)(0x80 | ((cp >> 12) & 0x3f)));
            result.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
            result.push_back((char)(0x80 | (cp & 0x3f)));
        }
    }
    return result;
}

static NSString* wideToNSString(LPCWSTR value)
{
    std::string utf8 = wideToUtf8(value);
    return [NSString stringWithUTF8String:utf8.c_str()];
}

static LPARAM pointToLParam(NSPoint point)
{
    return MAKELPARAM((int)point.x, (int)point.y);
}

static WPARAM eventButtonFlags(NSEvent* event)
{
    WPARAM flags = 0;
    NSEventModifierFlags modifiers = [event modifierFlags];
    if (modifiers & NSEventModifierFlagShift)
        flags |= MK_SHIFT;
    if (modifiers & NSEventModifierFlagControl)
        flags |= MK_CONTROL;
    if ([NSEvent pressedMouseButtons] & (1 << 0))
        flags |= MK_LBUTTON;
    if ([NSEvent pressedMouseButtons] & (1 << 1))
        flags |= MK_RBUTTON;
    if ([NSEvent pressedMouseButtons] & (1 << 2))
        flags |= MK_MBUTTON;
    return flags;
}

static UINT normalizeHotKeyModifiers(UINT modifiers)
{
    return modifiers & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
}

static UINT hotKeyModifiersFromEvent(NSEvent* event)
{
    UINT modifiers = 0;
    NSEventModifierFlags flags = [event modifierFlags];
    if (flags & NSEventModifierFlagOption)
        modifiers |= MOD_ALT;
    if (flags & NSEventModifierFlagControl)
        modifiers |= MOD_CONTROL;
    if (flags & NSEventModifierFlagShift)
        modifiers |= MOD_SHIFT;
    if (flags & NSEventModifierFlagCommand)
        modifiers |= MOD_WIN;
    return modifiers;
}

static UINT virtualKeyFromEvent(NSEvent* event)
{
    switch ([event keyCode]) {
    case 36:
    case 76:
        return VK_RETURN;
    case 48:
        return VK_TAB;
    case 49:
        return VK_SPACE;
    case 51:
        return VK_BACK;
    case 53:
        return VK_ESCAPE;
    case 123:
        return VK_LEFT;
    case 124:
        return VK_RIGHT;
    case 125:
        return VK_DOWN;
    case 126:
        return VK_UP;
    case 115:
        return VK_HOME;
    case 119:
        return VK_END;
    case 116:
        return VK_PRIOR;
    case 121:
        return VK_NEXT;
    case 117:
        return VK_DELETE;
    default:
        break;
    }

    NSString* chars = [event charactersIgnoringModifiers];
    if ([chars length] > 0) {
        unichar ch = [chars characterAtIndex:0];
        if ('a' <= ch && ch <= 'z')
            return ch - 'a' + 'A';
        return ch;
    }
    return 0;
}

static bool findRegisteredHotKey(HWND hwnd, UINT modifiers, UINT vk, HotKeyRegistration* registration)
{
    std::lock_guard<std::mutex> lock(g_hotKeyMutex);
    for (const HotKeyRegistration& candidate : g_hotKeys) {
        if (candidate.vk != vk || candidate.modifiers != modifiers)
            continue;
        if (candidate.hwnd && candidate.hwnd != hwnd)
            continue;
        if (registration)
            *registration = candidate;
        return true;
    }
    return false;
}

static void unregisterHotKeysForWindow(HWND hwnd)
{
    std::lock_guard<std::mutex> lock(g_hotKeyMutex);
    g_hotKeys.erase(std::remove_if(g_hotKeys.begin(), g_hotKeys.end(), [hwnd](const HotKeyRegistration& registration) {
        return registration.hwnd == hwnd;
    }), g_hotKeys.end());
}

static void dispatchQueuedMessagesForWindow(HWND hwnd)
{
    for (;;) {
        MSG msg = { 0 };
        {
            std::lock_guard<std::mutex> lock(g_messageQueueMutex);
            auto it = std::find_if(g_messageQueue.begin(), g_messageQueue.end(), [hwnd](const MSG& candidate) {
                return candidate.message != WM_QUIT && (!hwnd || candidate.hwnd == hwnd);
            });
            if (it == g_messageQueue.end())
                return;
            msg = *it;
            g_messageQueue.erase(it);
        }
        DispatchMessageW(&msg);
    }
}

static bool takeQueuedMessage(MSG* out, HWND hwnd, bool remove)
{
    auto it = std::find_if(g_messageQueue.begin(), g_messageQueue.end(), [hwnd](const MSG& msg) {
        return !hwnd || msg.hwnd == hwnd;
    });
    if (it == g_messageQueue.end())
        return false;
    if (out)
        *out = *it;
    if (remove)
        g_messageQueue.erase(it);
    return true;
}

static void pumpCocoaOnce(NSDate* limitDate)
{
    if (![NSThread isMainThread])
        return;
    NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:limitDate inMode:NSDefaultRunLoopMode dequeue:YES];
    if (event)
        [NSApp sendEvent:event];
}

} // namespace

@interface MacWindowView : NSView {
@public
    HwndMac* _hwnd;
}
- (instancetype)initWithHwnd:(HwndMac*)hwnd frame:(NSRect)frame;
@end

@interface MacWindowDelegate : NSObject <NSWindowDelegate> {
@public
    HwndMac* _hwnd;
}
- (instancetype)initWithHwnd:(HwndMac*)hwnd;
@end

@implementation MacWindowView

- (instancetype)initWithHwnd:(HwndMac*)hwnd frame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        _hwnd = hwnd;
        [self setWantsLayer:YES];
    }
    return self;
}

- (OBJC_BOOL)isFlipped
{
    return YES;
}

- (OBJC_BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] makeFirstResponder:self];
}

- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    if (!_hwnd || !HwndMac::isValid((HWND)_hwnd) || !_hwnd->m_wndProc)
        return;
    _hwnd->m_clientRect = { 0, 0, (LONG)newSize.width, (LONG)newSize.height };
    _hwnd->m_wndProc((HWND)_hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM((int)newSize.width, (int)newSize.height));
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (!_hwnd || !HwndMac::isValid((HWND)_hwnd) || !_hwnd->m_wndProc)
        return;

    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    NSRect bounds = [self bounds];
    _hwnd->m_clientRect = { 0, 0, (LONG)bounds.size.width, (LONG)bounds.size.height };

    PAINTSTRUCT paintStruct = { 0 };
    paintStruct.hdc = (HDC)context;
    paintStruct.fErase = TRUE;
    paintStruct.rcPaint.left = (LONG)dirtyRect.origin.x;
    paintStruct.rcPaint.top = (LONG)dirtyRect.origin.y;
    paintStruct.rcPaint.right = (LONG)(dirtyRect.origin.x + dirtyRect.size.width);
    paintStruct.rcPaint.bottom = (LONG)(dirtyRect.origin.y + dirtyRect.size.height);

    _hwnd->m_msgPtr = &paintStruct;
    _hwnd->m_wndProc((HWND)_hwnd, WM_PAINT, (WPARAM)context, 0);
    _hwnd->m_msgPtr = nullptr;
}

- (void)mouseDown:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    [[self window] makeFirstResponder:self];
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    UINT msg = [event clickCount] > 1 ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN;
    _hwnd->m_wndProc((HWND)_hwnd, msg, eventButtonFlags(event), pointToLParam(point));
}

- (void)mouseUp:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    _hwnd->m_wndProc((HWND)_hwnd, WM_LBUTTONUP, eventButtonFlags(event), pointToLParam(point));
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    UINT msg = [event clickCount] > 1 ? WM_RBUTTONDBLCLK : WM_RBUTTONDOWN;
    _hwnd->m_wndProc((HWND)_hwnd, msg, eventButtonFlags(event), pointToLParam(point));
}

- (void)rightMouseUp:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    _hwnd->m_wndProc((HWND)_hwnd, WM_RBUTTONUP, eventButtonFlags(event), pointToLParam(point));
}

- (void)otherMouseDown:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    UINT msg = [event clickCount] > 1 ? WM_MBUTTONDBLCLK : WM_MBUTTONDOWN;
    _hwnd->m_wndProc((HWND)_hwnd, msg, eventButtonFlags(event), pointToLParam(point));
}

- (void)otherMouseUp:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    _hwnd->m_wndProc((HWND)_hwnd, WM_MBUTTONUP, eventButtonFlags(event), pointToLParam(point));
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    _hwnd->m_wndProc((HWND)_hwnd, WM_MOUSEMOVE, eventButtonFlags(event), pointToLParam(point));
    _hwnd->m_wndProc((HWND)_hwnd, WM_SETCURSOR, 0, 0);
}

- (void)mouseDragged:(NSEvent*)event
{
    [self mouseMoved:event];
}

- (void)rightMouseDragged:(NSEvent*)event
{
    [self mouseMoved:event];
}

- (void)otherMouseDragged:(NSEvent*)event
{
    [self mouseMoved:event];
}

- (void)scrollWheel:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    int delta = (int)std::max(-WHEEL_DELTA, std::min(WHEEL_DELTA, (int)([event scrollingDeltaY] * WHEEL_DELTA)));
    if (delta == 0)
        delta = [event scrollingDeltaY] > 0 ? WHEEL_DELTA : -WHEEL_DELTA;
    WPARAM wParam = MAKEWPARAM(eventButtonFlags(event), delta);
    _hwnd->m_wndProc((HWND)_hwnd, WM_MOUSEWHEEL, wParam, pointToLParam(point));
}

- (void)keyDown:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    UINT key = virtualKeyFromEvent(event);
    UINT hotKeyModifiers = hotKeyModifiersFromEvent(event);
    HotKeyRegistration hotKey;
    if (key && findRegisteredHotKey((HWND)_hwnd, hotKeyModifiers, key, &hotKey)) {
        HWND target = hotKey.hwnd ? hotKey.hwnd : (HWND)_hwnd;
        SendMessageW(target, WM_HOTKEY, (WPARAM)hotKey.id, MAKELPARAM(hotKey.modifiers, hotKey.vk));
        return;
    }

    _hwnd->m_wndProc((HWND)_hwnd, WM_KEYDOWN, key, 0);

    NSString* chars = [event characters];
    for (NSUInteger i = 0; i < [chars length]; ++i) {
        unichar ch = [chars characterAtIndex:i];
        if (ch >= 0x20 || ch == '\r' || ch == '\t' || ch == '\b')
            _hwnd->m_wndProc((HWND)_hwnd, WM_CHAR, ch == '\r' ? VK_RETURN : ch, 0);
    }
}

- (void)keyUp:(NSEvent*)event
{
    if (!_hwnd || !_hwnd->m_wndProc)
        return;
    _hwnd->m_wndProc((HWND)_hwnd, WM_KEYUP, virtualKeyFromEvent(event), 0);
}

- (OBJC_BOOL)becomeFirstResponder
{
    g_focusWindow = (HWND)_hwnd;
    if (_hwnd && _hwnd->m_wndProc)
        _hwnd->m_wndProc((HWND)_hwnd, WM_SETFOCUS, 0, 0);
    return YES;
}

- (OBJC_BOOL)resignFirstResponder
{
    if (g_focusWindow == (HWND)_hwnd)
        g_focusWindow = nullptr;
    if (_hwnd && _hwnd->m_wndProc)
        _hwnd->m_wndProc((HWND)_hwnd, WM_KILLFOCUS, 0, 0);
    return YES;
}

@end

@implementation MacWindowDelegate

- (instancetype)initWithHwnd:(HwndMac*)hwnd
{
    self = [super init];
    if (self)
        _hwnd = hwnd;
    return self;
}

- (OBJC_BOOL)windowShouldClose:(id)sender
{
    if (_hwnd && HwndMac::isValid((HWND)_hwnd) && _hwnd->m_wndProc)
        _hwnd->m_wndProc((HWND)_hwnd, WM_CLOSE, 0, 0);
    return _hwnd ? _hwnd->m_isDestroying : YES;
}

- (void)windowWillClose:(NSNotification*)notification
{
    if (_hwnd && HwndMac::isValid((HWND)_hwnd) && _hwnd->m_wndProc)
        _hwnd->m_wndProc((HWND)_hwnd, WM_NCDESTROY, 0, 0);
}

@end

@interface MacStatusItemTarget : NSObject {
@public
    HWND _hwnd;
    UINT _id;
    UINT _callbackMessage;
}
- (instancetype)initWithHwnd:(HWND)hwnd id:(UINT)id callbackMessage:(UINT)callbackMessage;
- (void)statusItemClicked:(id)sender;
@end

@implementation MacStatusItemTarget

- (instancetype)initWithHwnd:(HWND)hwnd id:(UINT)id callbackMessage:(UINT)callbackMessage
{
    self = [super init];
    if (self) {
        _hwnd = hwnd;
        _id = id;
        _callbackMessage = callbackMessage;
    }
    return self;
}

- (void)statusItemClicked:(id)sender
{
    if (_hwnd && _callbackMessage)
        PostMessageW(_hwnd, _callbackMessage, _id, WM_LBUTTONUP);
}

@end

struct MacStatusItemHandle {
    NSStatusItem* item = nil;
    MacStatusItemTarget* target = nil;
};

std::map<std::u16string, WNDCLASSEXW>* HwndMac::s_wndClassMap = nullptr;
std::set<HWND>* HwndMac::s_hwnds = nullptr;
std::recursive_mutex* HwndMac::s_hwndMutex = nullptr;

HwndMac::HwndMac() = default;
HwndMac::~HwndMac()
{
    if (m_systemMenu)
        DestroyMenu(m_systemMenu);
}

void HwndMac::ensureStatics()
{
    static std::recursive_mutex initMutex;
    std::lock_guard<std::recursive_mutex> lock(initMutex);
    if (!s_wndClassMap)
        s_wndClassMap = new std::map<std::u16string, WNDCLASSEXW>();
    if (!s_hwnds)
        s_hwnds = new std::set<HWND>();
    if (!s_hwndMutex)
        s_hwndMutex = new std::recursive_mutex();
}

bool HwndMac::isValid(HWND hwnd)
{
    ensureStatics();
    std::lock_guard<std::recursive_mutex> lock(*s_hwndMutex);
    return hwnd && s_hwnds->find(hwnd) != s_hwnds->end();
}

HwndMac* HwndMac::from(HWND hwnd)
{
    return isValid(hwnd) ? (HwndMac*)hwnd : nullptr;
}

unsigned int HwndMac::hashString(LPCWSTR value)
{
    unsigned int hash = 2166136261u;
    if (!value)
        return hash;
    if (((uintptr_t)value >> 16) == 0)
        return (unsigned int)((uintptr_t)value & 0xffff);
    while (*value) {
        hash ^= (unsigned int)*value++;
        hash *= 16777619u;
    }
    return hash;
}

void HwndMac::destroy(HWND hwnd, bool forceDelete)
{
    HwndMac* self = HwndMac::from(hwnd);
    if (!self)
        return;

    unregisterHotKeysForWindow(hwnd);

    void* window = self->m_window;
    {
        std::lock_guard<std::recursive_mutex> lock(*s_hwndMutex);
        s_hwnds->erase(hwnd);
        self->m_isDestroying = true;
    }

    if (window && (self->m_autoHandleClose || forceDelete)) {
        NSWindow* nsWindow = (NSWindow*)window;
        [nsWindow close];
    }

    delete self;
}

extern "C" void MacInitializeApplication(void)
{
    auto init = ^{
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        if (![NSApp mainMenu]) {
            NSMenu* menubar = [[NSMenu alloc] initWithTitle:@""];
            NSMenuItem* appMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
            [menubar addItem:appMenuItem];
            [NSApp setMainMenu:menubar];

            NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@""];
            NSString* quitTitle = [@"Quit " stringByAppendingString:[[NSProcessInfo processInfo] processName]];
            NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:quitTitle action:@selector(terminate:) keyEquivalent:@"q"];
            [appMenu addItem:quitItem];
            [appMenuItem setSubmenu:appMenu];
        }
        [NSApp finishLaunching];
    };

    if ([NSThread isMainThread])
        init();
    else
        dispatch_sync(dispatch_get_main_queue(), init);
}

static NSString* macStatusItemText(const char* title)
{
    if (!title || !title[0])
        return @"MB";
    NSString* text = [NSString stringWithUTF8String:title];
    return text ? text : @"MB";
}

static void macApplyStatusItem(MacStatusItemHandle* handle, UINT callbackMessage, const char* title, bool hidden)
{
    if (!handle || !handle->item)
        return;
    handle->target->_callbackMessage = callbackMessage;

    NSString* text = macStatusItemText(title);
    if ([handle->item respondsToSelector:@selector(setVisible:)])
        [handle->item setVisible:!hidden];
    NSStatusBarButton* button = [handle->item button];
    if (!button)
        return;
    [button setToolTip:text];
    [button setTitle:text];
    [button setTarget:handle->target];
    [button setAction:@selector(statusItemClicked:)];
}

extern "C" void* MacCreateStatusItem(HWND hwnd, UINT id, UINT callbackMessage, const char* title, bool hidden)
{
    __block MacStatusItemHandle* handle = nullptr;
    auto create = ^{
        MacInitializeApplication();
        handle = new MacStatusItemHandle();
        handle->target = [[MacStatusItemTarget alloc] initWithHwnd:hwnd id:id callbackMessage:callbackMessage];
        handle->item = [[[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength] retain];
        macApplyStatusItem(handle, callbackMessage, title, hidden);
    };
    if ([NSThread isMainThread])
        create();
    else
        dispatch_sync(dispatch_get_main_queue(), create);
    return handle;
}

extern "C" bool MacUpdateStatusItem(void* statusItem, UINT callbackMessage, const char* title, bool hidden)
{
    MacStatusItemHandle* handle = (MacStatusItemHandle*)statusItem;
    if (!handle)
        return false;
    auto update = ^{
        macApplyStatusItem(handle, callbackMessage, title, hidden);
    };
    if ([NSThread isMainThread])
        update();
    else
        dispatch_sync(dispatch_get_main_queue(), update);
    return true;
}

extern "C" void MacDestroyStatusItem(void* statusItem)
{
    MacStatusItemHandle* handle = (MacStatusItemHandle*)statusItem;
    if (!handle)
        return;
    auto destroy = ^{
        if (handle->item) {
            [[NSStatusBar systemStatusBar] removeStatusItem:handle->item];
            [handle->item release];
            handle->item = nil;
        }
        [handle->target release];
        handle->target = nil;
    };
    if ([NSThread isMainThread])
        destroy();
    else
        dispatch_sync(dispatch_get_main_queue(), destroy);
    delete handle;
}

extern "C" ATOM RegisterClassExW(CONST WNDCLASSEXW* wndClass)
{
    if (!wndClass || !wndClass->lpszClassName || !wndClass->lpfnWndProc)
        return 0;
    HwndMac::ensureStatics();
    std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
    (*HwndMac::s_wndClassMap)[wideKey(wndClass->lpszClassName)] = *wndClass;
    return (ATOM)(HwndMac::s_wndClassMap->size() & 0xffff);
}

extern "C" ATOM RegisterClassW(CONST WNDCLASSW* wndClass)
{
    if (!wndClass)
        return 0;
    WNDCLASSEXW ex = { 0 };
    ex.cbSize = sizeof(ex);
    ex.style = wndClass->style;
    ex.lpfnWndProc = wndClass->lpfnWndProc;
    ex.cbClsExtra = wndClass->cbClsExtra;
    ex.cbWndExtra = wndClass->cbWndExtra;
    ex.hInstance = wndClass->hInstance;
    ex.hIcon = wndClass->hIcon;
    ex.hCursor = wndClass->hCursor;
    ex.hbrBackground = wndClass->hbrBackground;
    ex.lpszMenuName = wndClass->lpszMenuName;
    ex.lpszClassName = wndClass->lpszClassName;
    return RegisterClassExW(&ex);
}

extern "C" BOOL GetClassInfoExW(HINSTANCE hInstance, LPCWSTR lpszClass, LPWNDCLASSEXW lpwcx)
{
    if (!lpszClass || !lpwcx)
        return FALSE;
    HwndMac::ensureStatics();
    std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
    auto it = HwndMac::s_wndClassMap->find(wideKey(lpszClass));
    if (it == HwndMac::s_wndClassMap->end())
        return FALSE;
    *lpwcx = it->second;
    return TRUE;
}

extern "C" BOOL GetClassInfoW(HINSTANCE hInstance, LPCWSTR lpClassName, LPWNDCLASSW lpWndClass)
{
    WNDCLASSEXW ex = { 0 };
    if (!GetClassInfoExW(hInstance, lpClassName, &ex) || !lpWndClass)
        return FALSE;
    lpWndClass->style = ex.style;
    lpWndClass->lpfnWndProc = ex.lpfnWndProc;
    lpWndClass->cbClsExtra = ex.cbClsExtra;
    lpWndClass->cbWndExtra = ex.cbWndExtra;
    lpWndClass->hInstance = ex.hInstance;
    lpWndClass->hIcon = ex.hIcon;
    lpWndClass->hCursor = ex.hCursor;
    lpWndClass->hbrBackground = ex.hbrBackground;
    lpWndClass->lpszMenuName = ex.lpszMenuName;
    lpWndClass->lpszClassName = ex.lpszClassName;
    return TRUE;
}

extern "C" HWND CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    MacInitializeApplication();
    HwndMac::ensureStatics();

    WNDCLASSEXW wndClass = { 0 };
    {
        std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
        auto it = HwndMac::s_wndClassMap->find(wideKey(lpClassName));
        if (it == HwndMac::s_wndClassMap->end())
            return nullptr;
        wndClass = it->second;
    }

    if (nWidth <= 0 || nWidth == CW_USEDEFAULT)
        nWidth = 800;
    if (nHeight <= 0 || nHeight == CW_USEDEFAULT)
        nHeight = 600;
    if (X == CW_USEDEFAULT)
        X = 100;
    if (Y == CW_USEDEFAULT)
        Y = 100;

    HwndMac* self = new HwndMac();
    self->m_autoHandleClose = true;
    self->m_parent = hWndParent;
    self->m_menu = hMenu;
    self->m_wndProc = wndClass.lpfnWndProc;
    self->m_userdata = lpParam;
    self->m_style = dwStyle;
    self->m_styleex = dwExStyle;
    self->m_threadId = GetCurrentThreadId();
    self->m_clientRect = { 0, 0, nWidth, nHeight };
    self->m_windowRect = { X, Y, X + nWidth, Y + nHeight };

    NSRect contentRect = NSMakeRect(X, Y, nWidth, nHeight);
    NSUInteger styleMask = NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    if (dwStyle & WS_CAPTION)
        styleMask |= NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
    if (!(dwStyle & WS_POPUP))
        styleMask |= NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;

    NSWindow* window = [[NSWindow alloc] initWithContentRect:contentRect styleMask:styleMask backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:wideToNSString(lpWindowName)];
    [window setAcceptsMouseMovedEvents:YES];

    MacWindowView* view = [[MacWindowView alloc] initWithHwnd:self frame:NSMakeRect(0, 0, nWidth, nHeight)];
    MacWindowDelegate* delegate = [[MacWindowDelegate alloc] initWithHwnd:self];
    [window setContentView:view];
    [window setDelegate:delegate];
    [window makeFirstResponder:view];

    self->m_window = window;
    self->m_view = view;

    {
        std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
        HwndMac::s_hwnds->insert((HWND)self);
    }

    CREATESTRUCTW createStruct = { 0 };
    createStruct.lpCreateParams = lpParam;
    createStruct.hInstance = hInstance;
    createStruct.hMenu = hMenu;
    createStruct.hwndParent = hWndParent;
    createStruct.cy = nHeight;
    createStruct.cx = nWidth;
    createStruct.y = Y;
    createStruct.x = X;
    createStruct.style = dwStyle;
    createStruct.lpszName = lpWindowName;
    createStruct.lpszClass = lpClassName;
    createStruct.dwExStyle = dwExStyle;
    self->m_wndProc((HWND)self, WM_CREATE, 0, (LPARAM)&createStruct);
    self->m_wndProc((HWND)self, WM_SIZE, SIZE_RESTORED, MAKELPARAM(nWidth, nHeight));

    return (HWND)self;
}

extern "C" HWND LinuxGdiBindWindowByGtk(
    void* rootWindow, void* drawingArea, BOOL isGl, DWORD dwExStyle, LPCWSTR lpClassName, DWORD dwStyle, int nWidth, int nHeight, LPVOID lpParam)
{
    HwndMac::ensureStatics();
    WNDCLASSEXW wndClass = { 0 };
    {
        std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
        auto it = HwndMac::s_wndClassMap->find(wideKey(lpClassName));
        if (it == HwndMac::s_wndClassMap->end())
            return nullptr;
        wndClass = it->second;
    }

    if (nWidth <= 0)
        nWidth = 800;
    if (nHeight <= 0)
        nHeight = 600;

    HwndMac* self = new HwndMac();
    self->m_autoHandleClose = false;
    self->m_window = rootWindow;
    self->m_view = drawingArea;
    self->m_wndProc = wndClass.lpfnWndProc;
    self->m_userdata = lpParam;
    self->m_style = dwStyle;
    self->m_styleex = dwExStyle;
    self->m_threadId = GetCurrentThreadId();
    self->m_clientRect = { 0, 0, nWidth, nHeight };
    self->m_windowRect = { 0, 0, nWidth, nHeight };

    NSView* view = (NSView*)drawingArea;
    if (view && [view isKindOfClass:[NSView class]]) {
        [view setWantsLayer:YES];
        self->m_clientRect.right = (LONG)[view bounds].size.width;
        self->m_clientRect.bottom = (LONG)[view bounds].size.height;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
        HwndMac::s_hwnds->insert((HWND)self);
    }

    CREATESTRUCTW createStruct = { 0 };
    createStruct.lpCreateParams = lpParam;
    createStruct.cy = nHeight;
    createStruct.cx = nWidth;
    createStruct.style = dwStyle;
    createStruct.lpszClass = lpClassName;
    createStruct.dwExStyle = dwExStyle;
    self->m_wndProc((HWND)self, WM_CREATE, 0, (LPARAM)&createStruct);
    self->m_wndProc((HWND)self, WM_SIZE, SIZE_RESTORED, MAKELPARAM(nWidth, nHeight));
    return (HWND)self;
}

extern "C" BOOL DestroyWindow(HWND hWnd)
{
    HwndMac::destroy(hWnd, false);
    return TRUE;
}

extern "C" BOOL IsWindow(HWND hWnd)
{
    return HwndMac::isValid(hWnd);
}

extern "C" BOOL ShowWindow(HWND hWnd, int nCmdShow)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_window)
        return FALSE;
    NSWindow* window = (NSWindow*)self->m_window;
    if (nCmdShow == SW_HIDE) {
        [window orderOut:nil];
        self->m_visible = false;
        self->m_minimized = false;
    } else if (nCmdShow == SW_MINIMIZE) {
        [window miniaturize:nil];
        self->m_visible = true;
        self->m_minimized = true;
    } else if (nCmdShow == SW_MAXIMIZE) {
        if ([window isMiniaturized])
            [window deminiaturize:nil];
        self->m_minimized = false;
        [window zoom:nil];
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        self->m_visible = true;
    } else if (nCmdShow == SW_RESTORE) {
        if ([window isMiniaturized])
            [window deminiaturize:nil];
        self->m_minimized = false;
        if ([window isZoomed])
            [window zoom:nil];
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        self->m_visible = true;
    } else {
        if ([window isMiniaturized])
            [window deminiaturize:nil];
        self->m_minimized = false;
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        self->m_visible = true;
    }
    return TRUE;
}

extern "C" BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_window)
        return FALSE;
    NSWindow* window = (NSWindow*)self->m_window;
    NSRect frame = [window frame];
    if (!(uFlags & SWP_NOMOVE)) {
        frame.origin.x = X;
        frame.origin.y = Y;
    }
    if (!(uFlags & SWP_NOSIZE)) {
        frame.size.width = std::max(1, cx);
        frame.size.height = std::max(1, cy);
        self->m_clientRect = { 0, 0, (LONG)frame.size.width, (LONG)frame.size.height };
    }
    [window setFrame:frame display:YES];
    self->m_windowRect = { (LONG)frame.origin.x, (LONG)frame.origin.y, (LONG)(frame.origin.x + frame.size.width), (LONG)(frame.origin.y + frame.size.height) };
    return TRUE;
}

extern "C" BOOL MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint)
{
    BOOL ok = SetWindowPos(hWnd, nullptr, X, Y, nWidth, nHeight, 0);
    if (ok && bRepaint)
        InvalidateRect(hWnd, nullptr, TRUE);
    return ok;
}

extern "C" BOOL GetClientRect(HWND hWnd, LPRECT lpRect)
{
    if (!lpRect)
        return FALSE;
    HwndMac* self = HwndMac::from(hWnd);
    if (!self)
        return FALSE;
    if (self->m_view) {
        NSView* view = (NSView*)self->m_view;
        if ([view isKindOfClass:[NSView class]]) {
            NSRect bounds = [view bounds];
            self->m_clientRect = { 0, 0, (LONG)bounds.size.width, (LONG)bounds.size.height };
        }
    }
    *lpRect = self->m_clientRect;
    return TRUE;
}

extern "C" BOOL GetWindowRect(HWND hWnd, LPRECT lpRect)
{
    if (!lpRect)
        return FALSE;
    HwndMac* self = HwndMac::from(hWnd);
    if (!self)
        return FALSE;
    if (self->m_window) {
        NSWindow* window = (NSWindow*)self->m_window;
        if ([window isKindOfClass:[NSWindow class]]) {
            NSRect frame = [window frame];
            self->m_windowRect = { (LONG)frame.origin.x, (LONG)frame.origin.y, (LONG)(frame.origin.x + frame.size.width), (LONG)(frame.origin.y + frame.size.height) };
        }
    }
    *lpRect = self->m_windowRect;
    return TRUE;
}

extern "C" BOOL InvalidateRect(HWND hWnd, CONST RECT* lpRect, BOOL bErase)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_view)
        return FALSE;
    NSView* view = (NSView*)self->m_view;
    if (![view isKindOfClass:[NSView class]])
        return FALSE;
    if (lpRect) {
        NSRect rect = NSMakeRect(lpRect->left, lpRect->top, lpRect->right - lpRect->left, lpRect->bottom - lpRect->top);
        [view setNeedsDisplayInRect:rect];
    } else {
        [view setNeedsDisplay:YES];
    }
    return TRUE;
}

extern "C" BOOL UpdateWindow(HWND hWnd)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_view)
        return FALSE;
    [(NSView*)self->m_view displayIfNeeded];
    return TRUE;
}

extern "C" HDC BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_msgPtr || !lpPaint)
        return nullptr;
    *lpPaint = *(LPPAINTSTRUCT)self->m_msgPtr;
    return lpPaint->hdc;
}

extern "C" BOOL EndPaint(HWND hWnd, CONST PAINTSTRUCT* lpPaint)
{
    return TRUE;
}

extern "C" LONG SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self)
        return 0;
    LONG oldValue = 0;
    if (nIndex == GWL_STYLE) {
        oldValue = (LONG)self->m_style;
        self->m_style = (DWORD)dwNewLong;
    } else if (nIndex == GWL_EXSTYLE) {
        oldValue = (LONG)self->m_styleex;
        self->m_styleex = (DWORD)dwNewLong;
    } else if (nIndex == GWLP_USERDATA) {
        oldValue = (LONG)(intptr_t)self->m_userdata;
        self->m_userdata = (LPVOID)(intptr_t)dwNewLong;
    } else if (nIndex == GWLP_WNDPROC) {
        oldValue = (LONG)(intptr_t)self->m_wndProc;
        self->m_wndProc = (WNDPROC)(intptr_t)dwNewLong;
    }
    return oldValue;
}

extern "C" LONG GetWindowLongW(HWND hWnd, int nIndex)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self)
        return 0;
    if (nIndex == GWL_STYLE)
        return (LONG)self->m_style;
    if (nIndex == GWL_EXSTYLE)
        return (LONG)self->m_styleex;
    if (nIndex == GWLP_USERDATA)
        return (LONG)(intptr_t)self->m_userdata;
    if (nIndex == GWLP_WNDPROC)
        return (LONG)(intptr_t)self->m_wndProc;
    return 0;
}

extern "C" BOOL SetPropW(HWND hWnd, LPCWSTR lpString, HANDLE hData)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !lpString)
        return FALSE;
    std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
    self->m_props[HwndMac::hashString(lpString)] = hData;
    return TRUE;
}

extern "C" HANDLE GetPropW(HWND hWnd, LPCWSTR lpString)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !lpString)
        return nullptr;
    std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
    auto it = self->m_props.find(HwndMac::hashString(lpString));
    return it == self->m_props.end() ? nullptr : (HANDLE)it->second;
}

extern "C" HANDLE RemovePropW(HWND hWnd, LPCWSTR lpString)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !lpString)
        return nullptr;
    std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
    auto it = self->m_props.find(HwndMac::hashString(lpString));
    if (it == self->m_props.end())
        return nullptr;
    HANDLE value = (HANDLE)it->second;
    self->m_props.erase(it);
    return value;
}

extern "C" LRESULT DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg == WM_CLOSE) {
        DestroyWindow(hWnd);
        return 0;
    }
    return 0;
}

extern "C" LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_wndProc)
        return 0;
    return self->m_wndProc(hWnd, Msg, wParam, lParam);
}

extern "C" BOOL PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    MSG msg = { 0 };
    msg.hwnd = hWnd;
    msg.message = Msg;
    msg.wParam = wParam;
    msg.lParam = lParam;
    msg.time = GetTickCount();
    GetCursorPos(&msg.pt);
    {
        std::lock_guard<std::mutex> lock(g_messageQueueMutex);
        g_messageQueue.push_back(msg);
    }
    g_messageQueueCondition.notify_one();
    if (hWnd) {
        dispatch_async(dispatch_get_main_queue(), ^{
            dispatchQueuedMessagesForWindow(hWnd);
        });
    }
    return TRUE;
}

extern "C" BOOL GetMessageW(MSG* lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    if (!lpMsg)
        return FALSE;
    for (;;) {
        std::unique_lock<std::mutex> lock(g_messageQueueMutex);
        if (takeQueuedMessage(lpMsg, hWnd, true))
            return lpMsg->message != WM_QUIT;
        if (![NSThread isMainThread]) {
            g_messageQueueCondition.wait_for(lock, std::chrono::milliseconds(10));
            continue;
        }
        lock.unlock();
        pumpCocoaOnce([NSDate dateWithTimeIntervalSinceNow:0.01]);
    }
}

extern "C" BOOL PeekMessageW(MSG* lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    if (lpMsg) {
        std::lock_guard<std::mutex> lock(g_messageQueueMutex);
        if (takeQueuedMessage(lpMsg, hWnd, (wRemoveMsg & PM_REMOVE) != 0))
            return TRUE;
    }
    pumpCocoaOnce([NSDate distantPast]);
    return FALSE;
}

extern "C" LRESULT DispatchMessageW(CONST MSG* lpMsg)
{
    if (!lpMsg)
        return 0;
    return SendMessageW(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
}

extern "C" BOOL TranslateMessage(CONST MSG* lpMsg)
{
    return TRUE;
}

extern "C" BOOL RegisterHotKey(HWND hWnd, int id, UINT fsModifiers, UINT vk)
{
    if ((hWnd && !HwndMac::isValid(hWnd)) || id < 0 || vk == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    UINT modifiers = normalizeHotKeyModifiers(fsModifiers);
    std::lock_guard<std::mutex> lock(g_hotKeyMutex);
    for (const HotKeyRegistration& registration : g_hotKeys) {
        if (registration.hwnd == hWnd && registration.id == id) {
            SetLastError(ERROR_ALREADY_EXISTS);
            return FALSE;
        }
        if (registration.modifiers == modifiers && registration.vk == vk) {
            SetLastError(ERROR_ALREADY_EXISTS);
            return FALSE;
        }
    }

    g_hotKeys.push_back({ hWnd, id, modifiers, vk });
    SetLastError(0);
    return TRUE;
}

extern "C" BOOL UnregisterHotKey(HWND hWnd, int id)
{
    std::lock_guard<std::mutex> lock(g_hotKeyMutex);
    auto it = std::find_if(g_hotKeys.begin(), g_hotKeys.end(), [hWnd, id](const HotKeyRegistration& registration) {
        return registration.hwnd == hWnd && registration.id == id;
    });
    if (it == g_hotKeys.end()) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    g_hotKeys.erase(it);
    SetLastError(0);
    return TRUE;
}

extern "C" VOID PostQuitMessage(int nExitCode)
{
    PostMessageW(nullptr, WM_QUIT, (WPARAM)nExitCode, 0);
}

extern "C" BOOL GetCursorPos(POINT* lpPoint)
{
    if (!lpPoint)
        return FALSE;
    NSPoint point = [NSEvent mouseLocation];
    lpPoint->x = (LONG)point.x;
    lpPoint->y = (LONG)point.y;
    return TRUE;
}

extern "C" BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !lpPoint || !self->m_window || !self->m_view)
        return FALSE;
    NSWindow* window = (NSWindow*)self->m_window;
    NSView* view = (NSView*)self->m_view;
    NSPoint screenPoint = NSMakePoint(lpPoint->x, lpPoint->y);
    NSPoint windowPoint = [window convertPointFromScreen:screenPoint];
    NSPoint viewPoint = [view convertPoint:windowPoint fromView:nil];
    lpPoint->x = (LONG)viewPoint.x;
    lpPoint->y = (LONG)viewPoint.y;
    return TRUE;
}

extern "C" BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !lpPoint || !self->m_window || !self->m_view)
        return FALSE;
    NSWindow* window = (NSWindow*)self->m_window;
    NSView* view = (NSView*)self->m_view;
    NSPoint viewPoint = NSMakePoint(lpPoint->x, lpPoint->y);
    NSPoint windowPoint = [view convertPoint:viewPoint toView:nil];
    NSPoint screenPoint = [window convertPointToScreen:windowPoint];
    lpPoint->x = (LONG)screenPoint.x;
    lpPoint->y = (LONG)screenPoint.y;
    return TRUE;
}

extern "C" HWND GetParent(HWND hWnd)
{
    HwndMac* self = HwndMac::from(hWnd);
    return self ? self->m_parent : nullptr;
}

extern "C" DWORD GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId)
{
    if (lpdwProcessId)
        *lpdwProcessId = GetCurrentProcessId();
    HwndMac* self = HwndMac::from(hWnd);
    return self ? self->m_threadId : 0;
}

extern "C" HWND GetFocus(void)
{
    return g_focusWindow;
}

extern "C" HWND SetFocus(HWND hWnd)
{
    HWND oldFocus = g_focusWindow;
    HwndMac* self = HwndMac::from(hWnd);
    if (self && self->m_window && self->m_view) {
        [(NSWindow*)self->m_window makeFirstResponder:(NSView*)self->m_view];
        g_focusWindow = hWnd;
    }
    return oldFocus;
}

extern "C" HWND SetCapture(HWND hWnd)
{
    HWND oldCapture = g_captureWindow;
    g_captureWindow = hWnd;
    return oldCapture;
}

extern "C" BOOL ReleaseCapture(void)
{
    g_captureWindow = nullptr;
    return TRUE;
}

extern "C" BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpString)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_window)
        return FALSE;
    [(NSWindow*)self->m_window setTitle:wideToNSString(lpString)];
    return TRUE;
}

extern "C" int GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_window || !lpString || nMaxCount <= 0)
        return 0;
    NSString* title = [(NSWindow*)self->m_window title];
    std::string utf8([title UTF8String] ? [title UTF8String] : "");
    int copied = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, lpString, nMaxCount);
    return copied > 0 ? copied - 1 : 0;
}

extern "C" BOOL EnableWindow(HWND hWnd, BOOL bEnable)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_window)
        return FALSE;
    [(NSWindow*)self->m_window setIgnoresMouseEvents:!bEnable];
    return TRUE;
}

extern "C" BOOL IsWindowEnabled(HWND hWnd)
{
    HwndMac* self = HwndMac::from(hWnd);
    return (self && self->m_window && ![(NSWindow*)self->m_window ignoresMouseEvents]) ? TRUE : FALSE;
}

extern "C" BOOL IsWindowVisible(HWND hWnd)
{
    HwndMac* self = HwndMac::from(hWnd);
    return (self && self->m_window && [(NSWindow*)self->m_window isVisible]) ? TRUE : FALSE;
}

extern "C" BOOL IsZoomed(HWND hWnd)
{
    HwndMac* self = HwndMac::from(hWnd);
    return (self && self->m_window && [(NSWindow*)self->m_window isZoomed]) ? TRUE : FALSE;
}

extern "C" BOOL IsIconic(HWND hWnd)
{
    HwndMac* self = HwndMac::from(hWnd);
    return (self && self->m_window && (self->m_minimized || [(NSWindow*)self->m_window isMiniaturized])) ? TRUE : FALSE;
}

extern "C" HWND GetActiveWindow(void)
{
    NSWindow* keyWindow = [NSApp keyWindow];
    if (!keyWindow)
        return nullptr;
    HwndMac::ensureStatics();
    std::lock_guard<std::recursive_mutex> lock(*HwndMac::s_hwndMutex);
    for (HWND hwnd : *HwndMac::s_hwnds) {
        HwndMac* self = (HwndMac*)hwnd;
        if (self->m_window == keyWindow)
            return hwnd;
    }
    return nullptr;
}

extern "C" BOOL SetForegroundWindow(HWND hWnd)
{
    HwndMac* self = HwndMac::from(hWnd);
    if (!self || !self->m_window)
        return FALSE;
    [(NSWindow*)self->m_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    return TRUE;
}

extern "C" HICON LoadIconW(HINSTANCE hInstance, LPCWSTR lpIconName)
{
    return nullptr;
}

extern "C" HCURSOR LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName)
{
    if (lpCursorName == IDC_IBEAM)
        return (HCURSOR)[NSCursor IBeamCursor];
    if (lpCursorName == IDC_HAND)
        return (HCURSOR)[NSCursor pointingHandCursor];
    if (lpCursorName == IDC_CROSS)
        return (HCURSOR)[NSCursor crosshairCursor];
    if (lpCursorName == IDC_SIZEWE)
        return (HCURSOR)[NSCursor resizeLeftRightCursor];
    if (lpCursorName == IDC_SIZENS)
        return (HCURSOR)[NSCursor resizeUpDownCursor];
    if (lpCursorName == IDC_NO)
        return (HCURSOR)[NSCursor operationNotAllowedCursor];
    return (HCURSOR)[NSCursor arrowCursor];
}

extern "C" HCURSOR SetCursor(HCURSOR hCursor)
{
    if (hCursor)
        [(NSCursor*)hCursor set];
    return hCursor;
}

extern "C" HCURSOR linuxSetCursor(HWND hWnd, HCURSOR hCursor)
{
    return SetCursor(hCursor);
}

extern "C" SHORT GetKeyState(int nVirtKey)
{
    NSEventModifierFlags flags = [NSEvent modifierFlags];
    if (nVirtKey == VK_SHIFT)
        return (flags & NSEventModifierFlagShift) ? 0x8000 : 0;
    if (nVirtKey == VK_CONTROL)
        return (flags & NSEventModifierFlagControl) ? 0x8000 : 0;
    if (nVirtKey == VK_MENU)
        return (flags & NSEventModifierFlagOption) ? 0x8000 : 0;
    if (nVirtKey == VK_CAPITAL)
        return (flags & NSEventModifierFlagCapsLock) ? 1 : 0;
    return 0;
}

extern "C" int GetSystemMetrics(int nIndex)
{
    NSScreen* screen = [NSScreen mainScreen];
    NSRect frame = screen ? [screen frame] : NSMakeRect(0, 0, 1024, 768);
    if (nIndex == SM_CXSCREEN)
        return (int)frame.size.width;
    if (nIndex == SM_CYSCREEN)
        return (int)frame.size.height;
    if (nIndex == SM_CMONITORS)
        return (int)[[NSScreen screens] count];
    return 0;
}

static RECT rectFromScreen(NSScreen* screen)
{
    NSRect frame = [screen frame];
    return { (LONG)frame.origin.x, (LONG)frame.origin.y,
        (LONG)(frame.origin.x + frame.size.width), (LONG)(frame.origin.y + frame.size.height) };
}

static HMONITOR primaryMonitor(NSArray<NSScreen*>* screens)
{
    return (HMONITOR)([NSScreen mainScreen] ?: [screens firstObject]);
}

static long long rectIntersectionArea(const RECT& a, const RECT& b)
{
    LONG left = std::max(a.left, b.left);
    LONG top = std::max(a.top, b.top);
    LONG right = std::min(a.right, b.right);
    LONG bottom = std::min(a.bottom, b.bottom);
    if (left >= right || top >= bottom)
        return 0;
    return static_cast<long long>(right - left) * static_cast<long long>(bottom - top);
}

static long long rectDistanceSquared(const RECT& a, const RECT& b)
{
    long long dx = 0;
    if (a.right < b.left)
        dx = static_cast<long long>(b.left) - a.right;
    else if (b.right < a.left)
        dx = static_cast<long long>(a.left) - b.right;

    long long dy = 0;
    if (a.bottom < b.top)
        dy = static_cast<long long>(b.top) - a.bottom;
    else if (b.bottom < a.top)
        dy = static_cast<long long>(a.top) - b.bottom;

    return dx * dx + dy * dy;
}

static long long pointDistanceSquaredToRect(POINT pt, const RECT& rect)
{
    long long dx = 0;
    if (pt.x < rect.left)
        dx = static_cast<long long>(rect.left) - pt.x;
    else if (pt.x >= rect.right)
        dx = static_cast<long long>(pt.x) - rect.right;

    long long dy = 0;
    if (pt.y < rect.top)
        dy = static_cast<long long>(rect.top) - pt.y;
    else if (pt.y >= rect.bottom)
        dy = static_cast<long long>(pt.y) - rect.bottom;

    return dx * dx + dy * dy;
}

extern "C" HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags)
{
    NSArray<NSScreen*>* screens = [NSScreen screens];
    for (NSScreen* screen in screens) {
        RECT rect = rectFromScreen(screen);
        if (pt.x >= rect.left && pt.x < rect.right && pt.y >= rect.top && pt.y < rect.bottom)
            return (HMONITOR)screen;
    }
    if (dwFlags == MONITOR_DEFAULTTONULL)
        return nullptr;
    if (dwFlags == MONITOR_DEFAULTTONEAREST) {
        HMONITOR nearest = nullptr;
        long long nearest_distance = 0;
        for (NSScreen* screen in screens) {
            RECT rect = rectFromScreen(screen);
            long long distance = pointDistanceSquaredToRect(pt, rect);
            if (!nearest || distance < nearest_distance) {
                nearest = (HMONITOR)screen;
                nearest_distance = distance;
            }
        }
        if (nearest)
            return nearest;
    }
    return primaryMonitor(screens);
}

extern "C" HMONITOR MonitorFromRect(const RECT* lprc, DWORD dwFlags)
{
    NSArray<NSScreen*>* screens = [NSScreen screens];
    if (!lprc) {
        if (dwFlags == MONITOR_DEFAULTTONULL)
            return nullptr;
        return primaryMonitor(screens);
    }

    HMONITOR best_intersection = nullptr;
    long long best_area = 0;
    for (NSScreen* screen in screens) {
        RECT rect = rectFromScreen(screen);
        long long area = rectIntersectionArea(*lprc, rect);
        if (area > best_area) {
            best_intersection = (HMONITOR)screen;
            best_area = area;
        }
    }
    if (best_intersection)
        return best_intersection;
    if (dwFlags == MONITOR_DEFAULTTONULL)
        return nullptr;
    if (dwFlags == MONITOR_DEFAULTTONEAREST) {
        HMONITOR nearest = nullptr;
        long long nearest_distance = 0;
        for (NSScreen* screen in screens) {
            RECT rect = rectFromScreen(screen);
            long long distance = rectDistanceSquared(*lprc, rect);
            if (!nearest || distance < nearest_distance) {
                nearest = (HMONITOR)screen;
                nearest_distance = distance;
            }
        }
        if (nearest)
            return nearest;
    }
    return primaryMonitor(screens);
}

extern "C" HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags)
{
    HwndMac* self = HwndMac::from(hwnd);
    if (self && self->m_window) {
        NSWindow* window = (NSWindow*)self->m_window;
        NSScreen* screen = [window screen];
        if (screen)
            return (HMONITOR)screen;
    }

    if (dwFlags == MONITOR_DEFAULTTONULL)
        return nullptr;
    return (HMONITOR)([NSScreen mainScreen] ?: [[NSScreen screens] firstObject]);
}

extern "C" BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi)
{
    if (!lpmi || lpmi->cbSize < sizeof(MONITORINFO))
        return FALSE;

    NSScreen* screen = hMonitor ? (NSScreen*)hMonitor : [NSScreen mainScreen];
    if (!screen)
        return FALSE;

    NSRect frame = [screen frame];
    NSRect work = [screen visibleFrame];
    lpmi->rcMonitor = { (LONG)frame.origin.x, (LONG)frame.origin.y,
        (LONG)(frame.origin.x + frame.size.width), (LONG)(frame.origin.y + frame.size.height) };
    lpmi->rcWork = { (LONG)work.origin.x, (LONG)work.origin.y,
        (LONG)(work.origin.x + work.size.width), (LONG)(work.origin.y + work.size.height) };
    lpmi->dwFlags = screen == [NSScreen mainScreen] ? MONITORINFOF_PRIMARY : 0;
    if (lpmi->cbSize >= sizeof(MONITORINFOEXW)) {
        LPMONITORINFOEXW infoEx = (LPMONITORINFOEXW)lpmi;
        const WCHAR name[] = u"\\\\.\\DISPLAY1";
        size_t i = 0;
        for (; i + 1 < CCHDEVICENAME && name[i]; ++i)
            infoEx->szDevice[i] = name[i];
        for (; i < CCHDEVICENAME; ++i)
            infoEx->szDevice[i] = 0;
        infoEx->szDevice[CCHDEVICENAME - 1] = 0;
    }
    return TRUE;
}

extern "C" BOOL EnumDisplayMonitors(HDC hdc, const RECT* lprcClip, MONITORENUMPROC lpfnEnum, LPARAM dwData)
{
    if (!lpfnEnum)
        return FALSE;
    NSArray<NSScreen*>* screens = [NSScreen screens];
    for (NSScreen* screen in screens) {
        NSRect frame = [screen frame];
        RECT rect = { (LONG)frame.origin.x, (LONG)frame.origin.y,
            (LONG)(frame.origin.x + frame.size.width), (LONG)(frame.origin.y + frame.size.height) };
        if (lprcClip) {
            RECT overlap = {};
            if (!IntersectRect(&overlap, &rect, lprcClip))
                continue;
        }
        if (!lpfnEnum((HMONITOR)screen, hdc, &rect, dwData))
            return TRUE;
    }
    return TRUE;
}

extern "C" UINT GetDoubleClickTime(void)
{
    return (UINT)([NSEvent doubleClickInterval] * 1000.0);
}

extern "C" void* HwndToNSWindow(HWND hwnd)
{
    HwndMac* self = HwndMac::from(hwnd);
    return self ? self->m_window : nullptr;
}

extern "C" bool MacRunOpenPanel(HWND owner, bool chooseDirectory, bool saveAs, bool allowMultiple, const char* title, const char* defaultPath,
    std::vector<std::string>* paths)
{
    if (!paths)
        return false;
    paths->clear();

    bool accepted = false;
    @autoreleasepool {
        NSString* titleString = title && title[0] ? [NSString stringWithUTF8String:title] : nil;
        NSString* defaultString = defaultPath && defaultPath[0] ? [NSString stringWithUTF8String:defaultPath] : nil;

        NSSavePanel* panel = saveAs ? [NSSavePanel savePanel] : [NSOpenPanel openPanel];
        if (titleString)
            [panel setTitle:titleString];
        if (defaultString) {
            OBJC_BOOL isDirectory = NO;
            if ([[NSFileManager defaultManager] fileExistsAtPath:defaultString isDirectory:&isDirectory]) {
                if (isDirectory)
                    [panel setDirectoryURL:[NSURL fileURLWithPath:defaultString]];
                else {
                    [panel setDirectoryURL:[NSURL fileURLWithPath:[defaultString stringByDeletingLastPathComponent]]];
                    [panel setNameFieldStringValue:[defaultString lastPathComponent]];
                }
            } else {
                [panel setNameFieldStringValue:[defaultString lastPathComponent]];
            }
        }

        if (!saveAs) {
            NSOpenPanel* openPanel = (NSOpenPanel*)panel;
            [openPanel setCanChooseFiles:!chooseDirectory];
            [openPanel setCanChooseDirectories:chooseDirectory];
            [openPanel setAllowsMultipleSelection:allowMultiple];
        }

        NSInteger result = [panel runModal];
        if (result != NSModalResponseOK)
            return false;

        accepted = true;
        if (saveAs) {
            NSURL* url = [panel URL];
            if (url && [url isFileURL])
                paths->push_back([[url path] UTF8String]);
        } else {
            for (NSURL* url in [(NSOpenPanel*)panel URLs]) {
                if (url && [url isFileURL])
                    paths->push_back([[url path] UTF8String]);
            }
        }
    }
    return accepted;
}

extern "C" void* HwndToNSView(HWND hwnd)
{
    HwndMac* self = HwndMac::from(hwnd);
    return self ? self->m_view : nullptr;
}

void* HwndToGtkWindow(HWND hwnd)
{
    return HwndToNSWindow(hwnd);
}
