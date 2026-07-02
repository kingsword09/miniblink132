#include "electron/nodeblink.h"
#include "electron/common/NodeRegisterHelp.h"
#include "third_party/libnode/src/node_binding.h"
#include "v8/include/v8.h"

#include "client/mac/crash_generation/crash_generation_client.h"
#include "client/mac/crash_generation/crash_generation_server.h"
#include "client/mac/handler/exception_handler.h"
#include "google_breakpad/common/minidump_format.h"

#include <dirent.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <spawn.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits.h>
#include <memory>
#include <set>
#include <string>
#include <vector>

extern char** environ;

namespace {

struct DumpStats {
    uint32_t stream_count = 0;
    uint32_t thread_count = 0;
    uint32_t module_count = 0;
    uint32_t file_size = 0;
};

struct DumpWriteContext {
    std::string generated_path;
    bool succeeded = false;
};

struct CrashServiceState {
    pid_t pid = -1;
    std::string port_name;
    std::string directory;
    bool out_of_process = false;
};

std::unique_ptr<google_breakpad::ExceptionHandler> g_breakpad_handler;
CrashServiceState g_crash_service;
volatile sig_atomic_t g_service_process_stopping = 0;

v8::Local<v8::String> v8String(v8::Isolate* isolate, const std::string& value)
{
    return v8::String::NewFromUtf8(isolate, value.c_str()).ToLocalChecked();
}

std::string stringFromV8(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    v8::String::Utf8Value utf8(isolate, value);
    return *utf8 ? std::string(*utf8, utf8.length()) : std::string();
}

void throwTypeError(v8::Isolate* isolate, const char* message)
{
    isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, message)));
}

void handleCrashServiceProcessSignal(int)
{
    g_service_process_stopping = 1;
}

std::string directoryName(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return ".";
    if (slash == 0)
        return "/";
    return path.substr(0, slash);
}

uint64_t nowMicros()
{
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + static_cast<uint64_t>(tv.tv_usec);
}

bool processIsRunning(pid_t pid)
{
    if (pid <= 0)
        return false;
    if (kill(pid, 0) == 0)
        return true;
    return errno == EPERM;
}

std::string currentExecutablePath()
{
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (!size)
        return std::string();

    std::vector<char> buffer(size + 1);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return std::string();

    char resolved[PATH_MAX];
    if (realpath(buffer.data(), resolved))
        return std::string(resolved);
    return std::string(buffer.data());
}

std::string makeCrashServicePortName()
{
    char buffer[160];
    snprintf(buffer,
        sizeof(buffer),
        "org.miniblink.electron.breakpad.%d.%llu",
        static_cast<int>(getpid()),
        static_cast<unsigned long long>(nowMicros()));
    return std::string(buffer);
}

bool waitForCrashServicePort(const std::string& port_name, int timeout_ms)
{
    mach_port_t task_bootstrap_port = MACH_PORT_NULL;
    if (task_get_bootstrap_port(mach_task_self(), &task_bootstrap_port) != KERN_SUCCESS)
        return false;

    const uint64_t deadline = nowMicros() + static_cast<uint64_t>(timeout_ms) * 1000ULL;
    while (nowMicros() < deadline) {
        mach_port_t service_port = MACH_PORT_NULL;
        kern_return_t result = bootstrap_look_up(task_bootstrap_port,
            const_cast<char*>(port_name.c_str()),
            &service_port);
        if (result == KERN_SUCCESS) {
            mach_port_deallocate(mach_task_self(), service_port);
            return true;
        }
        usleep(50 * 1000);
    }
    return false;
}

void resetCrashServiceState()
{
    g_crash_service = CrashServiceState();
}

bool stopCrashService()
{
    if (g_crash_service.pid <= 0) {
        resetCrashServiceState();
        return true;
    }

    pid_t pid = g_crash_service.pid;
    if (processIsRunning(pid))
        kill(pid, SIGTERM);

    for (int i = 0; i < 40; ++i) {
        int status = 0;
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid || waited == -1) {
            resetCrashServiceState();
            return true;
        }
        usleep(50 * 1000);
    }

    if (processIsRunning(pid))
        kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    resetCrashServiceState();
    return true;
}

