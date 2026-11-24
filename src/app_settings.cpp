// Windows Registry persistence and JSON settings export/import for Taskbar Vitals.
#include "app_settings.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "alert_rules.h"
#include "app_state.h"
#include "cputemp_client.h"
#include "dashboard.h"
#include "diskmon.h"
#include "evening_reminder.h"
#include "file_dialog.h"
#include "gpumon.h"
#include "i18n.h"
#include "ignore_devices.h"
#include "laptopbat.h"
#include "netmon.h"
#include "osd_policy.h"
#include "panel.h"
#include "settings_port.h"
#include "sysmon.h"
#include "system_order.h"
#include "update_win.h"
#include "vitals_strip.h"
#include "widget.h"

namespace app {

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
    g_systemOrder = system_order::DecodeSystemOrder(value, kSystemItemCount);
}

void SaveSystemOrder() {
    DWORD value = system_order::EncodeSystemOrder(g_systemOrder);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"SystemOrder", REG_DWORD, &value, sizeof(value));
}

std::wstring LoadIgnoreDevicesRaw() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"IgnoreDevices", RRF_RT_REG_SZ, nullptr, nullptr, &size) !=
            ERROR_SUCCESS ||
        size < sizeof(wchar_t) || size > 64 * 1024)
        return L"";
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"IgnoreDevices", RRF_RT_REG_SZ, nullptr, buf.data(), &size) !=
        ERROR_SUCCESS)
        return L"";
    return buf.data();
}

void SaveIgnoreDevicesRaw(const std::wstring& raw) {
    if (raw.empty()) RegDeleteKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"IgnoreDevices");
    else RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"IgnoreDevices", REG_SZ, raw.c_str(),
                         (DWORD)((raw.size() + 1) * sizeof(wchar_t)));
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
sysmon::TempPreset LoadTempPreset() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"TempColorPreset", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return sysmon::TempPreset::Standard;
    return sysmon::ClampTempPreset(v <= 2 ? (int)v : -1);
}
void SaveTempPreset(sysmon::TempPreset preset) {
    DWORD v = (DWORD)preset;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TempColorPreset", REG_DWORD, &v, sizeof(v));
}
int LoadTempAlertCelsius() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"TempAlertCelsius", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return 0;
    if (v == (DWORD)sysmon::kTempAlertMatchPreset) return sysmon::kTempAlertMatchPreset;
    for (int t : sysmon::kTempAlertOptions)
        if (v == (DWORD)t) return t;
    return 0;
}
void SaveTempAlertCelsius(int celsius) {
    DWORD v = (DWORD)celsius;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TempAlertCelsius", REG_DWORD, &v, sizeof(v));
}
osd_policy::OsdMode LoadOsdMode() {
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"InGameAlerts", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return osd_policy::OsdMode::FullscreenOnly;
    return osd_policy::ClampOsdMode(v <= 3 ? (int)v : 0);
}
void SaveOsdMode(osd_policy::OsdMode mode) {
    DWORD v = (DWORD)mode;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"InGameAlerts", REG_DWORD, &v, sizeof(v));
}
osd_policy::OsdCorner LoadOsdCorner() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"InGameAlertCorner", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return osd_policy::OsdCorner::TopRight;
    return osd_policy::ClampOsdCorner(v <= 3 ? (int)v : 0);
}
void SaveOsdCorner(osd_policy::OsdCorner corner) {
    DWORD v = (DWORD)corner;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"InGameAlertCorner", REG_DWORD, &v, sizeof(v));
}
alerts::AlertSound LoadAlertSound() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"AlertSound", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return alerts::AlertSound::Off;
    return alerts::ClampAlertSound(v == 1 ? 1 : 0);
}
void SaveAlertSound(alerts::AlertSound mode) {
    DWORD v = static_cast<DWORD>(mode);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"AlertSound", REG_DWORD, &v, sizeof(v));
}
int LoadEveningReminderHour() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"EveningReminderHour", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return 0;
    return evening_reminder::ClampReminderHour(v <= 23 ? static_cast<int>(v) : 0);
}
void SaveEveningReminderHour(int hour) {
    DWORD v = static_cast<DWORD>(evening_reminder::ClampReminderHour(hour));
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"EveningReminderHour", REG_DWORD, &v, sizeof(v));
}
int LoadEveningReminderLastCycle() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"EveningReminderLastCycle", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return 0;
    return v <= 99991231u ? static_cast<int>(v) : 0;
}
void SaveEveningReminderLastCycle(int cycleDate) {
    DWORD v = cycleDate > 0 ? static_cast<DWORD>(cycleDate) : 0u;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"EveningReminderLastCycle", REG_DWORD, &v, sizeof(v));
}
void SavePollLessFullscreen(bool on) { SaveBoolSetting(L"PollLessFullscreen", on); }
void SaveLocalApiPipe(bool on) { SaveBoolSetting(L"LocalApiPipe", on); }

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

