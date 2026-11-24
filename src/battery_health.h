#pragma once
// Pure, Windows-independent battery health tracking (365-day daily summaries, full discharge cycles, and wear trend).
#include "history_model.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace battery_history {

constexpr int64_t kHealthRetentionSeconds = 365LL * 24 * 3600;
constexpr int64_t kSecondsPerDay = 86400;
constexpr int64_t kMaxDischargeGapSeconds = 7LL * 86400;
constexpr int kCycleStartMinPercent = 95;
constexpr int kCycleEndMaxPercent = 20;
constexpr int kMaxAllowedReboundPct = 3;
constexpr double kMinCycleHours = 1.0;
constexpr double kMaxCycleHours = 90.0 * 24.0;
constexpr double kMaxNormalizedCycleHours =
    kMaxCycleHours * 100.0 / static_cast<double>(kCycleStartMinPercent - kCycleEndMaxPercent);
constexpr size_t kMaxDailySummaries = 8192;
constexpr size_t kMaxCycles = 2048;
constexpr size_t kMaxActiveDischarges = 512;
constexpr size_t kMinCyclesForTrend = 3;
constexpr int64_t kMinHealthTimestamp = 946684800LL;
constexpr int64_t kMaxHealthTimestamp = 4102444800LL;
constexpr int kMaxDailySampleCount = 1000000;

struct DailySummary {
    std::wstring key;
    int64_t utcDay = 0;
    int minPercent = 100;
    int maxPercent = 0;
    int sampleCount = 0;
    bool sawCharging = false;
};

struct DischargeCycle {
    std::wstring key;
    int64_t completedAt = 0;
    double normalizedHours = 0.0;
};

struct ActiveDischarge {
    std::wstring key;
    int64_t startTime = 0;
    int startPercent = -1;
    int64_t lastTime = 0;
    int lastPercent = -1;
    bool active = false;
};

namespace detail {

inline bool ValidKey(const std::wstring& key) {
    return !key.empty() && key.size() <= kMaxKeyChars && key.find(L'\0') == std::wstring::npos;
}

inline bool SplitTabs(const std::string& row, std::vector<std::string>& fields, size_t expectedCount) {
    fields.clear();
    size_t start = 0;
    while (fields.size() < expectedCount) {
        const size_t tab = row.find('\t', start);
        if (fields.size() + 1 == expectedCount) {
            if (tab != std::string::npos) return false;
            fields.push_back(row.substr(start));
            return true;
        }
        if (tab == std::string::npos) return false;
        fields.push_back(row.substr(start, tab - start));
        start = tab + 1;
    }
    return false;
}

inline bool ValidDaily(const DailySummary& d) {
    return ValidKey(d.key) &&
           d.utcDay >= kMinHealthTimestamp / kSecondsPerDay &&
           d.utcDay <= kMaxHealthTimestamp / kSecondsPerDay &&
           d.minPercent >= 0 && d.maxPercent <= 100 && d.minPercent <= d.maxPercent &&
           d.sampleCount >= 1 && d.sampleCount <= kMaxDailySampleCount;
}

inline bool ValidCycle(const DischargeCycle& c) {
    return ValidKey(c.key) &&
           c.completedAt >= kMinHealthTimestamp && c.completedAt <= kMaxHealthTimestamp &&
           std::isfinite(c.normalizedHours) &&
           c.normalizedHours >= kMinCycleHours && c.normalizedHours <= kMaxNormalizedCycleHours;
}

inline bool ValidActive(const ActiveDischarge& a) {
    return a.active && ValidKey(a.key) &&
           a.startTime >= kMinHealthTimestamp &&
           a.lastTime >= a.startTime && a.lastTime <= kMaxHealthTimestamp &&
           a.startPercent >= kCycleStartMinPercent && a.startPercent <= 100 &&
           a.lastPercent > kCycleEndMaxPercent && a.lastPercent <= 100;
}

}  // namespace detail

inline std::string EncodeDaily(const DailySummary& d) {
    if (!detail::ValidDaily(d)) return {};
    return "D\t" + HexKey(d.key) + "\t" + std::to_string(d.utcDay) + "\t" +
           std::to_string(d.minPercent) + "\t" + std::to_string(d.maxPercent) + "\t" +
           std::to_string(d.sampleCount) + "\t" + (d.sawCharging ? "1\n" : "0\n");
}

