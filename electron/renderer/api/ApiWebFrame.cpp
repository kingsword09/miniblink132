// Copyright (c) 2017 weolar, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/nodeblink.h"
#include "mbvip/core/mb.h"
#include "content/renderer/V8ValueConverterImpl.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libnode/src/node_version.h"
#include "third_party/libuv/include/uv.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/promise.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "electron/common/gin_helper/wrappable.h"
#include "electron/common/gin_helper/arguments.h"
#include "electron/common/gin_helper/CallbackConverter.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/api/EventEmitter.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/renderer/api/ApiContextBridge.h"
#include "electron/renderer/api/ObjectCache.h"
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "third_party/blink/public/web/web_security_policy.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_text_check_client.h"
#include "third_party/blink/public/web/web_text_checking_completion.h"
#include "third_party/blink/public/web/web_text_checking_result.h"
#include "third_party/blink/public/web/web_text_decoration_type.h"
#include "third_party/blink/renderer/platform/bindings/dom_wrapper_world.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace gin_helper {

template <> struct Converter<blink::WebCssOrigin> {
    static bool FromV8(v8::Isolate* isolate, v8::Local<v8::Value> val, blink::WebCssOrigin* out)
    {
        std::string cssOrigin;
        if (!ConvertFromV8(isolate, val, &cssOrigin))
            return false;
        if (cssOrigin == "user") {
            *out = blink::WebCssOrigin::kUser;
        } else if (cssOrigin == "author") {
            *out = blink::WebCssOrigin::kAuthor;
        } else {
            return false;
        }
        return true;
    }
};

}

namespace atom {

namespace {

WTF::String toBlinkScheme(const std::string& scheme)
{
    return WTF::String::FromUTF8(scheme).LowerASCII();
}

blink::WebString toWebScheme(const std::string& scheme)
{
    return blink::WebString::FromUTF8(toBlinkScheme(scheme).Utf8());
}

double zoomLevelToZoomFactor(double zoomLevel)
{
    return std::pow(1.2, zoomLevel);
}

double zoomFactorToZoomLevel(double zoomFactor)
{
    return std::log(zoomFactor) / std::log(1.2);
}

class ScriptExecutionCallback {
public:
    // for compatibility with the older version of this, error is after result
    using CompletionCallback = base::OnceCallback<void(const v8::Local<v8::Value>& result, const v8::Local<v8::Value>& error)>;

    explicit ScriptExecutionCallback(gin_helper::Promise<v8::Local<v8::Value>> promise, CompletionCallback callback)
        : m_promise(std::move(promise))
        , m_callback(std::move(callback))
    {
    }

    ~ScriptExecutionCallback() = default;

    ScriptExecutionCallback(const ScriptExecutionCallback&) = delete;
    ScriptExecutionCallback& operator=(const ScriptExecutionCallback&) = delete;

    void opyResultToCallingContextAndFinalize(v8::Isolate* isolate, const v8::Local<v8::Object>& result)
    {
        v8::MaybeLocal<v8::Value> maybeResult;
        bool success = true;
        std::string errorMessage = "An unknown exception occurred while getting the result of the script";
        {
            v8::TryCatch tryCatch(isolate);
            api::context_bridge::ObjectCache object_cache;
            maybeResult = api::PassValueToOtherContext(result->GetCreationContextChecked(), m_promise.GetContext(), result, &object_cache, false, 0);
            if (maybeResult.IsEmpty() || tryCatch.HasCaught()) {
                success = false;
            }
            if (tryCatch.HasCaught()) {
                auto message = tryCatch.Message();

                if (!message.IsEmpty()) {
                    gin_helper::ConvertFromV8(isolate, message->Get(), &errorMessage);
                }
            }
        }
        if (!success) {
            // Failed convert so we send undefined everywhere
            if (m_callback) {
                std::move(m_callback)
                    .Run(v8::Undefined(isolate), v8::Exception::Error(v8::String::NewFromUtf8(isolate, errorMessage.c_str()).ToLocalChecked()));
            }
            m_promise.RejectWithErrorMessage(errorMessage);
        } else {
            v8::Local<v8::Context> context = m_promise.GetContext();
            v8::Context::Scope context_scope(context);
            v8::Local<v8::Value> clonedValue = maybeResult.ToLocalChecked();

            if (m_callback)
                std::move(m_callback).Run(clonedValue, v8::Undefined(isolate));
            m_promise.Resolve(clonedValue);
        }
    }

