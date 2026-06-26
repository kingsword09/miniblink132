#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/api/EventEmitter.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/wrappable.h"
#include "third_party/libnode/src/node_binding.h"

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"

#include <CoreFoundation/CoreFoundation.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <map>
#include <memory>
#include <string>

namespace atom {

namespace {

struct NodeNative {
    const char* name;
    const char* source;
    size_t sourceLen;
};

std::string filePathToUTF8(const base::FilePath& path)
{
    return path.AsUTF8Unsafe();
}

std::string pathForKey(int key)
{
    base::FilePath path;
    if (!base::PathService::Get(key, &path))
        return std::string();
    return filePathToUTF8(path);
}

std::string pathForKeyOrFallback(int key, const std::string& fallback)
{
    std::string path = pathForKey(key);
    return path.empty() ? fallback : path;
}

std::string homePath()
{
    std::string path = pathForKey(base::DIR_HOME);
    if (!path.empty())
        return path;
    const char* home = getenv("HOME");
    return home ? std::string(home) : std::string("/tmp");
}

std::string homeChildPath(const char* child)
{
    return homePath() + child;
}

std::string executablePath()
{
    return pathForKey(base::FILE_EXE);
}

std::string modulePath()
{
    return executablePath();
}

std::string lockPath()
{
    std::string exe = executablePath();
    std::string hashInput = exe.empty() ? "miniblink-electron-app" : exe;
    std::hash<std::string> hasher;
    return std::string("/tmp/miniblink-electron-app-") + std::to_string(hasher(hashInput)) + ".lock";
}

std::string currentLocale()
{
    CFLocaleRef locale = CFLocaleCopyCurrent();
    if (!locale)
        return "en-US";

    CFStringRef identifier = CFLocaleGetIdentifier(locale);
    std::string result = "en-US";
    if (identifier) {
        char buffer[128] = { 0 };
        if (CFStringGetCString(identifier, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
            result = buffer;
            for (char& ch : result) {
                if (ch == '_')
                    ch = '-';
            }
        }
    }
    CFRelease(locale);
    return result;
}

bool canOverridePath(const std::string& name)
{
    return name == "appData"
        || name == "cache"
        || name == "crashDumps"
        || name == "desktop"
        || name == "documents"
        || name == "downloads"
        || name == "music"
        || name == "pictures"
        || name == "recent"
        || name == "temp"
        || name == "userCache"
        || name == "userData"
        || name == "userDesktop"
        || name == "videos"
        || name == "pepperFlashSystemPlugin";
}

} // namespace

class App : public mate::EventEmitter<App> {
public:
    explicit App(v8::Isolate* isolate, v8::Local<v8::Object> wrapper)
    {
        gin_helper::Wrappable<App>::InitWith(isolate, wrapper);
        instance_ = this;
    }

    ~App()
    {
        releaseSingleInstanceApi();
        if (instance_ == this)
            instance_ = nullptr;
    }

