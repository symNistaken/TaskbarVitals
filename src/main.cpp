// Taskbar Vitals - tray app showing the battery level of wireless mice, headsets and Bluetooth devices.
#include <windows.h>
#include <shellapi.h>
#include <dbt.h>
#include <initguid.h>
#include <hidclass.h>
#include <bthdef.h>
#include <bluetoothapis.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "device_aliases.h"
#include "alert_rules.h"
#include "devices.h"
#include "battery_history.h"
#include "i18n.h"
#include "json_output.h"
#include "labels.h"
#include "refresh_policy.h"
#include "memory_trim.h"
#include "panel.h"
#include "report.h"
#include "rename_dialog.h"
#include "offset_dialog.h"
#include "gpumon.h"
#include "sysmon.h"
#include "vitals_strip.h"
#include "widget.h"
#include "update_win.h"
#include "update_apply.h"

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_RESULTS = WM_APP + 2;
constexpr UINT WM_WIDGET_CLICK = WM_APP + 3;
constexpr UINT_PTR TIMER_DEBOUNCE = 1;
constexpr UINT_PTR TIMER_WIDGET = 2;
constexpr UINT_PTR TIMER_ALERT_ICON = 3;
constexpr UINT_PTR TIMER_RESUME = 4;
constexpr GUID kConsoleDisplayStateGuid = {
    0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 }
};
// "Check battery every" choices in seconds; 60 is the default. A poll resets an awake device's sleep timer,
// so a device only sleeps if its timer is shorter than the interval.
constexpr DWORD POLL_SECONDS[] = { 30, 60, 120, 300, 600, 900, 1800, 3600 };
constexpr int LOW_BATTERY = 15;
constexpr UINT PLACEHOLDER_ID = 1;
constexpr UINT ALERT_ICON_ID = 2;

enum MenuId : UINT {
    ID_REFRESH = 100, ID_AUTOSTART, ID_HIDE_LAST_KNOWN, ID_WIDGET_BACKGROUND, ID_EXIT, ID_MODE_WIDGET, ID_MODE_TRAY, ID_SUPPORT, ID_REPORT,
    ID_BRAND_COLORS, ID_STACKED, ID_SHOW_DISCONNECTED, ID_RECORD_HISTORY, ID_DIM_SLEEPING,
    ID_TRAY_WHEN_EMPTY, ID_MAKE_ROOM, ID_BAT_LEFT, ID_BAT_CLOCK, ID_VIT_LEFT, ID_VIT_CLOCK, ID_SHOW_CPU, ID_SHOW_RAM,
    ID_SHOW_GPU, ID_SHOW_GPU_TEMP, ID_SHOW_VRAM, ID_SHOW_CPU_W, ID_SHOW_GPU_W,
    ID_PLACE_AUTO, ID_PLACE_TOP, ID_PLACE_TRAY, ID_PLACE_BOTTOM,  // "Position on a vertical taskbar"
    ID_POSITION_FIRST = 700,  // "Position on the taskbar": ID_POSITION_FIRST + 0..5, then the custom value
    ID_POLL_FIRST = 500,  // "Check battery every": ID_POLL_FIRST + 0..7
    ID_LIMIT_OFF = 600, ID_LIMIT_FIRST,  // "Notify when charged to": off, then alerts::kChargeLimits
    ID_UPDATE = 200, ID_AUTO_UPDATE,
    ID_LANG_AUTO = 400, ID_LANG_ADD, ID_LANG_FIRST,  // "Language": ID_LANG_FIRST + 0..63
    ID_MONITOR_ALL = 300, ID_MONITOR_MAIN, ID_MONITOR_FIRST,  // "Show the widget on": ID_MONITOR_FIRST + 0..31
    ID_DEVICE_FIRST = 1000
};
enum DeviceCmd : UINT {
    DEV_TOGGLE, DEV_LEFT, DEV_RIGHT, DEV_RENAME, DEV_RESET_NAME, DEV_HISTORY,
    DEV_ALERT_FIRST = 6, DEV_ALERT_DEFAULT = DEV_ALERT_FIRST + 8, DEV_ALERT_OFF, DEV_STRIDE = 16
};

const wchar_t* kAppName = L"Taskbar Vitals";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"WirelessBattery";

const wchar_t* kSettingsKey = L"Software\\WirelessBattery";

HINSTANCE g_inst;
HWND g_hwnd;
HANDLE g_refreshEvt, g_stopEvt, g_worker;
UINT g_taskbarCreatedMsg;
bool g_lightTaskbar;
bool g_wantWidget = true;      // user's choice: taskbar widget (default) or tray icons
bool g_useWidget = false;      // what is actually shown right now
bool g_hideLastKnown = false;  // hide (last known) / asleep devices from taskbar and tray
bool g_trayWhenEmpty = false;  // taskbar mode: swap the strip for the tray placeholder while no device is shown
bool g_emptyToTray = false;    // that swap is in effect right now
bool g_showDisconnectedDevices = false;  // opt-in, explicit driver-reported offline devices only
bool g_dimSleeping = true;  // dim a sleeping device's last known value (menu option, on by default)
bool g_recordHistory = true;  // save readings for "Battery history..." (menu option, on by default)
int g_chargeLimit = 0;        // "Notify when charged to" 80/85/90%, 0 = off (the default)
std::wstring g_widgetMonitor = L"all";  // which taskbars show the widget: "all", "main" or one monitor id
bool g_widgetBackground = false;  // draw a plate behind the taskbar text (transparent taskbar themes)
bool g_stacked = false;      // icon above the percentage (menu option)
bool g_makeRoom = true;      // step past other apps' taskbar widgets (menu option)
// "Position on the taskbar": distance from the notification area in DPI-independent pixels. Stored as
// the TaskbarOffset value the widget has always read, so a value set by hand keeps working.
const int POSITION_OFFSETS[] = { 0, 40, 80, 120, 160, 200 };
widget::VerticalPlace g_verticalPlace = widget::VerticalPlace::Auto;  // column position on a left / right taskbar
bool g_farLeft = false;      // widget at the left end of the taskbar instead of next to the clock (menu option)
bool g_vitalsFarLeft = false;  // CPU / RAM / GPU readings at the left end of the taskbar (menu option)
bool g_leftEndPlaced = false;  // with two strips: the left-end one found room there at the last update
bool g_showCpu = false, g_showRam = false;  // system readings on the strip (menu options)
bool g_showGpu = false, g_showGpuTemp = false, g_showVram = false;
bool g_showCpuW = false, g_showGpuW = false;  // power draw in watts
sysmon::Reading g_sys;       // last CPU / RAM sample, taken every 2 s while one of them is shown
gpumon::Reading g_gpu;       // last GPU sample, likewise
int g_sysTicks = 0;
bool AnyGpuReading() { return g_showGpu || g_showGpuTemp || g_showVram || g_showGpuW; }
bool AnyCpuReading() { return g_showCpu || g_showRam || g_showCpuW; }
bool AnyReading() { return AnyCpuReading() || AnyGpuReading(); }
bool g_brandColors = false;  // off by default; known brands get their color (Razer green, Logitech blue, ...)
std::atomic<DWORD> g_pollIntervalMs{ 60 * 1000 };  // set from the menu, read by the worker thread

struct TrayEntry {
    UINT uid = 0;
    DeviceStatus st;
    int lastPct = -1;        // last known percentage, kept while the device sleeps
    ULONGLONG lastSeen = 0;
    bool added = false;
    bool lowWarned = false;
    bool limitWarned = false;  // "unplug at 80%" already shown during this charge
    std::wstring iconSig;    // what the current icon shows, to avoid needless redraws
    HICON icon = nullptr;
};
std::map<std::wstring, TrayEntry> g_entries;
std::map<std::wstring, bool> g_alertLatches;  // survive transient removal of a device
bool g_alertIconAdded = false;  // short-lived tray icon for widget-mode notifications
bool g_placeholder = false;
HICON g_placeholderIcon = nullptr;

// User-supplied names are presentation-only, keyed by stable DeviceStatus.key.
// Driver identity, HID polling, brand detection and device diagnostics are unchanged.
aliases::AliasMap g_aliases;

std::wstring DisplayName(const TrayEntry& e) {
    return aliases::DisplayName(g_aliases, e.st.key, e.st.name);
}

void LoadAliases() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceAliases", RRF_RT_REG_MULTI_SZ,
                     nullptr, nullptr, &size) != ERROR_SUCCESS) return;
    if (size < 2 * sizeof(wchar_t) || size > 128 * 1024 || size % sizeof(wchar_t)) return;
    std::vector<wchar_t> data(size / sizeof(wchar_t), 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceAliases", RRF_RT_REG_MULTI_SZ,
                     nullptr, data.data(), &size) == ERROR_SUCCESS)
        g_aliases = aliases::Decode(data.data(), size / sizeof(wchar_t));
}

void SaveAliases() {
    auto data = aliases::Encode(g_aliases);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceAliases", REG_MULTI_SZ,
                    data.data(), (DWORD)(data.size() * sizeof(wchar_t)));
}

// ---- helpers ----------------------------------------------------------------------------------

UINT StableId(const std::wstring& key) {
    UINT h = 2166136261u;
    for (wchar_t c : key) { h ^= c; h *= 16777619u; }
    return (h & 0x7FFFFFFF) | 0x100;
}

bool ReadLightTaskbar() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return v != 0;
    return false;
}

void ApplyMenuTheme(bool light) {
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setMode = ux ? (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135)) : nullptr;
    static auto flush = ux ? (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136)) : nullptr;
    if (setMode) setMode(light ? 3 : 2);
    if (flush) flush();
}

// Small Windows 11 icons (Segoe Fluent Icons, Segoe MDL2 Assets on Windows 10) in the menu's icon column,
// drawn in the menu's text colour for the light or dark theme. The menu stays the native Windows menu;
// check marks and radio marks keep their own column. The bitmaps live until the object goes away.
class MenuIcons {
  public:
    explicit MenuIcons(bool light) : light_(light) {
        UINT dpi = GetDpiForWindow(g_hwnd);
        size_ = MulDiv(16, dpi ? (int)dpi : 96, 96);
        for (const wchar_t* face : { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" }) {
            HFONT f = CreateFontW(-size_, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
            HDC dc = CreateCompatibleDC(nullptr);
            HGDIOBJ old = SelectObject(dc, f);
            wchar_t got[LF_FACESIZE] = {};
            GetTextFaceW(dc, LF_FACESIZE, got);
            SelectObject(dc, old);
            DeleteDC(dc);
            if (!_wcsicmp(got, face)) { font_ = f; break; }
            DeleteObject(f);
        }
    }
    ~MenuIcons() {
        for (HBITMAP b : bitmaps_) DeleteObject(b);
        if (font_) DeleteObject(font_);
    }
    MenuIcons(const MenuIcons&) = delete;
    MenuIcons& operator=(const MenuIcons&) = delete;

    // `item` is a command id, or a position when `byPosition` (submenus have no command id).
    void Set(HMENU menu, UINT item, bool byPosition, wchar_t glyph) {
        HBITMAP b = Make(glyph);
        if (!b) return;
        MENUITEMINFOW mii{ sizeof(mii) };
        mii.fMask = MIIM_BITMAP;
        mii.hbmpItem = b;
        SetMenuItemInfoW(menu, item, byPosition ? TRUE : FALSE, &mii);
    }
    void SetLast(HMENU menu, wchar_t glyph) { Set(menu, (UINT)GetMenuItemCount(menu) - 1, true, glyph); }

  private:
    HBITMAP Make(wchar_t glyph) {
        if (!font_) return nullptr;
        BITMAPINFO bi{};
        bi.bmiHeader = { sizeof(BITMAPINFOHEADER), size_, -size_, 1, 32, BI_RGB };
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp) { DeleteDC(dc); return nullptr; }
        HGDIOBJ ob = SelectObject(dc, bmp);
        HGDIOBJ of = SelectObject(dc, font_);
        memset(bits, 0, (size_t)size_ * size_ * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT r{ 0, 0, size_, size_ };
        wchar_t s[2] = { glyph, 0 };
        DrawTextW(dc, s, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        GdiFlush();
        // white-on-black coverage -> premultiplied alpha in the menu text colour
        const int c = light_ ? 0x1B : 0xF2;
        auto* p = static_cast<DWORD*>(bits);
        for (int i = 0; i < size_ * size_; i++) {
            int a = ((p[i] & 0xFF) + ((p[i] >> 8) & 0xFF) + ((p[i] >> 16) & 0xFF)) / 3;
            int v = c * a / 255;
            p[i] = ((DWORD)a << 24) | ((DWORD)v << 16) | ((DWORD)v << 8) | (DWORD)v;
        }
        SelectObject(dc, of);
        SelectObject(dc, ob);
        DeleteDC(dc);
        bitmaps_.push_back(bmp);
        return bmp;
    }
    bool light_;
    int size_ = 16;
    HFONT font_ = nullptr;
    std::vector<HBITMAP> bitmaps_;
};

std::wstring ExePath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

// Devices the user chose to hide (by key), stored as a REG_MULTI_SZ.
std::set<std::wstring> g_hidden;

void LoadHidden() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || !size)
        return;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS)
        return;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) g_hidden.insert(p);
}

