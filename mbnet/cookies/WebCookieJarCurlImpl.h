#ifndef net_cookies_WebCookieJarImph_h
#define net_cookies_WebCookieJarImph_h

// #include "third_party/WebKit/public/platform/WebCookieJar.h"
// #include "third_party/WebKit/Source/platform/weborigin/KURL.h"
#include "third_party/blink/public/platform/web_url.h"
#include <string>

struct curl_slist;
typedef void CURL;
typedef void CURLSH;

namespace mbnet {

class ParsedCookie;
class CookieMonster;

class WebCookieJarImpl /*: public blink::WebCookieJar*/ {
public:
    static WebCookieJarImpl* create(const std::string& cookieJarFullPath);
    ~WebCookieJarImpl();

    void setCookiesFromDOM(const blink::KURL&, const blink::KURL& url, const std::string& value);

    // /*virtual*/ void setCookie(const blink::WebURL&, const blink::WebURL& firstPartyForCookies, const std::string& cookie) /*override*/;
    /*virtual*/ std::string cookies(const blink::WebURL&, const blink::WebURL& firstPartyForCookies) /*override*/;
    /*virtual*/ std::string cookieRequestHeaderFieldValue(const blink::WebURL&, const blink::WebURL& firstPartyForCookies) /*override*/;
    /*virtual*/ bool cookiesEnabled(const blink::WebURL&, const blink::WebURL& firstPartyForCookies) /*override*/
    {
        return true;
    }

    void setToRecordFromRawHeads(const blink::KURL& url, const std::string& rawHeadsString);

    void deleteCookies(const blink::KURL& url, const std::string& cookieName);
    std::string getCookiesForSession(const blink::KURL&, const blink::KURL& url, bool httponly);
    const curl_slist* getAllCookiesBegin();
    void getAllCookiesEnd(const curl_slist* list);

    typedef bool (*CookieVisitor)(
        void* params, const char* name, const char* value, const char* domain, const char* path, int secure, int httpOnly, int* expires);
    void visitAllCookie(void* params, CookieVisitor visit);

    CURLSH* getCurlShareHandle() const
    {
        return m_curlShareHandle;
    }
    std::string getCookieJarFullPath();

private:
    friend class WebCookieJarImpl;
    WebCookieJarImpl(const std::string& cookieJarFullPath);

    void flushCurlCookie(CURL* curl);

    CURLSH* m_curlShareHandle;
    std::string m_cookieJarFileName;
    bool m_dirty;

    //static WebCookieJarImpl* m_inst;
};

} // content

#endif // net_cookies_WebCookieJarImph_h