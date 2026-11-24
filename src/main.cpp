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
#include "command_routing.h"
#include "devices.h"
#include "battery_history.h"
#include "battery_intelligence.h"
#include "charge_estimate.h"
#include "dashboard.h"
#include "evening_reminder.h"
#include "file_dialog.h"
#include "i18n.h"
#include "ignore_devices.h"
#include "json_output.h"
#include "labels.h"
#include "live_api_json.h"
#include "live_api_pipe.h"
#include "osd_overlay.h"
#include "osd_policy.h"
#include "reading_freshness.h"
#include "status_json.h"
#include "refresh_policy.h"
#include "settings_port.h"
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
#include "system_order.h"
#include "vector_icons.h"
#include "vitals_strip.h"
#include "widget.h"
#include "update_win.h"
#include "update_apply.h"
#include "app_commands.h"
#include "app_settings.h"
#include "app_state.h"
#include "notifications.h"

namespace app {


constexpr GUID kConsoleDisplayStateGuid = {
    0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 }
};


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
int g_eveningReminderHour = 0;       // "Evening charge reminder": 0 = off, or 20..23
int g_eveningReminderLastCycle = 0;  // YYYYMMDD of the last evening charge reminder shown
int g_eveningReminderCheckedCycle = 0;
ULONGLONG g_eveningReminderCheckedTick = 0;
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
sysmon::TempPreset g_tempPreset = sysmon::TempPreset::Standard;
int g_tempAlertC = 0;  // 0 = off, kTempAlertMatchPreset = active preset's high threshold, or explicit °C
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
std::atomic<bool> g_pollLessFullscreen{ true };
bool g_localApiPipe = false;
bool g_settingsDialogOpen = false;
osd_policy::OsdMode g_osdMode = osd_policy::OsdMode::FullscreenOnly;
osd_policy::OsdCorner g_osdCorner = osd_policy::OsdCorner::TopRight;
alerts::AlertSound g_alertSound = alerts::AlertSound::Off;

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

ULONGLONG NowSec() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000;
}

SysTraySlot g_sysTray[7];

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

std::wstring ExePath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

// Devices the user chose to hide (by key), stored as a REG_MULTI_SZ.
std::set<std::wstring> g_hidden;

bool IsHidden(const std::wstring& key) { return g_hidden.count(key) != 0; }

// The user's left-to-right order (device keys), stored as a REG_MULTI_SZ. Devices not in it follow
// in the default order (by type, then name).
std::vector<std::wstring> g_order;

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

// A REG_MULTI_SZ with one "device-key<TAB>threshold" entry per override.
// Threshold -1 disables alerts; a missing key uses the default 15%.
std::map<std::wstring, int> g_alertOverrides;

HWND DialogOwner() {
    HWND active = GetActiveWindow();
    if (active && IsWindowVisible(active) && IsWindowEnabled(active)) return active;
    if (IsWindowVisible(g_hwnd) && IsWindowEnabled(g_hwnd)) return g_hwnd;
    return nullptr;
}

refresh_policy::UserNotificationState QueryNotificationState() {
    QUERY_USER_NOTIFICATION_STATE quns = QUNS_ACCEPTS_NOTIFICATIONS;
    if (FAILED(SHQueryUserNotificationState(&quns)))
        return refresh_policy::UserNotificationState::Unknown;
    switch (quns) {
        case QUNS_NOT_PRESENT:             return refresh_policy::UserNotificationState::NotPresent;
        case QUNS_BUSY:                    return refresh_policy::UserNotificationState::Busy;
        case QUNS_RUNNING_D3D_FULL_SCREEN: return refresh_policy::UserNotificationState::RunningD3DFullScreen;
        case QUNS_PRESENTATION_MODE:       return refresh_policy::UserNotificationState::PresentationMode;
        case QUNS_ACCEPTS_NOTIFICATIONS:   return refresh_policy::UserNotificationState::AcceptsNotifications;
        case QUNS_QUIET_TIME:              return refresh_policy::UserNotificationState::QuietTime;
        case QUNS_APP:                     return refresh_policy::UserNotificationState::App;
        default:                           return refresh_policy::UserNotificationState::Unknown;
    }
}