void SaveHidden() {
    std::wstring multi;
    for (const auto& k : g_hidden) { multi += k; multi.push_back(L'\0'); }
    multi.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", REG_MULTI_SZ, multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
}

bool IsHidden(const std::wstring& key) { return g_hidden.count(key) != 0; }

// The user's left-to-right order (device keys), stored as a REG_MULTI_SZ. Devices not in it follow
// in the default order (by type, then name).
std::vector<std::wstring> g_order;

void LoadOrder() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceOrder", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || !size)
        return;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceOrder", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS)
        return;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) g_order.push_back(p);
}

void SaveOrder() {
    if (g_order.size() > 64) g_order.resize(64);  // forget long-gone devices
    std::wstring multi;
    for (const auto& k : g_order) { multi += k; multi.push_back(L'\0'); }
    multi.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceOrder", REG_MULTI_SZ, multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
}

bool IsEntryHidden(const TrayEntry& e) {
    if (IsHidden(e.st.key)) return true;
    // An explicitly offline device is not just a stale battery reading.
    if (g_showDisconnectedDevices && !e.st.connected) return false;
    return g_hideLastKnown && (e.st.asleep || (e.st.percent < 0 && e.lastPct >= 0));
}

bool ShouldDim(const DeviceStatus& s) {
    // An explicitly offline device keeps its dimmed "x", whatever the sleeping-device choice is.
    if (!s.connected) return true;
    // A sleeping device has no fresh reading: keep the last one, optionally at full brightness.
    if (s.asleep && !g_dimSleeping) return false;
    return s.percent < 0;
}

bool LoadBoolSetting(const wchar_t* name, bool defaultVal) {
    DWORD v = defaultVal ? 1 : 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v != 0;
}

void SaveBoolSetting(const wchar_t* name, bool on) {
    DWORD v = on ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &v, sizeof(v));
}

// A REG_MULTI_SZ with one "device-key<TAB>threshold" entry per override.
// Threshold -1 disables alerts; a missing key uses the default 15%.
std::map<std::wstring, int> g_alertOverrides;

void LoadAlertThresholds() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"AlertThresholds", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size < 2 * sizeof(wchar_t) || size > 128 * 1024) return;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"AlertThresholds", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS)
        return;
    size_t limit = size / sizeof(wchar_t);
    for (size_t i = 0; i < limit && buf[i];) {
        size_t end = i;
        while (end < limit && buf[end]) ++end;
        if (end == limit) break;
        std::wstring item(buf.data() + i, end - i);
        size_t sep = item.rfind(L'\t');
        if (sep != std::wstring::npos && sep != 0) {
            std::wstring value = item.substr(sep + 1);
            wchar_t* tail = nullptr;
            long n = std::wcstol(value.c_str(), &tail, 10);
            if (tail != value.c_str() && *tail == 0 && n >= -1 && n <= 40 && alerts::ValidOverride((int)n))
                g_alertOverrides[item.substr(0, sep)] = (int)n;
        }
        i = end + 1;
    }
}

void SaveAlertThresholds() {
    std::wstring multi;
    for (const auto& [key, threshold] : g_alertOverrides) {
        multi += key + L"\t" + std::to_wstring(threshold);
        multi.push_back(L'\0');
    }
    multi.push_back(L'\0');
    if (multi.size() == 1) multi.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"AlertThresholds", REG_MULTI_SZ,
                    multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
}

bool LoadWidgetMode() { return LoadBoolSetting(L"TaskbarWidget", true); }
void SaveWidgetMode(bool on) { SaveBoolSetting(L"TaskbarWidget", on); }
bool LoadHideLastKnown() { return LoadBoolSetting(L"HideLastKnownDevices", false); }
void SaveHideLastKnown(bool on) { SaveBoolSetting(L"HideLastKnownDevices", on); }
bool LoadTrayWhenEmpty() { return LoadBoolSetting(L"MoveToTrayWhenEmpty", false); }
void SaveTrayWhenEmpty(bool on) { SaveBoolSetting(L"MoveToTrayWhenEmpty", on); }
bool LoadShowDisconnectedDevices() { return LoadBoolSetting(L"ShowDisconnectedDevices", false); }
void SaveShowDisconnectedDevices(bool on) { SaveBoolSetting(L"ShowDisconnectedDevices", on); }
bool LoadDimSleeping() { return LoadBoolSetting(L"DimSleepingDevices", true); }
void SaveDimSleeping(bool on) { SaveBoolSetting(L"DimSleepingDevices", on); }
bool LoadRecordHistory() { return LoadBoolSetting(L"RecordBatteryHistory", true); }
bool LoadMakeRoom() { return LoadBoolSetting(L"MakeRoomForOtherWidgets", true); }
void SaveMakeRoom(bool on) { SaveBoolSetting(L"MakeRoomForOtherWidgets", on); }
// Signed: stored as a DWORD, read back as int, so negative values move the widget towards the tray.
int LoadTaskbarOffset() {
    DWORD v = 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return (int)v;
}
void SaveTaskbarOffset(int px) {
    DWORD v = (DWORD)px;
    if (px) RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TaskbarOffset", REG_DWORD, &v, sizeof(v));
    else RegDeleteKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TaskbarOffset");
}
void SaveRecordHistory(bool on) { SaveBoolSetting(L"RecordBatteryHistory", on); }
bool LoadWidgetBackground() { return LoadBoolSetting(L"WidgetBackground", false); }
void SaveWidgetBackground(bool on) { SaveBoolSetting(L"WidgetBackground", on); }
std::wstring LoadWidgetMonitor() {
    wchar_t buf[512] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"WidgetMonitor", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS && buf[0])
        return buf;
    return LoadBoolSetting(L"OtherTaskbars", true) ? L"all" : L"main";  // the earlier on/off toggle
}
void SaveWidgetMonitor(const std::wstring& v) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"WidgetMonitor", REG_SZ, v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}
void SetWidgetMonitor(const std::wstring& v) {
    g_widgetMonitor = v;
    SaveWidgetMonitor(v);
    widget::SetMonitorChoice(v);
    vitals_strip::SetMonitorChoice(v);
}
bool LoadStacked() { return LoadBoolSetting(L"StackedLayout", false); }
void SaveStacked(bool on) { SaveBoolSetting(L"StackedLayout", on); }
widget::VerticalPlace LoadVerticalPlace() {
    DWORD v = 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"VerticalPlace", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v == 1 ? widget::VerticalPlace::Top : v == 2 ? widget::VerticalPlace::AboveTray
         : v == 3 ? widget::VerticalPlace::Bottom : widget::VerticalPlace::Auto;
}
void SaveVerticalPlace(widget::VerticalPlace p) {
    DWORD v = p == widget::VerticalPlace::Top ? 1 : p == widget::VerticalPlace::AboveTray ? 2
              : p == widget::VerticalPlace::Bottom ? 3 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"VerticalPlace", REG_DWORD, &v, sizeof(v));
}
bool LoadFarLeft() { return LoadBoolSetting(L"WidgetFarLeft", false); }
void SaveFarLeft(bool on) { SaveBoolSetting(L"WidgetFarLeft", on); }
bool LoadShowCpu() { return LoadBoolSetting(L"ShowCpu", false); }
bool LoadShowRam() { return LoadBoolSetting(L"ShowRam", false); }
bool LoadBrandColors() { return LoadBoolSetting(L"BrandColors", false); }
void SaveBrandColors(bool on) { SaveBoolSetting(L"BrandColors", on); }
DWORD LoadPollSeconds() {
    DWORD v = 60, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"PollIntervalSeconds", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return std::clamp<DWORD>(v, 30, 24 * 60 * 60);
}
void SavePollSeconds(DWORD seconds) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"PollIntervalSeconds", REG_DWORD, &seconds, sizeof(seconds));
}

bool AutostartEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return false;
    return _wcsicmp(buf, (L"\"" + ExePath() + L"\"").c_str()) == 0;
}

void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + ExePath() + L"\"";
        RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunValue);
    }
    RegCloseKey(k);
}

// ---- icon rendering ---------------------------------------------------------------------------
// Draws the percentage as large digits with a small level bar underneath. GDI text is rendered
// white-on-black into a scratch bitmap and its brightness is used as the alpha channel, which
// gives smooth edges on the transparent icon.

struct Rgb { BYTE r, g, b; };

HICON RenderIcon(const std::wstring& text, int barPct, Rgb textColor, Rgb barColor, bool dim) {
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    UINT dpi = taskbar ? GetDpiForWindow(taskbar) : 0;
    const int S = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : GetDpiForSystem());
    const int barH = std::max(2, S / 8);
    const int textH = S - barH - 1;

    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    // scratch bitmap for the glyphs
    const int W = S * 3, H = S * 2;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    void* sbits = nullptr;
    HBITMAP scratch = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &sbits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, scratch);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    auto* sp = static_cast<DWORD*>(sbits);

    int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;
    for (int fh = textH * 2; fh >= 6; fh--) {
        HFONT f = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ oldF = SelectObject(dc, f);
        SetTextCharacterExtra(dc, text.size() >= 3 ? -1 : 0);
        memset(sbits, 0, (size_t)W * H * 4);
        TextOutW(dc, 2, 0, text.c_str(), (int)text.size());
        GdiFlush();
        SelectObject(dc, oldF);
        DeleteObject(f);
        bx0 = W; by0 = H; bx1 = -1; by1 = -1;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (sp[y * W + x] & 0xFF) { bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); by0 = std::min(by0, y); by1 = std::max(by1, y); }
        if (bx1 < 0) break;
        if (bx1 - bx0 + 1 <= S && by1 - by0 + 1 <= textH) break;
    }

    // final icon bitmap
    bi.bmiHeader.biWidth = S;
    bi.bmiHeader.biHeight = -S;
    void* ibits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &ibits, nullptr, 0);
    auto* ip = static_cast<DWORD*>(ibits);
    memset(ip, 0, (size_t)S * S * 4);
    const int alphaScale = dim ? 140 : 255;
    auto put = [&](int x, int y, Rgb c, int a) {
        if (x < 0 || y < 0 || x >= S || y >= S || a <= 0) return;
        a = a * alphaScale / 255;
        ip[y * S + x] = ((DWORD)a << 24) | ((DWORD)c.r << 16) | ((DWORD)c.g << 8) | c.b;  // straight alpha
    };
    if (bx1 >= 0) {
        int gw = bx1 - bx0 + 1, gh = by1 - by0 + 1;
        int ox = (S - gw) / 2, oy = (textH - gh) / 2;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++)
                put(ox + x, oy + y, textColor, sp[(by0 + y) * W + (bx0 + x)] & 0xFF);
    }
    // level bar: faint track + filled part
    int fill = barPct < 0 ? 0 : std::max(barPct > 0 ? 1 : 0, (barPct * S + 50) / 100);
    for (int y = S - barH; y < S; y++)
        for (int x = 0; x < S; x++) {
            if (x < fill) put(x, y, barColor, 255);
            else put(x, y, g_lightTaskbar ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }, 70);
        }

    SelectObject(dc, oldBmp);
    DeleteObject(scratch);
    DeleteDC(dc);

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    {
        HDC mdc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(mdc, mask);
        PatBlt(mdc, 0, 0, S, S, BLACKNESS);
        SelectObject(mdc, o);
        DeleteDC(mdc);
    }
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

Rgb Foreground() { return g_lightTaskbar ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }; }

Accent AccentFor(const DeviceStatus& s) { return g_brandColors ? BrandOf(s) : Accent::None; }

HICON IconFor(int pct, bool charging, bool dim, Accent accent) {
    Rgb fg = Foreground();
    BrandColor bc = AccentColor(accent, g_lightTaskbar);
    Rgb bar = charging        ? Rgb{ 0, 170, 255 }
            : pct <= LOW_BATTERY ? Rgb{ 232, 32, 48 }
            : pct <= 30       ? Rgb{ 255, 150, 0 }
            : accent != Accent::None ? Rgb{ bc.r, bc.g, bc.b }
                              : Rgb{ 30, 190, 90 };
    Rgb text = (!charging && pct >= 0 && pct <= LOW_BATTERY) ? Rgb{ 240, 50, 60 } : fg;
    std::wstring t = pct < 0 ? L"?" : std::to_wstring(pct);
    return RenderIcon(t, pct, text, bar, dim);
}

// ---- tray management --------------------------------------------------------------------------

void CopyTip(wchar_t* dst, size_t cap, const std::wstring& s) {
    wcsncpy(dst, s.c_str(), cap - 1);
    dst[cap - 1] = 0;
}

std::wstring StatusText(const TrayEntry& e) {
    const auto& s = e.st;
    if (!s.connected) return T(L"off / not connected");
    std::wstring t;
    if (!s.levelText.empty() && s.percent >= 0) t = T(s.levelText);
    else if (s.percent >= 0) t = std::to_wstring(s.percent) + L"%";
    else if (e.lastPct >= 0) t = TF(L"{percent}% (last known)", { { L"percent", std::to_wstring(e.lastPct) } });
    else t = T(L"unknown");
    if (!s.detail.empty() && s.percent >= 0) t += L" (" + s.detail + L")";
    if (s.full || (s.charging && s.percent >= 100)) t += std::wstring(L", ") + T(L"fully charged");
    else if (s.charging) t += std::wstring(L", ") + T(L"charging");
    if (s.asleep) t += std::wstring(L", ") + T(L"asleep");
    return t;
}

