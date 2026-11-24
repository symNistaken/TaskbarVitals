// Win32 context menu builders, settings panel host, setters, and command routing for Taskbar Vitals.
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

void SetWidgetMonitor(const std::wstring& v) {
    g_widgetMonitor = v;
    SaveWidgetMonitor(v);
    widget::SetMonitorChoice(v);
    vitals_strip::SetMonitorChoice(v);
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
    g_diskTempWarned = false;
    panel::Refresh();
    TickSystemReadings(true);
}

void SetTempPreset(sysmon::TempPreset preset) {
    g_tempPreset = preset;
    SaveTempPreset(preset);
    g_cpuTempWarned = false;
    g_gpuTempWarned = false;
    g_diskTempWarned = false;
    UpdateWidget();
    UpdateSystemTrayIcons();
    panel::Refresh();
    TickSystemReadings(true);
}

void SetChargeLimit(int limit) {
    g_chargeLimit = limit;
    DWORD v = (DWORD)limit;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"ChargeLimitAlert", REG_DWORD, &v, sizeof(v));
    for (auto& [k, e] : g_entries) e.limitWarned = false;
}

void SetEveningReminderHour(int hour) {
    g_eveningReminderHour = evening_reminder::ClampReminderHour(hour);
    SaveEveningReminderHour(g_eveningReminderHour);
    if (g_eveningReminderHour != 0) CheckEveningReminder(true);
}

void SetPollSeconds(DWORD seconds) {
    g_pollIntervalMs = seconds * 1000;
    SavePollSeconds(seconds);
    SetEvent(g_refreshEvt);  // poll now, so the new interval starts right away
}

void SetPollLessFullscreen(bool on) {
    g_pollLessFullscreen = on;
    SavePollLessFullscreen(on);
    SetEvent(g_refreshEvt);
}

void SetOsdMode(osd_policy::OsdMode mode) {
    g_osdMode = mode;
    SaveOsdMode(mode);
    if (mode == osd_policy::OsdMode::Off) osd_overlay::Shutdown();
}

void SetOsdCorner(osd_policy::OsdCorner corner) {
    g_osdCorner = corner;
    SaveOsdCorner(corner);
}

void SetAlertSound(alerts::AlertSound mode) {
    g_alertSound = mode;
    SaveAlertSound(mode);
    if (mode == alerts::AlertSound::Off) FreeAlertSoundLib();
}

void SetLocalApiPipe(bool on) {
    g_localApiPipe = on;
    SaveLocalApiPipe(on);
    if (on) {
        RefreshLiveApiSnapshot();
        live_api_pipe::StartServer();
    } else {
        live_api_pipe::Stop();
    }
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

void IgnoreDevice(const std::wstring& key) {
    auto it = g_entries.find(key);
    if (it == g_entries.end()) return;
    const std::uint16_t vid = it->second.st.vid;
    const std::uint16_t pid = it->second.st.pid;
    if (!vid || !pid) return;
    const std::wstring name = DisplayName(it->second);
    const std::wstring vidPid = ignore_devices::FormatPair(vid, pid);
    const std::wstring prompt = TF(
        L"Ignore {name} ({id})?\n\n"
        L"The device will be hidden from Taskbar Vitals until you unignore it in Settings -> Batteries -> Ignored devices.",
        { { L"name", name }, { L"id", vidPid } });
    if (MessageBoxW(DialogOwner(), prompt.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) != IDYES)
        return;
    SaveIgnoreDevicesRaw(ignore_devices::Add(LoadIgnoreDevicesRaw(), vid, pid));
    for (auto ei = g_entries.begin(); ei != g_entries.end();) {
        if (ei->second.st.vid == vid && ei->second.st.pid == pid) {
            if (ei->second.added) TrayDelete(ei->second.uid);
            if (ei->second.icon) DestroyIcon(ei->second.icon);
            ei = g_entries.erase(ei);
        } else {
            ++ei;
        }
    }
    Reconcile();
    UpdatePlaceholder();
    UpdateWidget();
    RefreshLiveApiSnapshot();
    panel::Refresh();
    SetEvent(g_refreshEvt);
}

void UnignoreDevice(std::uint16_t vid, std::uint16_t pid) {
    SaveIgnoreDevicesRaw(ignore_devices::Remove(LoadIgnoreDevicesRaw(), vid, pid));
    panel::Refresh();
    SetEvent(g_refreshEvt);
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
    if (dir == 0) return;
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
    std::vector<ignore_devices::VidPid> ignored;
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
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sub, MF_STRING | ((e.st.vid && e.st.pid) ? 0 : MF_GRAYED), base + DEV_IGNORE,
                T(L"Ignore this device..."));
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

HMENU BuildTempAlertMenu();

std::wstring EveningReminderLabel(int hour) {
    const int clamped = evening_reminder::ClampReminderHour(hour);
    return clamped == 0 ? std::wstring(T(L"Off")) : (std::to_wstring(clamped) + L":00");
}

HMENU BuildEveningReminderMenu() {
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING, ID_EVENING_REMINDER_OFF, T(L"Off"));
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    UINT checked = ID_EVENING_REMINDER_OFF;
    for (UINT i = 0; i < static_cast<UINT>(evening_reminder::kHourCount); ++i) {
        const int h = evening_reminder::kHours[i];
        AppendMenuW(sub, MF_STRING, ID_EVENING_REMINDER_FIRST + i, EveningReminderLabel(h).c_str());
        if (g_eveningReminderHour == h) checked = ID_EVENING_REMINDER_FIRST + i;
    }
    CheckMenuRadioItem(sub, ID_EVENING_REMINDER_OFF,
                       ID_EVENING_REMINDER_FIRST + evening_reminder::kHourCount - 1, checked, MF_BYCOMMAND);
    return sub;
}

