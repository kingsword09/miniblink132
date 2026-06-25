#include "electron/common/NodeRegisterHelp.h"
#include "third_party/libnode/src/node_binding.h"
#include "v8/include/v8.h"

#include <windows.h>

#include <ctype.h>
#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

struct NodeNative {
    const char* name;
    const char* source;
    size_t sourceLen;
};

struct Accelerator {
    UINT modifiers = 0;
    UINT vk = 0;
};

std::vector<std::string> splitAccelerator(const std::string& accelerator)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= accelerator.size()) {
        size_t separator = accelerator.find('+', start);
        std::string part = accelerator.substr(start, separator == std::string::npos ? separator : separator - start);
        if (part.empty())
            return {};
        parts.push_back(part);
        if (separator == std::string::npos)
            break;
        start = separator + 1;
    }
    return parts;
}

bool parseFunctionKey(const std::string& part, UINT* vk)
{
    if (part.size() < 2 || part.size() > 3 || part[0] != 'F')
        return false;

    int number = 0;
    for (size_t i = 1; i < part.size(); ++i) {
        if (part[i] < '0' || part[i] > '9')
            return false;
        number = number * 10 + (part[i] - '0');
    }

    if (number < 1 || number > 24)
        return false;

    *vk = VK_F1 + number - 1;
    return true;
}

bool parseKey(const std::string& part, UINT* vk)
{
    if (part.size() == 1) {
        unsigned char ch = static_cast<unsigned char>(part[0]);
        if (('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z')) {
            *vk = static_cast<UINT>(toupper(ch));
            return true;
        }
        if ('0' <= ch && ch <= '9') {
            *vk = static_cast<UINT>(ch);
            return true;
        }
    }

    if (parseFunctionKey(part, vk))
        return true;
    if (part == "Escape") {
        *vk = VK_ESCAPE;
        return true;
    }
    if (part == "Enter") {
        *vk = VK_RETURN;
        return true;
    }
    if (part == "Space") {
        *vk = VK_SPACE;
        return true;
    }
    if (part == "Plus") {
        *vk = VK_OEM_PLUS;
        return true;
    }
    return false;
}

bool parseAccelerator(const std::string& accelerator, Accelerator* parsed)
{
    std::vector<std::string> parts = splitAccelerator(accelerator);
    if (parts.empty())
        return false;

    UINT modifiers = 0;
    UINT vk = 0;
    for (const std::string& part : parts) {
        UINT modifier = 0;
        if (part == "Alt")
            modifier = MOD_ALT;
        else if (part == "Control")
            modifier = MOD_CONTROL;
        else if (part == "Shift")
            modifier = MOD_SHIFT;
        else if (part == "Command" || part == "Super")
            modifier = MOD_WIN;

        if (modifier) {
            if (modifiers & modifier)
                return false;
            modifiers |= modifier;
            continue;
        }

        if (vk || !parseKey(part, &vk))
            return false;
    }

    if (!vk)
        return false;

    parsed->modifiers = modifiers;
    parsed->vk = vk;
    return true;
}

bool stringArgument(v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out)
{
    if (!value->IsString())
        return false;

    v8::String::Utf8Value utf8(isolate, value);
    if (!*utf8)
        return false;

    out->assign(*utf8, utf8.length());
    return true;
}

class GlobalShortcutState {
public:
    explicit GlobalShortcutState(v8::Isolate* isolate)
        : isolate_(isolate)
    {
    }

    ~GlobalShortcutState()
    {
        unregisterAll();
        if (window_)
            DestroyWindow(window_);
    }

    bool registerShortcut(const std::string& accelerator, v8::Local<v8::Function> callback);
    void unregisterShortcut(const std::string& accelerator);
    void unregisterAll();
    void dispatch(int id);

private:
    struct Registration {
        int id = 0;
        std::string accelerator;
        v8::Global<v8::Context> context;
        v8::Global<v8::Function> callback;
    };

    bool ensureWindow();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    v8::Isolate* isolate_;
    HWND window_ = nullptr;
    int nextId_ = 9001;
    std::map<int, std::unique_ptr<Registration>> registrationsById_;
    std::map<std::string, int> idsByAccelerator_;
};

std::map<v8::Isolate*, std::unique_ptr<GlobalShortcutState>>& states()
{
    static std::map<v8::Isolate*, std::unique_ptr<GlobalShortcutState>>* stateMap = new std::map<v8::Isolate*, std::unique_ptr<GlobalShortcutState>>();
    return *stateMap;
}

GlobalShortcutState* stateForIsolate(v8::Isolate* isolate)
{
    auto& stateMap = states();
    auto it = stateMap.find(isolate);
    if (it != stateMap.end())
        return it->second.get();

    auto state = std::make_unique<GlobalShortcutState>(isolate);
    GlobalShortcutState* rawState = state.get();
    stateMap.emplace(isolate, std::move(state));
    return rawState;
}