void TraySetVersion(UINT uid) {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

bool TrayCall(DWORD msg, UINT uid, HICON icon, const std::wstring& tip, const wchar_t* infoTitle = nullptr,
              const std::wstring& info = L"") {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = icon;
    CopyTip(nid.szTip, ARRAYSIZE(nid.szTip), tip);
    if (infoTitle) {
        nid.uFlags |= NIF_INFO;
        CopyTip(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), infoTitle);
        CopyTip(nid.szInfo, ARRAYSIZE(nid.szInfo), info);
        nid.dwInfoFlags = NIIF_WARNING;
    }
    bool ok = Shell_NotifyIconW(msg, &nid) != FALSE;
    if (ok && msg == NIM_ADD) TraySetVersion(uid);
    return ok;
}

// Adds or updates a tray icon; returns whether the shell has it. While Explorer is busy (e.g. at sign-in)
// NIM_ADD can time out and report failure although the icon was added, and NIM_MODIFY fails for an icon
// the shell no longer has, so each one falls back to the other.
bool TrayShow(bool added, UINT uid, HICON icon, const std::wstring& tip, const wchar_t* infoTitle = nullptr,
              const std::wstring& info = L"") {
    if (TrayCall(added ? NIM_MODIFY : NIM_ADD, uid, icon, tip, infoTitle, info)) return true;
    if (!TrayCall(added ? NIM_ADD : NIM_MODIFY, uid, icon, tip, infoTitle, info)) return false;
    if (!added) TraySetVersion(uid);
    return true;
}