const wchar_t* AlertSoundLabelKey(alerts::AlertSound mode) {
    return mode == alerts::AlertSound::SystemDefault ? L"Windows default" : L"Off";
}

HMENU BuildAlertSoundMenu() {
    HMENU sub = CreatePopupMenu();
    const alerts::AlertSound modes[] = {
        alerts::AlertSound::Off,
        alerts::AlertSound::SystemDefault,
    };
    UINT checked = ID_ALERT_SOUND_FIRST;
    for (UINT i = 0; i < ARRAYSIZE(modes); ++i) {
        AppendMenuW(sub, MF_STRING, ID_ALERT_SOUND_FIRST + i, T(AlertSoundLabelKey(modes[i])));
        if (g_alertSound == modes[i]) checked = ID_ALERT_SOUND_FIRST + i;
    }
    CheckMenuRadioItem(sub, ID_ALERT_SOUND_FIRST, ID_ALERT_SOUND_FIRST + ARRAYSIZE(modes) - 1, checked, MF_BYCOMMAND);
    return sub;
}

const osd_policy::OsdMode kOsdMenuModes[] = {
    osd_policy::OsdMode::Off,
    osd_policy::OsdMode::FullscreenOnly,
    osd_policy::OsdMode::FullscreenGame,
    osd_policy::OsdMode::Always,
};

const wchar_t* OsdModeLabelKey(osd_policy::OsdMode mode) {
    switch (mode) {
        case osd_policy::OsdMode::FullscreenOnly: return L"Only in full screen";
        case osd_policy::OsdMode::FullscreenGame: return L"Full screen (game monitor)";
        case osd_policy::OsdMode::Always:         return L"Always";
        case osd_policy::OsdMode::Off:
        default:                                  return L"Off";
    }
}

const wchar_t* OsdCornerLabelKey(osd_policy::OsdCorner corner) {
    switch (corner) {
        case osd_policy::OsdCorner::TopLeft:     return L"Top left";
        case osd_policy::OsdCorner::BottomRight: return L"Bottom right";
        case osd_policy::OsdCorner::BottomLeft:  return L"Bottom left";
        case osd_policy::OsdCorner::TopRight:
        default:                                 return L"Top right";
    }
}

HMENU BuildOsdModeMenu() {
    HMENU sub = CreatePopupMenu();
    UINT checked = ID_OSD_MODE_FIRST;
    for (UINT i = 0; i < ARRAYSIZE(kOsdMenuModes); ++i) {
        AppendMenuW(sub, MF_STRING, ID_OSD_MODE_FIRST + i, T(OsdModeLabelKey(kOsdMenuModes[i])));
        if (g_osdMode == kOsdMenuModes[i]) checked = ID_OSD_MODE_FIRST + i;
    }
    CheckMenuRadioItem(sub, ID_OSD_MODE_FIRST, ID_OSD_MODE_FIRST + ARRAYSIZE(kOsdMenuModes) - 1, checked, MF_BYCOMMAND);
    return sub;
}