    void completed(v8::Local<v8::Context> context, std::optional<base::Value> result, base::TimeTicks)
    {
        v8::Isolate* isolate = m_promise.isolate();
        if (result.has_value()) {
            if (/*!result[0].IsEmpty()*/true) {
                //v8::Local<v8::Value> value = result.value();
                const base::Value& valueTemp = result.value();
                std::unique_ptr<content::V8ValueConverter> converter = content::V8ValueConverter::Create();
                v8::Local<v8::Value> value = converter->ToV8Value(base::ValueView(valueTemp), context);

                // Either the result was created in the same world as the caller
                // or the result is not an object and therefore does not have a
                // prototype chain to protect
                bool shouldCloneValue
                    = !(value->IsObject() && m_promise.GetContext() == context/*value.As<v8::Object>()->GetCreationContextChecked()*/) 
                    && value->IsObject();
                if (shouldCloneValue) {
                    opyResultToCallingContextAndFinalize(isolate, value.As<v8::Object>());
                } else {
                    // Right now only single results per frame is supported.
                    if (m_callback)
                        std::move(m_callback).Run(value, v8::Undefined(isolate));
                    m_promise.Resolve(value);
                }
            } else {
                const char errorMessage[] = "Script failed to execute, this normally means an error "
                                            "was thrown. Check the renderer console for the error.";
                if (!m_callback.is_null()) {
                    v8::Local<v8::Context> context = m_promise.GetContext();
                    v8::Context::Scope context_scope(context);
                    std::move(m_callback).Run(v8::Undefined(isolate), v8::Exception::Error(v8::String::NewFromUtf8(isolate, errorMessage).ToLocalChecked()));
                }
                m_promise.RejectWithErrorMessage(errorMessage);
            }
        } else {
            const char errorMessage[] = "WebFrame was removed before script could run. This normally means the underlying frame was destroyed";
            if (!m_callback.is_null()) {
                v8::Local<v8::Context> context = m_promise.GetContext();
                v8::Context::Scope context_scope(context);
                std::move(m_callback).Run(v8::Undefined(isolate), v8::Exception::Error(v8::String::NewFromUtf8(isolate, errorMessage).ToLocalChecked()));
            }
            m_promise.RejectWithErrorMessage(errorMessage);
        }
        delete this;
    }

private:
    gin_helper::Promise<v8::Local<v8::Value>> m_promise;
    CompletionCallback m_callback;
};

class SpellCheckProviderClient : public blink::WebTextCheckClient {
public:
    SpellCheckProviderClient(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> provider)
        : m_isolate(isolate)
    {
        m_context.Reset(isolate, context);
        m_provider.Reset(isolate, provider);
    }

    ~SpellCheckProviderClient() override
    {
        m_context.Reset();
        m_provider.Reset();
    }

    bool IsSpellCheckingEnabled() const override
    {
        return true;
    }

    void CheckSpelling(
        const blink::WebString& text,
        size_t& misspelled_offset,
        size_t& misspelled_length,
        blink::WebVector<blink::WebString>* optional_suggestions) override
    {
        std::vector<blink::WebTextCheckingResult> results = runSpellCheck(text);
        if (results.empty()) {
            misspelled_offset = 0;
            misspelled_length = 0;
            if (optional_suggestions)
                optional_suggestions->Assign(std::vector<blink::WebString>());
            return;
        }

        misspelled_offset = static_cast<size_t>(std::max(results[0].location, 0));
        misspelled_length = static_cast<size_t>(std::max(results[0].length, 0));
        if (optional_suggestions)
            *optional_suggestions = results[0].replacements;
    }

