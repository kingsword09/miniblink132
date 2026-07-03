
#include <stdlib.h>
#include <crtdbg.h>
#include "mbvip/core/mb.h"
#include "mbvip/download/SimpleDownload.h"
#include "mbvip/core/MbJsValue.h"
#include "content/browser/MbWebview.h"
#include "content/browser/SharedTimerWin.h"
#include "content/common/LiveIdDetect.h"
#include "content/common/ThreadCall.h"
#include "content/common/mbchar.h"
#include "content/renderer/RenderThreadImpl.h"
#include "mbnet/WebURLLoaderInternal.h"
#include "mbnet/WebURLLoaderManager.h"
#include "mbnet/InitializeHandleInfo.h"
#include "mbnet/FlattenHTTPBodyElement.h"
#include "mbnet/WebURLLoaderManagerSetupInfo.h"
#include "mbnet/DefaultLocalStorageDir.h"
#include "mbnet/websocket/WebSocketChannelCurl.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/platform/wtf/allocator/partitions.h"
#include "third_party/blink/public/web/web_view.h"
#include "third_party/blink/public/platform/web_http_header_visitor.h"
#include "services/network/public/cpp/resource_request.h"
#include "base/command_line.h"
#include "base/base64.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_executor.h"
#include "base/at_exit.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/libcurl/include/curl/curl.h"
#include <atomic>
#include <climits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "v8.h"

bool checkThreadCallIsValid(const char* funcName);
const char* createTempCharString(const char* str, size_t length);


namespace mbnet {
void onNetSetData(mbNetJob jobPtr, void* buf, int len);
void onNetSetMIMEType(mbNetJob jobPtr, const char* type);
void onNetSetHTTPHeaderFieldCommon(mbNetJob jobPtr, const utf8* key, const utf8* value, BOOL response);
void changeRequestUrl(mbNetJob jobPtr, const char* url);
}

void MB_CALL_TYPE mbNetSetHTTPHeaderFieldUtf8(mbNetJob jobPtr, const utf8* key, const utf8* value, BOOL response)
{
    mbnet::onNetSetHTTPHeaderFieldCommon(jobPtr, key, value, response);
    //     if (content::ThreadCall::isBlinkThread()) {
    //         mbnet::onNetSetHTTPHeaderField(jobPtr, key, value, response);
    //     } else {
    //         DebugBreak();
    //         std::string* keyCopy = new std::string(key);
    //         std::string* valueCopy = new std::string(value);
    //         content::ThreadCall::callBlinkThreadAsync(MB_FROM_HERE, [jobPtr, keyCopy, valueCopy, response] {
    //             mbnet::onNetSetHTTPHeaderField(jobPtr, keyCopy->c_str(), valueCopy->c_str(), response);
    //             delete keyCopy;
    //             delete valueCopy;
    //         });
    //     }
}

void MB_CALL_TYPE mbNetSetMIMEType(mbNetJob jobPtr, const char* type)
{
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    if (job->m_isUrlBegining || content::ThreadCall::isBlinkThread()) {
        mbnet::onNetSetMIMEType(jobPtr, type);
    } else {
        std::string* typeCopy = new std::string(type);
        content::ThreadCall::callBlinkThreadAsync(MB_FROM_HERE, [jobPtr, typeCopy] {
            mbnet::onNetSetMIMEType(jobPtr, typeCopy->c_str());
            delete typeCopy;
        });
    }
}

void MB_CALL_TYPE mbNetSetData(mbNetJob jobPtr, void* buf, int len)
{
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    if (job->m_isUrlBegining || content::ThreadCall::isBlinkThread()) {
        mbnet::onNetSetData(jobPtr, buf, len);
    } else {
        std::vector<char>* bufferCopy = new std::vector<char>();
        bufferCopy->resize(len);
        memcpy(&bufferCopy->at(0), buf, len);
        content::ThreadCall::callBlinkThreadAsync(MB_FROM_HERE, [jobPtr, bufferCopy] {
            mbnet::onNetSetData(jobPtr, &bufferCopy->at(0), (int)bufferCopy->size());
            delete bufferCopy;
        });
    }
}