HMENU BuildOsdCornerMenu() {
    HMENU sub = CreatePopupMenu();
    const osd_policy::OsdCorner corners[] = {
        osd_policy::OsdCorner::TopRight,
        osd_policy::OsdCorner::TopLeft,
        osd_policy::OsdCorner::BottomRight,
        osd_policy::OsdCorner::BottomLeft,
    };
    UINT checked = ID_OSD_CORNER_FIRST;
    for (UINT i = 0; i < ARRAYSIZE(corners); ++i) {
        AppendMenuW(sub, MF_STRING, ID_OSD_CORNER_FIRST + i, T(OsdCornerLabelKey(corners[i])));
        if (g_osdCorner == corners[i]) checked = ID_OSD_CORNER_FIRST + i;
    }
    CheckMenuRadioItem(sub, ID_OSD_CORNER_FIRST, ID_OSD_CORNER_FIRST + ARRAYSIZE(corners) - 1, checked, MF_BYCOMMAND);
    return sub;
}

std::wstring TempPresetLabel(sysmon::TempPreset preset) {
    const sysmon::TempThresholds t = sysmon::ThresholdsFor(preset);
    const wchar_t* key = L"Standard ({elevated} / {high} °C)";
    switch (preset) {
        case sysmon::TempPreset::Laptop: key = L"Laptop ({elevated} / {high} °C)"; break;
        case sysmon::TempPreset::Strict: key = L"Strict ({elevated} / {high} °C)"; break;
        case sysmon::TempPreset::Standard:
        default: break;
    }
    return TF(key, { { L"elevated", std::to_wstring(t.elevated) }, { L"high", std::to_wstring(t.high) } });
}

std::wstring TempAlertMatchLabel() {
    return TF(L"Match colour preset ({temp} °C)",
              { { L"temp", std::to_wstring(sysmon::ThresholdsFor(g_tempPreset).high) } });
}

std::wstring TempAlertLabel() {
    if (g_tempAlertC == sysmon::kTempAlertMatchPreset) return TempAlertMatchLabel();
    if (g_tempAlertC > 0) return std::to_wstring(g_tempAlertC) + L" °C";
    return T(L"Off");
}

HMENU BuildTempPresetMenu() {
    HMENU sub = CreatePopupMenu();
    const UINT count = ARRAYSIZE(TEMP_PRESETS);
    UINT checked = ID_TEMP_PRESET_FIRST;
    for (UINT i = 0; i < count; i++) {
        AppendMenuW(sub, MF_STRING, ID_TEMP_PRESET_FIRST + i, TempPresetLabel(TEMP_PRESETS[i]).c_str());
        if (g_tempPreset == TEMP_PRESETS[i]) checked = ID_TEMP_PRESET_FIRST + i;
    }
    CheckMenuRadioItem(sub, ID_TEMP_PRESET_FIRST, ID_TEMP_PRESET_FIRST + count - 1, checked, MF_BYCOMMAND);
    return sub;
}

HMENU BuildIgnoredMenu(std::vector<ignore_devices::VidPid>& ignored) {
    ignored = ignore_devices::Parse(LoadIgnoreDevicesRaw());
    if (ignored.size() > 64) ignored.resize(64);
    HMENU sub = CreatePopupMenu();
    if (ignored.empty()) {
        AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, T(L"None"));
        return sub;
    }
    for (size_t i = 0; i < ignored.size(); ++i) {
        std::wstring id = ignore_devices::FormatPair(ignored[i].vid, ignored[i].pid);
        AppendMenuW(sub, MF_STRING, ID_UNIGNORE_FIRST + (UINT)i,
                    TF(L"Unignore {id}", { { L"id", id } }).c_str());
    }
    return sub;
}