    void RequestCheckingOfText(
        const blink::WebString& text,
        std::unique_ptr<blink::WebTextCheckingCompletion> completion) override
    {
        std::vector<blink::WebTextCheckingResult> results = runSpellCheck(text);
        blink::WebVector<blink::WebTextCheckingResult> webResults;
        webResults.Assign(results);
        completion->DidFinishCheckingText(webResults);
    }

private:
    std::vector<blink::WebTextCheckingResult> runSpellCheck(const blink::WebString& text)
    {
        std::vector<blink::WebTextCheckingResult> results;
        v8::HandleScope handleScope(m_isolate);
        v8::Local<v8::Context> context = m_context.Get(m_isolate);
        if (context.IsEmpty())
            return results;
        v8::Context::Scope contextScope(context);
        v8::Local<v8::Object> provider = m_provider.Get(m_isolate);
        if (provider.IsEmpty())
            return results;

        v8::Local<v8::Value> spellCheckValue;
        if (!provider->Get(context, v8::String::NewFromUtf8(m_isolate, "spellCheck").ToLocalChecked()).ToLocal(&spellCheckValue)
            || !spellCheckValue->IsFunction()) {
            return results;
        }

        v8::Local<v8::Function> spellCheck = spellCheckValue.As<v8::Function>();
        v8::Local<v8::Array> words = splitWords(context, text.Utf8());
        v8::Local<v8::Array> providerResults = v8::Array::New(m_isolate);
        v8::Local<v8::Function> completion = v8::Function::New(
            context,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                if (info.Length() < 1 || !info[0]->IsArray())
                    return;
                v8::Local<v8::External> external = info.Data().As<v8::External>();
                v8::Local<v8::Array>* out = static_cast<v8::Local<v8::Array>*>(external->Value());
                *out = info[0].As<v8::Array>();
            },
            v8::External::New(m_isolate, &providerResults))
                                           .ToLocalChecked();

        v8::Local<v8::Value> argv[2] = { words, completion };
        v8::TryCatch tryCatch(m_isolate);
        v8::MaybeLocal<v8::Value> callResult = spellCheck->Call(context, provider, 2, argv);
        if (callResult.IsEmpty() || tryCatch.HasCaught())
            return results;

        std::u16string source = text.Utf16();
        size_t searchOffset = 0;
        uint32_t length = providerResults->Length();
        for (uint32_t i = 0; i < length; ++i) {
            v8::Local<v8::Value> resultValue;
            if (!providerResults->Get(context, i).ToLocal(&resultValue) || !resultValue->IsObject())
                continue;

            gin_helper::Dictionary resultDict(m_isolate, resultValue.As<v8::Object>());
            std::string word;
            if (!resultDict.Get("word", &word) || word.empty())
                continue;
            std::u16string word16 = blink::WebString::FromUTF8(word).Utf16();

            size_t location = source.find(word16, searchOffset);
            if (location == std::string::npos)
                location = source.find(word16);
            if (location == std::string::npos)
                continue;
            searchOffset = location + word16.size();

            std::vector<std::string> suggestions;
            resultDict.Get("suggestions", &suggestions);
            std::vector<blink::WebString> replacements;
            for (const std::string& suggestion : suggestions)
                replacements.push_back(blink::WebString::FromUTF8(suggestion));
            blink::WebVector<blink::WebString> webReplacements;
            webReplacements.Assign(replacements);
            results.push_back(blink::WebTextCheckingResult(
                blink::kWebTextDecorationTypeSpelling,
                static_cast<int>(location),
                static_cast<int>(word16.size()),
                webReplacements));
        }
        return results;
    }