void TrayDelete(UINT uid) {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowNotification(TrayEntry& e, const std::wstring& title, const std::wstring& message) {
    if (!g_useWidget && e.added) {
        TrayCall(NIM_MODIFY, e.uid, e.icon, DisplayName(e) + L"\n" + StatusText(e), title.c_str(), message);
        return;
    }
    // Windows notification balloons need a tray icon. Keep a temporary one in widget mode
    // long enough for the shell to display the notification, then remove it.
    if (!g_placeholderIcon) g_placeholderIcon = RenderIcon(L"-", -1, Foreground(), Foreground(), true);
    g_alertIconAdded =
        TrayShow(g_alertIconAdded, ALERT_ICON_ID, g_placeholderIcon, L"Taskbar Vitals", title.c_str(), message);
    if (g_alertIconAdded) SetTimer(g_hwnd, TIMER_ALERT_ICON, 60 * 1000, nullptr);
}

void ShowBatteryAlert(TrayEntry& e) {
    ShowNotification(e, T(L"Low battery"), TF(L"{name} is at {percent}%",
                                              { { L"name", DisplayName(e) }, { L"percent", std::to_wstring(e.st.percent) } }));
}

void ShowChargeLimitAlert(TrayEntry& e) {
    ShowNotification(e, T(L"You can unplug it"),
                     TF(L"{name} is charged to {percent}%. Stopping around here helps the battery last longer.",
                        { { L"name", DisplayName(e) }, { L"percent", std::to_wstring(e.st.percent) } }));
}

void UpdatePlaceholder() {
    if (g_useWidget && !g_emptyToTray) {
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
        return;
    }
    bool any = false;
    for (auto& [k, e] : g_entries) any |= e.added;
    if (!any && !g_placeholder) {
        if (!g_placeholderIcon) g_placeholderIcon = RenderIcon(L"-", -1, Foreground(), Foreground(), true);
        g_placeholder = TrayShow(false, PLACEHOLDER_ID, g_placeholderIcon,
                                 std::wstring(L"Taskbar Vitals\n") + T(L"No devices with battery info found"));
    } else if (any && g_placeholder) {
        TrayDelete(PLACEHOLDER_ID);
        g_placeholder = false;
    }
}

void ApplyEntry(TrayEntry& e) {
    const auto& s = e.st;
    bool visible = !g_useWidget && !IsEntryHidden(e) && (s.connected || g_showDisconnectedDevices);
    if (!visible) {
        if (e.added) { TrayDelete(e.uid); e.added = false; }
        return;
    }
    // Never display an old percentage as a live reading for an explicitly offline device.
    int shown = !s.connected ? -1 : (s.percent >= 0 ? s.percent : e.lastPct);
    bool dim = ShouldDim(s);
    // Fully charged on power: the normal green bar instead of the blue charging one.
    bool charging = s.connected && !s.asleep && s.charging && !s.full && shown < 100;
    Accent accent = AccentFor(s);
    std::wstring sig = std::to_wstring(shown) + (charging ? L"c" : L"") + (dim ? L"d" : L"") + (g_lightTaskbar ? L"l" : L"k") +
                       L"a" + std::to_wstring((int)accent) + (s.connected ? L"|on" : L"|off");
    if (sig != e.iconSig || !e.icon) {
        if (e.icon) DestroyIcon(e.icon);
        e.icon = s.connected ? IconFor(shown, charging, dim, accent)
                             : RenderIcon(L"\u00D7", -1, Foreground(), Foreground(), true);
        e.iconSig = sig;
    }
    std::wstring tip = DisplayName(e) + L"\n" + StatusText(e);

    e.added = TrayShow(e.added, e.uid, e.icon, tip);
}

int KindOrder(DeviceKind k) {  // default order: headset first, then mouse, ...
    switch (k) {
        case DeviceKind::Headset:  return 0;
        case DeviceKind::Mouse:    return 1;
        case DeviceKind::Keyboard: return 2;
        case DeviceKind::Gamepad:  return 3;
        case DeviceKind::Phone:    return 4;
        default:                   return 5;
    }
}

bool IsShown(const TrayEntry& e) { return (e.st.connected || g_showDisconnectedDevices) && !IsEntryHidden(e); }

// Entries in left-to-right order: the user's order first, then the rest by type and name. Works the
// same for every kind of device (receivers, Bluetooth, controllers, HeadsetControl) as it only uses
// the device key.
std::vector<TrayEntry*> OrderedEntries(bool shownOnly) {
    std::vector<TrayEntry*> list;
    for (auto& [k, e] : g_entries)
        if (!shownOnly || IsShown(e)) list.push_back(&e);
    auto rank = [](const TrayEntry* e) {
        auto it = std::find(g_order.begin(), g_order.end(), e->st.key);
        return it == g_order.end() ? INT_MAX : (int)(it - g_order.begin());
    };
    std::stable_sort(list.begin(), list.end(), [&](const TrayEntry* a, const TrayEntry* b) {
        int ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        int ka = KindOrder(a->st.kind), kb = KindOrder(b->st.kind);
        if (ka != kb) return ka < kb;
        return DisplayName(*a) != DisplayName(*b) ? DisplayName(*a) < DisplayName(*b) : a->st.key < b->st.key;  // same name: keep a fixed order
    });
    return list;
}

void UpdateWidget() {
    if (!g_useWidget || !widget::Alive()) return;  // not alive while the strip is swapped for the tray
    std::vector<TrayEntry*> list = OrderedEntries(true);
    // Two devices of one kind share an icon: give each a tiny caption so they can be told apart.
    std::vector<LabelInput> labelIn;
    for (auto* e : list) labelIn.push_back({ e->st.kind, DisplayName(*e) });
    std::vector<std::wstring> labels = ShortLabels(labelIn, 3);
    std::vector<WidgetItem> items;
    std::wstring tip;
    for (auto* e : list) {
        WidgetItem it;
        it.kind = e->st.kind;
        bool offline = !e->st.connected;
        it.percent = offline ? -1 : (e->st.percent >= 0 ? e->st.percent : e->lastPct);
        if (offline) it.text = L"\u00D7";  // clear status, not a stale percentage
        else if (e->st.percent >= 0) it.text = T(e->st.levelText);
        it.charging = !offline && !e->st.asleep && e->st.charging;
        it.full = !offline && (e->st.full || (e->st.charging && it.percent >= 100));
        it.dim = ShouldDim(e->st);
        it.low = !offline && !it.charging && it.percent >= 0 && it.percent <= LOW_BATTERY;
        it.bluetooth = e->st.via == L"Bluetooth";
        it.label = labels[items.size()];
        it.accent = AccentFor(e->st);
        items.push_back(it);
        if (!tip.empty()) tip += L"\n";
        tip += DisplayName(*e) + L": " + StatusText(*e);
    }
    if (tip.empty()) tip = std::wstring(L"Taskbar Vitals: ") + T(L"No devices with battery info found");
    // CPU / RAM / GPU readings: their own strip when it sits apart from the batteries, else after them.
    std::vector<WidgetItem> readings;
    std::wstring readingsTip;
    auto reading = [&](wchar_t glyph, int pct, const std::wstring& line, const std::wstring& extra = L"",
                       const std::wstring& caption = L"") {
        WidgetItem it;
        it.glyph = glyph;
        it.text = (pct >= 0 ? std::to_wstring(pct) + L"%" : std::wstring()) + (pct >= 0 && !extra.empty() ? L" " : L"") + extra;
        it.label = caption;
        readings.push_back(it);
        readingsTip += (readingsTip.empty() ? L"" : L"\n") + line;
    };
    // CPU: use and power share one item ("14% 18W"), like the GPU's use and temperature.
    int cpuUse = g_showCpu ? g_sys.cpu : -1;
    int cpuW = g_showCpuW ? g_sys.cpuWatts : -1;
    if (cpuUse >= 0 || cpuW >= 0) {
        std::wstring line = std::wstring(T(L"CPU")) + L": ";
        if (cpuUse >= 0) line += std::to_wstring(cpuUse) + L"%";
        if (cpuW >= 0) line += (cpuUse >= 0 ? L", " : L"") + std::to_wstring(cpuW) + L" W";
        reading(0xE950, cpuUse, line, cpuW >= 0 ? std::to_wstring(cpuW) + L"W" : L"");
    }
    if (g_showRam && g_sys.ram >= 0) {
        wchar_t gb[64];
        swprintf(gb, 64, L" (%.1f / %.1f GB)", g_sys.ramUsedGb, g_sys.ramTotalGb);
        reading(0xE964, g_sys.ram, std::wstring(T(L"RAM usage")) + L": " + std::to_wstring(g_sys.ram) + L"%" + gb);
    }
    // GPU: use and temperature share one item ("12% 58°"); VRAM is the memory icon with a "GPU" caption.
    int gpuUse = g_showGpu ? g_gpu.use : -1;
    int gpuTemp = g_showGpuTemp ? g_gpu.tempC : -1;
    int gpuW = g_showGpuW ? g_gpu.watts : -1;
    if (g_gpu.available && (gpuUse >= 0 || gpuTemp >= 0 || gpuW >= 0)) {
        std::wstring temp = (gpuTemp >= 0 ? std::to_wstring(gpuTemp) + L"°" : L"") +
                            (gpuW >= 0 ? (gpuTemp >= 0 ? L" " : L"") + std::to_wstring(gpuW) + L"W" : L"");
        std::wstring line = L"GPU (" + g_gpu.name + L"): ";
        if (gpuUse >= 0) line += std::to_wstring(gpuUse) + L"%";
        if (gpuTemp >= 0) line += (gpuUse >= 0 ? L", " : L"") + std::to_wstring(gpuTemp) + L" °C";
        if (gpuW >= 0) line += (gpuUse >= 0 || gpuTemp >= 0 ? L", " : L"") + std::to_wstring(gpuW) + L" W";
        reading(0xE7F4, gpuUse, line, temp);  // TVMonitor: reads as "graphics" and stays apart from the CPU chip
        readings.back().heat = sysmon::HeatLevel(gpuTemp);
    }
    if (g_showVram && g_gpu.available && g_gpu.vram >= 0) {
        wchar_t gb[64];
        swprintf(gb, 64, L" (%.1f / %.1f GB)", g_gpu.vramUsedGb, g_gpu.vramTotalGb);
        reading(0xE964, g_gpu.vram, std::wstring(T(L"GPU memory")) + L": " + std::to_wstring(g_gpu.vram) + L"%" + gb, L"", L"GPU");
    }
    std::vector<WidgetItem> all = items;
    all.insert(all.end(), readings.begin(), readings.end());
    std::wstring allTip = readingsTip.empty() ? tip : tip + L"\n" + readingsTip;
    if (!vitals_strip::Alive()) {
        widget::SetItems(all, allTip);
        return;
    }
    // Two strips. The one at the left end hides when there is no room there; the other then shows everything.
    // The left-end strip keeps its own items even while hidden (it needs its width to find room).
    g_leftEndPlaced = g_farLeft ? widget::AtFarLeft() : vitals_strip::AtFarLeft();
    bool batteriesAlone = g_farLeft || g_leftEndPlaced;
    bool readingsAlone = !g_farLeft || g_leftEndPlaced;
    widget::SetItems(batteriesAlone ? items : all, batteriesAlone ? tip : allTip);
    vitals_strip::SetItems(readingsAlone ? readings : all, readingsAlone ? readingsTip : allTip);
}

// Batteries and readings sit on two strips when one is at the left end of a horizontal taskbar and the other is
// next to the clock. Otherwise (same place, a vertical taskbar, no readings shown) one strip shows everything.
bool SplitStrips() {
    return g_useWidget && widget::Alive() && AnyReading() && g_farLeft != g_vitalsFarLeft && !widget::Vertical();
}

// Creates, keeps or removes the readings strip; called from Reconcile (once a second and after changes).
void SyncVitalsStrip() {
    bool want = SplitStrips(), alive = vitals_strip::Alive();
    if (want && !alive) {
        vitals_strip::SetBackground(g_widgetBackground);
        vitals_strip::SetStacked(g_stacked);
        vitals_strip::SetMakeRoom(g_makeRoom);
        vitals_strip::SetMonitorChoice(g_widgetMonitor);
        vitals_strip::SetFarLeft(g_vitalsFarLeft);
        vitals_strip::SetFarLeftOnly(g_vitalsFarLeft);
        vitals_strip::Create(g_inst, g_hwnd, WM_WIDGET_CLICK, g_lightTaskbar);
        widget::SetFarLeftOnly(g_farLeft);
        UpdateWidget();
    } else if (!want && alive) {
        vitals_strip::Destroy();
        widget::SetFarLeftOnly(false);
        UpdateWidget();
    } else if (want) {
        vitals_strip::Reposition();
        bool placed = g_farLeft ? widget::AtFarLeft() : vitals_strip::AtFarLeft();
        if (placed != g_leftEndPlaced) UpdateWidget();  // room at the left end came or went
    }
}

void Reconcile();

void OnResults(std::vector<DeviceStatus>& results) {
    std::map<std::wstring, bool> seen;
    for (auto& s : results) {
        auto& e = g_entries[s.key];
        if (!e.uid) e.uid = StableId(s.key);
        const int previousPercent = e.st.connected && !e.st.asleep ? e.st.percent : -1;  // the last fresh reading
        e.st = s;
        if (s.percent >= 0) e.lastPct = s.percent;
        e.lastSeen = GetTickCount64();
        seen[s.key] = true;
        // Evaluate even in widget mode: ApplyEntry only manages tray icons.
        // Alert latches are kept by stable device key even if the driver briefly stops reporting it.
        if (const auto it = g_alertLatches.find(s.key); it != g_alertLatches.end())
            e.lowWarned = it->second;
        bool notify = !IsHidden(s.key) &&
                      alerts::ShouldNotify(s, alerts::EffectiveThreshold(g_alertOverrides, s.key), e.lowWarned);
        g_alertLatches[s.key] = e.lowWarned;
        if (g_alertLatches.size() > 256) g_alertLatches.erase(g_alertLatches.begin());
        if (g_recordHistory && s.connected && !s.asleep && s.percent >= 0)
            battery_history::Observe(s.key, s.percent, s.charging);
        bool limit = !IsHidden(s.key) && alerts::ShouldNotifyChargeLimit(s, g_chargeLimit, previousPercent, e.limitWarned);
        ApplyEntry(e);
        if (notify) ShowBatteryAlert(e);
        else if (limit) ShowChargeLimitAlert(e);
    }
    for (auto it = g_entries.begin(); it != g_entries.end();) {
        auto& e = it->second;
        // Some receivers drop sleeping devices from the link; keep showing the last value for a while
        // (at least two poll intervals).
        bool inGrace = !seen.count(it->first) && e.st.graceSec > 0 && e.st.connected &&
                       GetTickCount64() - e.lastSeen <
                           std::max<ULONGLONG>((ULONGLONG)e.st.graceSec * 1000, 2ULL * g_pollIntervalMs);
        if (inGrace) {
            e.st.percent = -1;
            e.st.asleep = true;
            ApplyEntry(e);
        }
        if (!seen.count(it->first) && !inGrace) {
            if (it->second.added) TrayDelete(it->second.uid);
            if (it->second.icon) DestroyIcon(it->second.icon);
            it = g_entries.erase(it);
        } else {
            ++it;
        }
    }
    Reconcile();  // react to the new device list now, not at the next timer tick
    UpdatePlaceholder();
    UpdateWidget();
    panel::Refresh();  // new levels in the settings panel, if it is open
    memory_trim::Soon();  // the poll (HID, Bluetooth, drivers) is done: give its pages back until the next one
}

bool NothingShown() {
    if (AnyReading()) return false;  // the readings keep the strip on the taskbar
    for (const auto& [k, e] : g_entries)
        if (IsShown(e)) return false;
    return true;
}

// Makes the actual display match the chosen mode; falls back to tray icons if the taskbar
// widget cannot be created (e.g. Explorer not ready yet). Called once a second.
// With "Move to tray when no devices" the strip gives way to the tray placeholder while nothing is shown.
void Reconcile() {
    // Taskbar mode, option on, nothing to show: the strip is replaced by the tray placeholder.
    bool emptyToTray = g_wantWidget && g_trayWhenEmpty && NothingShown();
    bool wasAlive = widget::Alive();
    // Keep, create or remove the strip.
    bool alive = g_wantWidget && !emptyToTray && wasAlive;
    if (g_wantWidget && !emptyToTray && !alive) alive = widget::Create(g_inst, g_hwnd, WM_WIDGET_CLICK, g_lightTaskbar);
    if (!g_wantWidget || emptyToTray) widget::Destroy();
    // Stays in widget mode while swapped, so device icons are not added to the tray in between.
    bool useWidget = alive || emptyToTray;
    bool changed = useWidget != g_useWidget || emptyToTray != g_emptyToTray;
    g_useWidget = useWidget;
    g_emptyToTray = emptyToTray;
    // Refresh after a change, or when the strip was just re-created.
    if (changed || (alive && !wasAlive)) {
        for (auto& [k, e] : g_entries) ApplyEntry(e);
        UpdatePlaceholder();
        UpdateWidget();
    } else if (g_useWidget) {
        widget::Reposition();
    } else {
        // Retry tray icons whose NIM_ADD failed while the shell notification area was starting.
        bool anyAdded = false;
        for (auto& [k, e] : g_entries) {
            if (!e.added && IsShown(e)) ApplyEntry(e);
            anyAdded |= e.added;
        }
        if (anyAdded == g_placeholder) UpdatePlaceholder();
    }
    SyncVitalsStrip();
}

void SetMode(bool useWidget) {
    g_wantWidget = useWidget;
    SaveWidgetMode(useWidget);
    Reconcile();
}

void SetWidgetBackground(bool on) {
    g_widgetBackground = on;
    SaveWidgetBackground(on);
    widget::SetBackground(on);
    vitals_strip::SetBackground(on);
}

void SetHideLastKnown(bool on) {
    g_hideLastKnown = on;
    SaveHideLastKnown(on);
    for (auto& [k, e] : g_entries) ApplyEntry(e);
    UpdatePlaceholder();
    UpdateWidget();
}

void SetTrayWhenEmpty(bool on) {
    g_trayWhenEmpty = on;
    SaveTrayWhenEmpty(on);
    Reconcile();
}

void SetShowDisconnectedDevices(bool on) {
    g_showDisconnectedDevices = on;
    SaveShowDisconnectedDevices(on);
    for (auto& [k, e] : g_entries) ApplyEntry(e);
    UpdatePlaceholder();
    UpdateWidget();
}

void SetDimSleeping(bool on) {
    g_dimSleeping = on;
    SaveDimSleeping(on);
    for (auto& [k, e] : g_entries) ApplyEntry(e);
    UpdateWidget();
}

void ReAddAll() {
    g_placeholder = false;
    for (auto& [k, e] : g_entries) { e.added = false; ApplyEntry(e); }
    UpdatePlaceholder();
}

// Switches the UI language ("" = follow Windows) and redraws everything that shows text.
void SetLanguage(const std::wstring& code);

void RedrawAll() {
    if (g_alertIconAdded) {  // alert icon shares the placeholder's HICON
        KillTimer(g_hwnd, TIMER_ALERT_ICON);
        TrayDelete(ALERT_ICON_ID);
        g_alertIconAdded = false;
    }
    if (g_placeholderIcon) {
        DestroyIcon(g_placeholderIcon);
        g_placeholderIcon = nullptr;
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
    }
    for (auto& [k, e] : g_entries) { e.iconSig.clear(); ApplyEntry(e); }
    UpdatePlaceholder();
}

void SetLanguage(const std::wstring& code) {
    i18n::SetChoice(code);
    RedrawAll();  // tray tooltips and the placeholder text
    UpdateWidget();
}

void SetStacked(bool on) {
    g_stacked = on;
    SaveStacked(on);
    widget::SetStacked(on);
    vitals_strip::SetStacked(on);
}

void SetMakeRoom(bool on) {
    g_makeRoom = on;
    SaveMakeRoom(on);
    widget::SetMakeRoom(on);
    vitals_strip::SetMakeRoom(on);
}

void SetTaskbarOffset(int px) {
    SaveTaskbarOffset(px);
    if (g_useWidget) widget::Reposition();
    if (vitals_strip::Alive()) vitals_strip::Reposition();
}

void SetWidgetPlace(widget::VerticalPlace p) {
    g_verticalPlace = p;
    SaveVerticalPlace(p);
    widget::SetVerticalPlace(p);
}

// Takes a CPU / RAM / GPU sample every 2 s while a reading is shown; redraws the strip only when a shown value changed.
void TickSystemReadings(bool now = false) {
    if (!AnyReading()) return;
    if (!now && ++g_sysTicks % 2) return;
    bool changed = false;
    if (AnyCpuReading()) {
        sysmon::Reading r = sysmon::Sample(g_showCpuW);
        changed |= (g_showCpu && r.cpu != g_sys.cpu) || (g_showRam && r.ram != g_sys.ram) || (g_showCpuW && r.cpuWatts != g_sys.cpuWatts);
        g_sys = r;
    }
    if (AnyGpuReading()) {
        gpumon::Reading g = gpumon::Sample(g_showGpuTemp, g_showGpuW);
        changed |= g.available != g_gpu.available || (g_showGpu && g.use != g_gpu.use) ||
                   (g_showGpuTemp && g.tempC != g_gpu.tempC) || (g_showVram && g.vram != g_gpu.vram) || (g_showGpuW && g.watts != g_gpu.watts);
        g_gpu = g;
    }
    if (changed) UpdateWidget();
}

void SetShowReading(bool& flag, const wchar_t* name, bool on) {
    flag = on;
    SaveBoolSetting(name, on);
    if (!g_showCpuW) sysmon::StopPower();  // the Energy Meter counters are only open while CPU power is shown
    if (!AnyGpuReading()) {  // free the GPU counters and adapter handle once no GPU reading is shown
        gpumon::Stop();
        g_gpu = gpumon::Reading();
    }
    TickSystemReadings(true);
    SyncVitalsStrip();  // the first or last reading may add or remove the readings strip
    UpdateWidget();
}

// Battery strip at the left end (true) or next to the clock.
void SetFarLeft(bool on) {
    g_farLeft = on;
    SaveFarLeft(on);
    widget::SetFarLeft(on);
    widget::SetFarLeftOnly(on && vitals_strip::Alive());
    SyncVitalsStrip();
    UpdateWidget();
}

// Readings strip (CPU / RAM / GPU) at the left end (true) or next to the clock.
void SetVitalsFarLeft(bool on) {
    g_vitalsFarLeft = on;
    SaveBoolSetting(L"VitalsFarLeft", on);
    vitals_strip::SetFarLeft(on);
    vitals_strip::SetFarLeftOnly(on);
    SyncVitalsStrip();
    UpdateWidget();
}

void SetBrandColors(bool on) {
    g_brandColors = on;
    SaveBrandColors(on);
    RedrawAll();
    UpdateWidget();
}

void SetChargeLimit(int limit) {
    g_chargeLimit = limit;
    DWORD v = (DWORD)limit;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"ChargeLimitAlert", REG_DWORD, &v, sizeof(v));
    for (auto& [k, e] : g_entries) e.limitWarned = false;
}

int LoadChargeLimit() {
    DWORD v = 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"ChargeLimitAlert", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    for (int l : alerts::kChargeLimits)
        if ((int)v == l) return l;
    return 0;
}

void SetPollSeconds(DWORD seconds) {
    g_pollIntervalMs = seconds * 1000;
    SavePollSeconds(seconds);
    SetEvent(g_refreshEvt);  // poll now, so the new interval starts right away
}

void ToggleHidden(const std::wstring& key) {
    if (!g_hidden.erase(key)) g_hidden.insert(key);
    SaveHidden();
    auto it = g_entries.find(key);
    if (it != g_entries.end()) ApplyEntry(it->second);
    UpdatePlaceholder();
    UpdateWidget();
}

void RenameDevice(const std::wstring& key) {
    auto it = g_entries.find(key);
    if (it == g_entries.end()) return;
    const std::wstring originalName = it->second.st.name;
    std::wstring chosen;
    if (!PromptDeviceName(g_hwnd, DisplayName(it->second), chosen)) return;
    if (chosen == originalName) g_aliases.erase(key);
    else if (g_aliases.count(key) || g_aliases.size() < aliases::kMaxAliases) g_aliases[key] = chosen;
    else return;  // bounded registry value; don't silently drop other users' names
    SaveAliases();
    // The modal editor pumps the message queue, so polling may remove the entry.
    // Resolve the iterator again rather than dereferencing a potentially stale one.
    it = g_entries.find(key);
    if (it != g_entries.end()) ApplyEntry(it->second);
    UpdateWidget();
}