int LoadChargeLimit() {
    DWORD v = 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"ChargeLimitAlert", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    for (int l : alerts::kChargeLimits)
        if ((int)v == l) return l;
    return 0;
}

struct BoolSettingBinding {
    bool* flag;
    const wchar_t* regName;
    bool settings_port::SettingsSnapshot::* snap;
    bool settings_port::SettingsPresence::* pres;
};

const std::vector<BoolSettingBinding>& SettingsBoolBindings() {
    static const std::vector<BoolSettingBinding> kBindings = {
        { &g_wantWidget, L"TaskbarWidget", &settings_port::SettingsSnapshot::taskbarWidget, &settings_port::SettingsPresence::taskbarWidget },
        { &g_myFinder, L"MyFinderWidget", &settings_port::SettingsSnapshot::myFinderWidget, &settings_port::SettingsPresence::myFinderWidget },
        { &g_hideLastKnown, L"HideLastKnownDevices", &settings_port::SettingsSnapshot::hideLastKnownDevices, &settings_port::SettingsPresence::hideLastKnownDevices },
        { &g_keepMouseLastKnown, L"KeepMouseLastKnown", &settings_port::SettingsSnapshot::keepMouseLastKnown, &settings_port::SettingsPresence::keepMouseLastKnown },
        { &g_trayWhenEmpty, L"MoveToTrayWhenEmpty", &settings_port::SettingsSnapshot::moveToTrayWhenEmpty, &settings_port::SettingsPresence::moveToTrayWhenEmpty },
        { &g_showDisconnectedDevices, L"ShowDisconnectedDevices", &settings_port::SettingsSnapshot::showDisconnectedDevices, &settings_port::SettingsPresence::showDisconnectedDevices },
        { &g_dimSleeping, L"DimSleepingDevices", &settings_port::SettingsSnapshot::dimSleepingDevices, &settings_port::SettingsPresence::dimSleepingDevices },
        { &g_recordHistory, L"RecordBatteryHistory", &settings_port::SettingsSnapshot::recordBatteryHistory, &settings_port::SettingsPresence::recordBatteryHistory },
        { &g_chargedAlert, L"ChargedAlert", &settings_port::SettingsSnapshot::chargedAlert, &settings_port::SettingsPresence::chargedAlert },
        { &g_tieredAlerts, L"TieredAlerts", &settings_port::SettingsSnapshot::tieredAlerts, &settings_port::SettingsPresence::tieredAlerts },
        { &g_shortRuntimeAlert, L"ShortRuntimeAlert", &settings_port::SettingsSnapshot::shortRuntimeAlert, &settings_port::SettingsPresence::shortRuntimeAlert },
        { &g_hoverCard, L"HoverCard", &settings_port::SettingsSnapshot::hoverCard, &settings_port::SettingsPresence::hoverCard },
        { &g_hoverChart, L"HoverCardChart", &settings_port::SettingsSnapshot::hoverCardChart, &settings_port::SettingsPresence::hoverCardChart },
        { &g_animateCharging, L"AnimateCharging", &settings_port::SettingsSnapshot::animateCharging, &settings_port::SettingsPresence::animateCharging },
        { &g_trayDeviceIcons, L"TrayDeviceIcons", &settings_port::SettingsSnapshot::trayDeviceIcons, &settings_port::SettingsPresence::trayDeviceIcons },
        { &g_runtimeWidget, L"EstimatedRuntimeInWidget", &settings_port::SettingsSnapshot::runtimeWidget, &settings_port::SettingsPresence::runtimeWidget },
        { &g_twoRows, L"TwoRowsLayout", &settings_port::SettingsSnapshot::twoRowsLayout, &settings_port::SettingsPresence::twoRowsLayout },
        { &g_customIcons, L"CustomVectorIcons", &settings_port::SettingsSnapshot::customVectorIcons, &settings_port::SettingsPresence::customVectorIcons },
        { &g_showPercentSign, L"ShowPercentSign", &settings_port::SettingsSnapshot::showPercentSign, &settings_port::SettingsPresence::showPercentSign },
        { &g_ringStyle, L"RingStyle", &settings_port::SettingsSnapshot::ringStyle, &settings_port::SettingsPresence::ringStyle },
        { &g_connectionIcons, L"ConnectionIcons", &settings_port::SettingsSnapshot::connectionIcons, &settings_port::SettingsPresence::connectionIcons },
        { &g_makeRoom, L"MakeRoomForOtherWidgets", &settings_port::SettingsSnapshot::makeRoomForOtherWidgets, &settings_port::SettingsPresence::makeRoomForOtherWidgets },
        { &g_widgetBackground, L"WidgetBackground", &settings_port::SettingsSnapshot::widgetBackground, &settings_port::SettingsPresence::widgetBackground },
        { &g_stacked, L"StackedLayout", &settings_port::SettingsSnapshot::stackedLayout, &settings_port::SettingsPresence::stackedLayout },
        { &g_farLeft, L"WidgetFarLeft", &settings_port::SettingsSnapshot::widgetFarLeft, &settings_port::SettingsPresence::widgetFarLeft },
        { &g_beforeApps, L"WidgetBeforeApps", &settings_port::SettingsSnapshot::widgetBeforeApps, &settings_port::SettingsPresence::widgetBeforeApps },
        { &g_vitalsFarLeft, L"VitalsFarLeft", &settings_port::SettingsSnapshot::vitalsFarLeft, &settings_port::SettingsPresence::vitalsFarLeft },
        { &g_vitalsBeforeApps, L"VitalsBeforeApps", &settings_port::SettingsSnapshot::vitalsBeforeApps, &settings_port::SettingsPresence::vitalsBeforeApps },
        { &g_vitalsFirst, L"VitalsFirst", &settings_port::SettingsSnapshot::vitalsFirst, &settings_port::SettingsPresence::vitalsFirst },
        { &g_showCpu, L"ShowCpu", &settings_port::SettingsSnapshot::showCpu, &settings_port::SettingsPresence::showCpu },
        { &g_showRam, L"ShowRam", &settings_port::SettingsSnapshot::showRam, &settings_port::SettingsPresence::showRam },
        { &g_showGpu, L"ShowGpu", &settings_port::SettingsSnapshot::showGpu, &settings_port::SettingsPresence::showGpu },
        { &g_showGpuTemp, L"ShowGpuTemp", &settings_port::SettingsSnapshot::showGpuTemp, &settings_port::SettingsPresence::showGpuTemp },
        { &g_showGpuClocks, L"ShowGpuClocks", &settings_port::SettingsSnapshot::showGpuClocks, &settings_port::SettingsPresence::showGpuClocks },
        { &g_showCpuW, L"ShowCpuWatts", &settings_port::SettingsSnapshot::showCpuWatts, &settings_port::SettingsPresence::showCpuWatts },
        { &g_showGpuW, L"ShowGpuWatts", &settings_port::SettingsSnapshot::showGpuWatts, &settings_port::SettingsPresence::showGpuWatts },
        { &g_showVram, L"ShowVram", &settings_port::SettingsSnapshot::showVram, &settings_port::SettingsPresence::showVram },
        { &g_showDiskTemp, L"ShowDiskTemp", &settings_port::SettingsSnapshot::showDiskTemp, &settings_port::SettingsPresence::showDiskTemp },
        { &g_showDiskUse, L"ShowDiskUse", &settings_port::SettingsSnapshot::showDiskUse, &settings_port::SettingsPresence::showDiskUse },
        { &g_showLaptopBat, L"ShowLaptopBat", &settings_port::SettingsSnapshot::showLaptopBattery, &settings_port::SettingsPresence::showLaptopBattery },
        { &g_showLaptopBatW, L"ShowLaptopBatWatts", &settings_port::SettingsSnapshot::showLaptopBatWatts, &settings_port::SettingsPresence::showLaptopBatWatts },
        { &g_showNetSpeed, L"ShowNetSpeed", &settings_port::SettingsSnapshot::showNetwork, &settings_port::SettingsPresence::showNetwork },
        { &g_brandColors, L"BrandColors", &settings_port::SettingsSnapshot::brandColors, &settings_port::SettingsPresence::brandColors },
        { &g_systemColors, L"SystemColors", &settings_port::SettingsSnapshot::systemColors, &settings_port::SettingsPresence::systemColors },
    };
    return kBindings;
}