inline bool DecodeDaily(const std::string& row, DailySummary& out) {
    std::vector<std::string> fields;
    if (!detail::SplitTabs(row, fields, 7) || fields[0] != "D") return false;
    DailySummary d;
    int64_t utcDay = 0, minP = 0, maxP = 0, count = 0, charge = 0;
    if (!UnhexKey(fields[1], d.key) ||
        !ParseUInt(fields[2], utcDay) ||
        !ParseUInt(fields[3], minP) ||
        !ParseUInt(fields[4], maxP) ||
        !ParseUInt(fields[5], count) ||
        !ParseUInt(fields[6], charge) ||
        charge > 1 || minP > 100 || maxP > 100 || count > kMaxDailySampleCount) {
        return false;
    }
    d.utcDay = utcDay;
    d.minPercent = static_cast<int>(minP);
    d.maxPercent = static_cast<int>(maxP);
    d.sampleCount = static_cast<int>(count);
    d.sawCharging = charge != 0;
    if (!detail::ValidDaily(d)) return false;
    out = std::move(d);
    return true;
}

inline std::string EncodeCycle(const DischargeCycle& c) {
    if (!detail::ValidCycle(c)) return {};
    const long long centiHours = std::llround(c.normalizedHours * 100.0);
    return "C\t" + HexKey(c.key) + "\t" + std::to_string(c.completedAt) + "\t" +
           std::to_string(centiHours) + "\n";
}

inline bool DecodeCycle(const std::string& row, DischargeCycle& out) {
    std::vector<std::string> fields;
    if (!detail::SplitTabs(row, fields, 4) || fields[0] != "C") return false;
    DischargeCycle c;
    int64_t completedAt = 0, centiHours = 0;
    if (!UnhexKey(fields[1], c.key) ||
        !ParseUInt(fields[2], completedAt) ||
        !ParseUInt(fields[3], centiHours)) {
        return false;
    }
    const int64_t maxCenti = static_cast<int64_t>(std::llround(kMaxNormalizedCycleHours * 100.0));
    if (centiHours < static_cast<int64_t>(kMinCycleHours * 100.0) || centiHours > maxCenti) return false;
    c.completedAt = completedAt;
    c.normalizedHours = static_cast<double>(centiHours) / 100.0;
    if (!detail::ValidCycle(c)) return false;
    out = std::move(c);
    return true;
}

inline std::string EncodeActive(const ActiveDischarge& a) {
    if (!detail::ValidActive(a)) return {};
    return "A\t" + HexKey(a.key) + "\t" + std::to_string(a.startTime) + "\t" +
           std::to_string(a.startPercent) + "\t" + std::to_string(a.lastTime) + "\t" +
           std::to_string(a.lastPercent) + "\n";
}

inline bool DecodeActive(const std::string& row, ActiveDischarge& out) {
    std::vector<std::string> fields;
    if (!detail::SplitTabs(row, fields, 6) || fields[0] != "A") return false;
    ActiveDischarge a;
    int64_t startTime = 0, startPct = 0, lastTime = 0, lastPct = 0;
    if (!UnhexKey(fields[1], a.key) ||
        !ParseUInt(fields[2], startTime) ||
        !ParseUInt(fields[3], startPct) ||
        !ParseUInt(fields[4], lastTime) ||
        !ParseUInt(fields[5], lastPct) ||
        startPct > 100 || lastPct > 100) {
        return false;
    }
    a.startTime = startTime;
    a.startPercent = static_cast<int>(startPct);
    a.lastTime = lastTime;
    a.lastPercent = static_cast<int>(lastPct);
    a.active = true;
    if (!detail::ValidActive(a)) return false;
    out = std::move(a);
    return true;
}

class HealthStore {
public:
    std::vector<DailySummary> dailies;
    std::vector<DischargeCycle> cycles;
    std::vector<ActiveDischarge> active;

    void Prune(int64_t now) {
        const int64_t minDay = (now - kHealthRetentionSeconds) / kSecondsPerDay;
        const int64_t maxDay = (now + 3600) / kSecondsPerDay;
        dailies.erase(std::remove_if(dailies.begin(), dailies.end(), [&](const DailySummary& d) {
            return !detail::ValidDaily(d) || d.utcDay < minDay || d.utcDay > maxDay;
        }), dailies.end());
        if (dailies.size() > kMaxDailySummaries) {
            dailies.erase(dailies.begin(), dailies.begin() + (dailies.size() - kMaxDailySummaries));
        }

        cycles.erase(std::remove_if(cycles.begin(), cycles.end(), [&](const DischargeCycle& c) {
            return !detail::ValidCycle(c) || c.completedAt < now - kHealthRetentionSeconds ||
                   c.completedAt > now + 3600;
        }), cycles.end());
        if (cycles.size() > kMaxCycles) {
            cycles.erase(cycles.begin(), cycles.begin() + (cycles.size() - kMaxCycles));
        }

        active.erase(std::remove_if(active.begin(), active.end(), [&](const ActiveDischarge& a) {
            return !detail::ValidActive(a) || a.lastTime > now + 3600 ||
                   now - a.lastTime > kMaxDischargeGapSeconds;
        }), active.end());
        if (active.size() > kMaxActiveDischarges) {
            active.erase(active.begin(), active.begin() + (active.size() - kMaxActiveDischarges));
        }
    }

