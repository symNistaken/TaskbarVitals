#include "battery_intelligence.h"
#include "i18n.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>

namespace battery_intelligence {
namespace {

bool g_demo = false;
SRWLOCK g_lock = SRWLOCK_INIT;

// File paths
std::wstring ChargingHistoryPath() {
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    std::wstring folder = std::wstring(appdata) + L"\\WirelessBattery";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder + L"\\charging_history.tsv";
}

std::wstring BatteryHealthPath() {
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    std::wstring folder = std::wstring(appdata) + L"\\WirelessBattery";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder + L"\\battery_health.tsv";
}

// Active in-progress charging session tracking
struct ActiveCharge {
    std::wstring deviceName;
    int64_t startTime = 0;
    int64_t lastTime = 0;
    int startPercent = -1;
    int lastPercent = -1;
    bool reportedFull = false;
};

std::map<std::wstring, ActiveCharge> g_activeCharges;
std::vector<ChargingSession> g_chargingSessions;
constexpr size_t kMaxChargingSessions = 500;

// Health records: key -> list of (timestamp, healthPercent, designMwh, fullMwh)
struct HealthEntry {
    int64_t timestamp = 0;
    int healthPercent = 0;
    uint32_t designMwh = 0;
    uint32_t fullMwh = 0;
};
std::map<std::wstring, std::vector<HealthEntry>> g_healthHistory;

// Utilities for hex encoding device keys in TSV to prevent format corruption
std::string HexEncode(const std::wstring& str) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(str.size() * 8);
    for (wchar_t wc : str) {
        uint32_t c = static_cast<uint32_t>(wc);
        for (int shift = 28; shift >= 0; shift -= 4)
            out.push_back(digits[(c >> shift) & 15]);
    }
    return out;
}

bool HexDecode(const std::string& encoded, std::wstring& out) {
    if (encoded.empty() || encoded.size() % 8 != 0) return false;
    std::wstring decoded;
    for (size_t i = 0; i < encoded.size(); i += 8) {
        uint32_t n = 0;
        for (size_t j = i; j < i + 8; ++j) {
            char c = encoded[j];
            int v = c >= '0' && c <= '9' ? c - '0'
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (v < 0) return false;
            n = (n << 4) | static_cast<uint32_t>(v);
        }
        decoded.push_back(static_cast<wchar_t>(n));
    }
    out = std::move(decoded);
    return true;
}

void SaveChargingHistoryDisk() {
    if (g_demo) return;
    std::wstring path = ChargingHistoryPath();
    if (path.empty()) return;
    std::string content;
    for (const auto& s : g_chargingSessions) {
        content += std::to_string(s.startTime) + "\t" +
                   std::to_string(s.endTime) + "\t" +
                   HexEncode(s.deviceKey) + "\t" +
                   HexEncode(s.deviceName) + "\t" +
                   std::to_string(s.startPercent) + "\t" +
                   std::to_string(s.endPercent) + "\t" +
                   std::to_string(s.durationMinutes) + "\t" +
                   (s.completed ? "1\n" : "0\n");
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(f, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
        CloseHandle(f);
    }
}

void LoadChargingHistoryDisk() {
    if (g_demo) return;
    g_chargingSessions.clear();
    std::wstring path = ChargingHistoryPath();
    if (path.empty()) return;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 2 * 1024 * 1024) {
        CloseHandle(f);
        return;
    }
    std::string data(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD got = 0;
    ReadFile(f, &data[0], static_cast<DWORD>(data.size()), &got, nullptr);
    CloseHandle(f);

    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::stringstream ls(line);
        std::string sStart, sEnd, sKey, sName, sSPct, sEPct, sDur, sComp;
        if (std::getline(ls, sStart, '\t') &&
            std::getline(ls, sEnd, '\t') &&
            std::getline(ls, sKey, '\t') &&
            std::getline(ls, sName, '\t') &&
            std::getline(ls, sSPct, '\t') &&
            std::getline(ls, sEPct, '\t') &&
            std::getline(ls, sDur, '\t') &&
            std::getline(ls, sComp, '\t')) {
            ChargingSession s;
            try {
                s.startTime = std::stoll(sStart);
                s.endTime = std::stoll(sEnd);
                if (!HexDecode(sKey, s.deviceKey)) continue;
                HexDecode(sName, s.deviceName);
                s.startPercent = std::stoi(sSPct);
                s.endPercent = std::stoi(sEPct);
                s.durationMinutes = std::stoi(sDur);
                s.completed = (sComp == "1");
                g_chargingSessions.push_back(s);
            } catch (...) {}
        }
    }
    if (g_chargingSessions.size() > kMaxChargingSessions) {
        g_chargingSessions.erase(g_chargingSessions.begin(),
            g_chargingSessions.begin() + (g_chargingSessions.size() - kMaxChargingSessions));
    }
}

void SaveHealthDisk() {
    if (g_demo) return;
    std::wstring path = BatteryHealthPath();
    if (path.empty()) return;
    std::string content;
    for (const auto& [key, list] : g_healthHistory) {
        for (const auto& h : list) {
            content += std::to_string(h.timestamp) + "\t" +
                       HexEncode(key) + "\t" +
                       std::to_string(h.healthPercent) + "\t" +
                       std::to_string(h.designMwh) + "\t" +
                       std::to_string(h.fullMwh) + "\n";
        }
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(f, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
        CloseHandle(f);
    }
}

void LoadHealthDisk() {
    if (g_demo) return;
    g_healthHistory.clear();
    std::wstring path = BatteryHealthPath();
    if (path.empty()) return;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 2 * 1024 * 1024) {
        CloseHandle(f);
        return;
    }
    std::string data(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD got = 0;
    ReadFile(f, &data[0], static_cast<DWORD>(data.size()), &got, nullptr);
    CloseHandle(f);

    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::stringstream ls(line);
        std::string sTime, sKey, sHealth, sDesign, sFull;
        if (std::getline(ls, sTime, '\t') &&
            std::getline(ls, sKey, '\t') &&
            std::getline(ls, sHealth, '\t') &&
            std::getline(ls, sDesign, '\t') &&
            std::getline(ls, sFull, '\t')) {
            try {
                HealthEntry h;
                h.timestamp = std::stoll(sTime);
                std::wstring key;
                if (!HexDecode(sKey, key)) continue;
                h.healthPercent = std::stoi(sHealth);
                h.designMwh = static_cast<uint32_t>(std::stoul(sDesign));
                h.fullMwh = static_cast<uint32_t>(std::stoul(sFull));
                g_healthHistory[key].push_back(h);
            } catch (...) {}
        }
    }
}

} // namespace

