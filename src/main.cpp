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
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "device_aliases.h"
#include "drivers/hidpp_events.h"
#include "hid_listener.h"
#include "alert_rules.h"
#include "devices.h"
#include "battery_history.h"
#include "battery_intelligence.h"
#include "charge_estimate.h"
#include "dashboard.h"
#include "i18n.h"
#include "json_output.h"
#include "labels.h"
#include "reading_freshness.h"
#include "status_json.h"
#include "refresh_policy.h"
#include "memory_trim.h"
#include "panel.h"
#include "report.h"
#include "rename_dialog.h"
#include "offset_dialog.h"
#include "cputemp_client.h"
#include "diskmon.h"
#include "gpumon.h"
#include "laptopbat.h"
#include "netmon.h"
#include "sysmon.h"
#include "vector_icons.h"
#include "vitals_strip.h"
#include "widget.h"
#include "update_win.h"
#include "update_apply.h"

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_RESULTS = WM_APP + 2;
constexpr UINT WM_WIDGET_CLICK = WM_APP + 3;
constexpr UINT WM_DEVICE_EVENT = WM_APP + 4;  // lParam: ms to wait before polling
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
constexpr DWORD SYS_POLL_SECONDS[] = { 1, 2, 5, 10 };
constexpr int TEMP_ALERT_CELSIUS[] = { 80, 85, 90, 95 };
constexpr int LOW_BATTERY = 15;
constexpr UINT PLACEHOLDER_ID = 1;
constexpr UINT ALERT_ICON_ID = 2;
constexpr UINT SYS_TRAY_UID_BASE = 10;

enum MenuId : UINT {
    ID_REFRESH = 100, ID_AUTOSTART, ID_HIDE_LAST_KNOWN, ID_WIDGET_BACKGROUND, ID_EXIT,
    ID_MODE_WIDGET, ID_MODE_TRAY, ID_MODE_MYFINDER, ID_SUPPORT, ID_REPORT,
    ID_BRAND_COLORS, ID_SYSTEM_COLORS, ID_STACKED, ID_TWO_ROWS, ID_CUSTOM_ICONS,
    ID_SHOW_DISCONNECTED, ID_KEEP_MOUSE_LAST_KNOWN, ID_RECORD_HISTORY, ID_DIM_SLEEPING,
    ID_TRAY_WHEN_EMPTY, ID_MAKE_ROOM,
    ID_BAT_LEFT, ID_BAT_BEFORE, ID_BAT_CLOCK,
    ID_VIT_LEFT, ID_VIT_BEFORE, ID_VIT_CLOCK,
    ID_BATTERIES_FIRST, ID_VITALS_FIRST,
    ID_SHOW_CPU, ID_SHOW_RAM, ID_SHOW_GPU, ID_SHOW_GPU_TEMP, ID_SHOW_GPU_CLOCKS, ID_SHOW_VRAM,
    ID_SHOW_CPU_W, ID_SHOW_GPU_W, ID_SHOW_CPU_TEMP, ID_REMOVE_TEMP,
    ID_SHOW_DISK_TEMP, ID_SHOW_DISK_USE, ID_SHOW_NET_SPEED, ID_RUN_SPEED_TEST,
    ID_SHOW_LAPTOP_BAT, ID_SHOW_LAPTOP_BAT_W, ID_SYSTEM_DASHBOARD,
    ID_DASHBOARD, ID_CHARGED_ALERT, ID_TIERED_ALERTS, ID_SHORT_RUNTIME_ALERT,
    ID_TRACK_SINCE, ID_TRACK_AWAKE, ID_TRACK_EMPTY, ID_RUNTIME_WIDGET,
    ID_PERCENT_SIGN, ID_RING_STYLE, ID_ANIMATE_CHARGING, ID_CONNECTION_ICONS,
    ID_HOVER_CARD, ID_HOVER_CHART, ID_TRAY_DEVICE_ICONS,
    ID_PLACE_AUTO, ID_PLACE_TOP, ID_PLACE_TRAY, ID_PLACE_BOTTOM,  // "Position on a vertical taskbar"
    ID_SYS_MOVE_FIRST = 750,  // System strip item order: CPU, RAM, GPU, VRAM, Disk, Network, Laptop battery
    ID_POSITION_FIRST = 700,  // "Position on the taskbar": ID_POSITION_FIRST + 0..5, then the custom value
    ID_POLL_FIRST = 500,      // "Check battery every": ID_POLL_FIRST + 0..7
    ID_SYS_POLL_FIRST = 550,  // "Check system every": ID_SYS_POLL_FIRST + 0..3
    ID_LIMIT_OFF = 600, ID_LIMIT_FIRST,  // "Notify when charged to": off, then alerts::kChargeLimits
    ID_TEMP_ALERT_OFF = 650, ID_TEMP_ALERT_FIRST,  // "High temperature alert": off, then TEMP_ALERT_CELSIUS
    ID_POPUP_CHARGE_LIMIT = 800, ID_POPUP_POLL_BATTERY, ID_POPUP_TEMP_ALERT, ID_POPUP_POLL_SYSTEM,
    ID_POPUP_BAT_POS, ID_POPUP_VIT_POS, ID_POPUP_STRIP_ORDER, ID_POPUP_TASKBAR_POS,
    ID_POPUP_VERTICAL_POS, ID_POPUP_MONITORS, ID_POPUP_LANG, ID_POPUP_TRACKING, ID_POPUP_UNSUPPORTED,
    ID_UPDATE = 200, ID_AUTO_UPDATE,
    ID_LANG_AUTO = 400, ID_LANG_ADD, ID_LANG_FIRST,  // "Language": ID_LANG_FIRST + 0..63
    ID_MONITOR_ALL = 300, ID_MONITOR_MAIN, ID_MONITOR_FIRST,  // "Show the widget on": ID_MONITOR_FIRST + 0..31
    ID_DEVICE_FIRST = 1000
};
enum DeviceCmd : UINT {
    DEV_TOGGLE, DEV_LEFT, DEV_RIGHT, DEV_RENAME, DEV_RESET_NAME, DEV_HISTORY,
    DEV_ALERT_FIRST = 6, DEV_ALERT_DEFAULT = DEV_ALERT_FIRST + 8, DEV_ALERT_OFF,
    DEV_MORE_POPUP = 16, DEV_DASHBOARD = 17,
    DEV_STRIDE = 32
};

const wchar_t* kAppName = L"Taskbar Vitals";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"WirelessBattery";

const wchar_t* kSettingsKey = L"Software\\WirelessBattery";

HINSTANCE g_inst;
HWND g_hwnd;
HANDLE g_refreshEvt, g_stopEvt, g_worker;
// Push-event emitters: a Logitech mouse switched on behind a receiver that stays plugged in produces no Windows event.
std::vector<std::unique_ptr<IEventSource>> g_sources;
struct WindowRefreshSink final : IRefreshSink {
    void RequestRefresh(std::uint32_t delayMs) override { PostMessageW(g_hwnd, WM_DEVICE_EVENT, 0, (LPARAM)delayMs); }
} g_refreshSink;
UINT g_taskbarCreatedMsg;
bool g_lightTaskbar;
bool g_myFinder = false;       // widget mode on MyDockFinder's bar instead of the taskbar
bool g_wantWidget = true;      // user's choice: taskbar widget (default) or tray icons
bool g_useWidget = false;      // what is actually shown right now
bool g_hideLastKnown = false;  // hide (last known) / asleep devices from taskbar and tray
bool g_keepMouseLastKnown = false;  // with "Hide last known", still show mice
bool g_trayWhenEmpty = false;  // taskbar mode: swap the strip for the tray placeholder while no device is shown
bool g_emptyToTray = false;    // that swap is in effect right now
bool g_showDisconnectedDevices = false;  // opt-in, explicit driver-reported offline devices only
bool g_dimSleeping = true;  // dim a sleeping device's last known value (menu option, on by default)
bool g_recordHistory = true;  // save readings for "Battery history..." (menu option, on by default)
int g_chargeLimit = 0;        // "Notify when charged to" 80/85/90%, 0 = off (the default)
bool g_chargedAlert = true;   // "fully charged" notification
bool g_tieredAlerts = true;   // extra low-battery warnings at 10% and 5%
bool g_shortRuntimeAlert = true;  // "charge today" notification when the time-to-empty is under 12 h
bool g_hoverCard = true;      // card when the mouse rests on an item (off: plain tooltip)
bool g_hoverChart = true;     // chart in the hover card
bool g_animateCharging = false;  // charging icons fill in a loop on the taskbar
bool g_trayDeviceIcons = false;  // tray mode: device symbol + level bar instead of the number
bool g_runtimeWidget = false;    // optionally show estimated remaining hours instead of % in taskbar widget
std::wstring g_widgetMonitor = L"all";  // which taskbars show the widget: "all", "main" or one monitor id
bool g_widgetBackground = false;  // draw a plate behind the taskbar text (transparent taskbar themes)
bool g_stacked = false;      // icon above the percentage
bool g_twoRows = false;      // compact 2-row grid layout on the taskbar
bool g_customIcons = true;   // 4x4 supersampled vector glyphs (off: Segoe Fluent / MDL2)
bool g_showPercentSign = true;  // include '%' in taskbar battery labels
bool g_ringStyle = false;    // device icon inside a battery ring
bool g_connectionIcons = false;  // wireless mark for 2.4 GHz devices
bool g_makeRoom = true;      // step past other apps' taskbar widgets
// "Position on the taskbar": distance from the notification area in DPI-independent pixels. Stored as
// the TaskbarOffset value the widget has always read, so a value set by hand keeps working.
const int POSITION_OFFSETS[] = { 0, 40, 80, 120, 160, 200 };
widget::VerticalPlace g_verticalPlace = widget::VerticalPlace::Auto;  // column position on a left / right taskbar
bool g_farLeft = false;      // battery strip at the left end of the taskbar instead of next to the clock
bool g_beforeApps = false;   // battery strip just before the taskbar apps
bool g_vitalsFarLeft = false;  // CPU / RAM / GPU readings at the left end of the taskbar
bool g_vitalsBeforeApps = false;  // system readings just before the taskbar apps
bool g_vitalsFirst = false;    // system readings before batteries when both sit on the same side
bool g_leftEndPlaced = false;  // with two strips: the left-end one found room there at the last update
bool g_showCpu = false, g_showRam = false;  // system readings on the strip
bool g_showGpu = false, g_showGpuTemp = false, g_showGpuClocks = false, g_showVram = false;
bool g_showCpuW = false, g_showGpuW = false;  // power draw in watts
bool g_showCpuTemp = false;  // CPU temperature, from the elevated helper (src/cputemp.h)
bool g_showDiskTemp = false, g_showDiskUse = false;  // SSD / HDD temperature & busy utilization
bool g_showNetSpeed = false;  // live network throughput
bool g_hasLaptopBat = false;  // true when Windows reports an internal system battery
bool g_showLaptopBat = false, g_showLaptopBatW = false;
cputemp::Reading g_cpuTemp;
std::vector<int> g_systemOrder{ 0, 1, 2, 3, 4, 5, 6 };  // CPU, RAM, GPU, VRAM, Storage, Network, Laptop battery
sysmon::Reading g_sys;       // last CPU / RAM sample
gpumon::Reading g_gpu;       // last GPU sample
diskmon::Reading g_disk;     // last SSD/HDD sample
netmon::Reading g_net;       // last network sample
laptopbat::Reading g_laptopBat;
int g_sysTicks = 0;
DWORD g_sysPollSeconds = 2;
int g_tempAlertC = 0;
bool g_cpuTempWarned = false, g_gpuTempWarned = false, g_diskTempWarned = false;
netmon::TestState g_lastSpeedTestState = netmon::TestState::Idle;
bool AnyGpuReading() { return g_showGpu || g_showGpuTemp || g_showGpuClocks || g_showVram || g_showGpuW; }
bool AnyCpuReading() { return g_showCpu || g_showRam || g_showCpuW || g_showCpuTemp; }
bool AnyDiskReading() { return g_showDiskTemp || g_showDiskUse; }
bool AnyLaptopBatReading() { return g_hasLaptopBat && (g_showLaptopBat || g_showLaptopBatW); }
bool AnyReading() { return AnyCpuReading() || AnyGpuReading() || AnyDiskReading() || g_showNetSpeed || AnyLaptopBatReading(); }
bool g_brandColors = false;   // off by default; known brands get their color (Razer green, Logitech blue, ...)
bool g_systemColors = false;  // off by default; CPU blue, RAM purple, GPU green, VRAM orange, SSD yellow, Net teal, Bat green
bool g_trackSince = false, g_trackAwake = false, g_trackEmpty = false;  // "Battery tracking" menu
bool g_demo = false;          // --demo
bool g_trace = false;         // --trace: one line per poll in %TEMP%\wirelessbattery-trace.log
std::atomic<long> g_lastCount{ 0 };
ULONGLONG g_lastPollTick = 0;
std::atomic<DWORD> g_pollIntervalMs{ 60 * 1000 };  // set from the menu, read by the worker thread

// Rolling 3-minute live history for Vitals hover cards.
struct VitalPoint {
    ULONGLONG tickMs = 0;
    int value = 0;
};
constexpr ULONGLONG kVitalHistoryMs = 180000;
std::deque<VitalPoint> g_cpuHist, g_ramHist, g_gpuHist, g_vramHist, g_diskHist, g_netHist, g_batHist;

void PushVitalHist(std::deque<VitalPoint>& hist, ULONGLONG now, int value) {
    if (value < 0) return;
    hist.push_back({ now, value });
    while (!hist.empty() && now - hist.front().tickMs > kVitalHistoryMs + 15000) hist.pop_front();
}

std::vector<std::pair<long long, int>> BuildVitalTrend(const std::deque<VitalPoint>& hist, ULONGLONG now) {
    std::vector<std::pair<long long, int>> out;
    out.reserve(hist.size());
    for (const auto& pt : hist) {
        if (now < pt.tickMs || now - pt.tickMs > kVitalHistoryMs) continue;
        long long secAgo = -(long long)((now - pt.tickMs) / 1000);
        out.push_back({ secAgo, pt.value });
    }
    return out;
}

// Battery tracking: when each device was last charged and roughly how long it was awake since.
const wchar_t* kTrackerKey = L"Software\\WirelessBattery\\ChargeTracker";
struct ChargeRecord {
    ULONGLONG since = 0;     // when the last charge ended, in seconds since 1601 (0 = not yet)
    ULONGLONG awakeSec = 0;  // seconds awake since then
    LONGLONG lastPct = -1;   // last live level (a jump up means it was charged)
};

ULONGLONG NowSec() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000;
}

struct SysTraySlot {
    bool added = false;
    std::wstring sig;
    HICON icon = nullptr;
};
SysTraySlot g_sysTray[7];

struct TrayEntry {
    UINT uid = 0;
    DeviceStatus st;
    int lastPct = -1;        // last known percentage, kept while the device sleeps
    ULONGLONG lastSeen = 0;
    ULONGLONG lastReadingTick = 0;  // last confirmed battery sample, for "Last reading: 5 min ago"
    bool added = false;
    bool lowWarned = false;
    bool chargedWarned = false;
    int missStreak = 0;
    bool limitWarned = false;  // "unplug at 80%" already shown during this charge
    bool shortWarned = false;  // "charge today" already shown during this discharge
    charge::Tracker charge;    // readings of the current charge, for "full in ~40 min"
    std::wstring iconSig;    // what the current icon shows, to avoid needless redraws
    HICON icon = nullptr;
    ChargeRecord chargeRecord;
    bool chargeLoaded = false;
};
std::map<std::wstring, TrayEntry> g_entries;
std::vector<std::wstring> g_unsupported;  // seen wireless devices without a battery driver, menu only
std::map<std::wstring, alerts::LowLatch> g_alertLatches;  // survive transient removal of a device
bool g_alertIconAdded = false;  // short-lived tray icon for widget-mode notifications
bool g_placeholder = false;
HICON g_placeholderIcon = nullptr;

// Reads/updates the charge tracker for one device.
void TrackCharge(TrayEntry& e, const DeviceStatus& s, ULONGLONG sampleMs) {
    ChargeRecord& r = e.chargeRecord;
    if (!e.chargeLoaded) {
        DWORD sz = sizeof(r);
        LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, kTrackerKey, s.key.c_str(), RRF_RT_REG_BINARY, nullptr, &r, &sz);
        if (st != ERROR_SUCCESS || sz != sizeof(r)) r = ChargeRecord{};
        e.chargeLoaded = true;
    }
    ChargeRecord before = r;
    bool awake = s.connected && !s.asleep && s.percent >= 0;
    if (s.charging || s.full || !r.since || (awake && r.lastPct >= 0 && s.percent >= r.lastPct + 10)) {
        r.since = NowSec();
        r.awakeSec = 0;
    } else if (awake) {
        r.awakeSec += sampleMs / 1000;
    }
    if (awake) r.lastPct = s.percent;
    if (memcmp(&before, &r, sizeof(r)) != 0)
        RegSetKeyValueW(HKEY_CURRENT_USER, kTrackerKey, s.key.c_str(), REG_BINARY, &r, sizeof(r));
}

std::wstring Duration(ULONGLONG sec) {
    std::wstring d = std::to_wstring(sec / 86400), h = std::to_wstring(sec / 3600 % 24);
    std::wstring m = std::to_wstring(sec / 60 % 60);
    if (sec >= 86400) return TF(L"{days} d {hours} h", { { L"days", d }, { L"hours", h } });
    if (sec >= 3600) return TF(L"{hours} h {minutes} min", { { L"hours", h }, { L"minutes", m } });
    return TF(L"{minutes} min", { { L"minutes", m } });
}

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

void LoadSystemOrder() {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"SystemOrder", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return;
    std::vector<int> order;
    for (int i = 0; i < 7; ++i) {
        int item = (value >> (i * 4)) & 0xF;
        if (item < 7 && std::find(order.begin(), order.end(), item) == order.end()) order.push_back(item);
    }
    for (int i = 0; i < 7; ++i)
        if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
    g_systemOrder = std::move(order);
}

void SaveSystemOrder() {
    DWORD value = 0;
    for (size_t i = 0; i < g_systemOrder.size() && i < 7; ++i) value |= (DWORD(g_systemOrder[i]) & 0xF) << (i * 4);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"SystemOrder", REG_DWORD, &value, sizeof(value));
}

