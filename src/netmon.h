#pragma once
// Network monitor: live download/upload throughput (via IP Helper GetIfTable2, no admin required)
// and an optional one-click HTTP speed test (WinHTTP background worker).
#include <cstdint>
#include <cstdio>
#include <string>

namespace netmon {

// Bytes/sec between two cumulative octet samples, or -1.0 when counters wrapped/reset or elapsed == 0.
inline double RateBytesPerSec(std::uint64_t prevBytes, std::uint64_t nowBytes, std::uint64_t elapsedMs) {
    if (elapsedMs == 0 || nowBytes < prevBytes) return -1.0;
    return (double)(nowBytes - prevBytes) * 1000.0 / (double)elapsedMs;
}

// Compact strip formatting: "0K", "420K", "4.2M", "125M".
inline std::wstring FormatRateCompact(double bytesPerSec) {
    if (bytesPerSec < 0.0) return L"-";
    double kb = bytesPerSec / 1024.0;
    if (kb < 1024.0) {
        int k = (int)(kb + 0.5);
        return std::to_wstring(k) + L"K";
    }
    double mb = kb / 1024.0;
    wchar_t buf[24]{};
    if (mb < 10.0) swprintf(buf, 24, L"%.1fM", mb);
    else swprintf(buf, 24, L"%.0fM", mb);
    return buf;
}

// Detailed tooltip/panel formatting: "420 KB/s" or "4.2 MB/s".
inline std::wstring FormatRateFull(double bytesPerSec) {
    if (bytesPerSec < 0.0) return L"-";
    double kb = bytesPerSec / 1024.0;
    wchar_t buf[32]{};
    if (kb < 1024.0) swprintf(buf, 32, L"%.0f KB/s", kb);
    else swprintf(buf, 32, L"%.1f MB/s", kb / 1024.0);
    return buf;
}

// Speed test formatting: "94 Mbps" or "8.4 Mbps".
inline std::wstring FormatMbps(double mbps) {
    if (mbps < 0.0) return L"-";
    wchar_t buf[32]{};
    if (mbps < 10.0) swprintf(buf, 32, L"%.1f Mbps", mbps);
    else swprintf(buf, 32, L"%.0f Mbps", mbps);
    return buf;
}

struct Reading {
    bool available = false;
    double downBytesPerSec = -1.0;
    double upBytesPerSec = -1.0;
};

enum class TestState { Idle, Running, Done, Failed };

struct SpeedTestResult {
    TestState state = TestState::Idle;
    double downMbps = -1.0;
    double upMbps = -1.0;
    std::wstring error;
    std::uint64_t finishedTick = 0;
};

Reading Sample();
bool StartSpeedTest();
SpeedTestResult GetSpeedTest();
void Stop();

}  // namespace netmon