void Initialize(bool demo) {
    AcquireSRWLockExclusive(&g_lock);
    g_demo = demo;
    if (!demo) {
        LoadChargingHistoryDisk();
        LoadHealthDisk();
    } else {
        // Pre-populate some demo charging records
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        g_chargingSessions = {
            { L"demo:logi", L"Logitech G PRO X 2 LIGHTSPEED", now - 86400 * 2, now - 86400 * 2 + 7200, 20, 100, 120, true },
            { L"razer:demo", L"Razer DeathAdder V3 Pro", now - 86400 * 3, now - 86400 * 3 + 4800, 35, 85, 80, false },
            { L"steelseries:demo", L"SteelSeries Arctis Nova 7", now - 86400 * 4, now - 86400 * 4 + 5400, 15, 95, 90, false },
            { L"xbox:demo", L"Xbox Wireless Controller", now - 86400 * 5, now - 86400 * 5 + 9000, 10, 100, 150, true },
        };
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void Shutdown() {
    AcquireSRWLockExclusive(&g_lock);
    SaveChargingHistoryDisk();
    SaveHealthDisk();
    ReleaseSRWLockExclusive(&g_lock);
}

void ObserveDevice(const DeviceStatus& s, int64_t now) {
    AcquireSRWLockExclusive(&g_lock);

    // 1. Charging tracking
    bool isCharging = s.connected && !s.asleep && s.charging;
    auto itCharge = g_activeCharges.find(s.key);

    if (isCharging) {
        if (itCharge == g_activeCharges.end()) {
            // New charge session started
            ActiveCharge c;
            c.deviceName = s.name;
            c.startTime = now;
            c.lastTime = now;
            c.startPercent = s.percent >= 0 ? s.percent : 0;
            c.lastPercent = c.startPercent;
            c.reportedFull = s.full || s.percent >= 100;
            g_activeCharges[s.key] = c;
        } else {
            // Ongoing charge session
            itCharge->second.lastTime = now;
            if (s.percent >= 0) itCharge->second.lastPercent = s.percent;
            if (s.full || s.percent >= 100) itCharge->second.reportedFull = true;
        }
    } else {
        if (itCharge != g_activeCharges.end()) {
            // Charging stopped: record session if it lasted at least 60 seconds
            const ActiveCharge& c = itCharge->second;
            int64_t durSec = now - c.startTime;
            if (durSec >= 60 && c.startPercent >= 0) {
                ChargingSession sess;
                sess.deviceKey = s.key;
                sess.deviceName = c.deviceName.empty() ? s.name : c.deviceName;
                sess.startTime = c.startTime;
                sess.endTime = now;
                sess.startPercent = c.startPercent;
                sess.endPercent = s.percent >= 0 ? s.percent : c.lastPercent;
                sess.durationMinutes = static_cast<int>(durSec / 60);
                sess.completed = c.reportedFull || sess.endPercent >= 98 || s.full;
                g_chargingSessions.push_back(sess);
                if (g_chargingSessions.size() > kMaxChargingSessions) {
                    g_chargingSessions.erase(g_chargingSessions.begin());
                }
                SaveChargingHistoryDisk();
            }
            g_activeCharges.erase(itCharge);
        }
    }

    // 2. Battery Health tracking if device reports capacity
    if (s.hasCapacity && s.designCapacityMwh > 0 && s.fullChargeCapacityMwh > 0) {
        int health = static_cast<int>((static_cast<uint64_t>(s.fullChargeCapacityMwh) * 100) / s.designCapacityMwh);
        health = std::clamp(health, 0, 150); // Cap at 150% in case of slight manufacturing over-provisioning
        auto& list = g_healthHistory[s.key];
        bool shouldAdd = list.empty();
        if (!list.empty()) {
            // Record only if changed or > 24 hours since last sample
            const auto& last = list.back();
            if (last.healthPercent != health || (now - last.timestamp) >= 86400) {
                shouldAdd = true;
            }
        }
        if (shouldAdd) {
            list.push_back({ now, health, s.designCapacityMwh, s.fullChargeCapacityMwh });
            if (list.size() > 100) list.erase(list.begin());
            SaveHealthDisk();
        }
    }

    ReleaseSRWLockExclusive(&g_lock);
}

DrainRateResult CalculateDrainRate(const std::wstring& deviceKey,
                                   const std::wstring& deviceName,
                                   const std::vector<battery_history::Sample>& inputSamples,
                                   int currentPercent,
                                   bool isCharging,
                                   int64_t now) {
    DrainRateResult result;
    (void)deviceKey;

    if (isCharging) {
        result.statusReason = T(L"Currently charging");
        return result;
    }
    if (inputSamples.empty()) {
        result.statusReason = T(L"Insufficient data");
        return result;
    }

    // Sort samples chronologically
    std::vector<battery_history::Sample> points = inputSamples;
    std::stable_sort(points.begin(), points.end(), [](const battery_history::Sample& a, const battery_history::Sample& b) {
        return a.timestamp < b.timestamp;
    });

    const auto& latest = points.back();
    // Check for stale readings (no updates in > 3 hours)
    if (now - latest.timestamp > 3 * 3600) {
        result.statusReason = T(L"No recent readings");
        return result;
    }
    if (latest.charging) {
        result.statusReason = T(L"Currently charging");
        return result;
    }

    // Find the current continuous discharge segment backwards
    size_t start = points.size() - 1;
    while (start > 0) {
        const auto& prev = points[start - 1];
        const auto& curr = points[start];
        const int64_t gap = curr.timestamp - prev.timestamp;
        const int diff = curr.percent - prev.percent;

        // Break on:
        // - charging state flag
        // - excessive gap > 4 hours (sleep / disconnected period)
        // - unexpected battery jump > 1% (sensor noise or partial charge)
        // - abnormal single-step drop > 25% (reporting anomaly)
        if (prev.charging || curr.charging || gap <= 0 || gap > 4 * 3600 || diff > 1 || diff < -25) {
            break;
        }
        --start;
    }

    result.sampleCount = points.size() - start;
    if (result.sampleCount < 3) {
        result.statusReason = T(L"Insufficient data");
        return result;
    }

    result.observationHours = static_cast<double>(latest.timestamp - points[start].timestamp) / 3600.0;
    if (result.observationHours < 1.0) {
        result.statusReason = T(L"Insufficient observation period");
        return result;
    }

    result.percentDropped = points[start].percent - latest.percent;
    if (result.percentDropped < 2) {
        result.statusReason = T(L"Insufficient decline");
        return result;
    }

    result.percentPerHour = static_cast<double>(result.percentDropped) / result.observationHours;
    // Anomaly filtering: < 0.05%/hr or > 15%/hr are reporting glitches or extreme outliers
    if (result.percentPerHour < 0.05 || result.percentPerHour > 15.0) {
        result.statusReason = T(L"Drain rate outside normal range");
        return result;
    }

    result.percentPerDay = result.percentPerHour * 24.0;
    int effPct = currentPercent >= 0 ? currentPercent : latest.percent;
    result.hoursRemaining = effPct > 0 ? (static_cast<double>(effPct) / result.percentPerHour) : 0.0;
    result.hasData = true;
    result.isMeasured = (result.sampleCount >= 4 && result.observationHours >= 2.0);

    // Format human readable summary message
    // e.g. "Your Logitech mouse has lost 18% battery in the last 6 hours. At this rate, approximately 33 hours remain."
    std::wstring name = deviceName.empty() ? L"Device" : deviceName;
    int obsHrsRounded = static_cast<int>(std::round(result.observationHours));
    int remHrsRounded = static_cast<int>(std::round(result.hoursRemaining));

    result.summaryMessage = TF(L"{name} has lost {drop}% battery in the last {hours} hours. At this rate, approximately {rem} hours remain.", {
        { L"name", name },
        { L"drop", std::to_wstring(result.percentDropped) },
        { L"hours", std::to_wstring(obsHrsRounded > 0 ? obsHrsRounded : 1) },
        { L"rem", std::to_wstring(remHrsRounded) }
    });

    int rateTenths = static_cast<int>(std::round(result.percentPerHour * 10.0));
    int dayTenths = static_cast<int>(std::round(result.percentPerDay * 10.0));
    result.rateLabel = std::to_wstring(rateTenths / 10) + L"." + std::to_wstring(rateTenths % 10) +
                       L"%/hr (~" + std::to_wstring(dayTenths / 10) + L"." + std::to_wstring(dayTenths % 10) + L"%/day)";

    return result;
}

FullChargeRuntimeResult EstimateFullChargeRuntime(const std::wstring& deviceKey,
                                                 const std::vector<battery_history::Sample>& inputSamples,
                                                 int64_t now) {
    (void)deviceKey;
    FullChargeRuntimeResult result;
    if (inputSamples.empty()) return result;

    std::vector<battery_history::Sample> points = inputSamples;
    std::stable_sort(points.begin(), points.end(), [](const battery_history::Sample& a, const battery_history::Sample& b) {
        return a.timestamp < b.timestamp;
    });

    // Scan history for discharge segments (minimum 4 samples, >= 2 hours, >= 4% drop)
    std::vector<double> sessionRuntimes;
    size_t segStart = 0;

    for (size_t i = 1; i < points.size(); ++i) {
        const auto& prev = points[i - 1];
        const auto& curr = points[i];
        const int64_t gap = curr.timestamp - prev.timestamp;
        const int diff = curr.percent - prev.percent;

        bool segmentBreak = prev.charging || curr.charging || gap > 4 * 3600 || diff > 1 || diff < -25;
        if (segmentBreak || i == points.size() - 1) {
            size_t segEnd = segmentBreak ? i - 1 : i;
            if (segEnd > segStart) {
                double durHrs = static_cast<double>(points[segEnd].timestamp - points[segStart].timestamp) / 3600.0;
                int drop = points[segStart].percent - points[segEnd].percent;
                if (durHrs >= 2.0 && drop >= 4 && (segEnd - segStart + 1) >= 4) {
                    double rate = static_cast<double>(drop) / durHrs;
                    if (rate >= 0.05 && rate <= 15.0) {
                        double fullRuntime = 100.0 / rate;
                        if (fullRuntime >= 4.0 && fullRuntime <= 90.0 * 24.0) {
                            sessionRuntimes.push_back(fullRuntime);
                        }
                    }
                }
            }
            segStart = i;
        }
    }

    if (sessionRuntimes.empty()) {
        // Fall back to drain rate on current window if valid
        auto drain = CalculateDrainRate(deviceKey, L"", inputSamples, 100, false, now);
        if (drain.hasData && drain.percentPerHour > 0.05) {
            double runtime = 100.0 / drain.percentPerHour;
            if (runtime >= 4.0 && runtime <= 90.0 * 24.0) {
                sessionRuntimes.push_back(runtime);
            }
        }
    }

    if (!sessionRuntimes.empty()) {
        result.hasEstimate = true;
        result.historicalSessions = sessionRuntimes.size();
        result.mostRecentHours = sessionRuntimes.back();

        double sum = 0.0;
        for (double r : sessionRuntimes) sum += r;
        result.averageHours = sum / sessionRuntimes.size();

        result.displayLabel = TF(L"Estimated full-charge runtime: {runtime}", {
            { L"runtime", FormatDurationHours(result.averageHours) }
        });
        result.recentLabel = TF(L"Recent session: {runtime}", {
            { L"runtime", FormatDurationHours(result.mostRecentHours) }
        });
        result.averageLabel = TF(L"Historical average: {runtime}", {
            { L"runtime", FormatDurationHours(result.averageHours) }
        });
    }

    return result;
}

BatteryHealthResult GetBatteryHealth(const std::wstring& deviceKey,
                                    const DeviceStatus& s) {
    BatteryHealthResult result;
    AcquireSRWLockShared(&g_lock);

    // Live capacity if exposed by device
    if (s.hasCapacity && s.designCapacityMwh > 0 && s.fullChargeCapacityMwh > 0) {
        result.supported = true;
        result.designCapacityMwh = s.designCapacityMwh;
        result.fullChargeCapacityMwh = s.fullChargeCapacityMwh;
        result.healthPercent = static_cast<int>((static_cast<uint64_t>(s.fullChargeCapacityMwh) * 100) / s.designCapacityMwh);
        result.healthPercent = std::clamp(result.healthPercent, 0, 150);
        result.displayText = std::to_wstring(result.healthPercent) + L"%";
    }

    // Historical entries for this device
    auto it = g_healthHistory.find(deviceKey);
    if (it != g_healthHistory.end() && !it->second.empty()) {
        if (!result.supported) {
            // Device disconnected but had historical health readings
            const auto& last = it->second.back();
            result.supported = true;
            result.designCapacityMwh = last.designMwh;
            result.fullChargeCapacityMwh = last.fullMwh;
            result.healthPercent = last.healthPercent;
            result.displayText = std::to_wstring(result.healthPercent) + L"%";
        }
        for (const auto& h : it->second) {
            result.history.push_back({ h.timestamp, h.healthPercent });
        }
    }

    if (!result.supported) {
        result.displayText = T(L"Battery health unavailable");
    }

    ReleaseSRWLockShared(&g_lock);
    return result;
}

std::vector<ChargingSession> GetChargingHistory(const std::wstring& deviceKey) {
    AcquireSRWLockShared(&g_lock);
    std::vector<ChargingSession> out;
    for (const auto& s : g_chargingSessions) {
        if (s.deviceKey == deviceKey) out.push_back(s);
    }
    ReleaseSRWLockShared(&g_lock);
    std::stable_sort(out.begin(), out.end(), [](const ChargingSession& a, const ChargingSession& b) {
        return a.startTime > b.startTime; // newest first
    });
    return out;
}

std::vector<ChargingSession> GetAllChargingHistory() {
    AcquireSRWLockShared(&g_lock);
    std::vector<ChargingSession> out = g_chargingSessions;
    ReleaseSRWLockShared(&g_lock);
    std::stable_sort(out.begin(), out.end(), [](const ChargingSession& a, const ChargingSession& b) {
        return a.startTime > b.startTime; // newest first
    });
    return out;
}

void RecordChargingSession(const ChargingSession& s) {
    AcquireSRWLockExclusive(&g_lock);
    g_chargingSessions.push_back(s);
    if (g_chargingSessions.size() > kMaxChargingSessions) {
        g_chargingSessions.erase(g_chargingSessions.begin());
    }
    SaveChargingHistoryDisk();
    ReleaseSRWLockExclusive(&g_lock);
}

void ClearChargingHistory(const std::wstring& deviceKey) {
    AcquireSRWLockExclusive(&g_lock);
    g_chargingSessions.erase(std::remove_if(g_chargingSessions.begin(), g_chargingSessions.end(),
        [&](const ChargingSession& s) { return s.deviceKey == deviceKey; }), g_chargingSessions.end());
    SaveChargingHistoryDisk();
    ReleaseSRWLockExclusive(&g_lock);
}

std::vector<DeviceConsumptionEntry> CompareConsumption(
    const std::vector<DeviceStatus>& currentDevices,
    const battery_history::Store& store,
    int64_t now,
    DeviceKind filterKind) {

    std::vector<DeviceConsumptionEntry> list;
    constexpr int64_t kCompareWindow = 7 * 86400; // 7 days window

    for (const auto& d : currentDevices) {
        if (filterKind != DeviceKind::Other && d.kind != filterKind) continue;

        DeviceConsumptionEntry e;
        e.key = d.key;
        e.name = d.name;
        e.kind = d.kind;

        auto samples = store.For(d.key, now - kCompareWindow, now);
        auto drain = CalculateDrainRate(d.key, d.name, samples, d.percent, d.charging, now);

        if (drain.hasData) {
            e.hasData = true;
            e.percentPerHour = drain.percentPerHour;
            e.percentPerDay = drain.percentPerDay;
            e.estimatedRuntimeHours = 100.0 / drain.percentPerHour;
            e.rateText = drain.rateLabel;
            e.runtimeText = FormatDurationHours(e.estimatedRuntimeHours);
        } else {
            // Check if full charge runtime is available from historical sessions
            auto full = EstimateFullChargeRuntime(d.key, samples, now);
            if (full.hasEstimate && full.averageHours > 0) {
                e.hasData = true;
                e.estimatedRuntimeHours = full.averageHours;
                e.percentPerHour = 100.0 / full.averageHours;
                e.percentPerDay = e.percentPerHour * 24.0;
                int dayTenths = static_cast<int>(std::round(e.percentPerDay * 10.0));
                e.rateText = L"~" + std::to_wstring(dayTenths / 10) + L"." + std::to_wstring(dayTenths % 10) + L"%/day";
                e.runtimeText = FormatDurationHours(e.estimatedRuntimeHours);
            } else {
                e.rateText = T(L"Insufficient data");
                e.runtimeText = L"—";
            }
        }
        list.push_back(e);
    }

    // Sort by drain rate ascending (most efficient first)
    std::stable_sort(list.begin(), list.end(), [](const DeviceConsumptionEntry& a, const DeviceConsumptionEntry& b) {
        if (a.hasData != b.hasData) return a.hasData;
        return a.percentPerHour < b.percentPerHour;
    });

    return list;
}

std::wstring FormatDurationHours(double hours) {
    if (!std::isfinite(hours) || hours <= 0) return L"—";
    int total = static_cast<int>(std::round(hours));
    int days = total / 24;
    int hrs = total % 24;
    if (days > 0) {
        if (hrs == 0) return TF(L"~{days} days", { { L"days", std::to_wstring(days) } });
        return TF(L"~{days} d {hours} h", {
            { L"days", std::to_wstring(days) },
            { L"hours", std::to_wstring(hrs) }
        });
    }
    return TF(L"~{hours} h", { { L"hours", std::to_wstring(total) } });
}

} // namespace battery_intelligence
