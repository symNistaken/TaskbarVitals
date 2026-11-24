// CPU percentage from two GetSystemTimes samples (kernel time includes idle time).
#include <cassert>
#include <cstdio>

#include "../src/sysmon.h"

using sysmon::CpuPercent;
using sysmon::CpuTimes;

int main() {
    // 1000 ticks passed on all cores, 750 of them idle: 25% busy.
    assert(CpuPercent({ 100, 400, 50 }, { 850, 1200, 250 }) == 25);
    // Fully idle and fully busy.
    assert(CpuPercent({ 0, 0, 0 }, { 500, 500, 0 }) == 0);
    assert(CpuPercent({ 0, 0, 0 }, { 0, 300, 700 }) == 100);
    // Rounds to the nearest percent: 1 of 3 busy is 33%, 2 of 3 is 67%.
    assert(CpuPercent({ 0, 0, 0 }, { 2, 3, 0 }) == 33);
    assert(CpuPercent({ 0, 0, 0 }, { 1, 3, 0 }) == 67);
    // No time passed, counters went back, or more idle than total: no value.
    assert(CpuPercent({ 5, 10, 10 }, { 5, 10, 10 }) == -1);
    assert(CpuPercent({ 5, 10, 10 }, { 4, 20, 20 }) == -1);
    assert(CpuPercent({ 0, 0, 0 }, { 50, 10, 10 }) == -1);
    // Temperature colour levels: default thresholds (80 / 90 °C), explicit parameters, and presets.
    assert(sysmon::HeatLevel(-1) == 0 && sysmon::HeatLevel(79) == 0);
    assert(sysmon::HeatLevel(80) == 1 && sysmon::HeatLevel(89) == 1);
    assert(sysmon::HeatLevel(90) == 2 && sysmon::HeatLevel(105) == 2);

    assert(sysmon::HeatLevel(-1, -5, 10) == 0);
    assert(sysmon::HeatLevel(69, 70, 85) == 0 && sysmon::HeatLevel(70, 70, 85) == 1);
    assert(sysmon::HeatLevel(84, 70, 85) == 1 && sysmon::HeatLevel(85, 70, 85) == 2);
    assert(sysmon::HeatLevel(75, 75) == 1 && sysmon::HeatLevel(90, 75) == 2);

    using sysmon::TempPreset;
    assert(sysmon::ClampTempPreset(0) == TempPreset::Standard);
    assert(sysmon::ClampTempPreset(1) == TempPreset::Laptop);
    assert(sysmon::ClampTempPreset(2) == TempPreset::Strict);
    assert(sysmon::ClampTempPreset(-1) == TempPreset::Standard);
    assert(sysmon::ClampTempPreset(3) == TempPreset::Standard);
    assert(sysmon::ClampTempPreset(999) == TempPreset::Standard);

    auto tStd = sysmon::ThresholdsFor(TempPreset::Standard);
    auto tLap = sysmon::ThresholdsFor(TempPreset::Laptop);
    auto tStr = sysmon::ThresholdsFor(TempPreset::Strict);
    auto tBad = sysmon::ThresholdsFor(static_cast<TempPreset>(42));
    assert(tStd.elevated == 80 && tStd.high == 90);
    assert(tLap.elevated == 85 && tLap.high == 95);
    assert(tStr.elevated == 70 && tStr.high == 85);
    assert(tBad.elevated == 80 && tBad.high == 90);

    assert(sysmon::HeatLevel(-1, TempPreset::Standard) == 0);
    assert(sysmon::HeatLevel(79, TempPreset::Standard) == 0 && sysmon::HeatLevel(80, TempPreset::Standard) == 1);
    assert(sysmon::HeatLevel(89, TempPreset::Standard) == 1 && sysmon::HeatLevel(90, TempPreset::Standard) == 2);

    assert(sysmon::HeatLevel(-1, TempPreset::Laptop) == 0);
    assert(sysmon::HeatLevel(84, TempPreset::Laptop) == 0 && sysmon::HeatLevel(85, TempPreset::Laptop) == 1);
    assert(sysmon::HeatLevel(94, TempPreset::Laptop) == 1 && sysmon::HeatLevel(95, TempPreset::Laptop) == 2);

    assert(sysmon::HeatLevel(-1, TempPreset::Strict) == 0);
    assert(sysmon::HeatLevel(69, TempPreset::Strict) == 0 && sysmon::HeatLevel(70, TempPreset::Strict) == 1);
    assert(sysmon::HeatLevel(84, TempPreset::Strict) == 1 && sysmon::HeatLevel(85, TempPreset::Strict) == 2);

    // High temperature alert resolution against colour presets.
    assert(sysmon::EffectiveTempAlertCelsius(0, TempPreset::Standard) == 0);
    assert(sysmon::EffectiveTempAlertCelsius(0, TempPreset::Laptop) == 0);
    assert(sysmon::EffectiveTempAlertCelsius(0, TempPreset::Strict) == 0);
    assert(sysmon::EffectiveTempAlertCelsius(sysmon::kTempAlertMatchPreset, TempPreset::Standard) == 90);
    assert(sysmon::EffectiveTempAlertCelsius(sysmon::kTempAlertMatchPreset, TempPreset::Laptop) == 95);
    assert(sysmon::EffectiveTempAlertCelsius(sysmon::kTempAlertMatchPreset, TempPreset::Strict) == 85);
    for (int t : sysmon::kTempAlertOptions) {
        assert(sysmon::EffectiveTempAlertCelsius(t, TempPreset::Standard) == t);
        assert(sysmon::EffectiveTempAlertCelsius(t, TempPreset::Laptop) == t);
        assert(sysmon::EffectiveTempAlertCelsius(t, TempPreset::Strict) == t);
    }
    const int invalidAlerts[] = { -5, -1, 2, 50, 70, 75, 100 };
    for (int invalid : invalidAlerts)
        assert(sysmon::EffectiveTempAlertCelsius(invalid, TempPreset::Laptop) == 0);
    // The package power meter among the Energy Meter instances (as seen on an i5-12450HX).
    assert(sysmon::IsPackageMeter(L"rapl_package0_pkg") && sysmon::IsPackageMeter(L"RAPL_Package0_PKG"));
    assert(!sysmon::IsPackageMeter(L"rapl_package0_pp0") && !sysmon::IsPackageMeter(L"rapl_package0_dram"));
    assert(!sysmon::IsPackageMeter(L"_total") && !sysmon::IsPackageMeter(L"pkg") && !sysmon::IsPackageMeter(nullptr));
    std::puts("System monitor tests passed (CPU share, rounding, idle and busy, bad samples, heat levels, power meter).");
    return 0;
}
