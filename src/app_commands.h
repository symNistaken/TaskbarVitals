#pragma once
// Win32 context menu builders, settings panel host, setters, and command routing for Taskbar Vitals.
#include <windows.h>

#include <cstdint>
#include <string>

#include "alert_rules.h"
#include "app_state.h"
#include "osd_policy.h"
#include "sysmon.h"
#include "widget.h"

namespace app {

void SetWidgetMonitor(const std::wstring& v);
void SetMode(bool useWidget);
void SetWidgetBackground(bool on);
void SetKeepMouseLastKnown(bool on);
void SetHideLastKnown(bool on);
void SetTrayWhenEmpty(bool on);
void SetShowDisconnectedDevices(bool on);
void SetDimSleeping(bool on);
void SetLanguage(const std::wstring& code);
void SetConnectionIcons(bool on);
void SetRingStyle(bool on);
void SetShowPercentSign(bool on);
void SetTwoRows(bool on);
void SetCustomIcons(bool on);
void SetTrackSince(bool on);
void SetTrackAwake(bool on);
void SetTrackEmpty(bool on);
void SetStacked(bool on);
void SetMakeRoom(bool on);
void SetTaskbarOffset(int px);
void SetWidgetPlace(widget::VerticalPlace p);
void SetShowCpuTemp(bool on);
void RemoveCpuTempHelper();
void SetShowReading(bool& flag, const wchar_t* name, bool on);
void SetBatPlace(bool farLeft, bool beforeApps);
void SetVitPlace(bool farLeft, bool beforeApps);
void SetVitalsFirst(bool on);
void SetBrandColors(bool on);
void SetSystemColors(bool on);
void SetSystemPollSeconds(DWORD seconds);
void SetTempAlert(int celsius);
void SetTempPreset(sysmon::TempPreset preset);
void SetChargeLimit(int limit);
void SetEveningReminderHour(int hour);
void SetPollSeconds(DWORD seconds);
void SetPollLessFullscreen(bool on);
void SetOsdMode(osd_policy::OsdMode mode);
void SetOsdCorner(osd_policy::OsdCorner corner);
void SetAlertSound(alerts::AlertSound mode);
void SetLocalApiPipe(bool on);
void ToggleHidden(const std::wstring& key);
void RenameDevice(const std::wstring& key);
void ResetDeviceName(const std::wstring& key);
void SetAlertOverride(const std::wstring& key, int threshold);
void IgnoreDevice(const std::wstring& key);
void UnignoreDevice(std::uint16_t vid, std::uint16_t pid);
void MoveEntry(const std::wstring& key, int dir);
void MoveSystemItem(int item, int dir);
void ShowMenu();
void OpenSettings();

}  // namespace app
