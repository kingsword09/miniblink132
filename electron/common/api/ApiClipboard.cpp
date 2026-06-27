// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/nodeblink.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/api/EventEmitter.h"
#include "electron/common/api/ApiNativeImage.h"
#include "electron/common/gin_helper/wrappable.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libuv/include/uv.h"
#include "third_party/libnode/src/node_buffer.h"
#include "base/threading/thread_local.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#if !defined(__APPLE__)
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "base/containers/contains.h"
#include "mojo/public/cpp/base/big_buffer.h"
#endif
#include <string>
#include <vector>

namespace atom {

THREAD_LOCAL_CONSTRUCTOR(Clipboard)

namespace {

#if defined(__APPLE__)

std::vector<std::string>& writtenClipboardFormats()
{
    static std::vector<std::string> formats;
    return formats;
}

void rememberClipboardFormat(const std::string& format)
{
    if (format.empty())
        return;

    std::vector<std::string>& formats = writtenClipboardFormats();
    for (const std::string& existing : formats) {
        if (existing == format)
            return;
    }
    formats.push_back(format);
}

UINT clipboardFormatForName(const std::string& format)
{
    if (format.empty() || format == "text/plain" || format == "text")
        return CF_UNICODETEXT;
    return ::RegisterClipboardFormatA(format.c_str());
}

std::string readClipboardBytes(UINT format)
{
    if (!format || !::OpenClipboard(nullptr))
        return std::string();

    HANDLE data = ::GetClipboardData(format);
    if (!data) {
        ::CloseClipboard();
        return std::string();
    }

    SIZE_T size = ::GlobalSize(static_cast<HGLOBAL>(data));
    const char* bytes = static_cast<const char*>(::GlobalLock(data));
    std::string result;
    if (bytes && size)
        result.assign(bytes, bytes + size);
    if (bytes)
        ::GlobalUnlock(data);
    ::CloseClipboard();
    return result;
}

void writeClipboardBytes(const std::string& format, const char* data, size_t size)
{
    UINT clipboardFormat = clipboardFormatForName(format);
    if (!clipboardFormat)
        return;

    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory)
        return;

    if (size) {
        void* rawData = ::GlobalLock(memory);
        if (rawData)
            memcpy(rawData, data, size);
        ::GlobalUnlock(memory);
    }

    ::OpenClipboard(nullptr);
    ::EmptyClipboard();
    ::SetClipboardData(clipboardFormat, memory);
    ::CloseClipboard();
    rememberClipboardFormat(format);
}

#endif

std::u16string readUtf16ClipboardText(HANDLE data)
{
    if (!data)
        return std::u16string();

    const WCHAR* dataText = static_cast<const WCHAR*>(::GlobalLock(data));
    if (!dataText)
        return std::u16string();

    size_t length = 0;
    while (dataText[length])
        ++length;

    std::u16string text(reinterpret_cast<const char16_t*>(dataText), length);
    ::GlobalUnlock(data);
    return text;
}

} // namespace

class Clipboard : public mate::EventEmitter<Clipboard> {
public:
    Clipboard(v8::Isolate* isolate, v8::Local<v8::Object> wrapper)
    {
        gin_helper::Wrappable<Clipboard>::InitWith(isolate, wrapper);
    }