    v8::Local<v8::Array> splitWords(v8::Local<v8::Context> context, const std::string& text)
    {
        std::vector<std::string> words;
        std::string current;
        for (char ch : text) {
            bool wordChar = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '\'';
            if (wordChar) {
                current.push_back(ch);
            } else if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        }
        if (!current.empty())
            words.push_back(current);

        v8::Local<v8::Array> result = v8::Array::New(m_isolate, static_cast<int>(words.size()));
        for (uint32_t i = 0; i < words.size(); ++i) {
            result->Set(context, i, v8::String::NewFromUtf8(m_isolate, words[i].c_str()).ToLocalChecked()).ToChecked();
        }
        return result;
    }

    v8::Isolate* m_isolate;
    v8::Persistent<v8::Context> m_context;
    v8::Persistent<v8::Object> m_provider;
};

} // namespace

class WebFrame : public mate::EventEmitter<WebFrame> {
public:
    explicit WebFrame(v8::Isolate* isolate, v8::Local<v8::Object> wrapper)
    {
        gin_helper::Wrappable<WebFrame>::InitWith(isolate, wrapper);

        m_zoomLevel = 0;
        m_zoomFactor = 1;
    }

    ~WebFrame()
    {
        clearSpellCheckProvider();
    }

    static void init(v8::Isolate* isolate, v8::Local<v8::Object> target)
    {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::FunctionTemplate> prototype = v8::FunctionTemplate::New(isolate, newFunction);

        prototype->SetClassName(v8::String::NewFromUtf8(isolate, "WebFrame").ToLocalChecked());
        gin_helper::ObjectTemplateBuilder builder(isolate, prototype->PrototypeTemplate());
        gin_helper::ObjectTemplateBuilder instanceBuilder(isolate, prototype->InstanceTemplate());
        builder.SetMethod("registerEmbedderCustomElement", &WebFrame::registerEmbedderCustomElementApi);
        builder.SetMethod("setZoomFactor", &WebFrame::setZoomFactorApi);
        builder.SetMethod("getZoomFactor", &WebFrame::getZoomFactorApi);
        builder.SetMethod("getZoomLevel", &WebFrame::getZoomLevelApi);
        builder.SetMethod("setZoomLevel", &WebFrame::setZoomLevelApi);
        builder.SetMethod("setZoomLevelLimits", &WebFrame::setZoomLevelLimitsApi);
        builder.SetMethod("registerURLSchemeAsSecure", &WebFrame::registerURLSchemeAsSecureApi);
        builder.SetMethod("registerURLSchemeAsBypassingCSP", &WebFrame::registerURLSchemeAsBypassingCSPApi);
        builder.SetMethod("registerURLSchemeAsPrivileged", &WebFrame::registerURLSchemeAsPrivilegedApi);
        builder.SetMethod("executeJavaScript", &WebFrame::executeJavaScriptApi);
        builder.SetMethod("setVisualZoomLevelLimits", &WebFrame::setVisualZoomLevelLimitsApi);
        builder.SetMethod("setLayoutZoomLevelLimits", &WebFrame::setLayoutZoomLevelLimitsApi);
        builder.SetMethod("removeInsertedCSS", &WebFrame::removeInsertedCSSApi);
        builder.SetMethod("insertCSS", &WebFrame::insertCSSApi);
        builder.SetMethod("insertText", &WebFrame::insertTextApi);
        builder.SetMethod("setSpellCheckProvider", &WebFrame::setSpellCheckProviderApi);

        constructor.Reset(isolate, prototype->GetFunction(context).ToLocalChecked());
        target->Set(context, v8::String::NewFromUtf8(isolate, "WebFrame").ToLocalChecked(), prototype->GetFunction(context).ToLocalChecked());
    }

