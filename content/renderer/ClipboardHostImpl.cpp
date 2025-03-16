
#include "content/renderer/ClipboardHostImpl.h"
#include "content/ui/ClipboardUtil.h"
#include "content/common/mbchar.h"
#include "third_party/blink/public/platform/web_url.h"
#include "base/strings/utf_string_conversions.h"
#include <windows.h>

bool blink::mojom::blink::ClipboardHost::GetSequenceNumber(::blink::mojom::blink::ClipboardBuffer buffer, ::blink::ClipboardSequenceNumberToken* out_result)
{
    DebugBreak();
    return false;
}

bool blink::mojom::blink::ClipboardHost::IsFormatAvailable(
    ::blink::mojom::blink::ClipboardFormat format, ::blink::mojom::blink::ClipboardBuffer buffer, bool* out_result)
{
    return false;
}

bool blink::mojom::blink::ClipboardHost::ReadAvailableTypes(::blink::mojom::blink::ClipboardBuffer buffer, WTF::Vector<::WTF::String>* out_types)
{
    return false;
}

bool blink::mojom::blink::ClipboardHost::ReadText(::blink::mojom::blink::ClipboardBuffer buffer, ::WTF::String* out_result)
{
    return false;
}

bool blink::mojom::blink::ClipboardHost::ReadHtml(
    ::blink::mojom::blink::ClipboardBuffer buffer, ::WTF::String* out_markup, ::blink::KURL* out_url, uint32_t* out_fragment_start, uint32_t* out_fragment_end)
{
    return false;
}

bool blink::mojom::blink::ClipboardHost::ReadRtf(::blink::mojom::blink::ClipboardBuffer buffer, ::WTF::String* out_result)
{
    return false;
}

bool blink::mojom::blink::ClipboardHost::ReadPng(::blink::mojom::blink::ClipboardBuffer buffer, ::mojo_base::BigBuffer* out_png)
{
    return false;
}

bool blink::mojom::blink::ClipboardHost::ReadFiles(::blink::mojom::blink::ClipboardBuffer buffer, ::blink::mojom::blink::ClipboardFilesPtr* out_result)
{
    return false;
}

bool blink::mojom::blink::ClipboardHost::ReadAvailableCustomAndStandardFormats(WTF::Vector<::WTF::String>* out_format_types)
{
    return false;
}

namespace {

void freeData(unsigned int format, HANDLE data)
{
    if (format == CF_BITMAP)
        ::DeleteObject(static_cast<HBITMAP>(data));
    else
        ::GlobalFree(data);
}

template <class str> void appendEscapedCharForHTMLImpl(typename str::value_type c, str* output)
{
    static const struct {
        char key;
        const char* replacement;
    } kCharsToEscape[] = {
        { '<', "&lt;" },
        { '>', "&gt;" },
        { '&', "&amp;" },
        { '"', "&quot;" },
        { '\'', "&#39;" },
    };
    size_t k;
    for (k = 0; k < std::size(kCharsToEscape); ++k) {
        if (c == kCharsToEscape[k].key) {
            const char* p = kCharsToEscape[k].replacement;
            while (*p)
                //output->push_back(*p++);
                *output += (*p++);
            break;
        }
    }
    if (k == /*arraysize*/ std::size(kCharsToEscape))
        //output->push_back(c);
        *output += c;
}

template <class str> str escapeForHTMLImpl(const str& input)
{
    str result;
    result.reserve(input.size()); // Optimize for no escaping.

    for (typename str::const_iterator i = input.begin(); i != input.end(); ++i)
        appendEscapedCharForHTMLImpl(*i, &result);

    return result;
}

std::string escapeForHTML(const Vector<char>& input)
{
    std::string inputStr(input.data(), input.size());
    return escapeForHTMLImpl(inputStr);
}

std::string escapeForHTML(const std::string input)
{
    return escapeForHTMLImpl(input);
}

std::string URLToMarkup(const blink::WebURL& url, const blink::WebString& title)
{
    std::string markup("<a href=\"");
    markup.append(url.GetString().Utf8());
    markup.append("\">");
    // TODO(darin): HTML escape this
    markup.append(escapeForHTML(title.Utf8()));
    markup.append("</a>");
    return markup;
}

std::string URLToImageMarkup(const blink::WebURL& url, const blink::WebString& title)
{
    std::string markup("<img src=\"");
    markup.append(escapeForHTML(url.GetString().Utf8()));
    markup.append("\"");
    if (!title.IsEmpty()) {
        markup.append(" alt=\"");
        markup.append(escapeForHTML(title.Utf8()));
        markup.append("\"");
    }
    markup.append("/>");
    return markup;
}

// A scoper to impersonate the anonymous token and revert when leaving scope
class AnonymousImpersonator {
public:
    AnonymousImpersonator()
    {
        m_mustRevert = ::ImpersonateAnonymousToken(::GetCurrentThread());
    }