    static void init(v8::Isolate* isolate, v8::Local<v8::Object> target)
    {
        v8::Local<v8::FunctionTemplate> prototype = v8::FunctionTemplate::New(isolate, newFunction);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        prototype->SetClassName(v8::String::NewFromUtf8(isolate, "Clipboard").ToLocalChecked());
        gin_helper::ObjectTemplateBuilder builder(isolate, prototype->InstanceTemplate());
        builder.SetMethod("_readImage", &Clipboard::_readImageApi);
        builder.SetMethod("_readText", &Clipboard::_readTextApi);
        builder.SetMethod("_writeText", &Clipboard::_writeTextApi);
        builder.SetMethod("_writeImage", &Clipboard::_writeImageApi);
        builder.SetMethod("readBuffer", &Clipboard::readBufferApi);
        builder.SetMethod("writeBuffer", &Clipboard::writeBufferApi);
        builder.SetMethod("availableFormats", &Clipboard::availableFormatsApi);

        builder.SetMethod("has", &Clipboard::hasApi);
        builder.SetMethod("read", &Clipboard::readApi);
        builder.SetMethod("write", &Clipboard::writeApi);
        builder.SetMethod("readRTF", &Clipboard::readRTFApi);
        builder.SetMethod("writeRTF", &Clipboard::writeRTFApi);
        builder.SetMethod("readHTML", &Clipboard::readHTMLApi);
        builder.SetMethod("writeHTML", &Clipboard::writeHTMLApi);
        builder.SetMethod("readBookmark", &Clipboard::readBookmarkApi);
        builder.SetMethod("writeBookmark", &Clipboard::writeBookmarkApi);
        builder.SetMethod("readFindText", &Clipboard::readFindTextApi);
        builder.SetMethod("writeFindText", &Clipboard::writeFindTextApi);
        builder.SetMethod("_clear", &Clipboard::_clearApi);

        getClipboardConstructor().Reset(isolate, prototype->GetFunction(context).ToLocalChecked());
        target->Set(context, v8::String::NewFromUtf8(isolate, "Clipboard").ToLocalChecked(), prototype->GetFunction(context).ToLocalChecked());
    }

    void writeFindTextApi(const std::u16string& text) {}

    std::u16string readFindTextApi() 
    {
        return std::u16string();
    }

    std::u16string readHTMLApi(gin_helper::Arguments* args)
    {
#if defined(__APPLE__)
        return base::UTF8ToUTF16(readClipboardBytes(clipboardFormatForName("text/html")));
#else
        std::u16string data;
        std::u16string html;
        std::string url;
        uint32_t start;
        uint32_t end;
        ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
        clipboard->ReadHTML(/*GetClipboardBuffer(args)*/ui::ClipboardBuffer::kCopyPaste, /* data_dst = */ nullptr, &html, &url, &start, &end);
        data = html.substr(start, end - start);
        return data;
#endif
    }

    void Clipboard::writeHTMLApi(const std::u16string& html, gin_helper::Arguments* args)
    {
#if defined(__APPLE__)
        std::string data = base::UTF16ToUTF8(html);
        writeClipboardBytes("text/html", data.data(), data.size());
#else
        ui::ScopedClipboardWriter writer(/*GetClipboardBuffer(args)*/ui::ClipboardBuffer::kCopyPaste);
        writer.WriteHTML(html, std::string());
#endif
    }

    v8::Local<v8::Value> readBookmarkApi(gin_helper::Arguments* args)
    {
        std::u16string title;
        std::string url;
        auto dict = gin_helper::Dictionary::CreateEmpty(args->isolate());
#if !defined(__APPLE__)
        ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
        clipboard->ReadBookmark(/* data_dst = */ nullptr, &title, &url);
#endif
        dict.Set("title", title);
        dict.Set("url", url);
        return dict.GetHandle();
    }

    void writeBookmarkApi(const std::u16string& title, const std::string& url, gin_helper::Arguments* args)
    {
#if defined(__APPLE__)
        std::string data = base::UTF16ToUTF8(title);
        if (!url.empty()) {
            data += "\n";
            data += url;
        }
        writeClipboardBytes("text/uri-list", data.data(), data.size());
#else
        ui::ScopedClipboardWriter writer(/*GetClipboardBuffer(args)*/ui::ClipboardBuffer::kCopyPaste);
        writer.WriteBookmark(title, url);
#endif
    }

    void writeRTFApi(const std::string& text, gin_helper::Arguments* args) 
    {
#if defined(__APPLE__)
        writeClipboardBytes("text/rtf", text.data(), text.size());
#else
        ui::ScopedClipboardWriter writer(/*GetClipboardBuffer(args)*/ui::ClipboardBuffer::kCopyPaste);
        writer.WriteRTF(text);
#endif
    }

    std::u16string Clipboard::readRTFApi()
    {
#if defined(__APPLE__)
        return base::UTF8ToUTF16(readClipboardBytes(clipboardFormatForName("text/rtf")));
#else
        std::string data;
        ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
        clipboard->ReadRTF(/*GetClipboardBuffer(args)*/ui::ClipboardBuffer::kCopyPaste, /* data_dst = */ nullptr, &data);
        return base::UTF8ToUTF16(data);
#endif
    }