    v8::Local<v8::Value> registerEmbedderCustomElementApi(const std::string& name, v8::Local<v8::Object> options)
    {
        mbWebView webview = mbGetWebViewForCurrentContext();
        mbWebFrameHandle mainFrame = mbWebFrameGetMainFrame(webview);

        v8::Persistent<v8::Value> result;
        mbRegisterEmbedderCustomElement(webview, mainFrame, name.c_str(), &options, &result);
        v8::Local<v8::Value> elementConstructor = result.Get(isolate());
        result.Reset();
        return elementConstructor;
    }

    void registerElementResizeCallbackApi(/*int element_instance_id, const GuestViewContainer::ResizeCallback& callback*/)
    {
        //         auto guest_view_container = GuestViewContainer::FromID(element_instance_id);
        //         if (guest_view_container)
        //             guest_view_container->RegisterElementResizeCallback(callback);
    }

    void setVisualZoomLevelLimitsApi(int Level1, int Level2)
    {
        setZoomLevelLimitsApi(Level1, Level2);
    }

    void setLayoutZoomLevelLimitsApi(int Level1, int Level2)
    {
        setZoomLevelLimitsApi(Level1, Level2);
    }

    void setZoomFactorApi(float factor)
    {
        if (factor <= 0)
            return;

        m_zoomFactor = factor;
        m_zoomLevel = zoomFactorToZoomLevel(factor);

        mbWebView webview = mbGetWebViewForCurrentContext();
        if (webview)
            mbSetZoomFactor(webview, factor);
    }

    float getZoomFactorApi()
    {
        mbWebView webview = mbGetWebViewForCurrentContext();
        if (webview) {
            m_zoomFactor = mbGetZoomFactor(webview);
            m_zoomLevel = zoomFactorToZoomLevel(m_zoomFactor);
        }
        return m_zoomFactor;
    }

    void setZoomLevelLimitsApi(float minimumLevel, float maximumLevel)
    {
        m_minimumZoomLevel = minimumLevel;
        m_maximumZoomLevel = maximumLevel;
    }

    void setZoomLevelApi(float level)
    {
        if (m_minimumZoomLevel <= m_maximumZoomLevel) {
            if (level < m_minimumZoomLevel)
                level = m_minimumZoomLevel;
            if (level > m_maximumZoomLevel)
                level = m_maximumZoomLevel;
        }

        m_zoomLevel = level;
        setZoomFactorApi(static_cast<float>(zoomLevelToZoomFactor(level)));
    }

    float getZoomLevelApi()
    {
        getZoomFactorApi();
        return m_zoomLevel;
    }

    void registerURLSchemeAsSecureApi(const std::string& scheme)
    {
        blink::WebSecurityPolicy::AddSchemeToSecureContextSafelist(toWebScheme(scheme));
    }

    void registerURLSchemeAsBypassingCSPApi(const std::string& scheme)
    {
        blink::SchemeRegistry::RegisterURLSchemeAsBypassingContentSecurityPolicy(toBlinkScheme(scheme));
    }
    void registerURLSchemeAsPrivilegedApi(const std::string& scheme)
    {
        blink::WebString webScheme = toWebScheme(scheme);
        blink::WebSecurityPolicy::AddSchemeToSecureContextSafelist(webScheme);
        blink::WebSecurityPolicy::RegisterURLSchemeAsSupportingFetchAPI(webScheme);
        blink::WebSecurityPolicy::RegisterURLSchemeAsAllowingServiceWorkers(webScheme);
        blink::SchemeRegistry::RegisterURLSchemeAsBypassingContentSecurityPolicy(toBlinkScheme(scheme));
    }

    std::string insertCSSApi(const std::string& css, gin_helper::Arguments* args)
    {
        blink::WebCssOrigin cssOrigin = blink::WebCssOrigin::kAuthor;
        gin_helper::Dictionary options(isolate());
        if (args->GetNext(&options))
            options.Get("cssOrigin", &cssOrigin);

        v8::Local<v8::Context> context = isolate()->GetCurrentContext();

        blink::WebLocalFrame* webFrame = blink::WebLocalFrame::FrameForContext(context);
        if (webFrame)
            return webFrame->GetDocument().InsertStyleSheet(blink::WebString::FromUTF8(css), nullptr, (blink::WebCssOrigin)cssOrigin).Utf8();
        return std::string();
    }