settings_port::SettingsSnapshot BuildCurrentSettingsSnapshot() {
    settings_port::SettingsSnapshot s;
    for (const auto& b : SettingsBoolBindings()) s.*(b.snap) = *b.flag;
    s.showCpuTemp = g_showCpuTemp;
    s.autoCheckUpdates = updates::AutomaticSetting();
    s.pollLessFullscreen = g_pollLessFullscreen.load();
    s.localApiPipe = g_localApiPipe;

    s.taskbarOffset = LoadTaskbarOffset();
    s.verticalPlace = static_cast<int>(g_verticalPlace);
    s.pollIntervalSeconds = static_cast<int>(g_pollIntervalMs.load() / 1000);
    s.systemPollSeconds = static_cast<int>(g_sysPollSeconds);
    s.tempColorPreset = static_cast<int>(g_tempPreset);
    s.tempAlertCelsius = g_tempAlertC;
    s.chargeLimitAlert = g_chargeLimit;
    s.inGameAlerts = static_cast<int>(g_osdMode);
    s.inGameAlertCorner = static_cast<int>(g_osdCorner);
    s.alertSound = static_cast<int>(g_alertSound);
    s.eveningReminderHour = g_eveningReminderHour;
    s.systemOrder = g_systemOrder;

    s.widgetMonitor = g_widgetMonitor;
    s.language = i18n::Choice();
    s.ignoreDevices = ignore_devices::FormatList(ignore_devices::Parse(LoadIgnoreDevicesRaw()));
    s.hiddenDevices = g_hidden;
    s.deviceOrder = g_order;
    s.deviceAliases = g_aliases;
    s.alertThresholds = g_alertOverrides;
    return s;
}

