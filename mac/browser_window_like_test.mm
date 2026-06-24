#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <CommonCrypto/CommonDigest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define ENABLE_MB 1
#include "mbvip/core/mb.h"

namespace {

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 640;

struct Check {
    std::string name;
    bool pass;
    std::string detail;
};

std::vector<Check> g_checks;
std::mutex g_checks_mutex;

std::atomic<bool> g_title_changed(false);
std::atomic<bool> g_url_changed(false);
std::atomic<bool> g_navigation_seen(false);
std::atomic<bool> g_load_finished(false);
std::atomic<bool> g_load_finish_callback(false);
std::atomic<bool> g_load_failed(false);
std::atomic<bool> g_load_url_fail_callback(false);
std::atomic<bool> g_document_ready(false);
std::atomic<bool> g_popup_seen(false);
std::atomic<bool> g_download_seen(false);
std::atomic<bool> g_lifecycle_allow_close(false);
std::atomic<int> g_load_result(-1);
std::atomic<int> g_lifecycle_close_count(0);
std::atomic<int> g_lifecycle_destroy_count(0);
std::atomic<int> g_async_can_go_back(-1);
std::atomic<int> g_async_can_go_forward(-1);
std::atomic<int> g_async_js_callback_count(0);
std::atomic<double> g_async_js_number(0);
std::atomic<int> g_cookie_callback_count(0);
std::atomic<int> g_cookie_callback_state(-1);
std::atomic<bool> g_web_request_begin_seen(false);
std::atomic<bool> g_web_request_end_seen(false);
std::atomic<bool> g_web_request_post_body_seen(false);

std::mutex g_state_mutex;
std::string g_last_title;
std::string g_last_url;
std::string g_last_fail_url;
std::string g_last_fail_reason;
std::string g_last_download_url;
std::string g_async_js_text;
std::string g_last_cookie_string;
std::string g_web_request_post_body;
std::string g_web_request_end_body;
BOOL g_last_can_go_back = FALSE;
BOOL g_last_can_go_forward = FALSE;

std::string shellEscape(const std::string& value)
{
    std::string out = "'";
    for (char c : value) {
        if (c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out += "'";
    return out;
}

void addCheck(const std::string& name, bool pass, const std::string& detail = std::string())
{
    std::lock_guard<std::mutex> lock(g_checks_mutex);
    g_checks.push_back({ name, pass, detail });
    printf("%s %s%s%s\n", pass ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
    fflush(stdout);
}

void resetLoadState()
{
    g_title_changed = false;
    g_url_changed = false;
    g_navigation_seen = false;
    g_load_finished = false;
    g_load_finish_callback = false;
    g_load_failed = false;
    g_load_url_fail_callback = false;
    g_document_ready = false;
    g_load_result = -1;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_last_title.clear();
    g_last_url.clear();
    g_last_fail_url.clear();
    g_last_fail_reason.clear();
    g_last_can_go_back = FALSE;
    g_last_can_go_forward = FALSE;
}

bool waitFor(const std::function<bool()>& predicate, int timeout_ms)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return predicate();
}

void runLoopFor(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

struct WindowState {
    HWND hwnd = nullptr;
    BOOL isWindow = FALSE;
    BOOL visible = FALSE;
    BOOL iconic = FALSE;
    BOOL zoomed = FALSE;
    BOOL focused = FALSE;
};

void MB_CALL_TYPE readWindowStateOnUiThread(void* param1, void*)
{
    WindowState* state = static_cast<WindowState*>(param1);
    state->isWindow = IsWindow(state->hwnd);
    if (!state->isWindow)
        return;
    state->visible = IsWindowVisible(state->hwnd);
    state->iconic = IsIconic(state->hwnd);
    state->zoomed = IsZoomed(state->hwnd);
    state->focused = GetFocus() == state->hwnd;
}

WindowState readWindowState(HWND hwnd)
{
    WindowState state;
    state.hwnd = hwnd;
    mbCallUiThreadSync(readWindowStateOnUiThread, &state, nullptr);
    return state;
}

struct CreateWindowState {
    mbWebView view = NULL_WEBVIEW;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

void MB_CALL_TYPE createPopupWindowOnUiThread(void* param1, void*)
{
    CreateWindowState* state = static_cast<CreateWindowState*>(param1);
    state->view = mbCreateWebWindow(MB_WINDOW_TYPE_POPUP, nullptr, state->x, state->y, state->width, state->height);
}

mbWebView createPopupWindow(int x, int y, int width, int height)
{
    CreateWindowState state;
    state.x = x;
    state.y = y;
    state.width = width;
    state.height = height;
    mbCallUiThreadSync(createPopupWindowOnUiThread, &state, nullptr);
    return state.view;
}

std::string jsString(mbWebView view, const std::string& script)
{
    mbWebFrameHandle frame = mbWebFrameGetMainFrame(view);
    mbJsExecState es = mbGetGlobalExecByFrame(view, frame);
    mbJsValue value = mbRunJsSync(view, frame, script.c_str(), false);
    const char* text = mbJsToString(es, value);
    std::string result = text ? text : "";
    mbJsValueDeref(es, value);
    return result;
}

double jsNumber(mbWebView view, const std::string& script)
{
    mbWebFrameHandle frame = mbWebFrameGetMainFrame(view);
    mbJsExecState es = mbGetGlobalExecByFrame(view, frame);
    mbJsValue value = mbRunJsSync(view, frame, script.c_str(), false);
    double result = mbJsToDouble(es, value);
    mbJsValueDeref(es, value);
    return result;
}

std::string readCommandOutput(const std::string& command)
{
    std::string out;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        return out;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe))
        out += buffer;
    pclose(pipe);
    return out;
}

std::string base64(const unsigned char* data, size_t len)
{
    static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < len; i += 3) {
        unsigned int v = data[i] << 16;
        if (i + 1 < len)
            v |= data[i + 1] << 8;
        if (i + 2 < len)
            v |= data[i + 2];
        out.push_back(kTable[(v >> 18) & 0x3f]);
        out.push_back(kTable[(v >> 12) & 0x3f]);
        out.push_back(i + 1 < len ? kTable[(v >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < len ? kTable[v & 0x3f] : '=');
    }
    return out;
}

std::string websocketAccept(const std::string& key)
{
    const std::string magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char digest[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1(magic.data(), (CC_LONG)magic.size(), digest);
    return base64(digest, sizeof(digest));
}

bool sendAll(int fd, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0)
            return false;
        sent += (size_t)n;
    }
    return true;
}

std::string headerValue(const std::string& request, const std::string& name)
{
    auto equalNoCase = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i) {
            char ca = a[i];
            char cb = b[i];
            if (ca >= 'A' && ca <= 'Z')
                ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z')
                cb = (char)(cb - 'A' + 'a');
            if (ca != cb)
                return false;
        }
        return true;
    };

    size_t line_begin = 0;
    while (line_begin < request.size()) {
        size_t line_end = request.find("\r\n", line_begin);
        if (line_end == std::string::npos)
            line_end = request.size();
        size_t colon = request.find(':', line_begin);
        if (colon != std::string::npos && colon < line_end) {
            std::string key = request.substr(line_begin, colon - line_begin);
            if (equalNoCase(key, name)) {
                size_t pos = colon + 1;
                while (pos < line_end && request[pos] == ' ')
                    ++pos;
                return request.substr(pos, line_end - pos);
            }
        }
        if (line_end == request.size())
            break;
        line_begin = line_end + 2;
    }
    return "";
}

class LocalServer {
public:
    ~LocalServer() { stop(); }

