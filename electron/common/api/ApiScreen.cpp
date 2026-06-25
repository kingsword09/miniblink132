// Copyright (c) 2017 weolar, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/nodeblink.h"
#include "electron/common/api/EventEmitter.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/gin_helper/wrappable.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libnode/src/node_version.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_local.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

void setEmptyRECT(RECT* rc)
{
    rc->left = 0;
    rc->top = 0;
    rc->right = 0;
    rc->bottom = 0;
}

RECT scaleToEnclosingRect(const RECT& rect, float x_scale, float y_scale)
{
    if (x_scale == 1.f && y_scale == 1.f)
        return rect;

    // These next functions cast instead of using e.g. ToFlooredInt() because we
    // haven't checked to ensure that the clamping behavior of the helper
    // functions doesn't degrade performance, and callers shouldn't be passing
    // values that cause overflow anyway.
    //     DCHECK(base::IsValueInRangeForNumericType<int>(std::floor(rect.x() * x_scale)));
    //     DCHECK(base::IsValueInRangeForNumericType<int>(std::floor(rect.y() * y_scale)));
    //     DCHECK(base::IsValueInRangeForNumericType<int>(std::ceil(rect.right() * x_scale)));
    //     DCHECK(base::IsValueInRangeForNumericType<int>(std::ceil(rect.bottom() * y_scale)));
    int x = static_cast<int>(std::floor(rect.left * x_scale));
    int y = static_cast<int>(std::floor(rect.right * y_scale));
    int r = rect.left == rect.right ? x : static_cast<int>(std::ceil(rect.right * x_scale));
    int b = rect.top == rect.bottom ? y : static_cast<int>(std::ceil(rect.bottom * y_scale));
    RECT rc = { x, y, r, b };
    return rc;
}

RECT normalizedRect(const RECT& rect)
{
    RECT normalized = rect;
    if (normalized.right < normalized.left)
        std::swap(normalized.left, normalized.right);
    if (normalized.bottom < normalized.top)
        std::swap(normalized.top, normalized.bottom);
    return normalized;
}

bool rectContainsPoint(const RECT& rect, const POINT& point)
{
    return rect.left <= point.x && point.x < rect.right && rect.top <= point.y && point.y < rect.bottom;
}

int64_t squaredDistanceFromPointToRect(const POINT& point, const RECT& rect)
{
    int64_t dx = 0;
    int64_t dy = 0;
    if (point.x < rect.left)
        dx = static_cast<int64_t>(rect.left) - static_cast<int64_t>(point.x);
    else if (point.x > rect.right)
        dx = static_cast<int64_t>(point.x) - static_cast<int64_t>(rect.right);
    if (point.y < rect.top)
        dy = static_cast<int64_t>(rect.top) - static_cast<int64_t>(point.y);
    else if (point.y > rect.bottom)
        dy = static_cast<int64_t>(point.y) - static_cast<int64_t>(rect.bottom);
    return dx * dx + dy * dy;
}

int64_t rangeDistance(LONG left_a, LONG right_a, LONG left_b, LONG right_b)
{
    if (right_a < left_b)
        return static_cast<int64_t>(left_b) - static_cast<int64_t>(right_a);
    if (right_b < left_a)
        return static_cast<int64_t>(left_a) - static_cast<int64_t>(right_b);
    return 0;
}

int64_t squaredDistanceBetweenRects(const RECT& rect_a, const RECT& rect_b)
{
    int64_t dx = rangeDistance(rect_a.left, rect_a.right, rect_b.left, rect_b.right);
    int64_t dy = rangeDistance(rect_a.top, rect_a.bottom, rect_b.top, rect_b.bottom);
    return dx * dx + dy * dy;
}

int64_t intersectionArea(const RECT& rect_a, const RECT& rect_b)
{
    LONG left = std::max(rect_a.left, rect_b.left);
    LONG top = std::max(rect_a.top, rect_b.top);
    LONG right = std::min(rect_a.right, rect_b.right);
    LONG bottom = std::min(rect_a.bottom, rect_b.bottom);
    if (right <= left || bottom <= top)
        return 0;
    return (static_cast<int64_t>(right) - static_cast<int64_t>(left)) * (static_cast<int64_t>(bottom) - static_cast<int64_t>(top));
}

LONG addClampedLong(int origin, int delta)
{
    using Limits = std::numeric_limits<LONG>;
    int64_t value = static_cast<int64_t>(origin) + static_cast<int64_t>(delta);
    if (value < static_cast<int64_t>(Limits::min()))
        return Limits::min();
    if (value > static_cast<int64_t>(Limits::max()))
        return Limits::max();
    return static_cast<LONG>(value);
}

