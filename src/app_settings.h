#pragma once
// Windows Registry persistence and JSON settings export/import for Taskbar Vitals.
#include <windows.h>

#include <string>

#include "alert_rules.h"
#include "osd_policy.h"
#include "settings_port.h"
#include "sysmon.h"
#include "widget.h"

namespace app {

void LoadAliases();
void SaveAliases();
void LoadHidden();
void SaveHidden();
void LoadOrder();
void SaveOrder();
void LoadSystemOrder();
void SaveSystemOrder();
std::wstring LoadIgnoreDevicesRaw();
void SaveIgnoreDevicesRaw(const std::wstring& raw);
bool LoadBoolSetting(const wchar_t* name, bool defaultVal);
void SaveBoolSetting(const wchar_t* name, bool on);
void LoadAlertThresholds();
void SaveAlertThresholds();
bool LoadWidgetMode();
void SaveWidgetMode(bool on);
bool LoadHideLastKnown();
void SaveHideLastKnown(bool on);
bool LoadTrayWhenEmpty();
void SaveTrayWhenEmpty(bool on);
bool LoadShowDisconnectedDevices();
void SaveShowDisconnectedDevices(bool on);
bool LoadDimSleeping();
void SaveDimSleeping(bool on);
bool LoadRecordHistory();
bool LoadMakeRoom();
void SaveMakeRoom(bool on);
int LoadTaskbarOffset();
void SaveTaskbarOffset(int px);
void SaveRecordHistory(bool on);
bool LoadWidgetBackground();
void SaveWidgetBackground(bool on);
std::wstring LoadWidgetMonitor();
void SaveWidgetMonitor(const std::wstring& v);
bool LoadStacked();
void SaveStacked(bool on);
widget::VerticalPlace LoadVerticalPlace();
void SaveVerticalPlace(widget::VerticalPlace p);
bool LoadFarLeft();
void SaveFarLeft(bool on);
bool LoadBeforeApps();
void SaveBeforeApps(bool on);
bool LoadRuntimeWidget();
void SaveRuntimeWidget(bool on);
bool LoadShowPercentSign();
void SaveShowPercentSign(bool on);
bool LoadRingStyle();
void SaveRingStyle(bool on);
bool LoadConnectionIcons();
void SaveConnectionIcons(bool on);
bool LoadTrackSince();
void SaveTrackSince(bool on);
bool LoadTrackAwake();
void SaveTrackAwake(bool on);
bool LoadTrackEmpty();
void SaveTrackEmpty(bool on);
bool LoadShowCpu();
bool LoadShowRam();
bool LoadBrandColors();
void SaveBrandColors(bool on);
DWORD LoadPollSeconds();
void SavePollSeconds(DWORD seconds);
DWORD LoadSystemPollSeconds();
void SaveSystemPollSeconds(DWORD seconds);
sysmon::TempPreset LoadTempPreset();
void SaveTempPreset(sysmon::TempPreset preset);
int LoadTempAlertCelsius();
void SaveTempAlertCelsius(int celsius);
osd_policy::OsdMode LoadOsdMode();
void SaveOsdMode(osd_policy::OsdMode mode);
osd_policy::OsdCorner LoadOsdCorner();
void SaveOsdCorner(osd_policy::OsdCorner corner);
alerts::AlertSound LoadAlertSound();
void SaveAlertSound(alerts::AlertSound mode);
int LoadEveningReminderHour();
void SaveEveningReminderHour(int hour);
int LoadEveningReminderLastCycle();
void SaveEveningReminderLastCycle(int cycleDate);
void SavePollLessFullscreen(bool on);
void SaveLocalApiPipe(bool on);
bool AutostartEnabled();
void SetAutostart(bool on);
int LoadChargeLimit();
settings_port::SettingsSnapshot BuildCurrentSettingsSnapshot();
void ApplyImportedSettings(const settings_port::ParsedSettings& parsed);
void ExportSettings();
void ImportSettings();

}  // namespace app