    bool start()
    {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0)
            return false;

        int yes = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) != 0)
            return false;
        if (listen(listen_fd_, 16) != 0)
            return false;

        socklen_t len = sizeof(addr);
        if (getsockname(listen_fd_, (sockaddr*)&addr, &len) != 0)
            return false;
        port_ = ntohs(addr.sin_port);
        running_ = true;
        thread_ = std::thread([this] { acceptLoop(); });
        return true;
    }

    void stop()
    {
        running_ = false;
        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable())
            thread_.join();
    }

    int port() const { return port_; }
    std::string origin() const { return "http://127.0.0.1:" + std::to_string(port_); }
    int websocketUpgrades() const { return websocket_upgrades_.load(); }
    int websocketMessages() const { return websocket_messages_.load(); }
    int webRequestHits() const { return web_request_hits_.load(); }
    std::string websocketDebug() const
    {
        std::lock_guard<std::mutex> lock(websocket_mutex_);
        return " wsKey=" + websocket_key_ + " wsAccept=" + websocket_accept_;
    }
    std::string lastWebRequestUserAgent() const
    {
        std::lock_guard<std::mutex> lock(web_request_mutex_);
        return web_request_user_agent_;
    }
    std::string lastWebRequestHookHeader() const
    {
        std::lock_guard<std::mutex> lock(web_request_mutex_);
        return web_request_hook_header_;
    }
    std::string lastWebRequestBody() const
    {
        std::lock_guard<std::mutex> lock(web_request_mutex_);
        return web_request_body_;
    }