class Display final {
public:
    // Screen Rotation in clock-wise degrees.
    // This enum corresponds to DisplayRotationDefaultProto::Rotation in
    // chrome/browser/chromeos/policy/proto/chrome_device_policy.proto.
    enum Rotation {
        ROTATE_0 = 0,
        ROTATE_90,
        ROTATE_180,
        ROTATE_270,
    };

    // The display rotation can have multiple causes for change. A user can set a
    // preference. On devices with accelerometers, they can change the rotation.
    // RotationSource allows for the tracking of a Rotation per source of the
    // change. ROTATION_SOURCE_ACTIVE is the current rotation of the display.
    // Rotation changes not due to an accelerometer, nor the user, are to use this
    // source directly. ROTATION_SOURCE_UNKNOWN is when no rotation source has
    // been provided.
    enum RotationSource {
        ROTATION_SOURCE_ACCELEROMETER = 0,
        ROTATION_SOURCE_ACTIVE,
        ROTATION_SOURCE_USER,
        ROTATION_SOURCE_UNKNOWN,
    };

    // Touch support for the display.
    enum TouchSupport {
        TOUCH_SUPPORT_UNKNOWN,
        TOUCH_SUPPORT_AVAILABLE,
        TOUCH_SUPPORT_UNAVAILABLE,
    };

    Display()
        : Display(-1)
    {
    }

    Display(uint32_t id)
        : m_id(id)
    {
        setEmptyRECT(&m_screenRect);
        setEmptyRECT(&m_screenWorkRect);
        setEmptyRECT(&m_workArea);
        setEmptyRECT(&m_bounds);
    }

    uint32_t getId() const
    {
        return m_id;
    }
    Rotation getRotation() const
    {
        return m_rotation;
    }
    RECT getScreenRect() const
    {
        return m_screenRect;
    }
    RECT getScreenWorkRect() const
    {
        return m_screenWorkRect;
    }
    RECT getWorkArea() const
    {
        return m_workArea;
    }
    RECT getBounds() const
    {
        return m_bounds;
    }
    float getDeviceScaleFactor() const
    {
        return m_deviceScaleFactor;
    }

    void setRotation(Rotation rotation)
    {
        m_rotation = rotation;
    }
    void setScreenRect(const RECT& screenRect)
    {
        m_screenRect = screenRect;
    }
    void setScreenWorkRect(const RECT& screenWorkRect)
    {
        m_screenWorkRect = screenWorkRect;
    }
    void setWorkArea(const RECT& workArea)
    {
        m_workArea = workArea;
    }
    void setBounds(const RECT& bounds)
    {
        m_bounds = bounds;
    }
    void setDeviceScaleFactor(float deviceScaleFactor)
    {
        m_deviceScaleFactor = deviceScaleFactor;
    }

private:
    uint32_t m_id;
    Rotation m_rotation;
    RECT m_screenRect;
    RECT m_screenWorkRect;
    RECT m_workArea;
    RECT m_bounds;
    float m_deviceScaleFactor;
};

class DisplayInfo {
public:
    DisplayInfo(const MONITORINFOEX& monitor_info, float device_scale_factor)
        : DisplayInfo(monitor_info, device_scale_factor, GetRotationForDevice(monitor_info.szDevice))
    {
    }

    DisplayInfo(const MONITORINFOEX& monitor_info, float device_scale_factor, Display::Rotation rotation)
        : id_(DeviceIdFromDeviceName(monitor_info.szDevice))
        , rotation_(rotation)
        , screen_rect_(monitor_info.rcMonitor)
        , screen_work_rect_(monitor_info.rcWork)
        , device_scale_factor_(device_scale_factor)
    {
    }

    static uint32_t getHash(const char* str, uint32_t len)
    {
        uint32_t hash = 1315423911;
        uint32_t i = 0;

        for (i = 0; i < len; str++, i++) {
            hash ^= ((hash << 5) + (*str) + (hash >> 2));
        }

        return hash;
    }

    // static
    static uint32_t DeviceIdFromDeviceName(const WCHAR* deviceName)
    {
        std::string deviceNameA = base::UTF16ToUTF8((const char16_t*)deviceName);
        return static_cast<uint32_t>(getHash(deviceNameA.c_str(), deviceNameA.length()));
    }

