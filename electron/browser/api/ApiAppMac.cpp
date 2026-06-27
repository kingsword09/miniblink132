#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/AtomCommandLine.h"
#include "electron/common/api/EventEmitter.h"
#include "electron/common/api/EventEmitterCaller.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/wrappable.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libuv/include/uv.h"

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"

#include <CoreFoundation/CoreFoundation.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

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

uint64_t stableHashString(const std::string& input)
{
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : input) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string singleInstanceId()
{
    std::string exe = executablePath();
    std::string hashInput = exe.empty() ? "miniblink-electron-app" : exe;
    return std::to_string(static_cast<unsigned long long>(stableHashString(hashInput)));
}

std::string lockPath()
{
    return std::string("/tmp/miniblink-electron-app-") + singleInstanceId() + ".lock";
}

std::string socketPath()
{
    return std::string("/tmp/miniblink-electron-app-") + singleInstanceId() + ".sock";
}

std::string currentWorkingDirectory()
{
    char buffer[PATH_MAX] = { 0 };
    if (getcwd(buffer, sizeof(buffer)))
        return buffer;
    return homePath();
}

std::string makeSecondInstancePayload()
{
    base::Value::List payload;
    std::vector<std::string> args = atom::AtomCommandLine::argv();
    for (const std::string& arg : args)
        payload.Append(arg);
    payload.Append(currentWorkingDirectory());

    std::string json;
    base::JSONWriter::Write(payload, &json);
    return json;
}

