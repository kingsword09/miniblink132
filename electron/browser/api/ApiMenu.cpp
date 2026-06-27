
#include "electron/nodeblink.h"

#include <windows.h>
#include "electron/browser/api/WindowInterface.h"
#include "electron/browser/api/WindowList.h"
#include "electron/browser/api/MenuEventNotif.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/HideWndHelp.h"
#include "electron/common/api/EventEmitter.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libuv/include/uv.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include <set>

namespace atom {

class Menu;

class MenuItem {
public:
    enum MenuItemType { ActionType, CheckableActionType, SeparatorType, SubmenuType };

    MenuItem(v8::Isolate* isolate, Menu* menu);

    ~MenuItem();

    v8::Isolate* getIsolate() const
    {
        return m_isolate;
    }

    Menu* getMenu() const
    {
        return m_menu;
    }

    void setType(MenuItemType type)
    {
        m_type = type;
    }

    MenuItemType getType() const
    {
        return m_type;
    }

    Menu* getSubMenu() const
    {
        return m_subMenu;
    }

    void setSubMenu(Menu* subMenu);

    void setLabel(const std::string label)
    {
        m_label = label;
    }

    void setEnabled(bool b)
    {
        m_isEnabled = b;
    }

    void setChecked(bool b)
    {
        m_isChecked = b;
    }

    bool getChecked() const
    {
        return m_isChecked;
    }

    void setClickCallback(v8::Local<v8::Value> callback)
    {
        m_clickCallbackValue.Reset(m_isolate, callback);
    }

    v8::Local<v8::Value> getClickCallbackValue() const
    {
        return m_clickCallbackValue.Get(m_isolate);
    }

    void setId(int id)
    {
        m_id = id;
    }

    int getId() const
    {
        return m_id;
    }

    UINT getAction() const
    {
        return m_action;
    }

    void insertPlatformMenu(size_t pos, HMENU hMenu) const;

    void clear();

private:
    MenuItemType m_type;
    HMENU m_hSubMenu;
    Menu* m_subMenu;
    std::string m_label;
    bool m_isEnabled;
    bool m_isChecked;
    UINT m_action;
    v8::Persistent<v8::Value> m_clickCallbackValue;
    Menu* m_menu;
    int m_id;
    v8::Isolate* m_isolate;
};

//////////////////////////////////////////////////////////////////////////

class Menu : public mate::EventEmitter<Menu> {
public:
    explicit Menu(v8::Isolate* isolate, v8::Local<v8::Object> wrapper)
        : m_hideWndHelp(nullptr)
    {
        gin_helper::Wrappable<Menu>::InitWith(isolate, wrapper);
        //m_menuTemplate = nullptr;
        m_hMenu = NULL;
        m_isItemNeedRebuilt = false;
        m_isAppOrPopupMenu = kNoInit;
    }

    virtual ~Menu() override
    {
        OutputDebugStringA("~Menu\n");
        ::DestroyMenu(m_hMenu);
        m_hMenu = nullptr;

        if (m_appMenu == this)
            m_appMenu = nullptr;
    }