LRESULT CALLBACK GlobalShortcutState::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    GlobalShortcutState* self = static_cast<GlobalShortcutState*>(GetPropW(hwnd, u"MiniElectronGlobalShortcutState"));
    if (!self && message == WM_CREATE) {
        LPCREATESTRUCTW create = reinterpret_cast<LPCREATESTRUCTW>(lParam);
        self = static_cast<GlobalShortcutState*>(create->lpCreateParams);
        SetPropW(hwnd, u"MiniElectronGlobalShortcutState", self);
        return 0;
    }

    if (message == WM_HOTKEY && self) {
        self->dispatch(static_cast<int>(wParam));
        return 0;
    }

    if (message == WM_NCDESTROY)
        RemovePropW(hwnd, u"MiniElectronGlobalShortcutState");

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool GlobalShortcutState::ensureWindow()
{
    if (window_)
        return true;

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = windowProc;
    wc.lpszClassName = u"MiniElectronGlobalShortcutWindow";
    RegisterClassW(&wc);
    window_ = CreateWindowExW(0, wc.lpszClassName, u"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, this);
    return window_ != nullptr;
}

bool GlobalShortcutState::registerShortcut(const std::string& accelerator, v8::Local<v8::Function> callback)
{
    if (idsByAccelerator_.find(accelerator) != idsByAccelerator_.end())
        return false;

    Accelerator parsed;
    if (!parseAccelerator(accelerator, &parsed) || !ensureWindow())
        return false;

    int id = nextId_++;
    if (!RegisterHotKey(window_, id, parsed.modifiers, parsed.vk))
        return false;

    auto registration = std::make_unique<Registration>();
    registration->id = id;
    registration->accelerator = accelerator;
    registration->context.Reset(isolate_, isolate_->GetCurrentContext());
    registration->callback.Reset(isolate_, callback);

    idsByAccelerator_[accelerator] = id;
    registrationsById_[id] = std::move(registration);
    return true;
}

void GlobalShortcutState::unregisterShortcut(const std::string& accelerator)
{
    auto acceleratorIt = idsByAccelerator_.find(accelerator);
    if (acceleratorIt == idsByAccelerator_.end())
        return;

    int id = acceleratorIt->second;
    idsByAccelerator_.erase(acceleratorIt);
    registrationsById_.erase(id);
    UnregisterHotKey(window_, id);
}

void GlobalShortcutState::unregisterAll()
{
    std::vector<int> ids;
    ids.reserve(registrationsById_.size());
    for (const auto& entry : registrationsById_)
        ids.push_back(entry.first);

    for (int id : ids)
        UnregisterHotKey(window_, id);

    registrationsById_.clear();
    idsByAccelerator_.clear();
}

void GlobalShortcutState::dispatch(int id)
{
    auto it = registrationsById_.find(id);
    if (it == registrationsById_.end())
        return;

    v8::HandleScope handleScope(isolate_);
    v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate_, it->second->context);
    v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(isolate_, it->second->callback);
    v8::Context::Scope contextScope(context);
    v8::TryCatch tryCatch(isolate_);
    v8::Local<v8::Value> ignored;
    [[maybe_unused]] bool didCall = callback->Call(context, v8::Undefined(isolate_), 0, nullptr).ToLocal(&ignored);
}

void registerApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 2 || !info[1]->IsFunction()) {
        info.GetReturnValue().Set(false);
        return;
    }

    std::string accelerator;
    if (!stringArgument(isolate, info[0], &accelerator)) {
        info.GetReturnValue().Set(false);
        return;
    }

    bool didRegister = stateForIsolate(isolate)->registerShortcut(accelerator, v8::Local<v8::Function>::Cast(info[1]));
    info.GetReturnValue().Set(didRegister);
}

void unregisterApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    std::string accelerator;
    if (info.Length() >= 1 && stringArgument(isolate, info[0], &accelerator))
        stateForIsolate(isolate)->unregisterShortcut(accelerator);
}

void unregisterAllApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    stateForIsolate(info.GetIsolate())->unregisterAll();
}

void initializeGlobalShortcutApi(v8::Local<v8::Object> exports, v8::Local<v8::Value>, v8::Local<v8::Context> context, const NodeNative*)
{
    v8::Isolate* isolate = context->GetIsolate();

    exports->Set(context,
        v8::String::NewFromUtf8(isolate, "register").ToLocalChecked(),
        v8::FunctionTemplate::New(isolate, registerApi)->GetFunction(context).ToLocalChecked()).ToChecked();

    exports->Set(context,
        v8::String::NewFromUtf8(isolate, "unregister").ToLocalChecked(),
        v8::FunctionTemplate::New(isolate, unregisterApi)->GetFunction(context).ToLocalChecked()).ToChecked();

    exports->Set(context,
        v8::String::NewFromUtf8(isolate, "unregisterAll").ToLocalChecked(),
        v8::FunctionTemplate::New(isolate, unregisterAllApi)->GetFunction(context).ToLocalChecked()).ToChecked();
}

const char GlobalShortcutScript[] = "exports = {};";
NodeNative nativeGlobalShortcutNative { "ApiGlobalShortcut", GlobalShortcutScript, sizeof(GlobalShortcutScript) - 1 };

} // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_browser_global_shortcut, initializeGlobalShortcutApi, &nativeGlobalShortcutNative)
