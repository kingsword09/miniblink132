// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/common/api/ApiNativeImage.h"

#include "electron/nodeblink.h"
#include "electron/common/asar/Archive.h"
#include "electron/common/asar/AsarUtil.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/gin_helper/wrappable.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libnode/src/node_buffer.h"
#include "third_party/libnode/src/node_version.h"
#include "third_party/libuv/include/uv.h"
#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_local.h"
#include "mbvip/core/mb.h"

#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ImageIO/ImageIO.h>
#else
#undef min
#undef max
using std::max;
using std::min;

#include <Unknwn.h>
#include <gdiplus.h>
#include <objidl.h>
#include "electron/common/InitGdiPlus.h"
#endif

#include <string.h>
#include <vector>

namespace atom {

THREAD_LOCAL_CONSTRUCTOR(NativeImage)

namespace {

v8::Local<v8::Object> copyBytesToNodeBuffer(v8::Isolate* isolate, const unsigned char* data, size_t size)
{
    if (!data && size)
        return v8::Local<v8::Object>();
    return node::Buffer::Copy(isolate, reinterpret_cast<const char*>(data), size).ToLocalChecked();
}

#if defined(__APPLE__)

CGImageRef createImageFromBuffer(const unsigned char* data, size_t size)
{
    if (!data || !size)
        return nullptr;

    CFDataRef imageData = CFDataCreate(kCFAllocatorDefault, data, static_cast<CFIndex>(size));
    if (!imageData)
        return nullptr;

    CGDataProviderRef provider = CGDataProviderCreateWithCFData(imageData);
    CGImageRef image = provider ? CGImageCreateWithPNGDataProvider(provider, nullptr, true, kCGRenderingIntentDefault) : nullptr;
    if (!image) {
        CGImageSourceRef source = provider ? CGImageSourceCreateWithDataProvider(provider, nullptr) : CGImageSourceCreateWithData(imageData, nullptr);
        if (source) {
            image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
            CFRelease(source);
        }
    }

    if (provider)
        CGDataProviderRelease(provider);
    CFRelease(imageData);
    return image;
}

CGImageRef createRGBACopy(CGImageRef image, bool dropAlpha)
{
    if (!image)
        return nullptr;

    size_t width = CGImageGetWidth(image);
    size_t height = CGImageGetHeight(image);
    if (!width || !height)
        return nullptr;

    std::vector<unsigned char> pixels(width * height * 4);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo = static_cast<CGBitmapInfo>(
        static_cast<uint32_t>(kCGBitmapByteOrder32Little)
        | static_cast<uint32_t>(dropAlpha ? kCGImageAlphaNoneSkipFirst : kCGImageAlphaPremultipliedFirst));
    CGContextRef context = CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, colorSpace, bitmapInfo);
    if (colorSpace)
        CGColorSpaceRelease(colorSpace);
    if (!context)
        return nullptr;

    if (dropAlpha) {
        CGContextSetRGBFillColor(context, 0, 0, 0, 1);
        CGContextFillRect(context, CGRectMake(0, 0, width, height));
    }
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGImageRef copy = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    return copy;
}

std::vector<unsigned char>* encodeImage(CGImageRef image, CFStringRef type, double quality)
{
    if (!image)
        return nullptr;

    bool isJpeg = CFEqual(type, CFSTR("public.jpeg"));
    CGImageRef normalized = createRGBACopy(image, isJpeg);
    CGImageRef imageToEncode = normalized ? normalized : image;

    CFMutableDataRef outputData = CFDataCreateMutable(kCFAllocatorDefault, 0);
    if (!outputData) {
        if (normalized)
            CGImageRelease(normalized);
        return nullptr;
    }

    CGImageDestinationRef destination = CGImageDestinationCreateWithData(outputData, type, 1, nullptr);
    if (!destination) {
        CFRelease(outputData);
        if (normalized)
            CGImageRelease(normalized);
        return nullptr;
    }

    CFDictionaryRef options = nullptr;
    if (quality >= 0) {
        CFNumberRef qualityValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &quality);
        const void* keys[] = { kCGImageDestinationLossyCompressionQuality };
        const void* values[] = { qualityValue };
        options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (qualityValue)
            CFRelease(qualityValue);
    }