    static void init(v8::Local<v8::Object> target, v8::Isolate* isolate)
    {
        v8::Local<v8::FunctionTemplate> prototype = v8::FunctionTemplate::New(isolate, newFunction);
        prototype->SetClassName(v8::String::NewFromUtf8(isolate, "App").ToLocalChecked());

        gin_helper::ObjectTemplateBuilder builder(isolate, prototype->InstanceTemplate());
        builder.SetMethod("quit", &App::quitApi);
        builder.SetMethod("exit", &App::exitApi);
        builder.SetMethod("focus", &App::focusApi);
        builder.SetMethod("getVersion", &App::getVersionApi);
        builder.SetMethod("setVersion", &App::setVersionApi);
        builder.SetMethod("getName", &App::getNameApi);
        builder.SetMethod("setName", &App::setNameApi);
        builder.SetMethod("isReady", &App::isReadyApi);
        builder.SetProperty("isPackaged", &App::isPackagedApi);
        builder.SetMethod("_setAppPath", &App::_setAppPathApi);
        builder.SetMethod("_setIsReady", &App::_setIsReadyApi);
        builder.SetMethod("isOnline", &App::isOnlineApi);
        builder.SetMethod("addRecentDocument", &App::addRecentDocumentApi);
        builder.SetMethod("clearRecentDocuments", &App::clearRecentDocumentsApi);
        builder.SetMethod("setAppUserModelId", &App::setAppUserModelIdApi);
        builder.SetMethod("requestSingleInstanceLock", &App::requestSingleInstanceLockApi);
        builder.SetMethod("isDefaultProtocolClient", &App::isDefaultProtocolClientApi);
        builder.SetMethod("setAsDefaultProtocolClient", &App::setAsDefaultProtocolClientApi);
        builder.SetMethod("removeAsDefaultProtocolClient", &App::removeAsDefaultProtocolClientApi);
        builder.SetMethod("setBadgeCount", &App::setBadgeCountApi);
        builder.SetMethod("getBadgeCount", &App::getBadgeCountApi);
        builder.SetMethod("getLoginItemSettings", &App::getLoginItemSettingsApi);
        builder.SetMethod("setLoginItemSettings", &App::setLoginItemSettingsApi);
        builder.SetMethod("setUserTasks", &App::setUserTasksApi);
        builder.SetMethod("getJumpListSettings", &App::getJumpListSettingsApi);
        builder.SetMethod("setJumpList", &App::setJumpListApi);
        builder.SetMethod("setPath", &App::setPathApi);
        builder.SetMethod("getPath", &App::getPathApi);
        builder.SetMethod("setDesktopName", &App::setDesktopNameApi);
        builder.SetMethod("getLocale", &App::getLocaleApi);
        builder.SetMethod("makeSingleInstanceImpl", &App::makeSingleInstanceImplApi);
        builder.SetMethod("releaseSingleInstance", &App::releaseSingleInstanceApi);
        builder.SetMethod("_relaunch", &App::relaunchApi);
        builder.SetMethod("isAccessibilitySupportEnabled", &App::isAccessibilitySupportEnabledApi);
        builder.SetMethod("disableHardwareAcceleration", &App::disableHardwareAccelerationApi);
        builder.SetMethod("getFileIcon", &App::getFileIconApi);

        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Function> ctor = prototype->GetFunction(context).ToLocalChecked();
        constructor.Reset(isolate, ctor);
        target->Set(context, v8::String::NewFromUtf8(isolate, "App").ToLocalChecked(), ctor).ToChecked();
    }

    static void newFunction(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        if (!args.IsConstructCall())
            return;
        new App(args.GetIsolate(), args.This());
        args.GetReturnValue().Set(args.This());
    }

    void quitApi()
    {
        if (isQuitting_)
            return;
        isQuitting_ = true;
        emit("before-quit");
        emit("window-all-closed");
        emit("quit", exitCode_);
    }

    void exitApi(int code = 0)
    {
        _exit(code);
    }

    void focusApi() { }
    bool isReadyApi() const { return isReady_; }
    void _setIsReadyApi() { isReady_ = true; }
    bool isPackagedApi() const { return true; }
    void _setAppPathApi(const std::string& path) { appPath_ = path; }
    bool isOnlineApi() const { return true; }
    void addRecentDocumentApi(const std::string&) { }
    void clearRecentDocumentsApi() { }
    void setAppUserModelIdApi(const std::string&) { }
    bool isDefaultProtocolClientApi(const v8::FunctionCallbackInfo<v8::Value>&) { return true; }
    bool setAsDefaultProtocolClientApi(const v8::FunctionCallbackInfo<v8::Value>& args) { return args.Length() > 0; }
    bool removeAsDefaultProtocolClientApi(const v8::FunctionCallbackInfo<v8::Value>&) { return true; }
    bool setBadgeCountApi(int) { return false; }
    int getBadgeCountApi() const { return 0; }
    void setLoginItemSettingsApi(const v8::FunctionCallbackInfo<v8::Value>&) { }
    bool setUserTasksApi(const v8::FunctionCallbackInfo<v8::Value>&) { return true; }
    void setDesktopNameApi(const std::string&) { }
    void setJumpListApi(const v8::FunctionCallbackInfo<v8::Value>&) { }
    void relaunchApi(const v8::FunctionCallbackInfo<v8::Value>&) { }
    bool isAccessibilitySupportEnabledApi() const { return false; }
    void disableHardwareAccelerationApi() { }
    void getFileIconApi(const v8::FunctionCallbackInfo<v8::Value>& args) { args.GetReturnValue().Set(v8::Undefined(args.GetIsolate())); }

    std::string getVersionApi() const { return version_; }
    void setVersionApi(const std::string& version) { version_ = version; }
    std::string getNameApi() const { return name_; }
    void setNameApi(const std::string& name) { name_ = name; }
    std::string getLocaleApi() const { return currentLocale(); }

    void setPathApi(const std::string& name, const std::string& path)
    {
        if (canOverridePath(name))
            paths_[name] = path;
    }