    bool Record(const Sample& s) {
        if (!Valid(s)) return false;

        const int64_t utcDay = s.timestamp / kSecondsPerDay;
        DailySummary* summary = FindDaily(s.key, utcDay);
        if (!summary) {
            dailies.push_back({s.key, utcDay, s.percent, s.percent, 1, s.charging});
        } else {
            summary->minPercent = std::min(summary->minPercent, s.percent);
            summary->maxPercent = std::max(summary->maxPercent, s.percent);
            if (summary->sampleCount < kMaxDailySampleCount) ++summary->sampleCount;
            summary->sawCharging = summary->sawCharging || s.charging;
        }

        UpdateActiveDischarge(s);
        Prune(s.timestamp);
        return true;
    }

    void SeedFromHistory(const std::vector<Sample>& history, int64_t now) {
        dailies.clear();
        cycles.clear();
        active.clear();
        std::vector<Sample> ordered = history;
        std::stable_sort(ordered.begin(), ordered.end(), [](const Sample& a, const Sample& b) {
            return a.timestamp < b.timestamp;
        });
        for (const auto& s : ordered) Record(s);
        Prune(now);
    }

    void Clear(const std::wstring& key) {
        auto matchesKey = [&](const auto& item) { return item.key == key; };
        dailies.erase(std::remove_if(dailies.begin(), dailies.end(), matchesKey), dailies.end());
        cycles.erase(std::remove_if(cycles.begin(), cycles.end(), matchesKey), cycles.end());
        active.erase(std::remove_if(active.begin(), active.end(), matchesKey), active.end());
    }

    std::string Serialize() const {
        std::string out;
        for (const auto& d : dailies) out += EncodeDaily(d);
        for (const auto& c : cycles) out += EncodeCycle(c);
        for (const auto& a : active) out += EncodeActive(a);
        return out;
    }

    void Load(const std::string& content, int64_t now) {
        dailies.clear();
        cycles.clear();
        active.clear();
        size_t start = 0;
        while (start < content.size()) {
            const size_t end = content.find('\n', start);
            if (end == std::string::npos) break;  // ignore incomplete final write
            std::string row = content.substr(start, end - start);
            if (!row.empty() && row.back() == '\r') row.pop_back();
            start = end + 1;

            if (row.size() < 2 || row[1] != '\t') continue;
            if (row[0] == 'D') {
                DailySummary d;
                if (!DecodeDaily(row, d)) continue;
                if (DailySummary* existing = FindDaily(d.key, d.utcDay)) {
                    existing->minPercent = std::min(existing->minPercent, d.minPercent);
                    existing->maxPercent = std::max(existing->maxPercent, d.maxPercent);
                    existing->sampleCount = std::min(kMaxDailySampleCount, existing->sampleCount + d.sampleCount);
                    existing->sawCharging = existing->sawCharging || d.sawCharging;
                } else {
                    dailies.push_back(std::move(d));
                    if (dailies.size() > kMaxDailySummaries) {
                        dailies.erase(dailies.begin(), dailies.begin() + (dailies.size() - kMaxDailySummaries));
                    }
                }
            } else if (row[0] == 'C') {
                DischargeCycle c;
                if (!DecodeCycle(row, c)) continue;
                cycles.push_back(std::move(c));
                if (cycles.size() > kMaxCycles) {
                    cycles.erase(cycles.begin(), cycles.begin() + (cycles.size() - kMaxCycles));
                }
            } else if (row[0] == 'A') {
                ActiveDischarge a;
                if (!DecodeActive(row, a)) continue;
                auto it = FindActiveIt(a.key);
                if (it != active.end()) {
                    *it = std::move(a);
                } else {
                    active.push_back(std::move(a));
                    if (active.size() > kMaxActiveDischarges) {
                        active.erase(active.begin(), active.begin() + (active.size() - kMaxActiveDischarges));
                    }
                }
            }
        }
        Prune(now);
    }

private:
    DailySummary* FindDaily(const std::wstring& key, int64_t utcDay) {
        for (auto it = dailies.rbegin(); it != dailies.rend(); ++it) {
            if (it->key == key && it->utcDay == utcDay) return &(*it);
        }
        return nullptr;
    }

    std::vector<ActiveDischarge>::iterator FindActiveIt(const std::wstring& key) {
        for (auto it = active.begin(); it != active.end(); ++it) {
            if (it->key == key) return it;
        }
        return active.end();
    }