    ~AnonymousImpersonator()
    {
        if (m_mustRevert)
            ::RevertToSelf();
    }

private:
    BOOL m_mustRevert;
};

// A scoper to manage acquiring and automatically releasing the clipboard.
class ScopedClipboard {
public:
    ScopedClipboard()
        : m_isOpened(false)
    {
    }

    ~ScopedClipboard()
    {
        if (m_isOpened)
            release();
    }

    bool acquire(HWND owner)
    {
        const int kMaxAttemptsToOpenClipboard = 5;

        if (m_isOpened) {
            CHECK(false);
            return false;
        }

        // Attempt to open the clipboard, which will acquire the Windows clipboard
        // lock.  This may fail if another process currently holds this lock.
        // We're willing to try a few times in the hopes of acquiring it.
        //
        // This turns out to be an issue when using remote desktop because the
        // rdpclip.exe process likes to read what we've written to the clipboard and
        // send it to the RDP client.  If we open and close the clipboard in quick
        // succession, we might be trying to open it while rdpclip.exe has it open,
        // See Bug 815425.
        //
        // In fact, we believe we'll only spin this loop over remote desktop.  In
        // normal situations, the user is initiating clipboard operations and there
        // shouldn't be contention.

        for (int attempts = 0; attempts < kMaxAttemptsToOpenClipboard; ++attempts) {
            // If we didn't manage to open the clipboard, sleep a bit and be hopeful.
            if (attempts != 0)
                ::Sleep(5);

            if (::OpenClipboard(owner)) {
                m_isOpened = true;
                return true;
            }
        }

        // We failed to acquire the clipboard.
        return false;
    }

    void release()
    {
        if (m_isOpened) {
            // Impersonate the anonymous token during the call to CloseClipboard
            // This prevents Windows 8+ capturing the broker's access token which
            // could be accessed by lower-privileges chrome processes leading to
            // a risk of EoP
            AnonymousImpersonator impersonator;
            ::CloseClipboard();
            m_isOpened = false;
        } else {
            CHECK(false);
        }
    }

private:
    bool m_isOpened;
};

} // namespace