void MB_CALL_TYPE mbNetChangeRequestUrl(mbNetJob jobPtr, const char* url)
{
    if (content::ThreadCall::isBlinkThread())
        mbnet::changeRequestUrl(jobPtr, url);
    else {
        std::string* urlCopy = new std::string(url);
        content::ThreadCall::callBlinkThreadAsync(MB_FROM_HERE, [jobPtr, urlCopy] {
            mbnet::changeRequestUrl(jobPtr, urlCopy->c_str());
            delete urlCopy;
        });
    }
}

mbRequestType MB_CALL_TYPE mbNetGetRequestMethod(void* jobPtr)
{
    checkThreadCallIsValid(__FUNCTION__);
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    mbnet::InitializeHandleInfo* info = job->m_initializeHandleInfo;
    std::string method;
    if (!info) {
        method = job->firstRequest()->method;
        if (method.empty())
            return kMbRequestTypeInvalidation;
    } else
        method = info->method;

    if ("POST" == method) {
        return kMbRequestTypePost;
    } else if ("PUT" == method) {
        return kMbRequestTypePut;
    } else if ("GET" == method) {
        return kMbRequestTypeGet;
    }
    return kMbRequestTypeInvalidation;
}

const mbSlist* MB_CALL_TYPE mbNetGetRawHttpHeadInBlinkThread(mbNetJob jobPtr)
{
    if (content::ThreadCall::isBlinkThread())
        checkThreadCallIsValid(__FUNCTION__);
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    if (!job->m_initializeHandleInfo)
        return nullptr;
    return (const mbSlist*)job->m_initializeHandleInfo->headers;
}

class HTTPHeaderVisitor : public blink::WebHTTPHeaderVisitor {
public:
    HTTPHeaderVisitor(curl_slist** result)
    {
        m_result = result;
    }

    virtual void VisitHeader(const blink::WebString& name, const blink::WebString& value) override
    {
        *m_result = curl_slist_append(*m_result, name.Utf8().c_str());
        *m_result = curl_slist_append(*m_result, value.Utf8().c_str());
    }

private:
    curl_slist** m_result;
};

const mbSlist* MB_CALL_TYPE mbNetGetRawResponseHeadInBlinkThread(mbNetJob jobPtr)
{
    if (content::ThreadCall::isBlinkThread()) {
        mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
        mbSlist* result = nullptr;
        HTTPHeaderVisitor visitor((curl_slist**)&result);
        job->m_response.VisitHttpHeaderFields(&visitor);

        content::ThreadCall::callBlinkThreadAsync(FROM_HERE, [result] {
            curl_slist_free_all((curl_slist*)result);
        });

        return result;
    }
    return nullptr;
}

mbPostBodyElements* MB_CALL_TYPE mbNetCreatePostBodyElements(mbWebView webView, size_t length)
{
    checkThreadCallIsValid(__FUNCTION__);
    if (0 == length)
        return nullptr;

    mbPostBodyElements* result = new mbPostBodyElements();
    result->size = sizeof(mbPostBodyElements);
    result->isDirty = true;

    size_t allocLength = sizeof(mbPostBodyElement*) * length;
    result->element = (mbPostBodyElement**)malloc(allocLength);
    memset(result->element, 0, allocLength);

    result->elementSize = length;

    return result;
}

struct mbString {
    mbString(const utf8* utfString, size_t length)
    {
        str = utfString;
        len = length;
        freeStrFunc = nullptr;
    }

    ~mbString()
    {
        if (freeStrFunc)
            freeStrFunc((utf8*)str, len);
    }

    static void defaultFreeStr(utf8* str, size_t len)
    {
        free(str);
    }

    const utf8* str;
    size_t len;
    void (*freeStrFunc)(utf8* str, size_t len);
};

mbStringPtr MB_CALL_TYPE mbCreateString(const utf8* str, size_t len)
{
    mbStringPtr mbStr = new mbString(str, len);
    return mbStr;
}

mbStringPtr MB_CALL_TYPE mbCreateStringWithCopy(const utf8* str, size_t len)
{
    if (!str || 0 == len)
        return nullptr;

    utf8* strCopy = (utf8*)malloc(len + 1);
    memcpy(strCopy, str, len);
    strCopy[len] = 0;

    mbStringPtr mbStr = new mbString(strCopy, len);
    mbStr->freeStrFunc = mbString::defaultFreeStr;
    return mbStr;
}