    static void init(v8::Isolate* isolate, v8::Local<v8::Object> target)
    {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        m_liveMenuItem = new std::set<MenuItem*>();
        v8::Local<v8::FunctionTemplate> prototype = v8::FunctionTemplate::New(isolate, newFunction);

        prototype->SetClassName(v8::String::NewFromUtf8(isolate, "Menu").ToLocalChecked());
        gin_helper::ObjectTemplateBuilder builder(isolate, prototype->InstanceTemplate());
        builder.SetMethod("_setApplicationMenu", &Menu::setApplicationMenuApi);
        builder.SetMethod("_sendActionToFirstResponder", &Menu::sendActionToFirstResponderApi);
        builder.SetMethod("_insert", &Menu::_insertApi);
        builder.SetMethod("_append", &Menu::_appendeApi);
        builder.SetMethod("_popup", &Menu::_popupApi);
        builder.SetMethod("_clear", &Menu::_clearApi);
        builder.SetMethod("_dispatchCommandForTesting", &Menu::dispatchCommandForTestingApi);
        builder.SetMethod("getItemCount", &Menu::getItemCountApi);
        builder.SetMethod("quit", &Menu::nullFunction);

        v8::Local<v8::Function> constructorFunction = prototype->GetFunction(context).ToLocalChecked();
        constructor.Reset(isolate, constructorFunction);
        target->Set(context, v8::String::NewFromUtf8(isolate, "Menu").ToLocalChecked(), constructorFunction).ToChecked();
        target->Set(context,
            v8::String::NewFromUtf8(isolate, "_clearApplicationMenu").ToLocalChecked(),
            v8::FunctionTemplate::New(isolate, clearApplicationMenuBinding)->GetFunction(context).ToLocalChecked()).ToChecked();
        target->Set(context,
            v8::String::NewFromUtf8(isolate, "_sendActionToFirstResponder").ToLocalChecked(),
            v8::FunctionTemplate::New(isolate, sendActionToFirstResponderBinding)->GetFunction(context).ToLocalChecked()).ToChecked();
    }

    void nullFunction()
    {
        DebugBreak();
    }

    // Set the global menubar.
    void setApplicationMenuApi()
    {
        HMENU hmenuBar = buildMenus(true);

        WindowList::iterator winIt = WindowList::getInstance()->begin();
        for (; winIt != WindowList::getInstance()->end(); ++winIt) {
            WindowInterface* windowInterface = *winIt;
            if (!windowInterface)
                continue;
            HWND hParentWnd = windowInterface->getHWND();
            if (hParentWnd)
                ::SetMenu(hParentWnd, hmenuBar);
        }
        m_appMenu = this;
    }

    bool sendActionToFirstResponderApi(const std::string& action)
    {
        return sendActionToFirstResponder(action);
    }

    static void clearApplicationMenuBinding(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        clearApplicationMenu();
    }

    static void sendActionToFirstResponderBinding(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        std::string action;
        if (args.Length() >= 1)
            gin_helper::Converter<std::string>::FromV8(isolate, args[0], &action);
        args.GetReturnValue().Set(sendActionToFirstResponder(action));
    }

    static void clearApplicationMenu()
    {
        WindowList::iterator winIt = WindowList::getInstance()->begin();
        for (; winIt != WindowList::getInstance()->end(); ++winIt) {
            WindowInterface* windowInterface = *winIt;
            if (!windowInterface)
                continue;
            HWND hParentWnd = windowInterface->getHWND();
            if (hParentWnd)
                ::SetMenu(hParentWnd, nullptr);
        }
        m_appMenu = nullptr;
    }

    static bool sendActionToFirstResponder(const std::string& action)
    {
        return false;
    }

    int getItemCountApi() const
    {
        return static_cast<int>(m_items.size());
    }

    void _appendeApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
    }

    void _insertApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        if (2 != args.Length())
            return;

        v8::Isolate* isolate = args.GetIsolate();
        if (!args[0]->IsUint32())
            return;
        if (!args[1]->IsObject())
            return;
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        size_t pos = args[0]->ToUint32(context).ToLocalChecked()->Value();
        if (pos > m_items.size())
            pos = m_items.size();

        v8::Object* v8Obj = v8::Object::Cast(*args[1]);
        v8::MaybeLocal<v8::Array> v8MaybeObjProps = v8Obj->GetOwnPropertyNames(isolate->GetCurrentContext());
        if (v8MaybeObjProps.IsEmpty())
            return;
        v8::Local<v8::Array> v8ObjProps = v8MaybeObjProps.ToLocalChecked();

        size_t size = v8ObjProps->Length();

        std::string label;
        std::string role;

        MenuItem* item = new MenuItem(isolate, this);