    std::string getPathApi(const std::string& name) const
    {
        auto it = paths_.find(name);
        if (it != paths_.end())
            return it->second;

        if (name == "home")
            return homePath();
        if (name == "temp" || name == "crashDumps")
            return pathForKeyOrFallback(base::DIR_TEMP, "/tmp");
        if (name == "desktop" || name == "userDesktop")
            return pathForKeyOrFallback(base::DIR_USER_DESKTOP, homeChildPath("/Desktop"));
        if (name == "documents")
            return homeChildPath("/Documents");
        if (name == "downloads")
            return homeChildPath("/Downloads");
        if (name == "music")
            return homeChildPath("/Music");
        if (name == "pictures")
            return homeChildPath("/Pictures");
        if (name == "videos")
            return homeChildPath("/Movies");
        if (name == "appData" || name == "userData")
            return pathForKeyOrFallback(base::DIR_APP_DATA, homeChildPath("/Library/Application Support"));
        if (name == "cache" || name == "userCache")
            return pathForKeyOrFallback(base::DIR_CACHE, homeChildPath("/Library/Caches"));
        if (name == "recent")
            return homeChildPath("/Documents");
        if (name == "exe")
            return executablePath();
        if (name == "module")
            return modulePath();
        return std::string();
    }

    bool requestSingleInstanceLockApi()
    {
        if (singleInstanceFd_ >= 0)
            return true;

        singleInstancePath_ = lockPath();
        singleInstanceFd_ = open(singleInstancePath_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (singleInstanceFd_ < 0)
            return errno == EEXIST ? false : false;

        std::string pid = std::to_string(static_cast<long long>(getpid()));
        write(singleInstanceFd_, pid.c_str(), pid.size());
        return true;
    }

    bool makeSingleInstanceImplApi(const v8::FunctionCallbackInfo<v8::Value>&)
    {
        return !requestSingleInstanceLockApi();
    }

    void releaseSingleInstanceApi()
    {
        if (singleInstanceFd_ >= 0) {
            close(singleInstanceFd_);
            singleInstanceFd_ = -1;
            if (!singleInstancePath_.empty())
                unlink(singleInstancePath_.c_str());
            singleInstancePath_.clear();
        }
    }

    v8::Local<v8::Value> getLoginItemSettingsApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> result = v8::Object::New(isolate);
        result->Set(context, v8::String::NewFromUtf8(isolate, "openAtLogin").ToLocalChecked(), v8::False(isolate)).ToChecked();
        result->Set(context, v8::String::NewFromUtf8(isolate, "openAsHidden").ToLocalChecked(), v8::False(isolate)).ToChecked();
        result->Set(context, v8::String::NewFromUtf8(isolate, "restoreState").ToLocalChecked(), v8::False(isolate)).ToChecked();
        result->Set(context, v8::String::NewFromUtf8(isolate, "wasOpenedAtLogin").ToLocalChecked(), v8::False(isolate)).ToChecked();
        result->Set(context, v8::String::NewFromUtf8(isolate, "wasOpenedAsHidden").ToLocalChecked(), v8::False(isolate)).ToChecked();
        return result;
    }

    v8::Local<v8::Value> getJumpListSettingsApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> result = v8::Object::New(isolate);
        result->Set(context, v8::String::NewFromUtf8(isolate, "minItems").ToLocalChecked(), v8::Integer::New(isolate, 0)).ToChecked();
        result->Set(context, v8::String::NewFromUtf8(isolate, "removedItems").ToLocalChecked(), v8::Array::New(isolate)).ToChecked();
        return result;
    }

    bool exitCalledForTesting() const { return exitCalled_; }
    bool isQuittingForTesting() const { return isQuitting_; }

    static gin_helper::WrapperInfo kWrapperInfo;
    static v8::Persistent<v8::Function> constructor;

private:
    static App* instance_;
    bool isReady_ = false;
    bool isQuitting_ = false;
    bool exitCalled_ = false;
    int exitCode_ = 0;
    int singleInstanceFd_ = -1;
    std::string singleInstancePath_;
    std::string version_ = "1.3.3";
    std::string name_;
    std::string appPath_;
    std::map<std::string, std::string> paths_;
};

App* App::instance_ = nullptr;
gin_helper::WrapperInfo App::kWrapperInfo = { gin::kEmbedderNativeGin };
v8::Persistent<v8::Function> App::constructor;

static void initializeAppApi(v8::Local<v8::Object> target, v8::Local<v8::Value>, v8::Local<v8::Context> context, const NodeNative*)
{
    App::init(target, context->GetIsolate());
}

static const char BrowserAppNative[] = "exports = {};";
static NodeNative nativeBrowserAppNative { "App", BrowserAppNative, sizeof(BrowserAppNative) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_browser_app, initializeAppApi, &nativeBrowserAppNative)

} // namespace atom