mbStringPtr MB_CALL_TYPE mbCreateStringWithoutNullTermination(const utf8* str, size_t len)
{
    return mbCreateStringWithCopy(str, len);
}

void MB_CALL_TYPE mbDeleteString(mbStringPtr str)
{
    delete str;
}

const utf8* MB_CALL_TYPE mbGetString(mbStringPtr s)
{
    return s ? s->str : "";
}

size_t MB_CALL_TYPE mbGetStringLen(mbStringPtr s)
{
    return s ? s->len : 0;
}

void MB_CALL_TYPE mbNetFreePostBodyElement(mbPostBodyElement* element)
{
    mbFreeMemBuf(element->data);
    mbDeleteString(element->filePath);
    delete element;
}

mbPostBodyElement* MB_CALL_TYPE mbNetCreatePostBodyElement(mbWebView webView)
{
    checkThreadCallIsValid(__FUNCTION__);
    mbPostBodyElement* wkeElement = new mbPostBodyElement();
    wkeElement->size = sizeof(mbPostBodyElement);
    return wkeElement;
}

static mbPostBodyElements* flattenHTTPBodyElementToWke(const std::vector<mbnet::FlattenHTTPBodyElement*>& body)
{
    if (0 == body.size())
        return nullptr;

    mbPostBodyElements* result = mbNetCreatePostBodyElements(NULL_WEBVIEW, body.size());
    result->isDirty = false;
    for (size_t i = 0; i < result->elementSize; ++i) {
        mbPostBodyElement* wkeElement = mbNetCreatePostBodyElement(NULL_WEBVIEW);
        result->element[i] = wkeElement;
        const mbnet::FlattenHTTPBodyElement* element = body[i];

        if (mbnet::FlattenHTTPBodyElement::TypeFile == element->type
            /*|| mbnet::FlattenHTTPBodyElement::TypeFileSystemURL == element->type*/) {

            wkeElement->type = mbHttBodyElementTypeFile;

            std::string filePathUtf8;
            base::UTF16ToUTF8(element->filePath.c_str(), element->filePath.size(), &filePathUtf8);

            wkeElement->filePath = mbCreateString(filePathUtf8.c_str(), filePathUtf8.size());
            wkeElement->fileLength = element->fileLength;
            wkeElement->fileStart = element->fileStart;
            wkeElement->data = nullptr;
        } else {
            wkeElement->type = mbHttBodyElementTypeData;
            wkeElement->filePath = nullptr;
            wkeElement->fileLength = 0;
            wkeElement->fileStart = 0;
            wkeElement->data = mbCreateMemBuf(NULL_WEBVIEW, (void*)element->data.data(), element->data.size());
        }
    }
    return result;
}

mbPostBodyElements* MB_CALL_TYPE mbNetGetPostBody(void* jobPtr)
{
    checkThreadCallIsValid(__FUNCTION__);
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    mbnet::InitializeHandleInfo* info = job->m_initializeHandleInfo;
    if (!info)
        return nullptr;

    std::vector<mbnet::FlattenHTTPBodyElement*>* flattenElements = nullptr;
    if ("POST" == info->method) {
        if (!info->methodInfo || !info->methodInfo->post || !info->methodInfo->post->data)
            return nullptr;
        flattenElements = &(info->methodInfo->post->data->flattenElements);
    } else if ("PUT" == info->method) {
        if (!info->methodInfo || !info->methodInfo->put || !info->methodInfo->put->data)
            return nullptr;
        flattenElements = &info->methodInfo->put->data->flattenElements;
    }
    if (!flattenElements)
        return nullptr;

    mbPostBodyElements* postBody = flattenHTTPBodyElementToWke(*flattenElements);
    return postBody;
}

static void netFreePostBodyElements(mbPostBodyElements* elements)
{
    checkThreadCallIsValid(__FUNCTION__);
    for (size_t i = 0; i < elements->elementSize; ++i) {
        mbNetFreePostBodyElement(elements->element[i]);
    }
    free(elements->element);
    delete elements;
}

