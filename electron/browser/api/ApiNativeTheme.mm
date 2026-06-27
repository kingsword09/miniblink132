#include "electron/common/NodeRegisterHelp.h"
#include "third_party/libnode/src/node_binding.h"

#include <stddef.h>

#import <Cocoa/Cocoa.h>

namespace {
class NativeThemeWatchState;

struct NodeNative {
    const char* name;
    const char* source;
    size_t sourceLen;
};
}

@interface MiniElectronNativeThemeObserver : NSObject {
@private
    NativeThemeWatchState* _state;
    BOOL _observingEffectiveAppearance;
    BOOL _observingAppearance;
}
- (instancetype)initWithState:(NativeThemeWatchState*)state;
- (void)invalidate;
@end

namespace {

bool shouldUseDarkColors()
{
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        NSArray<NSAppearanceName>* names = @[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ];
        NSAppearanceName appearance = [app.effectiveAppearance bestMatchFromAppearancesWithNames:names];
        return [appearance isEqualToString:NSAppearanceNameDarkAqua];
    }
}

class NativeThemeWatchState {
public:
    explicit NativeThemeWatchState(v8::Isolate* isolate)
        : m_isolate(isolate)
    {
    }

    ~NativeThemeWatchState()
    {
        stop();
    }

    void start(v8::Local<v8::Function> callback);
    void stop();

    void notify()
    {
        if (m_callback.IsEmpty())
            return;

        v8::HandleScope handleScope(m_isolate);
        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(m_isolate, m_context);
        v8::Context::Scope contextScope(context);
        v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(m_isolate, m_callback);
        v8::Local<v8::Value> argv[] = { v8::Boolean::New(m_isolate, shouldUseDarkColors()) };
        v8::Local<v8::Value> ignored;
        [[maybe_unused]] bool didCall = callback->Call(context, v8::Undefined(m_isolate), 1, argv).ToLocal(&ignored);
    }

private:
    v8::Isolate* m_isolate;
    v8::Global<v8::Function> m_callback;
    v8::Global<v8::Context> m_context;
    MiniElectronNativeThemeObserver* __strong m_observer = nil;
};

void NativeThemeWatchState::start(v8::Local<v8::Function> callback)
{
    stop();

    m_callback.Reset(m_isolate, callback);
    m_context.Reset(m_isolate, callback->GetCreationContext().ToLocalChecked());
    m_observer = [[MiniElectronNativeThemeObserver alloc] initWithState:this];
}

void NativeThemeWatchState::stop()
{
    if (m_observer) {
        [m_observer invalidate];
        m_observer = nil;
    }
    m_context.Reset();
    m_callback.Reset();
}

NativeThemeWatchState* watchStateForIsolate(v8::Isolate* isolate)
{
    static NativeThemeWatchState* state = nullptr;
    if (!state)
        state = new NativeThemeWatchState(isolate);
    return state;
}

void shouldUseDarkColorsApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    info.GetReturnValue().Set(shouldUseDarkColors());
}

void startWatchingApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    if (info.Length() < 1 || !info[0]->IsFunction()) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8(info.GetIsolate(), "callback is required").ToLocalChecked()));
        return;
    }

    v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(info[0]);
    watchStateForIsolate(info.GetIsolate())->start(callback);
    info.GetReturnValue().Set(callback);
}

void stopWatchingApi(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    watchStateForIsolate(info.GetIsolate())->stop();
}

void initializeNativeThemeApi(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused, v8::Local<v8::Context> context, const NodeNative* native)
{
    v8::Isolate* isolate = context->GetIsolate();

    exports->Set(context,
        v8::String::NewFromUtf8(isolate, "shouldUseDarkColors").ToLocalChecked(),
        v8::FunctionTemplate::New(isolate, shouldUseDarkColorsApi)->GetFunction(context).ToLocalChecked()).ToChecked();

    exports->Set(context,
        v8::String::NewFromUtf8(isolate, "startWatching").ToLocalChecked(),
        v8::FunctionTemplate::New(isolate, startWatchingApi)->GetFunction(context).ToLocalChecked()).ToChecked();

    exports->Set(context,
        v8::String::NewFromUtf8(isolate, "stopWatching").ToLocalChecked(),
        v8::FunctionTemplate::New(isolate, stopWatchingApi)->GetFunction(context).ToLocalChecked()).ToChecked();
}

const char NativeThemeScript[] = "exports = {};";
NodeNative nativeThemeNative { "ApiNativeTheme", NativeThemeScript, sizeof(NativeThemeScript) - 1 };

} // namespace

extern "C" bool electronMacNativeThemeSetAppearanceForTesting(bool dark)
{
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        NSAppearanceName name = dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
        NSAppearance* appearance = [NSAppearance appearanceNamed:name];
        if (!appearance)
            return false;

        [app setAppearance:appearance];
        NSArray<NSAppearanceName>* names = @[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ];
        NSAppearanceName effective = [[app effectiveAppearance] bestMatchFromAppearancesWithNames:names];
        return [effective isEqualToString:name];
    }
}

extern "C" void electronMacNativeThemeResetAppearanceForTesting()
{
    @autoreleasepool {
        [[NSApplication sharedApplication] setAppearance:nil];
    }
}

@implementation MiniElectronNativeThemeObserver

- (instancetype)initWithState:(NativeThemeWatchState*)state
{
    self = [super init];
    if (self) {
        _state = state;
        _observingEffectiveAppearance = YES;
        _observingAppearance = YES;
        [[NSApplication sharedApplication] addObserver:self
                                            forKeyPath:@"effectiveAppearance"
                                               options:0
                                               context:nullptr];
        [[NSApplication sharedApplication] addObserver:self
                                            forKeyPath:@"appearance"
                                               options:0
                                               context:nullptr];
    }
    return self;
}

- (void)invalidate
{
    if (_observingEffectiveAppearance) {
        [[NSApplication sharedApplication] removeObserver:self forKeyPath:@"effectiveAppearance"];
        _observingEffectiveAppearance = NO;
    }
    if (_observingAppearance) {
        [[NSApplication sharedApplication] removeObserver:self forKeyPath:@"appearance"];
        _observingAppearance = NO;
    }
    _state = nullptr;
}

- (void)dealloc
{
    [self invalidate];
#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

- (void)observeValueForKeyPath:(NSString*)forKeyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context
{
    if (_state)
        _state->notify();
}

@end

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_browser_native_theme, initializeNativeThemeApi, &nativeThemeNative)
