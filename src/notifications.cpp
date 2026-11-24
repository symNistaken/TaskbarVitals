// Tray balloon notifications, alert sound playback, in-game OSD alert dispatch, and evening charge reminders.
#include "notifications.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#include "alert_rules.h"
#include "app_settings.h"
#include "app_state.h"
#include "battery_history.h"
#include "charge_estimate.h"
#include "evening_reminder.h"
#include "i18n.h"
#include "osd_overlay.h"
#include "osd_policy.h"
#include "reading_freshness.h"
#include "refresh_policy.h"
#include "widget.h"

namespace app {

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
bool TrayShow(bool added, UINT uid, HICON icon, const std::wstring& tip, const wchar_t* infoTitle,
              const std::wstring& info) {
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

using PlaySoundWFn = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);
HMODULE g_winmm = nullptr;
PlaySoundWFn g_playSound = nullptr;

void FreeAlertSoundLib() {
    if (g_playSound) g_playSound(nullptr, nullptr, 0);
    if (g_winmm) {
        FreeLibrary(g_winmm);
        g_winmm = nullptr;
    }
    g_playSound = nullptr;
}

void PlayBatteryAlertSound(alerts::AlertKind kind = alerts::AlertKind::BatteryLow) {
    if (!alerts::ShouldPlayAlertSound(g_alertSound, kind)) return;
    if (!g_playSound) {
        if (!g_winmm) g_winmm = LoadLibraryExW(L"winmm.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!g_winmm) return;
        g_playSound = reinterpret_cast<PlaySoundWFn>(
            reinterpret_cast<void*>(GetProcAddress(g_winmm, "PlaySoundW")));
        if (!g_playSound) {
            FreeLibrary(g_winmm);
            g_winmm = nullptr;
            return;
        }
    }
    constexpr DWORD kSndAsync = 0x0001u;
    constexpr DWORD kSndAlias = 0x00010000u;
    constexpr DWORD kSndNoDefault = 0x0002u;
    g_playSound(L"SystemExclamation", nullptr, kSndAlias | kSndAsync | kSndNoDefault);
}

void ShowBatteryAlert(TrayEntry& e, alerts::LowAlert level) {
    const wchar_t* title = level == alerts::LowAlert::Emergency ? L"Battery almost empty"
                         : level == alerts::LowAlert::Critical  ? L"Battery critically low"
                                                                : L"Low battery";
    const wchar_t* text = level == alerts::LowAlert::Emergency ? L"{name} is at {percent}%. Charge it now."
                                                               : L"{name} is at {percent}%";
    ShowNotification(e, T(title), TF(text, { { L"name", DisplayName(e) }, { L"percent", std::to_wstring(e.st.percent) } }));
    PlayBatteryAlertSound(alerts::AlertKind::BatteryLow);
    if (g_osdMode != osd_policy::OsdMode::Off) {
        const auto state = QueryNotificationState();
        const bool activeFs = g_osdMode == osd_policy::OsdMode::Always
                                  ? false
                                  : refresh_policy::IsActiveFullscreen(osd_overlay::IsForegroundFullscreen(), state);
        if (osd_policy::ShouldShowOsd(g_osdMode, activeFs)) {
            const Accent ac = AccentFor(e.st);
            const bool hasAccent = ac != Accent::None;
            const COLORREF accentColor = hasAccent ? AccentColorRef(ac, g_lightTaskbar) : 0;
            const wchar_t glyph = widget::DeviceGlyph(e.st.kind, e.st.via == L"Bluetooth");
            osd_overlay::Show(g_inst, glyph, DisplayName(e), e.st.percent, g_osdMode, g_osdCorner,
                              state, g_lightTaskbar, accentColor, hasAccent);
        }
    }
}

void ShowChargedAlert(TrayEntry& e) {
    ShowNotification(e, T(L"Fully charged"), TF(L"{name} is fully charged.", { { L"name", DisplayName(e) } }));
    PlayBatteryAlertSound(alerts::AlertKind::BatteryCharged);
}

void ShowChargeLimitAlert(TrayEntry& e) {
    ShowNotification(e, T(L"You can unplug it"),
                     TF(L"{name} is charged to {percent}%. Stopping around here helps the battery last longer.",
                        { { L"name", DisplayName(e) }, { L"percent", std::to_wstring(e.st.percent) } }));
    PlayBatteryAlertSound(alerts::AlertKind::BatteryChargeLimit);
}

// The time-to-empty estimate says the device will not last the next 12 hours: once per discharge.
void ShowShortRuntimeAlert(TrayEntry& e, const std::wstring& left) {
    ShowNotification(e, T(L"Charge today"), TF(L"{name} may run out in {time}", { { L"name", DisplayName(e) }, { L"time", left } }));
    PlayBatteryAlertSound(alerts::AlertKind::ShortRuntime);
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

void CheckEveningReminder(bool force) {
    if (g_demo || g_eveningReminderHour == 0 || !g_recordHistory || g_entries.empty()) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    const evening_reminder::LocalDateTime now{
        static_cast<int>(st.wYear),
        static_cast<int>(st.wMonth),
        static_cast<int>(st.wDay),
        static_cast<int>(st.wHour),
    };
    const int activeCycle = evening_reminder::ActiveCycleDate(now, g_eveningReminderHour);
    if (activeCycle == 0 || activeCycle == g_eveningReminderLastCycle) {
        g_eveningReminderCheckedCycle = 0;
        return;
    }
    const ULONGLONG nowTick = GetTickCount64();
    if (!force && activeCycle == g_eveningReminderCheckedCycle && nowTick - g_eveningReminderCheckedTick < 60000ULL)
        return;
    g_eveningReminderCheckedCycle = activeCycle;
    g_eveningReminderCheckedTick = nowTick;

    std::vector<evening_reminder::DeviceCandidate> candidates;
    for (TrayEntry* e : OrderedEntries(true)) {
        if (!e->st.connected || e->st.asleep || e->st.charging || e->st.full ||
            e->st.percent < 0 || e->st.percent > 100) {
            continue;
        }
        evening_reminder::DeviceCandidate c;
        c.name = DisplayName(*e);
        c.percent = e->st.percent;
        c.connected = e->st.connected;
        c.asleep = e->st.asleep;
        c.charging = e->st.charging || e->st.full;
        c.hidden = IsHidden(e->st.key);
        const battery_history::RuntimeEstimate est = battery_history::EstimateFor(e->st.key);
        if (est.state == battery_history::RuntimeState::Ready) {
            c.hasEta = true;
            c.etaHours = est.hoursRemaining;
        }
        candidates.push_back(std::move(c));
    }

    const evening_reminder::ReminderDecision decision = evening_reminder::Evaluate(
        now, g_eveningReminderHour, g_eveningReminderLastCycle, candidates,
        evening_reminder::kDefaultThresholdPercent, evening_reminder::kMaxEtaHours,
        T(L"{name} {percent}%"),
        T(L"{name} {percent}% (about {hours} h left)"),
        T(L"Charge tonight: {items}."));
    if (decision.notify) {
        g_eveningReminderLastCycle = decision.cycleDate;
        SaveEveningReminderLastCycle(decision.cycleDate);
        ShowTextNotification(T(L"Evening charge reminder"), decision.message);
    }
}

}  // namespace app
