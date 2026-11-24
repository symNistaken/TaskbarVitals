#pragma once
#include <string>
#include <vector>

#include "devices.h"

// One device as exposed to external tools (YASB, Rainmeter, scripts, ...). Kept separate from
// DeviceStatus so the JSON/snapshot code does not need the tray state in main.cpp.
struct StatusDevice {
    std::wstring key;
    std::wstring name;
    std::wstring via;
    std::wstring detail;
    std::wstring levelText;
    DeviceKind kind = DeviceKind::Other;
    int  percent    = -1;   // live percentage from this poll, -1 when unknown right now
    int  lastPct    = -1;   // last known percentage, kept while the device sleeps
    int  millivolts = 0;
    bool charging   = false;
    bool asleep     = false;  // percent is the last known value, the device is sleeping
    bool connected  = true;
};

// System hardware telemetry snapshot exported in status.json when any vitals are monitored.
struct StatusVitals {
    bool included = false;
    int cpuPercent = -1;
    int cpuTempC = -1;
    int cpuWatts = -1;
    int ramPercent = -1;
    double ramUsedGb = 0.0;
    double ramTotalGb = 0.0;
    bool gpuAvailable = false;
    std::wstring gpuName;
    int gpuPercent = -1;
    int gpuTempC = -1;
    int gpuWatts = -1;
    int gpuClockMhz = -1;
    int vramClockMhz = -1;
    int vramPercent = -1;
    double vramUsedGb = 0.0;
    double vramTotalGb = 0.0;
    bool diskAvailable = false;
    std::wstring diskName;
    std::wstring diskDrive;
    int diskTempC = -1;
    int diskPercent = -1;
    double diskReadMBs = 0.0;
    double diskWriteMBs = 0.0;
    double diskFreeGb = 0.0;
    double diskTotalGb = 0.0;
    bool netAvailable = false;
    double netDownBytesPerSec = -1.0;
    double netUpBytesPerSec = -1.0;
    double netSpeedTestDownMbps = -1.0;
    double netSpeedTestUpMbps = -1.0;
    bool laptopBatteryPresent = false;
    int laptopBatteryPercent = -1;
    bool laptopBatteryCharging = false;
    bool laptopBatteryAcOnline = false;
    bool laptopBatteryHasWatts = false;
    double laptopBatteryWatts = 0.0;
    int laptopBatteryRemainingMinutes = -1;
};

// Stable machine-readable device kind: "headset", "mouse", "keyboard", "gamepad", "phone", "other".
const char* KindId(DeviceKind kind);

// A UTF-8 JSON string literal, including the surrounding quotes.
std::string JsonQuote(const std::wstring& text);

// The document shared by `--json` and status.json. `source` is "cli" or "snapshot"; `updated` is
// passed in so tests can be deterministic (see IsoTimestampNow).
std::string BuildStatusJson(const std::vector<StatusDevice>& devices, const std::string& source,
                            int lowThreshold, const std::string& updated,
                            const StatusVitals* vitals = nullptr);

// Current UTC time as "2026-11-17T12:34:56Z".
std::string IsoTimestampNow();

// %LOCALAPPDATA%\WirelessBattery\status.json (falls back to %TEMP%).
std::wstring StatusSnapshotPath();

// Writes the snapshot atomically (temp file, then rename). Failures are silent: the app must keep
// working when the folder is read-only.
bool WriteStatusSnapshot(const std::vector<StatusDevice>& devices, const std::string& source, int lowThreshold,
                         const StatusVitals* vitals = nullptr);