HMENU BuildTempAlertMenu() {
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING, ID_TEMP_ALERT_OFF, T(L"Off"));
    AppendMenuW(sub, MF_STRING, ID_TEMP_ALERT_MATCH, TempAlertMatchLabel().c_str());
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    UINT checked = ID_TEMP_ALERT_OFF;
    if (g_tempAlertC == sysmon::kTempAlertMatchPreset) checked = ID_TEMP_ALERT_MATCH;
    const UINT count = ARRAYSIZE(sysmon::kTempAlertOptions);
    for (UINT i = 0; i < count; i++) {
        AppendMenuW(sub, MF_STRING, ID_TEMP_ALERT_FIRST + i,
                    (std::to_wstring(sysmon::kTempAlertOptions[i]) + L" °C").c_str());
        if (g_tempAlertC == sysmon::kTempAlertOptions[i]) checked = ID_TEMP_ALERT_FIRST + i;
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
    std::vector<ignore_devices::VidPid> ignored;
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
        AppendMenuW(batMenu, MF_POPUP | (g_recordHistory ? 0 : MF_GRAYED), (UINT_PTR)BuildEveningReminderMenu(),
                    T(L"Evening charge reminder"));
        AppendMenuW(batMenu, MF_STRING | (g_chargedAlert ? MF_CHECKED : 0), ID_CHARGED_ALERT,
                    T(L"Notify when fully charged"));
        AppendMenuW(batMenu, MF_STRING | (g_tieredAlerts ? MF_CHECKED : 0), ID_TIERED_ALERTS,
                    T(L"Warn again at 10% and 5%"));
        AppendMenuW(batMenu, MF_STRING | (g_shortRuntimeAlert ? MF_CHECKED : 0) | (g_recordHistory ? 0 : MF_GRAYED),
                    ID_SHORT_RUNTIME_ALERT, T(L"Warn when a battery won't last the day"));
        AppendMenuW(batMenu, MF_POPUP, (UINT_PTR)BuildAlertSoundMenu(), T(L"Alert sound"));
        AppendMenuW(batMenu, MF_POPUP, (UINT_PTR)BuildOsdModeMenu(), T(L"In-game alerts"));
        AppendMenuW(batMenu, MF_POPUP, (UINT_PTR)BuildOsdCornerMenu(), T(L"In-game alert corner"));
        AppendMenuW(batMenu, MF_POPUP, (UINT_PTR)BuildTrackingMenu(), T(L"Battery tracking"));
        AppendMenuW(batMenu, MF_POPUP, (UINT_PTR)BuildPollBatteryMenu(), T(L"Check battery every"));
        AppendMenuW(batMenu, MF_SEPARATOR, 0, nullptr);
        HMENU ignoredMenu = BuildIgnoredMenu(ignored);
        AppendMenuW(batMenu, MF_POPUP | (ignored.empty() ? MF_GRAYED : 0), (UINT_PTR)ignoredMenu,
                    T(L"Ignored devices..."));
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
        AppendMenuW(sysMenu, MF_POPUP, (UINT_PTR)BuildTempPresetMenu(), T(L"Temperature colours"));
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
    AppendMenuW(m, MF_STRING | (g_pollLessFullscreen.load() ? MF_CHECKED : 0), ID_POLL_LESS_FULLSCREEN,
                T(L"Poll less during full-screen games"));
    AppendMenuW(m, MF_STRING | (g_localApiPipe ? MF_CHECKED : 0), ID_LOCAL_API_PIPE,
                T(L"Local JSON pipe (\\\\.\\pipe\\TaskbarVitals)"));
    AppendMenuW(m, MF_STRING, ID_EXPORT_SETTINGS, T(L"Export settings..."));
    AppendMenuW(m, MF_STRING, ID_IMPORT_SETTINGS, T(L"Import settings..."));
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
    model.ignored = std::move(ignored);
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
        case ID_EVENING_REMINDER_OFF: SetEveningReminderHour(0); break;
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
        case ID_TEMP_ALERT_MATCH: SetTempAlert(sysmon::kTempAlertMatchPreset); break;
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
        case ID_EXPORT_SETTINGS: ExportSettings(); break;
        case ID_IMPORT_SETTINGS: ImportSettings(); break;
        case ID_POLL_LESS_FULLSCREEN: SetPollLessFullscreen(!g_pollLessFullscreen.load()); break;
        case ID_LOCAL_API_PIPE: SetLocalApiPipe(!g_localApiPipe); break;
        case ID_EXIT: DestroyWindow(g_hwnd); break;
        default: {
            int sysItem = -1, sysDir = 0;
            if (command_routing::DecodeSystemMoveCommand(cmd, kSystemItemCount, sysItem, sysDir)) {
                MoveSystemItem(sysItem, sysDir);
                break;
            }
            command_routing::DecodedDeviceCmd devCmd;
            if (command_routing::DecodeDeviceCommand(cmd, keys.size(), devCmd)) {
                const std::wstring& key = keys[devCmd.deviceIndex];
                switch (devCmd.subCmd) {
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
                    case DEV_IGNORE: IgnoreDevice(key); break;
                    case DEV_ALERT_FIRST:
                        if (devCmd.alertSlot >= 0 && devCmd.alertSlot < alerts::kThresholdCount)
                            SetAlertOverride(key, alerts::kThresholds[devCmd.alertSlot]);
                        break;
                    default: break;
                }
                break;
            }
            const auto indexed = command_routing::DecodeIndexedCommand(
                cmd, ARRAYSIZE(POSITION_OFFSETS), alerts::kChargeLimitCount, evening_reminder::kHourCount,
                ARRAYSIZE(kOsdMenuModes), ARRAYSIZE(TEMP_PRESETS), ARRAYSIZE(sysmon::kTempAlertOptions),
                ARRAYSIZE(POLL_SECONDS), ARRAYSIZE(SYS_POLL_SECONDS), languages.size(), monitors.size(),
                model.ignored.size());
            switch (indexed.kind) {
                case command_routing::IndexedRangeKind::TaskbarOffsetPreset:
                    SetTaskbarOffset(POSITION_OFFSETS[indexed.index]);
                    break;
                case command_routing::IndexedRangeKind::TaskbarOffsetCustom: {
                    int px = 0;
                    if (PromptTaskbarOffset(g_hwnd, LoadTaskbarOffset(), px)) SetTaskbarOffset(px);
                    break;
                }
                case command_routing::IndexedRangeKind::ChargeLimit:
                    SetChargeLimit(alerts::kChargeLimits[indexed.index]);
                    break;
                case command_routing::IndexedRangeKind::EveningReminder:
                    SetEveningReminderHour(evening_reminder::kHours[indexed.index]);
                    break;
                case command_routing::IndexedRangeKind::AlertSound:
                    SetAlertSound(alerts::ClampAlertSound(static_cast<int>(indexed.index)));
                    break;
                case command_routing::IndexedRangeKind::OsdMode:
                    SetOsdMode(kOsdMenuModes[indexed.index]);
                    break;
                case command_routing::IndexedRangeKind::OsdCorner:
                    SetOsdCorner(osd_policy::ClampOsdCorner(static_cast<int>(indexed.index)));
                    break;
                case command_routing::IndexedRangeKind::TempPreset:
                    SetTempPreset(TEMP_PRESETS[indexed.index]);
                    break;
                case command_routing::IndexedRangeKind::TempAlert:
                    SetTempAlert(sysmon::kTempAlertOptions[indexed.index]);
                    break;
                case command_routing::IndexedRangeKind::PollBattery:
                    SetPollSeconds(POLL_SECONDS[indexed.index]);
                    break;
                case command_routing::IndexedRangeKind::PollSystem:
                    SetSystemPollSeconds(SYS_POLL_SECONDS[indexed.index]);
                    break;
                case command_routing::IndexedRangeKind::Language:
                    SetLanguage(languages[indexed.index].code);
                    break;
                case command_routing::IndexedRangeKind::Monitor:
                    SetWidgetMonitor(monitors[indexed.index].id);
                    break;
                case command_routing::IndexedRangeKind::Unignore: {
                    const auto& vp = model.ignored[indexed.index];
                    UnignoreDevice(vp.vid, vp.pid);
                    break;
                }
                case command_routing::IndexedRangeKind::None:
                default:
                    break;
            }
            break;
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
    addChoice(bat, ID_POPUP_EVENING_REMINDER, T(L"Evening charge reminder"),
              EveningReminderLabel(g_eveningReminderHour), !g_recordHistory);
    addToggle(bat, ID_CHARGED_ALERT, T(L"Notify when fully charged"), g_chargedAlert);
    addToggle(bat, ID_TIERED_ALERTS, T(L"Warn again at 10% and 5%"), g_tieredAlerts);
    addToggle(bat, ID_SHORT_RUNTIME_ALERT, T(L"Warn when a battery won't last the day"), g_shortRuntimeAlert,
              !g_recordHistory);
    addChoice(bat, ID_POPUP_ALERT_SOUND, T(L"Alert sound"), T(AlertSoundLabelKey(g_alertSound)));
    addChoice(bat, ID_POPUP_OSD_MODE, T(L"In-game alerts"), T(OsdModeLabelKey(g_osdMode)));
    addChoice(bat, ID_POPUP_OSD_CORNER, T(L"In-game alert corner"), T(OsdCornerLabelKey(g_osdCorner)));
    std::wstring trackVal = (g_trackSince || g_trackAwake || g_trackEmpty) ? T(L"On") : T(L"Off");
    addChoice(bat, ID_POPUP_TRACKING, T(L"Battery tracking"), trackVal);
    addChoice(bat, ID_POPUP_POLL_BATTERY, T(L"Check battery every"),
              FormatDurationLabel((DWORD)(g_pollIntervalMs / 1000)));
    g_panelModel.ignored = ignore_devices::Parse(LoadIgnoreDevicesRaw());
    addChoice(bat, ID_POPUP_IGNORED, T(L"Ignored devices..."),
              g_panelModel.ignored.empty() ? std::wstring(T(L"None"))
                                           : std::to_wstring(g_panelModel.ignored.size()),
              g_panelModel.ignored.empty(), true);

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
    addChoice(sys, ID_POPUP_TEMP_COLORS, T(L"Temperature colours"), TempPresetLabel(g_tempPreset), false, true);
    addChoice(sys, ID_POPUP_TEMP_ALERT, T(L"High temperature alert"), TempAlertLabel());
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
    addToggle(gen, ID_POLL_LESS_FULLSCREEN, T(L"Poll less during full-screen games"), g_pollLessFullscreen.load());
    addToggle(gen, ID_LOCAL_API_PIPE, T(L"Local JSON pipe (\\\\.\\pipe\\TaskbarVitals)"), g_localApiPipe);
    addToggle(gen, ID_AUTO_UPDATE, T(L"Automatically check for updates"), updates::AutomaticEnabled(),
              !updates::Supported());
    addAction(gen, ID_UPDATE, updates::MenuLabel(), updates::Busy(), false, true);
    addAction(gen, ID_REPORT, T(L"Request support for a device..."));
    addAction(gen, ID_SUPPORT, T(L"Support the project..."));
    addAction(gen, ID_EXPORT_SETTINGS, T(L"Export settings..."), false, false, true);
    addAction(gen, ID_IMPORT_SETTINGS, T(L"Import settings..."));

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
            case ID_POPUP_CHARGE_LIMIT:     sub = BuildChargeLimitMenu(); break;
            case ID_POPUP_EVENING_REMINDER: sub = BuildEveningReminderMenu(); break;
            case ID_POPUP_ALERT_SOUND:      sub = BuildAlertSoundMenu(); break;
            case ID_POPUP_OSD_MODE:         sub = BuildOsdModeMenu(); break;
            case ID_POPUP_OSD_CORNER:       sub = BuildOsdCornerMenu(); break;
            case ID_POPUP_TRACKING:         sub = BuildTrackingMenu(); break;
            case ID_POPUP_UNSUPPORTED:      sub = BuildUnsupportedMenu(); break;
            case ID_POPUP_POLL_BATTERY:     sub = BuildPollBatteryMenu(); break;
            case ID_POPUP_IGNORED:          sub = BuildIgnoredMenu(g_panelModel.ignored); break;
            case ID_POPUP_TEMP_COLORS:      sub = BuildTempPresetMenu(); break;
            case ID_POPUP_TEMP_ALERT:       sub = BuildTempAlertMenu(); break;
            case ID_POPUP_POLL_SYSTEM:      sub = BuildPollSystemMenu(); break;
            case ID_POPUP_BAT_POS:          sub = BuildStripPlaceMenu(ID_BAT_LEFT, ID_BAT_BEFORE, ID_BAT_CLOCK, g_farLeft, g_beforeApps, g_wantWidget && !g_myFinder); break;
            case ID_POPUP_VIT_POS:          sub = BuildStripPlaceMenu(ID_VIT_LEFT, ID_VIT_BEFORE, ID_VIT_CLOCK, g_vitalsFarLeft, g_vitalsBeforeApps, g_wantWidget && AnyReading() && !g_myFinder); break;
            case ID_POPUP_STRIP_ORDER:      sub = BuildStripOrderMenu(); break;
            case ID_POPUP_TASKBAR_POS:      sub = BuildTaskbarOffsetMenu(false); break;
            case ID_POPUP_VERTICAL_POS:     sub = BuildVerticalPlaceMenu(); break;
            case ID_POPUP_MONITORS:         sub = BuildMonitorMenu(g_panelModel.monitors); break;
            case ID_POPUP_LANG:             sub = BuildLanguageMenu(g_panelModel.languages); break;
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

}  // namespace app