namespace content {

bool ClipboardHostImpl::GetSequenceNumber(::blink::mojom::blink::ClipboardBuffer buffer, ::blink::ClipboardSequenceNumberToken* out_result)
{
    uint64_t sequence = ::GetClipboardSequenceNumber();
    std::optional<base::UnguessableToken> token = base::UnguessableToken::Deserialize(sequence, 0);
    if (!token.has_value())
        return false;
    *out_result = ::blink::ClipboardSequenceNumberToken(token.value());
    return true;
}

//using GetSequenceNumberCallback = base::OnceCallback<void(const ::blink::ClipboardSequenceNumberToken&)>;
void ClipboardHostImpl::GetSequenceNumber(::blink::mojom::blink::ClipboardBuffer buffer, GetSequenceNumberCallback callback)
{
    uint64_t sequence = ::GetClipboardSequenceNumber();        
    std::optional<base::UnguessableToken> token = base::UnguessableToken::Deserialize(sequence, 0);

    if (token.has_value())    
        std::move(callback).Run(::blink::ClipboardSequenceNumberToken(token.value()));
    else
        std::move(callback).Run(::blink::ClipboardSequenceNumberToken());
}

bool convertBufferType(::blink::mojom::ClipboardBuffer buffer, ClipboardType* result)
{
    *result = CLIPBOARD_TYPE_COPY_PASTE;
    switch (buffer) {
    case ::blink::mojom::ClipboardBuffer::kStandard:
        break;
    case ::blink::mojom::ClipboardBuffer::kSelection:
        // Chrome OS and non-X11 unix builds do not support
        // the X selection clipboad.
        // TODO: remove the need for this case, see http://crbug.com/361753
        return false;
    default:
        CHECK(false);
        return false;
    }
    return true;
}

bool ClipboardHostImpl::IsFormatAvailable(::blink::mojom::blink::ClipboardFormat format, ::blink::mojom::blink::ClipboardBuffer buffer, bool* out_result)
{
#if defined(OS_WIN)
    switch (format) {
    case blink::mojom::ClipboardFormat::kPlaintext:
        *out_result = ::IsClipboardFormatAvailable(CF_UNICODETEXT) || ::IsClipboardFormatAvailable(CF_TEXT);
        return true;
    case blink::mojom::ClipboardFormat::kHtml:
        *out_result = ::IsClipboardFormatAvailable(ClipboardUtil::getHtmlFormatType());
        return true;
    case blink::mojom::ClipboardFormat::kSmartPaste:
        *out_result = ::IsClipboardFormatAvailable(ClipboardUtil::getWebKitSmartPasteFormatType());
        return true;
    case blink::mojom::ClipboardFormat::kBookmark:
        *out_result = ::IsClipboardFormatAvailable(ClipboardUtil::getUrlWFormatType());
        return true;
    default:
        CHECK(false);
    }
#endif
    return false;
}

//using IsFormatAvailableCallback = base::OnceCallback<void(bool)>;
void ClipboardHostImpl::IsFormatAvailable(
    ::blink::mojom::blink::ClipboardFormat format, ::blink::mojom::blink::ClipboardBuffer buffer, IsFormatAvailableCallback callback)
{
    CHECK(false);
    std::move(callback).Run(false);
}

void ClipboardHostImpl::readAvailableTypes(ClipboardType type, WTF::Vector<WTF::String>* types, bool* containsFilenames) const
{
#if defined(OS_WIN)
    if (!types || !containsFilenames) {
        CHECK(false);
        return;
    }

    *containsFilenames = false;
    types->clear();

    if (::IsClipboardFormatAvailable(CF_TEXT))
        types->push_back(WTF::String::FromUTF8(kMimeTypeText));
    if (::IsClipboardFormatAvailable(ClipboardUtil::getHtmlFormatType()))
        types->push_back(WTF::String::FromUTF8(kMimeTypeHTML));
    if (::IsClipboardFormatAvailable(ClipboardUtil::getRtfFormatType()))
        types->push_back(WTF::String::FromUTF8(kMimeTypeRTF));

    if (::IsClipboardFormatAvailable(CF_DIB)) {
        types->push_back(WTF::String::FromUTF8(kMimeTypePNG));
        return; // DataObjectItem::createFromPasteboard里只认识png格式。 不返回的话，wangEditor里会出现粘贴不了的问题
    }

    if (::IsClipboardFormatAvailable(CF_BITMAP))
        types->push_back(WTF::String::FromUTF8(kMimeTypeBMP));
#endif
}

bool ClipboardHostImpl::ReadAvailableTypes(::blink::mojom::blink::ClipboardBuffer buffer, WTF::Vector<::WTF::String>* types)
{
    bool containsFilenames = false;
    ClipboardType clipboardType;
    if (convertBufferType(buffer, &clipboardType))
        readAvailableTypes(clipboardType, types, &containsFilenames);

    return true;
}

//using ReadAvailableTypesCallback = base::OnceCallback<void(const WTF::Vector<::WTF::String>&)>;
void ClipboardHostImpl::ReadAvailableTypes(::blink::mojom::blink::ClipboardBuffer buffer, ReadAvailableTypesCallback callback)
{
    CHECK(false);
    std::move(callback).Run(WTF::Vector<::WTF::String>());
}

bool ClipboardHostImpl::ReadText(::blink::mojom::blink::ClipboardBuffer buffer, ::WTF::String* text)
{
    ClipboardType clipboardType;
    if (!convertBufferType(buffer, &clipboardType))
        return false;

    // Acquire the clipboard.
    ScopedClipboard clipboard;
    if (!clipboard.acquire(getClipboardWindow()))
        return false;

    HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    if (!data)
        return false;

    LPCWSTR dataText = (LPCWSTR)::GlobalLock(data);
    *text = String((const UChar*)dataText, u16len((const UChar*)dataText));
    ::GlobalUnlock(data);

    return true;
}

//using ReadTextCallback = base::OnceCallback<void(const ::WTF::String&)>;
void ClipboardHostImpl::ReadText(::blink::mojom::blink::ClipboardBuffer buffer, ReadTextCallback callback)
{
    CHECK(false);
    std::move(callback).Run(::WTF::String());
}

bool ClipboardHostImpl::ReadHtml(
    ::blink::mojom::blink::ClipboardBuffer buffer, ::WTF::String* out_markup, ::blink::KURL* out_url, uint32_t* out_fragment_start, uint32_t* out_fragment_end)
{
    CHECK(false);
    return false;
}

//using ReadHtmlCallback = base::OnceCallback<void(const ::WTF::String&, const ::blink::KURL&, uint32_t, uint32_t)>;
void ClipboardHostImpl::ReadHtml(::blink::mojom::blink::ClipboardBuffer buffer, ReadHtmlCallback callback)
{
    CHECK(false);
    std::move(callback).Run(::WTF::String(), ::blink::KURL(), 0, 0);
}

//using ReadSvgCallback = base::OnceCallback<void(const ::WTF::String&)>;
void ClipboardHostImpl::ReadSvg(::blink::mojom::blink::ClipboardBuffer buffer, ReadSvgCallback callback)
{
    CHECK(false);
    std::move(callback).Run(::WTF::String());
}

bool ClipboardHostImpl::ReadRtf(::blink::mojom::blink::ClipboardBuffer buffer, ::WTF::String* out_result)
{
    CHECK(false);
    return false;
}

//using ReadRtfCallback = base::OnceCallback<void(const ::WTF::String&)>;
void ClipboardHostImpl::ReadRtf(::blink::mojom::blink::ClipboardBuffer buffer, ReadRtfCallback callback)
{
    CHECK(false);
    std::move(callback).Run(::WTF::String());
}

bool ClipboardHostImpl::ReadPng(::blink::mojom::blink::ClipboardBuffer buffer, ::mojo_base::BigBuffer* out_png)
{
    CHECK(false);
    return false;
}

//using ReadPngCallback = base::OnceCallback<void(::mojo_base::BigBuffer)>;
void ClipboardHostImpl::ReadPng(::blink::mojom::blink::ClipboardBuffer buffer, ReadPngCallback callback)
{
    std::move(callback).Run(::mojo_base::BigBuffer());
}

bool ClipboardHostImpl::ReadFiles(::blink::mojom::blink::ClipboardBuffer buffer, ::blink::mojom::blink::ClipboardFilesPtr* out_result)
{
    return false;
}

//using ReadFilesCallback = base::OnceCallback<void(ClipboardFilesPtr)>;
void ClipboardHostImpl::ReadFiles(::blink::mojom::blink::ClipboardBuffer buffer, ReadFilesCallback callback)
{
    std::move(callback).Run(::blink::mojom::blink::ClipboardFiles::New());
}

// bool ClipboardHostImpl::ReadCustomData(::blink::mojom::blink::ClipboardBuffer buffer, const ::WTF::String& type, ::WTF::String* out_result)
// {
//     return false;
// }
// 
// //using ReadCustomDataCallback = base::OnceCallback<void(const ::WTF::String&)>;
// void ClipboardHostImpl::ReadCustomData(::blink::mojom::blink::ClipboardBuffer buffer, const ::WTF::String& type, ReadCustomDataCallback callback)
// {
//     std::move(callback).Run(::WTF::String());
// }

bool ClipboardHostImpl::ReadAvailableCustomAndStandardFormats(WTF::Vector<::WTF::String>* out_format_types)
{
    return false;
}

//using ReadAvailableCustomAndStandardFormatsCallback = base::OnceCallback<void(const WTF::Vector<::WTF::String>&)>;
void ClipboardHostImpl::ReadAvailableCustomAndStandardFormats(ReadAvailableCustomAndStandardFormatsCallback callback)
{
    std::move(callback).Run(WTF::Vector<::WTF::String>());
}

//using ReadUnsanitizedCustomFormatCallback = base::OnceCallback<void(::mojo_base::BigBuffer)>;
void ClipboardHostImpl::ReadUnsanitizedCustomFormat(const ::WTF::String& format, ReadUnsanitizedCustomFormatCallback callback)
{
    std::move(callback).Run(::mojo_base::BigBuffer());
}

bool ClipboardHostImpl::ReadDataTransferCustomData(blink::mojom::blink::ClipboardBuffer buffer, const ::WTF::String& type, ::WTF::String* out_result)
{
    *(int*)1 = 1;
    return false;
}

void ClipboardHostImpl::ReadDataTransferCustomData(blink::mojom::blink::ClipboardBuffer buffer, const ::WTF::String& type, ReadDataTransferCustomDataCallback callback)
{
    *(int*)1 = 1;
}

void ClipboardHostImpl::WriteDataTransferCustomData(const WTF::HashMap<::WTF::String, ::WTF::String>& data)
{
    *(int*)1 = 1;
}

void ClipboardHostImpl::writeToClipboardInternal(unsigned int format, HANDLE handle)
{
    DCHECK(m_clipboardOwner != NULL);
    if (handle && !::SetClipboardData(format, handle)) {
        DCHECK(ERROR_CLIPBOARD_NOT_OPEN != GetLastError());
        freeData(format, handle);
    }
}

void ClipboardHostImpl::writeTextInternal(const WTF::String& string)
{
    if (string.empty())
        return;

    std::u16string strW;
    HGLOBAL glob = NULL;
    if (string.Is8Bit()) {
        strW = base::UTF8ToUTF16(string.Utf8());
    } else
        strW.assign((const char16_t*)string.Characters16(), string.length());

    glob = ClipboardUtil::createGlobalData<char16_t>(strW);

    writeToClipboardInternal(CF_UNICODETEXT, glob);
}

void ClipboardHostImpl::WriteText(const ::WTF::String& text)
{
    ScopedClipboard clipboard;
    if (!clipboard.acquire(getClipboardWindow()))
        return;

    ::EmptyClipboard();

    writeTextInternal(text);
}

void ClipboardHostImpl::WriteHtml(const ::WTF::String& markup, const ::blink::KURL& url)
{
}

void ClipboardHostImpl::WriteSvg(const ::WTF::String& markup)
{
}

void ClipboardHostImpl::WriteSmartPasteMarker()
{
}

void ClipboardHostImpl::WriteBookmark(const WTF::String& url, const ::WTF::String& title)
{
}

void ClipboardHostImpl::WriteImage(const ::SkBitmap& image)
{
}

void ClipboardHostImpl::WriteUnsanitizedCustomFormat(const ::WTF::String& format, ::mojo_base::BigBuffer data)
{
}

void ClipboardHostImpl::CommitWrite()
{
}

extern "C" LRESULT __stdcall clipboardOwnerWndProc(HWND hWnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    LRESULT result = 0;
    switch (message) {
    case WM_RENDERFORMAT:
        // This message comes when SetClipboardData was sent a null data handle
        // and now it's come time to put the data on the clipboard.
        // We always set data, so there isn't a need to actually do anything here.
        break;
    case WM_RENDERALLFORMATS:
        // This message comes when SetClipboardData was sent a null data handle
        // and now this application is about to quit, so it must put data on
        // the clipboard before it exits.
        // We always set data, so there isn't a need to actually do anything here.
        break;
    case WM_DRAWCLIPBOARD:
        break;
    case WM_DESTROY:
        break;
    case WM_CHANGECBCHAIN:
        break;
    default:
        return DefWindowProcW(hWnd, message, wparam, lparam);
    }

    return result;
}

HWND ClipboardHostImpl::m_clipboardOwner = nullptr;

HWND ClipboardHostImpl::getClipboardWindow()
{
    if (INVALID_HANDLE_VALUE != m_clipboardOwner && NULL != m_clipboardOwner)
        return m_clipboardOwner;

    WNDCLASSEXW windowClass;
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = 0;
    windowClass.lpfnWndProc = clipboardOwnerWndProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = nullptr;
    windowClass.hIcon = NULL;
    windowClass.hCursor = NULL;
    windowClass.hbrBackground = NULL;
    windowClass.lpszMenuName = NULL;
    windowClass.lpszClassName = mbu16("WebClipboardImplMessageWindow");
    windowClass.hIconSm = NULL;
    ATOM atom = RegisterClassExW(&windowClass);

    m_clipboardOwner = ::CreateWindowW(MAKEINTATOM(atom), mbu16("window_name"), 0, 0, 0, 1, 1, HWND_MESSAGE, 0, NULL, NULL);

    return m_clipboardOwner;
}

}