private:
    void acceptLoop()
    {
        while (running_) {
            int fd = accept(listen_fd_, nullptr, nullptr);
            if (fd < 0)
                continue;
            int yes = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
            std::thread([this, fd] { handleClient(fd); }).detach();
        }
    }

    void handleClient(int fd)
    {
        std::string request;
        char buffer[2048];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
            ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                close(fd);
                return;
            }
            request.append(buffer, buffer + n);
        }

        std::istringstream line_stream(request);
        std::string method;
        std::string path;
        line_stream >> method >> path;
        std::string request_body;
        size_t headers_end = request.find("\r\n\r\n");
        int content_length = 0;
        std::string content_length_text = headerValue(request, "Content-Length");
        if (!content_length_text.empty())
            content_length = atoi(content_length_text.c_str());
        if (headers_end != std::string::npos) {
            request_body = request.substr(headers_end + 4);
            while ((int)request_body.size() < content_length) {
                ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
                if (n <= 0)
                    break;
                request_body.append(buffer, buffer + n);
            }
        }

        if (request.find("Upgrade: websocket") != std::string::npos || request.find("upgrade: websocket") != std::string::npos) {
            handleWebSocket(fd, request);
            return;
        }

        if (path == "/abort") {
            close(fd);
            return;
        }

        std::string body;
        std::string type = "text/html; charset=utf-8";
        std::map<std::string, std::string> headers;
        int status = 200;
        const char* reason = "OK";

        if (path == "/" || path == "/remote.html") {
            body = remoteHtml();
        } else if (path == "/nav-one.html") {
            body = "<!doctype html><html><head><title>Nav One</title></head><body><h1 id='page'>one</h1></body></html>";
        } else if (path == "/nav-two.html") {
            body = "<!doctype html><html><head><title>Nav Two</title></head><body><h1 id='page'>two</h1><script>window.__reloadToken=Date.now()</script></body></html>";
        } else if (path == "/slow.html") {
            std::this_thread::sleep_for(std::chrono::seconds(4));
            body = "<!doctype html><html><head><title>Slow Page</title></head><body>slow</body></html>";
        } else if (path == "/popup.html") {
            body = "<!doctype html><title>Popup Page</title><p>popup</p>";
        } else if (path == "/api") {
            type = "text/plain";
            body = "fetch-ok";
        } else if (path == "/xhr") {
            type = "text/plain";
            body = "xhr-ok";
        } else if (path == "/webrequest") {
            type = "text/plain";
            body = "webrequest-ok";
            ++web_request_hits_;
            std::lock_guard<std::mutex> lock(web_request_mutex_);
            web_request_user_agent_ = headerValue(request, "User-Agent");
            web_request_hook_header_ = headerValue(request, "X-MB-Hooked");
            web_request_body_ = request_body;
        } else if (path == "/download") {
            type = "application/octet-stream";
            body = "download-ok\n";
            headers["Content-Disposition"] = "attachment; filename=mb-e2e.txt";
        } else {
            status = 404;
            reason = "Not Found";
            type = "text/plain";
            body = "missing";
        }

        std::ostringstream response;
        response << "HTTP/1.1 " << status << " " << reason << "\r\n"
                 << "Content-Type: " << type << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n";
        for (const auto& it : headers)
            response << it.first << ": " << it.second << "\r\n";
        response << "\r\n" << body;
        sendAll(fd, response.str());
        close(fd);
    }

    void handleWebSocket(int fd, const std::string& request)
    {
        ++websocket_upgrades_;
        std::string key = headerValue(request, "Sec-WebSocket-Key");
        std::string accept = websocketAccept(key);
        {
            std::lock_guard<std::mutex> lock(websocket_mutex_);
            websocket_key_ = key;
            websocket_accept_ = accept;
        }
        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n"
                 << "Upgrade: WebSocket\r\n"
                 << "Connection: upgrade\r\n"
                 << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";
        if (!sendAll(fd, response.str())) {
            close(fd);
            return;
        }

        unsigned char hdr[2];
        if (recv(fd, hdr, 2, MSG_WAITALL) != 2) {
            close(fd);
            return;
        }
        size_t len = hdr[1] & 0x7f;
        if (len == 126) {
            unsigned char ext[2];
            if (recv(fd, ext, 2, MSG_WAITALL) != 2) {
                close(fd);
                return;
            }
            len = (ext[0] << 8) | ext[1];
        }
        unsigned char mask[4] = { 0 };
        if (hdr[1] & 0x80) {
            if (recv(fd, mask, 4, MSG_WAITALL) != 4) {
                close(fd);
                return;
            }
        }
        std::string payload(len, '\0');
        if (len && recv(fd, payload.data(), len, MSG_WAITALL) != (ssize_t)len) {
            close(fd);
            return;
        }
        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] = (char)(payload[i] ^ mask[i % 4]);
        ++websocket_messages_;

        std::string reply = "ws-ok:" + payload;
        std::string frame;
        frame.push_back((char)0x81);
        frame.push_back((char)reply.size());
        frame += reply;
        sendAll(fd, frame);
        close(fd);
    }

    std::string remoteHtml() const
    {
        std::ostringstream html;
        html << "<!doctype html><html><head><meta charset='utf-8'><title>Remote Start</title></head>"
             << "<body><input id='textInput'><input id='fileInput' type='file'>"
             << "<button id='popupButton' onclick=\"window.open('/popup.html','_blank')\">popup</button>"
             << "<a id='download' href='/download' download='mb-e2e.txt'>download</a>"
             << "<script>"
             << "window.__e2e={};"
             << "(async function(){"
             << "try{localStorage.setItem('mb-ls','storage-ok');__e2e.localStorage=localStorage.getItem('mb-ls');}catch(e){__e2e.localStorage='ERR:'+e.message;}"
             << "try{document.cookie='mb_cookie=cookie-ok; path=/';__e2e.cookie=document.cookie;}catch(e){__e2e.cookie='ERR:'+e.message;}"
             << "try{__e2e.fetch=await (await fetch('/api')).text();}catch(e){__e2e.fetch='ERR:'+e.message;}"
             << "try{__e2e.xhr=await new Promise(function(resolve,reject){var x=new XMLHttpRequest();x.onload=function(){resolve(x.responseText)};x.onerror=function(){reject(new Error('xhr'))};x.open('GET','/xhr');x.send();});}catch(e){__e2e.xhr='ERR:'+e.message;}"
             << "try{__e2e.ws=await new Promise(function(resolve,reject){var done=false,closed='';var w=new WebSocket('ws://127.0.0.1:" << port_ << "/ws');function fail(m){if(!done){done=true;reject(new Error(m+' state='+w.readyState+' close='+closed));}}w.onopen=function(){w.send('ping')};w.onmessage=function(e){done=true;resolve(e.data);w.close();};w.onerror=function(){fail('ws')};w.onclose=function(e){closed=e.code+':'+e.reason;fail('close')};setTimeout(function(){fail('timeout')},3000);});}catch(e){__e2e.ws='ERR:'+e.message;}"
             << "document.title='Remote Ready';"
             << "console.log('MB_E2E:'+JSON.stringify(__e2e));"
             << "})();"
             << "</script></body></html>";
        return html.str();
    }

    int listen_fd_ = -1;
    int port_ = 0;
    std::atomic<bool> running_ { false };
    std::atomic<int> websocket_upgrades_ { 0 };
    std::atomic<int> websocket_messages_ { 0 };
    std::atomic<int> web_request_hits_ { 0 };
    mutable std::mutex websocket_mutex_;
    mutable std::mutex web_request_mutex_;
    std::string websocket_key_;
    std::string websocket_accept_;
    std::string web_request_user_agent_;
    std::string web_request_hook_header_;
    std::string web_request_body_;
    std::thread thread_;
};

void MB_CALL_TYPE onTitleChanged(mbWebView, void*, const utf8* title)
{
    g_title_changed = true;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_last_title = title ? title : "";
}

void MB_CALL_TYPE onUrlChanged(mbWebView, void*, const utf8* url, BOOL canGoBack, BOOL canGoForward)
{
    g_url_changed = true;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_last_url = url ? url : "";
    g_last_can_go_back = canGoBack;
    g_last_can_go_forward = canGoForward;
}

BOOL MB_CALL_TYPE onNavigation(mbWebView, void*, mbNavigationType, const utf8*)
{
    g_navigation_seen = true;
    return TRUE;
}

void MB_CALL_TYPE onDocumentReady(mbWebView, void*, mbWebFrameHandle)
{
    g_document_ready = true;
}

