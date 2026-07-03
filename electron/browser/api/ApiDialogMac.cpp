#include "electron/nodeblink.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/promise.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "electron/common/gin_helper/wrappable.h"

#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libuv/include/uv.h"

#include <commdlg.h>
#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace atom {

namespace {

std::string getString(const base::Value::Dict& options, const char* key)
{
    const std::string* value = options.FindString(key);
    return value ? *value : std::string();
}

const base::Value::List* getList(const base::Value::Dict& options, const char* key)
{
    return options.FindList(key);
}

int messageBoxTypeForOptions(const base::Value::Dict& options)
{
    std::string type = getString(options, "type");
    UINT flags = MB_ICONINFORMATION;
    if (type == "error")
        flags = MB_ICONERROR;
    else if (type == "question")
        flags = MB_ICONQUESTION;
    else if (type == "warning")
        flags = MB_ICONWARNING;

    const base::Value::List* buttons = getList(options, "buttons");
    size_t buttonCount = buttons ? buttons->size() : 0;
    if (buttonCount <= 1)
        flags |= MB_OK;
    else if (buttonCount == 2)
        flags |= MB_YESNO;
    else
        flags |= MB_YESNOCANCEL;
    return flags;
}

int responseIndexFromMessageBoxResult(int result, const base::Value::Dict& options)
{
    const base::Value::List* buttons = getList(options, "buttons");
    size_t buttonCount = buttons ? buttons->size() : 0;
    if (buttonCount <= 1)
        return 0;
    if (buttonCount == 2)
        return result == IDYES ? 0 : 1;
    if (result == IDYES)
        return 0;
    if (result == IDNO)
        return 1;
    return 2;
}

std::u16string optionText(const base::Value::Dict& options, const char* key)
{
    return base::UTF8ToUTF16(getString(options, key));
}

std::u16string messageText(const base::Value::Dict& options)
{
    std::string message = getString(options, "message");
    std::string detail = getString(options, "detail");
    if (!detail.empty()) {
        if (!message.empty())
            message += " ";
        message += detail;
    }
    return base::UTF8ToUTF16(message);
}

base::Value::Dict showMessageBoxResult(const base::Value::Dict& options)
{
    std::u16string title = optionText(options, "title");
    std::u16string message = messageText(options);
    int result = ::MessageBoxW(nullptr,
        reinterpret_cast<LPCWSTR>(message.c_str()),
        reinterpret_cast<LPCWSTR>(title.c_str()),
        messageBoxTypeForOptions(options));

    base::Value::Dict response;
    response.Set("response", responseIndexFromMessageBoxResult(result, options));
    response.Set("checkboxChecked", false);
    return response;
}

bool hasProperty(const base::Value::Dict& options, const char* name)
{
    const base::Value::List* properties = getList(options, "properties");
    if (!properties)
        return false;
    for (const base::Value& property : *properties) {
        if (property.is_string() && property.GetString() == name)
            return true;
    }
    return false;
}

std::vector<WCHAR> fileDialogFilter(const base::Value::Dict& options)
{
    std::vector<WCHAR> filter;
    const base::Value::List* filters = getList(options, "filters");
    if (!filters) {
        filter.push_back(0);
        filter.push_back(0);
        return filter;
    }

    for (const base::Value& filterValue : *filters) {
        if (!filterValue.is_dict())
            continue;
        const base::Value::Dict& filterDict = filterValue.GetDict();
        const std::string* name = filterDict.FindString("name");
        const base::Value::List* extensions = filterDict.FindList("extensions");
        if (!name || !extensions)
            continue;

        std::u16string label = base::UTF8ToUTF16(*name);
        filter.insert(filter.end(), label.begin(), label.end());
        filter.push_back(0);

        bool hasExtension = false;
        for (const base::Value& extensionValue : *extensions) {
            if (!extensionValue.is_string())
                continue;
            std::string extension = extensionValue.GetString();
            if (extension.empty() || extension.find('.') != std::string::npos)
                continue;
            if (hasExtension)
                filter.push_back(';');
            std::u16string pattern = extension == "*" ? u"*" : base::UTF8ToUTF16("*." + extension);
            filter.insert(filter.end(), pattern.begin(), pattern.end());
            hasExtension = true;
        }
        if (!hasExtension)
            filter.push_back('*');
        filter.push_back(0);
    }

    if (filter.empty())
        filter.push_back(0);
    filter.push_back(0);
    return filter;
}

std::string utf16PathToUTF8(const WCHAR* value)
{
    if (!value)
        return std::string();
    std::u16string text;
    while (*value) {
        text.push_back(static_cast<char16_t>(*value));
        ++value;
    }
    return base::UTF16ToUTF8(text);
}

base::Value::List splitFileDialogResult(const std::vector<WCHAR>& fileResult)
{
    base::Value::List paths;
    std::vector<std::u16string> parts;
    const WCHAR* cursor = fileResult.data();
    while (cursor && *cursor) {
        std::u16string part;
        while (*cursor) {
            part.push_back(static_cast<char16_t>(*cursor));
            ++cursor;
        }
        parts.push_back(part);
        ++cursor;
    }

    if (parts.empty())
        return paths;
    if (parts.size() == 1) {
        paths.Append(base::UTF16ToUTF8(parts[0]));
        return paths;
    }

    std::u16string root = parts[0];
    if (!root.empty() && root.back() != '/' && root.back() != '\\')
        root.push_back('/');
    for (size_t i = 1; i < parts.size(); ++i)
        paths.Append(base::UTF16ToUTF8(root + parts[i]));
    return paths;
}

bool showFolderDialog(const base::Value::Dict& options, base::Value::List* paths)
{
    std::u16string title = optionText(options, "title");
    std::u16string defaultPath = optionText(options, "defaultPath");
    WCHAR displayName[MAX_PATH] = {};
    BROWSEINFOW browseInfo = {};
    browseInfo.pszDisplayName = displayName;
    browseInfo.lpszTitle = reinterpret_cast<LPCWSTR>(title.c_str());
    browseInfo.lParam = reinterpret_cast<LPARAM>(defaultPath.c_str());
    browseInfo.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;

    LPITEMIDLIST item = SHBrowseForFolderW(&browseInfo);
    if (!item)
        return false;

    WCHAR path[MAX_PATH] = {};
    BOOL ok = SHGetPathFromIDListW(item, path);
    CoTaskMemFree(item);
    if (!ok)
        return false;

    paths->Append(utf16PathToUTF8(path));
    return true;
}

bool showOpenOrSaveDialog(bool openDialog, const base::Value::Dict& options, base::Value::List* paths)
{
    if (openDialog && hasProperty(options, "openDirectory"))
        return showFolderDialog(options, paths);

    std::u16string title = optionText(options, "title");
    std::u16string defaultPath = optionText(options, "defaultPath");
    std::vector<WCHAR> filter = fileDialogFilter(options);
    std::vector<WCHAR> fileResult(4 * MAX_PATH + 1);
    if (!defaultPath.empty()) {
        size_t length = std::min(defaultPath.size(), fileResult.size() - 1);
        for (size_t i = 0; i < length; ++i)
            fileResult[i] = static_cast<WCHAR>(defaultPath[i]);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter.data();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = fileResult.data();
    ofn.nMaxFile = static_cast<DWORD>(fileResult.size());
    ofn.lpstrInitialDir = reinterpret_cast<LPCWSTR>(defaultPath.c_str());
    ofn.lpstrTitle = reinterpret_cast<LPCWSTR>(title.c_str());
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (openDialog)
        ofn.Flags |= OFN_FILEMUSTEXIST;
    if (openDialog && hasProperty(options, "multiSelections"))
        ofn.Flags |= OFN_ALLOWMULTISELECT;

    BOOL ok = openDialog ? GetOpenFileNameW(&ofn) : GetSaveFileNameW(&ofn);
    if (!ok && CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
        WORD required = *reinterpret_cast<WORD*>(fileResult.data());
        fileResult.assign(static_cast<size_t>(required) + 2, 0);
        ofn.lpstrFile = fileResult.data();
        ofn.nMaxFile = required;
        ok = openDialog ? GetOpenFileNameW(&ofn) : GetSaveFileNameW(&ofn);
    }
    if (!ok)
        return false;

    *paths = splitFileDialogResult(fileResult);
    return !paths->empty();
}

v8::Local<v8::Object> createObject(v8::Isolate* isolate)
{
    return v8::Object::New(isolate);
}

void setValue(v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char* key, v8::Local<v8::Value> value)
{
    object->Set(context, v8::String::NewFromUtf8(context->GetIsolate(), key).ToLocalChecked(), value).ToChecked();
}

void callCallback(v8::Local<v8::Context> context, v8::Local<v8::Value> callback, v8::Local<v8::Value>* argv, int argc)
{
    if (!callback->IsFunction())
        return;
    callback.As<v8::Function>()->Call(context, context->Global(), argc, argv).ToLocalChecked();
}

v8::Local<v8::Promise> resolvedPromise(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
    resolver->Resolve(context, value).ToChecked();
    return resolver->GetPromise();
}

base::Value::Dict optionsFromArg(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    base::Value::Dict options;
    if (!value.IsEmpty() && value->IsObject())
        gin_helper::Converter<base::Value::Dict>::FromV8(isolate, value, &options);
    return options;
}

} // namespace

class Dialog : public gin_helper::Wrappable<Dialog> {
public:
    Dialog(v8::Isolate* isolate, v8::Local<v8::Object> wrapper)
    {
        gin_helper::Wrappable<Dialog>::InitWith(isolate, wrapper);
    }