        for (size_t i = 0; i < size; ++i) {
            v8::MaybeLocal<v8::Value> keyNameValueMaybe = v8ObjProps->Get(context, i);
            if (keyNameValueMaybe.IsEmpty())
                continue;
            v8::Local<v8::Value> keyNameValue = keyNameValueMaybe.ToLocalChecked();
            v8::MaybeLocal<v8::Value> outValueMaybe = v8Obj->Get(context, keyNameValue);
            if (outValueMaybe.IsEmpty())
                continue;
            v8::Local<v8::Value> outValue = outValueMaybe.ToLocalChecked();

            std::string keyNameStr;
            if (!gin_helper::Converter<std::string>::FromV8(isolate, keyNameValue, &keyNameStr))
                return;

            std::string type;
            if ("type" == keyNameStr && outValue->IsString()) {
                v8::String::Utf8Value utf8(isolate, outValue->ToString(context).ToLocalChecked());
                type = *utf8;
            }
            if ("separator" == type)
                item->setType(MenuItem::SeparatorType);
            if ("checkbox" == type || "radio" == type)
                item->setType(MenuItem::CheckableActionType);

            if ("label" == keyNameStr && outValue->IsString()) {
                v8::String::Utf8Value utf8(isolate, outValue->ToString(context).ToLocalChecked());
                label = *utf8;
            }

            if ("role" == keyNameStr && outValue->IsString()) {
                v8::String::Utf8Value utf8(isolate, outValue->ToString(context).ToLocalChecked());
                role = *utf8;
            }

            if ("enabled" == keyNameStr && outValue->IsBoolean()) {
                item->setEnabled(outValue->ToBoolean(isolate)->Value());
            }

            if ("checked" == keyNameStr && outValue->IsBoolean()) {
                item->setChecked(outValue->ToBoolean(isolate)->Value());
            }

            if ("submenu" == keyNameStr) {
                Menu* subMenu = nullptr;
                if (!gin_helper::Converter<Menu*>::FromV8(isolate, outValue, &subMenu))
                    subMenu = nullptr;
                if (subMenu)
                    item->setSubMenu(subMenu);
            }

            if ("click" == keyNameStr && outValue->IsFunction()) {
                item->setClickCallback(outValue);
            }
        }

        if (label.empty())
            label = role;
        item->setLabel(label);

        m_items.insert(m_items.begin() + pos, item);
        m_isItemNeedRebuilt = true;
    }

    static void newFunction(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        if (args.IsConstructCall()) {
            new Menu(isolate, args.This());
            args.GetReturnValue().Set(args.This());
            return;
        }
    }

    void _popupApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        Menu* self = this;
        if (!m_hideWndHelp) {
            static const WCHAR kHideParentWindowClass[] = {
                'H', 'i', 'd', 'e', 'P', 'a', 'r', 'e', 'n', 't', 'W', 'i', 'n', 'd', 'o', 'w', 'C', 'l', 'a', 's', 's', 0
            };
            m_hideWndHelp = new HideWndHelp(kHideParentWindowClass,
                [self](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT { return self->hideWndProc(hWnd, uMsg, wParam, lParam); });
        }

        buildMenus(false);

        ::SetForegroundWindow(m_hideWndHelp->getWnd());

        POINT pt;
        if (args.Length() == 2 && args[0]->IsInt32() && args[1]->IsInt32()) {
            pt.x = args[0]->ToInt32(context).ToLocalChecked()->Value();
            pt.y = args[1]->ToInt32(context).ToLocalChecked()->Value();
        } else {
            ::GetCursorPos(&pt);
        }
        ::TrackPopupMenu(m_hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hideWndHelp->getWnd(), NULL);
    }

    bool dispatchCommandForTestingApi(int index)
    {
        if (index < 0 || static_cast<size_t>(index) >= m_items.size())
            return false;
        if (!m_hMenu && !buildMenus(false))
            return false;
        MenuItem* item = m_items[index];
        if (!item)
            return false;
        MenuEventNotif::onMenuCommon(WM_COMMAND, MAKEWPARAM(item->getAction(), 0), 0);
        return true;
    }

    void _clearApi()
    {
        clear();
    }
    void clear()
    {
        if (m_hMenu)
            ::DestroyMenu(m_hMenu);
        m_hMenu = nullptr;
        for (size_t i = 0; i < m_items.size(); ++i) {
            MenuItem* it = m_items[i];
            it->clear();
            delete it;
        }
        m_items.clear();
    }

    void onCommon(MenuItem* item, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        MENUITEMINFOW info = { 0 };
        info.cbSize = sizeof(MENUITEMINFOW);
        info.fMask = MIIM_STATE; // information to get
        int index = findItemIndex(item);
        BOOL b = ::GetMenuItemInfoW(m_hMenu, (UINT)index, TRUE, &info);

        if (MenuItem::CheckableActionType == item->getType()) {
            if (item->getChecked()) {
                info.fState = MFS_UNCHECKED;
                b = ::SetMenuItemInfoW(m_hMenu, (UINT)index, TRUE, &info);
                item->setChecked(false);
            } else {
                info.fState |= MFS_CHECKED;
                b = ::SetMenuItemInfoW(m_hMenu, (UINT)index, TRUE, &info);
                item->setChecked(true);
            }
        }

        v8::Local<v8::Value> focusedWindow = v8::Null(isolate());
        item->getMenu()->mate::EventEmitter<Menu>::emit("click", item->getClickCallbackValue(), focusedWindow /*, focusedWebContents*/);
    }

    static Menu* getAppMenu()
    {
        return m_appMenu;
    }