    CGImageDestinationAddImage(destination, imageToEncode, options);
    bool ok = CGImageDestinationFinalize(destination);
    if (normalized)
        CGImageRelease(normalized);
    if (options)
        CFRelease(options);
    CFRelease(destination);

    std::vector<unsigned char>* output = nullptr;
    if (ok) {
        CFIndex size = CFDataGetLength(outputData);
        if (size > 0 && size <= 2024 * 2024) {
            output = new std::vector<unsigned char>(static_cast<size_t>(size));
            CFDataGetBytes(outputData, CFRangeMake(0, size), output->data());
        }
    }
    CFRelease(outputData);
    return output;
}

#endif

} // namespace

NativeImage::NativeImage(v8::Isolate* isolate, v8::Local<v8::Object> wrapper)
{
#if defined(__APPLE__)
    m_image = nullptr;
#else
    m_gdipBitmap = nullptr;
#endif
    gin_helper::Wrappable<NativeImage>::InitWith(isolate, wrapper);
}

NativeImage::~NativeImage()
{
#if defined(__APPLE__)
    if (m_image)
        CGImageRelease(m_image);
#else
    delete m_gdipBitmap;
#endif
}

void NativeImage::init(v8::Isolate* isolate, v8::Local<v8::Object> target)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::FunctionTemplate> prototype = v8::FunctionTemplate::New(isolate, newFunction);

    prototype->SetClassName(v8::String::NewFromUtf8(isolate, "NativeImage").ToLocalChecked());
    gin_helper::ObjectTemplateBuilder builder(isolate, prototype->InstanceTemplate());
    builder.SetMethod("toPNG", &NativeImage::toPNGAPI);
    builder.SetMethod("toJPEG", &NativeImage::toJpeg);
    builder.SetMethod("toBitmap", &NativeImage::toBitmap);
    builder.SetMethod("toDataURL", &NativeImage::toDataURLApi);
    builder.SetMethod("isEmpty", &NativeImage::isEmptyApi);
    builder.SetMethod("getSize", &NativeImage::getSizeApi);

    getNativeImageConstructor().Reset(isolate, prototype->GetFunction(context).ToLocalChecked());
    target->Set(context, v8::String::NewFromUtf8(isolate, "NativeImage").ToLocalChecked(), prototype->GetFunction(context).ToLocalChecked());

    gin_helper::Dictionary nativeImageClass(isolate, prototype->GetFunction(context).ToLocalChecked());
    nativeImageClass.SetMethod("createEmpty", &NativeImage::createEmptyApi);
    nativeImageClass.SetMethod("createFromPath", &NativeImage::createFromPathApi);
    nativeImageClass.SetMethod("createFromBuffer", &NativeImage::createFromBufferApi);
}

#if !defined(__APPLE__)
std::vector<unsigned char>* NativeImage::encodeToBuffer(const CLSID* clsid)
{
    IStream* pIStream = nullptr;

    std::vector<unsigned char>* output = nullptr;
    bool ok = false;
    HRESULT hr = ::CreateStreamOnHGlobal(NULL, true, &pIStream);
    if (S_OK != hr)
        return nullptr;
    Gdiplus::Status status = m_gdipBitmap->Save(pIStream, clsid, NULL);

    LARGE_INTEGER liTemp = { 0 };
    pIStream->Seek(liTemp, STREAM_SEEK_SET, NULL);
    DWORD dwSize = 0;
    STATSTG stats = { 0 };
    pIStream->Stat(&stats, 0);

    do {
        if (0 == stats.cbSize.QuadPart || stats.cbSize.QuadPart > 2024 * 2024)
            break;

        dwSize = (DWORD)stats.cbSize.QuadPart;
        output = new std::vector<unsigned char>();
        output->resize(dwSize);

        ULONG readSize = 0;
        hr = pIStream->Read(output->data(), dwSize, &readSize);
        ok = (S_OK == hr);
    } while (false);

    if (!ok && output) {
        delete output;
        output = nullptr;
    }

    if (pIStream)
        pIStream->Release();

    return output;
}
#endif