void MB_CALL_TYPE mbNetFreePostBodyElements(mbPostBodyElements* elements)
{
    netFreePostBodyElements(elements);
}

struct mbWebUrlResponse {
    mbWebUrlResponse(const blink::WebURLResponse& response)
    {
        m_response = response;
    }
    blink::WebURLResponse m_response;
};

struct mbWebUrlRequest {
    std::string url;
    std::string method;
    std::string mime;
    std::vector<std::pair<std::string, std::string>> headers;
    int id = 0;
    std::atomic_bool cancelled { false };
    std::atomic_bool responseSent { false };
};

namespace {

std::atomic_int g_nextUrlRequestId { 1 };
std::mutex g_urlRequestMutex;
std::unordered_map<int, mbWebUrlRequest*> g_urlRequests;
std::once_flag g_curlInitOnce;

struct MbWebsocketCallbackState {
    mbWebsocketHookCallbacks callbacks {};
    void* param = nullptr;
    bool hasCallbacks = false;
};

struct MbResPacketState {
    std::u16string path;
    bool enabled = false;
};

struct MbUrlRequestContext {
    mbWebView webviewHandle;
    mbWebUrlRequest* request;
    void* param;
    mbUrlRequestCallbacks callbacks;
    CURL* curl;
};

std::mutex& mbWebsocketCallbackLock()
{
    static std::mutex* lock = new std::mutex();
    return *lock;
}

std::unordered_map<int64_t, MbWebsocketCallbackState>& mbWebsocketCallbacksByView()
{
    static std::unordered_map<int64_t, MbWebsocketCallbackState>* callbacks = new std::unordered_map<int64_t, MbWebsocketCallbackState>();
    return *callbacks;
}

std::mutex& mbResPacketStateLock()
{
    static std::mutex* lock = new std::mutex();
    return *lock;
}

std::unordered_map<int64_t, MbResPacketState>& mbResPacketStatesByView()
{
    static std::unordered_map<int64_t, MbResPacketState>* states = new std::unordered_map<int64_t, MbResPacketState>();
    return *states;
}

void ensureCurlInitialized()
{
    std::call_once(g_curlInitOnce, [] { curl_global_init(CURL_GLOBAL_ALL); });
}

mbWebUrlResponse* createMbUrlResponse(CURL* curl, const mbWebUrlRequest* request)
{
    char* effectiveUrl = nullptr;
    long statusCode = 0;
    curl_off_t contentLength = -1;
    char* contentType = nullptr;

    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength);
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);

    blink::WebURLResponse response;
    response.SetCurrentRequestUrl(blink::KURL(effectiveUrl ? effectiveUrl : request->url.c_str()));
    response.SetHttpStatusCode(static_cast<int>(statusCode));
    if (contentLength >= 0)
        response.SetExpectedContentLength(static_cast<int64_t>(contentLength));
    if (contentType && contentType[0])
        response.SetMimeType(blink::WebString::FromUTF8(contentType));
    else if (!request->mime.empty())
        response.SetMimeType(blink::WebString::FromUTF8(request->mime));

    return new mbWebUrlResponse(response);
}

void maybeSendUrlResponse(CURL* curl, mbWebView webviewHandle, mbWebUrlRequest* request, void* param, const mbUrlRequestCallbacks& callbacks)
{
    bool expected = false;
    if (!request->responseSent.compare_exchange_strong(expected, true))
        return;

    if (!callbacks.didReceiveResponseCallback)
        return;

    mbWebUrlResponse* response = createMbUrlResponse(curl, request);
    callbacks.didReceiveResponseCallback(webviewHandle, param, request, response);
    delete response;
}

size_t mbUrlRequestWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    MbUrlRequestContext* context = static_cast<MbUrlRequestContext*>(userdata);
    size_t length = size * nmemb;
    if (!context || !context->request || context->request->cancelled.load())
        return 0;

    maybeSendUrlResponse(context->curl, context->webviewHandle, context->request, context->param, context->callbacks);
    if (context->callbacks.didReceiveDataCallback && length > 0)
        context->callbacks.didReceiveDataCallback(context->webviewHandle, context->param, context->request, ptr, static_cast<int>(length));
    return length;
}

