#pragma once
// Tray balloon notifications, alert sound playback, in-game OSD alert dispatch, and evening charge reminders.
#include <windows.h>

#include <string>

#include "alert_rules.h"
#include "app_state.h"

namespace app {

bool IsStale(const TrayEntry& e);
std::wstring StatusText(const TrayEntry& e);
bool TrayShow(bool added, UINT uid, HICON icon, const std::wstring& tip, const wchar_t* infoTitle = nullptr,
              const std::wstring& info = L"");
void TrayDelete(UINT uid);
void ShowTextNotification(const std::wstring& title, const std::wstring& message);
void FreeAlertSoundLib();
void ShowBatteryAlert(TrayEntry& e, alerts::LowAlert level);
void ShowChargedAlert(TrayEntry& e);
void ShowChargeLimitAlert(TrayEntry& e);
void ShowShortRuntimeAlert(TrayEntry& e, const std::wstring& left);
void UpdatePlaceholder();
void CheckEveningReminder(bool force = false);

}  // namespace app