// ---- icon rendering ---------------------------------------------------------------------------
// Draws the percentage as large digits with a small level bar underneath. GDI text is rendered
// white-on-black into a scratch bitmap and its brightness is used as the alpha channel, which
// gives smooth edges on the transparent icon.


// deviceGlyph: `text` is a Segoe Fluent Icons / MDL2 glyph (tray device icons), drawn regular weight.
HICON RenderIcon(const std::wstring& text, int barPct, Rgb textColor, Rgb barColor, bool dim, bool deviceGlyph) {
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

void RefreshLiveApiSnapshot() {
    if (!g_localApiPipe) return;
    std::vector<DeviceStatus> devices;
    for (TrayEntry* e : OrderedEntries(false)) devices.push_back(e->st);
    live_api_json::SystemMetricsSnapshot sys;
    sys.showCpu = g_showCpu;
    sys.cpuPercent = g_sys.cpu;
    sys.showCpuTemp = g_showCpuTemp;
    sys.cpuTempC = g_cpuTemp.tempC;
    sys.showCpuWatts = g_showCpuW;
    sys.cpuWatts = g_sys.cpuWatts;
    sys.showRam = g_showRam;
    sys.ramPercent = g_sys.ram;
    sys.ramUsedGb = g_sys.ramUsedGb;
    sys.ramTotalGb = g_sys.ramTotalGb;
    sys.gpuAvailable = AnyGpuReading() && g_gpu.available;
    sys.gpuName = g_gpu.name;
    sys.showGpu = g_showGpu;
    sys.gpuPercent = g_gpu.use;
    sys.showGpuTemp = g_showGpuTemp;
    sys.gpuTempC = g_gpu.tempC;
    sys.showGpuWatts = g_showGpuW;
    sys.gpuWatts = g_gpu.watts;
    sys.showVram = g_showVram;
    sys.vramPercent = g_gpu.vram;
    sys.vramUsedGb = g_gpu.vramUsedGb;
    sys.vramTotalGb = g_gpu.vramTotalGb;
    sys.showLaptopBattery = AnyLaptopBatReading() && g_showLaptopBat;
    sys.laptopBatteryPresent = g_laptopBat.present;
    sys.laptopBatteryPercent = g_laptopBat.percent;
    sys.laptopBatteryCharging = g_laptopBat.charging;
    sys.laptopBatteryFull = g_laptopBat.acOnline && g_laptopBat.percent >= 100 && !g_laptopBat.charging;
    sys.laptopBatteryRemainingSec = (!g_laptopBat.charging && g_laptopBat.remainingMinutes > 0)
                                        ? g_laptopBat.remainingMinutes * 60
                                        : -1;
    sys.showNetwork = g_showNetSpeed;
    sys.netValid = g_net.available && g_net.downBytesPerSec >= 0.0 && g_net.upBytesPerSec >= 0.0;
    if (sys.netValid) {
        sys.netRxBps = static_cast<std::uint64_t>(g_net.downBytesPerSec + 0.5);
        sys.netTxBps = static_cast<std::uint64_t>(g_net.upBytesPerSec + 0.5);
    }
    live_api_pipe::UpdateSnapshot(live_api_json::BuildLiveSnapshotJson(devices, sys));
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
            heat = sysmon::HeatLevel(cpuTemp, g_tempPreset);
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
            heat = sysmon::HeatLevel(gpuTemp, g_tempPreset);
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
        readings.back().heat = sysmon::HeatLevel(cpuTemp, g_tempPreset);
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
        readings.back().heat = sysmon::HeatLevel(gpuTemp, g_tempPreset);
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
    const auto ignored = ignore_devices::Parse(LoadIgnoreDevicesRaw());
    if (!ignored.empty()) {
        results.erase(std::remove_if(results.begin(), results.end(), [&](const DeviceStatus& s) {
                          return s.vid && s.pid && ignore_devices::Contains(ignored, s.vid, s.pid);
                      }),
                      results.end());
    }
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
    RefreshLiveApiSnapshot();
    CheckEveningReminder(true);
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

void ReAddAll() {
    g_placeholder = false;
    for (auto& [k, e] : g_entries) { e.added = false; ApplyEntry(e); }
    for (auto& s : g_sysTray) s.added = false;
    UpdateSystemTrayIcons();
    UpdatePlaceholder();
}

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

void TickSystemReadings(bool now) {
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
    const int tempAlertC = sysmon::EffectiveTempAlertCelsius(g_tempAlertC, g_tempPreset);
    if (tempAlertC > 0) {
        auto checkTempAlert = [&](int tempC, const std::wstring& name, bool& warned) {
            if (tempC < 0) return;
            if (tempC >= tempAlertC && !warned) {
                warned = true;
                ShowTextNotification(T(L"High temperature"),
                                     TF(L"{name} reached {temp} °C",
                                        { { L"name", name }, { L"temp", std::to_wstring(tempC) } }));
            } else if (tempC <= tempAlertC - 5) {
                warned = false;
            }
        };
        if (g_showCpuTemp) checkTempAlert(g_cpuTemp.tempC, T(L"CPU"), g_cpuTempWarned);
        if (g_showGpuTemp && g_gpu.available) checkTempAlert(g_gpu.tempC, L"GPU", g_gpuTempWarned);
        if (g_showDiskTemp && g_disk.available) checkTempAlert(g_disk.tempC, T(L"Storage"), g_diskTempWarned);
    }
    if (wantDash) dashboard::UpdateVitals(BuildDashboardVitals());
    if (changed) {
        UpdateWidget();
        UpdateSystemTrayIcons();
        UpdatePlaceholder();
        RefreshLiveApiSnapshot();
        panel::Refresh();
    }
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
        DWORD baseMs = g_pollIntervalMs.load();
        bool pollLess = g_pollLessFullscreen.load();
        bool fgFs = pollLess ? osd_overlay::IsForegroundFullscreen() : false;
        refresh_policy::UserNotificationState state =
            pollLess ? QueryNotificationState() : refresh_policy::UserNotificationState::Unknown;
        DWORD effectiveMs = refresh_policy::EffectivePollIntervalMs(baseMs, pollLess, fgFs, state);
        DWORD delay = monitor.DelayAfterPoll(*v, effectiveMs);
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
            } else if (wp == PBT_APMPOWERSTATUSCHANGE) {
                laptopbat::Reading b = laptopbat::Sample();
                g_laptopBat = b;
                g_hasLaptopBat = b.present;
                if (AnyLaptopBatReading() || dashboard::IsOpen()) {
                    SyncVitalsStrip();
                    UpdateWidget();
                    UpdateSystemTrayIcons();
                    UpdatePlaceholder();
                    if (dashboard::IsOpen()) dashboard::UpdateVitals(BuildDashboardVitals());
                }
                RefreshLiveApiSnapshot();
                panel::Refresh();
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
            if (wp == TIMER_WIDGET) { Reconcile(); TickSystemReadings(); updates::Tick(h); CheckEveningReminder(); }
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
            live_api_pipe::Stop();
            KillTimer(h, TIMER_DEBOUNCE);
            KillTimer(h, TIMER_RESUME);
            KillTimer(h, TIMER_WIDGET);
            KillTimer(h, TIMER_ALERT_ICON);
            if (g_alertIconAdded) { TrayDelete(ALERT_ICON_ID); g_alertIconAdded = false; }
            panel::Close();
            FreeAlertSoundLib();
            osd_overlay::Shutdown();
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

}  // namespace app

using namespace app;

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
    g_eveningReminderHour = LoadEveningReminderHour();
    g_eveningReminderLastCycle = LoadEveningReminderLastCycle();
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
    g_tempPreset = LoadTempPreset();
    g_tempAlertC = LoadTempAlertCelsius();
    g_pollIntervalMs = LoadPollSeconds() * 1000;
    g_pollLessFullscreen = LoadBoolSetting(L"PollLessFullscreen", true);
    g_alertSound = LoadAlertSound();
    g_osdMode = LoadOsdMode();
    g_osdCorner = LoadOsdCorner();
    g_localApiPipe = LoadBoolSetting(L"LocalApiPipe", false);
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
    if (g_localApiPipe) {
        RefreshLiveApiSnapshot();
        live_api_pipe::StartServer();
    }
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