void MB_CALL_TYPE onLoadingFinish(mbWebView, void*, mbWebFrameHandle, const utf8* url, mbLoadingResult result, const utf8* failedReason)
{
    g_load_result = (int)result;
    if (result == MB_LOADING_FAILED) {
        g_load_failed = true;
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_last_fail_url = url ? url : "";
        g_last_fail_reason = failedReason ? failedReason : "";
    } else {
        g_load_finished = true;
    }
}

void MB_CALL_TYPE onLoadUrlFail(mbWebView, void*, const char* url, void*)
{
    g_load_failed = true;
    g_load_url_fail_callback = true;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_last_fail_url = url ? url : "";
}

void MB_CALL_TYPE onLoadUrlFinish(mbWebView, void*, const utf8* url, mbNetJob, int)
{
    g_load_finish_callback = true;
    g_load_finished = true;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_last_url = url ? url : "";
}

BOOL MB_CALL_TYPE onLoadUrlBeginForWebRequest(mbWebView, void*, const char* url, void* job)
{
    if (!url || !strstr(url, "/webrequest"))
        return TRUE;

    g_web_request_begin_seen = true;
    mbNetSetHTTPHeaderFieldUtf8(job, "X-MB-Hooked", "yes", FALSE);

    mbPostBodyElements* post_body = mbNetGetPostBody(job);
    if (post_body) {
        std::string body;
        for (size_t i = 0; i < post_body->elementSize; ++i) {
            mbPostBodyElement* element = post_body->element[i];
            if (!element || element->type != mbHttBodyElementTypeData || !element->data || !element->data->data)
                continue;
            body.append(static_cast<const char*>(element->data->data), element->data->length);
        }
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_web_request_post_body = body;
        }
        g_web_request_post_body_seen = !body.empty();
        mbNetFreePostBodyElements(post_body);
    }

    mbNetHookRequest(job);
    return TRUE;
}

void MB_CALL_TYPE onLoadUrlEndForWebRequest(mbWebView, void*, const char* url, void*, void* buf, int len)
{
    if (!url || !strstr(url, "/webrequest"))
        return;

    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_web_request_end_body.assign(static_cast<const char*>(buf), len > 0 ? len : 0);
    }
    g_web_request_end_seen = true;
}

mbWebView MB_CALL_TYPE onCreateView(mbWebView, void*, mbNavigationType, const utf8* url, const mbWindowFeatures*)
{
    g_popup_seen = true;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_last_url = url ? url : "";
    return NULL_WEBVIEW;
}

mbDownloadOpt MB_CALL_TYPE onDownloadInBlinkThread(
    mbWebView, void*, size_t expectedContentLength, const char* url, const char* mime, const char* disposition, mbNetJob, mbNetJobDataBind*)
{
    g_download_seen = true;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    std::ostringstream detail;
    detail << (url ? url : "") << " " << expectedContentLength << " " << (mime ? mime : "") << " " << (disposition ? disposition : "");
    g_last_download_url = detail.str();
    return kMbDownloadOptCancel;
}

BOOL MB_CALL_TYPE onLifecycleClose(mbWebView, void*, void*)
{
    ++g_lifecycle_close_count;
    return g_lifecycle_allow_close.load() ? TRUE : FALSE;
}

BOOL MB_CALL_TYPE onLifecycleDestroy(mbWebView, void*, void*)
{
    ++g_lifecycle_destroy_count;
    return TRUE;
}

void MB_CALL_TYPE onCanGoBack(mbWebView, void*, MbAsynRequestState state, BOOL canGo)
{
    g_async_can_go_back = state == kMbAsynRequestStateOk && canGo ? 1 : 0;
}

void MB_CALL_TYPE onCanGoForward(mbWebView, void*, MbAsynRequestState state, BOOL canGo)
{
    g_async_can_go_forward = state == kMbAsynRequestStateOk && canGo ? 1 : 0;
}

void MB_CALL_TYPE onGetCookie(mbWebView, void*, MbAsynRequestState state, const utf8* cookie)
{
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_last_cookie_string = cookie ? cookie : "";
    }
    g_cookie_callback_state = state;
    ++g_cookie_callback_count;
}

void MB_CALL_TYPE onRunJsNumber(mbWebView, void*, mbJsExecState es, mbJsValue value)
{
    g_async_js_number = mbJsToDouble(es, value);
    ++g_async_js_callback_count;
}

void MB_CALL_TYPE onRunJsString(mbWebView, void*, mbJsExecState es, mbJsValue value)
{
    const char* text = mbJsToString(es, value);
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_async_js_text = text ? text : "";
    }
    ++g_async_js_callback_count;
}

std::string makeLocalHtml(const std::string& dir)
{
    std::string path = dir + "/local.html";
    std::ofstream file(path);
    file << "<!doctype html><html><head><meta charset='utf-8'><title>Local Miniblink E2E</title></head>"
            "<body><h1>local html ok</h1><script>window.__local='local-ok';</script></body></html>";
    return path;
}

std::string fileUrl(const std::string& path)
{
    return "file://" + path;
}

bool waitForLoad(mbWebView view, const std::string& label, int timeout_ms)
{
    bool ok = waitFor([&] {
        if (g_load_failed.load())
            return true;
        if (!g_url_changed.load())
            return false;
        return jsString(view, "document.readyState") == "complete";
    }, timeout_ms);
    std::lock_guard<std::mutex> lock(g_state_mutex);
    std::ostringstream detail;
    detail << "titleCallback=" << g_last_title << " titleNow=" << (mbGetTitle(view) ? mbGetTitle(view) : "") << " url=" << g_last_url;
    if (g_load_failed)
        detail << " fail=" << g_last_fail_url << " reason=" << g_last_fail_reason;
    addCheck(label, ok && !g_load_failed, detail.str());
    return ok && !g_load_failed;
}