    static void init(v8::Isolate* isolate, v8::Local<v8::Object> target)
    {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::FunctionTemplate> prototype = v8::FunctionTemplate::New(isolate, newFunction);
        prototype->SetClassName(v8::String::NewFromUtf8(isolate, "Dialog").ToLocalChecked());

        gin_helper::ObjectTemplateBuilder builder(isolate, prototype->InstanceTemplate());
        builder.SetMethod("_showOpenDialog", &Dialog::showOpenDialogApi);
        builder.SetMethod("_showSaveDialog", &Dialog::showSaveDialogApi);
        builder.SetMethod("_showOpenDialogSync", &Dialog::showOpenDialogSyncApi);
        builder.SetMethod("_showSaveDialogSync", &Dialog::showSaveDialogSyncApi);
        builder.SetMethod("_showMessageBox", &Dialog::showMessageBoxApi);
        builder.SetMethod("_showMessageBoxSync", &Dialog::showMessageBoxSyncApi);
        builder.SetMethod("_showErrorBox", &Dialog::showErrorBoxApi);

        v8::Local<v8::Function> constructor = prototype->GetFunction(context).ToLocalChecked();
        target->Set(context, v8::String::NewFromUtf8(isolate, "Dialog").ToLocalChecked(), constructor).ToChecked();
    }