    bool insertTextApi(const std::string& text)
    {
        v8::Local<v8::Context> context = isolate()->GetCurrentContext();
        blink::WebLocalFrame* webFrame = blink::WebLocalFrame::FrameForContext(context);
        if (!webFrame)
            return false;
        return webFrame->ExecuteCommand(blink::WebString::FromUTF8("InsertText"), blink::WebString::FromUTF8(text));
    }

    void setSpellCheckProviderApi(const std::string& language, bool autoCorrectWord, v8::Local<v8::Value> providerValue)
    {
        v8::Isolate* currentIsolate = isolate();
        v8::Local<v8::Context> context = currentIsolate->GetCurrentContext();
        blink::WebLocalFrame* webFrame = blink::WebLocalFrame::FrameForContext(context);
        if (!webFrame)
            return;

        if (providerValue->IsNullOrUndefined()) {
            clearSpellCheckProvider();
            webFrame->SetTextCheckClient(nullptr);
            return;
        }

        if (!providerValue->IsObject()) {
            currentIsolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8(currentIsolate, "provider must be an object").ToLocalChecked()));
            return;
        }

        v8::Local<v8::Object> provider = providerValue.As<v8::Object>();
        v8::Local<v8::Value> spellCheckValue;
        if (!provider->Get(context, v8::String::NewFromUtf8(currentIsolate, "spellCheck").ToLocalChecked()).ToLocal(&spellCheckValue)
            || !spellCheckValue->IsFunction()) {
            currentIsolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8(currentIsolate, "provider.spellCheck must be a function").ToLocalChecked()));
            return;
        }

        clearSpellCheckProvider();
        m_spellCheckProvider = std::make_unique<SpellCheckProviderClient>(currentIsolate, context, provider);
        webFrame->SetTextCheckClient(m_spellCheckProvider.get());
    }

    void removeInsertedCSSApi(const std::string& key)
    {
        v8::Local<v8::Context> context = isolate()->GetCurrentContext();
        blink::WebLocalFrame* webFrame = blink::WebLocalFrame::FrameForContext(context);
        if (webFrame) {
            webFrame->GetDocument().RemoveInsertedStyleSheet(blink::WebString::FromUTF8(key));
        }
    }

    v8::Local<v8::Promise> executeJavaScriptApi(gin_helper::Arguments* gin_args, const std::string& code)
    {
        gin_helper::Arguments* args = static_cast<gin_helper::Arguments*>(gin_args);

        gin_helper::Promise<v8::Local<v8::Value>> promise(isolate());
        v8::Local<v8::Promise> handle = promise.GetHandle();

        v8::Local<v8::Context> context = isolate()->GetCurrentContext();
        blink::WebLocalFrame* webFrame = blink::WebLocalFrame::FrameForContext(context);

        const blink::WebScriptSource source(blink::WebString::FromUTF8(code));

        bool has_user_gesture = false;
        args->GetNext(&has_user_gesture);

        ScriptExecutionCallback::CompletionCallback completionCallback;
        args->GetNext(&completionCallback);

        ScriptExecutionCallback* self = new ScriptExecutionCallback(std::move(promise), std::move(completionCallback));

        std::vector<blink::WebScriptSource> sourcesTemp;
        sourcesTemp.push_back(source);
        base::span<const blink::WebScriptSource> sources(sourcesTemp.begin(), sourcesTemp.end());
        webFrame->RequestExecuteScript(
            blink::DOMWrapperWorld::kMainWorldId, 
            sources,
            has_user_gesture ? blink::mojom::UserActivationOption::kActivate : blink::mojom::UserActivationOption::kDoNotActivate,
            blink::mojom::EvaluationTiming::kSynchronous, 
            blink::mojom::LoadEventBlockingOption::kDoNotBlock, 
            //base::NullCallback(),
            base::BindOnce(&ScriptExecutionCallback::completed, base::Unretained(self), context),
            blink::BackForwardCacheAware::kAllow,
            blink::mojom::WantResultOption::kWantResult, 
            blink::mojom::PromiseResultOption::kDoNotWait);

        return handle;
    }

    // callback, code, hasUserGesture
    void executeJavaScriptApi2(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Local<v8::Context> context = isolate()->GetCurrentContext();
        if (3 != args.Length())
            return;

        std::string codeString;
        if (args[0]->IsString()) {
            v8::String::Utf8Value code(isolate(), args[0]);
            codeString = *code;
        }

        bool hasUserGesture = false;
        if (args[1]->IsBoolean()) {
            v8::Local<v8::Boolean> hasUserGestureValue = args[1]->ToBoolean(isolate());
            hasUserGesture = hasUserGestureValue->Value();
        }

        v8::Persistent<v8::Value> executeJavaScriptCallback;
        if (args[2]->IsFunction())
            executeJavaScriptCallback.Reset(args.GetIsolate(), args[2]);
        else
            executeJavaScriptCallback.Reset();

        v8::HandleScope handleScope(isolate());
        v8::Function* callback = nullptr;
        v8::Local<v8::Value> f;
        if (codeString.empty() && !executeJavaScriptCallback.IsEmpty()) {
            f = executeJavaScriptCallback.Get(isolate());
            callback = v8::Function::Cast(*(f));
            callback->Call(context, v8::Undefined(isolate()), 0, nullptr);
            return;
        }

        blink::WebScriptSource code(blink::WebString::FromUTF8(codeString));
        v8::Local<v8::Value> result;

        blink::WebLocalFrame* mainFrame = blink::WebLocalFrame::FrameForContext(context);
        context = mainFrame->MainWorldScriptContext();
        v8::Context::Scope contextScope(context);
        result = mainFrame->ExecuteScriptAndReturnValue(code);

        if (executeJavaScriptCallback.IsEmpty())
            return;

        v8::Local<v8::Value> argv[1];
        f = executeJavaScriptCallback.Get(isolate());
        callback = v8::Function::Cast(*(f));
        callback->Call(context, v8::Undefined(isolate()), 0, nullptr);

        argv[0] = result;
        callback->Call(context, v8::Undefined(isolate()), 1, argv);
    }

    static void newFunction(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        if (args.IsConstructCall()) {
            new WebFrame(isolate, args.This());
            args.GetReturnValue().Set(args.This());
            return;
        }
    }

public:
    static gin::WrapperInfo kWrapperInfo;
    static v8::Persistent<v8::Function> constructor;

    float m_zoomFactor;
    float m_zoomLevel;
    float m_minimumZoomLevel = 0;
    float m_maximumZoomLevel = -1;
    std::unique_ptr<SpellCheckProviderClient> m_spellCheckProvider;

private:
    void clearSpellCheckProvider()
    {
        m_spellCheckProvider.reset();
    }
};

v8::Persistent<v8::Function> WebFrame::constructor;
gin_helper::WrapperInfo WebFrame::kWrapperInfo = { gin_helper::GinEmbedder::kEmbedderNativeGin };

void initializeWebFrameApi(v8::Local<v8::Object> exports, v8::Local<v8::Value> target, v8::Local<v8::Context> context, void* priv)
{
    WebFrame::init(context->GetIsolate(), exports);
}

} // namespace

static const char RendererWebFrameNative[] = "console.log('RendererWebFrameNative');;";
static NodeNative nativeRendererWebFrameNative { "WebFrame", RendererWebFrameNative, sizeof(RendererWebFrameNative) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_renderer_webframe, atom::initializeWebFrameApi, &nativeRendererWebFrameNative)