    static Display::Rotation GetRotationForDevice(const wchar_t* device_name)
    {
        DEVMODE mode;
        ::ZeroMemory(&mode, sizeof(mode));
        mode.dmSize = sizeof(mode);
        mode.dmDriverExtra = 0;
        if (::EnumDisplaySettings(device_name, ENUM_CURRENT_SETTINGS, &mode)) {
            switch (mode.dmDisplayOrientation) {
            case DMDO_DEFAULT:
                return Display::ROTATE_0;
            case DMDO_90:
                return Display::ROTATE_90;
            case DMDO_180:
                return Display::ROTATE_180;
            case DMDO_270:
                return Display::ROTATE_270;
            default:
                ::DebugBreak();
            }
        }
        return Display::ROTATE_0;
    }

    uint32_t id() const
    {
        return id_;
    }
    Display::Rotation rotation() const
    {
        return rotation_;
    }
    const RECT& screen_rect() const
    {
        return screen_rect_;
    }
    const RECT& screen_work_rect() const
    {
        return screen_work_rect_;
    }
    float device_scale_factor() const
    {
        return device_scale_factor_;
    }

private:
    uint32_t id_;
    Display::Rotation rotation_;
    RECT screen_rect_;
    RECT screen_work_rect_;
    float device_scale_factor_;
};

// A display used by display::ScreenWin.
// It holds a display and additional parameters used for DPI calculations.
class ScreenWinDisplay {
public:
    ScreenWinDisplay()
    {
        setEmptyRECT(&pixel_bounds_);
    }

    //explicit ScreenWinDisplay(const DisplayInfo& display_info);
    ScreenWinDisplay(const Display& display, const DisplayInfo& display_info)
        : display_(display)
        , pixel_bounds_(display_info.screen_rect())
    {
    }

    const Display& display() const
    {
        return display_;
    }
    const RECT& pixel_bounds() const
    {
        return pixel_bounds_;
    }

private:
    Display display_;
    RECT pixel_bounds_;
};

class ScreenWin {
public:
    void Initialize()
    {
        updateFromDisplayInfos(getDisplayInfosFromSystem());
    }

    static float getMonitorScaleFactor(HMONITOR monitor)
    {
        return 1.0f;
    }

    static MONITORINFOEX monitorInfoFromHMONITOR(HMONITOR monitor)
    {
        MONITORINFOEX monitor_info;
        ::ZeroMemory(&monitor_info, sizeof(monitor_info));
        monitor_info.cbSize = sizeof(monitor_info);
        ::GetMonitorInfo(monitor, &monitor_info);
        return monitor_info;
    }

    static BOOL CALLBACK EnumMonitorCallback(HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM data)
    {
        std::vector<DisplayInfo>* display_infos = reinterpret_cast<std::vector<DisplayInfo>*>(data);
        DCHECK(display_infos);
        display_infos->push_back(DisplayInfo(monitorInfoFromHMONITOR(monitor), getMonitorScaleFactor(monitor)));
        return TRUE;
    }

    static std::vector<DisplayInfo> getDisplayInfosFromSystem()
    {
        std::vector<DisplayInfo> display_infos;
        ::EnumDisplayMonitors(nullptr, nullptr, EnumMonitorCallback, reinterpret_cast<LPARAM>(&display_infos));
        DCHECK(static_cast<size_t>(::GetSystemMetrics(SM_CMONITORS)) == display_infos.size());
        return display_infos;
    }

    MONITORINFOEX monitorInfoFromWindow(HWND hwnd, DWORD default_options) const
    {
        return monitorInfoFromHMONITOR(::MonitorFromWindow(hwnd, default_options));
    }

    Display createDisplayFromDisplayInfo(const DisplayInfo& display_info)
    {
        Display display(display_info.id());
        float scale_factor = display_info.device_scale_factor();
        display.setDeviceScaleFactor(scale_factor);
        display.setWorkArea(scaleToEnclosingRect(display_info.screen_work_rect(), 1.0f / scale_factor, 1.0f / scale_factor));
        display.setBounds(scaleToEnclosingRect(display_info.screen_rect(), 1.0f / scale_factor, 1.0f / scale_factor));
        display.setRotation(display_info.rotation());
        return display;
    }