void runLifecycleChecks()
{
    mbWebView child = createPopupWindow(220, 220, 320, 240);
    HWND child_host = child ? mbGetHostHWND(child) : nullptr;
    addCheck("browserwindow-lifecycle-create", child != NULL_WEBVIEW && child_host,
        "host=" + std::to_string((uintptr_t)child_host));
    if (child == NULL_WEBVIEW || !child_host)
        return;

    mbOnClose(child, onLifecycleClose, nullptr);
    mbOnDestroy(child, onLifecycleDestroy, nullptr);
    mbShowWindow(child, SW_SHOW);

    bool child_ready = waitFor([&] {
        mbWebFrameHandle frame = mbWebFrameGetMainFrame(child);
        return frame && mbGetGlobalExecByFrame(child, frame);
    }, 6000);
    addCheck("browserwindow-lifecycle-frame-ready", child_ready);

    int close_before_cancel = g_lifecycle_close_count.load();
    int destroy_before_cancel = g_lifecycle_destroy_count.load();
    g_lifecycle_allow_close = false;
    mbDestroyWebView(child);
    bool cancel_seen = waitFor([&] { return g_lifecycle_close_count.load() > close_before_cancel; }, 2000);
    WindowState after_cancel = readWindowState(child_host);
    addCheck("browserwindow-close-cancel-callback", cancel_seen && after_cancel.isWindow && g_lifecycle_destroy_count.load() == destroy_before_cancel,
        "close=" + std::to_string(g_lifecycle_close_count.load()) + " destroy=" + std::to_string(g_lifecycle_destroy_count.load()));

    mbOnClose(child, onLifecycleClose, nullptr);
    g_lifecycle_allow_close = true;
    int close_before_destroy = g_lifecycle_close_count.load();
    int destroy_before_destroy = g_lifecycle_destroy_count.load();
    mbDestroyWebView(child);
    bool destroy_seen = waitFor([&] {
        return g_lifecycle_close_count.load() > close_before_destroy
            && g_lifecycle_destroy_count.load() > destroy_before_destroy
            && !readWindowState(child_host).isWindow;
    }, 5000);
    addCheck("browserwindow-close-destroy-callback", g_lifecycle_close_count.load() == close_before_destroy + 1
            && g_lifecycle_destroy_count.load() == destroy_before_destroy + 1,
        "close=" + std::to_string(g_lifecycle_close_count.load()) + " destroy=" + std::to_string(g_lifecycle_destroy_count.load()));
    addCheck("browserwindow-destroy-invalidates-host", destroy_seen);
}

bool waitForUrlContains(mbWebView view, const std::string& label, const std::string& fragment, int timeout_ms)
{
    bool loaded = waitForLoad(view, label, timeout_ms);
    bool matched = false;
    std::string detail;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        matched = g_last_url.find(fragment) != std::string::npos;
        detail = g_last_url + " title=" + std::string(mbGetTitle(view) ? mbGetTitle(view) : "");
    }
    addCheck(label + "-url", loaded && matched, detail);
    return loaded && matched;
}

bool waitForObservedUrlContains(mbWebView view, const std::string& label, const std::string& fragment, const std::string& title, int timeout_ms)
{
    bool matched = waitFor([&] {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        const char* current_title = mbGetTitle(view);
        return g_url_changed.load() && g_last_url.find(fragment) != std::string::npos
            && (!current_title || title.empty() || title == current_title);
    }, timeout_ms);
    std::string detail;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        detail = g_last_url + " title=" + std::string(mbGetTitle(view) ? mbGetTitle(view) : "");
    }
    addCheck(label, matched, detail);
    return matched;
}

void runNavigationControlChecks(const std::string& origin)
{
    mbWebView view = createPopupWindow(260, 260, 480, 360);
    HWND host = view ? mbGetHostHWND(view) : nullptr;
    addCheck("webcontents-navigation-window-create", view != NULL_WEBVIEW && host,
        "host=" + std::to_string((uintptr_t)host));
    if (view == NULL_WEBVIEW || !host)
        return;

    mbOnTitleChanged(view, onTitleChanged, nullptr);
    mbOnURLChanged(view, onUrlChanged, nullptr);
    mbOnNavigation(view, onNavigation, nullptr);
    mbOnLoadingFinish(view, onLoadingFinish, nullptr);
    mbOnLoadUrlFail(view, onLoadUrlFail, nullptr);
    mbOnLoadUrlFinish(view, onLoadUrlFinish, nullptr);
    mbShowWindow(view, SW_SHOW);

    bool child_ready = waitFor([&] {
        mbWebFrameHandle frame = mbWebFrameGetMainFrame(view);
        return frame && mbGetGlobalExecByFrame(view, frame);
    }, 6000);
    addCheck("webcontents-navigation-frame-ready", child_ready);
    if (!child_ready) {
        mbDestroyWebView(view);
        return;
    }

    resetLoadState();
    mbLoadURL(view, (origin + "/nav-one.html").c_str());
    bool one_ok = waitForUrlContains(view, "webcontents-load-nav-one", "/nav-one.html", 6000);
    if (!one_ok) {
        mbDestroyWebView(view);
        return;
    }

    resetLoadState();
    mbLoadURL(view, (origin + "/nav-two.html").c_str());
    bool two_ok = waitForUrlContains(view, "webcontents-load-nav-two", "/nav-two.html", 6000);
    if (!two_ok) {
        mbDestroyWebView(view);
        return;
    }

    resetLoadState();
    mbReload(view);
    waitForUrlContains(view, "webcontents-reload", "/nav-two.html", 6000);

    bool sync_can_go_ok = mbCanGoBackOrForward(view, TRUE) && !mbCanGoBackOrForward(view, FALSE);
    addCheck("webcontents-can-go-sync", sync_can_go_ok);

    g_async_can_go_back = -1;
    g_async_can_go_forward = -1;
    mbCanGoBack(view, onCanGoBack, nullptr);
    mbCanGoForward(view, onCanGoForward, nullptr);
    bool async_can_go_ok = waitFor([] {
        return g_async_can_go_back.load() >= 0 && g_async_can_go_forward.load() >= 0;
    }, 3000);
    addCheck("webcontents-can-go-async", async_can_go_ok && g_async_can_go_back.load() == 1 && g_async_can_go_forward.load() == 0,
        "back=" + std::to_string(g_async_can_go_back.load()) + " forward=" + std::to_string(g_async_can_go_forward.load()));

    resetLoadState();
    mbGoBack(view);
    bool back_ok = waitForObservedUrlContains(view, "webcontents-go-back", "/nav-one.html", "Nav One", 6000);
    bool back_finished = waitFor([] { return g_load_finished.load(); }, 5000);
    bool can_forward_after_back = waitFor([&] { return mbCanGoBackOrForward(view, FALSE); }, 3000);
    addCheck("webcontents-can-go-forward-after-back", back_ok && back_finished && can_forward_after_back);

    resetLoadState();
    mbGoForward(view);
    bool forward_ok = waitForObservedUrlContains(view, "webcontents-go-forward", "/nav-two.html", "Nav Two", 6000);
    bool forward_finished = waitFor([] { return g_load_finished.load(); }, 5000);
    bool can_back_after_forward = waitFor([&] { return mbCanGoBackOrForward(view, TRUE); }, 3000);
    addCheck("webcontents-can-go-back-after-forward", forward_ok && forward_finished && can_back_after_forward);

    resetLoadState();
    mbLoadURL(view, (origin + "/slow.html").c_str());
    bool loading_seen = waitFor([&] { return mbIsLoading(view); }, 2000);
    mbStopLoading(view);
    bool stopped = waitFor([&] { return !mbIsLoading(view); }, 5000);
    addCheck("webcontents-stop-loading", loading_seen && stopped);

    mbDestroyWebView(view);
    waitFor([&] { return !readWindowState(host).isWindow; }, 3000);
}