int mbUrlRequestProgressCallback(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    mbWebUrlRequest* request = static_cast<mbWebUrlRequest*>(userdata);
    return request && request->cancelled.load() ? 1 : 0;
}

void removeUrlRequest(int requestId)
{
    std::lock_guard<std::mutex> lock(g_urlRequestMutex);
    g_urlRequests.erase(requestId);
}

} // namespace

mbWebUrlRequestPtr MB_CALL_TYPE mbNetCreateWebUrlRequest(const utf8* url, const utf8* method, const utf8* mime)
{
    mbWebUrlRequest* request = new mbWebUrlRequest();
    request->url = url ? url : "";
    request->method = (method && method[0]) ? method : "GET";
    request->mime = mime ? mime : "";
    return request;
}

void MB_CALL_TYPE mbNetAddHTTPHeaderFieldToUrlRequest(mbWebUrlRequestPtr request, const utf8* name, const utf8* value)
{
    if (!request || !name)
        return;
    request->headers.push_back(std::make_pair(std::string(name), std::string(value ? value : "")));
}

int MB_CALL_TYPE mbNetStartUrlRequest(mbWebView webviewHandle, mbWebUrlRequestPtr request, void* param, const mbUrlRequestCallbacks* callbacks)
{
    if (!request || request->url.empty()) {
        if (callbacks && callbacks->didFailCallback)
            callbacks->didFailCallback(webviewHandle, param, request, "invalid request");
        delete request;
        return 0;
    }

    ensureCurlInitialized();
    mbUrlRequestCallbacks callbacksCopy = {};
    if (callbacks)
        callbacksCopy = *callbacks;

    int requestId = g_nextUrlRequestId.fetch_add(1);
    request->id = requestId;
    {
        std::lock_guard<std::mutex> lock(g_urlRequestMutex);
        g_urlRequests[requestId] = request;
    }

    std::thread([webviewHandle, request, param, callbacksCopy, requestId] {
        CURL* curl = curl_easy_init();
        if (!curl) {
            if (callbacksCopy.didFailCallback)
                callbacksCopy.didFailCallback(webviewHandle, param, request, "curl_easy_init failed");
            removeUrlRequest(requestId);
            delete request;
            return;
        }

        MbUrlRequestContext context = { webviewHandle, request, param, callbacksCopy, curl };
        char errorBuffer[CURL_ERROR_SIZE] = { 0 };
        curl_slist* headerList = nullptr;
        for (const auto& header : request->headers) {
            std::string line = header.first + ": " + header.second;
            headerList = curl_slist_append(headerList, line.c_str());
        }
        if (!request->mime.empty()) {
            std::string contentType = "Content-Type: " + request->mime;
            headerList = curl_slist_append(headerList, contentType.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, request->url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mbUrlRequestWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, mbUrlRequestProgressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, request);
        if (headerList)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        if (request->method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        } else if (request->method == "HEAD") {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        } else if (request->method != "GET") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request->method.c_str());
        }

        CURLcode code = curl_easy_perform(curl);
        if (!request->cancelled.load() && code == CURLE_OK) {
            maybeSendUrlResponse(curl, webviewHandle, request, param, callbacksCopy);
            if (callbacksCopy.didFinishLoadingCallback)
                callbacksCopy.didFinishLoadingCallback(webviewHandle, param, request, 0);
        } else if (callbacksCopy.didFailCallback) {
            const char* error = request->cancelled.load() ? "cancelled" : (errorBuffer[0] ? errorBuffer : curl_easy_strerror(code));
            callbacksCopy.didFailCallback(webviewHandle, param, request, error);
        }

        if (headerList)
            curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
        removeUrlRequest(requestId);
        delete request;
    }).detach();

    return requestId;
}

int MB_CALL_TYPE mbNetGetHttpStatusCode(mbWebUrlResponsePtr response)
{
    checkThreadCallIsValid(__FUNCTION__);
    return response->m_response.HttpStatusCode();
}

