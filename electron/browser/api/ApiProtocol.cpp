// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/nodeblink.h"

#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libuv/include/uv.h"
#include "electron/browser/api/ProtocolInterface.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/api/EventEmitter.h"
#include "content/common/ThreadCall.h"
#include "mbvip/core/mb.h"
#include "base/memory/ref_counted.h"
#include "url/url_util.h"
#include <algorithm>
#include <vector>
#include <map>

namespace atom {

namespace {

struct ProtocolCallbackInfo {
    ProtocolCallbackInfo(mbNetJob jobPtr)
    {
        job = jobPtr;
        isAsnyc = false;
        isCalled = false;
        isCancel = false;
    }
    mbNetJob job;

    std::string type;
    std::string url;
    std::map<std::string, std::string> newHttpHead;
    bool isAsnyc;
    bool isCalled;
    bool isCancel;
};

std::string toUrlScheme(const std::string& scheme)
{
    std::string normalized = scheme;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : static_cast<char>(ch);
    });
    return normalized;
}

WTF::String toBlinkScheme(const std::string& scheme)
{
    return WTF::String::FromUTF8(toUrlScheme(scheme));
}

struct BlinkSchemeRegistration {
    char scheme[128] = { 0 };
    bool standard = false;
    bool corsEnabled = false;
    bool secure = false;
    bool supportFetchAPI = false;
    bool bypassCSP = false;
    bool allowServiceWorkers = false;
};

constexpr size_t kMaxPendingBlinkSchemeRegistrations = 128;
constexpr size_t kMaxPendingBlinkSchemeLength = sizeof(BlinkSchemeRegistration::scheme) - 1;

BlinkSchemeRegistration g_pendingBlinkSchemeRegistrations[kMaxPendingBlinkSchemeRegistrations];
size_t g_pendingBlinkSchemeRegistrationCount = 0;

void applyBlinkSchemeRegistration(const BlinkSchemeRegistration& registration)
{
    if (!registration.scheme[0])
        return;
    if (registration.standard)
        url::AddStandardScheme(registration.scheme, url::SCHEME_WITH_HOST);
    if (registration.corsEnabled)
        url::AddCorsEnabledScheme(registration.scheme);

    WTF::String blinkScheme = toBlinkScheme(registration.scheme);
    if (registration.secure)
        blink::SchemeRegistry::RegisterURLSchemeBypassingSecureContextCheck(blinkScheme);
    if (registration.supportFetchAPI)
        blink::SchemeRegistry::RegisterURLSchemeAsSupportingFetchAPI(blinkScheme);
    if (registration.bypassCSP)
        blink::SchemeRegistry::RegisterURLSchemeAsBypassingContentSecurityPolicy(blinkScheme);
    if (registration.allowServiceWorkers)
        blink::SchemeRegistry::RegisterURLSchemeAsAllowingServiceWorkers(blinkScheme);
}

void queueBlinkSchemeRegistration(
    const std::string& scheme,
    bool standard,
    bool corsEnabled,
    bool secure,
    bool supportFetchAPI,
    bool bypassCSP,
    bool allowServiceWorkers)
{
    if (!standard && !corsEnabled && !secure && !supportFetchAPI && !bypassCSP && !allowServiceWorkers)
        return;

    if (g_pendingBlinkSchemeRegistrationCount >= kMaxPendingBlinkSchemeRegistrations)
        return;

    BlinkSchemeRegistration registration;
    std::string normalizedScheme = toUrlScheme(scheme);
    size_t copyLength = std::min(normalizedScheme.size(), kMaxPendingBlinkSchemeLength);
    memcpy(registration.scheme, normalizedScheme.c_str(), copyLength);
    registration.scheme[copyLength] = '\0';
    registration.standard = standard;
    registration.corsEnabled = corsEnabled;
    registration.secure = secure;
    registration.supportFetchAPI = supportFetchAPI;
    registration.bypassCSP = bypassCSP;
    registration.allowServiceWorkers = allowServiceWorkers;

    g_pendingBlinkSchemeRegistrations[g_pendingBlinkSchemeRegistrationCount++] = registration;
}

void applyPendingBlinkSchemeRegistrations()
{
    size_t count = g_pendingBlinkSchemeRegistrationCount;
    g_pendingBlinkSchemeRegistrationCount = 0;
    for (size_t i = 0; i < count; ++i)
        applyBlinkSchemeRegistration(g_pendingBlinkSchemeRegistrations[i]);
}

void registerPrivilegedScheme(
    const std::string& scheme,
    bool secure,
    bool standard,
    bool supportFetchAPI,
    bool bypassCSP,
    bool allowServiceWorkers,
    bool corsEnabled)
{
    std::string urlScheme = toUrlScheme(scheme);
    queueBlinkSchemeRegistration(urlScheme, standard, corsEnabled, secure, supportFetchAPI, bypassCSP, allowServiceWorkers);
}