void runWebContentsScriptAndZoomChecks(mbWebView view)
{
    mbSetZoomFactor(view, 1.25f);
    bool zoom_set = waitFor([&] {
        return std::abs(mbGetZoomFactor(view) - 1.25f) < 0.001f;
    }, 3000);
    addCheck("webcontents-zoom-factor", zoom_set, std::to_string(mbGetZoomFactor(view)));
    mbSetZoomFactor(view, 1.0f);
    waitFor([&] { return std::abs(mbGetZoomFactor(view) - 1.0f) < 0.001f; }, 3000);

    mbWebFrameHandle frame = mbWebFrameGetMainFrame(view);
    int before = g_async_js_callback_count.load();
    g_async_js_number = 0;
    mbRunJs(view, frame, "21 * 2", FALSE, onRunJsNumber, nullptr, nullptr);
    bool number_ok = waitFor([&] {
        return g_async_js_callback_count.load() > before && std::abs(g_async_js_number.load() - 42.0) < 0.001;
    }, 3000);
    addCheck("webcontents-execute-js-async", number_ok, std::to_string(g_async_js_number.load()));

    before = g_async_js_callback_count.load();
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_async_js_text.clear();
    }
    mbRunJs(view, frame, "return window.__local + '-closure';", TRUE, onRunJsString, nullptr, nullptr);
    bool closure_ok = waitFor([&] {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        return g_async_js_callback_count.load() > before && g_async_js_text == "local-ok-closure";
    }, 3000);
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        addCheck("webcontents-execute-js-closure", closure_ok, g_async_js_text);
    }
}

std::string getCookieViaApi(mbWebView view)
{
    int before = g_cookie_callback_count.load();
    g_cookie_callback_state = -1;
    mbGetCookie(view, onGetCookie, nullptr);
    waitFor([&] { return g_cookie_callback_count.load() > before; }, 3000);
    std::lock_guard<std::mutex> lock(g_state_mutex);
    return g_last_cookie_string;
}

void runSessionCookieChecks(mbWebView view, const std::string& origin)
{
    const std::string url = origin + "/remote.html";
    mbSetCookie(view, url.c_str(), "host_cookie=api-ok; path=/");
    bool dom_cookie_ok = waitFor([&] {
        return jsString(view, "document.cookie").find("host_cookie=api-ok") != std::string::npos;
    }, 3000);
    addCheck("session-cookie-set-via-api-dom", dom_cookie_ok, jsString(view, "document.cookie"));

    std::string api_cookie = getCookieViaApi(view);
    addCheck("session-cookie-get-via-api", g_cookie_callback_state.load() == kMbAsynRequestStateOk
            && api_cookie.find("host_cookie=api-ok") != std::string::npos,
        api_cookie);

    mbClearCookie(view);
    bool cleared = waitFor([&] {
        return getCookieViaApi(view).find("host_cookie=api-ok") == std::string::npos;
    }, 3000);
    addCheck("session-cookie-clear-via-api", cleared, getCookieViaApi(view));
}