    // Windows historically has had a hard time handling displays of DPIs higher
    // than 96. Handling multiple DPI displays means we have to deal with Windows'
    // monitor physical coordinates and map into Chrome's DIP coordinates.
    //
    // To do this, DisplayInfosToScreenWinDisplays reasons over monitors as a tree
    // using the primary monitor as the root. All monitors touching this root are
    // considered a children.
    //
    // This also presumes that all monitors are connected components. Windows, by UI
    // construction restricts the layout of monitors to connected components except
    // when DPI virtualization is happening. When this happens, we scale relative
    // to (0, 0).
    //
    // Note that this does not handle cases where a scaled display may have
    // insufficient room to lay out its children. In these cases, a DIP point could
    // map to multiple screen points due to overlap. The first discovered screen
    // will take precedence.
    std::vector<ScreenWinDisplay> DisplayInfosToScreenWinDisplays(const std::vector<DisplayInfo>& display_infos)
    {
        // Layout and create the ScreenWinDisplays.
        std::vector<Display> displays;
        for (const auto& display_info : display_infos)
            displays.push_back(createDisplayFromDisplayInfo(display_info));

        std::vector<ScreenWinDisplay> screen_win_displays;
        const size_t num_displays = display_infos.size();
        for (size_t i = 0; i < num_displays; ++i)
            screen_win_displays.push_back(ScreenWinDisplay(displays[i], display_infos[i]));

        return screen_win_displays;
    }

    void updateFromDisplayInfos(const std::vector<DisplayInfo>& display_infos)
    {
        screen_win_displays_ = DisplayInfosToScreenWinDisplays(display_infos);
    }

    ScreenWinDisplay getScreenWinDisplay(const MONITORINFOEX& monitor_info) const
    {
        uint32_t id = DisplayInfo::DeviceIdFromDeviceName(monitor_info.szDevice);
        std::vector<ScreenWinDisplay>::const_iterator screen_win_display = screen_win_displays_.begin();
        for (; screen_win_display != screen_win_displays_.end(); ++screen_win_display) {
            if (screen_win_display->display().getId() == id)
                return *screen_win_display;
        }
        // There is 1:1 correspondence between MONITORINFOEX and ScreenWinDisplay.
        // If we make it here, it means we have no displays and we should hand out the
        // default display.
        DCHECK(screen_win_displays_.size() == 0u);
        return ScreenWinDisplay();
    }

    ScreenWinDisplay getPrimaryScreenWinDisplay() const
    {
        MONITORINFOEX monitor_info = monitorInfoFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
        ScreenWinDisplay screen_win_display = getScreenWinDisplay(monitor_info);
        // Display display = screen_win_display.display();
        // The Windows primary monitor is defined to have an origin of (0, 0).
        //         DCHECK(0 == display.bounds().origin().x());
        //         DCHECK(0 == display.bounds().origin().y());
        return screen_win_display;
    }

    ScreenWinDisplay getScreenWinDisplayNearestPoint(const POINT& point) const
    {
        if (screen_win_displays_.empty())
            return getPrimaryScreenWinDisplay();

        const ScreenWinDisplay* nearest = nullptr;
        int64_t nearest_distance = std::numeric_limits<int64_t>::max();
        for (const auto& screen_win_display : screen_win_displays_) {
            const RECT bounds = normalizedRect(screen_win_display.pixel_bounds());
            if (rectContainsPoint(bounds, point))
                return screen_win_display;
            int64_t distance = squaredDistanceFromPointToRect(point, bounds);
            if (!nearest || distance < nearest_distance) {
                nearest = &screen_win_display;
                nearest_distance = distance;
            }
        }
        return nearest ? *nearest : getPrimaryScreenWinDisplay();
    }

    ScreenWinDisplay getScreenWinDisplayMatchingRect(const RECT& rect) const
    {
        if (screen_win_displays_.empty())
            return getPrimaryScreenWinDisplay();

        RECT normalized = normalizedRect(rect);
        const ScreenWinDisplay* best_intersection = nullptr;
        int64_t best_area = 0;
        const ScreenWinDisplay* nearest = nullptr;
        int64_t nearest_distance = std::numeric_limits<int64_t>::max();
        for (const auto& screen_win_display : screen_win_displays_) {
            const RECT bounds = normalizedRect(screen_win_display.pixel_bounds());
            int64_t area = intersectionArea(normalized, bounds);
            if (area > best_area) {
                best_intersection = &screen_win_display;
                best_area = area;
            }
            int64_t distance = squaredDistanceBetweenRects(normalized, bounds);
            if (!nearest || distance < nearest_distance) {
                nearest = &screen_win_display;
                nearest_distance = distance;
            }
        }
        if (best_intersection)
            return *best_intersection;
        return nearest ? *nearest : getPrimaryScreenWinDisplay();
    }

