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

struct Reading {
    bool available = false;  // an adapter was found
    std::wstring name;       // "NVIDIA GeForce RTX 4050 Laptop GPU"
    int use = -1;            // percent; -1 until two samples exist
    int vram = -1;           // percent of dedicated memory in use
    double vramUsedGb = 0, vramTotalGb = 0;
    int tempC = -1;          // -1 when the driver does not report it
};

// Samples the adapter with the most dedicated memory (the discrete GPU on a laptop with two).
// Runs on the UI thread every few seconds while a GPU reading is shown.
Reading Sample(bool wantTemperature);

// Frees the counters and the adapter handle (when the readings are switched off, and at exit).
void Stop();

}  // namespace gpumon
