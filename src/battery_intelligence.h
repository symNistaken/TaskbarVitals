#pragma once
// Battery Intelligence Engine for BatteryHub
// Computes battery drain rates, full-charge runtime estimates, battery health tracking,
// charging history tracking, and device battery consumption comparisons.
#include "devices.h"
#include "history_model.h"

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace battery_intelligence {

// Drain rate analytics result
struct DrainRateResult {
    bool hasData = false;            // True when enough reliable history exists
    double percentPerHour = 0.0;     // Consumed percentage per hour
    double percentPerDay = 0.0;      // Consumed percentage per 24 hours
    double hoursRemaining = 0.0;     // Estimated hours to 0% at current rate
    double observationHours = 0.0;   // Duration of the observation segment
    int percentDropped = 0;          // Total drop during observation
    size_t sampleCount = 0;          // Number of valid readings used
    bool isMeasured = false;         // True = observed active discharge; false = projected/estimated
    std::wstring summaryMessage;     // Human-readable summary
    std::wstring rateLabel;          // e.g. "1.5%/hr (~36%/day)"
    std::wstring statusReason;       // Reason if hasData is false ("Charging", "Insufficient data", etc.)
};

// Full-charge runtime estimate
struct FullChargeRuntimeResult {
    bool hasEstimate = false;
    double mostRecentHours = 0.0;    // Runtime from the most recent discharge session
    double averageHours = 0.0;       // Average runtime across historical sessions
    size_t historicalSessions = 0;   // Number of historical sessions analyzed
    std::wstring displayLabel;       // e.g. "Estimated full-charge runtime: ~12 days"
    std::wstring recentLabel;        // e.g. "Recent session: ~11 days"
    std::wstring averageLabel;       // e.g. "Historical average: ~13 days"
};

// Battery health record
struct BatteryHealthResult {
    bool supported = false;
    uint32_t designCapacityMwh = 0;
    uint32_t fullChargeCapacityMwh = 0;
    int healthPercent = -1;          // 0..100, or -1 if unavailable
    std::wstring displayText;        // "95%" or "Battery health unavailable"
    std::vector<std::pair<int64_t, int>> history; // timestamp -> health %
};

// Charging session record
struct ChargingSession {
    std::wstring deviceKey;
    std::wstring deviceName;
    int64_t startTime = 0;           // Unix seconds
    int64_t endTime = 0;             // Unix seconds
    int startPercent = -1;           // 0..100
    int endPercent = -1;             // 0..100
    int durationMinutes = 0;
    bool completed = false;          // True if fully charged (>= 98% or full flag)
};

// Device consumption comparison entry
struct DeviceConsumptionEntry {
    std::wstring key;
    std::wstring name;
    DeviceKind kind = DeviceKind::Other;
    double percentPerHour = 0.0;
    double percentPerDay = 0.0;
    double estimatedRuntimeHours = 0.0;
    bool hasData = false;
    std::wstring rateText;
    std::wstring runtimeText;
};

// Engine initialization & shutdown
void Initialize(bool demo);
void Shutdown();

// Observe a device poll: tracks active charging sessions, capacity health, etc.
void ObserveDevice(const DeviceStatus& status, int64_t now);

// Analytics calculation functions
DrainRateResult CalculateDrainRate(const std::wstring& deviceKey,
                                   const std::wstring& deviceName,
                                   const std::vector<battery_history::Sample>& samples,
                                   int currentPercent,
                                   bool isCharging,
                                   int64_t now);

FullChargeRuntimeResult EstimateFullChargeRuntime(const std::wstring& deviceKey,
                                                 const std::vector<battery_history::Sample>& samples,
                                                 int64_t now);

BatteryHealthResult GetBatteryHealth(const std::wstring& deviceKey,
                                    const DeviceStatus& currentStatus);

// Charging history retrieval and management
std::vector<ChargingSession> GetChargingHistory(const std::wstring& deviceKey);
std::vector<ChargingSession> GetAllChargingHistory();
void RecordChargingSession(const ChargingSession& session);
void ClearChargingHistory(const std::wstring& deviceKey);

// Device battery consumption comparisons
std::vector<DeviceConsumptionEntry> CompareConsumption(
    const std::vector<DeviceStatus>& currentDevices,
    const battery_history::Store& store,
    int64_t now,
    DeviceKind filterKind = DeviceKind::Other);

// Formatting helper: format hours as "~X d Y h" or "~X h"
std::wstring FormatDurationHours(double hours);

} // namespace battery_intelligence