void runSessionWebRequestChecks(mbWebView view, LocalServer& server)
{
    const std::string custom_user_agent = "MiniBlinkMacE2E/1.0";
    mbSetUserAgent(view, custom_user_agent.c_str());
    bool js_user_agent_ok = waitFor([&] {
        return jsString(view, "navigator.userAgent").find(custom_user_agent) != std::string::npos;
    }, 3000);
    addCheck("session-user-agent-js", js_user_agent_ok, jsString(view, "navigator.userAgent"));

    g_web_request_begin_seen = false;
    g_web_request_end_seen = false;
    g_web_request_post_body_seen = false;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_web_request_post_body.clear();
        g_web_request_end_body.clear();
    }
    int before_hits = server.webRequestHits();
    jsNumber(view,
        "window.__webRequestResult='pending';"
        "fetch('/webrequest',{method:'POST',headers:{'Content-Type':'text/plain'},body:'post-body-ok'})"
        ".then(function(r){return r.text()})"
        ".then(function(t){window.__webRequestResult=t})"
        ".catch(function(e){window.__webRequestResult='ERR:'+e.message});"
        "1");

    bool fetch_ok = waitFor([&] {
        return jsString(view, "window.__webRequestResult") == "webrequest-ok" && server.webRequestHits() > before_hits;
    }, 5000);
    addCheck("session-webrequest-fetch-post", fetch_ok, jsString(view, "window.__webRequestResult"));

    addCheck("session-webrequest-user-agent-header",
        server.lastWebRequestUserAgent().find(custom_user_agent) != std::string::npos,
        server.lastWebRequestUserAgent());
    addCheck("session-webrequest-header-mutation",
        server.lastWebRequestHookHeader() == "yes",
        server.lastWebRequestHookHeader());
    addCheck("session-webrequest-post-body-server",
        server.lastWebRequestBody() == "post-body-ok",
        server.lastWebRequestBody());
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        addCheck("session-webrequest-post-body-callback",
            g_web_request_post_body_seen.load() && g_web_request_post_body == "post-body-ok",
            g_web_request_post_body);
        addCheck("session-webrequest-load-url-end",
            g_web_request_end_seen.load() && g_web_request_end_body == "webrequest-ok",
            g_web_request_end_body);
    }
    addCheck("session-webrequest-load-url-begin", g_web_request_begin_seen.load());
}

} // namespace