bool IsEntryHidden(const TrayEntry& e) {
    if (IsHidden(e.st.key)) return true;
    // An explicitly offline device is not just a stale battery reading.
    if (g_showDisconnectedDevices && !e.st.connected) return false;
    if (g_keepMouseLastKnown && e.st.kind == DeviceKind::Mouse) return false;
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
bool LoadBeforeApps() { return LoadBoolSetting(L"WidgetBeforeApps", false); }
void SaveBeforeApps(bool on) { SaveBoolSetting(L"WidgetBeforeApps", on); }
bool LoadRuntimeWidget() { return LoadBoolSetting(L"EstimatedRuntimeInWidget", false); }
void SaveRuntimeWidget(bool on) { SaveBoolSetting(L"EstimatedRuntimeInWidget", on); }
bool LoadShowPercentSign() { return LoadBoolSetting(L"ShowPercentSign", true); }
void SaveShowPercentSign(bool on) { SaveBoolSetting(L"ShowPercentSign", on); }
bool LoadRingStyle() { return LoadBoolSetting(L"RingStyle", false); }
void SaveRingStyle(bool on) { SaveBoolSetting(L"RingStyle", on); }
bool LoadConnectionIcons() { return LoadBoolSetting(L"ConnectionIcons", false); }
void SaveConnectionIcons(bool on) { SaveBoolSetting(L"ConnectionIcons", on); }
bool LoadTrackSince() { return LoadBoolSetting(L"TrackSinceCharge", false); }
void SaveTrackSince(bool on) { SaveBoolSetting(L"TrackSinceCharge", on); }
bool LoadTrackAwake() { return LoadBoolSetting(L"TrackAwakeTime", false); }
void SaveTrackAwake(bool on) { SaveBoolSetting(L"TrackAwakeTime", on); }
bool LoadTrackEmpty() { return LoadBoolSetting(L"TrackTimeToEmpty", false); }
void SaveTrackEmpty(bool on) { SaveBoolSetting(L"TrackTimeToEmpty", on); }
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
DWORD LoadSystemPollSeconds() {
    DWORD v = 2, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"SystemPollSeconds", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    for (DWORD s : SYS_POLL_SECONDS)
        if (v == s) return s;
    return 2;
}
void SaveSystemPollSeconds(DWORD seconds) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"SystemPollSeconds", REG_DWORD, &seconds, sizeof(seconds));
}
int LoadTempAlertCelsius() {
    DWORD v = 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"TempAlertCelsius", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    for (int t : TEMP_ALERT_CELSIUS)
        if ((int)v == t) return t;
    return 0;
}
void SaveTempAlertCelsius(int celsius) {
    DWORD v = (DWORD)celsius;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TempAlertCelsius", REG_DWORD, &v, sizeof(v));
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

// deviceGlyph: `text` is a Segoe Fluent Icons / MDL2 glyph (tray device icons), drawn regular weight.
HICON RenderIcon(const std::wstring& text, int barPct, Rgb textColor, Rgb barColor, bool dim, bool deviceGlyph = false) {
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
        static const wchar_t* iconFace = [] {
            HDC probeDc = CreateCompatibleDC(nullptr);
            HFONT probe = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe Fluent Icons");
            HGDIOBJ old = SelectObject(probeDc, probe);
            wchar_t actual[LF_FACESIZE] = {};
            GetTextFaceW(probeDc, LF_FACESIZE, actual);
            SelectObject(probeDc, old);
            DeleteObject(probe);
            DeleteDC(probeDc);
            return _wcsicmp(actual, L"Segoe Fluent Icons") == 0 ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
        }();
        HFONT f = CreateFontW(-fh, 0, 0, 0, deviceGlyph ? FW_NORMAL : FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, deviceGlyph ? iconFace : L"Segoe UI");
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

Accent SystemAccentFor(int item) {
    if (!g_systemColors) return Accent::None;
    switch (item) {
        case 0: return Accent::Logitech;     // CPU: blue
        case 1: return Accent::CoolerMaster; // RAM: purple
        case 2: return Accent::Razer;        // GPU: green
        case 3: return Accent::SteelSeries;  // GPU memory: orange
        case 4: return Accent::Corsair;      // Storage / SSD: yellow-amber
        case 5: return Accent::Steam;        // Network: slate/teal blue
        case 6: return Accent::Xbox;         // Laptop battery: green
        default: return Accent::None;
    }
}

COLORREF AccentColorRef(Accent a, bool light) {
    BrandColor bc = AccentColor(a, light);
    return RGB(bc.r, bc.g, bc.b);
}

// glyph != 0: the device's symbol with the level as the bar, instead of the number (option).
HICON IconFor(int pct, bool charging, bool dim, Accent accent, wchar_t glyph = 0) {
    Rgb fg = Foreground();
    BrandColor bc = AccentColor(accent, g_lightTaskbar);
    Rgb bar = charging        ? Rgb{ 0, 170, 255 }
            : pct <= LOW_BATTERY ? Rgb{ 232, 32, 48 }
            : pct <= 30       ? Rgb{ 255, 150, 0 }
            : accent != Accent::None ? Rgb{ bc.r, bc.g, bc.b }
                              : Rgb{ 30, 190, 90 };
    Rgb text = (!charging && pct >= 0 && pct <= LOW_BATTERY) ? Rgb{ 240, 50, 60 } : fg;
    if (glyph) return RenderIcon(std::wstring(1, glyph), pct, fg, bar, dim, true);
    std::wstring t = pct < 0 ? L"?" : std::to_wstring(pct);
    return RenderIcon(t, pct, text, bar, dim);
}

// ---- tray management --------------------------------------------------------------------------

void CopyTip(wchar_t* dst, size_t cap, const std::wstring& s) {
    wcsncpy(dst, s.c_str(), cap - 1);
    dst[cap - 1] = 0;
}

// "~40 min", "~1 h 20 min": rounded to 5 minutes, then to 10 above an hour (it is an estimate).
std::wstring ApproxMinutes(int mins) {
    if (mins < 60) return TF(L"~{minutes} min", { { L"minutes", std::to_wstring(std::max(5, (mins + 2) / 5 * 5)) } });
    int h = mins / 60, m = (mins % 60 + 5) / 10 * 10;
    if (m == 60) { h++; m = 0; }
    return m ? TF(L"~{hours} h {minutes} min", { { L"hours", std::to_wstring(h) }, { L"minutes", std::to_wstring(m) } })
             : TF(L"~{hours} h", { { L"hours", std::to_wstring(h) } });
}

// How old the last confirmed reading is, never counting failed polls.
std::wstring FreshnessText(const TrayEntry& e) {
    if (!e.lastReadingTick) return L"";
    ULONGLONG now = GetTickCount64(), age = reading_freshness::AgeMinutes(e.lastReadingTick, now);
    std::wstring ago = age == 0 ? std::wstring(T(L"just now"))
                     : age < 60 ? TF(L"{count} min ago", { { L"count", std::to_wstring(age) } })
                     : age < 1440 ? TF(L"{count} h ago", { { L"count", std::to_wstring(age / 60) } })
                                  : TF(L"{count} d ago", { { L"count", std::to_wstring(age / 1440) } });
    switch (reading_freshness::Classify(e.lastReadingTick, now, g_pollIntervalMs)) {
        case reading_freshness::State::Fresh: return TF(L"Last reading: {age}", { { L"age", ago } });
        case reading_freshness::State::Delayed: return TF(L"Delayed reading: {age}", { { L"age", ago } });
        case reading_freshness::State::Stale: return TF(L"Stale reading: {age}", { { L"age", ago } });
        default: return L"";
    }
}

bool IsStale(const TrayEntry& e) {
    return reading_freshness::Classify(e.lastReadingTick, GetTickCount64(), g_pollIntervalMs) ==
           reading_freshness::State::Stale;
}

std::wstring StatusText(const TrayEntry& e) {
    const auto& s = e.st;
    if (!s.connected) {
        std::wstring off = T(L"off / not connected");
        if (e.lastReadingTick) off += L", " + FreshnessText(e);
        return off;
    }
    std::wstring t;
    if (!s.levelText.empty() && s.percent >= 0) t = T(s.levelText);
    else if (s.percent >= 0) t = std::to_wstring(s.percent) + L"%";
    else if (e.lastPct >= 0) t = TF(L"{percent}% (last known)", { { L"percent", std::to_wstring(e.lastPct) } });
    else t = T(L"unknown");
    if (!s.detail.empty() && s.percent >= 0) t += L" (" + s.detail + L")";
    if (s.full || (s.charging && s.percent >= 100)) t += std::wstring(L", ") + T(L"fully charged");
    else if (int mins = s.charging ? charge::MinutesToFull(e.charge, (int64_t)time(nullptr)) : -1; mins > 0)
        t += std::wstring(L", ") + T(L"charging") + L", " + TF(L"full in {time}", { { L"time", ApproxMinutes(mins) } });
    else if (s.charging) t += std::wstring(L", ") + T(L"charging");
    if (s.asleep) t += std::wstring(L", ") + T(L"asleep");
    if (!s.note.empty() && s.percent < 0) t += std::wstring(L", ") + T(s.note);
    const ChargeRecord& c = e.chargeRecord;
    if (c.since && !s.charging && !s.full) {
        ULONGLONG now = NowSec(), age = now > c.since ? now - c.since : 0;
        if (g_trackSince) t += L", " + TF(L"{time} since charge", { { L"time", Duration(age) } });
        if (g_trackAwake) t += L", " + TF(L"~{time} awake", { { L"time", Duration(c.awakeSec) } });
    }
    if (g_trackEmpty && g_recordHistory && !s.charging && !s.full) {
        std::wstring left = battery_history::TimeToEmpty(s.key);
        if (!left.empty()) t += L", " + TF(L"{time} to empty", { { L"time", left } });
    }
    if (e.lastReadingTick) t += L", " + FreshnessText(e);
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

void ShowTextNotification(const std::wstring& title, const std::wstring& message) {
    // Windows notification balloons need a tray icon. Keep a temporary one long enough for the shell
    // to display the notification, then remove it.
    if (!g_placeholderIcon) g_placeholderIcon = RenderIcon(L"-", -1, Foreground(), Foreground(), true);
    g_alertIconAdded =
        TrayShow(g_alertIconAdded, ALERT_ICON_ID, g_placeholderIcon, L"Taskbar Vitals", title.c_str(), message);
    if (g_alertIconAdded) SetTimer(g_hwnd, TIMER_ALERT_ICON, 60 * 1000, nullptr);
}

void ShowNotification(TrayEntry& e, const std::wstring& title, const std::wstring& message) {
    if (!g_useWidget && e.added) {
        TrayCall(NIM_MODIFY, e.uid, e.icon, DisplayName(e) + L"\n" + StatusText(e), title.c_str(), message);
        return;
    }
    ShowTextNotification(title, message);
}

void ShowBatteryAlert(TrayEntry& e, alerts::LowAlert level) {
    const wchar_t* title = level == alerts::LowAlert::Emergency ? L"Battery almost empty"
                         : level == alerts::LowAlert::Critical  ? L"Battery critically low"
                                                                : L"Low battery";
    const wchar_t* text = level == alerts::LowAlert::Emergency ? L"{name} is at {percent}%. Charge it now."
                                                               : L"{name} is at {percent}%";
    ShowNotification(e, T(title), TF(text, { { L"name", DisplayName(e) }, { L"percent", std::to_wstring(e.st.percent) } }));
}

void ShowChargedAlert(TrayEntry& e) {
    ShowNotification(e, T(L"Fully charged"), TF(L"{name} is fully charged.", { { L"name", DisplayName(e) } }));
}

void ShowChargeLimitAlert(TrayEntry& e) {
    ShowNotification(e, T(L"You can unplug it"),
                     TF(L"{name} is charged to {percent}%. Stopping around here helps the battery last longer.",
                        { { L"name", DisplayName(e) }, { L"percent", std::to_wstring(e.st.percent) } }));
}

// The time-to-empty estimate says the device will not last the next 12 hours: once per discharge.
void ShowShortRuntimeAlert(TrayEntry& e, const std::wstring& left) {
    ShowNotification(e, T(L"Charge today"), TF(L"{name} may run out in {time}", { { L"name", DisplayName(e) }, { L"time", left } }));
}

void UpdatePlaceholder() {
    if (g_useWidget && !g_emptyToTray) {
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
        return;
    }
    bool any = false;
    for (auto& [k, e] : g_entries) any |= e.added;
    for (const auto& s : g_sysTray) any |= s.added;
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
    bool dim = ShouldDim(s) || IsStale(e);
    // Fully charged on power: the normal green bar instead of the blue charging one.
    bool charging = s.connected && !s.asleep && s.charging && !s.full && shown < 100;
    Accent accent = AccentFor(s);
    std::wstring sig = std::to_wstring(shown) + (charging ? L"c" : L"") + (dim ? L"d" : L"") + (g_lightTaskbar ? L"l" : L"k") +
                       L"a" + std::to_wstring((int)accent) + (s.connected ? L"|on" : L"|off") +
                       (g_trayDeviceIcons ? L"|g" + std::to_wstring((int)s.kind) + (s.via == L"Bluetooth" ? L"b" : L"") : L"");
    if (sig != e.iconSig || !e.icon) {
        if (e.icon) DestroyIcon(e.icon);
        e.icon = s.connected ? IconFor(shown, charging, dim, accent,
                                       g_trayDeviceIcons ? widget::DeviceGlyph(s.kind, s.via == L"Bluetooth") : 0)
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

// Why there is no CPU temperature on the strip (for the tooltip while it is on).
std::wstring CpuTempStatusText() {
    if (!g_cpuTemp.running) return T(L"starting the helper...");
    switch (g_cpuTemp.status) {
        case cputemp::Status::NoPawnIo: return T(L"the PawnIO driver (pawnio.eu) was not found");
        case cputemp::Status::Unsupported: return T(L"this CPU gives no reading (Intel & AMD Ryzen 17h-1Ah)");
        case cputemp::Status::ModuleFailed:
        case cputemp::Status::ReadFailed: return T(L"the driver could not read it");
        default: return T(L"waiting for the first reading...");
    }
}

std::wstring JoinParts(const std::vector<std::wstring>& parts, const wchar_t* sep) {
    std::wstring out;
    for (const auto& p : parts) {
        if (!out.empty()) out += sep;
        out += p;
    }
    return out;
}

std::wstring FormatGbPair(int pct, double usedGb, double totalGb) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%d%% (%.1f / %.1f GB)", pct, usedGb, totalGb);
    return buf;
}

void UpdateSystemTrayIcons() {
    if (g_useWidget) {
        for (int item = 0; item < 7; ++item) {
            if (g_sysTray[item].added) {
                TrayDelete(SYS_TRAY_UID_BASE + item);
                g_sysTray[item].added = false;
            }
        }
        return;
    }
    for (int item = 0; item < 7; ++item) {
        bool active = false;
        bool charging = false;
        bool low = false;
        std::wstring text, tip;
        int barPct = -1, heat = 0;
        if (item == 0 && (g_showCpu || g_showCpuW || g_showCpuTemp)) {
            active = true;
            int cpuUse = g_showCpu ? g_sys.cpu : -1;
            int cpuW = g_showCpuW ? g_sys.cpuWatts : -1;
            int cpuTemp = g_showCpuTemp ? g_cpuTemp.tempC : -1;
            if (cpuUse >= 0) { text = std::to_wstring(cpuUse); barPct = cpuUse; }
            else if (cpuTemp >= 0) { text = std::to_wstring(cpuTemp) + L"°"; barPct = std::clamp(cpuTemp, 0, 100); }
            else if (cpuW >= 0) { text = std::to_wstring(cpuW) + L"W"; }
            else { text = L"?"; }
            heat = sysmon::HeatLevel(cpuTemp);
            std::vector<std::wstring> parts;
            if (cpuUse >= 0) parts.push_back(std::to_wstring(cpuUse) + L"%");
            if (cpuTemp >= 0) parts.push_back(std::to_wstring(cpuTemp) + L" °C");
            if (cpuW >= 0) parts.push_back(std::to_wstring(cpuW) + L" W");
            tip = parts.empty() && g_showCpuTemp ? std::wstring(T(L"CPU temperature")) + L": " + CpuTempStatusText()
                                                 : std::wstring(T(L"CPU")) + L": " + (parts.empty() ? L"..." : JoinParts(parts, L", "));
        } else if (item == 1 && g_showRam) {
            active = true;
            if (g_sys.ram >= 0) {
                text = std::to_wstring(g_sys.ram);
                barPct = g_sys.ram;
                tip = std::wstring(T(L"RAM usage")) + L": " + FormatGbPair(g_sys.ram, g_sys.ramUsedGb, g_sys.ramTotalGb);
            } else {
                text = L"?";
                tip = std::wstring(T(L"RAM usage")) + L": ...";
            }
        } else if (item == 2 && (g_showGpu || g_showGpuTemp || g_showGpuClocks || g_showGpuW) && g_gpu.available) {
            active = true;
            int gpuUse = g_showGpu ? g_gpu.use : -1;
            int gpuTemp = g_showGpuTemp ? g_gpu.tempC : -1;
            int gpuW = g_showGpuW ? g_gpu.watts : -1;
            int gpuClk = g_showGpuClocks ? g_gpu.gpuClockMhz : -1;
            int memClk = g_showGpuClocks ? g_gpu.vramClockMhz : -1;
            if (gpuUse >= 0) { text = std::to_wstring(gpuUse); barPct = gpuUse; }
            else if (gpuTemp >= 0) { text = std::to_wstring(gpuTemp) + L"°"; barPct = std::clamp(gpuTemp, 0, 100); }
            else if (gpuW >= 0) { text = std::to_wstring(gpuW) + L"W"; }
            else if (gpuClk >= 0) { text = std::to_wstring(gpuClk); }
            else if (memClk >= 0) { text = std::to_wstring(memClk); }
            else { text = L"?"; }
            heat = sysmon::HeatLevel(gpuTemp);
            std::vector<std::wstring> parts;
            if (gpuUse >= 0) parts.push_back(std::to_wstring(gpuUse) + L"%");
            if (gpuTemp >= 0) parts.push_back(std::to_wstring(gpuTemp) + L" °C");
            if (gpuW >= 0) parts.push_back(std::to_wstring(gpuW) + L" W");
            if (g_showGpuClocks) {
                std::wstring clkStr = gpumon::FormatClocksMhz(gpuClk, memClk);
                if (!clkStr.empty()) parts.push_back(clkStr);
            }
            tip = L"GPU (" + g_gpu.name + L"): " + (parts.empty() ? L"..." : JoinParts(parts, L", "));
        } else if (item == 3 && g_showVram && g_gpu.available && g_gpu.vram >= 0) {
            active = true;
            text = std::to_wstring(g_gpu.vram);
            barPct = g_gpu.vram;
            tip = std::wstring(T(L"GPU memory")) + L": " + FormatGbPair(g_gpu.vram, g_gpu.vramUsedGb, g_gpu.vramTotalGb);
        } else if (item == 4 && AnyDiskReading() && g_disk.available) {
            active = true;
            int diskUse = g_showDiskUse ? g_disk.usePct : -1;
            int diskTemp = g_showDiskTemp ? g_disk.tempC : -1;
            if (diskUse >= 0) { text = std::to_wstring(diskUse); barPct = diskUse; }
            else if (diskTemp >= 0) { text = std::to_wstring(diskTemp) + L"°"; barPct = std::clamp(diskTemp, 0, 100); }
            else { text = L"?"; }
            heat = diskmon::HeatLevel(diskTemp);
            std::vector<std::wstring> parts;
            if (diskUse >= 0) parts.push_back(std::to_wstring(diskUse) + L"%");
            if (diskTemp >= 0) parts.push_back(std::to_wstring(diskTemp) + L" °C");
            if (g_disk.totalGb > 0.0)
                parts.push_back(diskmon::FormatCapacityGb(g_disk.freeGb) + L" / " + diskmon::FormatCapacityGb(g_disk.totalGb));
            tip = std::wstring(T(L"Storage")) + (!g_disk.driveLetter.empty() ? L" (" + g_disk.driveLetter + L")" : L"") +
                  L": " + (parts.empty() ? L"..." : JoinParts(parts, L", "));
        } else if (item == 5 && g_showNetSpeed) {
            active = true;
            double down = g_net.downBytesPerSec, up = g_net.upBytesPerSec;
            double peak = std::max(down, up);
            text = (g_net.available && peak >= 0.0) ? netmon::FormatRateCompact(peak) : L"?";
            tip = std::wstring(T(L"Network")) + L": \u2193 " + netmon::FormatRateFull(down) +
                  L", \u2191 " + netmon::FormatRateFull(up);
            auto st = netmon::GetSpeedTest();
            if (st.state == netmon::TestState::Done)
                tip += L" (" + netmon::FormatMbps(st.downMbps) + L" / " + netmon::FormatMbps(st.upMbps) + L")";
        } else if (item == 6 && AnyLaptopBatReading() && g_laptopBat.present) {
            int pct = g_showLaptopBat ? g_laptopBat.percent : -1;
            bool showW = g_showLaptopBatW && g_laptopBat.hasWatts;
            if (pct >= 0 || showW) {
                active = true;
                charging = g_laptopBat.charging;
                low = !charging && pct >= 0 && pct <= LOW_BATTERY;
                if (pct >= 0) { text = std::to_wstring(pct); barPct = pct; }
                else { text = laptopbat::FormatWattsCompact(g_laptopBat.watts); }
                std::vector<std::wstring> parts;
                if (pct >= 0) parts.push_back(std::to_wstring(pct) + L"%");
                if (showW) parts.push_back(laptopbat::FormatWattsDetailed(g_laptopBat.watts));
                if (charging) parts.push_back(T(L"Charging ⚡"));
                else if (g_laptopBat.acOnline) parts.push_back(T(L"Plugged in"));
                else parts.push_back(T(L"On battery"));
                if (!charging && g_laptopBat.remainingMinutes > 0)
                    parts.push_back(laptopbat::FormatRemaining(g_laptopBat.remainingMinutes));
                tip = std::wstring(T(L"Laptop battery")) + L": " + JoinParts(parts, L", ");
            }
        }
        auto& slot = g_sysTray[item];
        if (!active) {
            if (slot.added) {
                TrayDelete(SYS_TRAY_UID_BASE + item);
                slot.added = false;
            }
            continue;
        }
        Accent ac = SystemAccentFor(item);
        std::wstring sig = text + L"|" + std::to_wstring(barPct) + L"|" + std::to_wstring(heat) + L"|" +
                           std::to_wstring((int)ac) + (charging ? L"|c" : L"|d") + (low ? L"l" : L"") +
                           (g_lightTaskbar ? L"|l" : L"|k");
        if (sig != slot.sig || !slot.icon) {
            if (slot.icon) DestroyIcon(slot.icon);
            Rgb tc = heat >= 2 || low ? Rgb{ 240, 50, 60 } : heat == 1 ? Rgb{ 255, 160, 40 } : Foreground();
            BrandColor bc = AccentColor(ac, g_lightTaskbar);
            Rgb bar = heat >= 2 || low ? Rgb{ 232, 32, 48 }
                    : heat == 1 ? Rgb{ 255, 150, 0 }
                    : charging ? Rgb{ 72, 160, 255 }
                    : ac != Accent::None ? Rgb{ bc.r, bc.g, bc.b }
                                         : Rgb{ 30, 190, 90 };
            slot.icon = RenderIcon(text, barPct, tc, bar, charging);
            slot.sig = sig;
        }
        slot.added = TrayShow(slot.added, SYS_TRAY_UID_BASE + item, slot.icon, tip);
    }
}

StatusDevice ToStatus(const DeviceStatus& s, int lastPct) {
    StatusDevice d;
    d.key = s.key;
    d.name = s.name;
    d.via = s.via;
    d.detail = s.detail;
    d.levelText = s.levelText;
    d.kind = s.kind;
    d.percent = s.percent;
    d.lastPct = lastPct;
    d.millivolts = s.millivolts;
    d.charging = s.charging;
    d.asleep = s.asleep;
    d.connected = s.connected;
    return d;
}

StatusVitals BuildStatusVitals() {
    StatusVitals v;
    v.included = AnyReading();
    if (!v.included) return v;
    v.cpuPercent = g_showCpu ? g_sys.cpu : -1;
    v.cpuTempC = g_showCpuTemp ? g_cpuTemp.tempC : -1;
    v.cpuWatts = g_showCpuW ? g_sys.cpuWatts : -1;
    v.ramPercent = g_showRam ? g_sys.ram : -1;
    v.ramUsedGb = g_showRam ? g_sys.ramUsedGb : 0.0;
    v.ramTotalGb = g_showRam ? g_sys.ramTotalGb : 0.0;
    v.gpuAvailable = AnyGpuReading() && g_gpu.available;
    if (v.gpuAvailable) {
        v.gpuName = g_gpu.name;
        v.gpuPercent = g_showGpu ? g_gpu.use : -1;
        v.gpuTempC = g_showGpuTemp ? g_gpu.tempC : -1;
        v.gpuWatts = g_showGpuW ? g_gpu.watts : -1;
        v.gpuClockMhz = g_showGpuClocks ? g_gpu.gpuClockMhz : -1;
        v.vramClockMhz = g_showGpuClocks ? g_gpu.vramClockMhz : -1;
        v.vramPercent = g_showVram ? g_gpu.vram : -1;
        v.vramUsedGb = g_showVram ? g_gpu.vramUsedGb : 0.0;
        v.vramTotalGb = g_showVram ? g_gpu.vramTotalGb : 0.0;
    }
    v.diskAvailable = AnyDiskReading() && g_disk.available;
    if (v.diskAvailable) {
        v.diskName = g_disk.name;
        v.diskDrive = g_disk.driveLetter;
        v.diskTempC = g_showDiskTemp ? g_disk.tempC : -1;
        v.diskPercent = g_showDiskUse ? g_disk.usePct : -1;
        v.diskReadMBs = g_showDiskUse ? g_disk.readMBs : 0.0;
        v.diskWriteMBs = g_showDiskUse ? g_disk.writeMBs : 0.0;
        v.diskFreeGb = g_disk.freeGb;
        v.diskTotalGb = g_disk.totalGb;
    }
    v.netAvailable = g_showNetSpeed && g_net.available;
    if (v.netAvailable) {
        v.netDownBytesPerSec = g_net.downBytesPerSec;
        v.netUpBytesPerSec = g_net.upBytesPerSec;
    }
    if (g_showNetSpeed) {
        auto st = netmon::GetSpeedTest();
        if (st.state == netmon::TestState::Done) {
            v.netSpeedTestDownMbps = st.downMbps;
            v.netSpeedTestUpMbps = st.upMbps;
        }
    }
    v.laptopBatteryPresent = AnyLaptopBatReading() && g_laptopBat.present;
    if (v.laptopBatteryPresent) {
        v.laptopBatteryPercent = g_showLaptopBat ? g_laptopBat.percent : -1;
        v.laptopBatteryCharging = g_laptopBat.charging;
        v.laptopBatteryAcOnline = g_laptopBat.acOnline;
        v.laptopBatteryHasWatts = g_showLaptopBatW && g_laptopBat.hasWatts;
        v.laptopBatteryWatts = g_laptopBat.watts;
        v.laptopBatteryRemainingMinutes = g_laptopBat.remainingMinutes;
    }
    return v;
}

void UpdateStatusSnapshot() {
    if (g_demo) return;
    std::vector<StatusDevice> devices;
    for (auto* e : OrderedEntries(true)) devices.push_back(ToStatus(e->st, e->lastPct));
    StatusVitals vitals = BuildStatusVitals();
    WriteStatusSnapshot(devices, "snapshot", LOW_BATTERY, vitals.included ? &vitals : nullptr);
}

bool BatOnLeftSide() { return g_farLeft || g_beforeApps; }
bool VitOnLeftSide() { return g_vitalsFarLeft || g_vitalsBeforeApps; }

bool SplitStrips() {
    return g_useWidget && widget::Alive() && AnyReading() &&
           (g_farLeft != g_vitalsFarLeft || g_beforeApps != g_vitalsBeforeApps) &&
           !widget::Vertical() && !g_myFinder;
}

void ApplyStripOptions() {
    widget::SetMyFinder(g_myFinder);
    widget::SetBackground(g_widgetBackground);
    widget::SetStacked(g_stacked);
    widget::SetTwoRows(g_twoRows);
    widget::SetCustomIcons(g_customIcons);
    widget::SetShowPercentSign(g_showPercentSign);
    widget::SetRingStyle(g_ringStyle);
    widget::SetConnectionIcons(g_connectionIcons);
    widget::SetAnimateCharging(g_animateCharging);
    widget::SetCardOptions(g_hoverCard, g_hoverChart);
    widget::SetMakeRoom(g_makeRoom);
    widget::SetVerticalPlace(g_verticalPlace);
    widget::SetMonitorChoice(g_widgetMonitor);
    widget::SetFarLeft(g_farLeft);
    widget::SetBeforeApps(g_beforeApps);
    widget::SetFarLeftOnly(BatOnLeftSide() && vitals_strip::Alive());

    if (vitals_strip::Alive()) {
        vitals_strip::SetMyFinder(false);
        vitals_strip::SetBackground(g_widgetBackground);
        vitals_strip::SetStacked(g_stacked);
        vitals_strip::SetTwoRows(g_twoRows);
        vitals_strip::SetCustomIcons(g_customIcons);
        vitals_strip::SetShowPercentSign(g_showPercentSign);
        vitals_strip::SetRingStyle(g_ringStyle);
        vitals_strip::SetConnectionIcons(false);
        vitals_strip::SetAnimateCharging(false);
        vitals_strip::SetCardOptions(g_hoverCard, g_hoverChart);
        vitals_strip::SetMakeRoom(g_makeRoom);
        vitals_strip::SetMonitorChoice(g_widgetMonitor);
        vitals_strip::SetFarLeft(g_vitalsFarLeft);
        vitals_strip::SetBeforeApps(g_vitalsBeforeApps);
        vitals_strip::SetFarLeftOnly(VitOnLeftSide());
    }
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
        else if (e->st.percent >= 0) {
            if (g_runtimeWidget && g_recordHistory && !e->st.asleep && !e->st.charging && !e->st.full)
                it.text = battery_history::EstimatedRuntimeText(e->st.key);
            if (it.text.empty()) it.text = T(e->st.levelText);
        }
        it.charging = !offline && !e->st.asleep && e->st.charging;
        it.full = !offline && (e->st.full || (e->st.charging && it.percent >= 100));
        it.dim = ShouldDim(e->st) || IsStale(*e);
        it.low = !offline && !it.charging && it.percent >= 0 && it.percent <= LOW_BATTERY;
        it.bluetooth = e->st.via == L"Bluetooth";
        it.wirelessLink = !offline && e->st.via.rfind(L"2.4 GHz", 0) == 0;
        it.vectorIcon = vector_icons::ForDevice(e->st.kind, it.bluetooth);
        it.label = labels[items.size()];
        it.accent = AccentFor(e->st);
        it.name = DisplayName(*e);
        {
            std::vector<std::wstring> parts;
            if (offline) parts.push_back(T(L"off / not connected"));
            else {
                if (!e->st.detail.empty() && e->st.percent >= 0) parts.push_back(e->st.detail);
                if (it.full) parts.push_back(T(L"fully charged"));
                else if (it.charging) parts.push_back(T(L"charging"));
                if (e->st.asleep) parts.push_back(T(L"asleep"));
            }
            for (const auto& p : parts) it.status += (it.status.empty() ? L"" : L", ") + p;
            if (!it.status.empty()) it.status[0] = towupper(it.status[0]);
        }
        if (!offline && !e->st.charging) {
            it.hoursLeft = battery_history::HoursRemaining(e->st.key);
            it.hoursLeftText = battery_history::RemainingText(e->st.key);
            if (it.hoursLeft < 0) {
                it.hoursLeft = battery_history::RoughHoursRemaining(e->st.key);
                if (it.hoursLeft >= 0)
                    it.hoursLeftText = TF(L"{time} (approx.)", { { L"time", battery_history::FormatHours(it.hoursLeft) } });
            }
            if (!it.hoursLeftText.empty()) it.info.push_back(std::wstring(T(L"Estimated time to empty")) + L": " + it.hoursLeftText);
        }
        if (!e->st.via.empty() && e->st.via != L"demo") it.info.push_back(T(e->st.via));
        if (g_hoverCard && g_hoverChart && g_recordHistory)
            for (const auto& pt : battery_history::Recent(e->st.key, 24)) it.trend.push_back({ pt.secondsAgo, pt.percent });
        items.push_back(it);
        if (!tip.empty()) tip += L"\n";
        tip += DisplayName(*e) + L": " + StatusText(*e);
    }
    if (tip.empty()) tip = std::wstring(L"Taskbar Vitals: ") + T(L"No devices with battery info found");

    // Hardware Vitals items: CPU, RAM, GPU, VRAM, Storage, Network.
    std::vector<WidgetItem> readings;
    std::vector<int> readingKeys;
    std::vector<std::wstring> readingTips;
    const ULONGLONG nowTick = GetTickCount64();

    auto attachVitalChart = [&](WidgetItem& it, const std::deque<VitalPoint>& hist, int maxVal = 100) {
        if (!g_hoverCard || !g_hoverChart) return;
        it.trend = BuildVitalTrend(hist, nowTick);
        if (it.trend.empty()) return;
        it.trendSpanSec = 180;
        it.trendMax = maxVal;
        it.trendTitle = T(L"Last 3 minutes");
        it.trendAxisLeft = T(L"-3m");
    };

    auto addReading = [&](int key, wchar_t glyph, vector_icons::VectorIcon vecIcon, int pct,
                          const std::wstring& line, const std::wstring& stripExtra,
                          const std::wstring& cardName, const std::wstring& cardStatus,
                          const std::vector<std::wstring>& cardInfo,
                          const std::wstring& caption = L"", const std::wstring& customText = L"") {
        WidgetItem it;
        it.isVital = true;
        it.glyph = glyph;
        it.vectorIcon = vecIcon;
        it.percent = pct;
        it.accent = SystemAccentFor(key);
        if (!customText.empty()) {
            it.text = customText;
        } else {
            it.text = (pct >= 0 ? std::to_wstring(pct) + L"%" : std::wstring()) +
                      (pct >= 0 && !stripExtra.empty() ? L" " : L"") + stripExtra;
        }
        it.label = caption;
        it.name = cardName;
        it.status = cardStatus;
        it.info = cardInfo;
        readings.push_back(std::move(it));
        readingKeys.push_back(key);
        readingTips.push_back(line);
    };

    // 0: CPU
    int cpuUse = g_showCpu ? g_sys.cpu : -1;
    int cpuW = g_showCpuW ? g_sys.cpuWatts : -1;
    int cpuTemp = g_showCpuTemp ? g_cpuTemp.tempC : -1;
    if (cpuUse >= 0 || cpuW >= 0 || cpuTemp >= 0) {
        std::wstring extra = (cpuTemp >= 0 ? std::to_wstring(cpuTemp) + L"°" : L"") +
                             (cpuW >= 0 ? (cpuTemp >= 0 ? L" " : L"") + std::to_wstring(cpuW) + L"W" : L"");
        std::vector<std::wstring> parts;
        if (cpuUse >= 0) parts.push_back(std::to_wstring(cpuUse) + L"%");
        if (cpuTemp >= 0) parts.push_back(std::to_wstring(cpuTemp) + L" °C");
        if (cpuW >= 0) parts.push_back(std::to_wstring(cpuW) + L" W");
        std::wstring joined = JoinParts(parts, L", ");
        addReading(0, 0xE950, vector_icons::VectorIcon::Cpu, cpuUse,
                   std::wstring(T(L"CPU")) + L": " + joined, extra,
                   T(L"CPU"), JoinParts(parts, L" · "), {});
        readings.back().heat = sysmon::HeatLevel(cpuTemp);
        attachVitalChart(readings.back(), g_cpuHist);
    } else if (g_showCpuTemp) {
        addReading(0, 0xE950, vector_icons::VectorIcon::Cpu, -1,
                   std::wstring(T(L"CPU temperature")) + L": " + CpuTempStatusText(), L"?",
                   T(L"CPU temperature"), CpuTempStatusText(), {});
    }

    // 1: RAM
    if (g_showRam && g_sys.ram >= 0) {
        std::wstring status = FormatGbPair(g_sys.ram, g_sys.ramUsedGb, g_sys.ramTotalGb);
        addReading(1, 0xE964, vector_icons::VectorIcon::Ram, g_sys.ram,
                   std::wstring(T(L"RAM usage")) + L": " + status, L"",
                   T(L"RAM usage"), status, {});
        attachVitalChart(readings.back(), g_ramHist);
    }

    // 2: GPU
    int gpuUse = g_showGpu ? g_gpu.use : -1;
    int gpuTemp = g_showGpuTemp ? g_gpu.tempC : -1;
    int gpuW = g_showGpuW ? g_gpu.watts : -1;
    int gpuClk = g_showGpuClocks ? g_gpu.gpuClockMhz : -1;
    int vramClk = g_showGpuClocks ? g_gpu.vramClockMhz : -1;
    if (g_gpu.available && (gpuUse >= 0 || gpuTemp >= 0 || gpuW >= 0 || gpuClk >= 0 || vramClk >= 0)) {
        std::wstring clkShort;
        if (gpuClk >= 0) clkShort = std::to_wstring(gpuClk) + L"MHz";
        else if (vramClk >= 0) clkShort = std::to_wstring(vramClk) + L"MHz";
        std::wstring extra = (gpuTemp >= 0 ? std::to_wstring(gpuTemp) + L"°" : L"") +
                             (gpuW >= 0 ? (gpuTemp >= 0 ? L" " : L"") + std::to_wstring(gpuW) + L"W" : L"") +
                             (!clkShort.empty() ? ((gpuTemp >= 0 || gpuW >= 0) ? L" " : L"") + clkShort : L"");
        std::vector<std::wstring> parts;
        if (gpuUse >= 0) parts.push_back(std::to_wstring(gpuUse) + L"%");
        if (gpuTemp >= 0) parts.push_back(std::to_wstring(gpuTemp) + L" °C");
        if (gpuW >= 0) parts.push_back(std::to_wstring(gpuW) + L" W");
        std::wstring cardStatus = JoinParts(parts, L" · ");
        std::wstring clkFull = g_showGpuClocks ? gpumon::FormatClocksMhz(gpuClk, vramClk) : L"";
        if (!clkFull.empty()) parts.push_back(clkFull);
        if (cardStatus.empty()) cardStatus = clkFull;
        std::vector<std::wstring> info;
        if (!g_gpu.name.empty()) info.push_back(g_gpu.name);
        if (!clkFull.empty() && cardStatus != clkFull) info.push_back(std::wstring(T(L"Clocks")) + L": " + clkFull);
        addReading(2, 0xE7F4, vector_icons::VectorIcon::Gpu, gpuUse,
                   L"GPU (" + g_gpu.name + L"): " + JoinParts(parts, L", "), extra,
                   L"GPU", cardStatus, info);
        readings.back().heat = sysmon::HeatLevel(gpuTemp);
        attachVitalChart(readings.back(), g_gpuHist);
    }

    // 3: GPU Memory (VRAM)
    if (g_showVram && g_gpu.available && g_gpu.vram >= 0) {
        std::wstring status = FormatGbPair(g_gpu.vram, g_gpu.vramUsedGb, g_gpu.vramTotalGb);
        std::vector<std::wstring> info;
        if (!g_gpu.name.empty()) info.push_back(g_gpu.name);
        addReading(3, 0xE964, vector_icons::VectorIcon::Vram, g_gpu.vram,
                   std::wstring(T(L"GPU memory")) + L": " + status, L"",
                   T(L"GPU memory"), status, info, L"GPU");
        attachVitalChart(readings.back(), g_vramHist);
    }

    // 4: Storage / SSD / HDD
    int diskUse = g_showDiskUse ? g_disk.usePct : -1;
    int diskTemp = g_showDiskTemp ? g_disk.tempC : -1;
    if (AnyDiskReading() && g_disk.available && (diskUse >= 0 || diskTemp >= 0)) {
        std::wstring extra = diskTemp >= 0 ? std::to_wstring(diskTemp) + L"°" : L"";
        std::vector<std::wstring> parts;
        if (diskUse >= 0) parts.push_back(std::to_wstring(diskUse) + L"%");
        if (diskTemp >= 0) parts.push_back(std::to_wstring(diskTemp) + L" °C");
        std::vector<std::wstring> info;
        if (!g_disk.name.empty()) info.push_back(g_disk.name);
        if (g_showDiskUse && (g_disk.readMBs >= 0.05 || g_disk.writeMBs >= 0.05)) {
            wchar_t rw[64]{};
            swprintf(rw, 64, L"R: %.1f · W: %.1f MB/s", g_disk.readMBs, g_disk.writeMBs);
            info.push_back(rw);
        }
        if (g_disk.totalGb > 0.0) {
            info.push_back(TF(L"{free} free of {total}",
                              { { L"free", diskmon::FormatCapacityGb(g_disk.freeGb) },
                                { L"total", diskmon::FormatCapacityGb(g_disk.totalGb) } }));
        }
        std::wstring cardTitle = !g_disk.driveLetter.empty()
                                     ? std::wstring(T(L"Storage")) + L" (" + g_disk.driveLetter + L")"
                                     : std::wstring(T(L"Storage"));
        addReading(4, 0xEDA2, vector_icons::VectorIcon::Disk, diskUse,
                   cardTitle + L": " + JoinParts(parts, L", "), extra,
                   cardTitle, JoinParts(parts, L" · "), info);
        readings.back().heat = diskmon::HeatLevel(diskTemp);
        attachVitalChart(readings.back(), g_diskHist);
    } else if (AnyDiskReading()) {
        addReading(4, 0xEDA2, vector_icons::VectorIcon::Disk, -1,
                   std::wstring(T(L"Storage")) + L": ...", L"?",
                   T(L"Storage"), L"...", {});
    }

    // 5: Network
    if (g_showNetSpeed) {
        double down = g_net.downBytesPerSec, up = g_net.upBytesPerSec;
        std::wstring compact = (g_net.available && down >= 0.0 && up >= 0.0)
                                   ? (L"\u2193" + netmon::FormatRateCompact(down) + L" \u2191" + netmon::FormatRateCompact(up))
                                   : L"...";
        std::wstring status = L"\u2193 " + netmon::FormatRateFull(down) + L"  \u00B7  \u2191 " + netmon::FormatRateFull(up);
        std::vector<std::wstring> info;
        auto st = netmon::GetSpeedTest();
        if (st.state == netmon::TestState::Running) {
            info.push_back(T(L"Running speed test..."));
        } else if (st.state == netmon::TestState::Done) {
            info.push_back(TF(L"Speed test: \u2193 {down} \u00B7 \u2191 {up}",
                              { { L"down", netmon::FormatMbps(st.downMbps) },
                                { L"up", netmon::FormatMbps(st.upMbps) } }));
        }
        int peakKbps = 100;
        for (const auto& pt : g_netHist)
            if (nowTick >= pt.tickMs && nowTick - pt.tickMs <= kVitalHistoryMs)
                peakKbps = std::max(peakKbps, pt.value);
        addReading(5, 0xE839, vector_icons::VectorIcon::Network, -1,
                   std::wstring(T(L"Network")) + L": " + status, L"",
                   T(L"Network"), status, info, L"", compact);
        attachVitalChart(readings.back(), g_netHist, std::max(100, peakKbps * 5 / 4));
    }

    // 6: Laptop / internal battery
    if (AnyLaptopBatReading() && g_laptopBat.present) {
        int pct = g_showLaptopBat ? g_laptopBat.percent : -1;
        bool showW = g_showLaptopBatW && g_laptopBat.hasWatts;
        if (pct >= 0 || showW) {
            std::wstring wattsShort = showW ? laptopbat::FormatWattsCompact(g_laptopBat.watts) : L"";
            std::wstring wattsLong = showW ? laptopbat::FormatWattsDetailed(g_laptopBat.watts) : L"";
            std::vector<std::wstring> parts;
            if (pct >= 0) parts.push_back(std::to_wstring(pct) + L"%");
            if (!wattsLong.empty()) parts.push_back(wattsLong);
            if (g_laptopBat.charging) parts.push_back(T(L"Charging ⚡"));
            else if (g_laptopBat.acOnline) parts.push_back(T(L"Plugged in"));
            else parts.push_back(T(L"On battery"));
            std::wstring remStr = (!g_laptopBat.charging && g_laptopBat.remainingMinutes > 0)
                                      ? laptopbat::FormatRemaining(g_laptopBat.remainingMinutes)
                                      : L"";
            if (!remStr.empty()) parts.push_back(remStr);
            std::vector<std::wstring> info;
            if (g_laptopBat.charging) info.push_back(T(L"Charging ⚡"));
            else if (g_laptopBat.acOnline) info.push_back(T(L"Plugged in"));
            else info.push_back(T(L"On battery"));
            if (!remStr.empty()) info.push_back(TF(L"Remaining: {time}", { { L"time", remStr } }));
            if (g_laptopBat.remainingMwh > 0 && g_laptopBat.maxMwh > 0) {
                info.push_back(std::to_wstring(g_laptopBat.remainingMwh) + L" / " +
                               std::to_wstring(g_laptopBat.maxMwh) + L" mWh");
            }
            std::wstring status = (pct >= 0 ? std::to_wstring(pct) + L"%" : L"") +
                                  (!wattsLong.empty() ? (pct >= 0 ? L" · " : L"") + wattsLong : L"");
            if (status.empty()) status = L"...";
            std::wstring custom = pct < 0 ? wattsShort : L"";
            addReading(6, 0xEBA0, vector_icons::VectorIcon::LaptopBattery, pct,
                       std::wstring(T(L"Laptop battery")) + L": " + JoinParts(parts, L", "),
                       pct >= 0 ? wattsShort : L"",
                       T(L"Laptop battery"), status, info, L"", custom);
            readings.back().charging = g_laptopBat.charging;
            readings.back().low = !g_laptopBat.charging && pct >= 0 && pct <= LOW_BATTERY;
            attachVitalChart(readings.back(), g_batHist);
        }
    }

    std::vector<size_t> readingSort(readings.size());
    for (size_t i = 0; i < readingSort.size(); ++i) readingSort[i] = i;
    auto orderRank = [&](int key) {
        auto it = std::find(g_systemOrder.begin(), g_systemOrder.end(), key);
        return it == g_systemOrder.end() ? g_systemOrder.size() : size_t(it - g_systemOrder.begin());
    };
    std::stable_sort(readingSort.begin(), readingSort.end(), [&](size_t a, size_t b) {
        return orderRank(readingKeys[a]) < orderRank(readingKeys[b]);
    });
    std::vector<WidgetItem> sortedReadings;
    std::wstring readingsTip;
    for (size_t i : readingSort) {
        sortedReadings.push_back(readings[i]);
        if (!readingsTip.empty()) readingsTip += L"\n";
        readingsTip += readingTips[i];
    }
    readings = std::move(sortedReadings);
    std::vector<WidgetItem> all;
    std::wstring allTip;
    if (g_vitalsFirst) {
        all = readings;
        all.insert(all.end(), items.begin(), items.end());
        allTip = readingsTip.empty() ? tip : (items.empty() ? readingsTip : readingsTip + L"\n" + tip);
    } else {
        all = items;
        all.insert(all.end(), readings.begin(), readings.end());
        allTip = readingsTip.empty() ? tip : tip + L"\n" + readingsTip;
    }
    if (!vitals_strip::Alive()) {
        widget::SetItems(all, allTip);
        UpdateStatusSnapshot();
        return;
    }
    // Two strips. The one on the left side hides when there is no room there; the other then shows everything.
    g_leftEndPlaced = BatOnLeftSide() ? widget::AtFarLeft() : vitals_strip::AtFarLeft();
    bool batteriesAlone = BatOnLeftSide() || g_leftEndPlaced;
    bool readingsAlone = !BatOnLeftSide() || g_leftEndPlaced;
    widget::SetItems(batteriesAlone ? items : all, batteriesAlone ? tip : allTip);
    vitals_strip::SetItems(readingsAlone ? readings : all, readingsAlone ? readingsTip : allTip);
    UpdateStatusSnapshot();
}

// Creates, keeps or removes the readings strip; called from Reconcile (once a second and after changes).
void SyncVitalsStrip() {
    bool want = SplitStrips(), alive = vitals_strip::Alive();
    if (want && !alive) {
        vitals_strip::Create(g_inst, g_hwnd, WM_WIDGET_CLICK, g_lightTaskbar);
        ApplyStripOptions();
        UpdateWidget();
    } else if (!want && alive) {
        vitals_strip::Destroy();
        widget::SetFarLeftOnly(false);
        UpdateWidget();
    } else if (want) {
        vitals_strip::Reposition();
        bool placed = BatOnLeftSide() ? widget::AtFarLeft() : vitals_strip::AtFarLeft();
        if (placed != g_leftEndPlaced) UpdateWidget();  // room at the left end came or went
    }
}

void Reconcile();

struct IdleReading { int pct; ULONGLONG at; };
std::map<std::wstring, IdleReading> g_idlePct;
constexpr ULONGLONG kIdlePctMaxAgeMs = 12ull * 60 * 60 * 1000;

void SmoothCharging(DeviceStatus& s) {
    if (s.percent < 0 || s.name.empty() || s.name.find(L"wireless device") != std::wstring::npos) return;
    ULONGLONG now = GetTickCount64();
    if (!s.charging) {
        g_idlePct[s.name] = { s.percent, now };
        return;
    }
    auto it = g_idlePct.find(s.name);
    if (it == g_idlePct.end() || now - it->second.at > kIdlePctMaxAgeMs) return;
    if (s.percent < it->second.pct) {
        s.percent = it->second.pct;
        s.levelText.clear();
    } else {
        g_idlePct.erase(it);
    }
}

void OnResults(std::vector<DeviceStatus>& results) {
    if (dashboard::IsOpen()) dashboard::UpdateDevices(results, battery_history::GetStore());
    g_unsupported.clear();
    results.erase(std::remove_if(results.begin(), results.end(), [](const DeviceStatus& s) {
                      if (s.unsupported) g_unsupported.push_back(s.name);
                      return s.unsupported;
                  }), results.end());
    for (auto& s : results) SmoothCharging(s);
    std::map<std::wstring, bool> seen;
    ULONGLONG tick = GetTickCount64();
    ULONGLONG sampleMs = g_lastPollTick ? std::min<ULONGLONG>(tick - g_lastPollTick, g_pollIntervalMs.load()) : 0;
    g_lastPollTick = tick;
    for (auto& s : results) {
        auto& e = g_entries[s.key];
        if (!e.uid) e.uid = StableId(s.key);
        const int previousPercent = e.st.connected && !e.st.asleep ? e.st.percent : -1;
        e.st = s;
        if (s.percent >= 0) e.lastPct = s.percent;
        if (reading_freshness::Confirmed(s.percent, s.connected, s.asleep)) e.lastReadingTick = tick;
        e.lastSeen = tick;
        e.missStreak = 0;
        seen[s.key] = true;
        alerts::LowLatch& latch = g_alertLatches[s.key];
        alerts::LowAlert low = IsHidden(s.key) ? alerts::LowAlert::None
                             : alerts::EvaluateLow(s, alerts::EffectiveThreshold(g_alertOverrides, s.key), latch, g_tieredAlerts);
        e.lowWarned = latch.low;
        bool notify = low != alerts::LowAlert::None;
        if (g_alertLatches.size() > 256) g_alertLatches.erase(g_alertLatches.begin());
        bool charged = g_chargedAlert && !IsHidden(s.key) && alerts::ShouldNotifyCharged(s, previousPercent, e.chargedWarned);
        if (g_recordHistory && s.connected && !s.asleep && s.percent >= 0)
            battery_history::Observe(s.key, s.percent, s.charging);
        if (g_recordHistory && !g_demo) battery_intelligence::ObserveDevice(s, (int64_t)time(nullptr));
        bool limit = !IsHidden(s.key) && alerts::ShouldNotifyChargeLimit(s, g_chargeLimit, previousPercent, e.limitWarned);
        if (s.connected && !s.asleep) charge::Observe(e.charge, s.charging, s.percent, (int64_t)time(nullptr));
        bool shortRuntime = false;
        std::wstring left;
        if (g_shortRuntimeAlert && !notify && !IsHidden(s.key) && s.connected && !s.asleep && s.percent >= 0) {
            int threshold = alerts::EffectiveThreshold(g_alertOverrides, s.key);
            double hours = s.charging ? -1 : battery_history::HoursRemaining(s.key);
            if (threshold != alerts::kDisabled &&
                charge::ShouldWarnShortRuntime(hours, s.charging, s.percent, threshold, e.shortWarned)) {
                shortRuntime = true;
                left = battery_history::RemainingText(s.key);
            }
        } else if (s.charging) {
            e.shortWarned = false;
        }
        if (!g_demo && (g_trackSince || g_trackAwake)) TrackCharge(e, s, sampleMs);
        ApplyEntry(e);
        if (notify) ShowBatteryAlert(e, low);
        else if (charged) ShowChargedAlert(e);
        else if (limit) ShowChargeLimitAlert(e);
        else if (shortRuntime && !left.empty()) ShowShortRuntimeAlert(e, left);
    }
    std::set<std::wstring> liveNames;
    for (auto& s : results)
        if (s.connected && !s.asleep && s.percent >= 0 && s.name.find(L"wireless device") == std::wstring::npos)
            liveNames.insert(s.name);
    for (auto it = g_entries.begin(); it != g_entries.end();) {
        auto& e = it->second;
        bool inGrace = !seen.count(it->first) && e.st.graceSec > 0 && e.st.connected &&
                       GetTickCount64() - e.lastSeen <
                           std::max<ULONGLONG>((ULONGLONG)e.st.graceSec * 1000, 2ULL * g_pollIntervalMs) &&
                       !liveNames.count(e.st.name);
        if (inGrace) {
            if (++e.missStreak >= 2) {
                e.st.percent = -1;
                e.st.asleep = true;
            }
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
    Reconcile();
    UpdatePlaceholder();
    UpdateWidget();
    if (!g_useWidget) UpdateStatusSnapshot();
    panel::Refresh();
    memory_trim::Soon();
}

bool NothingShown() {
    if (AnyReading()) return false;  // the readings keep the strip on the taskbar
    for (const auto& [k, e] : g_entries)
        if (IsShown(e)) return false;
    return true;
}

void Reconcile() {
    bool emptyToTray = g_wantWidget && g_trayWhenEmpty && NothingShown();
    bool wasAlive = widget::Alive();
    bool alive = g_wantWidget && !emptyToTray && wasAlive;
    if (g_wantWidget && !emptyToTray && !alive) alive = widget::Create(g_inst, g_hwnd, WM_WIDGET_CLICK, g_lightTaskbar);
    if (!g_wantWidget || emptyToTray) widget::Destroy();
    bool useWidget = alive || emptyToTray;
    bool changed = useWidget != g_useWidget || emptyToTray != g_emptyToTray;
    g_useWidget = useWidget;
    g_emptyToTray = emptyToTray;
    if (changed || (alive && !wasAlive)) {
        ApplyStripOptions();
        for (auto& [k, e] : g_entries) ApplyEntry(e);
        UpdateSystemTrayIcons();
        UpdatePlaceholder();
        UpdateWidget();
    } else if (g_useWidget) {
        widget::Reposition();
    } else {
        bool anyAdded = false;
        for (auto& [k, e] : g_entries) {
            if (!e.added && IsShown(e)) ApplyEntry(e);
            anyAdded |= e.added;
        }
        UpdateSystemTrayIcons();
        for (const auto& s : g_sysTray) anyAdded |= s.added;
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

void SetKeepMouseLastKnown(bool on) {
    g_keepMouseLastKnown = on;
    SaveBoolSetting(L"KeepMouseLastKnown", on);
    for (auto& [k, e] : g_entries) ApplyEntry(e);
    UpdatePlaceholder();
    UpdateWidget();
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
    for (auto& s : g_sysTray) s.added = false;
    UpdateSystemTrayIcons();
    UpdatePlaceholder();
}

void SetLanguage(const std::wstring& code);

void RedrawAll() {
    if (g_alertIconAdded) {
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
    for (auto& s : g_sysTray) s.sig.clear();
    UpdateSystemTrayIcons();
    UpdatePlaceholder();
}

void SetLanguage(const std::wstring& code) {
    i18n::SetChoice(code);
    RedrawAll();
    UpdateWidget();
}

void SetConnectionIcons(bool on) {
    g_connectionIcons = on;
    SaveConnectionIcons(on);
    widget::SetConnectionIcons(on);
}

void SetRingStyle(bool on) {
    g_ringStyle = on;
    SaveRingStyle(on);
    widget::SetRingStyle(on);
}

void SetShowPercentSign(bool on) {
    g_showPercentSign = on;
    SaveShowPercentSign(on);
    widget::SetShowPercentSign(on);
}

void SetTwoRows(bool on) {
    g_twoRows = on;
    SaveBoolSetting(L"TwoRowsLayout", on);
    widget::SetTwoRows(on);
    if (vitals_strip::Alive()) vitals_strip::SetTwoRows(on);
}

void SetCustomIcons(bool on) {
    g_customIcons = on;
    SaveBoolSetting(L"CustomVectorIcons", on);
    widget::SetCustomIcons(on);
    if (vitals_strip::Alive()) vitals_strip::SetCustomIcons(on);
}

void SetTrackSince(bool on) {
    g_trackSince = on;
    SaveTrackSince(on);
    RedrawAll();
    UpdateWidget();
}

void SetTrackAwake(bool on) {
    g_trackAwake = on;
    SaveTrackAwake(on);
    RedrawAll();
    UpdateWidget();
}

void SetTrackEmpty(bool on) {
    g_trackEmpty = on;
    SaveTrackEmpty(on);
    RedrawAll();
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

extern DWORD g_displayState;

dashboard::VitalsSnapshot BuildDashboardVitals() {
    dashboard::VitalsSnapshot v;
    v.cpuPct = g_sys.cpu;
    v.cpuTempC = g_cpuTemp.tempC;
    v.cpuWatts = g_sys.cpuWatts;
    v.ramPct = g_sys.ram;
    v.ramUsedGb = g_sys.ramUsedGb;
    v.ramTotalGb = g_sys.ramTotalGb;

    v.gpuAvailable = g_gpu.available;
    if (g_gpu.available) {
        v.gpuName = g_gpu.name;
        v.gpuPct = g_gpu.use;
        v.gpuTempC = g_gpu.tempC;
        v.gpuWatts = g_gpu.watts;
        v.gpuClockMhz = g_gpu.gpuClockMhz;
        v.vramClockMhz = g_gpu.vramClockMhz;
        v.vramPct = g_gpu.vram;
        v.vramUsedGb = g_gpu.vramUsedGb;
        v.vramTotalGb = g_gpu.vramTotalGb;
    }

    v.diskAvailable = g_disk.available;
    if (g_disk.available) {
        v.diskName = g_disk.name;
        v.diskDrive = g_disk.driveLetter;
        v.diskTempC = g_disk.tempC;
        v.diskPct = g_disk.usePct;
        v.diskReadMBs = g_disk.readMBs;
        v.diskWriteMBs = g_disk.writeMBs;
        v.diskFreeGb = g_disk.freeGb;
        v.diskTotalGb = g_disk.totalGb;
    }

    v.netAvailable = g_net.available;
    if (g_net.available) {
        v.netDownFull = netmon::FormatRateFull(g_net.downBytesPerSec);
        v.netUpFull = netmon::FormatRateFull(g_net.upBytesPerSec);
    }
    auto st = netmon::GetSpeedTest();
    v.netTesting = st.state == netmon::TestState::Running;
    v.netTestDone = st.state == netmon::TestState::Done;
    v.netTestFailed = st.state == netmon::TestState::Failed;
    if (st.state == netmon::TestState::Done) {
        v.netTestDown = netmon::FormatMbps(st.downMbps);
        v.netTestUp = netmon::FormatMbps(st.upMbps);
    }

    v.batPresent = g_laptopBat.present;
    if (g_laptopBat.present) {
        v.batPct = g_laptopBat.percent;
        v.batCharging = g_laptopBat.charging;
        v.batAcOnline = g_laptopBat.acOnline;
        v.batHasWatts = g_laptopBat.hasWatts;
        if (g_laptopBat.hasWatts) v.batWattsText = laptopbat::FormatWattsDetailed(g_laptopBat.watts);
        if (!g_laptopBat.charging && g_laptopBat.remainingMinutes > 0)
            v.batRemainingText = laptopbat::FormatRemaining(g_laptopBat.remainingMinutes);
        v.batRemainingMwh = g_laptopBat.remainingMwh;
        v.batMaxMwh = g_laptopBat.maxMwh;
    }

    const ULONGLONG nowTick = GetTickCount64();
    auto toSeries = [&](const std::deque<VitalPoint>& hist) {
        std::vector<int> out;
        out.reserve(hist.size());
        for (const auto& pt : hist) {
            if (nowTick >= pt.tickMs && nowTick - pt.tickMs <= kVitalHistoryMs) out.push_back(pt.value);
        }
        return out;
    };
    v.cpuHist = toSeries(g_cpuHist);
    v.ramHist = toSeries(g_ramHist);
    v.gpuHist = toSeries(g_gpuHist);
    v.vramHist = toSeries(g_vramHist);
    v.diskHist = toSeries(g_diskHist);
    v.netHist = toSeries(g_netHist);
    v.batHist = toSeries(g_batHist);
    return v;
}

void StartNetSpeedTest() {
    if (netmon::StartSpeedTest()) {
        g_lastSpeedTestState = netmon::TestState::Running;
        UpdateWidget();
        panel::Refresh();
        if (dashboard::IsOpen()) dashboard::UpdateVitals(BuildDashboardVitals());
    }
}

void TickSystemReadings(bool now = false) {
    // Check background speed test completion even if network strip is currently off.
    netmon::SpeedTestResult st = netmon::GetSpeedTest();
    bool speedTestChanged = st.state != g_lastSpeedTestState;
    g_lastSpeedTestState = st.state;
    const bool wantDash = dashboard::IsOpen();
    static bool s_wasDash = false;
    if (s_wasDash && !wantDash) {
        if (!g_showCpuW) sysmon::StopPower();
        if (!AnyCpuReading()) g_cpuHist.clear();
        if (!g_showRam) g_ramHist.clear();
        if (!AnyGpuReading()) {
            gpumon::Stop();
            g_gpu = gpumon::Reading();
            g_gpuHist.clear();
            g_vramHist.clear();
        }
        if (!AnyDiskReading()) {
            diskmon::Stop();
            g_disk = diskmon::Reading();
            g_diskHist.clear();
        }
        if (!g_showNetSpeed) {
            netmon::Stop();
            g_net = netmon::Reading();
            g_netHist.clear();
        }
        if (!AnyLaptopBatReading()) {
            laptopbat::Stop();
            g_laptopBat = laptopbat::Reading();
            g_batHist.clear();
        }
    }
    s_wasDash = wantDash;

    if (!AnyReading() && !wantDash) {
        if (speedTestChanged) {
            panel::Refresh();
        }
        return;
    }
    // Pause hardware sensor polling while display is turned off (unless forced on wake/command).
    if (!now && g_displayState == 0) return;
    if (!now && (++g_sysTicks % std::max<DWORD>(1, g_sysPollSeconds))) {
        if (speedTestChanged) {
            UpdateWidget();
            UpdateSystemTrayIcons();
            panel::Refresh();
            if (wantDash) dashboard::UpdateVitals(BuildDashboardVitals());
        }
        return;
    }
    bool changed = speedTestChanged;
    const ULONGLONG sampleTick = GetTickCount64();

    if (g_showCpuTemp || (wantDash && cputemp::HelperInstalled())) {
        cputemp::Reading t = cputemp::ReadShared();
        changed |= g_showCpuTemp && (t.tempC != g_cpuTemp.tempC || t.status != g_cpuTemp.status || t.running != g_cpuTemp.running);
        g_cpuTemp = t;
    }
    if (AnyCpuReading() || wantDash) {
        sysmon::Reading r = sysmon::Sample(g_showCpuW || wantDash);
        changed |= (g_showCpu && r.cpu != g_sys.cpu) || (g_showRam && r.ram != g_sys.ram) ||
                   (g_showCpuW && r.cpuWatts != g_sys.cpuWatts);
        g_sys = r;
        if ((g_showCpu || wantDash) && r.cpu >= 0) PushVitalHist(g_cpuHist, sampleTick, r.cpu);
        else if (g_showCpuTemp && g_cpuTemp.tempC >= 0) PushVitalHist(g_cpuHist, sampleTick, std::clamp(g_cpuTemp.tempC, 0, 100));
        if ((g_showRam || wantDash) && r.ram >= 0) PushVitalHist(g_ramHist, sampleTick, r.ram);
    }
    if (AnyGpuReading() || wantDash) {
        gpumon::Reading g = gpumon::Sample(g_showGpuTemp || wantDash, g_showGpuW || wantDash, g_showGpuClocks || wantDash);
        changed |= AnyGpuReading() &&
                   (g.available != g_gpu.available || (g_showGpu && g.use != g_gpu.use) ||
                    (g_showGpuTemp && g.tempC != g_gpu.tempC) ||
                    (g_showGpuClocks && (g.gpuClockMhz != g_gpu.gpuClockMhz || g.vramClockMhz != g_gpu.vramClockMhz)) ||
                    (g_showVram && g.vram != g_gpu.vram) || (g_showGpuW && g.watts != g_gpu.watts));
        g_gpu = g;
        if (g.available) {
            if ((g_showGpu || wantDash) && g.use >= 0) PushVitalHist(g_gpuHist, sampleTick, g.use);
            else if (g_showGpuTemp && g.tempC >= 0) PushVitalHist(g_gpuHist, sampleTick, std::clamp(g.tempC, 0, 100));
            if ((g_showVram || wantDash) && g.vram >= 0) PushVitalHist(g_vramHist, sampleTick, g.vram);
        }
    }
    if (AnyDiskReading() || wantDash) {
        diskmon::Reading d = diskmon::Sample(g_showDiskTemp || wantDash, g_showDiskUse || wantDash);
        changed |= AnyDiskReading() &&
                   (d.available != g_disk.available || (g_showDiskTemp && d.tempC != g_disk.tempC) ||
                    (g_showDiskUse && (d.usePct != g_disk.usePct || (int)(d.readMBs * 10) != (int)(g_disk.readMBs * 10) ||
                                       (int)(d.writeMBs * 10) != (int)(g_disk.writeMBs * 10))));
        g_disk = d;
        if (d.available) {
            if ((g_showDiskUse || wantDash) && d.usePct >= 0) PushVitalHist(g_diskHist, sampleTick, d.usePct);
            else if (g_showDiskTemp && d.tempC >= 0) PushVitalHist(g_diskHist, sampleTick, std::clamp(d.tempC, 0, 100));
        }
    }
    if (g_showNetSpeed || wantDash) {
        netmon::Reading n = netmon::Sample();
        int prevDownK = g_net.downBytesPerSec >= 0 ? (int)(g_net.downBytesPerSec / 1024) : -1;
        int prevUpK = g_net.upBytesPerSec >= 0 ? (int)(g_net.upBytesPerSec / 1024) : -1;
        int nowDownK = n.downBytesPerSec >= 0 ? (int)(n.downBytesPerSec / 1024) : -1;
        int nowUpK = n.upBytesPerSec >= 0 ? (int)(n.upBytesPerSec / 1024) : -1;
        changed |= g_showNetSpeed && (n.available != g_net.available || nowDownK != prevDownK || nowUpK != prevUpK);
        g_net = n;
        if (n.available && n.downBytesPerSec >= 0.0 && n.upBytesPerSec >= 0.0) {
            int totalKbps = (int)((n.downBytesPerSec + n.upBytesPerSec) * 8.0 / 1000.0 + 0.5);
            PushVitalHist(g_netHist, sampleTick, std::max(0, totalKbps));
        }
    }
    if (AnyLaptopBatReading() || (wantDash && g_hasLaptopBat)) {
        laptopbat::Reading b = laptopbat::Sample();
        changed |= AnyLaptopBatReading() &&
                   (b.present != g_laptopBat.present || (g_showLaptopBat && b.percent != g_laptopBat.percent) ||
                    b.charging != g_laptopBat.charging || b.acOnline != g_laptopBat.acOnline ||
                    (g_showLaptopBatW && (b.hasWatts != g_laptopBat.hasWatts ||
                                          (int)std::llround(b.watts * 10.0) != (int)std::llround(g_laptopBat.watts * 10.0))) ||
                    b.remainingMinutes != g_laptopBat.remainingMinutes);
        g_laptopBat = b;
        if (b.present && b.percent >= 0) PushVitalHist(g_batHist, sampleTick, b.percent);
    }
    if (g_tempAlertC > 0) {
        if (g_showCpuTemp && g_cpuTemp.tempC >= 0) {
            if (g_cpuTemp.tempC >= g_tempAlertC && !g_cpuTempWarned) {
                g_cpuTempWarned = true;
                ShowTextNotification(T(L"High temperature"),
                                     TF(L"{name} reached {temp} °C",
                                        { { L"name", T(L"CPU") }, { L"temp", std::to_wstring(g_cpuTemp.tempC) } }));
            } else if (g_cpuTemp.tempC <= g_tempAlertC - 5) {
                g_cpuTempWarned = false;
            }
        }
        if (g_showGpuTemp && g_gpu.available && g_gpu.tempC >= 0) {
            if (g_gpu.tempC >= g_tempAlertC && !g_gpuTempWarned) {
                g_gpuTempWarned = true;
                ShowTextNotification(T(L"High temperature"),
                                     TF(L"{name} reached {temp} °C",
                                        { { L"name", L"GPU" }, { L"temp", std::to_wstring(g_gpu.tempC) } }));
            } else if (g_gpu.tempC <= g_tempAlertC - 5) {
                g_gpuTempWarned = false;
            }
        }
        if (g_showDiskTemp && g_disk.available && g_disk.tempC >= 0) {
            if (g_disk.tempC >= g_tempAlertC && !g_diskTempWarned) {
                g_diskTempWarned = true;
                ShowTextNotification(T(L"High temperature"),
                                     TF(L"{name} reached {temp} °C",
                                        { { L"name", T(L"Storage") }, { L"temp", std::to_wstring(g_disk.tempC) } }));
            } else if (g_disk.tempC <= g_tempAlertC - 5) {
                g_diskTempWarned = false;
            }
        }
    }
    if (wantDash) dashboard::UpdateVitals(BuildDashboardVitals());
    if (changed) {
        UpdateWidget();
        UpdateSystemTrayIcons();
        UpdatePlaceholder();
        panel::Refresh();
    }
}

void SetShowCpuTemp(bool on) {
    if (on) {
        if (!cputemp::PawnIoInstalled()) {
            if (MessageBoxW(g_hwnd, T(L"The CPU temperature needs the free PawnIO driver, which is not installed. Open pawnio.eu to get it?"),
                            kAppName, MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND) == IDYES)
                ShellExecuteW(nullptr, L"open", L"https://pawnio.eu", nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if (!cputemp::HelperInstalled()) {
            if (MessageBoxW(g_hwnd,
                            T(L"The CPU temperature is read by a small helper that needs administrator rights. Setup copies it to "
                              L"Program Files and adds a task that starts it when you sign in, so you will not be asked again. "
                              L"Windows asks for your permission once, now. Continue?"),
                            kAppName, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) != IDYES)
                return;
            cputemp::InstallResult r = cputemp::InstallHelper(g_hwnd);
            if (r == cputemp::InstallResult::Failed)
                MessageBoxW(g_hwnd, T(L"The helper could not be set up."), kAppName, MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
            if (r != cputemp::InstallResult::Ok) return;
        }
        g_showCpuTemp = true;
        SaveBoolSetting(L"ShowCpuTemp", true);
        cputemp::StartHelper();
    } else {
        g_showCpuTemp = false;
        SaveBoolSetting(L"ShowCpuTemp", false);
        cputemp::StopHelper();
        g_cpuTemp = cputemp::Reading();
        g_cpuTempWarned = false;
    }
    TickSystemReadings(true);
    SyncVitalsStrip();
    UpdateWidget();
    UpdateSystemTrayIcons();
    UpdatePlaceholder();
}

void RemoveCpuTempHelper() {
    if (MessageBoxW(g_hwnd, T(L"Remove the CPU temperature helper? Windows asks for your permission. The CPU temperature turns off."),
                    kAppName, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) != IDYES)
        return;
    g_showCpuTemp = false;
    SaveBoolSetting(L"ShowCpuTemp", false);
    cputemp::StopHelper();
    g_cpuTemp = cputemp::Reading();
    g_cpuTempWarned = false;
    cputemp::RemoveHelper(g_hwnd);
    TickSystemReadings(true);
    SyncVitalsStrip();
    UpdateWidget();
    UpdateSystemTrayIcons();
    UpdatePlaceholder();
}

void SetShowReading(bool& flag, const wchar_t* name, bool on) {
    flag = on;
    SaveBoolSetting(name, on);
    if (!g_showCpuW) sysmon::StopPower();
    if (!AnyCpuReading() && !dashboard::IsOpen()) g_cpuHist.clear();
    if (!g_showRam && !dashboard::IsOpen()) g_ramHist.clear();
    if (!AnyGpuReading() && !dashboard::IsOpen()) {
        gpumon::Stop();
        g_gpu = gpumon::Reading();
        g_gpuTempWarned = false;
        g_gpuHist.clear();
        g_vramHist.clear();
    }
    if (!AnyDiskReading() && !dashboard::IsOpen()) {
        diskmon::Stop();
        g_disk = diskmon::Reading();
        g_diskTempWarned = false;
        g_diskHist.clear();
    }
    if (!g_showNetSpeed && !dashboard::IsOpen()) {
        netmon::Stop();
        g_net = netmon::Reading();
        g_netHist.clear();
    }
    if (!AnyLaptopBatReading() && !dashboard::IsOpen()) {
        laptopbat::Stop();
        g_laptopBat = laptopbat::Reading();
        g_batHist.clear();
    }
    TickSystemReadings(true);
    SyncVitalsStrip();
    UpdateWidget();
    UpdateSystemTrayIcons();
    UpdatePlaceholder();
    UpdateStatusSnapshot();
}

// Battery strip place: 0 = next to clock, 1 = left end, 2 = before apps.
void SetBatPlace(bool farLeft, bool beforeApps) {
    g_farLeft = farLeft;
    g_beforeApps = beforeApps && !farLeft;
    SaveFarLeft(g_farLeft);
    SaveBeforeApps(g_beforeApps);
    ApplyStripOptions();
    SyncVitalsStrip();
    UpdateWidget();
}

// System readings strip place: 0 = next to clock, 1 = left end, 2 = before apps.
void SetVitPlace(bool farLeft, bool beforeApps) {
    g_vitalsFarLeft = farLeft;
    g_vitalsBeforeApps = beforeApps && !farLeft;
    SaveBoolSetting(L"VitalsFarLeft", g_vitalsFarLeft);
    SaveBoolSetting(L"VitalsBeforeApps", g_vitalsBeforeApps);
    ApplyStripOptions();
    SyncVitalsStrip();
    UpdateWidget();
}

void SetVitalsFirst(bool on) {
    g_vitalsFirst = on;
    SaveBoolSetting(L"VitalsFirst", on);
    UpdateWidget();
}

void SetBrandColors(bool on) {
    g_brandColors = on;
    SaveBrandColors(on);
    RedrawAll();
    UpdateWidget();
}

void SetSystemColors(bool on) {
    g_systemColors = on;
    SaveBoolSetting(L"SystemColors", on);
    UpdateSystemTrayIcons();
    UpdateWidget();
}

void SetSystemPollSeconds(DWORD seconds) {
    g_sysPollSeconds = seconds;
    SaveSystemPollSeconds(seconds);
    g_sysTicks = 0;
    TickSystemReadings(true);
}

void SetTempAlert(int celsius) {
    g_tempAlertC = celsius;
    SaveTempAlertCelsius(celsius);
    g_cpuTempWarned = false;
    g_gpuTempWarned = false;
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

void MoveSystemItem(int item, int dir) {
    auto visible = [](int id) { return id != 6 || g_hasLaptopBat; };
    auto pos = std::find(g_systemOrder.begin(), g_systemOrder.end(), item);
    if (pos == g_systemOrder.end() || !visible(item)) return;
    ptrdiff_t i = pos - g_systemOrder.begin(), j = i + dir;
    while (j >= 0 && j < (ptrdiff_t)g_systemOrder.size() && !visible(g_systemOrder[j])) j += dir;
    if (j < 0 || j >= (ptrdiff_t)g_systemOrder.size()) return;
    std::swap(g_systemOrder[i], g_systemOrder[j]);
    SaveSystemOrder();
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

HMENU BuildDeviceSubmenu(const TrayEntry& e, UINT base, bool canLeft, bool canRight, MenuIcons* icons,
                         bool includePlacement) {
    const std::wstring& k = e.st.key;
    HMENU sub = CreatePopupMenu();
    if (!includePlacement && !e.st.via.empty()) {
        const auto connection = TF(L"Connection: {connection}", { { L"connection", T(e.st.via) } });
        AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, connection.c_str());
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    }
    if (includePlacement) {
        AppendMenuW(sub, MF_STRING | (IsHidden(k) ? 0 : MF_CHECKED), base + DEV_TOGGLE,
                    g_wantWidget ? T(L"Show on the taskbar") : T(L"Show in the tray"));
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sub, MF_STRING | (canLeft ? 0 : MF_GRAYED), base + DEV_LEFT,
                    g_wantWidget ? T(L"Move left") : T(L"Move up"));
        AppendMenuW(sub, MF_STRING | (canRight ? 0 : MF_GRAYED), base + DEV_RIGHT,
                    g_wantWidget ? T(L"Move right") : T(L"Move down"));
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(sub, MF_STRING, base + DEV_RENAME, T(L"Rename device..."));
    if (icons) icons->SetLast(sub, 0xE8AC);  // Rename
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
    if (icons) icons->SetLast(sub, 0xEA8F);  // Ringer
    AppendMenuW(sub, MF_STRING, base + DEV_HISTORY, T(L"Battery history..."));
    if (icons) icons->SetLast(sub, 0xE81C);  // History
    AppendMenuW(sub, MF_STRING, base + DEV_DASHBOARD, T(L"View in dashboard..."));
    return sub;
}

HMENU BuildChargeLimitMenu() {
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING, ID_LIMIT_OFF, T(L"Off"));
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    UINT checked = ID_LIMIT_OFF;
    for (UINT i = 0; i < (UINT)alerts::kChargeLimitCount; i++) {
        AppendMenuW(sub, MF_STRING, ID_LIMIT_FIRST + i, (std::to_wstring(alerts::kChargeLimits[i]) + L"%").c_str());
        if (g_chargeLimit == alerts::kChargeLimits[i]) checked = ID_LIMIT_FIRST + i;
    }
    CheckMenuRadioItem(sub, ID_LIMIT_OFF, ID_LIMIT_FIRST + alerts::kChargeLimitCount - 1, checked, MF_BYCOMMAND);
    return sub;
}

HMENU BuildTrackingMenu() {
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING | (g_trackSince ? MF_CHECKED : 0), ID_TRACK_SINCE, T(L"Time since charge"));
    AppendMenuW(sub, MF_STRING | (g_trackAwake ? MF_CHECKED : 0), ID_TRACK_AWAKE, T(L"Awake time (rough)"));
    UINT emptyFlags = MF_STRING | (g_trackEmpty ? MF_CHECKED : 0) | (g_recordHistory ? 0 : MF_GRAYED);
    AppendMenuW(sub, emptyFlags, ID_TRACK_EMPTY, T(L"Estimated time to empty"));
    return sub;
}

HMENU BuildUnsupportedMenu() {
    HMENU un = CreatePopupMenu();
    for (const auto& n : g_unsupported) AppendMenuW(un, MF_STRING | MF_GRAYED, 0, n.c_str());
    AppendMenuW(un, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(un, MF_STRING, ID_REPORT, T(L"Request support for a device..."));
    return un;
}

std::wstring FormatDurationLabel(DWORD seconds) {
    return seconds < 60 ? TF(L"{seconds} s", { { L"seconds", std::to_wstring(seconds) } })
                        : TF(L"{minutes} min", { { L"minutes", std::to_wstring(seconds / 60) } });
}

HMENU BuildPollBatteryMenu() {
    HMENU sub = CreatePopupMenu();
    const UINT count = ARRAYSIZE(POLL_SECONDS);
    UINT checked = 0;
    for (UINT i = 0; i < count; i++) {
        DWORD s = POLL_SECONDS[i];
        AppendMenuW(sub, MF_STRING, ID_POLL_FIRST + i, FormatDurationLabel(s).c_str());
        if (s * 1000 == g_pollIntervalMs) checked = ID_POLL_FIRST + i;
    }
    if (checked) CheckMenuRadioItem(sub, ID_POLL_FIRST, ID_POLL_FIRST + count - 1, checked, MF_BYCOMMAND);
    return sub;
}

HMENU BuildTempAlertMenu() {
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING, ID_TEMP_ALERT_OFF, T(L"Off"));
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    UINT checked = ID_TEMP_ALERT_OFF;
    const UINT count = ARRAYSIZE(TEMP_ALERT_CELSIUS);
    for (UINT i = 0; i < count; i++) {
        AppendMenuW(sub, MF_STRING, ID_TEMP_ALERT_FIRST + i, (std::to_wstring(TEMP_ALERT_CELSIUS[i]) + L" °C").c_str());
        if (g_tempAlertC == TEMP_ALERT_CELSIUS[i]) checked = ID_TEMP_ALERT_FIRST + i;
    }
    CheckMenuRadioItem(sub, ID_TEMP_ALERT_OFF, ID_TEMP_ALERT_FIRST + count - 1, checked, MF_BYCOMMAND);
    return sub;
}

HMENU BuildPollSystemMenu() {
    HMENU sub = CreatePopupMenu();
    const UINT count = ARRAYSIZE(SYS_POLL_SECONDS);
    UINT checked = 0;
    for (UINT i = 0; i < count; i++) {
        DWORD s = SYS_POLL_SECONDS[i];
        AppendMenuW(sub, MF_STRING, ID_SYS_POLL_FIRST + i, FormatDurationLabel(s).c_str());
        if (s == g_sysPollSeconds) checked = ID_SYS_POLL_FIRST + i;
    }
    if (checked) CheckMenuRadioItem(sub, ID_SYS_POLL_FIRST, ID_SYS_POLL_FIRST + count - 1, checked, MF_BYCOMMAND);
    return sub;
}

std::wstring StripPlaceLabel(bool farLeft, bool beforeApps) {
    if (farLeft) return T(L"Left end of the taskbar");
    if (beforeApps) return T(L"Just before the taskbar apps");
    return T(L"Next to the clock");
}

HMENU BuildStripPlaceMenu(UINT leftId, UINT beforeId, UINT clockId, bool farLeft, bool beforeApps, bool enabled) {
    HMENU sub = CreatePopupMenu();
    UINT f = MF_STRING | (enabled ? 0 : MF_GRAYED);
    AppendMenuW(sub, f, leftId, T(L"Left end of the taskbar"));
    AppendMenuW(sub, f, beforeId, T(L"Just before the taskbar apps"));
    AppendMenuW(sub, f, clockId, T(L"Next to the clock"));
    UINT checked = farLeft ? leftId : beforeApps ? beforeId : clockId;
    CheckMenuRadioItem(sub, leftId, clockId, checked, MF_BYCOMMAND);
    return sub;
}

HMENU BuildStripOrderMenu() {
    HMENU sub = CreatePopupMenu();
    UINT f = MF_STRING | (g_wantWidget && !SplitStrips() && AnyReading() ? 0 : MF_GRAYED);
    AppendMenuW(sub, f, ID_BATTERIES_FIRST, T(L"Batteries first"));
    AppendMenuW(sub, f, ID_VITALS_FIRST, T(L"System first"));
    CheckMenuRadioItem(sub, ID_BATTERIES_FIRST, ID_VITALS_FIRST, g_vitalsFirst ? ID_VITALS_FIRST : ID_BATTERIES_FIRST, MF_BYCOMMAND);
    return sub;
}

HMENU BuildTaskbarOffsetMenu(bool includeMakeRoom) {
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
    std::wstring custom = checked == ID_POSITION_FIRST + count
                              ? TF(L"Custom ({px} px)...", { { L"px", std::to_wstring(current) } })
                              : std::wstring(T(L"Custom..."));
    AppendMenuW(sub, f, ID_POSITION_FIRST + count, custom.c_str());
    CheckMenuRadioItem(sub, ID_POSITION_FIRST, ID_POSITION_FIRST + count, checked, MF_BYCOMMAND);
    if (includeMakeRoom) {
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sub, f | (g_makeRoom ? MF_CHECKED : 0), ID_MAKE_ROOM, T(L"Make room for other taskbar widgets"));
    }
    return sub;
}

HMENU BuildVerticalPlaceMenu() {
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
    return sub;
}

HMENU BuildMonitorMenu(std::vector<widget::MonitorOption>& monitors) {
    monitors = widget::OtherMonitors();
    if (monitors.size() > 31) monitors.resize(31);
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
    if (!found) {
        AppendMenuW(sub, MF_STRING | MF_GRAYED, ID_MONITOR_FIRST + 31, T(L"Chosen monitor (not connected, shown on main)"));
        checked = ID_MONITOR_FIRST + 31;
    }
    CheckMenuRadioItem(sub, ID_MONITOR_ALL, ID_MONITOR_FIRST + 31, checked, MF_BYCOMMAND);
    return sub;
}

HMENU BuildLanguageMenu(std::vector<i18n::Language>& languages) {
    i18n::Init();
    languages = i18n::Available();
    if (languages.size() > 64) languages.resize(64);
    static HBITMAP russianFlag = LoadBitmapW(g_inst, L"FLAG_RU");
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING, ID_LANG_AUTO, T(L"Automatic (Windows language)"));
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    UINT checked = ID_LANG_AUTO;
    for (UINT i = 0; i < languages.size(); i++) {
        AppendMenuW(sub, MF_STRING, ID_LANG_FIRST + i, languages[i].name.c_str());
        if (russianFlag && _wcsicmp(languages[i].code.c_str(), L"ru") == 0) {
            MENUITEMINFOW item{ sizeof(item) };
            item.fMask = MIIM_BITMAP;
            item.hbmpItem = russianFlag;
            SetMenuItemInfoW(sub, ID_LANG_FIRST + i, FALSE, &item);
        }
        if (!i18n::Choice().empty() && _wcsicmp(languages[i].code.c_str(), i18n::Choice().c_str()) == 0)
            checked = ID_LANG_FIRST + i;
    }
    CheckMenuRadioItem(sub, ID_LANG_AUTO, ID_LANG_FIRST + 63, checked, MF_BYCOMMAND);
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sub, MF_STRING, ID_LANG_ADD, T(L"Add your own language..."));
    return sub;
}

// Builds the organized fallback context menu (used by Shift + click and keyboard access).
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
        HMENU sub = BuildDeviceSubmenu(*e, base, canLeft, canRight, &icons, true);
        std::wstring line = DisplayName(*e) + L":  " + StatusText(*e) + L"   [" + T(e->st.via) + L"]";
        AppendMenuW(m, MF_POPUP | (IsHidden(k) ? 0 : MF_CHECKED), (UINT_PTR)sub, line.c_str());
        icons.SetLast(m, widget::DeviceGlyph(e->st.kind, e->st.via == L"Bluetooth"));
        keys.push_back(k);
    }
    if (!g_unsupported.empty()) {
        std::wstring title = TF(L"No battery info ({count})", { { L"count", std::to_wstring(g_unsupported.size()) } });
        AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildUnsupportedMenu(), title.c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    // Batteries submenu
    {
        HMENU batMenu = CreatePopupMenu();
        AppendMenuW(batMenu, MF_STRING | (g_dimSleeping ? MF_CHECKED : 0), ID_DIM_SLEEPING, T(L"Dim sleeping devices"));
        AppendMenuW(batMenu, MF_STRING | (g_hideLastKnown ? MF_CHECKED : 0), ID_HIDE_LAST_KNOWN,
                    T(L"Hide last known / asleep devices"));
        AppendMenuW(batMenu, MF_STRING | (g_keepMouseLastKnown ? MF_CHECKED : 0) | (g_hideLastKnown ? 0 : MF_GRAYED),
                    ID_KEEP_MOUSE_LAST_KNOWN, T(L"Keep last known battery for mice"));
        AppendMenuW(batMenu, MF_STRING | (g_showDisconnectedDevices ? MF_CHECKED : 0), ID_SHOW_DISCONNECTED,
                    T(L"Show disconnected devices"));
        UINT emptyFlags = MF_STRING | (g_trayWhenEmpty ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED);
        AppendMenuW(batMenu, emptyFlags, ID_TRAY_WHEN_EMPTY, T(L"Move to tray when no devices"));
        AppendMenuW(batMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(batMenu, MF_STRING | (g_recordHistory ? MF_CHECKED : 0), ID_RECORD_HISTORY,
                    T(L"Record battery history"));
        AppendMenuW(batMenu, MF_POPUP, (UINT_PTR)BuildChargeLimitMenu(),
                    T(L"Notify when charged to (saves the battery)"));
        AppendMenuW(batMenu, MF_STRING | (g_chargedAlert ? MF_CHECKED : 0), ID_CHARGED_ALERT,
                    T(L"Notify when fully charged"));
        AppendMenuW(batMenu, MF_STRING | (g_tieredAlerts ? MF_CHECKED : 0), ID_TIERED_ALERTS,
                    T(L"Warn again at 10% and 5%"));
        AppendMenuW(batMenu, MF_STRING | (g_shortRuntimeAlert ? MF_CHECKED : 0) | (g_recordHistory ? 0 : MF_GRAYED),
                    ID_SHORT_RUNTIME_ALERT, T(L"Warn when a battery won't last the day"));
        AppendMenuW(batMenu, MF_POPUP, (UINT_PTR)BuildTrackingMenu(), T(L"Battery tracking"));
        AppendMenuW(batMenu, MF_POPUP, (UINT_PTR)BuildPollBatteryMenu(), T(L"Check battery every"));
        AppendMenuW(batMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(batMenu, MF_STRING, ID_DASHBOARD, T(L"Device dashboard..."));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)batMenu, T(L"Batteries"));
        icons.SetLast(m, 0xE83F);  // Battery
    }

    // System submenu
    {
        HMENU sysMenu = CreatePopupMenu();
        const wchar_t* labels[] = { L"CPU", L"RAM usage", L"GPU", L"GPU memory", L"Storage", L"Network", L"Laptop battery" };
        std::vector<int> visibleSys;
        for (int id : g_systemOrder) {
            if (id == 6 && !g_hasLaptopBat) continue;
            visibleSys.push_back(id);
        }
        for (size_t idx = 0; idx < visibleSys.size(); ++idx) {
            int item = visibleSys[idx];
            bool canUp = idx > 0, canDown = idx + 1 < visibleSys.size();
            HMENU itemMenu = CreatePopupMenu();
            if (item == 0) {
                AppendMenuW(itemMenu, MF_STRING | (g_showCpu ? MF_CHECKED : 0), ID_SHOW_CPU, T(L"Show CPU usage"));
                AppendMenuW(itemMenu, MF_STRING | (g_showCpuTemp ? MF_CHECKED : 0), ID_SHOW_CPU_TEMP,
                            T(L"Show CPU temperature"));
                AppendMenuW(itemMenu, MF_STRING | (g_showCpuW ? MF_CHECKED : 0), ID_SHOW_CPU_W,
                            T(L"Show CPU power (watts)"));
                if (cputemp::HelperInstalled()) {
                    AppendMenuW(itemMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(itemMenu, MF_STRING, ID_REMOVE_TEMP, T(L"Remove the CPU temperature helper..."));
                }
            } else if (item == 1) {
                AppendMenuW(itemMenu, MF_STRING | (g_showRam ? MF_CHECKED : 0), ID_SHOW_RAM, T(L"Show RAM usage"));
            } else if (item == 2) {
                AppendMenuW(itemMenu, MF_STRING | (g_showGpu ? MF_CHECKED : 0), ID_SHOW_GPU, T(L"Show GPU usage"));
                AppendMenuW(itemMenu, MF_STRING | (g_showGpuTemp ? MF_CHECKED : 0), ID_SHOW_GPU_TEMP,
                            T(L"Show GPU temperature"));
                AppendMenuW(itemMenu, MF_STRING | (g_showGpuClocks ? MF_CHECKED : 0), ID_SHOW_GPU_CLOCKS,
                            T(L"Show GPU clocks (MHz)"));
                AppendMenuW(itemMenu, MF_STRING | (g_showGpuW ? MF_CHECKED : 0), ID_SHOW_GPU_W,
                            T(L"Show GPU power (watts)"));
            } else if (item == 3) {
                AppendMenuW(itemMenu, MF_STRING | (g_showVram ? MF_CHECKED : 0), ID_SHOW_VRAM, T(L"Show GPU memory"));
            } else if (item == 4) {
                AppendMenuW(itemMenu, MF_STRING | (g_showDiskTemp ? MF_CHECKED : 0), ID_SHOW_DISK_TEMP,
                            T(L"Show storage temperature"));
                AppendMenuW(itemMenu, MF_STRING | (g_showDiskUse ? MF_CHECKED : 0), ID_SHOW_DISK_USE,
                            T(L"Show storage usage"));
            } else if (item == 5) {
                AppendMenuW(itemMenu, MF_STRING | (g_showNetSpeed ? MF_CHECKED : 0), ID_SHOW_NET_SPEED,
                            T(L"Show network speed"));
                auto st = netmon::GetSpeedTest();
                AppendMenuW(itemMenu, MF_STRING | (st.state == netmon::TestState::Running ? MF_GRAYED : 0),
                            ID_RUN_SPEED_TEST, T(L"Run speed test"));
            } else if (item == 6) {
                AppendMenuW(itemMenu, MF_STRING | (g_showLaptopBat ? MF_CHECKED : 0), ID_SHOW_LAPTOP_BAT,
                            T(L"Show laptop battery (%)"));
                AppendMenuW(itemMenu, MF_STRING | (g_showLaptopBatW ? MF_CHECKED : 0), ID_SHOW_LAPTOP_BAT_W,
                            T(L"Show battery power (watts)"));
            }
            AppendMenuW(itemMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(itemMenu, MF_STRING | (canUp ? 0 : MF_GRAYED), ID_SYS_MOVE_FIRST + item * 2, T(L"Move earlier"));
            AppendMenuW(itemMenu, MF_STRING | (canDown ? 0 : MF_GRAYED), ID_SYS_MOVE_FIRST + item * 2 + 1, T(L"Move later"));
            AppendMenuW(sysMenu, MF_POPUP, (UINT_PTR)itemMenu, T(labels[item]));
        }
        AppendMenuW(sysMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sysMenu, MF_STRING, ID_SYSTEM_DASHBOARD, T(L"System vitals dashboard..."));
        AppendMenuW(sysMenu, MF_POPUP, (UINT_PTR)BuildTempAlertMenu(), T(L"High temperature alert"));
        AppendMenuW(sysMenu, MF_POPUP, (UINT_PTR)BuildPollSystemMenu(), T(L"Check system every"));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sysMenu, T(L"System"));
        icons.SetLast(m, 0xE950);  // Diagnostic / Chip
    }

    // Appearance submenu
    std::vector<widget::MonitorOption> monitors;
    {
        HMENU appMenu = CreatePopupMenu();
        AppendMenuW(appMenu, MF_STRING | (g_wantWidget && !g_myFinder ? MF_CHECKED : 0), ID_MODE_WIDGET,
                    T(L"Show on the taskbar (icon + percentage)"));
        AppendMenuW(appMenu, MF_STRING | (!g_wantWidget ? MF_CHECKED : 0), ID_MODE_TRAY, T(L"Show as tray icons"));
        if (g_myFinder || FindWindowW(L"MyFinderApp", nullptr))
            AppendMenuW(appMenu, MF_STRING | (g_wantWidget && g_myFinder ? MF_CHECKED : 0), ID_MODE_MYFINDER,
                        T(L"Show on MyDockFinder (experimental)"));
        AppendMenuW(appMenu, MF_STRING | (g_trayDeviceIcons ? MF_CHECKED : 0) | (g_wantWidget ? MF_GRAYED : 0),
                    ID_TRAY_DEVICE_ICONS, T(L"Tray icons show the device type"));
        AppendMenuW(appMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(appMenu, MF_POPUP | (g_wantWidget && !g_myFinder ? 0 : MF_GRAYED),
                    (UINT_PTR)BuildStripPlaceMenu(ID_BAT_LEFT, ID_BAT_BEFORE, ID_BAT_CLOCK, g_farLeft, g_beforeApps,
                                                  g_wantWidget && !g_myFinder),
                    T(L"Battery strip position"));
        AppendMenuW(appMenu, MF_POPUP | (g_wantWidget && AnyReading() && !g_myFinder ? 0 : MF_GRAYED),
                    (UINT_PTR)BuildStripPlaceMenu(ID_VIT_LEFT, ID_VIT_BEFORE, ID_VIT_CLOCK, g_vitalsFarLeft,
                                                  g_vitalsBeforeApps, g_wantWidget && AnyReading() && !g_myFinder),
                    T(L"System strip position"));
        AppendMenuW(appMenu, MF_POPUP | (g_wantWidget && !SplitStrips() && AnyReading() ? 0 : MF_GRAYED),
                    (UINT_PTR)BuildStripOrderMenu(), T(L"Strip order"));
        AppendMenuW(appMenu, MF_POPUP | (g_wantWidget ? 0 : MF_GRAYED), (UINT_PTR)BuildTaskbarOffsetMenu(true),
                    T(L"Position on the taskbar"));
        if (widget::Vertical())
            AppendMenuW(appMenu, MF_POPUP, (UINT_PTR)BuildVerticalPlaceMenu(), T(L"Position on a vertical taskbar"));
        HMENU monSub = BuildMonitorMenu(monitors);
        if (!monitors.empty())
            AppendMenuW(appMenu, MF_POPUP | (g_wantWidget ? 0 : MF_GRAYED), (UINT_PTR)monSub,
                        T(L"Show the widget on"));
        else
            DestroyMenu(monSub);
        AppendMenuW(appMenu, MF_SEPARATOR, 0, nullptr);
        UINT wf = MF_STRING | (g_wantWidget ? 0 : MF_GRAYED);
        AppendMenuW(appMenu, wf | (g_customIcons ? MF_CHECKED : 0), ID_CUSTOM_ICONS, T(L"Custom crisp vector icons"));
        AppendMenuW(appMenu, wf | (g_twoRows ? MF_CHECKED : 0), ID_TWO_ROWS, T(L"Compact 2-row layout"));
        AppendMenuW(appMenu, wf | (g_stacked ? MF_CHECKED : 0), ID_STACKED, T(L"Icons above percentages (narrower)"));
        AppendMenuW(appMenu, wf | (g_showPercentSign ? MF_CHECKED : 0), ID_PERCENT_SIGN, T(L"Show percent sign (%)"));
        AppendMenuW(appMenu, wf | (g_ringStyle ? MF_CHECKED : 0), ID_RING_STYLE, T(L"Ring style (icon inside a battery ring)"));
        AppendMenuW(appMenu, wf | (g_animateCharging ? MF_CHECKED : 0), ID_ANIMATE_CHARGING, T(L"Animate charging devices"));
        AppendMenuW(appMenu, wf | (g_connectionIcons ? MF_CHECKED : 0), ID_CONNECTION_ICONS, T(L"Mark devices on a 2.4 GHz receiver"));
        AppendMenuW(appMenu, wf | (g_runtimeWidget ? MF_CHECKED : 0), ID_RUNTIME_WIDGET,
                    T(L"Show estimated time instead of % (when available)"));
        AppendMenuW(appMenu, wf | (g_hoverCard ? MF_CHECKED : 0), ID_HOVER_CARD,
                    T(L"Show a card when the mouse rests on a device"));
        AppendMenuW(appMenu, MF_STRING | (g_hoverChart ? MF_CHECKED : 0) | (g_wantWidget && g_hoverCard ? 0 : MF_GRAYED),
                    ID_HOVER_CHART, T(L"Show live charts in the card"));
        AppendMenuW(appMenu, wf | (g_widgetBackground ? MF_CHECKED : 0), ID_WIDGET_BACKGROUND,
                    T(L"Background behind taskbar text (for transparent taskbars)"));
        AppendMenuW(appMenu, MF_STRING | (g_brandColors ? MF_CHECKED : 0), ID_BRAND_COLORS,
                    T(L"Brand colors (Razer green, Logitech blue, ...)"));
        AppendMenuW(appMenu, MF_STRING | (g_systemColors ? MF_CHECKED : 0), ID_SYSTEM_COLORS,
                    T(L"System colors (CPU blue, RAM purple, GPU green, ...)"));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)appMenu, T(L"Appearance"));
        icons.SetLast(m, 0xE790);  // Color
    }

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    std::vector<i18n::Language> languages;
    AppendMenuW(m, MF_POPUP, (UINT_PTR)BuildLanguageMenu(languages), T(L"Language"));
    icons.SetLast(m, 0xE774);  // Globe
    AppendMenuW(m, MF_STRING, ID_DASHBOARD, T(L"Device dashboard..."));
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), ID_AUTOSTART, T(L"Start with Windows"));
    AppendMenuW(m, MF_STRING, ID_REPORT, T(L"Request support for a device..."));
    AppendMenuW(m, MF_STRING, ID_SUPPORT, T(L"Support the project..."));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (updates::Busy() ? MF_GRAYED : 0), ID_UPDATE, updates::MenuLabel().c_str());
    UINT updateFlags = MF_STRING | (updates::AutomaticEnabled() ? MF_CHECKED : 0) |
                       (updates::Supported() ? 0 : MF_GRAYED);
    AppendMenuW(m, updateFlags, ID_AUTO_UPDATE, T(L"Automatically check for updates"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_REFRESH, T(L"Refresh now"));
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

// Runs a command chosen from a menu or the settings panel (list-position ids are resolved against `model`).
void RunCommand(UINT cmd, const MenuModel& model) {
    const auto& keys = model.keys;
    const auto& languages = model.languages;
    const auto& monitors = model.monitors;
    switch (cmd) {
        case ID_REFRESH: SetEvent(g_refreshEvt); TickSystemReadings(true); break;
        case ID_AUTOSTART: SetAutostart(!AutostartEnabled()); break;
        case ID_HIDE_LAST_KNOWN: SetHideLastKnown(!g_hideLastKnown); break;
        case ID_KEEP_MOUSE_LAST_KNOWN: SetKeepMouseLastKnown(!g_keepMouseLastKnown); break;
        case ID_TRAY_WHEN_EMPTY: SetTrayWhenEmpty(!g_trayWhenEmpty); break;
        case ID_SHOW_DISCONNECTED: SetShowDisconnectedDevices(!g_showDisconnectedDevices); break;
        case ID_DIM_SLEEPING: SetDimSleeping(!g_dimSleeping); break;
        case ID_RECORD_HISTORY: g_recordHistory = !g_recordHistory; SaveRecordHistory(g_recordHistory); UpdateWidget(); break;
        case ID_LIMIT_OFF: SetChargeLimit(0); break;
        case ID_DASHBOARD:
        case ID_SYSTEM_DASHBOARD:
            dashboard::Show(g_hwnd, cmd == ID_SYSTEM_DASHBOARD ? 4 : 0);
            {
                std::vector<DeviceStatus> live;
                for (auto& [k, e] : g_entries) live.push_back(e.st);
                dashboard::UpdateDevices(live, battery_history::GetStore());
                TickSystemReadings(true);
                dashboard::UpdateVitals(BuildDashboardVitals());
            }
            break;
        case ID_CHARGED_ALERT: g_chargedAlert = !g_chargedAlert; SaveBoolSetting(L"ChargedAlert", g_chargedAlert); break;
        case ID_TIERED_ALERTS: g_tieredAlerts = !g_tieredAlerts; SaveBoolSetting(L"TieredAlerts", g_tieredAlerts); break;
        case ID_SHORT_RUNTIME_ALERT: g_shortRuntimeAlert = !g_shortRuntimeAlert; SaveBoolSetting(L"ShortRuntimeAlert", g_shortRuntimeAlert); break;
        case ID_TRACK_SINCE: SetTrackSince(!g_trackSince); break;
        case ID_TRACK_AWAKE: SetTrackAwake(!g_trackAwake); break;
        case ID_TRACK_EMPTY: SetTrackEmpty(!g_trackEmpty); break;
        case ID_HOVER_CARD:
        case ID_HOVER_CHART:
            if (cmd == ID_HOVER_CARD) { g_hoverCard = !g_hoverCard; SaveBoolSetting(L"HoverCard", g_hoverCard); }
            else { g_hoverChart = !g_hoverChart; SaveBoolSetting(L"HoverCardChart", g_hoverChart); }
            ApplyStripOptions();
            UpdateWidget();
            break;
        case ID_ANIMATE_CHARGING:
            g_animateCharging = !g_animateCharging;
            SaveBoolSetting(L"AnimateCharging", g_animateCharging);
            widget::SetAnimateCharging(g_animateCharging);
            break;
        case ID_TRAY_DEVICE_ICONS:
            g_trayDeviceIcons = !g_trayDeviceIcons;
            SaveBoolSetting(L"TrayDeviceIcons", g_trayDeviceIcons);
            for (auto& [k, e] : g_entries) ApplyEntry(e);
            break;
        case ID_RUNTIME_WIDGET: g_runtimeWidget = !g_runtimeWidget; SaveRuntimeWidget(g_runtimeWidget); UpdateWidget(); break;
        case ID_TEMP_ALERT_OFF: SetTempAlert(0); break;
        case ID_WIDGET_BACKGROUND: SetWidgetBackground(!g_widgetBackground); break;
        case ID_MONITOR_ALL: SetWidgetMonitor(L"all"); break;
        case ID_LANG_AUTO: SetLanguage(L""); break;
        case ID_LANG_ADD: i18n::OpenUserFolder(); break;
        case ID_MONITOR_MAIN: SetWidgetMonitor(L"main"); break;
        case ID_BRAND_COLORS: SetBrandColors(!g_brandColors); break;
        case ID_SYSTEM_COLORS: SetSystemColors(!g_systemColors); break;
        case ID_STACKED: SetStacked(!g_stacked); break;
        case ID_TWO_ROWS: SetTwoRows(!g_twoRows); break;
        case ID_CUSTOM_ICONS: SetCustomIcons(!g_customIcons); break;
        case ID_PERCENT_SIGN: SetShowPercentSign(!g_showPercentSign); break;
        case ID_RING_STYLE: SetRingStyle(!g_ringStyle); break;
        case ID_CONNECTION_ICONS: SetConnectionIcons(!g_connectionIcons); break;
        case ID_MAKE_ROOM: SetMakeRoom(!g_makeRoom); break;
        case ID_PLACE_AUTO: SetWidgetPlace(widget::VerticalPlace::Auto); break;
        case ID_PLACE_TOP: SetWidgetPlace(widget::VerticalPlace::Top); break;
        case ID_PLACE_TRAY: SetWidgetPlace(widget::VerticalPlace::AboveTray); break;
        case ID_PLACE_BOTTOM: SetWidgetPlace(widget::VerticalPlace::Bottom); break;
        case ID_BAT_LEFT:   SetBatPlace(true, false); break;
        case ID_BAT_BEFORE: SetBatPlace(false, true); break;
        case ID_BAT_CLOCK:  SetBatPlace(false, false); break;
        case ID_VIT_LEFT:   SetVitPlace(true, false); break;
        case ID_VIT_BEFORE: SetVitPlace(false, true); break;
        case ID_VIT_CLOCK:  SetVitPlace(false, false); break;
        case ID_BATTERIES_FIRST: SetVitalsFirst(false); break;
        case ID_VITALS_FIRST: SetVitalsFirst(true); break;
        case ID_SHOW_CPU: SetShowReading(g_showCpu, L"ShowCpu", !g_showCpu); break;
        case ID_SHOW_RAM: SetShowReading(g_showRam, L"ShowRam", !g_showRam); break;
        case ID_SHOW_GPU: SetShowReading(g_showGpu, L"ShowGpu", !g_showGpu); break;
        case ID_SHOW_GPU_TEMP: SetShowReading(g_showGpuTemp, L"ShowGpuTemp", !g_showGpuTemp); break;
        case ID_SHOW_GPU_CLOCKS: SetShowReading(g_showGpuClocks, L"ShowGpuClocks", !g_showGpuClocks); break;
        case ID_SHOW_CPU_W: SetShowReading(g_showCpuW, L"ShowCpuWatts", !g_showCpuW); break;
        case ID_SHOW_CPU_TEMP: SetShowCpuTemp(!g_showCpuTemp); break;
        case ID_REMOVE_TEMP: RemoveCpuTempHelper(); break;
        case ID_SHOW_GPU_W: SetShowReading(g_showGpuW, L"ShowGpuWatts", !g_showGpuW); break;
        case ID_SHOW_VRAM: SetShowReading(g_showVram, L"ShowVram", !g_showVram); break;
        case ID_SHOW_DISK_TEMP: SetShowReading(g_showDiskTemp, L"ShowDiskTemp", !g_showDiskTemp); break;
        case ID_SHOW_DISK_USE: SetShowReading(g_showDiskUse, L"ShowDiskUse", !g_showDiskUse); break;
        case ID_SHOW_NET_SPEED: SetShowReading(g_showNetSpeed, L"ShowNetSpeed", !g_showNetSpeed); break;
        case ID_SHOW_LAPTOP_BAT: SetShowReading(g_showLaptopBat, L"ShowLaptopBat", !g_showLaptopBat); break;
        case ID_SHOW_LAPTOP_BAT_W: SetShowReading(g_showLaptopBatW, L"ShowLaptopBatWatts", !g_showLaptopBatW); break;
        case ID_RUN_SPEED_TEST: StartNetSpeedTest(); break;
        case ID_MODE_WIDGET:
        case ID_MODE_MYFINDER:
            if (g_myFinder != (cmd == ID_MODE_MYFINDER)) {
                g_myFinder = (cmd == ID_MODE_MYFINDER);
                SaveBoolSetting(L"MyFinderWidget", g_myFinder);
                widget::SetMyFinder(g_myFinder);
                widget::Destroy();
                vitals_strip::Destroy();
                g_useWidget = false;
            }
            SetMode(true);
            break;
        case ID_MODE_TRAY: SetMode(false); break;
        case ID_SUPPORT: ShellExecuteW(nullptr, L"open", L"https://buymeacoffee.com/symnistaken", nullptr, nullptr, SW_SHOWNORMAL); break;
        case ID_REPORT: report::Show(g_inst, g_hwnd); break;
        case ID_UPDATE: updates::Activate(g_hwnd); break;
        case ID_AUTO_UPDATE: updates::ToggleAutomatic(); break;
        case ID_EXIT: DestroyWindow(g_hwnd); break;
        default:
            if (cmd >= ID_SYS_MOVE_FIRST && cmd < ID_SYS_MOVE_FIRST + 14) {
                int offset = int(cmd - ID_SYS_MOVE_FIRST);
                MoveSystemItem(offset / 2, (offset % 2) ? 1 : -1);
                break;
            }
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
            } else if (cmd >= ID_TEMP_ALERT_FIRST && cmd < ID_TEMP_ALERT_FIRST + ARRAYSIZE(TEMP_ALERT_CELSIUS)) {
                SetTempAlert(TEMP_ALERT_CELSIUS[cmd - ID_TEMP_ALERT_FIRST]);
            } else if (cmd >= ID_POLL_FIRST && cmd < ID_POLL_FIRST + ARRAYSIZE(POLL_SECONDS)) {
                SetPollSeconds(POLL_SECONDS[cmd - ID_POLL_FIRST]);
            } else if (cmd >= ID_SYS_POLL_FIRST && cmd < ID_SYS_POLL_FIRST + ARRAYSIZE(SYS_POLL_SECONDS)) {
                SetSystemPollSeconds(SYS_POLL_SECONDS[cmd - ID_SYS_POLL_FIRST]);
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
                    case DEV_DASHBOARD: {
                        dashboard::Show(g_hwnd);
                        std::vector<DeviceStatus> live;
                        for (auto& [k, e] : g_entries) live.push_back(e.st);
                        dashboard::UpdateDevices(live, battery_history::GetStore());
                        dashboard::SelectDevice(key);
                        TickSystemReadings(true);
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

// ---- settings panel (panel.cpp) ----------------------------------------------------------------

MenuModel g_panelModel;

std::wstring CurrentLanguageLabel(const std::vector<i18n::Language>& languages) {
    if (!i18n::Choice().empty()) {
        for (const auto& l : languages)
            if (_wcsicmp(l.code.c_str(), i18n::Choice().c_str()) == 0) return l.name;
    }
    return T(L"Automatic (Windows language)");
}

std::wstring CurrentMonitorLabel(const std::vector<widget::MonitorOption>& monitors) {
    if (g_widgetMonitor == L"main") return T(L"Main monitor only");
    if (g_widgetMonitor == L"all") return T(L"All monitors");
    std::vector<std::wstring> ids;
    for (const auto& mo : monitors) ids.push_back(mo.id);
    int idx = widget::FindMonitor(ids, g_widgetMonitor);
    if (idx >= 0) return TF(L"{monitor} only", { { L"monitor", monitors[idx].label } });
    return T(L"Chosen monitor (not connected, shown on main)");
}

std::wstring CurrentOffsetLabel() {
    int current = LoadTaskbarOffset();
    if (current == 0) return T(L"Next to the notification area");
    return TF(L"{px} px from the notification area", { { L"px", std::to_wstring(current) } });
}

std::wstring CurrentVerticalLabel() {
    switch (g_verticalPlace) {
        case widget::VerticalPlace::Top: return T(L"Top of the taskbar");
        case widget::VerticalPlace::AboveTray: return T(L"Above the notification area");
        case widget::VerticalPlace::Bottom:
            return T(L"Bottom of the taskbar (moves the taskbar icons up, Windows 11)");
        default: return T(L"Automatic (keeps clear of app icons)");
    }
}

void PanelBuild(panel::Model& out) {
    g_panelModel = MenuModel{};
    i18n::Init();
    g_panelModel.languages = i18n::Available();
    if (g_panelModel.languages.size() > 64) g_panelModel.languages.resize(64);
    g_panelModel.monitors = widget::OtherMonitors();
    if (g_panelModel.monitors.size() > 31) g_panelModel.monitors.resize(31);

    auto addHeader = [](std::vector<panel::Row>& list, const std::wstring& title) {
        panel::Row r;
        r.kind = panel::RowKind::SectionHeader;
        r.label = title;
        list.push_back(std::move(r));
    };
    auto addToggle = [](std::vector<panel::Row>& list, UINT id, const std::wstring& label, bool on,
                        bool grayed = false, bool indented = false, bool newCard = false) {
        panel::Row r;
        r.kind = panel::RowKind::Toggle;
        r.id = id;
        r.label = label;
        r.on = on;
        r.grayed = grayed;
        r.indented = indented;
        r.newCard = newCard;
        list.push_back(std::move(r));
    };
    auto addChoice = [](std::vector<panel::Row>& list, UINT popupId, const std::wstring& label,
                        const std::wstring& value, bool grayed = false, bool newCard = false) {
        panel::Row r;
        r.kind = panel::RowKind::Choice;
        r.id = popupId;
        r.label = label;
        r.value = value;
        r.grayed = grayed;
        r.newCard = newCard;
        list.push_back(std::move(r));
    };
    auto addAction = [](std::vector<panel::Row>& list, UINT id, const std::wstring& label,
                        bool grayed = false, bool indented = false, bool newCard = false,
                        const std::wstring& value = L"") {
        panel::Row r;
        r.kind = panel::RowKind::Action;
        r.id = id;
        r.label = label;
        r.value = value;
        r.grayed = grayed;
        r.indented = indented;
        r.newCard = newCard;
        list.push_back(std::move(r));
    };

    // ---- Tab 1: Batteries ----
    auto& bat = out.tabs[(int)panel::Tab::Batteries];
    addHeader(bat, T(L"Devices"));
    std::vector<TrayEntry*> all = OrderedEntries(false), shown = OrderedEntries(true);
    if (all.empty()) {
        addAction(bat, 0, T(L"No devices with battery info found"), true, false, true);
    } else {
        for (TrayEntry* e : all) {
            const std::wstring& k = e->st.key;
            UINT base = ID_DEVICE_FIRST + (UINT)g_panelModel.keys.size() * DEV_STRIDE;
            g_panelModel.keys.push_back(k);
            auto pos = std::find(shown.begin(), shown.end(), e);
            panel::Row r;
            r.kind = panel::RowKind::DeviceCard;
            r.newCard = true;
            r.label = DisplayName(*e);
            r.sublabel = StatusText(*e) + L"  ·  " + T(e->st.via);
            r.glyph = widget::DeviceGlyph(e->st.kind, e->st.via == L"Bluetooth");
            if (g_customIcons) r.vectorIcon = vector_icons::ForDevice(e->st.kind, e->st.via == L"Bluetooth");
            Accent ac = AccentFor(e->st);
            if (ac != Accent::None) {
                r.hasAccent = true;
                r.accentColor = AccentColorRef(ac, g_lightTaskbar);
            }
            r.on = !IsHidden(k);
            r.dim = IsHidden(k) || !e->st.connected || (e->st.asleep && g_dimSleeping);
            r.canUp = pos != shown.end() && pos != shown.begin();
            r.canDown = pos != shown.end() && pos + 1 != shown.end();
            r.id = base + DEV_TOGGLE;
            r.id2 = base + DEV_MORE_POPUP;
            r.upId = base + DEV_LEFT;
            r.downId = base + DEV_RIGHT;
            bat.push_back(std::move(r));
        }
    }
    addAction(bat, ID_DASHBOARD, T(L"Device dashboard..."), false, false, true);
    if (!g_unsupported.empty()) {
        addChoice(bat, ID_POPUP_UNSUPPORTED,
                  TF(L"No battery info ({count})", { { L"count", std::to_wstring(g_unsupported.size()) } }),
                  T(L"View"), false, true);
    }
    addHeader(bat, T(L"Visibility"));
    addToggle(bat, ID_DIM_SLEEPING, T(L"Dim sleeping devices"), g_dimSleeping, false, false, true);
    addToggle(bat, ID_HIDE_LAST_KNOWN, T(L"Hide last known / asleep devices"), g_hideLastKnown);
    addToggle(bat, ID_KEEP_MOUSE_LAST_KNOWN, T(L"Keep last known battery for mice"), g_keepMouseLastKnown,
              !g_hideLastKnown);
    addToggle(bat, ID_SHOW_DISCONNECTED, T(L"Show disconnected devices"), g_showDisconnectedDevices);
    addToggle(bat, ID_TRAY_WHEN_EMPTY, T(L"Move to tray when no devices"), g_trayWhenEmpty, !g_wantWidget);

    addHeader(bat, T(L"Battery alerts"));
    addToggle(bat, ID_RECORD_HISTORY, T(L"Record battery history"), g_recordHistory, false, false, true);
    addChoice(bat, ID_POPUP_CHARGE_LIMIT, T(L"Notify when charged to (saves the battery)"),
              g_chargeLimit ? std::to_wstring(g_chargeLimit) + L"%" : std::wstring(T(L"Off")));
    addToggle(bat, ID_CHARGED_ALERT, T(L"Notify when fully charged"), g_chargedAlert);
    addToggle(bat, ID_TIERED_ALERTS, T(L"Warn again at 10% and 5%"), g_tieredAlerts);
    addToggle(bat, ID_SHORT_RUNTIME_ALERT, T(L"Warn when a battery won't last the day"), g_shortRuntimeAlert,
              !g_recordHistory);
    std::wstring trackVal = (g_trackSince || g_trackAwake || g_trackEmpty) ? T(L"On") : T(L"Off");
    addChoice(bat, ID_POPUP_TRACKING, T(L"Battery tracking"), trackVal);
    addChoice(bat, ID_POPUP_POLL_BATTERY, T(L"Check battery every"),
              FormatDurationLabel((DWORD)(g_pollIntervalMs / 1000)));

    // ---- Tab 2: System ----
    auto& sys = out.tabs[(int)panel::Tab::System];
    addHeader(sys, T(L"System"));
    std::vector<int> visibleSys;
    for (int id : g_systemOrder) {
        if (id == 6 && !g_hasLaptopBat) continue;
        visibleSys.push_back(id);
    }
    for (size_t idx = 0; idx < visibleSys.size(); ++idx) {
        int item = visibleSys[idx];
        bool canUp = idx > 0, canDown = idx + 1 < visibleSys.size();
        UINT upId = ID_SYS_MOVE_FIRST + (UINT)item * 2;
        UINT downId = ID_SYS_MOVE_FIRST + (UINT)item * 2 + 1;
        Accent ac = SystemAccentFor(item);
        panel::Row hdr;
        hdr.kind = panel::RowKind::SystemHeader;
        hdr.newCard = true;
        hdr.canUp = canUp;
        hdr.canDown = canDown;
        hdr.upId = upId;
        hdr.downId = downId;
        if (ac != Accent::None) {
            hdr.hasAccent = true;
            hdr.accentColor = AccentColorRef(ac, g_lightTaskbar);
        }

        if (item == 0) {
            hdr.glyph = 0xE950;
            if (g_customIcons) hdr.vectorIcon = vector_icons::VectorIcon::Cpu;
            hdr.label = T(L"CPU");
            hdr.hasSwitch = false;
            std::vector<std::wstring> parts;
            if (g_showCpu && g_sys.cpu >= 0) parts.push_back(std::to_wstring(g_sys.cpu) + L"%");
            if (g_showCpuTemp && g_cpuTemp.tempC >= 0) parts.push_back(std::to_wstring(g_cpuTemp.tempC) + L" °C");
            if (g_showCpuW && g_sys.cpuWatts >= 0) parts.push_back(std::to_wstring(g_sys.cpuWatts) + L" W");
            bool anyCpu = g_showCpu || g_showCpuTemp || g_showCpuW;
            hdr.value = !anyCpu ? T(L"Off") : (parts.empty() ? L"..." : JoinParts(parts, L" · "));
            hdr.dim = !anyCpu;
            if (g_showCpuTemp && g_cpuTemp.tempC < 0) hdr.sublabel = CpuTempStatusText();
            sys.push_back(std::move(hdr));
            addToggle(sys, ID_SHOW_CPU, T(L"Show CPU usage"), g_showCpu, false, true);
            addToggle(sys, ID_SHOW_CPU_TEMP, T(L"Show CPU temperature"), g_showCpuTemp, false, true);
            if (cputemp::HelperInstalled())
                addAction(sys, ID_REMOVE_TEMP, T(L"Remove the CPU temperature helper..."), false, true);
            addToggle(sys, ID_SHOW_CPU_W, T(L"Show CPU power (watts)"), g_showCpuW, false, true);
        } else if (item == 1) {
            hdr.glyph = 0xE964;
            if (g_customIcons) hdr.vectorIcon = vector_icons::VectorIcon::Ram;
            hdr.label = T(L"RAM usage");
            hdr.hasSwitch = true;
            hdr.on = g_showRam;
            hdr.id = ID_SHOW_RAM;
            hdr.value = !g_showRam ? T(L"Off")
                      : (g_sys.ram >= 0 ? FormatGbPair(g_sys.ram, g_sys.ramUsedGb, g_sys.ramTotalGb) : L"...");
            hdr.dim = !g_showRam;
            sys.push_back(std::move(hdr));
        } else if (item == 2) {
            hdr.glyph = 0xE7F4;
            if (g_customIcons) hdr.vectorIcon = vector_icons::VectorIcon::Gpu;
            hdr.label = L"GPU";
            hdr.hasSwitch = false;
            hdr.sublabel = g_gpu.available ? g_gpu.name : std::wstring();
            std::vector<std::wstring> parts;
            if (g_gpu.available) {
                if (g_showGpu && g_gpu.use >= 0) parts.push_back(std::to_wstring(g_gpu.use) + L"%");
                if (g_showGpuTemp && g_gpu.tempC >= 0) parts.push_back(std::to_wstring(g_gpu.tempC) + L" °C");
                if (g_showGpuW && g_gpu.watts >= 0) parts.push_back(std::to_wstring(g_gpu.watts) + L" W");
                if (g_showGpuClocks) {
                    std::wstring clkStr = gpumon::FormatClocksMhz(g_gpu.gpuClockMhz, g_gpu.vramClockMhz);
                    if (!clkStr.empty()) parts.push_back(clkStr);
                }
            }
            bool anyGpu = g_showGpu || g_showGpuTemp || g_showGpuClocks || g_showGpuW;
            hdr.value = !anyGpu ? T(L"Off") : (parts.empty() ? L"..." : JoinParts(parts, L" · "));
            hdr.dim = !anyGpu;
            sys.push_back(std::move(hdr));
            addToggle(sys, ID_SHOW_GPU, T(L"Show GPU usage"), g_showGpu, false, true);
            addToggle(sys, ID_SHOW_GPU_TEMP, T(L"Show GPU temperature"), g_showGpuTemp, false, true);
            addToggle(sys, ID_SHOW_GPU_CLOCKS, T(L"Show GPU clocks (MHz)"), g_showGpuClocks, false, true);
            addToggle(sys, ID_SHOW_GPU_W, T(L"Show GPU power (watts)"), g_showGpuW, false, true);
        } else if (item == 3) {
            hdr.glyph = 0xE964;
            if (g_customIcons) hdr.vectorIcon = vector_icons::VectorIcon::Vram;
            hdr.label = T(L"GPU memory");
            hdr.hasSwitch = true;
            hdr.on = g_showVram;
            hdr.id = ID_SHOW_VRAM;
            hdr.sublabel = g_gpu.available ? g_gpu.name : std::wstring();
            hdr.value = !g_showVram ? T(L"Off")
                      : (g_gpu.available && g_gpu.vram >= 0 ? FormatGbPair(g_gpu.vram, g_gpu.vramUsedGb, g_gpu.vramTotalGb)
                                                            : L"...");
            hdr.dim = !g_showVram;
            sys.push_back(std::move(hdr));
        } else if (item == 4) {
            hdr.glyph = 0xEDA2;
            if (g_customIcons) hdr.vectorIcon = vector_icons::VectorIcon::Disk;
            hdr.label = T(L"Storage");
            hdr.hasSwitch = false;
            hdr.sublabel = g_disk.available ? g_disk.name : std::wstring();
            std::vector<std::wstring> parts;
            if (g_disk.available) {
                if (g_showDiskUse && g_disk.usePct >= 0) parts.push_back(std::to_wstring(g_disk.usePct) + L"%");
                if (g_showDiskTemp && g_disk.tempC >= 0) parts.push_back(std::to_wstring(g_disk.tempC) + L" °C");
                if ((g_showDiskUse || g_showDiskTemp) && g_disk.totalGb > 0.0)
                    parts.push_back(diskmon::FormatCapacityGb(g_disk.freeGb));
            }
            bool anyDisk = AnyDiskReading();
            hdr.value = !anyDisk ? T(L"Off") : (parts.empty() ? L"..." : JoinParts(parts, L" · "));
            hdr.dim = !anyDisk;
            sys.push_back(std::move(hdr));
            addToggle(sys, ID_SHOW_DISK_TEMP, T(L"Show storage temperature"), g_showDiskTemp, false, true);
            addToggle(sys, ID_SHOW_DISK_USE, T(L"Show storage usage"), g_showDiskUse, false, true);
        } else if (item == 5) {
            hdr.glyph = 0xE839;
            if (g_customIcons) hdr.vectorIcon = vector_icons::VectorIcon::Network;
            hdr.label = T(L"Network");
            hdr.hasSwitch = true;
            hdr.on = g_showNetSpeed;
            hdr.id = ID_SHOW_NET_SPEED;
            hdr.value = !g_showNetSpeed ? T(L"Off")
                      : (g_net.available && g_net.downBytesPerSec >= 0.0
                             ? (L"\u2193 " + netmon::FormatRateCompact(g_net.downBytesPerSec) +
                                L" · \u2191 " + netmon::FormatRateCompact(g_net.upBytesPerSec))
                             : L"...");
            hdr.dim = !g_showNetSpeed;
            sys.push_back(std::move(hdr));
            auto st = netmon::GetSpeedTest();
            std::wstring speedVal;
            bool running = st.state == netmon::TestState::Running;
            if (running) speedVal = T(L"Testing...");
            else if (st.state == netmon::TestState::Done)
                speedVal = L"\u2193 " + netmon::FormatMbps(st.downMbps) + L" · \u2191 " + netmon::FormatMbps(st.upMbps);
            else if (st.state == netmon::TestState::Failed) speedVal = T(L"Failed");
            else speedVal = T(L"Start");
            addAction(sys, ID_RUN_SPEED_TEST, T(L"Run speed test"), running, true, false, speedVal);
        } else if (item == 6) {
            hdr.glyph = 0xEBA0;
            if (g_customIcons) hdr.vectorIcon = vector_icons::VectorIcon::LaptopBattery;
            hdr.label = T(L"Laptop battery");
            hdr.hasSwitch = false;
            if (g_laptopBat.present) {
                std::wstring state = g_laptopBat.charging ? T(L"Charging ⚡")
                                   : g_laptopBat.acOnline ? T(L"Plugged in")
                                                          : T(L"On battery");
                if (!g_laptopBat.charging && g_laptopBat.remainingMinutes > 0)
                    state += L" · " + laptopbat::FormatRemaining(g_laptopBat.remainingMinutes);
                hdr.sublabel = state;
            }
            std::vector<std::wstring> parts;
            if (g_laptopBat.present) {
                if (g_showLaptopBat && g_laptopBat.percent >= 0) parts.push_back(std::to_wstring(g_laptopBat.percent) + L"%");
                if (g_showLaptopBatW && g_laptopBat.hasWatts) parts.push_back(laptopbat::FormatWattsDetailed(g_laptopBat.watts));
            }
            bool anyBat = AnyLaptopBatReading();
            hdr.value = !anyBat ? T(L"Off") : (parts.empty() ? L"..." : JoinParts(parts, L" · "));
            hdr.dim = !anyBat;
            sys.push_back(std::move(hdr));
            addToggle(sys, ID_SHOW_LAPTOP_BAT, T(L"Show laptop battery (%)"), g_showLaptopBat, false, true);
            addToggle(sys, ID_SHOW_LAPTOP_BAT_W, T(L"Show battery power (watts)"), g_showLaptopBatW, false, true);
        }
    }
    addAction(sys, ID_SYSTEM_DASHBOARD, T(L"System vitals dashboard..."), false, false, true);
    addHeader(sys, T(L"Options"));
    addChoice(sys, ID_POPUP_TEMP_ALERT, T(L"High temperature alert"),
              g_tempAlertC ? std::to_wstring(g_tempAlertC) + L" °C" : std::wstring(T(L"Off")), false, true);
    addChoice(sys, ID_POPUP_POLL_SYSTEM, T(L"Check system every"), FormatDurationLabel(g_sysPollSeconds));

    // ---- Tab 3: Appearance ----
    auto& app = out.tabs[(int)panel::Tab::Appearance];
    addHeader(app, T(L"Display mode"));
    {
        panel::Row seg;
        seg.kind = panel::RowKind::Segmented;
        seg.newCard = true;
        seg.label = T(L"Show on the taskbar");
        seg.sublabel = T(L"Show as tray icons");
        seg.on = g_wantWidget;
        seg.id = ID_MODE_WIDGET;
        seg.id2 = ID_MODE_TRAY;
        app.push_back(std::move(seg));
    }
    if (g_myFinder || FindWindowW(L"MyFinderApp", nullptr))
        addToggle(app, ID_MODE_MYFINDER, T(L"Show on MyDockFinder (experimental)"), g_wantWidget && g_myFinder);
    addHeader(app, T(L"Position on the taskbar"));
    addChoice(app, ID_POPUP_BAT_POS, T(L"Battery strip position"),
              StripPlaceLabel(g_farLeft, g_beforeApps), !g_wantWidget || g_myFinder, true);
    addChoice(app, ID_POPUP_VIT_POS, T(L"System strip position"),
              StripPlaceLabel(g_vitalsFarLeft, g_vitalsBeforeApps), !g_wantWidget || !AnyReading() || g_myFinder);
    addChoice(app, ID_POPUP_STRIP_ORDER, T(L"Strip order"),
              g_vitalsFirst ? T(L"System first") : T(L"Batteries first"),
              !g_wantWidget || SplitStrips() || !AnyReading());
    addChoice(app, ID_POPUP_TASKBAR_POS, T(L"Position on the taskbar"), CurrentOffsetLabel(), !g_wantWidget);
    addToggle(app, ID_MAKE_ROOM, T(L"Make room for other taskbar widgets"), g_makeRoom, !g_wantWidget);
    if (widget::Vertical())
        addChoice(app, ID_POPUP_VERTICAL_POS, T(L"Position on a vertical taskbar"), CurrentVerticalLabel(),
                  !g_wantWidget);
    if (!g_panelModel.monitors.empty())
        addChoice(app, ID_POPUP_MONITORS, T(L"Show the widget on"), CurrentMonitorLabel(g_panelModel.monitors),
                  !g_wantWidget);

    addHeader(app, T(L"Style"));
    addToggle(app, ID_CUSTOM_ICONS, T(L"Custom crisp vector icons"), g_customIcons, !g_wantWidget, false, true);
    addToggle(app, ID_TWO_ROWS, T(L"Compact 2-row layout"), g_twoRows, !g_wantWidget);
    addToggle(app, ID_STACKED, T(L"Icons above percentages (narrower)"), g_stacked, !g_wantWidget || g_twoRows);
    addToggle(app, ID_PERCENT_SIGN, T(L"Show percent sign (%)"), g_showPercentSign, !g_wantWidget);
    addToggle(app, ID_RING_STYLE, T(L"Ring style (icon inside a battery ring)"), g_ringStyle,
              !g_wantWidget || g_stacked || g_twoRows);
    addToggle(app, ID_ANIMATE_CHARGING, T(L"Animate charging devices"), g_animateCharging, !g_wantWidget);
    addToggle(app, ID_CONNECTION_ICONS, T(L"Mark devices on a 2.4 GHz receiver"), g_connectionIcons, !g_wantWidget);
    addToggle(app, ID_RUNTIME_WIDGET, T(L"Show estimated time instead of % (when available)"),
              g_runtimeWidget, !g_wantWidget || !g_recordHistory);
    addToggle(app, ID_TRAY_DEVICE_ICONS, T(L"Tray icons show the device type"), g_trayDeviceIcons, g_wantWidget);
    addToggle(app, ID_HOVER_CARD, T(L"Show a card when the mouse rests on a device"), g_hoverCard, !g_wantWidget);
    addToggle(app, ID_HOVER_CHART, T(L"Show live charts in the card"), g_hoverChart, !g_wantWidget || !g_hoverCard);
    addToggle(app, ID_WIDGET_BACKGROUND, T(L"Background behind taskbar text (for transparent taskbars)"),
              g_widgetBackground, !g_wantWidget);
    addToggle(app, ID_BRAND_COLORS, T(L"Brand colors (Razer green, Logitech blue, ...)"), g_brandColors);
    addToggle(app, ID_SYSTEM_COLORS, T(L"System colors (CPU blue, RAM purple, GPU green, ...)"), g_systemColors);

    // ---- Tab 4: General ----
    auto& gen = out.tabs[(int)panel::Tab::General];
    addHeader(gen, T(L"General"));
    addChoice(gen, ID_POPUP_LANG, T(L"Language"), CurrentLanguageLabel(g_panelModel.languages), false, true);
    addToggle(gen, ID_AUTOSTART, T(L"Start with Windows"), AutostartEnabled());
    addToggle(gen, ID_AUTO_UPDATE, T(L"Automatically check for updates"), updates::AutomaticEnabled(),
              !updates::Supported());
    addAction(gen, ID_UPDATE, updates::MenuLabel(), updates::Busy(), false, true);
    addAction(gen, ID_REPORT, T(L"Request support for a device..."));
    addAction(gen, ID_SUPPORT, T(L"Support the project..."));

    // ---- Footer ----
    out.footer.push_back({ T(L"Refresh now"), 0xE72C, ID_REFRESH });
    out.footer.push_back({ T(L"Exit"), 0xE711, ID_EXIT });
}

UINT PanelPopup(UINT id, HWND owner, POINT screenPt) {
    ApplyMenuTheme(g_lightTaskbar);
    MenuIcons icons(g_lightTaskbar);
    HMENU sub = nullptr;
    if (id >= ID_DEVICE_FIRST && id < ID_DEVICE_FIRST + g_panelModel.keys.size() * DEV_STRIDE) {
        size_t idx = (id - ID_DEVICE_FIRST) / DEV_STRIDE;
        const std::wstring& key = g_panelModel.keys[idx];
        auto it = g_entries.find(key);
        if (it != g_entries.end()) {
            sub = BuildDeviceSubmenu(it->second, ID_DEVICE_FIRST + (UINT)idx * DEV_STRIDE, false, false, &icons, false);
        }
    } else {
        switch (id) {
            case ID_POPUP_CHARGE_LIMIT: sub = BuildChargeLimitMenu(); break;
            case ID_POPUP_TRACKING:     sub = BuildTrackingMenu(); break;
            case ID_POPUP_UNSUPPORTED:  sub = BuildUnsupportedMenu(); break;
            case ID_POPUP_POLL_BATTERY: sub = BuildPollBatteryMenu(); break;
            case ID_POPUP_TEMP_ALERT:   sub = BuildTempAlertMenu(); break;
            case ID_POPUP_POLL_SYSTEM:  sub = BuildPollSystemMenu(); break;
            case ID_POPUP_BAT_POS:      sub = BuildStripPlaceMenu(ID_BAT_LEFT, ID_BAT_BEFORE, ID_BAT_CLOCK, g_farLeft, g_beforeApps, g_wantWidget && !g_myFinder); break;
            case ID_POPUP_VIT_POS:      sub = BuildStripPlaceMenu(ID_VIT_LEFT, ID_VIT_BEFORE, ID_VIT_CLOCK, g_vitalsFarLeft, g_vitalsBeforeApps, g_wantWidget && AnyReading() && !g_myFinder); break;
            case ID_POPUP_STRIP_ORDER:  sub = BuildStripOrderMenu(); break;
            case ID_POPUP_TASKBAR_POS:  sub = BuildTaskbarOffsetMenu(false); break;
            case ID_POPUP_VERTICAL_POS: sub = BuildVerticalPlaceMenu(); break;
            case ID_POPUP_MONITORS:     sub = BuildMonitorMenu(g_panelModel.monitors); break;
            case ID_POPUP_LANG:         sub = BuildLanguageMenu(g_panelModel.languages); break;
        }
    }
    if (!sub) return 0;
    UINT cmd = TrackPopupMenuEx(sub, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_TOPALIGN |
                                    (i18n::Rtl() ? TPM_LEFTALIGN | TPM_LAYOUTRTL : TPM_RIGHTALIGN),
                                screenPt.x, screenPt.y, owner, nullptr);
    DestroyMenu(sub);
    memory_trim::Soon();
    return cmd;
}

void PanelRun(UINT cmd) { RunCommand(cmd, g_panelModel); }

bool PanelLight() { return g_lightTaskbar; }

// Left or right click on the strip or the tray icon: the settings panel; with Shift held, the full menu.
void OpenSettings() {
    if (GetKeyState(VK_SHIFT) < 0) {
        ShowMenu();
        return;
    }
    panel::Host host;
    host.build = PanelBuild;
    host.run = PanelRun;
    host.popup = PanelPopup;
    host.light = PanelLight;
    panel::Show(g_inst, host);
}

// ---- worker thread ----------------------------------------------------------------------------

// `--demo`: one sample device of every brand instead of real ones, for screenshots and for checking the
// brand colors without owning the hardware. Keys, vendor ids and names are mixed on purpose, so every way
// of recognising a brand gets used.
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

std::vector<DeviceStatus> PollWithRetry() {
    std::vector<DeviceStatus> first = PollAllDevices();
    bool incomplete = first.empty();
    for (const auto& s : first)
        if (s.connected && !s.asleep && s.percent < 0 && s.levelText.empty()) incomplete = true;
    long known = g_lastCount.load();
    if (known > 0 && (long)first.size() < known) incomplete = true;
    if (!incomplete || (first.empty() && known == 0)) {
        g_lastCount = (long)first.size();
        return first;
    }
    if (WaitForSingleObject(g_stopEvt, 250) == WAIT_OBJECT_0) return first;
    std::vector<DeviceStatus> second = PollAllDevices();
    std::set<std::wstring> keys;
    for (const auto& s : second) keys.insert(s.key);
    for (const auto& s : first) {
        if (keys.count(s.key)) continue;
        second.push_back(s);
        keys.insert(s.key);
    }
    g_lastCount = (long)second.size();
    return second;
}

void TracePoll(const std::vector<DeviceStatus>& v) {
    if (!g_trace) return;
    wchar_t path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, path)) return;
    wcscat_s(path, L"wirelessbattery-trace.log");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || !f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(f, L"%02d:%02d:%02d.%03d  poll: %zu device(s)", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, v.size());
    for (const auto& s : v) fwprintf(f, L"  [%ls %d%%%ls]", s.key.c_str(), s.percent, s.asleep ? L" asleep" : L"");
    fwprintf(f, L"\n");
    fclose(f);
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

        auto* v = new std::vector<DeviceStatus>(g_demo ? DemoDevices() : PollWithRetry());
        TracePoll(*v);
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
                for (auto& src : g_sources) src->DevicesChanged();
            } else if (handle && (wp == DBT_DEVICEQUERYREMOVE || wp == DBT_DEVICEREMOVEPENDING)) {
                DropBluetoothRadioWatch(reinterpret_cast<const DEV_BROADCAST_HANDLE*>(lp)->dbch_handle);
            } else if (handle && wp == DBT_DEVICEQUERYREMOVEFAILED) {
                RefreshBluetoothRadioWatches();
            } else if (handle && wp == DBT_CUSTOMEVENT) {
                auto* bh = reinterpret_cast<const DEV_BROADCAST_HANDLE*>(lp);
                constexpr UCHAR kHciAcl = 1, kHciSco = 2;
                BTH_HCI_EVENT_INFO info{};
                if (bh->dbch_eventguid == GUID_BLUETOOTH_HCI_EVENT &&
                    bh->dbch_size >= offsetof(DEV_BROADCAST_HANDLE, dbch_data) + sizeof(info)) {
                    memcpy(&info, bh->dbch_data, sizeof(info));
                    if (info.connectionType != kHciSco) {
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
                for (auto& src : g_sources) src->DevicesChanged();
                TickSystemReadings(true);
            } else if (wp == PBT_POWERSETTINGCHANGE && lp) {
                auto* ps = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
                if (ps->PowerSetting == kConsoleDisplayStateGuid && ps->DataLength >= sizeof(DWORD)) {
                    DWORD state = *reinterpret_cast<const DWORD*>(ps->Data);
                    bool wokeDisplay = g_displayState == 0 && state != 0;
                    if (g_displayState != 0 && state == 0) g_awakeBeforeSleep = AwakeDevices();
                    g_displayState = state;
                    if (wokeDisplay) {
                        RefreshAfterWake(3000);
                        TickSystemReadings(true);
                    }
                }
            }
            return TRUE;
        case WM_DEVICE_EVENT:
            RefreshSoon((UINT)lp);
            return 0;
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
            dashboard::Close();
            battery_intelligence::Shutdown();
            for (auto& src : g_sources) src->Stop();
            gpumon::Stop();
            diskmon::Stop();
            netmon::Stop();
            laptopbat::Stop();
            SetEvent(g_stopEvt);
            if (g_worker) WaitForSingleObject(g_worker, 5000);
            ShutdownDevices();
            for (auto& [k, e] : g_entries) {
                if (e.added) TrayDelete(e.uid);
                if (e.icon) DestroyIcon(e.icon);
            }
            for (int i = 0; i < 7; ++i) {
                if (g_sysTray[i].added) TrayDelete(SYS_TRAY_UID_BASE + i);
                if (g_sysTray[i].icon) DestroyIcon(g_sysTray[i].icon);
            }
            if (g_placeholder) TrayDelete(PLACEHOLDER_ID);
            DeleteFileW(StatusSnapshotPath().c_str());
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

std::wstring ListAsText(const std::vector<DeviceStatus>& list) {
    g_hideLastKnown = LoadHideLastKnown();
    g_keepMouseLastKnown = LoadBoolSetting(L"KeepMouseLastKnown", false);
    LoadHidden();
    LoadOrder();
    g_entries.clear();
    for (const auto& s : list) g_entries[s.key].st = s;
    std::vector<WidgetItem> items;
    for (const TrayEntry* e : OrderedEntries(true)) {
        WidgetItem it;
        it.kind = e->st.kind;
        it.percent = e->st.connected ? e->st.percent : -1;
        if (e->st.percent >= 0) it.text = e->st.levelText;
        it.charging = e->st.connected && !e->st.asleep && e->st.charging;
        it.dim = ShouldDim(e->st);
        it.low = !it.charging && it.percent >= 0 && it.percent <= LOW_BATTERY;
        it.bluetooth = e->st.via == L"Bluetooth";
        items.push_back(it);
    }
    return widget::ItemsToText(items);
}

// `TaskbarVitals.exe --list` prints what it finds, `--dump` every HID collection, `--text` single line.
int ListToConsole(bool dump, bool text = false) {
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
        std::wstring dumpText = DumpHidCollections();
        wchar_t path[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, path);
        wcscat_s(path, L"TaskbarVitals-dump.txt");
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            std::wstring crlf;
            for (wchar_t ch : dumpText) crlf += ch == L'\n' ? std::wstring(L"\r\n") : std::wstring(1, ch);
            int n = WideCharToMultiByte(CP_UTF8, 0, crlf.c_str(), (int)crlf.size(), nullptr, 0, nullptr, nullptr);
            std::string u = "\xEF\xBB\xBF" + std::string(n, 0);
            WideCharToMultiByte(CP_UTF8, 0, crlf.c_str(), (int)crlf.size(), u.data() + 3, n, nullptr, nullptr);
            DWORD w;
            WriteFile(f, u.data(), (DWORD)u.size(), &w, nullptr);
            CloseHandle(f);
        }
        if (out && out != INVALID_HANDLE_VALUE) {
            write(dumpText);
            write(std::wstring(L"\nAlso saved to ") + path + L"\n");
        } else {
            ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    auto list = PollAllDevices();
    list.erase(std::remove_if(list.begin(), list.end(), [](const DeviceStatus& s) { return s.unsupported; }), list.end());
    if (text) {
        std::wstring line = ListAsText(list);
        if (!line.empty()) write(line + L"\n");
        ShutdownDevices();
        return 0;
    }
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
        if (!s.note.empty() && s.percent < 0) line += L", " + s.note;
        if (s.millivolts) line += L", " + std::to_wstring(s.millivolts) + L" mV";
        write(line + L"   (" + s.key + L")\n");
    }
    ShutdownDevices();
    return 0;
}

// Machine-readable device list, shared with the Linux entry point (src/json_output.h).
int ListJson() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    auto devices = PollAllDevices();
    devices.erase(std::remove_if(devices.begin(), devices.end(), [](const DeviceStatus& s) { return s.unsupported; }),
                  devices.end());
    std::string text = DevicesToJson(devices);  // UTF-8
    if (out && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, text.data(), (DWORD)text.size(), &written, nullptr);
    }
    ShutdownDevices();
    return 0;
}

int EventsToConsole(const wchar_t* args) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    auto write = [out](const std::wstring& s) {
        std::wstring line = s + L"\n";
        int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
        std::string u(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), u.data(), n, nullptr, nullptr);
        DWORD w;
        WriteFile(out, u.data(), (DWORD)u.size(), &w, nullptr);
    };

    for (const HidColl& c : EnumHid()) {
        if (c.vid != 0x046D || c.page() != 0xFF00 || c.usage() != 0x0001 || c.outLen() != 7) continue;
        HidDev d;
        if (!d.Open(c)) continue;
        BYTE req[7];
        hidpp::NotificationFlagsRequest(req);
        d.Flush();
        std::wstring state = L"no answer (not a receiver?)";
        if (d.Write(std::vector<BYTE>(req, req + 7))) {
            std::vector<BYTE> r;
            for (ULONGLONG end = GetTickCount64() + 500; GetTickCount64() < end;) {
                std::uint32_t flags = 0;
                if (d.Read(r, 50) > 0 && hidpp::ParseNotificationFlags(r.data(), r.size(), flags)) {
                    state = (flags & hidpp::kWirelessNotifications)
                                ? L"on"
                                : L"OFF (connection events need G HUB, Options+ or Solaar to switch them on)";
                    break;
                }
            }
        }
        write(L"receiver " + Hex4(c.vid) + L":" + Hex4(c.pid) + L" wireless notifications: " + state);
    }

    struct ConsoleSink final : IRefreshSink {
        std::function<void(const std::wstring&)> write;
        void RequestRefresh(std::uint32_t delayMs) override {
            write(L"  => refresh requested (in " + std::to_wstring(delayMs) + L" ms)");
        }
    } sink;
    sink.write = write;
    HidEventListener listener(HidEventDecoders(), write);
    if (!listener.Start(sink)) return 1;
    int seconds = (int)wcstol(wcsstr(args, L"--events") + 8, nullptr, 10);
    if (seconds <= 0) seconds = 60;
    write(L"listening for " + std::to_wstring(seconds) + L" s: switch a wireless device off and on...");
    Sleep((DWORD)seconds * 1000);
    listener.Stop();
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && wcsstr(cmdLine, L"--events")) return EventsToConsole(cmdLine);
    if (cmdLine && wcsstr(cmdLine, L"--json")) return ListJson();
    if (cmdLine && wcsstr(cmdLine, L"--help")) {
        MessageBoxW(nullptr,
                    L"Taskbar Vitals options:\n\n"
                    L"--list   Print devices and battery levels\n"
                    L"--json   Print a JSON array of devices and battery levels\n"
                    L"--text   Print a single-line status summary (for YASB/Rainmeter)\n"
                    L"--dump   Print every HID collection and device\n"
                    L"--events Print push events from wireless receivers (60 s)\n"
                    L"--report Open the \"Request support for a device\" window\n"
                    L"--demo   Show sample devices\n"
                    L"--trace  Log every poll to %TEMP%\\wirelessbattery-trace.log\n"
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
    if (cmdLine && wcsstr(cmdLine, L"--text")) return ListToConsole(false, true);
    if (cmdLine && wcsstr(cmdLine, L"--dump")) return ListToConsole(true);
    if (cmdLine && wcsstr(cmdLine, L"--report")) {
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
    if (cmdLine && wcsstr(cmdLine, L"--quit")) {
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
    g_trace = cmdLine && wcsstr(cmdLine, L"--trace");

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
    filter.dbcc_classguid = GUID_BTHPORT_DEVICE_INTERFACE;
    HDEVNOTIFY radioNotify = RegisterDeviceNotificationW(g_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    RefreshBluetoothRadioWatches();
    HPOWERNOTIFY displayNotify =
        RegisterPowerSettingNotification(g_hwnd, &kConsoleDisplayStateGuid, DEVICE_NOTIFY_WINDOW_HANDLE);

    g_refreshEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_stopEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_wantWidget = LoadWidgetMode();
    g_myFinder = LoadBoolSetting(L"MyFinderWidget", false);
    g_hideLastKnown = LoadHideLastKnown();
    g_keepMouseLastKnown = LoadBoolSetting(L"KeepMouseLastKnown", false);
    g_trayWhenEmpty = LoadTrayWhenEmpty();
    g_showDisconnectedDevices = LoadShowDisconnectedDevices();
    g_dimSleeping = LoadDimSleeping();
    g_recordHistory = LoadRecordHistory();
    g_chargeLimit = LoadChargeLimit();
    g_chargedAlert = LoadBoolSetting(L"ChargedAlert", true);
    g_tieredAlerts = LoadBoolSetting(L"TieredAlerts", true);
    g_shortRuntimeAlert = LoadBoolSetting(L"ShortRuntimeAlert", true);
    g_hoverCard = LoadBoolSetting(L"HoverCard", true);
    g_hoverChart = LoadBoolSetting(L"HoverCardChart", true);
    g_trackSince = LoadTrackSince();
    g_trackAwake = LoadTrackAwake();
    g_trackEmpty = LoadTrackEmpty();
    g_runtimeWidget = LoadRuntimeWidget();
    g_trayDeviceIcons = LoadBoolSetting(L"TrayDeviceIcons", false);
    g_animateCharging = LoadBoolSetting(L"AnimateCharging", false);
    g_widgetBackground = LoadWidgetBackground();
    g_brandColors = LoadBrandColors();
    g_stacked = LoadStacked();
    g_twoRows = LoadBoolSetting(L"TwoRowsLayout", false);
    g_customIcons = LoadBoolSetting(L"CustomVectorIcons", true);
    g_showPercentSign = LoadShowPercentSign();
    g_ringStyle = LoadRingStyle();
    g_connectionIcons = LoadConnectionIcons();
    g_makeRoom = LoadMakeRoom();
    g_verticalPlace = LoadVerticalPlace();
    g_farLeft = LoadFarLeft();
    g_beforeApps = LoadBeforeApps() && !g_farLeft;
    g_vitalsFarLeft = LoadBoolSetting(L"VitalsFarLeft", false);
    g_vitalsBeforeApps = LoadBoolSetting(L"VitalsBeforeApps", false) && !g_vitalsFarLeft;
    g_vitalsFirst = LoadBoolSetting(L"VitalsFirst", false);
    g_showCpu = LoadShowCpu();
    g_showRam = LoadShowRam();
    g_showGpu = LoadBoolSetting(L"ShowGpu", false);
    g_showGpuTemp = LoadBoolSetting(L"ShowGpuTemp", false);
    g_showGpuClocks = LoadBoolSetting(L"ShowGpuClocks", false);
    g_showCpuW = LoadBoolSetting(L"ShowCpuWatts", false);
    g_showGpuW = LoadBoolSetting(L"ShowGpuWatts", false);
    g_showCpuTemp = LoadBoolSetting(L"ShowCpuTemp", false) && cputemp::HelperInstalled();
    if (g_showCpuTemp) cputemp::StartHelper();
    g_showVram = LoadBoolSetting(L"ShowVram", false);
    g_showDiskTemp = LoadBoolSetting(L"ShowDiskTemp", false);
    g_showDiskUse = LoadBoolSetting(L"ShowDiskUse", false);
    g_showNetSpeed = LoadBoolSetting(L"ShowNetSpeed", false);
    g_laptopBat = laptopbat::Sample();
    g_hasLaptopBat = g_laptopBat.present;
    g_showLaptopBat = LoadBoolSetting(L"ShowLaptopBat", false);
    g_showLaptopBatW = LoadBoolSetting(L"ShowLaptopBatWatts", false);
    g_systemColors = LoadBoolSetting(L"SystemColors", false);
    g_sysPollSeconds = LoadSystemPollSeconds();
    g_tempAlertC = LoadTempAlertCelsius();
    g_pollIntervalMs = LoadPollSeconds() * 1000;
    i18n::Init();
    g_widgetMonitor = LoadWidgetMonitor();
    ApplyStripOptions();
    LoadHidden();
    LoadOrder();
    LoadSystemOrder();
    LoadAliases();
    LoadAlertThresholds();
    battery_history::Initialize(g_demo);
    battery_intelligence::Initialize(g_demo);
    dashboard::SetSpeedTestHandler(StartNetSpeedTest);
    Reconcile();
    if (AnyReading()) TickSystemReadings(true);
    UpdatePlaceholder();  // something to click on until the first poll finishes
    SetTimer(g_hwnd, TIMER_WIDGET, 1000, nullptr);
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!g_demo) {
        g_sources.push_back(std::make_unique<HidEventListener>(HidEventDecoders()));
        for (auto& src : g_sources) src->Start(g_refreshSink);
    }

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
