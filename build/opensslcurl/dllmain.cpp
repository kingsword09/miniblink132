// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include "third_party/libcurl/include/curl/curl.h"
#include <malloc.h>
#include <string>
#include <vector>
#include <list>
#include <process.h>

BOOL APIENTRY DllMainStub(HMODULE h, DWORD reason, LPVOID reserved);

const int selectTimeoutMS = 5;
const double pollTimeSeconds = 0.05;
const int maxRunningJobs = 5;

const bool ignoreSSLErrors = true; //  ("WEBKIT_IGNORE_SSL_ERRORS");
const int kAllowedProtocols = CURLPROTO_FILE | CURLPROTO_FTP | CURLPROTO_FTPS | CURLPROTO_HTTP | CURLPROTO_HTTPS;

static size_t writeCallbackTest(void* ptr, size_t size, size_t nmemb, void* data)
{
    return size * nmemb;
}

static size_t headerCallbackTest(char* ptr, size_t size, size_t nmemb, void* data)
{
    return size * nmemb;
}

static int debugCallbackTest(CURL* handle, curl_infotype type, char* data, size_t size, void* clientp)
{
    std::string output(data, size);
    output.insert(0, "debugCallbackTest:[");
    output += "]\n";
    OutputDebugStringA(output.c_str());
    return 0;
}

HANDLE s_handle = NULL;
char s_curlErrorBuffer[CURL_ERROR_SIZE];

int CheckUrlIsGmsslSiteImpl(const char* url)
{
    ;
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    memset(s_curlErrorBuffer, 0, CURL_ERROR_SIZE);
    //curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:82.0) Gecko/20100101 Firefox/82.0");
    //curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_GM);
    //curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, DebugCallback);

    curl_slist* clist = nullptr;
    //clist = curl_slist_append(clist, "Content-Type: application/json;charset=utf8");
    clist = curl_slist_append(clist,
        "accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9");
    clist = curl_slist_append(clist, "accept-encoding: gzip, deflate, br");
    clist = curl_slist_append(clist, "accept-language: zh-CN,zh;q=0.9");
    clist = curl_slist_append(clist, "cache-control: max-age=0");
    clist = curl_slist_append(
        clist, "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/94.0.4606.71 Safari/537.36 Core/1.94.233.400/12.3.5574.400");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, clist);

    // error:1000042e:SSL routines:OPENSSL_internal:TLSV1_ALERT_PROTOCOL_VERSION
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, s_curlErrorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallbackTest);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallbackTest);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debugCallbackTest);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false); // ignoreSSLErrors
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, FALSE);
    curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 32768); // 32KB of FFmpeg
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 60 * 5); // 5 minutes
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, kAllowedProtocols);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, kAllowedProtocols);
    curl_easy_setopt(curl, CURLOPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, TRUE);
    //curl_easy_setopt(curl, CURLOPT_URL, "https://ebssec.boc.cn/boc15/login.html");
    //curl_easy_setopt(curl, CURLOPT_URL, "https://demo.gmssl.cn:1443");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    res = curl_easy_perform(curl);
    s_curlErrorBuffer[CURL_ERROR_SIZE - 1] = 0;

    BOOL b = FALSE;
    if (CURLE_OK != res) {
        if (nullptr != strstr(s_curlErrorBuffer, "TLSV1_ALERT_PROTOCOL_VERSION") || nullptr != strstr(s_curlErrorBuffer, "SSLV3_ALERT_HANDSHAKE_FAILURE")) {
            b = TRUE;
        }
    }

    //     const int len = 401 + strlen(url);
    //     char* output = (char*)malloc(len);
    //     sprintf_s(output, len - 1, "CheckUrlIsGmsslSiteSync: %d %s\n", res, url);
    //     OutputDebugStringA(output);
    //     free(output);

    curl_easy_cleanup(curl);
    return b;
}

class CheckUrlMgr {
public:
    struct CheckUrlInfo {
        std::string url;
        int ret = 0;
        int finish = 0;
    };

    struct CheckUrlTask {
        std::list<CheckUrlInfo*> tasks;
        CRITICAL_SECTION mutex;
        HANDLE handle;
    };

    static const int kTaskNum = 4;

    CheckUrlMgr()
    {
        for (size_t i = 0; i < kTaskNum; ++i) {
            unsigned int threadIdentifier = 0;
            InitializeCriticalSection((&m_tasks[i].mutex));
            m_tasks[i].handle = (HANDLE)(_beginthreadex(0, 0, (_beginthreadex_proc_type)CheckUrlIsGmsslSiteThread, &(m_tasks[i]), 0, &threadIdentifier));
        }
    }

    static int __stdcall CheckUrlIsGmsslSiteThread(CheckUrlTask* task)
    {
        while (true) {
            CheckUrlInfo* info = nullptr;
            ::EnterCriticalSection(&(task->mutex));
            std::list<CheckUrlInfo*>::iterator it = task->tasks.begin();
            info = *it;
            if (it == task->tasks.end()) {
                ::LeaveCriticalSection(&(task->mutex));
                ::Sleep(10);
                continue;
            }
            task->tasks.pop_front();
            ::LeaveCriticalSection(&(task->mutex));

            info->ret = CheckUrlIsGmsslSiteImpl(info->url.c_str());
            info->finish = 1;
        }
    }

    int WaitCheckUrlIsGmsslSite(const char* url)
    {
        m_taskRandCount = (m_taskRandCount++) % kTaskNum;

        CheckUrlInfo* info = new CheckUrlInfo();
        info->url = url;

        ::EnterCriticalSection(&(m_tasks[m_taskRandCount].mutex));
        m_tasks[m_taskRandCount].tasks.push_back(info);
        ::LeaveCriticalSection(&(m_tasks[m_taskRandCount].mutex));

        while (info->finish != 1) {
            ::Sleep(1);
        }
        int ret = info->ret;
        delete info;
        return ret;
    }

    int m_taskRandCount;
    CheckUrlTask m_tasks[kTaskNum];
};

CheckUrlMgr* s_mgr = nullptr;

extern "C" int CheckUrlIsGmsslSiteSync(const char* url)
{
    if (!s_mgr)
        s_mgr = new CheckUrlMgr();

    return s_mgr->WaitCheckUrlIsGmsslSite(url);
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID reserved)
{
    //return DllMainStub(h, reason, reserved);
    switch (reason) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