bool writeAll(int fd, const void* data, size_t size)
{
    const char* cursor = static_cast<const char*>(data);
    while (size > 0) {
        ssize_t written = write(fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (written == 0)
            return false;
        cursor += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

bool notifyPrimaryInstance(const std::string& path)
{
    std::string payload = makeSecondInstancePayload();
    if (payload.empty() || payload.size() > UINT32_MAX)
        return false;

    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
        return false;
    memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    for (int attempt = 0; attempt < 50; ++attempt) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return false;

        bool ok = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
        if (ok) {
            uint32_t size = static_cast<uint32_t>(payload.size());
            ok = writeAll(fd, &size, sizeof(size)) && writeAll(fd, payload.data(), payload.size());
        }
        close(fd);
        if (ok)
            return true;
        usleep(10000);
    }
    return false;
}

bool processIsRunning(pid_t pid)
{
    if (pid <= 0)
        return false;
    if (kill(pid, 0) == 0)
        return true;
    return errno == EPERM;
}

bool lockOwnerIsRunning(const std::string& path)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;

    char buffer[64] = { 0 };
    ssize_t size = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (size <= 0)
        return false;

    char* end = nullptr;
    long pid = strtol(buffer, &end, 10);
    if (end == buffer)
        return false;
    return processIsRunning(static_cast<pid_t>(pid));
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
        builder.SetMethod("_getSingleInstanceFailureReasonForTesting", &App::getSingleInstanceFailureReasonForTestingApi);
        builder.SetMethod("_getSingleInstanceLockPathForTesting", &App::getSingleInstanceLockPathForTestingApi);
        builder.SetMethod("_getSingleInstanceSocketPathForTesting", &App::getSingleInstanceSocketPathForTestingApi);
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
        if (emit("before-quit")) {
            isQuitting_ = false;
            return;
        }
        emit("window-all-closed");
        emit("quit", exitCode_);
    }

    void exitApi(int code = 0)
    {
        exitCalled_ = true;
        exitCode_ = code;
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
    std::string getSingleInstanceFailureReasonForTestingApi() const { return singleInstanceFailureReason_; }

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

        singleInstanceFailureReason_.clear();
        singleInstancePath_ = lockPath();
        for (int attempt = 0; attempt < 2; ++attempt) {
            singleInstanceFd_ = open(singleInstancePath_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
            if (singleInstanceFd_ >= 0)
                break;

            int openErrno = errno;
            if (openErrno != EEXIST) {
                singleInstanceFailureReason_ = std::string("lock-open-failed:") + strerror(openErrno);
                return false;
            }

            if (notifyPrimaryInstance(socketPath())) {
                singleInstanceFailureReason_ = "already-running";
                return false;
            }

            if (lockOwnerIsRunning(singleInstancePath_)) {
                singleInstanceFailureReason_ = "already-running-notify-failed";
                return false;
            }

            if (attempt == 0) {
                unlink(singleInstancePath_.c_str());
                unlink(socketPath().c_str());
                continue;
            }

            singleInstanceFailureReason_ = "already-running-notify-failed";
            return false;
        }

        std::string pid = std::to_string(static_cast<long long>(getpid()));
        write(singleInstanceFd_, pid.c_str(), pid.size());
        if (!startSingleInstanceServer()) {
            if (singleInstanceFailureReason_.empty())
                singleInstanceFailureReason_ = "server-start-failed";
            releaseSingleInstanceApi();
            return false;
        }
        return true;
    }

    bool makeSingleInstanceImplApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        if (args.Length() > 0 && args[0]->IsFunction())
            singleInstanceCallback_.Reset(args.GetIsolate(), args[0]);
        else
            singleInstanceCallback_.Reset();
        return !requestSingleInstanceLockApi();
    }

    void releaseSingleInstanceApi()
    {
        stopSingleInstanceServer();
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
    std::string getSingleInstanceLockPathForTestingApi() const { return lockPath(); }
    std::string getSingleInstanceSocketPathForTestingApi() const { return socketPath(); }

    static gin_helper::WrapperInfo kWrapperInfo;
    static v8::Persistent<v8::Function> constructor;

private:
    struct Client {
        App* app = nullptr;
        uv_pipe_t pipe;
        std::string data;
        uint32_t expectedSize = 0;
        bool hasSize = false;
    };

    static void onSingleInstanceConnection(uv_stream_t* server, int status)
    {
        App* app = static_cast<App*>(server->data);
        if (!app || status < 0)
            return;

        Client* client = new Client();
        client->app = app;
        uv_pipe_init(app->singleInstanceLoop_, &client->pipe, 0);
        client->pipe.data = client;
        if (uv_accept(server, reinterpret_cast<uv_stream_t*>(&client->pipe)) == 0)
            uv_read_start(reinterpret_cast<uv_stream_t*>(&client->pipe), allocClientBuffer, readClientData);
        else
            closeClient(client);
    }

    static void allocClientBuffer(uv_handle_t*, size_t suggestedSize, uv_buf_t* buf)
    {
        char* data = new char[suggestedSize];
        *buf = uv_buf_init(data, static_cast<unsigned int>(suggestedSize));
    }

    static void readClientData(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
    {
        Client* client = static_cast<Client*>(stream->data);
        if (nread > 0 && client) {
            client->data.append(buf->base, static_cast<size_t>(nread));
            processClientData(client);
        }
        delete[] buf->base;
        if (nread < 0 && client)
            closeClient(client);
    }

    static void processClientData(Client* client)
    {
        if (!client->hasSize && client->data.size() >= sizeof(uint32_t)) {
            memcpy(&client->expectedSize, client->data.data(), sizeof(uint32_t));
            client->data.erase(0, sizeof(uint32_t));
            client->hasSize = true;
        }
        if (!client->hasSize || client->data.size() < client->expectedSize)
            return;

        std::string payload = client->data.substr(0, client->expectedSize);
        if (client->app)
            client->app->dispatchSecondInstance(payload);
        closeClient(client);
    }

    static void closeClient(Client* client)
    {
        if (!client)
            return;
        if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&client->pipe))) {
            uv_close(reinterpret_cast<uv_handle_t*>(&client->pipe), [](uv_handle_t* handle) {
                delete static_cast<Client*>(handle->data);
            });
        } else {
            delete client;
        }
    }

    bool startSingleInstanceServer()
    {
        if (singleInstanceServerStarted_)
            return true;

        singleInstanceLoop_ = node::GetCurrentEventLoop(isolate());
        if (!singleInstanceLoop_) {
            singleInstanceFailureReason_ = "missing-node-event-loop";
            return false;
        }

        singleInstanceSocketPath_ = socketPath();
        unlink(singleInstanceSocketPath_.c_str());

        int err = uv_pipe_init(singleInstanceLoop_, &singleInstanceServer_, 0);
        if (err != 0) {
            singleInstanceFailureReason_ = std::string("pipe-init-failed:") + uv_strerror(err);
            return false;
        }
        singleInstanceServer_.data = this;
        singleInstanceServerInitialized_ = true;

        err = uv_pipe_bind(&singleInstanceServer_, singleInstanceSocketPath_.c_str());
        if (err != 0) {
            singleInstanceFailureReason_ = std::string("pipe-bind-failed:") + uv_strerror(err);
            stopSingleInstanceServer();
            return false;
        }
        err = uv_listen(reinterpret_cast<uv_stream_t*>(&singleInstanceServer_), 16, onSingleInstanceConnection);
        if (err != 0) {
            singleInstanceFailureReason_ = std::string("pipe-listen-failed:") + uv_strerror(err);
            stopSingleInstanceServer();
            return false;
        }

        singleInstanceServerStarted_ = true;
        return true;
    }

    void stopSingleInstanceServer()
    {
        if (singleInstanceServerInitialized_ && !uv_is_closing(reinterpret_cast<uv_handle_t*>(&singleInstanceServer_))) {
            uv_close(reinterpret_cast<uv_handle_t*>(&singleInstanceServer_), nullptr);
        }
        singleInstanceServerInitialized_ = false;
        singleInstanceServerStarted_ = false;
        singleInstanceLoop_ = nullptr;
        if (!singleInstanceSocketPath_.empty())
            unlink(singleInstanceSocketPath_.c_str());
        singleInstanceSocketPath_.clear();
        singleInstanceCallback_.Reset();
    }

    void dispatchSecondInstance(const std::string& payload)
    {
        std::optional<base::Value> parsed = base::JSONReader::Read(payload);
        if (!parsed || !parsed->is_list())
            return;

        base::Value::List& payloadList = parsed->GetList();
        if (payloadList.size() == 0)
            return;

        const std::string* cwd = payloadList[payloadList.size() - 1].GetIfString();
        if (!cwd)
            return;

        base::Value::List argv;
        for (size_t i = 0; i + 1 < payloadList.size(); ++i) {
            const std::string* arg = payloadList[i].GetIfString();
            if (arg)
                argv.Append(*arg);
        }

        v8::HandleScope handleScope(isolate());
        v8::Local<v8::Value> argvValue = gin_helper::Converter<base::Value::List>::ToV8(isolate(), argv);
        v8::Local<v8::Value> cwdValue = v8::String::NewFromUtf8(isolate(), cwd->c_str(), v8::NewStringType::kNormal, cwd->size()).ToLocalChecked();
        mate::internal::ValueVector args = {
            gin_helper::StringToV8(isolate(), "second-instance"),
            argvValue,
            cwdValue,
        };
        mate::internal::callEmitWithArgs(isolate(), getWrapper(), &args);

        if (!singleInstanceCallback_.IsEmpty()) {
            v8::Local<v8::Value> callbackValue = singleInstanceCallback_.Get(isolate());
            if (callbackValue->IsFunction()) {
                v8::Local<v8::Value> callbackArgs[] = {
                    v8::String::NewFromUtf8(isolate(), payload.c_str(), v8::NewStringType::kNormal, payload.size()).ToLocalChecked()
                };
                callbackValue.As<v8::Function>()->Call(isolate()->GetCurrentContext(), v8::Undefined(isolate()), 1, callbackArgs).ToLocalChecked();
            }
        }
    }

    static App* instance_;
    bool isReady_ = false;
    bool isQuitting_ = false;
    bool exitCalled_ = false;
    int exitCode_ = 0;
    int singleInstanceFd_ = -1;
    std::string singleInstancePath_;
    std::string singleInstanceSocketPath_;
    std::string singleInstanceFailureReason_;
    uv_loop_t* singleInstanceLoop_ = nullptr;
    uv_pipe_t singleInstanceServer_;
    bool singleInstanceServerInitialized_ = false;
    bool singleInstanceServerStarted_ = false;
    v8::Persistent<v8::Value> singleInstanceCallback_;
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