void ApplyImportedSettings(const settings_port::ParsedSettings& parsed) {
    const auto& v = parsed.values;
    const auto& has = parsed.has;

    for (const auto& b : SettingsBoolBindings()) {
        if (has.*(b.pres)) {
            *b.flag = v.*(b.snap);
            SaveBoolSetting(b.regName, *b.flag);
        }
    }
    if (g_farLeft) g_beforeApps = false;
    if (g_vitalsFarLeft) g_vitalsBeforeApps = false;
    if (has.showCpuTemp) {
        const bool enable = v.showCpuTemp && cputemp::HelperInstalled();
        g_showCpuTemp = enable;
        SaveBoolSetting(L"ShowCpuTemp", enable);
        if (enable) cputemp::StartHelper();
        else {
            cputemp::StopHelper();
            g_cpuTemp = cputemp::Reading();
            g_cpuTempWarned = false;
        }
    }
    if (has.autoCheckUpdates) updates::SetAutomaticSetting(v.autoCheckUpdates);
    if (has.pollLessFullscreen) SetPollLessFullscreen(v.pollLessFullscreen);
    if (has.localApiPipe) SetLocalApiPipe(v.localApiPipe);

    if (has.taskbarOffset) SaveTaskbarOffset(v.taskbarOffset);
    if (has.verticalPlace) {
        g_verticalPlace = v.verticalPlace == 1 ? widget::VerticalPlace::Top
                        : v.verticalPlace == 2 ? widget::VerticalPlace::AboveTray
                        : v.verticalPlace == 3 ? widget::VerticalPlace::Bottom
                                               : widget::VerticalPlace::Auto;
        SaveVerticalPlace(g_verticalPlace);
        widget::SetVerticalPlace(g_verticalPlace);
    }
    if (has.pollIntervalSeconds) {
        g_pollIntervalMs = static_cast<DWORD>(v.pollIntervalSeconds) * 1000;
        SavePollSeconds(static_cast<DWORD>(v.pollIntervalSeconds));
    }
    if (has.systemPollSeconds) {
        g_sysPollSeconds = static_cast<DWORD>(v.systemPollSeconds);
        SaveSystemPollSeconds(g_sysPollSeconds);
        g_sysTicks = 0;
    }
    if (has.tempColorPreset) {
        g_tempPreset = sysmon::ClampTempPreset(v.tempColorPreset);
        SaveTempPreset(g_tempPreset);
        g_cpuTempWarned = false;
        g_gpuTempWarned = false;
        g_diskTempWarned = false;
    }
    if (has.tempAlertCelsius) {
        g_tempAlertC = v.tempAlertCelsius;
        SaveTempAlertCelsius(g_tempAlertC);
        g_cpuTempWarned = false;
        g_gpuTempWarned = false;
        g_diskTempWarned = false;
    }
    if (has.chargeLimitAlert) SetChargeLimit(v.chargeLimitAlert);
    if (has.inGameAlerts) SetOsdMode(osd_policy::ClampOsdMode(v.inGameAlerts));
    if (has.inGameAlertCorner) SetOsdCorner(osd_policy::ClampOsdCorner(v.inGameAlertCorner));
    if (has.alertSound) SetAlertSound(alerts::ClampAlertSound(v.alertSound));
    if (has.eveningReminderHour) SetEveningReminderHour(v.eveningReminderHour);
    if (has.systemOrder) {
        g_systemOrder = v.systemOrder;
        SaveSystemOrder();
    }
    if (has.widgetMonitor) SetWidgetMonitor(v.widgetMonitor);
    if (has.language) i18n::SetChoice(v.language);
    if (has.ignoreDevices) {
        SaveIgnoreDevicesRaw(v.ignoreDevices);
        const auto ignored = ignore_devices::Parse(v.ignoreDevices);
        for (auto ei = g_entries.begin(); ei != g_entries.end();) {
            if (ei->second.st.vid && ei->second.st.pid &&
                ignore_devices::Contains(ignored, ei->second.st.vid, ei->second.st.pid)) {
                if (ei->second.added) TrayDelete(ei->second.uid);
                if (ei->second.icon) DestroyIcon(ei->second.icon);
                ei = g_entries.erase(ei);
            } else {
                ++ei;
            }
        }
    }
    if (has.hiddenDevices) { g_hidden = v.hiddenDevices; SaveHidden(); }
    if (has.deviceOrder) { g_order = v.deviceOrder; SaveOrder(); }
    if (has.deviceAliases) { g_aliases = v.deviceAliases; SaveAliases(); }
    if (has.alertThresholds) {
        g_alertOverrides = v.alertThresholds;
        SaveAlertThresholds();
        g_alertLatches.clear();
        for (auto& [k, e] : g_entries) e.lowWarned = false;
    }

    if (!g_showCpuW) sysmon::StopPower();
    if (!AnyGpuReading() && !dashboard::IsOpen()) {
        gpumon::Stop();
        g_gpu = gpumon::Reading();
        g_gpuTempWarned = false;
    }
    if (!AnyDiskReading() && !dashboard::IsOpen()) {
        diskmon::Stop();
        g_disk = diskmon::Reading();
        g_diskTempWarned = false;
    }
    if (!g_showNetSpeed && !dashboard::IsOpen()) {
        netmon::Stop();
        g_net = netmon::Reading();
    }
    if (!AnyLaptopBatReading() && !dashboard::IsOpen()) {
        laptopbat::Stop();
        g_laptopBat = laptopbat::Reading();
    }

    ApplyStripOptions();
    Reconcile();
    TickSystemReadings(true);
    RedrawAll();
    UpdateWidget();
    UpdateSystemTrayIcons();
    UpdatePlaceholder();
    RefreshLiveApiSnapshot();
    panel::Refresh();
    SetEvent(g_refreshEvt);
}