long long MB_CALL_TYPE mbNetGetExpectedContentLength(mbWebUrlResponsePtr response)
{
    return response->m_response.ExpectedContentLength();
}

const utf8* MB_CALL_TYPE mbNetGetResponseUrl(mbWebUrlResponsePtr response)
{
    checkThreadCallIsValid(__FUNCTION__);
    blink::KURL kurl = response->m_response.ResponseUrl();
    String url = kurl;
    std::string urlStr = url.Utf8();
    return createTempCharString(urlStr.c_str(), urlStr.size());
}

void MB_CALL_TYPE mbNetCancelWebUrlRequest(int requestId)
{
    std::lock_guard<std::mutex> lock(g_urlRequestMutex);
    auto it = g_urlRequests.find(requestId);
    if (it != g_urlRequests.end() && it->second)
        it->second->cancelled.store(true);
}

void MB_CALL_TYPE mbSetViewProxy(mbWebView webviewHandle, const mbProxy* proxy)
{
    mbProxy* proxyCopy = new mbProxy();
    *proxyCopy = *proxy;

    content::ThreadCall::callBlinkThreadAsync(MB_FROM_HERE, [webviewHandle, proxyCopy] {
        content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr(webviewHandle);
    if (webview) {
        webview->setProxy(proxyCopy);
    }
        });
}

const char* MB_CALL_TYPE mbNetGetMIMEType(mbNetJob jobPtr)
{
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    blink::WebString contentType = job->m_response.HttpHeaderField(blink::WebString::FromUTF8("Content-Type"));
    std::string contentTypeUtf8 = contentType.Utf8();
    return createTempCharString(contentTypeUtf8.c_str(), contentTypeUtf8.size());
}

const char* netGetHTTPHeaderField(mbNetJob jobPtr, const char* key)
{
    checkThreadCallIsValid(__FUNCTION__);
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    std::optional<std::string> value = job->firstRequest()->headers.GetHeader(key);
    if (!value.has_value())
        return nullptr;
    return createTempCharString(value->c_str(), value->size());
}

const char* netGetHTTPHeaderFieldFromResponse(mbNetJob jobPtr, const char* key)
{
    checkThreadCallIsValid(__FUNCTION__);
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    blink::WebString value = job->m_response.HttpHeaderField(blink::WebString::FromUTF8(key));
    std::string valueBuffer = value.Utf8();

    return createTempCharString(valueBuffer.c_str(), valueBuffer.size());
}

const utf8* MB_CALL_TYPE mbNetGetHTTPHeaderField(mbNetJob jobPtr, const char* key, BOOL fromRequestOrResponse)
{
    if (fromRequestOrResponse)
        return netGetHTTPHeaderField(jobPtr, key);
    return netGetHTTPHeaderFieldFromResponse(jobPtr, key);
}

void MB_CALL_TYPE mbSetCookie(mbWebView webviewHandle, const utf8* url, const utf8* cookie)
{
    //checkThreadCallIsValid(__FUNCTION__);
    //cookie = "cna22=111111; domain=.1688.com; path=/; expires=Tue, 23-Jan-2029 13:17:21 GMT;";

    content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
    if (!webview)
        return;

    std::string* urlString = new std::string(url);
    std::string* cookieString = new std::string(cookie);

    content::ThreadCall::callBlinkThreadAsync(MB_FROM_HERE, [webviewHandle, urlString, cookieString] {
        content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
        if (webview) {
            webview->setCookie(*urlString, *cookieString);
        }

        OutputDebugStringA("mbSetCookie:");
        OutputDebugStringA(cookieString->c_str());
        OutputDebugStringA("\n");
        delete urlString;
        delete cookieString;
    });
}

void MB_CALL_TYPE mbGetCookie(mbWebView webviewHandle, mbGetCookieCallback callback, void* param)
{
    checkThreadCallIsValid(__FUNCTION__);
    if (!callback)
        return;

    content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
    if (!webview) {
        callback(NULL_WEBVIEW, param, kMbAsynRequestStateFail, nullptr);
        return;
    }

    content::ThreadCall::callBlinkThreadAsync(MB_FROM_HERE, [webviewHandle, callback, param] {
        std::string* cookie = nullptr;
        content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
        if (webview) {
            cookie = new std::string(webview->getCookie());
        } else
            cookie = new std::string("");

        content::ThreadCall::callUiThreadAsync(MB_FROM_HERE, [webviewHandle, callback, param, cookie] {
            content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
            if (!webview) {
                callback(NULL_WEBVIEW, param, kMbAsynRequestStateFail, nullptr);
                delete cookie;
                return;
            }
            callback(webviewHandle, param, kMbAsynRequestStateOk, cookie->c_str());
            delete cookie;
        });
    });
}