    std::string readApi(const std::string& formatSstring)
    {
        return readImpl(formatSstring);
    }

    void writeApi(v8::Local<v8::Object> object)
    {
        gin_helper::Dictionary data(v8::Isolate::GetCurrent(), object);
        writeImpl(data);
    }

    void writeImpl(const gin_helper::Dictionary& data)
    {
        std::u16string text, html, bookmark;
#if 0
        gfx::Image image;
#endif

#if !defined(__APPLE__)
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste/*GetClipboardBuffer(args)*/);
#endif

        if (data.Get("text", &text)) {
#if defined(__APPLE__)
            _writeTextApi(base::UTF16ToUTF8(text), std::string());
#else
            writer.WriteText(text);

            if (data.Get("bookmark", &bookmark))
                writer.WriteBookmark(bookmark, base::UTF16ToUTF8(text));
#endif
        }

        if (data.Get("rtf", &text)) {
            std::string rtf = base::UTF16ToUTF8(text);
#if defined(__APPLE__)
            writeClipboardBytes("text/rtf", rtf.data(), rtf.size());
#else
            writer.WriteRTF(rtf);
#endif
        }

        if (data.Get("html", &html)) {
#if defined(__APPLE__)
            std::string htmlUtf8 = base::UTF16ToUTF8(html);
            writeClipboardBytes("text/html", htmlUtf8.data(), htmlUtf8.size());
#else
            writer.WriteHTML(html, std::string());
#endif
        }
#if 0
        if (data.Get("image", &image))
            writer.WriteImage(image.AsBitmap());
#endif
    }

    bool hasApi(const std::string& formatSstring)
    {
#if defined(__APPLE__)
        return ::IsClipboardFormatAvailable(clipboardFormatForName(formatSstring));
#else
        ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
        ui::ClipboardFormatType format(ui::ClipboardFormatType::GetType(formatSstring));
        if (format.GetName().empty())
            format = ui::ClipboardFormatType::CustomPlatformType(formatSstring);
        return clipboard->IsFormatAvailable(format, ui::ClipboardBuffer::kCopyPaste, /* data_dst = */ nullptr);
#endif
    }

    void _clearApi(const std::string& type)
    {
        ::OpenClipboard(NULL);
        ::EmptyClipboard();
        ::CloseClipboard();
    }

    std::vector<std::u16string> availableFormatsApi()
    {
        std::vector<std::u16string> formatTypes;
#if defined(__APPLE__)
        if (::IsClipboardFormatAvailable(CF_UNICODETEXT))
            formatTypes.push_back(u"text/plain");
        for (const std::string& format : writtenClipboardFormats()) {
            if (::IsClipboardFormatAvailable(clipboardFormatForName(format)))
                formatTypes.push_back(base::UTF8ToUTF16(format));
        }
#else
        ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
        clipboard->ReadAvailableTypes(/*GetClipboardBuffer(args)*/ ui::ClipboardBuffer::kCopyPaste, /* data_dst = */ nullptr, &formatTypes);
#endif
        return formatTypes;
    }

    std::string readImpl(const std::string& formatSstring)
    {
#if defined(__APPLE__)
        UINT format = clipboardFormatForName(formatSstring);
        if (format == CF_UNICODETEXT)
            return _readTextApi(std::string());
        return readClipboardBytes(format);
#else
        ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
        // Prefer raw platform format names
        ui::ClipboardFormatType rawFormat(ui::ClipboardFormatType::CustomPlatformType(formatSstring));
        bool rawFormatAvailable = clipboard->IsFormatAvailable(rawFormat, ui::ClipboardBuffer::kCopyPaste, /* data_dst = */ nullptr);
#if BUILDFLAG(IS_LINUX)
        if (!rawFormatAvailable) {
            rawFormatAvailable = clipboard->IsFormatAvailable(rawFormat, ui::ClipboardBuffer::kSelection, /* data_dst = */ nullptr);
        }
#endif
        if (rawFormatAvailable) {
            std::string data;
            clipboard->ReadData(rawFormat, /* data_dst = */ nullptr, &data);
            return data;
        }
        // Otherwise, resolve custom format names
        std::map<std::string, std::string> customFormatNames;
        customFormatNames = clipboard->ExtractCustomPlatformNames(ui::ClipboardBuffer::kCopyPaste, /* data_dst = */ nullptr);
#if BUILDFLAG(IS_LINUX)
        if (!base::Contains(customFormatNames, formatSstring)) {
            customFormatNames = clipboard->ExtractCustomPlatformNames(ui::ClipboardBuffer::kSelection, /* data_dst = */ nullptr);
        }
#endif

        ui::ClipboardFormatType format;
        if (base::Contains(customFormatNames, formatSstring)) {
            format = ui::ClipboardFormatType(ui::ClipboardFormatType::CustomPlatformType(customFormatNames[formatSstring]));
        } else {
            format = ui::ClipboardFormatType(ui::ClipboardFormatType::CustomPlatformType(formatSstring));
        }
        std::string data;
        clipboard->ReadData(format, /* data_dst = */ nullptr, &data);
        return data;
#endif
    }

    v8::Local<v8::Value> readBufferApi(const std::string& formatSstring, gin_helper::Arguments* args) 
    {
        std::string data = readImpl(formatSstring);
        return node::Buffer::Copy(args->isolate(), data.data(), data.length()).ToLocalChecked();
    }

    void Clipboard::writeBufferApi(const std::string& format, const v8::Local<v8::Value> buffer)
    {
        v8::Isolate* isolate = v8::Isolate::GetCurrent();
        if (!node::Buffer::HasInstance(buffer)) {
            isolate->ThrowError("buffer must be a node Buffer");
            return;
        }

        CHECK(buffer->IsArrayBufferView());
        v8::Local<v8::ArrayBufferView> buffer_view = buffer.As<v8::ArrayBufferView>();
        const size_t n_bytes = buffer_view->ByteLength();
#if defined(__APPLE__)
        std::vector<char> data(n_bytes);
        [[maybe_unused]] const size_t n_got = buffer_view->CopyContents(data.data(), n_bytes);
        DCHECK_EQ(n_got, n_bytes);
        writeClipboardBytes(format, data.data(), data.size());
#else
        mojo_base::BigBuffer big_buffer{ n_bytes };
        [[maybe_unused]] const size_t n_got = buffer_view->CopyContents(big_buffer.data(), n_bytes);
        DCHECK_EQ(n_got, n_bytes);

        ui::ScopedClipboardWriter writer(/*GetClipboardBuffer(args)*/ui::ClipboardBuffer::kCopyPaste);
        writer.WriteData(base::UTF8ToUTF16(format), std::move(big_buffer));
#endif
    }

    void _writeImageApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        NativeImage* nativeImage = nullptr;
        if (args[0]->IsObject()) {
            v8::Local<v8::Object> handle = args[0]->ToObject(context).ToLocalChecked();
            nativeImage = NativeImage::GetSelf(handle);
        }
        if (!nativeImage)
            return;

        std::string type;
        if (args[1]->IsString()) {
            v8::Local<v8::String> str = args[0]->ToString(context).ToLocalChecked();
            v8::String::Utf8Value stringUtf8(isolate, str);
            if (0 != stringUtf8.length())
                type.assign(*stringUtf8);
        }

        HBITMAP sourceBitmap = nativeImage->getBitmap();
        int width = nativeImage->getWidth();
        int height = nativeImage->getHeight();
        if (!sourceBitmap || 0 == width || 0 == height)
            return;

        ::EmptyClipboard();

        HDC dc = ::GetDC(NULL);
        HDC compatibleDC = ::CreateCompatibleDC(NULL);
        HDC sourceDC = ::CreateCompatibleDC(NULL);

        // This is the HBITMAP we will eventually write to the clipboard
        HBITMAP hbitmap = ::CreateCompatibleBitmap(dc, width, height);
        if (!hbitmap) {
            // Failed to create the bitmap
            ::DeleteDC(compatibleDC);
            ::DeleteDC(sourceDC);
            ::ReleaseDC(NULL, dc);
            return;
        }

        HBITMAP oldBitmap = (HBITMAP)SelectObject(compatibleDC, hbitmap);
        HBITMAP oldSource = (HBITMAP)SelectObject(sourceDC, sourceBitmap);

        // Now we need to blend it into an HBITMAP we can place on the clipboard
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ::GdiAlphaBlend(compatibleDC, 0, 0, width, height, sourceDC, 0, 0, width, height, bf);

        // Clean up all the handles we just opened
        ::SelectObject(compatibleDC, oldBitmap);
        ::SelectObject(sourceDC, oldSource);
        ::DeleteObject(oldBitmap);
        ::DeleteObject(oldSource);
        ::DeleteDC(compatibleDC);
        ::DeleteDC(sourceDC);
        ::ReleaseDC(NULL, dc);

        ::OpenClipboard(NULL);
        ::SetClipboardData(CF_BITMAP, hbitmap);
        ::CloseClipboard();
    }

    static void newFunction(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        if (args.IsConstructCall()) {
            new Clipboard(isolate, args.This());
            args.GetReturnValue().Set(args.This());
            return;
        }
    }

    std::string _readTextApi(const std::string& type)
    {
        if (!::OpenClipboard(nullptr))
            return std::string();

        HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
        if (!data) {
            ::CloseClipboard();
            return std::string();
        }

        std::u16string text = readUtf16ClipboardText(data);
        ::CloseClipboard();
        return base::UTF16ToUTF8(text);
    }

    void _writeTextApi(const std::string& text, const std::string& type)
    {
        if (0 == text.size())
            return;

        std::u16string strW(base::UTF8ToUTF16(text));
        HGLOBAL data = ::GlobalAlloc(GMEM_MOVEABLE, ((strW.size() + 1) * sizeof(WCHAR)));
        if (!data)
            return;

        WCHAR* rawData = static_cast<WCHAR*>(::GlobalLock(data));
        memcpy(rawData, strW.data(), strW.size() * sizeof(WCHAR));
        rawData[strW.size()] = 0;
        ::GlobalUnlock(data);

        ::EmptyClipboard();

        if (!::OpenClipboard(nullptr))
            return;
        ::SetClipboardData(CF_UNICODETEXT, data);
        ::CloseClipboard();
    }

    v8::Local<v8::Object> readImage(const std::string& type)
    {
        if (!::OpenClipboard(nullptr))
            return NativeImage::createEmpty(isolate());

        HANDLE hBitmap = ::GetClipboardData(CF_DIB);
        if (!hBitmap) {
            ::CloseClipboard();
            return NativeImage::createEmpty(isolate());
        }

        BITMAPINFO* bitmap = static_cast<BITMAPINFO*>(::GlobalLock(hBitmap));
        if (!bitmap) {
            ::CloseClipboard();
            return NativeImage::createEmpty(isolate());
        }
        int colorTableLength = 0;
        switch (bitmap->bmiHeader.biBitCount) {
        case 1:
        case 4:
        case 8:
            colorTableLength = bitmap->bmiHeader.biClrUsed ? bitmap->bmiHeader.biClrUsed : 1 << bitmap->bmiHeader.biBitCount;
            break;
        case 16:
        case 32:
            if (bitmap->bmiHeader.biCompression == BI_BITFIELDS)
                colorTableLength = 3;
            break;
        case 24:
            break;
        default:
            DebugBreak();
        }
        void* bitmapBits = reinterpret_cast<char*>(bitmap) + bitmap->bmiHeader.biSize + colorTableLength * sizeof(RGBQUAD);
        size_t size = bitmap->bmiHeader.biWidth * bitmap->bmiHeader.biHeight * 4;
        v8::Local<v8::Object> obj = NativeImage::createFromBITMAPINFO(isolate(), bitmap, bitmapBits);
        ::GlobalUnlock(hBitmap);
        ::CloseClipboard();
        return obj;
    }

    v8::Local<v8::Object> _readImageApi(const std::string& type)
    {
        return readImage(type);
    }

public:
    static gin_helper::WrapperInfo kWrapperInfo;
};
gin_helper::WrapperInfo Clipboard::kWrapperInfo = { gin_helper::GinEmbedder::kEmbedderNativeGin };

void initializeClipboardApi(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused, v8::Local<v8::Context> context, void* priv)
{
    Clipboard::init(context->GetIsolate(), exports);
}

} // atom namespace

static const char CommonClipboardNative[] = "console.log('CommonClipboardNative');;";
static NodeNative nativeCommonClipboard { "Clipboard", CommonClipboardNative, sizeof(CommonClipboardNative) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_common_clipboard, atom::initializeClipboardApi, &nativeCommonClipboard)