void ResetDeviceName(const std::wstring& key) {
    if (!g_aliases.erase(key)) return;
    SaveAliases();
    auto it = g_entries.find(key);
    if (it != g_entries.end()) ApplyEntry(it->second);
    UpdateWidget();
}

void SetAlertOverride(const std::wstring& key, int threshold) {
    if (threshold == alerts::kUseDefault) g_alertOverrides.erase(key);
    else if (alerts::ValidOverride(threshold)) g_alertOverrides[key] = threshold;
    else return;
    SaveAlertThresholds();
    g_alertLatches.erase(key);
    auto it = g_entries.find(key);
    if (it != g_entries.end()) it->second.lowWarned = false;
    SetEvent(g_refreshEvt);  // reconsider with a fresh reading; never alert from stale data
}

// Swaps a shown device with its neighbour (dir -1 = left, +1 = right) and remembers the new order.
void MoveEntry(const std::wstring& key, int dir) {
    std::vector<TrayEntry*> list = OrderedEntries(true);
    auto pos = std::find_if(list.begin(), list.end(), [&](const TrayEntry* e) { return e->st.key == key; });
    if (pos == list.end()) return;
    ptrdiff_t i = pos - list.begin(), j = i + dir;
    if (j < 0 || j >= (ptrdiff_t)list.size()) return;
    std::swap(list[i], list[j]);
    std::vector<std::wstring> order;
    for (auto* e : list) order.push_back(e->st.key);
    for (auto& k : g_order)  // keep the places of devices that are not shown right now
        if (std::find(order.begin(), order.end(), k) == order.end()) order.push_back(k);
    g_order = order;
    SaveOrder();
    UpdateWidget();
}

// The menu and the lists it was built from. Some menu ids are positions in these lists (devices, monitors,
// languages), so a command must be run against the same model the menu was built with.
struct MenuModel {
    HMENU menu = nullptr;
    std::vector<std::wstring> keys;  // device index -> device key; menu id ID_DEVICE_FIRST + index * DEV_STRIDE + command
    std::vector<i18n::Language> languages;
    std::vector<widget::MonitorOption> monitors;
};

// Builds the full menu with the current state (checked, greyed, labels). The settings panel reads it as its model.
MenuModel BuildMenu(MenuIcons& icons) {
    HMENU m = CreatePopupMenu();
    std::vector<std::wstring> keys;
    std::vector<TrayEntry*> all = OrderedEntries(false), shown = OrderedEntries(true);
    if (all.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"No devices with battery info found"));
    else AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Point at a device to show, hide or move it"));
    for (TrayEntry* e : all) {
        const std::wstring& k = e->st.key;
        UINT base = ID_DEVICE_FIRST + (UINT)keys.size() * DEV_STRIDE;
        auto pos = std::find(shown.begin(), shown.end(), e);
        bool canLeft = pos != shown.end() && pos != shown.begin();
        bool canRight = pos != shown.end() && pos + 1 != shown.end();
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING | (IsHidden(k) ? 0 : MF_CHECKED), base + DEV_TOGGLE,
                    g_wantWidget ? T(L"Show on the taskbar") : T(L"Show in the tray"));
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sub, MF_STRING | (canLeft ? 0 : MF_GRAYED), base + DEV_LEFT, g_wantWidget ? T(L"Move left") : T(L"Move up"));
        AppendMenuW(sub, MF_STRING | (canRight ? 0 : MF_GRAYED), base + DEV_RIGHT, g_wantWidget ? T(L"Move right") : T(L"Move down"));
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sub, MF_STRING, base + DEV_RENAME, T(L"Rename device..."));
        icons.SetLast(sub, 0xE8AC);  // Rename
        AppendMenuW(sub, MF_STRING | (g_aliases.count(k) ? 0 : MF_GRAYED),
                    base + DEV_RESET_NAME, T(L"Reset to original name"));
        HMENU alertMenu = CreatePopupMenu();
        int selectedThreshold = alerts::OverrideFor(g_alertOverrides, k);
        AppendMenuW(alertMenu, MF_STRING | (selectedThreshold == alerts::kUseDefault ? MF_CHECKED : 0),
                    base + DEV_ALERT_DEFAULT, T(L"Use default (15%)"));
        AppendMenuW(alertMenu, MF_STRING | (selectedThreshold == alerts::kDisabled ? MF_CHECKED : 0),
                    base + DEV_ALERT_OFF, T(L"Disable notifications"));
        AppendMenuW(alertMenu, MF_SEPARATOR, 0, nullptr);
        for (int i = 0; i < alerts::kThresholdCount; ++i) {
            int level = alerts::kThresholds[i];
            std::wstring label = std::to_wstring(level) + L"%";
            AppendMenuW(alertMenu, MF_STRING | (selectedThreshold == level ? MF_CHECKED : 0),
                        base + DEV_ALERT_FIRST + i, label.c_str());
        }
        AppendMenuW(sub, MF_POPUP, (UINT_PTR)alertMenu, T(L"Battery alerts"));
        icons.SetLast(sub, 0xEA8F);  // Ringer
        AppendMenuW(sub, MF_STRING, base + DEV_HISTORY, T(L"Battery history..."));
        icons.SetLast(sub, 0xE81C);  // History
        std::wstring line = DisplayName(*e) + L":  " + StatusText(*e) + L"   [" + T(e->st.via) + L"]";
        AppendMenuW(m, MF_POPUP | (IsHidden(k) ? 0 : MF_CHECKED), (UINT_PTR)sub, line.c_str());
        icons.SetLast(m, widget::DeviceGlyph(e->st.kind, e->st.via == L"Bluetooth"));
        keys.push_back(k);
    }
    AppendMenuW(m, MF_STRING | (g_hideLastKnown ? MF_CHECKED : 0), ID_HIDE_LAST_KNOWN, T(L"Hide last known / asleep devices"));
    UINT emptyFlags = MF_STRING | (g_trayWhenEmpty ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED);
    AppendMenuW(m, emptyFlags, ID_TRAY_WHEN_EMPTY, T(L"Move to tray when no devices"));
    AppendMenuW(m, MF_STRING | (g_showDisconnectedDevices ? MF_CHECKED : 0), ID_SHOW_DISCONNECTED, T(L"Show disconnected devices"));
    AppendMenuW(m, MF_STRING | (g_dimSleeping ? MF_CHECKED : 0), ID_DIM_SLEEPING, T(L"Dim sleeping devices"));
    AppendMenuW(m, MF_STRING | (g_recordHistory ? MF_CHECKED : 0), ID_RECORD_HISTORY, T(L"Record battery history"));
    {
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_LIMIT_OFF, T(L"Off"));
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        UINT checked = ID_LIMIT_OFF;
        for (UINT i = 0; i < (UINT)alerts::kChargeLimitCount; i++) {
            AppendMenuW(sub, MF_STRING, ID_LIMIT_FIRST + i, (std::to_wstring(alerts::kChargeLimits[i]) + L"%").c_str());
            if (g_chargeLimit == alerts::kChargeLimits[i]) checked = ID_LIMIT_FIRST + i;
        }
        CheckMenuRadioItem(sub, ID_LIMIT_OFF, ID_LIMIT_FIRST + alerts::kChargeLimitCount - 1, checked, MF_BYCOMMAND);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, T(L"Notify when charged to (saves the battery)"));
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_wantWidget ? MF_CHECKED : 0), ID_MODE_WIDGET, T(L"Show on the taskbar (icon + percentage)"));
    AppendMenuW(m, MF_STRING | (!g_wantWidget ? MF_CHECKED : 0), ID_MODE_TRAY, T(L"Show as tray icons"));
    CheckMenuRadioItem(m, ID_MODE_WIDGET, ID_MODE_TRAY, g_wantWidget ? ID_MODE_WIDGET : ID_MODE_TRAY, MF_BYCOMMAND);
    UINT bgFlags = MF_STRING | (g_widgetBackground ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED);
    AppendMenuW(m, bgFlags, ID_WIDGET_BACKGROUND, T(L"Background behind taskbar text (for transparent taskbars)"));
    AppendMenuW(m, MF_STRING | (g_stacked ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED), ID_STACKED, T(L"Icons above percentages (narrower)"));
    {
        HMENU sub = CreatePopupMenu();
        UINT f = MF_STRING | (g_wantWidget ? 0 : MF_GRAYED);
        const UINT count = ARRAYSIZE(POSITION_OFFSETS);
        const int current = LoadTaskbarOffset();
        UINT checked = ID_POSITION_FIRST + count;
        for (UINT i = 0; i < count; i++) {
            int px = POSITION_OFFSETS[i];
            std::wstring label = px ? TF(L"{px} px from the notification area", { { L"px", std::to_wstring(px) } })
                                    : T(L"Next to the notification area");
            AppendMenuW(sub, f, ID_POSITION_FIRST + i, label.c_str());
            if (px == current) checked = ID_POSITION_FIRST + i;
        }
        // Any other value (typed in, or set by hand in the registry) shows as the custom entry.
        std::wstring custom = checked == ID_POSITION_FIRST + count
                                  ? TF(L"Custom ({px} px)...", { { L"px", std::to_wstring(current) } })
                                  : std::wstring(T(L"Custom..."));
        AppendMenuW(sub, f, ID_POSITION_FIRST + count, custom.c_str());
        CheckMenuRadioItem(sub, ID_POSITION_FIRST, ID_POSITION_FIRST + count, checked, MF_BYCOMMAND);
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sub, f | (g_makeRoom ? MF_CHECKED : 0), ID_MAKE_ROOM, T(L"Make room for other taskbar widgets"));
        AppendMenuW(m, MF_POPUP | (g_wantWidget ? 0 : MF_GRAYED), (UINT_PTR)sub, T(L"Position on the taskbar"));
    }
    // Left / right taskbar: choose where the column sits.
    if (widget::Vertical()) {
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_PLACE_AUTO, T(L"Automatic (keeps clear of app icons)"));
        AppendMenuW(sub, MF_STRING, ID_PLACE_TOP, T(L"Top of the taskbar"));
        AppendMenuW(sub, MF_STRING, ID_PLACE_TRAY, T(L"Above the notification area"));
        AppendMenuW(sub, MF_STRING, ID_PLACE_BOTTOM,
                    T(L"Bottom of the taskbar (moves the taskbar icons up, Windows 11)"));
        CheckMenuRadioItem(sub, ID_PLACE_AUTO, ID_PLACE_BOTTOM,
                           g_verticalPlace == widget::VerticalPlace::Top ? ID_PLACE_TOP
                           : g_verticalPlace == widget::VerticalPlace::AboveTray ? ID_PLACE_TRAY
                           : g_verticalPlace == widget::VerticalPlace::Bottom ? ID_PLACE_BOTTOM : ID_PLACE_AUTO,
                           MF_BYCOMMAND);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, T(L"Position on a vertical taskbar"));
    }
    // Where each strip sits: the left end of the taskbar, or next to the clock (two strips when they differ).
    auto placeMenu = [&](UINT leftId, UINT clockId, bool left, bool enabled, const wchar_t* title) {
        HMENU sub = CreatePopupMenu();
        UINT f = MF_STRING | (enabled ? 0 : MF_GRAYED);
        AppendMenuW(sub, f, leftId, T(L"Left end of the taskbar"));
        AppendMenuW(sub, f, clockId, T(L"Next to the clock"));
        CheckMenuRadioItem(sub, leftId, clockId, left ? leftId : clockId, MF_BYCOMMAND);
        AppendMenuW(m, MF_POPUP | (enabled ? 0 : MF_GRAYED), (UINT_PTR)sub, T(title));
    };
    placeMenu(ID_BAT_LEFT, ID_BAT_CLOCK, g_farLeft, g_wantWidget, L"Battery strip position");
    AppendMenuW(m, MF_STRING | (g_showCpu ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED), ID_SHOW_CPU, T(L"Show CPU usage"));
    AppendMenuW(m, MF_STRING | (g_showRam ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED), ID_SHOW_RAM, T(L"Show RAM usage"));
    AppendMenuW(m, MF_STRING | (g_showGpu ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED), ID_SHOW_GPU, T(L"Show GPU usage"));
    AppendMenuW(m, MF_STRING | (g_showGpuTemp ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED), ID_SHOW_GPU_TEMP, T(L"Show GPU temperature"));
    AppendMenuW(m, MF_STRING | (g_showCpuW ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED), ID_SHOW_CPU_W, T(L"Show CPU power (watts)"));
    AppendMenuW(m, MF_STRING | (g_showGpuW ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED), ID_SHOW_GPU_W, T(L"Show GPU power (watts)"));
    AppendMenuW(m, MF_STRING | (g_showVram ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED), ID_SHOW_VRAM, T(L"Show GPU memory"));
    placeMenu(ID_VIT_LEFT, ID_VIT_CLOCK, g_vitalsFarLeft, g_wantWidget && AnyReading(), L"CPU / RAM / GPU strip position");
    // With more than one monitor: choose which taskbars show the widget (all, main only, or one monitor).
    auto monitors = widget::OtherMonitors();
    if (monitors.size() > 31) monitors.resize(31);
    if (!monitors.empty()) {
        HMENU sub = CreatePopupMenu();
        UINT f = MF_STRING | (g_wantWidget ? 0 : MF_GRAYED);
        AppendMenuW(sub, f, ID_MONITOR_ALL, T(L"All monitors"));
        AppendMenuW(sub, f, ID_MONITOR_MAIN, T(L"Main monitor only"));
        UINT checked = g_widgetMonitor == L"main" ? ID_MONITOR_MAIN : ID_MONITOR_ALL;
        bool found = g_widgetMonitor == L"all" || g_widgetMonitor == L"main";
        std::vector<std::wstring> ids;
        for (const auto& mo : monitors) ids.push_back(mo.id);
        int chosen = found ? -1 : widget::FindMonitor(ids, g_widgetMonitor);
        for (UINT i = 0; i < monitors.size(); i++) {
            AppendMenuW(sub, f, ID_MONITOR_FIRST + i, TF(L"{monitor} only", { { L"monitor", monitors[i].label } }).c_str());
            if ((int)i == chosen) { checked = ID_MONITOR_FIRST + i; found = true; }
        }
        if (!found) {  // the chosen monitor is unplugged: the main taskbar shows the widget meanwhile
            AppendMenuW(sub, MF_STRING | MF_GRAYED, ID_MONITOR_FIRST + 31, T(L"Chosen monitor (not connected, shown on main)"));
            checked = ID_MONITOR_FIRST + 31;
        }
        CheckMenuRadioItem(sub, ID_MONITOR_ALL, ID_MONITOR_FIRST + 31, checked, MF_BYCOMMAND);
        AppendMenuW(m, MF_POPUP | (g_wantWidget ? 0 : MF_GRAYED), (UINT_PTR)sub, T(L"Show the widget on"));
        icons.SetLast(m, 0xE7F4);  // TVMonitor
    }
    AppendMenuW(m, MF_STRING | (g_brandColors ? MF_CHECKED : 0), ID_BRAND_COLORS, T(L"Brand colors (Razer green, Logitech blue, ...)"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    // Language: automatic, every built-in or user-added language (in its own name), and a way to add one.
    i18n::Init();  // picks up language files added since the last time
    auto languages = i18n::Available();
    if (languages.size() > 64) languages.resize(64);
    {
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_LANG_AUTO, T(L"Automatic (Windows language)"));
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        UINT checked = ID_LANG_AUTO;
        for (UINT i = 0; i < languages.size(); i++) {
            AppendMenuW(sub, MF_STRING, ID_LANG_FIRST + i, languages[i].name.c_str());
            if (!i18n::Choice().empty() && _wcsicmp(languages[i].code.c_str(), i18n::Choice().c_str()) == 0) checked = ID_LANG_FIRST + i;
        }
        CheckMenuRadioItem(sub, ID_LANG_AUTO, ID_LANG_FIRST + 63, checked, MF_BYCOMMAND);
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sub, MF_STRING, ID_LANG_ADD, T(L"Add your own language..."));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, T(L"Language"));
        icons.SetLast(m, 0xE774);  // Globe
    }
    {
        HMENU sub = CreatePopupMenu();
        const UINT count = ARRAYSIZE(POLL_SECONDS);
        UINT checked = 0;
        for (UINT i = 0; i < count; i++) {
            DWORD s = POLL_SECONDS[i];
            std::wstring label = s < 60 ? TF(L"{seconds} s", { { L"seconds", std::to_wstring(s) } })
                                        : TF(L"{minutes} min", { { L"minutes", std::to_wstring(s / 60) } });
            AppendMenuW(sub, MF_STRING, ID_POLL_FIRST + i, label.c_str());
            if (s * 1000 == g_pollIntervalMs) checked = ID_POLL_FIRST + i;
        }
        if (checked) CheckMenuRadioItem(sub, ID_POLL_FIRST, ID_POLL_FIRST + count - 1, checked, MF_BYCOMMAND);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, T(L"Check battery every"));
        icons.SetLast(m, 0xE916);  // Stopwatch
    }
    AppendMenuW(m, MF_STRING, ID_REFRESH, T(L"Refresh now"));
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), ID_AUTOSTART, T(L"Start with Windows"));
    AppendMenuW(m, MF_STRING, ID_REPORT, T(L"Request support for a device..."));
    AppendMenuW(m, MF_STRING, ID_SUPPORT, T(L"Support the project..."));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (updates::Busy() ? MF_GRAYED : 0), ID_UPDATE, updates::MenuLabel().c_str());
    UINT updateFlags = MF_STRING | (updates::AutomaticEnabled() ? MF_CHECKED : 0) |
                       (updates::Supported() ? 0 : MF_GRAYED);
    AppendMenuW(m, updateFlags, ID_AUTO_UPDATE, T(L"Automatically check for updates"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, T(L"Exit"));
    icons.Set(m, ID_REFRESH, false, 0xE72C);         // Refresh
    icons.Set(m, ID_REPORT, false, 0xE897);          // Help
    icons.Set(m, ID_SUPPORT, false, 0xEB51);         // Heart
    icons.Set(m, ID_UPDATE, false, 0xE777);          // UpdateRestore
    icons.Set(m, ID_EXIT, false, 0xE711);            // Cancel
    MenuModel model;
    model.menu = m;
    model.keys = std::move(keys);
    model.languages = std::move(languages);
    model.monitors = std::move(monitors);
    return model;
}

