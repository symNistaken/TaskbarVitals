#pragma once
// Shared application state, constants, value types and cross-module declarations for Taskbar Vitals.
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "alert_rules.h"
#include "brands.h"
#include "charge_estimate.h"
#include "command_routing.h"
#include "cputemp_client.h"
#include "device_aliases.h"
#include "devices.h"
#include "diskmon.h"
#include "gpumon.h"
#include "laptopbat.h"
#include "netmon.h"
#include "osd_policy.h"
#include "refresh_policy.h"
#include "sysmon.h"
#include "system_order.h"
#include "widget.h"

namespace app {

using namespace command_routing;

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_RESULTS = WM_APP + 2;
constexpr UINT WM_WIDGET_CLICK = WM_APP + 3;
constexpr UINT WM_DEVICE_EVENT = WM_APP + 4;  // lParam: ms to wait before polling
constexpr UINT_PTR TIMER_DEBOUNCE = 1;
constexpr UINT_PTR TIMER_WIDGET = 2;
constexpr UINT_PTR TIMER_ALERT_ICON = 3;
constexpr UINT_PTR TIMER_RESUME = 4;
// "Check battery every" choices in seconds; 60 is the default. A poll resets an awake device's sleep timer,
// so a device only sleeps if its timer is shorter than the interval.
constexpr DWORD POLL_SECONDS[] = { 30, 60, 120, 300, 600, 900, 1800, 3600 };
constexpr DWORD SYS_POLL_SECONDS[] = { 1, 2, 5, 10 };
constexpr sysmon::TempPreset TEMP_PRESETS[] = {
    sysmon::TempPreset::Standard,
    sysmon::TempPreset::Laptop,
    sysmon::TempPreset::Strict,
};
constexpr size_t kSystemItemCount = 7;
constexpr int LOW_BATTERY = 15;
constexpr UINT PLACEHOLDER_ID = 1;
constexpr UINT ALERT_ICON_ID = 2;
constexpr UINT SYS_TRAY_UID_BASE = 10;

inline const wchar_t* kAppName = L"Taskbar Vitals";
inline const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline const wchar_t* kRunValue = L"WirelessBattery";

inline const wchar_t* kSettingsKey = L"Software\\WirelessBattery";

// "Position on the taskbar": distance from the notification area in DPI-independent pixels. Stored as
// the TaskbarOffset value the widget has always read, so a value set by hand keeps working.
const int POSITION_OFFSETS[] = { 0, 40, 80, 120, 160, 200 };

// Rolling 3-minute live history for Vitals hover cards.
struct VitalPoint {
    ULONGLONG tickMs = 0;
    int value = 0;
};
constexpr ULONGLONG kVitalHistoryMs = 180000;

struct ChargeRecord {
    ULONGLONG since = 0;     // when the last charge ended, in seconds since 1601 (0 = not yet)
    ULONGLONG awakeSec = 0;  // seconds awake since then
    LONGLONG lastPct = -1;   // last live level (a jump up means it was charged)
};

struct SysTraySlot {
    bool added = false;
    std::wstring sig;
    HICON icon = nullptr;
};

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

struct Rgb { BYTE r, g, b; };

extern HINSTANCE g_inst;
extern HWND g_hwnd;
extern HANDLE g_refreshEvt, g_stopEvt, g_worker;
extern UINT g_taskbarCreatedMsg;
extern bool g_lightTaskbar;
extern bool g_myFinder;
extern bool g_wantWidget;
extern bool g_useWidget;
extern bool g_hideLastKnown;
extern bool g_keepMouseLastKnown;
extern bool g_trayWhenEmpty;
extern bool g_emptyToTray;
extern bool g_showDisconnectedDevices;
extern bool g_dimSleeping;
extern bool g_recordHistory;
extern int g_chargeLimit;
extern int g_eveningReminderHour;
extern int g_eveningReminderLastCycle;
extern int g_eveningReminderCheckedCycle;
extern ULONGLONG g_eveningReminderCheckedTick;
extern bool g_chargedAlert;
extern bool g_tieredAlerts;
extern bool g_shortRuntimeAlert;
extern bool g_hoverCard;
extern bool g_hoverChart;
extern bool g_animateCharging;
extern bool g_trayDeviceIcons;
extern bool g_runtimeWidget;
extern std::wstring g_widgetMonitor;
extern bool g_widgetBackground;
extern bool g_stacked;
extern bool g_twoRows;
extern bool g_customIcons;
extern bool g_showPercentSign;
extern bool g_ringStyle;
extern bool g_connectionIcons;
extern bool g_makeRoom;
extern widget::VerticalPlace g_verticalPlace;
extern bool g_farLeft;
extern bool g_beforeApps;
extern bool g_vitalsFarLeft;
extern bool g_vitalsBeforeApps;
extern bool g_vitalsFirst;
extern bool g_leftEndPlaced;
extern bool g_showCpu, g_showRam;
extern bool g_showGpu, g_showGpuTemp, g_showGpuClocks, g_showVram;
extern bool g_showCpuW, g_showGpuW;
extern bool g_showCpuTemp;
extern bool g_showDiskTemp, g_showDiskUse;
extern bool g_showNetSpeed;
extern bool g_hasLaptopBat;
extern bool g_showLaptopBat, g_showLaptopBatW;
extern cputemp::Reading g_cpuTemp;
extern std::vector<int> g_systemOrder;
extern sysmon::Reading g_sys;
extern gpumon::Reading g_gpu;
extern diskmon::Reading g_disk;
extern netmon::Reading g_net;
extern laptopbat::Reading g_laptopBat;
extern int g_sysTicks;
extern DWORD g_sysPollSeconds;
extern sysmon::TempPreset g_tempPreset;
extern int g_tempAlertC;
extern bool g_cpuTempWarned, g_gpuTempWarned, g_diskTempWarned;
extern netmon::TestState g_lastSpeedTestState;
extern bool g_brandColors;
extern bool g_systemColors;
extern bool g_trackSince, g_trackAwake, g_trackEmpty;
extern bool g_demo;
extern bool g_trace;
extern std::atomic<long> g_lastCount;
extern ULONGLONG g_lastPollTick;
extern std::atomic<DWORD> g_pollIntervalMs;
extern std::atomic<bool> g_pollLessFullscreen;
extern bool g_localApiPipe;
extern bool g_settingsDialogOpen;
extern osd_policy::OsdMode g_osdMode;
extern osd_policy::OsdCorner g_osdCorner;
extern alerts::AlertSound g_alertSound;
extern std::deque<VitalPoint> g_cpuHist, g_ramHist, g_gpuHist, g_vramHist, g_diskHist, g_netHist, g_batHist;
extern SysTraySlot g_sysTray[7];
extern std::map<std::wstring, TrayEntry> g_entries;
extern std::vector<std::wstring> g_unsupported;
extern std::map<std::wstring, alerts::LowLatch> g_alertLatches;
extern bool g_alertIconAdded;
extern bool g_placeholder;
extern aliases::AliasMap g_aliases;
extern std::set<std::wstring> g_hidden;
extern std::vector<std::wstring> g_order;
extern std::map<std::wstring, int> g_alertOverrides;

// Functions defined in main.cpp that other modules call.
std::wstring ExePath();
HWND DialogOwner();
void ApplyStripOptions();
void UpdateWidget();
void UpdateSystemTrayIcons();
void UpdatePlaceholder();
void RefreshLiveApiSnapshot();
void TrayDelete(UINT uid);
void Reconcile();
void RedrawAll();
void TickSystemReadings(bool now = false);
bool AnyGpuReading();
bool AnyDiskReading();
bool AnyLaptopBatReading();

// Command setters defined in main.cpp
void SetWidgetMonitor(const std::wstring& v);
void SetChargeLimit(int limit);
void SetEveningReminderHour(int hour);
void SetPollLessFullscreen(bool on);
void SetOsdMode(osd_policy::OsdMode mode);
void SetOsdCorner(osd_policy::OsdCorner corner);
void SetAlertSound(alerts::AlertSound mode);
void SetLocalApiPipe(bool on);

}  // namespace app
