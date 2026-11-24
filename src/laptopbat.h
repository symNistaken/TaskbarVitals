#pragma once
// Built-in laptop/system battery monitor (GetSystemPowerStatus + powrprof.dll SystemBatteryState).
// Reads charge percentage, AC/charging status, live charge/discharge rate in Watts, and remaining time.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace laptopbat {

// Converts Windows SYSTEM_BATTERY_STATE.Rate (signed milliwatts; 0x80000000 = unknown) to Watts.
inline double ParsePowerRateMw(std::int32_t rawRate, bool& validOut) {
    if (rawRate == (std::int32_t)0x80000000) {
        validOut = false;
        return 0.0;
    }
    long long mag = (long long)rawRate;
    if (mag < 0) mag = -mag;
    if (mag > 500000) {  // >500 W is bogus firmware telemetry
        validOut = false;
        return 0.0;
    }
    validOut = true;
    return (double)rawRate / 1000.0;
}

// Compact strip formatting with sign ("+45W", "-18W", "0W").
inline std::wstring FormatWattsCompact(double watts) {
    int rounded = (int)std::llround(watts);
    if (rounded == 0) return L"0W";
    return (rounded > 0 ? L"+" : L"") + std::to_wstring(rounded) + L"W";
}

// Detailed card/tooltip formatting with sign ("+45.2 W", "-18.4 W", "0.0 W").
inline std::wstring FormatWattsDetailed(double watts) {
    if (std::fabs(watts) < 0.05) return L"0.0 W";
    wchar_t buf[32]{};
    swprintf(buf, 32, L"%+.1f W", watts);
    return buf;
}

// Formats remaining runtime in minutes ("2 h 15 min", "3 h", "42 min"), or empty when <= 0.
inline std::wstring FormatRemaining(int minutes) {
    if (minutes <= 0) return L"";
    int h = minutes / 60, m = minutes % 60;
    if (h > 0 && m > 0) return std::to_wstring(h) + L" h " + std::to_wstring(m) + L" min";
    if (h > 0) return std::to_wstring(h) + L" h";
    return std::to_wstring(m) + L" min";
}

struct Reading {
    bool present = false;
    int percent = -1;
    bool acOnline = false;
    bool charging = false;
    bool hasWatts = false;
    double watts = 0.0;          // positive = charging into battery, negative = discharging
    int remainingMinutes = -1;
    std::uint32_t remainingMwh = 0;
    std::uint32_t maxMwh = 0;
};

Reading Sample();
void Stop();

}  // namespace laptopbat