// Runs a command chosen from a menu built by BuildMenu (list-position ids are resolved against `model`).
void RunCommand(UINT cmd, const MenuModel& model) {
    const auto& keys = model.keys;
    const auto& languages = model.languages;
    const auto& monitors = model.monitors;
    switch (cmd) {
        case ID_REFRESH: SetEvent(g_refreshEvt); break;
        case ID_AUTOSTART: SetAutostart(!AutostartEnabled()); break;
        case ID_HIDE_LAST_KNOWN: SetHideLastKnown(!g_hideLastKnown); break;
        case ID_TRAY_WHEN_EMPTY: SetTrayWhenEmpty(!g_trayWhenEmpty); break;
        case ID_SHOW_DISCONNECTED: SetShowDisconnectedDevices(!g_showDisconnectedDevices); break;
        case ID_DIM_SLEEPING: SetDimSleeping(!g_dimSleeping); break;
        case ID_RECORD_HISTORY: g_recordHistory = !g_recordHistory; SaveRecordHistory(g_recordHistory); break;
        case ID_LIMIT_OFF: SetChargeLimit(0); break;
        case ID_WIDGET_BACKGROUND: SetWidgetBackground(!g_widgetBackground); break;
        case ID_MONITOR_ALL: SetWidgetMonitor(L"all"); break;
        case ID_LANG_AUTO: SetLanguage(L""); break;
        case ID_LANG_ADD: i18n::OpenUserFolder(); break;
        case ID_MONITOR_MAIN: SetWidgetMonitor(L"main"); break;
        case ID_BRAND_COLORS: SetBrandColors(!g_brandColors); break;
        case ID_STACKED: SetStacked(!g_stacked); break;
        case ID_MAKE_ROOM: SetMakeRoom(!g_makeRoom); break;
        case ID_PLACE_AUTO: SetWidgetPlace(widget::VerticalPlace::Auto); break;
        case ID_PLACE_TOP: SetWidgetPlace(widget::VerticalPlace::Top); break;
        case ID_PLACE_TRAY: SetWidgetPlace(widget::VerticalPlace::AboveTray); break;
        case ID_PLACE_BOTTOM: SetWidgetPlace(widget::VerticalPlace::Bottom); break;
        case ID_BAT_LEFT: SetFarLeft(true); break;
        case ID_BAT_CLOCK: SetFarLeft(false); break;
        case ID_VIT_LEFT: SetVitalsFarLeft(true); break;
        case ID_VIT_CLOCK: SetVitalsFarLeft(false); break;
        case ID_SHOW_CPU: SetShowReading(g_showCpu, L"ShowCpu", !g_showCpu); break;
        case ID_SHOW_RAM: SetShowReading(g_showRam, L"ShowRam", !g_showRam); break;
        case ID_SHOW_GPU: SetShowReading(g_showGpu, L"ShowGpu", !g_showGpu); break;
        case ID_SHOW_GPU_TEMP: SetShowReading(g_showGpuTemp, L"ShowGpuTemp", !g_showGpuTemp); break;
        case ID_SHOW_CPU_W: SetShowReading(g_showCpuW, L"ShowCpuWatts", !g_showCpuW); break;
        case ID_SHOW_GPU_W: SetShowReading(g_showGpuW, L"ShowGpuWatts", !g_showGpuW); break;
        case ID_SHOW_VRAM: SetShowReading(g_showVram, L"ShowVram", !g_showVram); break;
        case ID_MODE_WIDGET: SetMode(true); break;
        case ID_MODE_TRAY: SetMode(false); break;
        case ID_SUPPORT: ShellExecuteW(nullptr, L"open", L"https://github.com/symNistaken/TaskbarVitals", nullptr, nullptr, SW_SHOWNORMAL); break;
        case ID_REPORT: report::Show(g_inst, g_hwnd); break;
        case ID_UPDATE: updates::Activate(g_hwnd); break;
        case ID_AUTO_UPDATE: updates::ToggleAutomatic(); break;
        case ID_EXIT: DestroyWindow(g_hwnd); break;
        default:
            if (cmd >= ID_POSITION_FIRST && cmd < ID_POSITION_FIRST + ARRAYSIZE(POSITION_OFFSETS)) {
                SetTaskbarOffset(POSITION_OFFSETS[cmd - ID_POSITION_FIRST]);
                break;
            }
            if (cmd == ID_POSITION_FIRST + ARRAYSIZE(POSITION_OFFSETS)) {
                int px = 0;
                if (PromptTaskbarOffset(g_hwnd, LoadTaskbarOffset(), px)) SetTaskbarOffset(px);
                break;
            }
            if (cmd >= ID_LIMIT_FIRST && cmd < ID_LIMIT_FIRST + (UINT)alerts::kChargeLimitCount) {
                SetChargeLimit(alerts::kChargeLimits[cmd - ID_LIMIT_FIRST]);
            } else if (cmd >= ID_POLL_FIRST && cmd < ID_POLL_FIRST + ARRAYSIZE(POLL_SECONDS)) {
                SetPollSeconds(POLL_SECONDS[cmd - ID_POLL_FIRST]);
            } else if (cmd >= ID_LANG_FIRST && cmd < ID_LANG_FIRST + languages.size()) {
                SetLanguage(languages[cmd - ID_LANG_FIRST].code);
            } else if (cmd >= ID_MONITOR_FIRST && cmd < ID_MONITOR_FIRST + monitors.size()) {
                SetWidgetMonitor(monitors[cmd - ID_MONITOR_FIRST].id);
            } else if (cmd >= ID_DEVICE_FIRST && cmd < ID_DEVICE_FIRST + keys.size() * DEV_STRIDE) {
                std::wstring key = keys[(cmd - ID_DEVICE_FIRST) / DEV_STRIDE];
                switch ((cmd - ID_DEVICE_FIRST) % DEV_STRIDE) {
                    case DEV_TOGGLE: ToggleHidden(key); break;
                    case DEV_LEFT:   MoveEntry(key, -1); break;
                    case DEV_RIGHT:  MoveEntry(key, +1); break;
                    case DEV_RENAME: RenameDevice(key); break;
                    case DEV_RESET_NAME: ResetDeviceName(key); break;
                    case DEV_ALERT_DEFAULT: SetAlertOverride(key, alerts::kUseDefault); break;
                    case DEV_ALERT_OFF: SetAlertOverride(key, alerts::kDisabled); break;
                    case DEV_HISTORY: {
                        auto it = g_entries.find(key);
                        if (it != g_entries.end())
                            battery_history::Show(g_hwnd, key, DisplayName(it->second));
                        break;
                    }
                    default: {
                        UINT slot = (cmd - ID_DEVICE_FIRST) % DEV_STRIDE;
                        if (slot >= DEV_ALERT_FIRST && slot < DEV_ALERT_FIRST + alerts::kThresholdCount)
                            SetAlertOverride(key, alerts::kThresholds[slot - DEV_ALERT_FIRST]);
                        break;
                    }
                }
            }
    }
}

// Shows `menu` at the cursor and returns the chosen command (0 if none).
UINT TrackAtCursor(HMENU menu) {
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    // Opened from the taskbar, the menu must not overlap it: Windows only keeps a menu inside the monitor,
    // so its last items (Exit) ended up under the taskbar. Excluding the taskbar's rectangle makes Windows
    // open the menu above it (below / beside it for a top or side taskbar).
    TPMPARAMS tpm{ sizeof(tpm) };
    HWND under = GetAncestor(WindowFromPoint(pt), GA_ROOT);
    wchar_t cls[32] = {};
    if (under) GetClassNameW(under, cls, ARRAYSIZE(cls));
    bool fromTaskbar = !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd");
    if (fromTaskbar) GetWindowRect(under, &tpm.rcExclude);
    bool sideBar = fromTaskbar && tpm.rcExclude.bottom - tpm.rcExclude.top > tpm.rcExclude.right - tpm.rcExclude.left;
    UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | (sideBar ? TPM_HORIZONTAL : TPM_VERTICAL) |
                                          (i18n::Rtl() ? TPM_LAYOUTRTL : 0),
                                pt.x, pt.y, g_hwnd, fromTaskbar ? &tpm : nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    return cmd;
}