v8::Local<v8::Object> NativeImage::toPNGAPI()
{
#if defined(__APPLE__)
    std::vector<unsigned char>* output = encodeImage(m_image, CFSTR("public.png"), -1);
#else
    std::vector<unsigned char>* output = encodeToBuffer(&s_pngClsid);
#endif
    if (!output)
        return v8::Local<v8::Object>();

    v8::Local<v8::Object> result = copyBytesToNodeBuffer(isolate(), output->data(), output->size());

    delete output;

    return result;
}

v8::Local<v8::Object> NativeImage::toJpeg()
{
#if defined(__APPLE__)
    std::vector<unsigned char>* output = encodeImage(m_image, CFSTR("public.jpeg"), 1.0);
#else
    std::vector<unsigned char>* output = encodeToBuffer(&s_jpgClsid);
#endif
    if (!output)
        return v8::Local<v8::Object>();

    v8::Local<v8::Object> result = copyBytesToNodeBuffer(isolate(), output->data(), output->size());
    delete output;

    return result;
}

v8::Local<v8::Object> NativeImage::toBitmap()
{
#if defined(__APPLE__)
    if (!m_image)
        return v8::Local<v8::Object>();

    int width = getWidth();
    int height = getHeight();
    if (width <= 0 || height <= 0)
        return v8::Local<v8::Object>();

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo = static_cast<CGBitmapInfo>(
        static_cast<uint32_t>(kCGBitmapByteOrder32Little)
        | static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst));
    CGContextRef context = CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, colorSpace,
        bitmapInfo);
    if (colorSpace)
        CGColorSpaceRelease(colorSpace);
    if (!context)
        return v8::Local<v8::Object>();

    CGContextDrawImage(context, CGRectMake(0, 0, width, height), m_image);
    CGContextRelease(context);
    return copyBytesToNodeBuffer(isolate(), pixels.data(), pixels.size());
#else
    UINT w = m_gdipBitmap->GetWidth();
    UINT h = m_gdipBitmap->GetHeight();

    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData lockedBitmapData;
    m_gdipBitmap->LockBits(
#if USING_VC6RT != 1
        &
#endif
        rect,
        Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &lockedBitmapData);

    v8::Local<v8::Object> result;
    const char* data = reinterpret_cast<const char*>(lockedBitmapData.Scan0);
    if (!data)
        return result;

    int stride = lockedBitmapData.Stride;
    size_t size = w * stride / 4 + h;
    result = node::Buffer::Copy(isolate(), data, size).ToLocalChecked();

    return result;
#endif
}

v8::Local<v8::Object> NativeImage::createEmpty(v8::Isolate* isolate)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Function> constructorFunction = v8::Local<v8::Function>::New(isolate, getNativeImageConstructor());
    v8::Local<v8::Object> obj = constructorFunction->NewInstance(context).ToLocalChecked();

    NativeImage* self = (NativeImage*)WrappableBase::GetNativePtr(obj, &kWrapperInfo);
    return obj;
}

void NativeImage::createEmptyApi(const v8::FunctionCallbackInfo<v8::Value> info)
{
    info.GetReturnValue().Set(createEmpty(info.GetIsolate()));
}

void NativeImage::createFromPathApi(const v8::FunctionCallbackInfo<v8::Value> info /*v8::Isolate* isolate, const std::string& path*/)
{
    std::string fileContents;
    std::string path;
    if (info.Length() == 1 && info[0]->IsString()) {
        v8::String::Utf8Value pathString(info.GetIsolate(), info[0]);
        path = *pathString;
    }

    if (0 == path.size())
        return;
    base::FilePath filePath = base::FilePath::FromUTF8Unsafe(path);
#if defined(__APPLE__)
    if (!base::ReadFileToString(filePath, &fileContents))
        return;
#else
    if (!asar::readFileToString(filePath, &fileContents))
        return;
#endif

    const unsigned char* data = reinterpret_cast<const unsigned char*>(fileContents.data());
    size_t size = fileContents.size();

    v8::Local<v8::Object> obj = createNativeImageFromBuffer(info.GetIsolate(), data, size);
    info.GetReturnValue().Set(obj);
}

