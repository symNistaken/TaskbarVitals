#pragma once
// Pure, Windows-independent policy and message formatting for the evening charge reminder.
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace evening_reminder {

constexpr int kHours[] = { 20, 21, 22, 23 };
constexpr int kHourCount = static_cast<int>(sizeof(kHours) / sizeof(kHours[0]));
constexpr int kDefaultThresholdPercent = 30;
constexpr double kMaxEtaHours = 24.0;
constexpr int kCatchUpCutoffHour = 3;

inline int ClampReminderHour(int raw) {
    for (int h : kHours) {
        if (raw == h) return h;
    }
    return 0;
}

struct LocalDateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
};

inline bool IsLeapYear(int year) {
    return year > 0 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

inline int DaysInMonth(int year, int month) {
    switch (month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12: return 31;
        case 4: case 6: case 9: case 11: return 30;
        case 2: return IsLeapYear(year) ? 29 : 28;
        default: return 0;
    }
}

inline bool IsValidDate(int year, int month, int day) {
    const int dim = DaysInMonth(year, month);
    return year >= 1 && year <= 9999 && dim > 0 && day >= 1 && day <= dim;
}

inline int PackDateKey(int year, int month, int day) {
    if (!IsValidDate(year, month, day)) return 0;
    return year * 10000 + month * 100 + day;
}

inline int PreviousDay(int year, int month, int day) {
    if (!IsValidDate(year, month, day)) return 0;
    if (day > 1) return PackDateKey(year, month, day - 1);
    if (month > 1) return PackDateKey(year, month - 1, DaysInMonth(year, month - 1));
    if (year <= 1) return 0;
    return PackDateKey(year - 1, 12, 31);
}

// Returns the YYYYMMDD key of the active evening reminder cycle, or 0 outside the window.
// Catch-up after wake is allowed before 03:00 and maps to the previous calendar day's cycle,
// ensuring at most one reminder per evening even across midnight.
inline int ActiveCycleDate(const LocalDateTime& now, int reminderHour) {
    const int targetHour = ClampReminderHour(reminderHour);
    if (targetHour == 0 || !IsValidDate(now.year, now.month, now.day) || now.hour < 0 || now.hour > 23)
        return 0;
    if (now.hour >= targetHour) return PackDateKey(now.year, now.month, now.day);
    if (now.hour < kCatchUpCutoffHour) return PreviousDay(now.year, now.month, now.day);
    return 0;
}

struct DeviceCandidate {
    std::wstring name;
    int percent = -1;
    bool connected = true;
    bool asleep = false;
    bool charging = false;
    bool hidden = false;
    bool hasEta = false;
    double etaHours = 0.0;
};

inline bool HasUsableEta(const DeviceCandidate& d, double maxEtaHours = kMaxEtaHours) {
    return d.hasEta && std::isfinite(d.etaHours) && d.etaHours >= 0.0 && d.etaHours <= maxEtaHours;
}

inline bool NeedsChargeTonight(const DeviceCandidate& d, int thresholdPercent = kDefaultThresholdPercent,
                               double maxEtaHours = kMaxEtaHours) {
    if (!d.connected || d.asleep || d.charging || d.hidden) return false;
    if (d.percent < 0 || d.percent > 100) return false;
    return d.percent < thresholdPercent || HasUsableEta(d, maxEtaHours);
}

namespace detail {

// Single-pass substitution prevents device names containing "{...}" from expanding as placeholders.
inline std::wstring Substitute(const std::wstring& tpl,
                               std::initializer_list<std::pair<const wchar_t*, std::wstring>> args) {
    std::wstring out;
    out.reserve(tpl.size() + 32);
    for (size_t i = 0; i < tpl.size();) {
        if (tpl[i] == L'{') {
            bool replaced = false;
            for (const auto& [key, val] : args) {
                const std::wstring tag = std::wstring(L"{") + key + L"}";
                if (tpl.compare(i, tag.size(), tag) == 0) {
                    out += val;
                    i += tag.size();
                    replaced = true;
                    break;
                }
            }
            if (replaced) continue;
        }
        out.push_back(tpl[i++]);
    }
    return out;
}

}  // namespace detail

inline std::wstring FormatReminderMessage(
    const std::vector<DeviceCandidate>& qualifying,
    const std::wstring& itemNoEtaTpl = L"{name} {percent}%",
    const std::wstring& itemWithEtaTpl = L"{name} {percent}% (about {hours} h left)",
    const std::wstring& summaryTpl = L"Charge tonight: {items}.",
    double maxEtaHours = kMaxEtaHours) {
    if (qualifying.empty()) return L"";
    std::wstring items;
    for (size_t i = 0; i < qualifying.size(); ++i) {
        if (i > 0) items += L", ";
        const auto& d = qualifying[i];
        const std::wstring pct = std::to_wstring(d.percent);
        if (HasUsableEta(d, maxEtaHours)) {
            const int rounded = std::max(1, static_cast<int>(std::llround(d.etaHours)));
            items += detail::Substitute(itemWithEtaTpl, {
                { L"name", d.name },
                { L"percent", pct },
                { L"hours", std::to_wstring(rounded) },
            });
        } else {
            items += detail::Substitute(itemNoEtaTpl, {
                { L"name", d.name },
                { L"percent", pct },
            });
        }
    }
    return detail::Substitute(summaryTpl, { { L"items", items } });
}

struct ReminderDecision {
    bool notify = false;
    int cycleDate = 0;
    std::wstring message;
};

inline ReminderDecision Evaluate(
    const LocalDateTime& now,
    int reminderHour,
    int lastCycleDate,
    const std::vector<DeviceCandidate>& devices,
    int thresholdPercent = kDefaultThresholdPercent,
    double maxEtaHours = kMaxEtaHours,
    const std::wstring& itemNoEtaTpl = L"{name} {percent}%",
    const std::wstring& itemWithEtaTpl = L"{name} {percent}% (about {hours} h left)",
    const std::wstring& summaryTpl = L"Charge tonight: {items}.") {
    ReminderDecision out;
    const int cycle = ActiveCycleDate(now, reminderHour);
    if (cycle == 0 || cycle == lastCycleDate) return out;

    std::vector<DeviceCandidate> qualifying;
    qualifying.reserve(devices.size());
    for (const auto& d : devices) {
        if (NeedsChargeTonight(d, thresholdPercent, maxEtaHours))
            qualifying.push_back(d);
    }
    if (qualifying.empty()) return out;

    out.notify = true;
    out.cycleDate = cycle;
    out.message = FormatReminderMessage(qualifying, itemNoEtaTpl, itemWithEtaTpl, summaryTpl, maxEtaHours);
    return out;
}

}  // namespace evening_reminder