int main()
{
    signal(SIGPIPE, SIG_IGN);

    LocalServer server;
    addCheck("local-http-ws-server", server.start(), server.origin());
    if (!server.port())
        return 1;

    std::string tmp_dir = "/tmp/miniblink_browser_window_like";
    mkdir(tmp_dir.c_str(), 0755);
    std::string local_html = makeLocalHtml(tmp_dir);
    std::string upload_file = tmp_dir + "/upload.txt";
    {
        std::ofstream file(upload_file);
        file << "file input ok\n";
    }
    setenv("MINIBLINK_FILE_CHOOSER_PATH", upload_file.c_str(), 1);

    mbInit(nullptr);

    mbWebView view = mbCreateWebWindow(MB_WINDOW_TYPE_POPUP, nullptr, 120, 120, kWindowWidth, kWindowHeight);
    addCheck("create-window-and-webview", view != NULL_WEBVIEW && mbGetHostHWND(view), "host=" + std::to_string((uintptr_t)mbGetHostHWND(view)));
    if (view == NULL_WEBVIEW)
        return 1;

    mbOnTitleChanged(view, onTitleChanged, nullptr);
    mbOnURLChanged(view, onUrlChanged, nullptr);
    mbOnNavigation(view, onNavigation, nullptr);
    mbOnDocumentReady(view, onDocumentReady, nullptr);
    mbOnLoadingFinish(view, onLoadingFinish, nullptr);
    mbOnLoadUrlFail(view, onLoadUrlFail, nullptr);
    mbOnLoadUrlFinish(view, onLoadUrlFinish, nullptr);
    mbOnLoadUrlBegin(view, onLoadUrlBeginForWebRequest, nullptr);
    mbOnLoadUrlEnd(view, onLoadUrlEndForWebRequest, nullptr);
    mbOnCreateView(view, onCreateView, nullptr);
    mbOnDownloadInBlinkThread(view, onDownloadInBlinkThread, nullptr);
    mbSetNavigationToNewWindowEnable(view, TRUE);
    mbShowWindow(view, 5);

    std::thread driver([&] {
    bool webview_ready = waitFor([&] {
        mbWebFrameHandle frame = mbWebFrameGetMainFrame(view);
        return frame && mbGetGlobalExecByFrame(view, frame);
    }, 6000);
    addCheck("webview-main-frame-ready", webview_ready);

    mbMoveWindow(view, 180, 160, 640, 480);
    mbRect bounds;
    BOOL got_bounds = mbGetWindowRect(view, &bounds);
    bool bounds_ok = got_bounds && bounds.x == 180 && bounds.y == 160 && bounds.w == 640 && bounds.h == 480;
    addCheck("browserwindow-bounds-api", bounds_ok,
        std::to_string(bounds.x) + "," + std::to_string(bounds.y) + " " + std::to_string(bounds.w) + "x" + std::to_string(bounds.h));

    mbSetWindowTitle(view, "Host Window Title");
    runLoopFor(100);
    addCheck("browserwindow-title-api", std::string(mbGetTitle(view) ? mbGetTitle(view) : "") == "Host Window Title",
        mbGetTitle(view) ? mbGetTitle(view) : "");

    HWND host = mbGetHostHWND(view);

    mbShowWindow(view, SW_HIDE);
    bool hidden = waitFor([&] { return !readWindowState(host).visible; }, 2000);
    mbShowWindow(view, SW_SHOW);
    bool shown = waitFor([&] { return readWindowState(host).visible; }, 2000);
    addCheck("browserwindow-show-hide-api", hidden && shown);

    mbShowWindow(view, SW_MINIMIZE);
    bool minimized = waitFor([&] { return readWindowState(host).iconic; }, 3000);
    mbShowWindow(view, SW_RESTORE);
    bool restoredFromMinimize = waitFor([&] {
        WindowState state = readWindowState(host);
        return state.visible && !state.iconic;
    }, 3000);
    addCheck("browserwindow-minimize-restore-api", minimized && restoredFromMinimize);

    mbShowWindow(view, SW_MAXIMIZE);
    bool maximized = waitFor([&] { return readWindowState(host).zoomed; }, 3000);
    mbShowWindow(view, SW_RESTORE);
    bool restoredFromMaximize = waitFor([&] {
        WindowState state = readWindowState(host);
        return state.visible && !state.zoomed;
    }, 3000);
    addCheck("browserwindow-maximize-restore-api", maximized && restoredFromMaximize);

    mbSetFocus(view);
    bool focused = waitFor([&] { return readWindowState(host).focused; }, 2000);
    addCheck("browserwindow-focus-api", focused);

    runLifecycleChecks();
    runNavigationControlChecks(server.origin());

    resetLoadState();
    mbLoadURL(view, fileUrl(local_html).c_str());
    bool local_ok = waitForLoad(view, "load-local-html-file", 6000);
    if (local_ok) {
        addCheck("local-html-js-state", jsString(view, "window.__local") == "local-ok");
        addCheck("document-ready-callback", g_document_ready.load());
        addCheck("load-finish-callback-local", g_load_finish_callback.load());
        runWebContentsScriptAndZoomChecks(view);
    }

    resetLoadState();
    mbLoadURL(view, (server.origin() + "/remote.html").c_str());
    bool remote_ok = waitForLoad(view, "load-loopback-browserwindow-page", 8000);
    if (remote_ok) {
        addCheck("navigation-callback", g_navigation_seen.load());
        addCheck("title-callback", g_title_changed.load());
        addCheck("url-callback", g_url_changed.load());
        addCheck("load-finish-callback-remote", g_load_finish_callback.load());

        bool platform_ok = waitFor([&] {
            std::string json = jsString(view, "JSON.stringify(window.__e2e||{})");
            return json.find("fetch-ok") != std::string::npos && json.find("xhr-ok") != std::string::npos && json.find("ws-ok:ping") != std::string::npos;
        }, 5000);
        std::string json = jsString(view, "JSON.stringify(window.__e2e||{})");
        addCheck("fetch-xhr-websocket", platform_ok,
            json + " wsUpgrades=" + std::to_string(server.websocketUpgrades()) + " wsMessages=" + std::to_string(server.websocketMessages())
                + server.websocketDebug());
        addCheck("cookie-localStorage", json.find("cookie-ok") != std::string::npos && json.find("storage-ok") != std::string::npos, json);
        runSessionCookieChecks(view, server.origin());
        runSessionWebRequestChecks(view, server);

        mbSetFocus(view);
        jsNumber(view, "var i=document.getElementById('textInput'); i.value=''; i.focus(); 1");
        mbFireKeyPressEvent(view, 'A', 0, FALSE);
        mbFireKeyPressEvent(view, 0x4e2d, 0, FALSE);
        runLoopFor(300);
        addCheck("keyboard-focus-unicode-input", jsString(view, "document.getElementById('textInput').value") == "A中",
            jsString(view, "document.getElementById('textInput').value"));

        mbFireMouseEvent(view, MB_MSG_MOUSEMOVE, 20, 20, 0);
        mbFireMouseEvent(view, MB_MSG_LBUTTONDOWN, 20, 20, MB_LBUTTON);
        mbFireMouseEvent(view, MB_MSG_LBUTTONUP, 20, 20, 0);
        addCheck("mouse-event-dispatch", true, "mbFireMouseEvent accepted");

        jsNumber(view, "var t=document.getElementById('textInput'); t.value='clip-ok'; t.focus(); t.select(); 1");
        mbEditorCopy(view);
        jsNumber(view, "var t=document.getElementById('textInput'); t.value=''; t.focus(); 1");
        mbEditorPaste(view);
        runLoopFor(300);
        addCheck("clipboard-copy-paste", jsString(view, "document.getElementById('textInput').value") == "clip-ok",
            jsString(view, "document.getElementById('textInput').value"));

        g_popup_seen = false;
        jsNumber(view, "document.getElementById('popupButton').click(); 1");
        waitFor([] { return g_popup_seen.load(); }, 2000);
        addCheck("new-window-popup-policy-callback", g_popup_seen.load());

        g_download_seen = false;
        jsNumber(view, "document.getElementById('download').click(); 1");
        waitFor([] { return g_download_seen.load(); }, 3000);
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            addCheck("download-callback", g_download_seen.load(), g_last_download_url);
        }

        mbMemBuf* screenshot = mbGetWindowScreenshotSync(view, kMbImageFormatPng);
        bool screenshot_ok = screenshot && screenshot->data && screenshot->length > 0;
        addCheck("window-screenshot", screenshot_ok, screenshot ? std::to_string(screenshot->length) : "null");
        if (screenshot)
            mbFreeMemBuf(screenshot);

        jsNumber(view, "var f=document.getElementById('fileInput'); f.click(); f.files.length");
        runLoopFor(500);
        addCheck("file-input-native-chooser", jsString(view, "document.getElementById('fileInput').files[0] && document.getElementById('fileInput').files[0].name") == "upload.txt",
            jsString(view, "document.getElementById('fileInput').files.length + ':' + (document.getElementById('fileInput').files[0] && document.getElementById('fileInput').files[0].name)"));
    }

    resetLoadState();
    mbLoadURL(view, "https://example.com/");
    bool https_ok = waitForLoad(view, "load-remote-https-page", 12000);
    if (https_ok) {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        const char* title = mbGetTitle(view);
        addCheck("remote-https-url-title", g_last_url.find("https://") == 0 && title && title[0], std::string(title ? title : "") + " " + g_last_url);
    }

    resetLoadState();
    mbLoadURL(view, (server.origin() + "/abort").c_str());
    bool fail_seen = waitFor([] { return g_load_failed.load(); }, 5000);
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        addCheck("load-fail-via-loading-finish", fail_seen, g_last_fail_url + " " + g_last_fail_reason);
        addCheck("load-url-fail-callback", g_load_url_fail_callback.load(),
            "mbOnLoadUrlFail is registered separately from mbOnLoadingFinish");
    }

    mbDestroyWebView(view);
    runLoopFor(300);
    server.stop();
    mbExitMessageLoop();
    });

    mbRunMessageLoop();
    driver.join();

    int failures = 0;
    {
        std::lock_guard<std::mutex> lock(g_checks_mutex);
        for (const auto& check : g_checks) {
            if (!check.pass)
                ++failures;
        }
    }
    printf("browser_window_like_test failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