bool startCrashService(const std::string& directory)
{
    if (g_crash_service.pid > 0 && processIsRunning(g_crash_service.pid)) {
        if (g_crash_service.directory == directory)
            return true;
        stopCrashService();
    }
    resetCrashServiceState();

    if (mkdir(directory.c_str(), 0755) != 0 && errno != EEXIST)
        return false;

    std::string executable = currentExecutablePath();
    if (executable.empty())
        return false;

    std::string port_name = makeCrashServicePortName();
    std::vector<std::string> args;
    args.push_back(executable);
    args.push_back("--electron-breakpad-crash-service");
    args.push_back("--electron-breakpad-crash-port=" + port_name);
    args.push_back("--electron-breakpad-crash-dir=" + directory);
    args.push_back("--electron-breakpad-crash-parent=" + std::to_string(static_cast<int>(getpid())));

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = -1;
    int spawn_result = posix_spawn(&pid, executable.c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawn_result != 0)
        return false;

    g_crash_service.pid = pid;
    g_crash_service.port_name = port_name;
    g_crash_service.directory = directory;
    g_crash_service.out_of_process = true;

    if (!waitForCrashServicePort(port_name, 5000)) {
        stopCrashService();
        return false;
    }
    return true;
}

std::set<std::string> listDumpFiles(const std::string& directory)
{
    std::set<std::string> files;
    DIR* dir = opendir(directory.c_str());
    if (!dir)
        return files;

    while (dirent* entry = readdir(dir)) {
        std::string name(entry->d_name);
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".dmp") == 0)
            files.insert(name);
    }
    closedir(dir);
    return files;
}

std::string findNewDumpFile(const std::string& directory, const std::set<std::string>& before)
{
    std::string best_path;
    time_t best_mtime = 0;
    std::set<std::string> after = listDumpFiles(directory);
    for (const std::string& name : after) {
        if (before.find(name) != before.end())
            continue;
        std::string path = directory + "/" + name;
        struct stat file_stat {};
        if (stat(path.c_str(), &file_stat) != 0)
            continue;
        if (best_path.empty() || file_stat.st_mtime >= best_mtime) {
            best_path = path;
            best_mtime = file_stat.st_mtime;
        }
    }
    return best_path;
}

bool breakpadDumpCallback(const char* dump_dir, const char* minidump_id, void* context, bool succeeded)
{
    DumpWriteContext* write_context = static_cast<DumpWriteContext*>(context);
    if (!write_context)
        return succeeded;

    write_context->succeeded = succeeded;
    if (succeeded && dump_dir && minidump_id)
        write_context->generated_path = std::string(dump_dir) + "/" + minidump_id + ".dmp";
    return succeeded;
}

bool replaceFile(const std::string& from, const std::string& to)
{
    if (from == to)
        return true;
    if (rename(from.c_str(), to.c_str()) == 0)
        return true;
    if (errno == EEXIST) {
        unlink(to.c_str());
        return rename(from.c_str(), to.c_str()) == 0;
    }
    return false;
}

template <typename T>
bool readAt(FILE* file, uint32_t rva, T* out)
{
    if (!file || !out)
        return false;
    if (fseek(file, rva, SEEK_SET) != 0)
        return false;
    return fread(out, sizeof(T), 1, file) == 1;
}

bool parseDumpStats(const std::string& path, DumpStats* stats)
{
    if (!stats)
        return false;

    struct stat file_stat {};
    if (stat(path.c_str(), &file_stat) != 0)
        return false;
    stats->file_size = file_stat.st_size > 0 ? static_cast<uint32_t>(file_stat.st_size) : 0;

    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
        return false;

    MDRawHeader header {};
    bool ok = fread(&header, sizeof(header), 1, file) == 1
        && header.signature == MD_HEADER_SIGNATURE
        && header.stream_count < 1024;
    if (ok)
        stats->stream_count = header.stream_count;

    for (uint32_t i = 0; ok && i < header.stream_count; ++i) {
        MDRawDirectory directory {};
        uint32_t directory_rva = header.stream_directory_rva + i * sizeof(MDRawDirectory);
        if (!readAt(file, directory_rva, &directory)) {
            ok = false;
            break;
        }

        if (directory.stream_type == MD_THREAD_LIST_STREAM) {
            uint32_t count = 0;
            if (readAt(file, directory.location.rva, &count))
                stats->thread_count = count;
        } else if (directory.stream_type == MD_MODULE_LIST_STREAM) {
            uint32_t count = 0;
            if (readAt(file, directory.location.rva, &count))
                stats->module_count = count;
        }
    }

    fclose(file);
    return ok;
}