const utf8* MB_CALL_TYPE mbGetCookieOnBlinkThread(mbWebView webviewHandle)
{
    content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
    if (!webview)
        return NULL;
    std::string cookie = webview->getCookie();
    return createTempCharString(cookie.c_str(), cookie.size());
}

static BOOL netHoldJobToAsynCommit(mbNetJob jobPtr)
{
    checkThreadCallIsValid(__FUNCTION__);
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    if (job->m_isRedirection || job->m_isSynchronous || job->m_isHoldJobToAsynCommit)
        return FALSE;

    job->m_isWkeNetSetDataBeSetted = false;
    if (job->m_asynWkeNetSetData)
        delete job->m_asynWkeNetSetData;
    job->m_asynWkeNetSetData = nullptr;

    if (job->m_hookBufForEndHook)
        delete job->m_hookBufForEndHook;
    job->m_hookBufForEndHook = nullptr;
    job->m_isHookRequest &= (~((unsigned int)1));
    job->m_isHoldJobToAsynCommit = true;

    return TRUE;
}

void MB_CALL_TYPE mbNetHoldJobToAsynCommit(mbNetJob jobPtr)
{
    if (content::ThreadCall::isBlinkThread()) {
        netHoldJobToAsynCommit(jobPtr);
    } else {
        content::ThreadCall::callBlinkThreadAsync(MB_FROM_HERE, [jobPtr] { netHoldJobToAsynCommit(jobPtr); });
    }
}

static void netContinueJob(mbNetJob jobPtr)
{
    checkThreadCallIsValid(__FUNCTION__);
    mbnet::WebURLLoaderInternal* job = (mbnet::WebURLLoaderInternal*)jobPtr;
    mbnet::WebURLLoaderManager::sharedInstance()->continueJob(job);
}

void MB_CALL_TYPE mbNetContinueJob(mbNetJob jobPtr)
{
    if (content::ThreadCall::isBlinkThread()) {
        netContinueJob(jobPtr);
    } else {
        content::ThreadCall::callBlinkThreadAsync(MB_FROM_HERE, [jobPtr] { netContinueJob(jobPtr); });
    }
}

static void setCookieJarFullPathImpl(mbWebView webView, const std::u16string& path)
{
    std::string jarPathA = base::UTF16ToUTF8(std::u16string_view(path));
    mbnet::WebURLLoaderManager::setCookieJarFullPath(jarPathA.c_str());
}

static void setLocalStorageFullPathImpl(mbWebView webView, const std::u16string& path)
{
    std::string pathA = base::UTF16ToUTF8(path);
    mbnet::setDefaultLocalStorageDir(pathA);
}

static void setFullPathOnBlinkThread(mbWebView webviewHandle, std::u16string* pathString, bool isCookiePath)
{
    if (!pathString)
        return;

    if (!webviewHandle) {
        isCookiePath ? setCookieJarFullPathImpl(NULL_WEBVIEW, *pathString) : setLocalStorageFullPathImpl(NULL_WEBVIEW, *pathString);
    } else {
        content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
        if (webview) {
            std::string pathUtf8 = base::UTF16ToUTF8(pathString->c_str());
            //wkeSetDebugConfig(webview->getWkeWebView(), isCookiePath ? "setCookieJarFullPath" : "setLocalStorageFullPath", pathUtf8.c_str());
            if (isCookiePath)
                webview->setCookieJarFullPath(pathUtf8.c_str());
            else
                webview->setLocalStorageFullPath(pathUtf8.c_str());
        }
    }
    delete pathString;
}

