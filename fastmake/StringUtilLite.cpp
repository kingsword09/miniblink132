#include "content/common/StringUtil.h"

namespace content {

std::string utf16ToUtf8(const WCHAR* lpszSrc)
{
    std::string result;
    if (!lpszSrc)
        return result;
    for (size_t i = 0; lpszSrc[i]; ++i) {
        uint32_t cp = lpszSrc[i];
        if (0xd800 <= cp && cp <= 0xdbff && lpszSrc[i + 1]) {
            uint32_t low = lpszSrc[i + 1];
            if (0xdc00 <= low && low <= 0xdfff) {
                ++i;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            }
        }
        if (cp < 0x80) {
            result.push_back((char)cp);
        } else if (cp < 0x800) {
            result.push_back((char)(0xc0 | (cp >> 6)));
            result.push_back((char)(0x80 | (cp & 0x3f)));
        } else if (cp < 0x10000) {
            result.push_back((char)(0xe0 | (cp >> 12)));
            result.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
            result.push_back((char)(0x80 | (cp & 0x3f)));
        } else {
            result.push_back((char)(0xf0 | (cp >> 18)));
            result.push_back((char)(0x80 | ((cp >> 12) & 0x3f)));
            result.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
            result.push_back((char)(0x80 | (cp & 0x3f)));
        }
    }
    return result;
}

std::u16string utf8ToUtf16(const std::string& utf8)
{
    std::u16string result;
    for (size_t i = 0; i < utf8.size();) {
        unsigned char c = (unsigned char)utf8[i++];
        uint32_t cp = 0xfffd;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i < utf8.size()) {
            cp = ((c & 0x1f) << 6) | ((unsigned char)utf8[i++] & 0x3f);
        } else if ((c >> 4) == 0xe && i + 1 < utf8.size()) {
            cp = ((c & 0x0f) << 12) | (((unsigned char)utf8[i++] & 0x3f) << 6);
            cp |= ((unsigned char)utf8[i++] & 0x3f);
        } else if ((c >> 3) == 0x1e && i + 2 < utf8.size()) {
            cp = ((c & 0x07) << 18) | (((unsigned char)utf8[i++] & 0x3f) << 12);
            cp |= (((unsigned char)utf8[i++] & 0x3f) << 6);
            cp |= ((unsigned char)utf8[i++] & 0x3f);
        }

        if (cp < 0x10000) {
            result.push_back((char16_t)cp);
        } else {
            cp -= 0x10000;
            result.push_back((char16_t)(0xd800 + (cp >> 10)));
            result.push_back((char16_t)(0xdc00 + (cp & 0x3ff)));
        }
    }
    return result;
}

} // namespace content