public:
    static gin_helper::WrapperInfo kWrapperInfo;
    static v8::Persistent<v8::Function> constructor;
    static std::set<MenuItem*>* m_liveMenuItem;

    HideWndHelp* m_hideWndHelp;

    friend class MenuItem;

    HMENU buildMenus(bool isAppOrPopupMenu)
    {
        if (!m_isItemNeedRebuilt)
            return m_hMenu;
        m_isItemNeedRebuilt = false;

        AppOrPopupType appOrPopupMenuType = isAppOrPopupMenu ? kIsApp : kIsPopup;
        if (kNoInit == m_isAppOrPopupMenu)
            m_isAppOrPopupMenu = appOrPopupMenuType;
        else if (m_isAppOrPopupMenu != appOrPopupMenuType)
            return nullptr;

        if (m_hMenu)
            ::DestroyMenu(m_hMenu);
        m_hMenu = nullptr;

        return buildMenu(this, isAppOrPopupMenu);
    }

private:
    static HMENU buildMenu(Menu* menu, bool isAppOrPopupMenu)
    {
        size_t size = menu->m_items.size();
        if (0 == size)
            return nullptr;

        menu->m_hMenu = ((isAppOrPopupMenu) ? ::CreateMenu() : ::CreatePopupMenu());

        for (size_t i = 0; i < size; ++i) {
            MenuItem* item = menu->m_items[i];
            item->insertPlatformMenu(i, menu->m_hMenu);
        }
        return menu->m_hMenu;
    }

    int findItemIndex(MenuItem* item)
    {
        for (size_t i = 0; i < m_items.size(); ++i) {
            MenuItem* it = m_items[i];
            if (it == item)
                return i;
        }
        return -1;
    }

    LRESULT hideWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_COMMAND: {
            MenuEventNotif::onMenuCommon(uMsg, wParam, lParam);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }
        return 0;
    }

    HMENU m_hMenu;
    std::vector<MenuItem*> m_items;
    bool m_isItemNeedRebuilt;

    static Menu* m_appMenu;

    enum AppOrPopupType {
        kNoInit,
        kIsApp,
        kIsPopup,
    };
    AppOrPopupType m_isAppOrPopupMenu;
};

//////////////////////////////////////////////////////////////////////////

static int s_menuItemCount = 1;

