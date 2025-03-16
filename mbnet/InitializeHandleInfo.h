
#ifndef net_InitializeHandleInfo_h
#define net_InitializeHandleInfo_h

#include "mbnet/ProxyType.h"
#include "mbnet/PageNetExtraData.h"
#include "third_party/libcurl/include/curl/curl.h"
#include "base/memory/scoped_refptr.h"
#include <string>

namespace mbnet {

struct SetupHttpMethodInfo;

struct InitializeHandleInfo {
    std::string url;
    std::string method;
    curl_slist* headers;
#if ENABLE_WKE
    scoped_refptr<PageNetExtraData> pageNetExtraData;
#endif
    std::string proxy;
    std::string proxyUserNamePassword;

    std::string range;
    std::string wkeNetInterface;
    ProxyType proxyType;
    SetupHttpMethodInfo* methodInfo;

    InitializeHandleInfo()
    {
        methodInfo = nullptr;
    }

    ~InitializeHandleInfo();
};

}

#endif // net_InitializeHandleInfo_h