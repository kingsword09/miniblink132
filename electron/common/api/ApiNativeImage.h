
#ifndef common_api_ApiNativeImageExport_h
#define common_api_ApiNativeImageExport_h

#include "common/api/EventEmitter.h"
#include "v8.h"
#include <windows.h>

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#else
namespace Gdiplus {
class Bitmap;
}
#endif

namespace atom {

class NativeImage : public mate::EventEmitter<NativeImage> {
public:
    NativeImage(v8::Isolate* isolate, v8::Local<v8::Object> wrapper);
    ~NativeImage() override;
    static void init(v8::Isolate* isolate, v8::Local<v8::Object> target);
    v8::Local<v8::Object> toPNGAPI();
    v8::Local<v8::Object> toJpeg();
    v8::Local<v8::Object> toBitmap();
    std::string toDataURLApi();
    bool isEmptyApi() const;
    v8::Local<v8::Object> getSizeApi() const;

    static v8::Local<v8::Object> createEmpty(v8::Isolate* isolate);
    static void createEmptyApi(const v8::FunctionCallbackInfo<v8::Value> info);
    static void createFromPathApi(const v8::FunctionCallbackInfo<v8::Value> info);
    static void createFromBufferApi(const v8::FunctionCallbackInfo<v8::Value> info);
    static v8::Local<v8::Object> createFromBITMAPINFO(v8::Isolate* isolate, const BITMAPINFO* gdiBitmapInfo, void* gdiBitmapData);
    static v8::Local<v8::Object> createNativeImageFromBuffer(v8::Isolate* isolate, const unsigned char* data, size_t size);

    static NativeImage* GetSelf(v8::Local<v8::Object> handle);
    HICON getIcon();
    HBITMAP getBitmap();

    int getWidth() const;
    int getHeight() const;

public:
    void createFromBufferImpl(const unsigned char* data, size_t size);
    static gin::WrapperInfo kWrapperInfo;

private:
    static void newFunction(const v8::FunctionCallbackInfo<v8::Value>& args);
#if defined(__APPLE__)
    CGImageRef m_image;
#else
    std::vector<unsigned char>* encodeToBuffer(const CLSID* clsid);
    Gdiplus::Bitmap* m_gdipBitmap;
#endif
};

}

#endif // common_api_ApiNativeImageExport_h
