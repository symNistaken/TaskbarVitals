#include "laptopbat.h"

#include <windows.h>

namespace laptopbat {
namespace {

using CallNtPowerInfoFn = LONG(WINAPI*)(ULONG, PVOID, ULONG, PVOID, ULONG);
HMODULE g_powrprof = nullptr;
bool g_powrprofTried = false;
CallNtPowerInfoFn g_callNtPowerInfo = nullptr;

void EnsurePowrProf() {
    if (g_powrprofTried) return;
    g_powrprofTried = true;
    g_powrprof = LoadLibraryExW(L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (g_powrprof) {
        g_callNtPowerInfo = reinterpret_cast<CallNtPowerInfoFn>(
            reinterpret_cast<void*>(GetProcAddress(g_powrprof, "CallNtPowerInformation")));
    }
}

}  // namespace

Reading Sample() {
    Reading out;
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return out;
    if ((sps.BatteryFlag & 128) != 0 || sps.BatteryLifePercent > 100) return out;

    out.present = true;
    out.percent = (int)sps.BatteryLifePercent;
    out.acOnline = (sps.ACLineStatus == 1);
    out.charging = (sps.BatteryFlag & 8) != 0;

    EnsurePowrProf();
    if (g_callNtPowerInfo) {
        SYSTEM_BATTERY_STATE sbs{};
        constexpr ULONG kSystemBatteryState = 5;
        if (g_callNtPowerInfo(kSystemBatteryState, nullptr, 0, &sbs, sizeof(sbs)) == 0) {
            if (!sbs.BatteryPresent) {
                out.present = false;
                return out;
            }
            out.charging = sbs.Charging != FALSE;
            out.acOnline = sbs.AcOnLine != FALSE;
            out.remainingMwh = (std::uint32_t)sbs.RemainingCapacity;
            out.maxMwh = (std::uint32_t)sbs.MaxCapacity;

            bool rateOk = false;
            double w = ParsePowerRateMw((std::int32_t)sbs.Rate, rateOk);
            if (rateOk) {
                if (!sbs.Charging && sbs.Discharging && w > 0.0) w = -w;
                out.hasWatts = true;
                out.watts = w;
            }
            if (!sbs.Charging && sbs.EstimatedTime > 0 && sbs.EstimatedTime < 100u * 3600u) {
                out.remainingMinutes = (int)(sbs.EstimatedTime / 60u);
            }
        }
    }

    if (out.remainingMinutes < 0 && !out.charging &&
        sps.BatteryLifeTime != (DWORD)-1 && sps.BatteryLifeTime > 0 && sps.BatteryLifeTime < 100u * 3600u) {
        out.remainingMinutes = (int)(sps.BatteryLifeTime / 60u);
    }
    return out;
}

void Stop() {
    if (g_powrprof) {
        FreeLibrary(g_powrprof);
        g_powrprof = nullptr;
        g_callNtPowerInfo = nullptr;
    }
    g_powrprofTried = false;
}

}  // namespace laptopbat