    std::vector<Display> screenWinDisplaysToDisplays(const std::vector<ScreenWinDisplay>& screen_win_displays) const
    {
        std::vector<Display> displays;
        for (const auto& screen_win_display : screen_win_displays)
            displays.push_back(screen_win_display.display());

        return displays;
    }

    std::vector<Display> getAllDisplays() const
    {
        return screenWinDisplaysToDisplays(screen_win_displays_);
    }

private:
    std::vector<ScreenWinDisplay> screen_win_displays_;
};

THREAD_LOCAL_CONSTRUCTOR(Screen)

class Screen : public mate::EventEmitter<Screen> {
public:
    explicit Screen(v8::Isolate* isolate, v8::Local<v8::Object> wrapper)
    {
        gin_helper::Wrappable<Screen>::InitWith(isolate, wrapper);
        m_screen.Initialize();
    }

    static void init(v8::Isolate* isolate, v8::Local<v8::Object> target)
    {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::FunctionTemplate> prototype = v8::FunctionTemplate::New(isolate, newFunction);

        prototype->SetClassName(v8::String::NewFromUtf8(isolate, "Screen").ToLocalChecked());
        gin_helper::ObjectTemplateBuilder builder(isolate, prototype->InstanceTemplate());
        builder.SetMethod("getCursorScreenPoint", &Screen::getCursorScreenPointApi);
        builder.SetMethod("getPrimaryDisplay", &Screen::getPrimaryDisplayApi);
        builder.SetMethod("getAllDisplays", &Screen::getAllDisplaysApi);
        builder.SetMethod("getDisplayNearestPoint", &Screen::getDisplayNearestPointApi);
        builder.SetMethod("getDisplayMatching", &Screen::getDisplayMatchingApi);
        builder.SetMethod("addListener", &Screen::addListenerApi);

        getScreenConstructor().Reset(isolate, prototype->GetFunction(context).ToLocalChecked());
        target->Set(context, v8::String::NewFromUtf8(isolate, "Screen").ToLocalChecked(), prototype->GetFunction(context).ToLocalChecked());
    }

    void nullFunction()
    {
    }