bool shouldSkipBlinkSchemeRegistrationForTesting(v8::Isolate* isolate)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> value;
    if (!context->Global()
             ->Get(context, v8::String::NewFromUtf8(isolate, "__electronProtocolSkipBlinkRegistrationForTesting").ToLocalChecked())
             .ToLocal(&value))
        return false;
    return value->IsBoolean() && value->BooleanValue(isolate);
}

}

class Protocol : public mate::EventEmitter<Protocol>, public ProtocolInterface {
public:
    Protocol(v8::Isolate* isolate, v8::Local<v8::Object> wrapper, v8::Local<v8::Function> jsReciver)
    {
        gin_helper::Wrappable<Protocol>::InitWith(isolate, wrapper);
        ProtocolInterface::m_inst = this;
        m_jsReciver.Reset(isolate, jsReciver);
    }

    static void init(v8::Isolate* isolate, v8::Local<v8::Object> target)
    {
        v8::Local<v8::FunctionTemplate> prototype = v8::FunctionTemplate::New(isolate, newFunction);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        prototype->SetClassName(v8::String::NewFromUtf8(isolate, "Protocol").ToLocalChecked());
        gin_helper::ObjectTemplateBuilder builder(isolate, prototype->InstanceTemplate());
        builder.SetMethod("registerStandardSchemes", &Protocol::registerStandardSchemesApi);
        builder.SetMethod("registerSchemesAsPrivileged", &Protocol::registerSchemesAsPrivilegedApi);

        builder.SetMethod("_registerProtocol", &Protocol::_registerProtocolApi);
        builder.SetMethod("_unregisterProtocol", &Protocol::_unregisterProtocolApi);
        builder.SetMethod("_isProtocolHandled", &Protocol::_isProtocolHandledApi);
        builder.SetMethod("onHandlerFinish", &Protocol::onHandlerFinishApi);

        constructor.Reset(isolate, prototype->GetFunction(context).ToLocalChecked());
        target->Set(context, v8::String::NewFromUtf8(isolate, "Protocol").ToLocalChecked(), prototype->GetFunction(context).ToLocalChecked());
    }