// The full menu: Shift + click and the keyboard keep it as the complete fallback to the settings panel.
void ShowMenu() {
    ApplyMenuTheme(g_lightTaskbar);
    MenuIcons icons(g_lightTaskbar);  // freed after the menu closes
    MenuModel model = BuildMenu(icons);
    UINT cmd = TrackAtCursor(model.menu);
    DestroyMenu(model.menu);
    memory_trim::Soon();
    RunCommand(cmd, model);
}

// ---- settings panel (panel.cpp): the menu is its model ----

MenuModel g_panelModel;
std::unique_ptr<MenuIcons> g_panelIcons;

void PanelRelease() {
    if (g_panelModel.menu) DestroyMenu(g_panelModel.menu);
    g_panelModel = MenuModel();
    g_panelIcons.reset();
}

HMENU PanelBuild(std::vector<panel::Device>& devices) {
    PanelRelease();
    ApplyMenuTheme(g_lightTaskbar);  // the panel's drop-downs are menus too
    g_panelIcons = std::make_unique<MenuIcons>(g_lightTaskbar);
    g_panelModel = BuildMenu(*g_panelIcons);
    for (const auto& key : g_panelModel.keys) {
        panel::Device d;
        d.key = key;
        d.hidden = IsHidden(key);
        auto it = g_entries.find(key);
        if (it != g_entries.end()) {
            d.name = DisplayName(it->second);
            d.status = StatusText(it->second);
            d.glyph = widget::DeviceGlyph(it->second.st.kind, it->second.st.via == L"Bluetooth");
        }
        devices.push_back(d);
    }
    return g_panelModel.menu;
}

void PanelRun(UINT cmd) { RunCommand(cmd, g_panelModel); }

bool PanelLight() { return g_lightTaskbar; }

// Which tab each menu item goes to. Ids not listed here land in General, so no menu option is ever missing.
panel::Place PanelPlace(UINT id) {
    using S = panel::Section;
    using K = panel::Kind;
    if (id >= ID_DEVICE_FIRST) return { S::Devices, K::Device };
    if (id >= ID_POSITION_FIRST && id <= ID_POSITION_FIRST + ARRAYSIZE(POSITION_OFFSETS)) return { S::Appearance, K::Choice };
    if (id >= ID_POLL_FIRST && id < ID_POLL_FIRST + ARRAYSIZE(POLL_SECONDS)) return { S::Devices, K::Choice };
    switch (id) {
        case ID_HIDE_LAST_KNOWN:
        case ID_TRAY_WHEN_EMPTY:
        case ID_SHOW_DISCONNECTED:
        case ID_DIM_SLEEPING:
        case ID_RECORD_HISTORY: return { S::Devices, K::Toggle };
        case ID_LIMIT_OFF: return { S::Devices, K::Choice };
        case ID_SHOW_CPU:
        case ID_SHOW_RAM:
        case ID_SHOW_GPU:
        case ID_SHOW_GPU_TEMP:
        case ID_SHOW_CPU_W:
        case ID_SHOW_GPU_W:
        case ID_SHOW_VRAM: return { S::System, K::Toggle };
        case ID_MODE_WIDGET:
        case ID_MODE_TRAY: return { S::Appearance, K::Radio };
        case ID_BAT_LEFT: return { S::Devices, K::Choice };   // where the battery strip sits
        case ID_VIT_LEFT: return { S::System, K::Choice };    // where the CPU / RAM / GPU strip sits
        case ID_WIDGET_BACKGROUND:
        case ID_STACKED:
        case ID_BRAND_COLORS: return { S::Appearance, K::Toggle };
        case ID_PLACE_AUTO:
        case ID_MONITOR_ALL: return { S::Appearance, K::Choice };
        case ID_LANG_AUTO: return { S::General, K::Choice };
        case ID_AUTOSTART:
        case ID_AUTO_UPDATE: return { S::General, K::Toggle };
        case ID_REFRESH:
        case ID_EXIT: return { S::Footer, K::Action };
        case 0: return { S::Skip, K::Action };  // the greyed hint line at the top of the menu
        default: return { S::General, K::Action };
    }
}

// Left or right click on the strip or the tray icon: the settings panel; with Shift held, the full menu.
void OpenSettings() {
    if (GetKeyState(VK_SHIFT) < 0) {
        ShowMenu();
        return;
    }
    panel::Host host;
    host.build = PanelBuild;
    host.run = PanelRun;
    host.release = PanelRelease;
    host.place = PanelPlace;
    host.light = PanelLight;
    panel::Show(g_inst, host);
}

// ---- worker thread ----------------------------------------------------------------------------

// `--demo`: one sample device of every brand instead of real ones, for screenshots and for checking the
// brand colors without owning the hardware. Keys, vendor ids and names are mixed on purpose, so every way
// of recognising a brand gets used.
bool g_demo = false;

std::vector<DeviceStatus> DemoDevices() {
    auto make = [](const wchar_t* key, const wchar_t* name, DeviceKind kind, int pct, unsigned short vid, bool charging) {
        DeviceStatus s;
        s.key = key;
        s.name = name;
        s.kind = kind;
        s.percent = pct;
        s.vid = vid;
        s.charging = charging;
        s.via = L"demo";
        return s;
    };
    std::vector<DeviceStatus> devices = {
        make(L"razer:demo", L"Razer DeathAdder V3 Pro", DeviceKind::Mouse, 78, 0x1532, false),
        make(L"demo:logi", L"Logitech G PRO X 2 LIGHTSPEED", DeviceKind::Headset, 64, 0x046D, false),
        make(L"steelseries:demo", L"SteelSeries Arctis Nova 7", DeviceKind::Headset, 55, 0, false),
        make(L"hsc:1b1c:0a6b", L"Corsair HS80 RGB Wireless", DeviceKind::Headset, 81, 0x1B1C, false),
        make(L"hyperx:demo", L"HyperX Cloud III Wireless", DeviceKind::Headset, 47, 0, false),
        make(L"rog:demo", L"ROG Harpe Ace", DeviceKind::Mouse, 90, 0, false),
        make(L"zowie:demo", L"ZOWIE EC2-CW", DeviceKind::Mouse, 66, 0x04A5, false),
        make(L"bt:demo-ps", L"Wireless Controller", DeviceKind::Gamepad, 38, 0x054C, false),
        make(L"xbox:demo", L"Xbox Wireless Controller", DeviceKind::Gamepad, 72, 0x045E, false),
        make(L"bt:demo-buds", L"Galaxy Buds3 Pro", DeviceKind::Headset, 59, 0, false),
        make(L"airpods:demo", L"AirPods Pro", DeviceKind::Headset, 85, 0, false),
        make(L"jabra:demo", L"Jabra Evolve2 65", DeviceKind::Headset, 23, 0x0B0E, false),
        make(L"jbl:demo", L"JBL Quantum 910", DeviceKind::Headset, 70, 0x0ECB, false),
        make(L"coolermaster:demo", L"Cooler Master MM712", DeviceKind::Mouse, 12, 0x2516, false),
        make(L"steam:demo", L"Steam Controller", DeviceKind::Gamepad, 50, 0x28DE, true),
        make(L"bt:demo-plain", L"Bluetooth Headphones", DeviceKind::Headset, 100, 0, true),  // charging at 100%: fully charged
    };
    DeviceStatus offline = make(L"demo:offline-receiver", L"Offline wireless headset", DeviceKind::Headset, -1, 0, false);
    offline.via = L"2.4 GHz receiver";
    offline.connected = false;
    devices.push_back(offline);  // hidden by default, shown when the opt-in setting is enabled
    return devices;
}

// A closer check of the next polls (see refresh_policy.h), handed to the worker for its next poll.
SRWLOCK g_monitorLock = SRWLOCK_INIT;
refresh_policy::Mode g_monitorRequest = refresh_policy::Mode::Idle;
std::set<std::wstring> g_monitorKeys;
std::set<std::wstring> g_awakeBeforeSleep;
DWORD g_displayState = 1;  // GUID_CONSOLE_DISPLAY_STATE: 0 off, 1 on, 2 dimmed

// Bluetooth connections (GUID_BLUETOOTH_HCI_EVENT) are reported only to windows that registered a
// handle to the radio.
struct BluetoothRadioWatch {
    HANDLE radio = nullptr;
    HDEVNOTIFY notify = nullptr;
};
std::vector<BluetoothRadioWatch> g_btRadioWatches;

void ClearBluetoothRadioWatches() {
    for (auto& w : g_btRadioWatches) {
        if (w.notify) UnregisterDeviceNotification(w.notify);
        if (w.radio) CloseHandle(w.radio);
    }
    g_btRadioWatches.clear();
}

void DropBluetoothRadioWatch(HANDLE radio) {
    for (auto it = g_btRadioWatches.begin(); it != g_btRadioWatches.end(); ++it) {
        if (it->radio != radio) continue;
        if (it->notify) UnregisterDeviceNotification(it->notify);
        if (it->radio) CloseHandle(it->radio);
        g_btRadioWatches.erase(it);
        return;
    }
}

void RefreshBluetoothRadioWatches() {
    ClearBluetoothRadioWatches();
    if (!g_hwnd) return;
    BLUETOOTH_FIND_RADIO_PARAMS params{ sizeof(params) };
    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio(&params, &radio);
    if (!find) return;
    do {
        DEV_BROADCAST_HANDLE filter{ sizeof(filter) };
        filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
        filter.dbch_handle = radio;
        HDEVNOTIFY notify = RegisterDeviceNotificationW(g_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (notify) {
            g_btRadioWatches.push_back({ radio, notify });
        } else {
            CloseHandle(radio);
        }
        radio = nullptr;
    } while (BluetoothFindNextRadio(find, &radio));
    BluetoothFindRadioClose(find);
}

std::set<std::wstring> AwakeDevices() {
    std::set<std::wstring> keys;
    for (const auto& [k, e] : g_entries)
        if (e.st.connected && !e.st.asleep) keys.insert(k);
    return keys;
}

std::set<std::wstring> DevicesWithLevel() {
    std::set<std::wstring> keys;
    for (const auto& [k, e] : g_entries)
        if (e.st.connected && e.st.percent >= 0) keys.insert(k);
    return keys;
}

void RequestMonitor(refresh_policy::Mode mode, std::set<std::wstring> keys) {
    AcquireSRWLockExclusive(&g_monitorLock);
    if (mode != refresh_policy::Mode::Arrival || g_monitorRequest != refresh_policy::Mode::Resume) {
        g_monitorRequest = mode;
        g_monitorKeys = std::move(keys);
    }
    ReleaseSRWLockExclusive(&g_monitorLock);
}

DWORD WINAPI Worker(LPVOID) {
    refresh_policy::Monitor monitor;
    monitor.BeginStartup();
    HANDLE events[2] = { g_stopEvt, g_refreshEvt };
    for (;;) {
        AcquireSRWLockExclusive(&g_monitorLock);
        refresh_policy::Mode request = std::exchange(g_monitorRequest, refresh_policy::Mode::Idle);
        std::set<std::wstring> keys = std::move(g_monitorKeys);
        g_monitorKeys.clear();
        ReleaseSRWLockExclusive(&g_monitorLock);
        if (request == refresh_policy::Mode::Resume) monitor.BeginResume(std::move(keys));
        else if (request == refresh_policy::Mode::Arrival) monitor.BeginArrival(std::move(keys));

        auto* v = new std::vector<DeviceStatus>(g_demo ? DemoDevices() : PollAllDevices());
        DWORD delay = monitor.DelayAfterPoll(*v, g_pollIntervalMs);
        if (!PostMessageW(g_hwnd, WM_RESULTS, 0, (LPARAM)v)) delete v;
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, delay);
        if (wait == WAIT_OBJECT_0) return 0;
        monitor.OnWaitFinished(wait == WAIT_TIMEOUT);
    }
}