void NativeImage::createFromBufferApi(const v8::FunctionCallbackInfo<v8::Value> info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Value> buffer = info.Length() >= 1 ? info[0] : v8::Local<v8::Value>();
    v8::Local<v8::Object> resultObj;
    if (buffer.IsEmpty() || !buffer->IsUint8Array()) {
        info.GetReturnValue().Set(resultObj);
        return;
    }

    const unsigned char* data = nullptr;
    size_t size = 0;
    if (node::Buffer::HasInstance(buffer)) {
        data = reinterpret_cast<const unsigned char*>(node::Buffer::Data(buffer));
        size = node::Buffer::Length(buffer);
    } else {
        v8::Local<v8::Uint8Array> view = buffer.As<v8::Uint8Array>();
        std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
        data = static_cast<const unsigned char*>(backing->Data()) + view->ByteOffset();
        size = view->ByteLength();
    }

    resultObj = createNativeImageFromBuffer(isolate, data, size);
    info.GetReturnValue().Set(resultObj);
}

v8::Local<v8::Object> NativeImage::createFromBITMAPINFO(v8::Isolate* isolate, const BITMAPINFO* gdiBitmapInfo, void* gdiBitmapData)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Function> constructorFunction = v8::Local<v8::Function>::New(isolate, getNativeImageConstructor());
    v8::Local<v8::Object> resultObj = constructorFunction->NewInstance(context).ToLocalChecked();
    NativeImage* self = (NativeImage*)WrappableBase::GetNativePtr(resultObj, &NativeImage::kWrapperInfo);

#if defined(__APPLE__)
    if (!gdiBitmapInfo || !gdiBitmapData)
        return resultObj;

    const BITMAPINFOHEADER& header = gdiBitmapInfo->bmiHeader;
    if (header.biBitCount != 32 || header.biWidth <= 0 || header.biHeight == 0)
        return resultObj;

    int width = header.biWidth;
    int height = header.biHeight < 0 ? -header.biHeight : header.biHeight;
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, gdiBitmapData, static_cast<size_t>(width) * height * 4, nullptr);
    if (colorSpace && provider) {
        CGBitmapInfo bitmapInfo = static_cast<CGBitmapInfo>(
            static_cast<uint32_t>(kCGBitmapByteOrder32Little)
            | static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst));
        self->m_image = CGImageCreate(width, height, 8, 32, width * 4, colorSpace, bitmapInfo, provider, nullptr, true, kCGRenderingIntentDefault);
    }
    if (provider)
        CGDataProviderRelease(provider);
    if (colorSpace)
        CGColorSpaceRelease(colorSpace);
    return resultObj;
#else
    Gdiplus::Bitmap* gdipBitmap = Gdiplus::Bitmap::FromBITMAPINFO(gdiBitmapInfo, gdiBitmapData);
    if (!gdipBitmap)
        return v8::Local<v8::Object>();

    UINT w = gdipBitmap->GetWidth();
    UINT h = gdipBitmap->GetHeight();
    Gdiplus::Rect rect(0, 0, w, h);
    self->m_gdipBitmap = gdipBitmap->Clone(rect, PixelFormat32bppARGB); // Clone the bitmap so the source can be released safely.
    delete gdipBitmap;

    return resultObj;
#endif
}

void NativeImage::createFromBufferImpl(const unsigned char* data, size_t size)
{
#if defined(__APPLE__)
    if (m_image) {
        CGImageRelease(m_image);
        m_image = nullptr;
    }
    m_image = createImageFromBuffer(data, size);
#else
    delete m_gdipBitmap;
    m_gdipBitmap = nullptr;
    HGLOBAL memHandle = ::GlobalAlloc(GMEM_FIXED, size);
    BYTE* pMem = (BYTE*)::GlobalLock(memHandle);
    memcpy(pMem, data, size);
    ::GlobalUnlock(memHandle);

    IStream* istream = nullptr;
    ::CreateStreamOnHGlobal(memHandle, FALSE, &istream);
    m_gdipBitmap = Gdiplus::Bitmap::FromStream(istream);

    if (memHandle)
        ::GlobalFree(memHandle);

    if (istream)
        istream->Release();
#endif
}

