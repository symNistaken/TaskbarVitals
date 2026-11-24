#pragma once
// Pure, Windows-independent JSON snapshot builder for the read-only local named pipe API.
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "devices.h"
#include "json_output.h"
#include "platform_compat.h"

namespace live_api_json {

struct SystemMetricsSnapshot {
    bool showCpu = false;
    int cpuPercent = -1;
    bool showCpuTemp = false;
    int cpuTempC = -1;
    bool showCpuWatts = false;
    int cpuWatts = -1;

    bool showRam = false;
    int ramPercent = -1;
    double ramUsedGb = 0.0;
    double ramTotalGb = 0.0;

    bool gpuAvailable = false;
    std::wstring gpuName;
    bool showGpu = false;
    int gpuPercent = -1;
    bool showGpuTemp = false;
    int gpuTempC = -1;
    bool showGpuWatts = false;
    int gpuWatts = -1;

    bool showVram = false;
    int vramPercent = -1;
    double vramUsedGb = 0.0;
    double vramTotalGb = 0.0;

    bool showLaptopBattery = false;
    bool laptopBatteryPresent = false;
    int laptopBatteryPercent = -1;
    bool laptopBatteryCharging = false;
    bool laptopBatteryFull = false;
    int laptopBatteryRemainingSec = -1;

    bool showNetwork = false;
    bool netValid = false;
    std::uint64_t netRxBps = 0;
    std::uint64_t netTxBps = 0;
};

inline std::string OptInt(bool enabled, int value) {
    return (enabled && value >= 0) ? std::to_string(value) : std::string("null");
}

inline std::string OptTenths(bool enabled, double value) {
    if (!enabled || !(value >= 0.0 && value <= 1000000.0)) return "null";
    const auto tenths = static_cast<std::uint64_t>(std::llround(value * 10.0));
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

inline std::string OptBool(bool enabled, bool value) {
    return enabled ? (value ? "true" : "false") : "null";
}

inline std::string BuildLiveSnapshotJson(const std::vector<DeviceStatus>& devices,
                                         const SystemMetricsSnapshot& sys) {
    std::string devJson = DevicesToJson(devices);
    if (!devJson.empty() && devJson.back() == '\n') devJson.pop_back();
    if (devJson.empty()) devJson = "[]";

    const bool hasRam = sys.showRam && sys.ramPercent >= 0;
    const bool hasGpu = sys.gpuAvailable;
    const bool hasVram = hasGpu && sys.showVram && sys.vramPercent >= 0;
    const bool hasLaptopBat = sys.showLaptopBattery && sys.laptopBatteryPresent && sys.laptopBatteryPercent >= 0;
    const bool hasNet = sys.showNetwork && sys.netValid;

    std::string out = "{\"version\":1,\"devices\":" + devJson + ",\"system\":{";
    out += "\"cpuPercent\":" + OptInt(sys.showCpu, sys.cpuPercent);
    out += ",\"cpuTempC\":" + OptInt(sys.showCpuTemp, sys.cpuTempC);
    out += ",\"cpuWatts\":" + OptInt(sys.showCpuWatts, sys.cpuWatts);
    out += ",\"ramPercent\":" + OptInt(hasRam, sys.ramPercent);
    out += ",\"ramUsedGb\":" + OptTenths(hasRam, sys.ramUsedGb);
    out += ",\"ramTotalGb\":" + OptTenths(hasRam, sys.ramTotalGb);
    out += ",\"gpuName\":" + (hasGpu && !sys.gpuName.empty()
                                  ? std::string("\"") + JsonEscape(WideToUtf8(sys.gpuName)) + "\""
                                  : std::string("null"));
    out += ",\"gpuPercent\":" + OptInt(hasGpu && sys.showGpu, sys.gpuPercent);
    out += ",\"gpuTempC\":" + OptInt(hasGpu && sys.showGpuTemp, sys.gpuTempC);
    out += ",\"gpuWatts\":" + OptInt(hasGpu && sys.showGpuWatts, sys.gpuWatts);
    out += ",\"vramPercent\":" + OptInt(hasVram, sys.vramPercent);
    out += ",\"vramUsedGb\":" + OptTenths(hasVram, sys.vramUsedGb);
    out += ",\"vramTotalGb\":" + OptTenths(hasVram, sys.vramTotalGb);
    out += ",\"laptopBatteryPercent\":" + OptInt(hasLaptopBat, sys.laptopBatteryPercent);
    out += ",\"laptopBatteryCharging\":" + OptBool(hasLaptopBat, sys.laptopBatteryCharging);
    out += ",\"laptopBatteryFull\":" + OptBool(hasLaptopBat, sys.laptopBatteryFull);
    out += ",\"laptopBatteryRemainingSec\":" + OptInt(hasLaptopBat, sys.laptopBatteryRemainingSec);
    out += ",\"netRxBps\":" + (hasNet ? std::to_string(sys.netRxBps) : std::string("null"));
    out += ",\"netTxBps\":" + (hasNet ? std::to_string(sys.netTxBps) : std::string("null"));
    out += "}}\n";
    return out;
}

}  // namespace live_api_json