void RefreshSoon(UINT ms) { SetTimer(g_hwnd, TIMER_DEBOUNCE, ms, nullptr); }
void RefreshAfterWake(UINT ms) { SetTimer(g_hwnd, TIMER_RESUME, ms, nullptr); }

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreatedMsg && msg != 0) {  // Explorer restarted
        widget::Destroy();
        vitals_strip::Destroy();
        g_useWidget = false;
        g_alertIconAdded = false;
        KillTimer(g_hwnd, TIMER_ALERT_ICON);
        ReAddAll();
        Reconcile();
        return 0;
    }
    switch (msg) {
        case WM_RESULTS: {
            auto* v = reinterpret_cast<std::vector<DeviceStatus>*>(lp);
            OnResults(*v);
            delete v;
            return 0;
        }
        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT: OpenSettings(); break;
                case NIN_KEYSELECT: ShowMenu(); break;  // keyboard: the menu, which is fully keyboard-usable
            }
            return 0;
        case WM_WIDGET_CLICK:
            OpenSettings();
            return 0;
        case WM_ENDSESSION:  // sign-out / shutdown / installer restart manager
            if (wp) DestroyWindow(h);
            return 0;
        case WM_DEVICECHANGE: {
            auto* hdr = reinterpret_cast<const DEV_BROADCAST_HDR*>(lp);
            bool handle = hdr && hdr->dbch_devicetype == DBT_DEVTYP_HANDLE;
            if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) {
                if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                    auto* iface = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(lp);
                    if (iface->dbcc_classguid == GUID_BTHPORT_DEVICE_INTERFACE) RefreshBluetoothRadioWatches();
                    else if (wp == DBT_DEVICEARRIVAL) RequestMonitor(refresh_policy::Mode::Arrival, DevicesWithLevel());
                }
                if (handle && wp == DBT_DEVICEREMOVECOMPLETE)
                    DropBluetoothRadioWatch(reinterpret_cast<const DEV_BROADCAST_HANDLE*>(lp)->dbch_handle);
                RefreshSoon(2500);
            } else if (handle && (wp == DBT_DEVICEQUERYREMOVE || wp == DBT_DEVICEREMOVEPENDING)) {
                // Let the radio go: an open handle would block its removal.
                DropBluetoothRadioWatch(reinterpret_cast<const DEV_BROADCAST_HANDLE*>(lp)->dbch_handle);
            } else if (handle && wp == DBT_DEVICEQUERYREMOVEFAILED) {
                RefreshBluetoothRadioWatches();
            } else if (handle && wp == DBT_CUSTOMEVENT) {
                auto* bh = reinterpret_cast<const DEV_BROADCAST_HANDLE*>(lp);
                // HCI_CONNECTION_TYPE_*: 1 classic (ACL), 2 the voice link of a call (SCO), 3 Bluetooth LE.
                constexpr UCHAR kHciAcl = 1, kHciSco = 2;
                BTH_HCI_EVENT_INFO info{};
                if (bh->dbch_eventguid == GUID_BLUETOOTH_HCI_EVENT &&
                    bh->dbch_size >= offsetof(DEV_BROADCAST_HANDLE, dbch_data) + sizeof(info)) {
                    memcpy(&info, bh->dbch_data, sizeof(info));
                    if (info.connectionType != kHciSco) {
                        // A Bluetooth headset reports its level a few seconds after it connects.
                        if (info.connected && info.connectionType == kHciAcl)
                            RequestMonitor(refresh_policy::Mode::Arrival, DevicesWithLevel());
                        RefreshSoon(2500);
                    }
                }
            }
            return TRUE;
        }
        case WM_POWERBROADCAST:
            if (wp == PBT_APMSUSPEND) {
                g_awakeBeforeSleep = AwakeDevices();
            } else if (wp == PBT_APMRESUMEAUTOMATIC) {
                RefreshAfterWake(5000);
            } else if (wp == PBT_POWERSETTINGCHANGE && lp) {
                // Modern Standby may not report a suspend / resume: the display turning off and on stands in.
                auto* ps = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
                if (ps->PowerSetting == kConsoleDisplayStateGuid && ps->DataLength >= sizeof(DWORD)) {
                    DWORD state = *reinterpret_cast<const DWORD*>(ps->Data);
                    if (g_displayState != 0 && state == 0) g_awakeBeforeSleep = AwakeDevices();
                    else if (g_displayState == 0 && state != 0) RefreshAfterWake(3000);
                    g_displayState = state;
                }
            }
            return TRUE;
        case WM_TIMER:
            if (wp == TIMER_DEBOUNCE) { KillTimer(h, TIMER_DEBOUNCE); SetEvent(g_refreshEvt); }
            if (wp == TIMER_RESUME) {
                KillTimer(h, TIMER_RESUME);
                RequestMonitor(refresh_policy::Mode::Resume, std::move(g_awakeBeforeSleep));
                g_awakeBeforeSleep.clear();
                SetEvent(g_refreshEvt);
            }
            if (wp == TIMER_WIDGET) { Reconcile(); TickSystemReadings(); updates::Tick(h); }
            if (wp == TIMER_ALERT_ICON) {
                KillTimer(h, TIMER_ALERT_ICON);
                if (g_alertIconAdded) { TrayDelete(ALERT_ICON_ID); g_alertIconAdded = false; }
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (lp && !lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet")) {
                bool light = ReadLightTaskbar();
                if (light != g_lightTaskbar) { g_lightTaskbar = light; ApplyMenuTheme(light); RedrawAll(); widget::SetLightTheme(light); vitals_strip::SetLightTheme(light); panel::Refresh(); }
            }
            return 0;
        case WM_DESTROY:
            updates::Shutdown();
            KillTimer(h, TIMER_DEBOUNCE);
            KillTimer(h, TIMER_RESUME);
            KillTimer(h, TIMER_WIDGET);
            KillTimer(h, TIMER_ALERT_ICON);
            if (g_alertIconAdded) { TrayDelete(ALERT_ICON_ID); g_alertIconAdded = false; }
            panel::Close();
            widget::Destroy();
            vitals_strip::Destroy();
            gpumon::Stop();
            SetEvent(g_stopEvt);
            if (g_worker) WaitForSingleObject(g_worker, 5000);
            for (auto& [k, e] : g_entries) {
                if (e.added) TrayDelete(e.uid);
                if (e.icon) DestroyIcon(e.icon);
            }
            if (g_placeholder) TrayDelete(PLACEHOLDER_ID);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// `TaskbarVitals.exe --list` prints what it finds, `--dump` every HID collection.
int ListToConsole(bool dump) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    auto write = [&](const std::wstring& s) {
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string u(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), u.data(), n, nullptr, nullptr);
        DWORD w;
        WriteFile(out, u.data(), (DWORD)u.size(), &w, nullptr);
    };
    if (dump) {
        // Also saved to a file: PowerShell does not wait for this windowed exe, so console output can end up
        // after the prompt or get lost, and a double-click has no console at all.
        std::wstring text = DumpHidCollections();
        wchar_t path[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, path);
        wcscat_s(path, L"TaskbarVitals-dump.txt");
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            std::wstring crlf;
            for (wchar_t ch : text) crlf += ch == L'\n' ? std::wstring(L"\r\n") : std::wstring(1, ch);
            int n = WideCharToMultiByte(CP_UTF8, 0, crlf.c_str(), (int)crlf.size(), nullptr, 0, nullptr, nullptr);
            std::string u = "\xEF\xBB\xBF" + std::string(n, 0);  // UTF-8 byte order mark, so Notepad shows it right
            WideCharToMultiByte(CP_UTF8, 0, crlf.c_str(), (int)crlf.size(), u.data() + 3, n, nullptr, nullptr);
            DWORD w;
            WriteFile(f, u.data(), (DWORD)u.size(), &w, nullptr);
            CloseHandle(f);
        }
        if (out && out != INVALID_HANDLE_VALUE) {
            write(text);
            write(std::wstring(L"\nAlso saved to ") + path + L"\n");
        } else {
            ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);  // no console: show the file
        }
        return 0;
    }
    auto list = PollAllDevices();
    if (list.empty()) write(L"No devices with battery info found.\n");
    for (auto& s : list) {
        Accent accent = BrandOf(s);
        std::wstring line = s.name + L"  [" + s.via + L"]  " +
                            (accent != Accent::None ? L"{" + std::wstring(StyleOf(accent).name) + L"}  " : L"");
        if (!s.connected) line += L"off / not connected";
        else if (s.asleep) line += L"asleep";
        else if (!s.levelText.empty()) line += s.levelText;
        else if (s.percent >= 0) line += std::to_wstring(s.percent) + L"%";
        else line += L"unknown";
        if (!s.detail.empty()) line += L" (" + s.detail + L")";
        if (s.full || (s.charging && s.percent >= 100)) line += L", fully charged";
        else if (s.charging) line += L", charging";
        if (s.millivolts) line += L", " + std::to_wstring(s.millivolts) + L" mV";
        write(line + L"   (" + s.key + L")\n");
    }
    return 0;
}

// Machine-readable device list, shared with the Linux entry point (src/json_output.h).
int ListJson() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    std::string text = DevicesToJson(PollAllDevices());  // UTF-8
    if (out && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, text.data(), (DWORD)text.size(), &written, nullptr);
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && wcsstr(cmdLine, L"--json")) return ListJson();
    if (cmdLine && wcsstr(cmdLine, L"--help")) {
        MessageBoxW(nullptr,
                    L"Taskbar Vitals options:\n\n"
                    L"--list   Print devices and battery levels\n"
                    L"--json   Print a JSON array of devices and battery levels\n"
                    L"--dump   Print every HID collection and device\n"
                    L"--report Open the \"Request support for a device\" window\n"
                    L"--demo   Show sample devices\n"
                    L"--quit   Quit a running instance",
                    L"Taskbar Vitals", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int applyResult = argv ? updates::RunApplyMode(argc, argv) : -1;
    if (argv) LocalFree(argv);
    if (applyResult >= 0) return applyResult;
    if (cmdLine && wcsstr(cmdLine, L"--list")) return ListToConsole(false);
    if (cmdLine && wcsstr(cmdLine, L"--dump")) return ListToConsole(true);
    if (cmdLine && wcsstr(cmdLine, L"--report")) {  // the "Request support for a device" window on its own
        i18n::Init();
        report::Show(inst, nullptr);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (report::PreTranslate(&msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return 0;
    }
    if (cmdLine && wcsstr(cmdLine, L"--quit")) {  // used by the installer before upgrading/uninstalling
        if (HWND w = FindWindowW(L"WirelessBatteryWnd", nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            HANDLE running = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
            if (!running) return 1;
            PostMessageW(w, WM_CLOSE, 0, 0);
            DWORD exited = WaitForSingleObject(running, 30000);
            CloseHandle(running);
            if (exited != WAIT_OBJECT_0) return 1;
        }
        return 0;
    }
    g_inst = inst;
    g_demo = cmdLine && wcsstr(cmdLine, L"--demo");

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\WirelessBatteryTray");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    updates::Initialize();

    g_lightTaskbar = ReadLightTaskbar();
    ApplyMenuTheme(g_lightTaskbar);
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"WirelessBatteryWnd";
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only) so it receives the TaskbarCreated broadcast.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;

    DEV_BROADCAST_DEVICEINTERFACE_W filter{ sizeof(filter) };
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVINTERFACE_HID;
    HDEVNOTIFY notify = RegisterDeviceNotificationW(g_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    // Bluetooth radios coming and going, and the connections on each one.
    filter.dbcc_classguid = GUID_BTHPORT_DEVICE_INTERFACE;
    HDEVNOTIFY radioNotify = RegisterDeviceNotificationW(g_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    RefreshBluetoothRadioWatches();
    HPOWERNOTIFY displayNotify =
        RegisterPowerSettingNotification(g_hwnd, &kConsoleDisplayStateGuid, DEVICE_NOTIFY_WINDOW_HANDLE);

    g_refreshEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_stopEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_wantWidget = LoadWidgetMode();
    g_hideLastKnown = LoadHideLastKnown();
    g_trayWhenEmpty = LoadTrayWhenEmpty();
    g_showDisconnectedDevices = LoadShowDisconnectedDevices();
    g_dimSleeping = LoadDimSleeping();
    g_recordHistory = LoadRecordHistory();
    g_chargeLimit = LoadChargeLimit();
    g_widgetBackground = LoadWidgetBackground();
    g_brandColors = LoadBrandColors();
    g_stacked = LoadStacked();
    g_makeRoom = LoadMakeRoom();
    widget::SetMakeRoom(g_makeRoom);
    g_farLeft = LoadFarLeft();
    g_vitalsFarLeft = LoadBoolSetting(L"VitalsFarLeft", false);
    g_showCpu = LoadShowCpu();
    g_showRam = LoadShowRam();
    g_showGpu = LoadBoolSetting(L"ShowGpu", false);
    g_showGpuTemp = LoadBoolSetting(L"ShowGpuTemp", false);
    g_showCpuW = LoadBoolSetting(L"ShowCpuWatts", false);
    g_showGpuW = LoadBoolSetting(L"ShowGpuWatts", false);
    g_showVram = LoadBoolSetting(L"ShowVram", false);
    g_pollIntervalMs = LoadPollSeconds() * 1000;
    widget::SetStacked(g_stacked);
    g_verticalPlace = LoadVerticalPlace();
    widget::SetVerticalPlace(g_verticalPlace);
    widget::SetFarLeft(g_farLeft);
    widget::SetBackground(g_widgetBackground);  // remembered by the widget across re-creation
    i18n::Init();
    g_widgetMonitor = LoadWidgetMonitor();
    widget::SetMonitorChoice(g_widgetMonitor);
    LoadHidden();
    LoadOrder();
    LoadAliases();
    LoadAlertThresholds();
    battery_history::Initialize(g_demo);
    Reconcile();
    UpdatePlaceholder();  // something to click on until the first poll finishes
    SetTimer(g_hwnd, TIMER_WIDGET, 1000, nullptr);
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (report::PreTranslate(&m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    // drain results posted after shutdown started
    while (PeekMessageW(&m, nullptr, WM_RESULTS, WM_RESULTS, PM_REMOVE)) delete reinterpret_cast<std::vector<DeviceStatus>*>(m.lParam);
    if (notify) UnregisterDeviceNotification(notify);
    if (radioNotify) UnregisterDeviceNotification(radioNotify);
    ClearBluetoothRadioWatches();
    if (displayNotify) UnregisterPowerSettingNotification(displayNotify);
    if (g_worker) CloseHandle(g_worker);
    ReleaseMutex(mutex);
    return 0;
}
