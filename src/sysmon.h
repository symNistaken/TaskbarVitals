#pragma once
// System readings shown next to the batteries: CPU and RAM use, from plain Win32 calls (no driver, no
// administrator rights). Sample() is cheap (two kernel calls) and runs on the UI thread every few seconds.
#include <cstdint>

namespace sysmon {

// Cumulative CPU times from GetSystemTimes, in 100 ns units. `kernel` includes idle time.
struct CpuTimes {
    std::uint64_t idle = 0, kernel = 0, user = 0;
};

// Busy share of all cores between two samples, 0..100, or -1 when no time passed (or the counters went back).
inline int CpuPercent(const CpuTimes& before, const CpuTimes& after) {
    if (after.idle < before.idle || after.kernel < before.kernel || after.user < before.user) return -1;
    std::uint64_t idle = after.idle - before.idle;
    std::uint64_t total = (after.kernel - before.kernel) + (after.user - before.user);
    if (total == 0 || idle > total) return -1;
    return (int)(((total - idle) * 100 + total / 2) / total);
}

// Colour level of a CPU or GPU temperature on the strip: 0 normal, 1 elevated (from 80 °C), 2 high (from 90 °C).
inline int HeatLevel(int celsius) { return celsius >= 90 ? 2 : celsius >= 80 ? 1 : 0; }

struct Reading {
    int cpu = -1;               // percent, -1 until two samples exist
    int ram = -1;               // percent of physical memory in use
    double ramUsedGb = 0, ramTotalGb = 0;
};

// Reads the counters now. The CPU value covers the time since the previous call.
Reading Sample();

}  // namespace sysmon