v8::Local<v8::Object> NativeImage::createNativeImageFromBuffer(v8::Isolate* isolate, const unsigned char* data, size_t size)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Function> constructorFunction = v8::Local<v8::Function>::New(isolate, getNativeImageConstructor());
    v8::Local<v8::Object> resultObj = constructorFunction->NewInstance(context).ToLocalChecked();
    NativeImage* self = (NativeImage*)WrappableBase::GetNativePtr(resultObj, &NativeImage::kWrapperInfo);
    self->createFromBufferImpl(data, size);

    return resultObj;
}

std::string NativeImage::toDataURLApi()
{
    if (isEmptyApi())
        return "";
#if defined(__APPLE__)
    std::vector<unsigned char>* output = encodeImage(m_image, CFSTR("public.jpeg"), 1.0);
#else
    UINT w = m_gdipBitmap->GetWidth();
    UINT h = m_gdipBitmap->GetHeight();

    Gdiplus::Rect rect(0, 0, w, h);
    std::vector<unsigned char>* output = encodeToBuffer(&s_jpgClsid);
#endif
    if (!output)
        return "";

    std::string ret("data:image/jpeg;base64,");
    ret += base::Base64Encode(base::span<const uint8_t>(output->data(), output->size()));
    delete output;
    return ret;
}

bool NativeImage::isEmptyApi() const
{
#if defined(__APPLE__)
    return !m_image;
#else
    return !m_gdipBitmap;
#endif
}

v8::Local<v8::Object> NativeImage::getSizeApi() const
{
    v8::Local<v8::Object> size = v8::Object::New(isolate());
    v8::Local<v8::Context> context = isolate()->GetCurrentContext();
    size->Set(context, v8::String::NewFromUtf8(isolate(), "width").ToLocalChecked(), v8::Integer::New(isolate(), getWidth())).ToChecked();
    size->Set(context, v8::String::NewFromUtf8(isolate(), "height").ToLocalChecked(), v8::Integer::New(isolate(), getHeight())).ToChecked();
    return size;
}

void NativeImage::newFunction(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    if (args.IsConstructCall()) {
        new NativeImage(isolate, args.This());
        args.GetReturnValue().Set(args.This());
        return;
    }
}

NativeImage* NativeImage::GetSelf(v8::Local<v8::Object> handle)
{
    return (NativeImage*)WrappableBase::GetNativePtr(handle, &kWrapperInfo);
}

HICON NativeImage::getIcon()
{
#if defined(__APPLE__)
    return NULL;
#else
    HICON hIcon = NULL;
    if (m_gdipBitmap)
        m_gdipBitmap->GetHICON(&hIcon);
    return hIcon;
#endif
}

HBITMAP NativeImage::getBitmap()
{
#if defined(__APPLE__)
    return NULL;
#else
    HBITMAP hBitmap = NULL;
    Gdiplus::Color colorBackground = 0xff000000;
    if (m_gdipBitmap)
        m_gdipBitmap->GetHBITMAP(colorBackground, &hBitmap);
    return hBitmap;
#endif
}

int NativeImage::getWidth() const
{
#if defined(__APPLE__)
    if (m_image)
        return static_cast<int>(CGImageGetWidth(m_image));
#else
    if (m_gdipBitmap)
        return m_gdipBitmap->GetWidth();
#endif
    return 0;
}
int NativeImage::getHeight() const
{
#if defined(__APPLE__)
    if (m_image)
        return static_cast<int>(CGImageGetHeight(m_image));
#else
    if (m_gdipBitmap)
        return m_gdipBitmap->GetHeight();
#endif
    return 0;
}

gin_helper::WrapperInfo NativeImage::kWrapperInfo = { gin_helper::GinEmbedder::kEmbedderNativeGin };

void initializeNativeImageApi(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused, v8::Local<v8::Context> context, void* priv)
{
    NativeImage::init(context->GetIsolate(), exports);
}

} // atom namespace

static const char CommonNativeImageNative[] = "console.log('BrowserNativeImageNative');;";
static NodeNative nativeCommonNativeImageNative { "NativeImage", CommonNativeImageNative, sizeof(CommonNativeImageNative) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_common_nativeImage, atom::initializeNativeImageApi, &nativeCommonNativeImageNative)