    void getCursorScreenPointApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        POINT pt;
        ::GetCursorPos(&pt);
        //         gfx::Point cursor_pos_pixels(pt);
        //         return ScreenToDIPPoint(cursor_pos_pixels);
        base::Value::Dict point;
        point.Set("x", (int)pt.x);
        point.Set("y", (int)pt.y);
        v8::Local<v8::Value> v8Value = gin_helper::Converter<base::Value::Dict>::ToV8(args.GetIsolate(), point);
        args.GetReturnValue().Set(v8Value);
    }

    void getPrimaryDisplayApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        //return screen_->GetPrimaryDisplay();
        ScreenWinDisplay screenWinDisplay = m_screen.getPrimaryScreenWinDisplay();
        setDisplayReturnValue(args, screenWinDisplay.display());
    }

    static base::Value::Dict createRectDictionaryValue(const RECT& rc)
    {
        base::Value::Dict out;
        out.Set("x", (int)rc.left);
        out.Set("y", (int)rc.top);
        out.Set("width", (int)(rc.right - rc.left));
        out.Set("height", (int)(rc.bottom - rc.top));
        return (out);
    }

    static base::Value::Dict createSizeDictionaryValue(const SIZE& size)
    {
        base::Value::Dict out;
        out.Set("width", (int)size.cx);
        out.Set("height", (int)size.cy);
        return (out);
    }

    static base::Value::Dict createDisplayDictionaryValue(const Display& display)
    {
        // id Integer - 与display 相关的唯一性标志.
        // rotation Integer - 可以是 0, 1, 2, 3, 每个代表了屏幕旋转的度数 0, 90, 180, 270.
        // scaleFactor Number - Output device's pixel scale factor.
        // touchSupport String - 可以是 available, unavailable, unknown.
        // bounds Object
        // size Object
        // workArea Object
        // workAreaSize Object
        base::Value::Dict out;
        out.Set("id", (int)(display.getId()));
        out.Set("rotation", display.getRotation());
        out.Set("scaleFactor", 1);
        out.Set("touchSupport", "unavailable");
        out.Set("bounds", createRectDictionaryValue(display.getBounds()));

        RECT bounds = display.getBounds();
        SIZE size = { bounds.right - bounds.left, bounds.bottom - bounds.top };
        out.Set("size", createSizeDictionaryValue(size));
        out.Set("workArea", createRectDictionaryValue(display.getWorkArea()));

        RECT workArea = display.getWorkArea();
        SIZE workAreaSize = { workArea.right - workArea.left, workArea.bottom - workArea.top };
        out.Set("workAreaSize", createSizeDictionaryValue(workAreaSize));
        return (out);
    }

    static void setDisplayReturnValue(const v8::FunctionCallbackInfo<v8::Value>& args, const Display& display)
    {
        base::Value::Dict displayValue = createDisplayDictionaryValue(display);
        v8::Local<v8::Value> v8Value = gin_helper::Converter<base::Value::Dict>::ToV8(args.GetIsolate(), std::move(displayValue));
        args.GetReturnValue().Set(v8Value);
    }

    void getAllDisplaysApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        std::vector<Display> display = m_screen.getAllDisplays();

        base::Value::List displays;

        for (std::vector<Display>::const_iterator it = display.begin(); it != display.end(); ++it) {
            base::Value::Dict displayDictionaryValue = createDisplayDictionaryValue(*it);
            displays.Append(std::move(displayDictionaryValue));
        }

        v8::Local<v8::Value> v8Value = gin_helper::Converter<base::Value::List>::ToV8(args.GetIsolate(), displays);
        args.GetReturnValue().Set(v8Value);
    }

    // const gfx::Point& point
    void getDisplayNearestPointApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        if (args.Length() < 1 || !args[0]->IsObject()) {
            getPrimaryDisplayApi(args);
            return;
        }

        v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
        gin_helper::Dictionary point(args.GetIsolate(), args[0]->ToObject(context).ToLocalChecked());
        int x = 0;
        int y = 0;
        if (!point.Get("x", &x) || !point.Get("y", &y)) {
            getPrimaryDisplayApi(args);
            return;
        }

        POINT screenPoint = { static_cast<LONG>(x), static_cast<LONG>(y) };
        ScreenWinDisplay screenWinDisplay = m_screen.getScreenWinDisplayNearestPoint(screenPoint);
        setDisplayReturnValue(args, screenWinDisplay.display());
    }

    // const gfx::Rect& match_rect
    void getDisplayMatchingApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        if (args.Length() < 1 || !args[0]->IsObject()) {
            getPrimaryDisplayApi(args);
            return;
        }

        v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
        gin_helper::Dictionary rect(args.GetIsolate(), args[0]->ToObject(context).ToLocalChecked());
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        if (!rect.Get("x", &x) || !rect.Get("y", &y) || !rect.Get("width", &width) || !rect.Get("height", &height)) {
            getPrimaryDisplayApi(args);
            return;
        }

        RECT matchRect = {
            static_cast<LONG>(x),
            static_cast<LONG>(y),
            addClampedLong(x, width),
            addClampedLong(y, height),
        };
        ScreenWinDisplay screenWinDisplay = m_screen.getScreenWinDisplayMatchingRect(matchRect);
        setDisplayReturnValue(args, screenWinDisplay.display());
    }

    void addListenerApi(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        OutputDebugStringA("Screen.addListener\n");
    }

    void onDisplayAdded(const Display& new_display)
    {
        //Emit("display-added", new_display);
    }

    void onDisplayRemoved(const Display& old_display)
    {
        //Emit("display-removed", old_display);
    }

    void OnDisplayMetricsChanged(const Display& display, uint32_t changed_metrics)
    {
        //Emit("display-metrics-changed", display, MetricsToArray(changed_metrics));
    }

    static void newFunction(const v8::FunctionCallbackInfo<v8::Value>& args)
    {
        v8::Isolate* isolate = args.GetIsolate();
        if (args.IsConstructCall()) {
            new Screen(isolate, args.This());
            args.GetReturnValue().Set(args.This());
            return;
        }
    }

public:
    static gin_helper::WrapperInfo kWrapperInfo;

    ScreenWin m_screen;
};

gin_helper::WrapperInfo Screen::kWrapperInfo = { gin_helper::GinEmbedder::kEmbedderNativeGin };

void initializeCommonScreenApi(v8::Local<v8::Object> exports, v8::Local<v8::Value> target, v8::Local<v8::Context> context, void* priv)
{
    Screen::init(context->GetIsolate(), exports);
}

} // namespace

static const char CommonScreenNative[] = "console.log('CommonScreenNative');;";
static NodeNative nativeCommonScreenNative { "Screen", CommonScreenNative, sizeof(CommonScreenNative) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_common_screen, initializeCommonScreenApi, &nativeCommonScreenNative)