MenuItem::MenuItem(v8::Isolate* isolate, Menu* menu)
{
    m_isolate = isolate;
    m_type = ActionType;
    m_isEnabled = true;
    m_isChecked = false;
    m_action = s_menuItemCount++;
    m_menu = menu;
    m_hSubMenu = nullptr;
    m_subMenu = nullptr;
    m_id = 0;
    Menu::m_liveMenuItem->insert(this);
}

MenuItem::~MenuItem()
{
    Menu::m_liveMenuItem->erase(this);
}

void MenuItem::setSubMenu(Menu* subMenu)
{
    HMENU hSubMenu = subMenu->m_hMenu;
    m_hSubMenu = hSubMenu;
    m_subMenu = subMenu;
    m_type = SubmenuType;
}

void MenuItem::insertPlatformMenu(size_t pos, HMENU hMenu) const
{
    int count = ::GetMenuItemCount(hMenu);
    if (count < 0 && (int)pos > count)
        return;

    MENUITEMINFOW info = { 0 };
    info.cbSize = sizeof(MENUITEMINFOW);

    if (m_type == SeparatorType) {
        info.fMask = MIIM_FTYPE;
        info.fType = MFT_SEPARATOR;
        ::InsertMenuItemW(hMenu, count, TRUE, &info);
        return;
    }

    info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
    info.fType = MFT_STRING;
    info.wID = m_action;

    if (m_type == SubmenuType) {
        info.fMask |= MIIM_SUBMENU;
        info.hSubMenu = Menu::buildMenu(m_subMenu, false); // m_hSubMenu;
    }

    std::u16string labelW = base::UTF8ToUTF16(m_label);
    if (!labelW.empty()) {
        info.fMask |= MIIM_STRING;
        info.cch = labelW.size();
        info.dwTypeData = (LPWSTR)(labelW.c_str());
    }

    info.fState |= m_isEnabled ? MFS_ENABLED : MFS_DISABLED;
    if (CheckableActionType == m_type)
        info.fState |= m_isChecked ? MFS_CHECKED : MFS_UNCHECKED;
    ::InsertMenuItemW(hMenu, count, TRUE, &info);
}

void MenuItem::clear()
{
    if (m_hSubMenu)
        ::DestroyMenu(m_hSubMenu);
    m_hSubMenu = nullptr;
    if (m_subMenu)
        m_subMenu->clear();
    m_subMenu = nullptr;
}

void MenuEventNotif::onWindowDidCreated(WindowInterface* window)
{
    HWND hParentWnd = window->getHWND();
    if (Menu::getAppMenu()) {
        HMENU hmenuBar = Menu::getAppMenu()->buildMenus(true);
        ::SetMenu(hParentWnd, hmenuBar);
    }
}

void MenuEventNotif::onMenuCommon(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT menuID = LOWORD(wParam);
    for (std::set<MenuItem*>::const_iterator it = Menu::m_liveMenuItem->begin(); it != Menu::m_liveMenuItem->end(); ++it) {
        MenuItem* item = *it;
        if (item->getAction() == menuID) {
            item->getMenu()->onCommon(item, uMsg, wParam, lParam);
            return;
        }
    }
}

v8::Persistent<v8::Function> Menu::constructor;
gin_helper::WrapperInfo Menu::kWrapperInfo = { gin_helper::GinEmbedder::kEmbedderNativeGin };
std::set<MenuItem*>* Menu::m_liveMenuItem = nullptr;

Menu* Menu::m_appMenu = nullptr;

static void initializeMenuApi(v8::Local<v8::Object> target, v8::Local<v8::Value> unused, v8::Local<v8::Context> context, const NodeNative* native)
{
    Menu::init(context->GetIsolate(), target);
}

static const char BrowserMenuNative[] = "exports = function {};";

static NodeNative nativeBrowserMenuNative { "Menu", BrowserMenuNative, sizeof(BrowserMenuNative) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_browser_menu, initializeMenuApi, &nativeBrowserMenuNative)

}