    void UpdateActiveDischarge(const Sample& s) {
        auto it = FindActiveIt(s.key);
        if (s.charging) {
            if (it != active.end()) active.erase(it);
            return;
        }
        if (it != active.end()) {
            const ActiveDischarge& a = *it;
            if (!a.active || s.timestamp < a.lastTime ||
                s.timestamp - a.lastTime > kMaxDischargeGapSeconds ||
                s.percent > a.lastPercent + kMaxAllowedReboundPct) {
                active.erase(it);
                it = active.end();
            }
        }
        if (it != active.end()) {
            ActiveDischarge& a = *it;
            if (s.percent >= kCycleStartMinPercent && s.percent >= a.startPercent) {
                // Sitting topped up before discharge drops should not inflate active cycle hours.
                a.startTime = s.timestamp;
                a.startPercent = s.percent;
                a.lastTime = s.timestamp;
                a.lastPercent = s.percent;
            } else {
                a.lastTime = s.timestamp;
                a.lastPercent = s.percent;
                if (s.percent <= kCycleEndMaxPercent) {
                    const double elapsedHours = static_cast<double>(s.timestamp - a.startTime) / 3600.0;
                    const int drop = a.startPercent - s.percent;
                    if (drop >= (kCycleStartMinPercent - kCycleEndMaxPercent) &&
                        elapsedHours >= kMinCycleHours && elapsedHours <= kMaxCycleHours) {
                        const double norm = elapsedHours * 100.0 / static_cast<double>(drop);
                        cycles.push_back({s.key, s.timestamp, norm});
                    }
                    active.erase(it);
                }
            }
            return;
        }
        if (s.percent >= kCycleStartMinPercent) {
            active.push_back({s.key, s.timestamp, s.percent, s.timestamp, s.percent, true});
        }
    }
};

enum class WearState { Insufficient, Ready };

struct WearSummary {
    WearState state = WearState::Insufficient;
    size_t cycleCount = 0;
    int earlyHours = 0;
    int recentHours = 0;
    int changePercent = 0;
};

inline WearSummary EvaluateWear(const HealthStore& store, const std::wstring& key) {
    WearSummary out;
    std::vector<DischargeCycle> devCycles;
    for (const auto& c : store.cycles) {
        if (c.key == key && std::isfinite(c.normalizedHours) && c.normalizedHours > 0.0) {
            devCycles.push_back(c);
        }
    }
    out.cycleCount = devCycles.size();
    if (out.cycleCount < kMinCyclesForTrend) return out;

    std::stable_sort(devCycles.begin(), devCycles.end(),
                     [](const DischargeCycle& a, const DischargeCycle& b) {
                         return a.completedAt < b.completedAt;
                     });

    const int64_t firstTime = devCycles.front().completedAt;
    const int64_t firstMonthEnd = firstTime + 30LL * kSecondsPerDay;
    size_t baselineCount = 0;
    while (baselineCount < out.cycleCount && devCycles[baselineCount].completedAt <= firstMonthEnd) {
        ++baselineCount;
    }

    double earlySum = 0.0;
    size_t earlyN = 0;
    double recentSum = 0.0;
    size_t recentN = 0;
    if (baselineCount == out.cycleCount) {
        const size_t n = std::max<size_t>(1, out.cycleCount / 3);
        earlyN = n;
        recentN = n;
        for (size_t i = 0; i < n; ++i) earlySum += devCycles[i].normalizedHours;
        for (size_t i = out.cycleCount - n; i < out.cycleCount; ++i) recentSum += devCycles[i].normalizedHours;
    } else {
        earlyN = baselineCount;
        for (size_t i = 0; i < baselineCount; ++i) earlySum += devCycles[i].normalizedHours;
        const size_t targetRecent = std::min<size_t>(3, std::max<size_t>(1, out.cycleCount / 3));
        recentN = std::min(targetRecent, out.cycleCount - baselineCount);
        for (size_t i = out.cycleCount - recentN; i < out.cycleCount; ++i) recentSum += devCycles[i].normalizedHours;
    }

    const double earlyAvg = earlySum / static_cast<double>(earlyN);
    const double recentAvg = recentSum / static_cast<double>(recentN);
    out.state = WearState::Ready;
    out.earlyHours = std::max(1, static_cast<int>(std::llround(earlyAvg)));
    out.recentHours = std::max(1, static_cast<int>(std::llround(recentAvg)));
    out.changePercent = (earlyAvg > 0.0)
        ? static_cast<int>(std::llround((recentAvg - earlyAvg) * 100.0 / earlyAvg))
        : 0;
    return out;
}

inline std::wstring FormatChangePercent(int changePercent) {
    if (changePercent < 0) {
        const long long absVal = -static_cast<long long>(changePercent);
        return L"\u2212" + std::to_wstring(absVal) + L"%";
    }
    if (changePercent > 0) return L"+" + std::to_wstring(changePercent) + L"%";
    return L"0%";
}

}  // namespace battery_history