void ExportSettings() {
    if (g_settingsDialogOpen) return;
    g_settingsDialogOpen = true;
    HWND owner = DialogOwner();
    const std::string json = settings_port::SettingsToJson(BuildCurrentSettingsSnapshot());
    const std::wstring filter = file_dialog::BuildFilter(T(L"JSON files (*.json)"), L"*.json", T(L"All files (*.*)"));
    std::wstring path;
    if (file_dialog::PickFile(owner, file_dialog::Mode::Save, T(L"Export settings"), filter, L"json",
                              L"TaskbarVitals-settings.json", path)) {
        if (file_dialog::WriteUtf8File(path, json)) {
            MessageBoxW(DialogOwner(), T(L"Settings exported."), T(L"Export settings"),
                        MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        } else {
            MessageBoxW(DialogOwner(), T(L"Could not export settings."), T(L"Export settings"),
                        MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        }
    }
    g_settingsDialogOpen = false;
}

void ImportSettings() {
    if (g_settingsDialogOpen) return;
    g_settingsDialogOpen = true;
    HWND owner = DialogOwner();
    const std::wstring filter = file_dialog::BuildFilter(T(L"JSON files (*.json)"), L"*.json", T(L"All files (*.*)"));
    std::wstring path;
    if (file_dialog::PickFile(owner, file_dialog::Mode::Open, T(L"Import settings"), filter, L"json", L"", path)) {
        std::string data;
        bool tooLarge = false;
        if (!file_dialog::ReadBoundedFile(path, settings_port::kMaxSettingsFileBytes, data, tooLarge)) {
            MessageBoxW(DialogOwner(),
                        tooLarge ? T(L"The selected settings file is too large.")
                                 : T(L"Could not read the selected settings file."),
                        T(L"Import settings"), MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        } else {
            settings_port::ParsedSettings parsed;
            if (!settings_port::ParseSettingsJson(data, parsed)) {
                MessageBoxW(DialogOwner(), T(L"Could not import settings. The selected file is not valid JSON."),
                            T(L"Import settings"), MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
            } else if (parsed.acceptedCount == 0) {
                MessageBoxW(DialogOwner(), T(L"No valid settings were found in the selected file."),
                            T(L"Import settings"), MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
            } else {
                ApplyImportedSettings(parsed);
                MessageBoxW(DialogOwner(),
                            TF(L"Settings imported ({count} updated).",
                               { { L"count", std::to_wstring(parsed.acceptedCount) } }).c_str(),
                            T(L"Import settings"), MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
            }
        }
    }
    g_settingsDialogOpen = false;
}

}  // namespace app
