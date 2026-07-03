#include "electron/common/PlatformUtil.h"

#include <windows.h>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ImageIO/ImageIO.h>

#include <limits>

namespace platform_util {

namespace {

struct LoadedIcon {
    HICON icon = nullptr;
};

CGImageRef createImageFromMemory(const uint8_t* data, size_t size)
{
    if (!data || !size || size > static_cast<size_t>(std::numeric_limits<CFIndex>::max()))
        return nullptr;

    CFDataRef imageData = CFDataCreate(kCFAllocatorDefault, data, static_cast<CFIndex>(size));
    if (!imageData)
        return nullptr;

    CGImageSourceRef source = CGImageSourceCreateWithData(imageData, nullptr);
    CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
    if (source)
        CFRelease(source);
    CFRelease(imageData);
    return image;
}

} // namespace

void* loadIconFromMemory(const uint8_t* data, size_t size, void* hIcon)
{
    if (hIcon)
        *static_cast<HICON*>(hIcon) = nullptr;

    CGImageRef image = createImageFromMemory(data, size);
    if (!image)
        return nullptr;

    size_t width = CGImageGetWidth(image);
    size_t height = CGImageGetHeight(image);
    CGImageRelease(image);

    if (!width || !height || width > INT_MAX || height > INT_MAX)
        return nullptr;

    HBITMAP bitmap = CreateBitmap(static_cast<int>(width), static_cast<int>(height), 1, 32, nullptr);
    if (!bitmap)
        return nullptr;

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = bitmap;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(bitmap);
    if (!icon)
        return nullptr;

    if (hIcon)
        *static_cast<HICON*>(hIcon) = icon;
    LoadedIcon* loaded = new LoadedIcon();
    loaded->icon = icon;
    return loaded;
}

void loadIconFromMemoryFree(void* picture)
{
    LoadedIcon* loaded = static_cast<LoadedIcon*>(picture);
    delete loaded;
}

} // namespace platform_util