    static void newFunction(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        if (!args.IsConstructCall()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8(isolate, "Protocol must be constructed with new").ToLocalChecked()));
            return;
        }

        if (args.Length() < 1 || !args[0]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8(isolate, "Protocol requires a handler function").ToLocalChecked()));
            return;
        }

        v8::Local<v8::Function> jsReciver = args[0].As<v8::Function>();
        new Protocol(isolate, args.This(), jsReciver);
        args.GetReturnValue().Set(args.This());
        return;
    }

    void registerStandardSchemesApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        if (args.Length() < 1)
            return;

        v8::Isolate* isolate = args.GetIsolate();
        if (!args[0]->IsArray())
            return;
        if (shouldSkipBlinkSchemeRegistrationForTesting(isolate))
            return;

        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> schemes = args[0].As<v8::Array>();
        for (uint32_t i = 0; i < schemes->Length(); ++i) {
            v8::Local<v8::Value> value;
            if (!schemes->Get(context, i).ToLocal(&value) || !value->IsString())
                continue;
            v8::String::Utf8Value scheme(isolate, value);
            if (!*scheme || !**scheme)
                continue;
            queueBlinkSchemeRegistration(std::string(*scheme), true, false, false, false, false, false);
        }
    }

    void registerSchemesAsPrivilegedApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        if (args.Length() < 1 || !args[0]->IsArray())
            return;
        if (shouldSkipBlinkSchemeRegistrationForTesting(isolate))
            return;

        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> schemes = args[0].As<v8::Array>();
        for (uint32_t i = 0; i < schemes->Length(); ++i) {
            v8::Local<v8::Value> value;
            if (!schemes->Get(context, i).ToLocal(&value) || !value->IsObject())
                continue;

            gin_helper::Dictionary entry(isolate, value.As<v8::Object>());
            std::string scheme;
            if (!entry.Get("scheme", &scheme) || scheme.empty())
                continue;

            gin_helper::Dictionary privileges(isolate);
            bool secure = false;
            bool standard = false;
            bool supportFetchAPI = false;
            bool bypassCSP = false;
            bool allowServiceWorkers = false;
            bool corsEnabled = false;
            if (entry.Get("privileges", &privileges)) {
                privileges.Get("secure", &secure);
                privileges.Get("standard", &standard);
                privileges.Get("supportFetchAPI", &supportFetchAPI);
                privileges.Get("bypassCSP", &bypassCSP);
                privileges.Get("allowServiceWorkers", &allowServiceWorkers);
                privileges.Get("corsEnabled", &corsEnabled);
            }
            registerPrivilegedScheme(scheme, secure, standard, supportFetchAPI, bypassCSP, allowServiceWorkers, corsEnabled);
        }
    }

    struct ProtocolInfo {
        ProtocolInfo(int handlerId, const std::string& protocolType)
        {
            id = handlerId;
            type = protocolType;
        }
        int id;
        std::string type;
    };

    bool registerProtocol(const std::string& scheme, int handlerId, const std::string& type, bool registerWithBlink)
    {
        base::AutoLock autoLock(m_lock);
        std::map<std::string, ProtocolInfo>::iterator it = m_schemeToHandleId.find(scheme);
        if (it != m_schemeToHandleId.end())
            return false;

        if (registerWithBlink) {
            content::ThreadCall::callBlinkThreadAsync(FROM_HERE, [scheme] {
                applyPendingBlinkSchemeRegistrations();
                WTF::String schemeStr = toBlinkScheme(scheme);
                blink::SchemeRegistry::RegisterURLSchemeAsSupportingFetchAPI(schemeStr);
                blink::SchemeRegistry::RegisterURLSchemeAsAllowingServiceWorkers(schemeStr);
            });
        }

        m_schemeToHandleId.insert(std::make_pair(scheme, ProtocolInfo(handlerId, type)));
        return true;
    }

    void _registerProtocolApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        std::string scheme;
        std::string type;
        int32_t handlerId = 0;
        if (args.Length() < 3
            || !gin_helper::ConvertFromV8(isolate, args[0], &scheme)
            || !gin_helper::ConvertFromV8(isolate, args[1], &handlerId)
            || !gin_helper::ConvertFromV8(isolate, args[2], &type)) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8(isolate, "_registerProtocol requires scheme, handler id, and type").ToLocalChecked()));
            return;
        }

        bool registerWithBlink = true;
        if (args.Length() > 3 && args[3]->IsBoolean())
            registerWithBlink = args[3]->BooleanValue(isolate);

        args.GetReturnValue().Set(v8::Boolean::New(isolate, registerProtocol(scheme, handlerId, type, registerWithBlink)));
    }

    void _unregisterProtocolApi(const std::string& scheme)
    {
        base::AutoLock autoLock(m_lock);
        m_schemeToHandleId.erase(scheme);
    }

    bool _isProtocolHandledApi(const std::string& scheme)
    {
        base::AutoLock autoLock(m_lock);
        std::map<std::string, ProtocolInfo>::iterator it = m_schemeToHandleId.find(scheme);
        return (it != m_schemeToHandleId.end());
    }

    static std::string normalizeFilePath(const std::string& path)
    {
        std::string result = "file:///";
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '\\')
                result += '/';
            else
                result += path[i];
        }
        return result;
    }

    void onHandlerFinishApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Value> args0 = args[0];

        v8::Local<v8::Value> args1 = args[1];
        uint64_t infoPtr = args1->ToBigInt(context).ToLocalChecked()->Uint64Value();
        ProtocolCallbackInfo* info = (ProtocolCallbackInfo*)infoPtr;
        info->isCalled = true;

        if (info->type == "file") {
            std::string filePath;
            if (args0->IsString()) {
                gin_helper::ConvertFromV8(isolate, args0, &filePath);
            } else if (args0->IsObject()) {
                gin_helper::Dictionary request(isolate, args0.As<v8::Object>());
                request.Get("path", &filePath);
            }
            if (filePath.empty()) {
                mbNetCancelRequest(info->job);
                if (info->isAsnyc)
                    delete info;
                return;
            }
            mbNetChangeRequestUrl(info->job, normalizeFilePath(filePath).c_str());
        } else if (info->type == "string") {
            if (!args0->IsObject()) {
                mbNetCancelRequest(info->job);
                if (info->isAsnyc)
                    delete info;
                return;
            }
            std::string mimeType;
            gin_helper::Dictionary request(isolate, args0.As<v8::Object>());

            std::string data;
            if (!request.Get("data", &data)) {
                mbNetCancelRequest(info->job);
                if (info->isAsnyc)
                    delete info;
                return;
            }
            request.Get("mimeType", &mimeType);
            mbNetSetData(info->job, (void*)data.c_str(), data.size());
            if (!mimeType.empty())
                mbNetSetMIMEType(info->job, mimeType.c_str());
        } else if (info->type == "buffer") {
            if (!args0->IsObject()) {
                mbNetCancelRequest(info->job);
                if (info->isAsnyc)
                    delete info;
                return;
            }
            std::string mimeType;
            gin_helper::Dictionary request(isolate, args0.As<v8::Object>());

            request.Get("mimeType", &mimeType);

            v8::Local<v8::Value> dataV8;
            if (!request.Get("data", &dataV8)) {
                mbNetCancelRequest(info->job);
                if (info->isAsnyc)
                    delete info;
                return;
            }
            size_t dataLen = 0;
            char* data = nodeBufferGetData(&dataV8, &dataLen);
            char emptyData = 0;
            if (!data && dataLen == 0)
                data = &emptyData;
            if (!data) {
                mbNetCancelRequest(info->job);
                if (info->isAsnyc)
                    delete info;
                return;
            }
            mbNetSetData(info->job, data, (int)dataLen);
            if (!mimeType.empty())
                mbNetSetMIMEType(info->job, mimeType.c_str());
        }

        if (true || info->isAsnyc) {
            //                 std::map<std::string, std::string>::const_iterator it = info->newHttpHead.begin();
            //                 for (; it != info->newHttpHead.end(); ++it) {
            //                     mbNetSetHTTPHeaderField(info->job, StringUtil::UTF8ToUTF16(it->first).c_str(), StringUtil::UTF8ToUTF16(it->second).c_str(), FALSE);
            //                 }
            mbNetContinueJob(info->job);
            if (info->isAsnyc)
                delete info;
        }
    }

    virtual bool handleLoadUrlBegin(void* param, const char* url, void* job) override
    {
//         std::string temp = "handleLoadUrlBegin:";
//         temp += url;
//         temp += "\n";
//         OutputDebugStringA(temp.c_str());

        const char* pos = strstr(url, "://");
        if (!pos)
            return false;
        std::string scheme(url, pos);

        base::AutoLock autoLock(m_lock);
        std::map<std::string, ProtocolInfo>::iterator it = m_schemeToHandleId.find(scheme);
        if (it == m_schemeToHandleId.end())
            return false;

        int id = it->second.id;

        ProtocolCallbackInfo* info = new ProtocolCallbackInfo(job);
        info->type = it->second.type;
        info->url = url;

        std::string* referrer = new std::string(mbNetGetReferrer(job));
        mbRequestType httpMethod = mbNetGetRequestMethod(job);

        content::ThreadCall::callUiThreadAsync(FROM_HERE, [id, info, referrer, httpMethod, job] {
            v8::Isolate* isolate = v8::Isolate::GetCurrent();
            Protocol* self = (Protocol*)Protocol::inst();
            v8::Local<v8::Value> args[5];
            args[0] = v8::Integer::New(isolate, id);

            v8::Local<v8::Object> request = v8::Object::New(isolate);
            gin_helper::Dictionary dictRequest(isolate, request);
            dictRequest.Set("url", info->url.c_str());
            dictRequest.Set("referrer", referrer->c_str());
            dictRequest.Set("method", httpMethod == kMbRequestTypeGet ? "GET" : (httpMethod == kMbRequestTypePost ? "POST" : "PUT"));

            args[1] = request;
            args[2] = v8::BigInt::New(isolate, (int64_t)info);

            v8::Local<v8::Function> func = self->m_jsReciver.Get(isolate);

            if (!func->GetCreationContext().IsEmpty())
                func->Call(func->GetCreationContextChecked(), v8::Undefined(isolate), 3, args);

            if (!info->isCalled) {
                info->isAsnyc = true;
                mbNetHoldJobToAsynCommit(job);
            } else {
                delete info;
            }

            delete referrer;
        });

        mbNetHoldJobToAsynCommit(job);

        return true;
    }

    v8::Local<v8::Object> getWrapper(v8::Isolate* isolate) override
    {
        return GetWrapper(isolate);
    }

public:
    static gin_helper::WrapperInfo kWrapperInfo;
    static v8::Persistent<v8::Function> constructor;

    v8::Persistent<v8::Function> m_jsReciver;
    std::map<std::string, ProtocolInfo> m_schemeToHandleId;
    base::Lock m_lock;
};

v8::Persistent<v8::Function> Protocol::constructor;
gin_helper::WrapperInfo Protocol::kWrapperInfo = { gin_helper::GinEmbedder::kEmbedderNativeGin };
ProtocolInterface* ProtocolInterface::m_inst = nullptr;

void initializeProtocolApi(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused, v8::Local<v8::Context> context, void* priv)
{
    Protocol::init(context->GetIsolate(), exports);
}

} // atom namespace

static const char BrowserProtocolNative[] = "console.log('BrowserProtocolNative');;";
static NodeNative nativeBrowserProtocolNative { "Protocol", BrowserProtocolNative, sizeof(BrowserProtocolNative) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_browser_protocol, atom::initializeProtocolApi, &nativeBrowserProtocolNative)
