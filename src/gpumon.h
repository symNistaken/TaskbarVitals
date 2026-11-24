#pragma once
// GPU readings for the strip: use, dedicated memory (VRAM) and temperature of one graphics adapter.
// The same sources as Task Manager, without a driver or administrator rights:
//  * use and VRAM: performance counters "GPU Engine" and "GPU Adapter Memory" (pdh.dll)
//  * total VRAM and the adapter list: DXGI (dxgi.dll); no Direct3D device is created
//  * temperature: D3DKMTQueryAdapterInfo(ADAPTERPERFDATA) (gdi32.dll, WDDM 2.4+); not every driver reports it
// All three DLLs are loaded on first use, so nothing is loaded while the GPU readings are off.
#include <cstdint>
#include <string>
#include <vector>

namespace gpumon {

// One "GPU Engine" counter instance, e.g.
// pid_1234_luid_0x00000000_0x0000C3A1_phys_0_eng_3_engtype_3D -> adapter 0x0000C3A1, engine type "3D".
struct EngineSample {
    std::uint64_t luid = 0;
    std::wstring type;
    double percent = 0;
};

// Reads the adapter LUID ("luid_0xHIGH_0xLOW") from a counter instance name; for engine instances also the
// engine type ("engtype_..."). False when the name has no LUID.
bool ParseInstance(const std::wstring& name, std::uint64_t& luid, std::wstring* engineType = nullptr);

// Use of one adapter as Task Manager shows it: the busiest engine type, with the engines (and processes) of
// one type added up. 0..100, or -1 when the adapter has no samples.
int AdapterUse(const std::vector<EngineSample>& samples, std::uint64_t luid);

// Rounds GPU wattage to nearest whole watt; returns -1 for NaN, negative, or implausible (> 1000 W) values.
inline int RoundGpuWatts(double watts) {
    if (!(watts >= 0.0 && watts <= 1000.0)) return -1;
    return static_cast<int>(watts + 0.5);
}

// Computes average Intel GPU power in watts from two monotonic microjoule/microsecond IGCL energy snapshots.
// Returns -1 on missing baseline, non-increasing timestamps, counter reset, or outside 50 ms..60 s window.
inline int IgclWattsFromEnergyDelta(std::uint64_t prevEnergyUj, std::uint64_t prevTimestampUs,
                                    std::uint64_t curEnergyUj, std::uint64_t curTimestampUs) {
    if (prevTimestampUs == 0 || curTimestampUs <= prevTimestampUs || curEnergyUj < prevEnergyUj) return -1;
    const std::uint64_t dtUs = curTimestampUs - prevTimestampUs;
    if (dtUs < 50000ULL || dtUs > 60000000ULL) return -1;
    const std::uint64_t watts = (curEnergyUj - prevEnergyUj + dtUs / 2ULL) / dtUs;
    return watts <= 1000ULL ? static_cast<int>(watts) : -1;
}

struct Reading {
    bool available = false;  // an adapter was found
    std::wstring name;       // "NVIDIA GeForce RTX 4050 Laptop GPU"
    int use = -1;            // percent; -1 until two samples exist
    int vram = -1;           // percent of dedicated memory in use
    double vramUsedGb = 0, vramTotalGb = 0;
    int tempC = -1;          // -1 when the driver does not report it
    int watts = -1;          // board power in W (NVIDIA nvml / AMD ADL2); -1 when not asked for or not reported
    int powerPct = -1;       // power as a share of the power limit (any WDDM 2.4+ driver); -1 when not reported
    int gpuClockMhz = -1;    // core graphics clock in MHz (NVIDIA nvml / AMD ADL2); -1 when not available
    int vramClockMhz = -1;   // memory / VRAM clock in MHz (NVIDIA nvml / AMD ADL2 / WDDM); -1 when not available
};

// Formats clock rate concisely ("1950/7000 MHz" or "1950 MHz"). Empty when both are < 0.
inline std::wstring FormatClocksMhz(int gpuMhz, int vramMhz) {
    if (gpuMhz >= 0 && vramMhz >= 0) return std::to_wstring(gpuMhz) + L"/" + std::to_wstring(vramMhz) + L" MHz";
    if (gpuMhz >= 0) return std::to_wstring(gpuMhz) + L" MHz";
    if (vramMhz >= 0) return std::to_wstring(vramMhz) + L" MHz";
    return L"";
}

// Samples the adapter with the most dedicated memory (the discrete GPU on a laptop with two).
// Runs on the UI thread every few seconds while a GPU reading is shown.
Reading Sample(bool wantTemperature, bool wantPower = false, bool wantClocks = false);

// Frees the counters and the adapter handle (when the readings are switched off, and at exit).
void Stop();

}  // namespace gpumon