void setFullPath(mbWebView webviewHandle, const WCHAR* path, bool isCookiePath)
{
    if (!path)
        return;
    std::u16string* pathString = new std::u16string((const char16_t*)path);
    if (0 == pathString->size()) {
        delete pathString;
        return;
    }

    if (content::ThreadCall::isBlinkThread()) {
        setFullPathOnBlinkThread(webviewHandle, pathString, isCookiePath);
    } else {
        content::ThreadCall::callBlinkThreadAsync(
            MB_FROM_HERE, [webviewHandle, pathString, isCookiePath] { setFullPathOnBlinkThread(webviewHandle, pathString, isCookiePath); });
    }
}

void MB_CALL_TYPE mbNetOnResponse(mbWebView webviewHandle, mbNetResponseCallback callback, void* param)
{
    checkThreadCallIsValid(__FUNCTION__);
    content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
    if (!webview)
        return;
    webview->getClosure().setNetResponseCallback(callback, param);
}

void MB_CALL_TYPE mbNetSetWebsocketCallback(mbWebView webviewHandle, const mbWebsocketHookCallbacks* callbacks, void* param)
{
    checkThreadCallIsValid(__FUNCTION__);
    content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
    if (!webview)
        return;

    std::lock_guard<std::mutex> lock(mbWebsocketCallbackLock());
    MbWebsocketCallbackState& state = mbWebsocketCallbacksByView()[(int64_t)webviewHandle];
    if (!callbacks) {
        state = MbWebsocketCallbackState();
        return;
    }
    state.callbacks = *callbacks;
    state.param = param;
    state.hasCallbacks = true;
}

void MB_CALL_TYPE mbNetSendWsText(void* channel, const char* buf, size_t len)
{
    if (!channel || !buf || !len)
        return;
    mbnet::WebSocketChannelCurl* channelPtr = (mbnet::WebSocketChannelCurl*)channel;
    channelPtr->send(buf, len > INT_MAX ? INT_MAX : (int)len, true);
}

void MB_CALL_TYPE mbNetSendWsBlob(void* channel, const char* buf, size_t len)
{
    if (!channel || !buf || !len)
        return;
    mbnet::WebSocketChannelCurl* channelPtr = (mbnet::WebSocketChannelCurl*)channel;
    channelPtr->send(buf, len > INT_MAX ? INT_MAX : (int)len, true);
}

const utf8* MB_CALL_TYPE mbUtilBase64Encode(const utf8* str)
{
    if (!str)
        return nullptr;

    std::string result = base::Base64Encode(str);
    return createTempCharString(result.c_str(), result.size());
}

const utf8* MB_CALL_TYPE mbUtilBase64Decode(const utf8* str)
{
    if (!str)
        return nullptr;

    std::string result;
    if (!base::Base64Decode(str, &result))
        return nullptr;
    return createTempCharString(result.c_str(), result.size());
}

void MB_CALL_TYPE mbNetEnableResPacket(mbWebView webviewHandle, const WCHAR* pathName)
{
    checkThreadCallIsValid(__FUNCTION__);
    content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
    if (!webview)
        return;

    std::lock_guard<std::mutex> lock(mbResPacketStateLock());
    MbResPacketState& state = mbResPacketStatesByView()[(int64_t)webviewHandle];
    if (!pathName || !pathName[0]) {
        state.path.clear();
        state.enabled = false;
        return;
    }
    state.path = std::u16string((const char16_t*)pathName);
    state.enabled = true;
}

void MB_CALL_TYPE mbOnNavigationSync(mbWebView webviewHandle, mbNavigationCallback callback, void* param)
{
    checkThreadCallIsValid(__FUNCTION__);
    content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
    if (!webview)
        return;
    webview->getClosure().setNavigationSyncCallback(callback, param);
}

void MB_CALL_TYPE mbOnNetGetFavicon(mbWebView webviewHandle, mbNetGetFaviconCallback callback, void* param)
{
    checkThreadCallIsValid(__FUNCTION__);
    content::MbWebView* webview = (content::MbWebView*)common::LiveIdDetect::getMbWebviewIds()->getPtr((int64_t)webviewHandle);
    if (!webview)
        return;
    webview->getClosure().setNetGetFaviconCallback(callback, param);
}
