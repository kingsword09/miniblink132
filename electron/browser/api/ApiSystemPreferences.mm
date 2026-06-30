#include "electron/common/NodeRegisterHelp.h"
#include "third_party/libnode/src/node_binding.h"

#include <algorithm>
#include <string>

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <ApplicationServices/ApplicationServices.h>
#import <dispatch/dispatch.h>

namespace {

struct NodeNative {
    const char* name;
    const char* source;
    size_t sourceLen;
};

NSString* stringFromV8(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    v8::String::Utf8Value utf8(isolate, value);
    if (!*utf8)
        return nil;
    return [NSString stringWithUTF8String:*utf8];
}

v8::Local<v8::String> v8String(v8::Isolate* isolate, const char* value)
{
    return v8::String::NewFromUtf8(isolate, value).ToLocalChecked();
}

v8::Local<v8::String> nsStringToV8(v8::Isolate* isolate, NSString* value)
{
    if (!value)
        value = @"";
    return v8String(isolate, [value UTF8String]);
}

NSColor* colorForName(NSString* color)
{
    static NSDictionary<NSString*, NSColor*>* colors = nil;
    if (!colors) {
        colors = @{
            @"alternate-selected-control-text": NSColor.alternateSelectedControlTextColor,
            @"control-background": NSColor.controlBackgroundColor,
            @"control": NSColor.controlColor,
            @"control-text": NSColor.controlTextColor,
            @"disabled-control-text": NSColor.disabledControlTextColor,
            @"find-highlight": NSColor.findHighlightColor,
            @"grid": NSColor.gridColor,
            @"header-text": NSColor.headerTextColor,
            @"highlight": NSColor.highlightColor,
            @"keyboard-focus-indicator": NSColor.keyboardFocusIndicatorColor,
            @"label": NSColor.labelColor,
            @"link": NSColor.linkColor,
            @"placeholder-text": NSColor.placeholderTextColor,
            @"quaternary-label": NSColor.quaternaryLabelColor,
            @"scrubber-textured-background": NSColor.scrubberTexturedBackgroundColor,
            @"secondary-label": NSColor.secondaryLabelColor,
            @"selected-content-background": NSColor.selectedContentBackgroundColor,
            @"selected-control": NSColor.selectedControlColor,
            @"selected-control-text": NSColor.selectedControlTextColor,
            @"selected-menu-item-text": NSColor.selectedMenuItemTextColor,
            @"selected-text-background": NSColor.selectedTextBackgroundColor,
            @"selected-text": NSColor.selectedTextColor,
            @"separator": NSColor.separatorColor,
            @"shadow": NSColor.shadowColor,
            @"tertiary-label": NSColor.tertiaryLabelColor,
            @"text-background": NSColor.textBackgroundColor,
            @"text": NSColor.textColor,
            @"under-page-background": NSColor.underPageBackgroundColor,
            @"unemphasized-selected-content-background": NSColor.unemphasizedSelectedContentBackgroundColor,
            @"unemphasized-selected-text-background": NSColor.unemphasizedSelectedTextBackgroundColor,
            @"unemphasized-selected-text": NSColor.unemphasizedSelectedTextColor,
            @"window-background": NSColor.windowBackgroundColor,
            @"window-frame-text": NSColor.windowFrameTextColor,
        };
    }
    return colors[color.lowercaseString];
}

NSString* hexForColor(NSColor* color)
{
    if (!color)
        return @"";
    NSColor* rgb = [color colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
    if (!rgb && color.CGColor)
        rgb = [NSColor colorWithCGColor:color.CGColor];
    if (!rgb)
        return @"";

    CGFloat red = 0;
    CGFloat green = 0;
    CGFloat blue = 0;
    CGFloat alpha = 0;
    [rgb getRed:&red green:&green blue:&blue alpha:&alpha];
    unsigned r = static_cast<unsigned>(std::max(0.0, std::min(255.0, red * 255.0 + 0.5)));
    unsigned g = static_cast<unsigned>(std::max(0.0, std::min(255.0, green * 255.0 + 0.5)));
    unsigned b = static_cast<unsigned>(std::max(0.0, std::min(255.0, blue * 255.0 + 0.5)));
    return [NSString stringWithFormat:@"%02X%02X%02X", r, g, b];
}

AVMediaType mediaTypeFromString(NSString* mediaType)
{
    NSString* normalized = mediaType.lowercaseString;
    if ([normalized isEqualToString:@"microphone"] || [normalized isEqualToString:@"audio"])
        return AVMediaTypeAudio;
    if ([normalized isEqualToString:@"camera"] || [normalized isEqualToString:@"video"])
        return AVMediaTypeVideo;
    return nil;
}

const char* statusToString(AVAuthorizationStatus status)
{
    switch (status) {
    case AVAuthorizationStatusAuthorized:
        return "granted";
    case AVAuthorizationStatusDenied:
        return "denied";
    case AVAuthorizationStatusRestricted:
        return "restricted";
    case AVAuthorizationStatusNotDetermined:
    default:
        return "not-determined";
    }
}

void getAccentColorApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    @autoreleasepool {
        info.GetReturnValue().Set(nsStringToV8(info.GetIsolate(), hexForColor(NSColor.controlAccentColor)));
    }
}

void getColorApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "getColor requires a color name")));
        return;
    }

    @autoreleasepool {
        NSString* colorName = stringFromV8(isolate, info[0]);
        NSString* hex = hexForColor(colorForName(colorName));
        if (hex.length == 0) {
            isolate->ThrowException(v8::Exception::Error(v8String(isolate, "Unknown system color")));
            return;
        }
        info.GetReturnValue().Set(nsStringToV8(isolate, hex));
    }
}

void isDarkModeApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        NSArray<NSAppearanceName>* names = @[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ];
        NSAppearanceName appearance = [app.effectiveAppearance bestMatchFromAppearancesWithNames:names];
        info.GetReturnValue().Set([appearance isEqualToString:NSAppearanceNameDarkAqua]);
    }
}

void isSwipeTrackingFromScrollEventsEnabledApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    @autoreleasepool {
        BOOL enabled = [[NSUserDefaults standardUserDefaults] boolForKey:@"AppleEnableSwipeNavigateWithScrolls"];
        info.GetReturnValue().Set(enabled);
    }
}

void getUserDefaultApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "getUserDefault requires name and type strings")));
        return;
    }

    @autoreleasepool {
        NSString* key = stringFromV8(isolate, info[0]);
        NSString* type = stringFromV8(isolate, info[1]).lowercaseString;
        id value = [[NSUserDefaults standardUserDefaults] objectForKey:key];
        if (!value) {
            info.GetReturnValue().Set(v8::Null(isolate));
            return;
        }

        if ([type isEqualToString:@"string"]) {
            if ([value isKindOfClass:NSString.class])
                info.GetReturnValue().Set(nsStringToV8(isolate, value));
            else
                info.GetReturnValue().Set(nsStringToV8(isolate, [value description]));
        } else if ([type isEqualToString:@"boolean"] || [type isEqualToString:@"bool"]) {
            info.GetReturnValue().Set([[NSUserDefaults standardUserDefaults] boolForKey:key]);
        } else if ([type isEqualToString:@"integer"] || [type isEqualToString:@"int"]) {
            info.GetReturnValue().Set(v8::Integer::New(isolate, static_cast<int32_t>([[NSUserDefaults standardUserDefaults] integerForKey:key])));
        } else if ([type isEqualToString:@"float"] || [type isEqualToString:@"double"]) {
            info.GetReturnValue().Set(v8::Number::New(isolate, [[NSUserDefaults standardUserDefaults] doubleForKey:key]));
        } else {
            isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "Unsupported user default type")));
        }
    }
}

void setUserDefaultApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 3 || !info[0]->IsString() || !info[1]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "setUserDefault requires name, type, and value")));
        return;
    }

    @autoreleasepool {
        NSString* key = stringFromV8(isolate, info[0]);
        NSString* type = stringFromV8(isolate, info[1]).lowercaseString;
        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];

        if ([type isEqualToString:@"string"]) {
            v8::String::Utf8Value value(isolate, info[2]);
            [defaults setObject:[NSString stringWithUTF8String:*value ? *value : ""] forKey:key];
        } else if ([type isEqualToString:@"boolean"] || [type isEqualToString:@"bool"]) {
            [defaults setBool:info[2]->BooleanValue(isolate) forKey:key];
        } else if ([type isEqualToString:@"integer"] || [type isEqualToString:@"int"]) {
            [defaults setInteger:info[2]->Int32Value(isolate->GetCurrentContext()).FromMaybe(0) forKey:key];
        } else if ([type isEqualToString:@"float"] || [type isEqualToString:@"double"]) {
            [defaults setDouble:info[2]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0) forKey:key];
        } else {
            isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "Unsupported user default type")));
            return;
        }

        [defaults synchronize];
    }
}

void removeUserDefaultApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "removeUserDefault requires a name string")));
        return;
    }

    @autoreleasepool {
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:stringFromV8(isolate, info[0])];
        [[NSUserDefaults standardUserDefaults] synchronize];
    }
}

void isTrustedAccessibilityClientApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    bool prompt = info.Length() > 0 && info[0]->BooleanValue(info.GetIsolate());
    if (!prompt) {
        info.GetReturnValue().Set(AXIsProcessTrusted());
        return;
    }

    @autoreleasepool {
        NSDictionary* options = @{ (__bridge NSString*)kAXTrustedCheckOptionPrompt : @YES };
        info.GetReturnValue().Set(AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options));
    }
}

void getMediaAccessStatusApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "getMediaAccessStatus requires a media type")));
        return;
    }

    @autoreleasepool {
        AVMediaType mediaType = mediaTypeFromString(stringFromV8(isolate, info[0]));
        if (!mediaType) {
            isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "Unsupported media type")));
            return;
        }

        info.GetReturnValue().Set(v8String(isolate, statusToString([AVCaptureDevice authorizationStatusForMediaType:mediaType])));
    }
}

void askForMediaAccessApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "askForMediaAccess requires a media type and callback")));
        return;
    }

    @autoreleasepool {
        AVMediaType mediaType = mediaTypeFromString(stringFromV8(isolate, info[0]));
        if (!mediaType) {
            isolate->ThrowException(v8::Exception::TypeError(v8String(isolate, "Unsupported media type")));
            return;
        }

        v8::Global<v8::Function>* callback = new v8::Global<v8::Function>(isolate, info[1].As<v8::Function>());
        v8::Global<v8::Context>* context = new v8::Global<v8::Context>(isolate, isolate->GetCurrentContext());
        [AVCaptureDevice requestAccessForMediaType:mediaType completionHandler:^(BOOL granted) {
            dispatch_async(dispatch_get_main_queue(), ^{
                v8::Isolate* currentIsolate = isolate;
                v8::HandleScope handleScope(currentIsolate);
                v8::Local<v8::Context> localContext = v8::Local<v8::Context>::New(currentIsolate, *context);
                v8::Context::Scope contextScope(localContext);
                v8::Local<v8::Function> localCallback = v8::Local<v8::Function>::New(currentIsolate, *callback);
                v8::Local<v8::Value> argv[] = { v8::Boolean::New(currentIsolate, granted) };
                v8::Local<v8::Value> ignored;
                [[maybe_unused]] bool didCall = localCallback->Call(localContext, v8::Undefined(currentIsolate), 1, argv).ToLocal(&ignored);
                callback->Reset();
                context->Reset();
                delete callback;
                delete context;
            });
        }];
    }
}

void initializeSystemPreferencesApi(v8::Local<v8::Object> exports, v8::Local<v8::Value>, v8::Local<v8::Context> context, const NodeNative*)
{
    v8::Isolate* isolate = context->GetIsolate();
    exports->Set(context, v8String(isolate, "getAccentColor"), v8::FunctionTemplate::New(isolate, getAccentColorApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "getColor"), v8::FunctionTemplate::New(isolate, getColorApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "isDarkMode"), v8::FunctionTemplate::New(isolate, isDarkModeApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "isSwipeTrackingFromScrollEventsEnabled"), v8::FunctionTemplate::New(isolate, isSwipeTrackingFromScrollEventsEnabledApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "getUserDefault"), v8::FunctionTemplate::New(isolate, getUserDefaultApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "setUserDefault"), v8::FunctionTemplate::New(isolate, setUserDefaultApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "removeUserDefault"), v8::FunctionTemplate::New(isolate, removeUserDefaultApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "isTrustedAccessibilityClient"), v8::FunctionTemplate::New(isolate, isTrustedAccessibilityClientApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "getMediaAccessStatus"), v8::FunctionTemplate::New(isolate, getMediaAccessStatusApi)->GetFunction(context).ToLocalChecked()).ToChecked();
    exports->Set(context, v8String(isolate, "askForMediaAccess"), v8::FunctionTemplate::New(isolate, askForMediaAccessApi)->GetFunction(context).ToLocalChecked()).ToChecked();
}

const char SystemPreferencesScript[] = "exports = {};";
NodeNative nativeSystemPreferencesNative { "SystemPreferences", SystemPreferencesScript, sizeof(SystemPreferencesScript) - 1 };

} // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_browser_system_preferences, initializeSystemPreferencesApi, &nativeSystemPreferencesNative)