    static void newFunction(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (!info.IsConstructCall())
            return;
        new Dialog(info.GetIsolate(), info.This());
        info.GetReturnValue().Set(info.This());
    }

    void showOpenDialogSyncApi(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        showOpenOrSaveDialogSyncApi(true, info);
    }

    void showSaveDialogSyncApi(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        showOpenOrSaveDialogSyncApi(false, info);
    }

    void showOpenDialogApi(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        showOpenOrSaveDialogAsyncApi(true, info);
    }

    void showSaveDialogApi(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        showOpenOrSaveDialogAsyncApi(false, info);
    }

    void showMessageBoxSyncApi(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        base::Value::Dict options = info.Length() > 1 ? optionsFromArg(isolate, info[1]) : base::Value::Dict();
        base::Value::Dict response = showMessageBoxResult(options);
        info.GetReturnValue().Set(v8::Integer::New(isolate, *response.FindInt("response")));
    }

    void showMessageBoxApi(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        base::Value::Dict options = info.Length() > 1 ? optionsFromArg(isolate, info[1]) : base::Value::Dict();
        v8::Local<v8::Value> result = gin_helper::Converter<base::Value::Dict>::ToV8(isolate, showMessageBoxResult(options));
        info.GetReturnValue().Set(resolvedPromise(isolate, result));
    }

    void showErrorBoxApi(const std::string& title, const std::string& content)
    {
        std::u16string titleW = base::UTF8ToUTF16(title);
        std::u16string contentW = base::UTF8ToUTF16(content);
        ::MessageBoxW(nullptr,
            reinterpret_cast<LPCWSTR>(contentW.c_str()),
            reinterpret_cast<LPCWSTR>(titleW.c_str()),
            MB_OK | MB_ICONERROR);
    }

    static gin_helper::WrapperInfo kWrapperInfo;

private:
    void showOpenOrSaveDialogSyncApi(bool openDialog, const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        base::Value::Dict options = info.Length() > 1 ? optionsFromArg(isolate, info[1]) : base::Value::Dict();
        base::Value::List paths;
        if (!showOpenOrSaveDialog(openDialog, options, &paths))
            return;

        if (openDialog)
            info.GetReturnValue().Set(gin_helper::Converter<base::Value::List>::ToV8(isolate, paths));
        else if (!paths.empty())
            info.GetReturnValue().Set(gin_helper::ConvertToV8(isolate, paths[0].GetString()));
    }

    void showOpenOrSaveDialogAsyncApi(bool openDialog, const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        base::Value::Dict options = info.Length() > 1 ? optionsFromArg(isolate, info[1]) : base::Value::Dict();
        base::Value::List paths;
        bool ok = showOpenOrSaveDialog(openDialog, options, &paths);

        v8::Local<v8::Object> result = createObject(isolate);
        setValue(context, result, "canceled", v8::Boolean::New(isolate, !ok));
        if (openDialog) {
            setValue(context, result, "filePaths", gin_helper::Converter<base::Value::List>::ToV8(isolate, paths));
        } else {
            v8::Local<v8::Value> filePath = ok && !paths.empty()
                ? gin_helper::ConvertToV8(isolate, paths[0].GetString())
                : v8::Undefined(isolate).As<v8::Value>();
            setValue(context, result, "filePath", filePath);
        }

        if (info.Length() > 2 && info[2]->IsFunction()) {
            v8::Local<v8::Value> argv[2] = { v8::Boolean::New(isolate, !ok), gin_helper::Converter<base::Value::List>::ToV8(isolate, paths) };
            callCallback(context, info[2], argv, 2);
        }
        info.GetReturnValue().Set(resolvedPromise(isolate, result));
    }
};

gin_helper::WrapperInfo Dialog::kWrapperInfo = { gin_helper::GinEmbedder::kEmbedderNativeGin };

void initializeDialogApi(v8::Local<v8::Object> target, v8::Local<v8::Value>, v8::Local<v8::Context> context, const NodeNative*)
{
    Dialog::init(context->GetIsolate(), target);
}

const char BrowserDialogNative[] = "exports = {};";
NodeNative nativeBrowserDialogNative { "Dialog", BrowserDialogNative, sizeof(BrowserDialogNative) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_browser_dialog, initializeDialogApi, &nativeBrowserDialogNative)

} // namespace atom