v8::Local<v8::Object> makeDumpResult(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    const std::string& path,
    const DumpStats& stats)
{
    v8::Local<v8::Object> result = v8::Object::New(isolate);
    result->Set(context, v8String(isolate, "path"), v8String(isolate, path)).ToChecked();
    result->Set(context, v8String(isolate, "size"), v8::Integer::NewFromUnsigned(isolate, stats.file_size)).ToChecked();
    result->Set(context, v8String(isolate, "streamCount"), v8::Integer::NewFromUnsigned(isolate, stats.stream_count)).ToChecked();
    result->Set(context, v8String(isolate, "threadCount"), v8::Integer::NewFromUnsigned(isolate, stats.thread_count)).ToChecked();
    result->Set(context, v8String(isolate, "moduleCount"), v8::Integer::NewFromUnsigned(isolate, stats.module_count)).ToChecked();
    return result;
}

v8::Local<v8::Object> makeCrashServiceStatus(v8::Isolate* isolate, v8::Local<v8::Context> context)
{
    bool running = g_crash_service.pid > 0 && processIsRunning(g_crash_service.pid);
    v8::Local<v8::Object> result = v8::Object::New(isolate);
    result->Set(context, v8String(isolate, "running"), v8::Boolean::New(isolate, running)).ToChecked();
    result->Set(context, v8String(isolate, "outOfProcess"), v8::Boolean::New(isolate, running && g_crash_service.out_of_process)).ToChecked();
    result->Set(context, v8String(isolate, "pid"), v8::Integer::New(isolate, running ? g_crash_service.pid : -1)).ToChecked();
    result->Set(context, v8String(isolate, "portName"), v8String(isolate, running ? g_crash_service.port_name : std::string())).ToChecked();
    result->Set(context, v8String(isolate, "directory"), v8String(isolate, running ? g_crash_service.directory : std::string())).ToChecked();
    return result;
}

bool writeBreakpadMinidump(const std::string& path, DumpStats* stats)
{
    DumpWriteContext context;
    std::string dump_dir = directoryName(path);
    bool ok = google_breakpad::ExceptionHandler::WriteMinidump(
        dump_dir, true, breakpadDumpCallback, &context);
    if (!ok || !context.succeeded || context.generated_path.empty())
        return false;
    if (!replaceFile(context.generated_path, path))
        return false;
    return parseDumpStats(path, stats);
}

void writeMinidumpApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsString()) {
        throwTypeError(isolate, "minidump path is required");
        return;
    }

    DumpStats stats;
    std::string path = stringFromV8(isolate, info[0]);
    if (!writeBreakpadMinidump(path, &stats)) {
        isolate->ThrowException(v8::Exception::Error(v8String(isolate, "failed to write Breakpad minidump")));
        return;
    }

    info.GetReturnValue().Set(makeDumpResult(isolate, context, path, stats));
}

void startCrashServiceApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsString()) {
        throwTypeError(isolate, "crash service directory is required");
        return;
    }

    std::string directory = stringFromV8(isolate, info[0]);
    if (!startCrashService(directory)) {
        isolate->ThrowException(v8::Exception::Error(v8String(isolate, "failed to start Breakpad crash service")));
        return;
    }
    info.GetReturnValue().Set(makeCrashServiceStatus(isolate, context));
}

void getCrashServiceStatusApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    info.GetReturnValue().Set(makeCrashServiceStatus(isolate, isolate->GetCurrentContext()));
}

void stopCrashServiceApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    bool stopped = stopCrashService();
    info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), stopped));
}

void requestCrashServiceDumpApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (g_crash_service.pid <= 0 || !processIsRunning(g_crash_service.pid) || g_crash_service.port_name.empty()) {
        isolate->ThrowException(v8::Exception::Error(v8String(isolate, "Breakpad crash service is not running")));
        return;
    }

    std::string requested_path;
    if (info.Length() > 0 && info[0]->IsString())
        requested_path = stringFromV8(isolate, info[0]);

    std::set<std::string> before = listDumpFiles(g_crash_service.directory);
    google_breakpad::CrashGenerationClient client(g_crash_service.port_name.c_str());
    if (!client.RequestDump()) {
        isolate->ThrowException(v8::Exception::Error(v8String(isolate, "Breakpad crash service dump request failed")));
        return;
    }

    std::string generated_path = findNewDumpFile(g_crash_service.directory, before);
    if (generated_path.empty()) {
        isolate->ThrowException(v8::Exception::Error(v8String(isolate, "Breakpad crash service did not produce a minidump")));
        return;
    }

    std::string final_path = requested_path.empty() ? generated_path : requested_path;
    if (!requested_path.empty() && !replaceFile(generated_path, requested_path)) {
        isolate->ThrowException(v8::Exception::Error(v8String(isolate, "failed to move Breakpad service minidump")));
        return;
    }

    DumpStats stats;
    if (!parseDumpStats(final_path, &stats)) {
        isolate->ThrowException(v8::Exception::Error(v8String(isolate, "failed to parse Breakpad service minidump")));
        return;
    }

    v8::Local<v8::Object> result = makeDumpResult(isolate, context, final_path, stats);
    result->Set(context, v8String(isolate, "servicePid"), v8::Integer::New(isolate, g_crash_service.pid)).ToChecked();
    result->Set(context, v8String(isolate, "portName"), v8String(isolate, g_crash_service.port_name)).ToChecked();
    result->Set(context, v8String(isolate, "outOfProcess"), v8::Boolean::New(isolate, true)).ToChecked();
    info.GetReturnValue().Set(result);
}

void installSignalHandlersApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsString()) {
        throwTypeError(isolate, "crash report directory is required");
        return;
    }

    std::string directory = stringFromV8(isolate, info[0]);
    const char* port_name = nullptr;
    if (startCrashService(directory))
        port_name = g_crash_service.port_name.c_str();
    g_breakpad_handler = std::make_unique<google_breakpad::ExceptionHandler>(
        directory, nullptr, breakpadDumpCallback, nullptr, true, port_name);
    info.GetReturnValue().Set(makeCrashServiceStatus(isolate, isolate->GetCurrentContext()));
}

void initializeCrashReporterApi(v8::Local<v8::Object> exports,
    v8::Local<v8::Value>,
    v8::Local<v8::Context> context,
    const NodeNative*)
{
    v8::Isolate* isolate = context->GetIsolate();
    exports->Set(context, v8String(isolate, "writeMinidump"),
        v8::FunctionTemplate::New(isolate, writeMinidumpApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "startCrashService"),
        v8::FunctionTemplate::New(isolate, startCrashServiceApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "getCrashServiceStatus"),
        v8::FunctionTemplate::New(isolate, getCrashServiceStatusApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "requestCrashServiceDump"),
        v8::FunctionTemplate::New(isolate, requestCrashServiceDumpApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "stopCrashService"),
        v8::FunctionTemplate::New(isolate, stopCrashServiceApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "installSignalHandlers"),
        v8::FunctionTemplate::New(isolate, installSignalHandlersApi)->GetFunction(context).ToLocalChecked()).ToChecked();
}

const char CrashReporterScript[] = "exports = {};";
NodeNative nativeCrashReporterNative { "CrashReporter", CrashReporterScript, sizeof(CrashReporterScript) - 1 };

} // namespace

extern "C" int electronBreakpadCrashServiceMain(const char* portName, const char* crashDir, int parentPid)
{
    if (!portName || !*portName || !crashDir || !*crashDir)
        return 94;

    signal(SIGTERM, handleCrashServiceProcessSignal);
    signal(SIGINT, handleCrashServiceProcessSignal);

    google_breakpad::CrashGenerationServer server(portName,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        true,
        crashDir);
    if (!server.Start())
        return 95;

    while (!g_service_process_stopping) {
        if (parentPid > 1 && kill(static_cast<pid_t>(parentPid), 0) != 0 && errno == ESRCH)
            break;
        sleep(1);
    }

    server.Stop();
    return 0;
}

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_common_crash_reporter, initializeCrashReporterApi, &nativeCrashReporterNative)
