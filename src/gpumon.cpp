#include "gpumon.h"

#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <string>

namespace gpumon {

bool ParseInstance(const std::wstring& name, std::uint64_t& luid, std::wstring* engineType) {
    size_t at = name.find(L"luid_0x");
    if (at == std::wstring::npos) return false;
    unsigned long high = 0, low = 0;
    if (swscanf(name.c_str() + at, L"luid_0x%lx_0x%lx", &high, &low) != 2) return false;
    luid = ((std::uint64_t)high << 32) | low;
    if (engineType) {
        size_t t = name.find(L"engtype_");
        *engineType = t == std::wstring::npos ? L"" : name.substr(t + 8);
    }
    return true;
}

int AdapterUse(const std::vector<EngineSample>& samples, std::uint64_t luid) {
    std::map<std::wstring, double> byType;
    for (const auto& s : samples)
        if (s.luid == luid) byType[s.type] += s.percent;
    if (byType.empty()) return -1;
    double busiest = 0;
    for (const auto& [type, pct] : byType) busiest = std::max(busiest, pct);
    return (int)(std::min(busiest, 100.0) + 0.5);
}

namespace {

// A function from a DLL loaded at run time, as its own type.
template <class F>
F Proc(HMODULE m, const char* name) {
    return m ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
}

// ---- performance counters (pdh.dll) ----
using OpenQueryFn = PDH_STATUS(WINAPI*)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
using AddCounterFn = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
using CollectFn = PDH_STATUS(WINAPI*)(PDH_HQUERY);
using ArrayFn = PDH_STATUS(WINAPI*)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
using CloseQueryFn = PDH_STATUS(WINAPI*)(PDH_HQUERY);

HMODULE g_pdh;
OpenQueryFn g_open;
AddCounterFn g_add;
CollectFn g_collect;
ArrayFn g_array;
CloseQueryFn g_close;
PDH_HQUERY g_query;
PDH_HCOUNTER g_engines, g_memory;
bool g_collectedOnce;

bool StartCounters() {
    if (g_query) return true;
    if (!g_pdh) g_pdh = LoadLibraryExW(L"pdh.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_pdh) return false;
    g_open = Proc<OpenQueryFn>(g_pdh, "PdhOpenQueryW");
    g_add = Proc<AddCounterFn>(g_pdh, "PdhAddEnglishCounterW");
    g_collect = Proc<CollectFn>(g_pdh, "PdhCollectQueryData");
    g_array = Proc<ArrayFn>(g_pdh, "PdhGetFormattedCounterArrayW");
    g_close = Proc<CloseQueryFn>(g_pdh, "PdhCloseQuery");
    if (!g_open || !g_add || !g_collect || !g_array || !g_close || g_open(nullptr, 0, &g_query) != ERROR_SUCCESS) {
        g_query = nullptr;
        return false;
    }
    g_add(g_query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_engines);
    g_add(g_query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &g_memory);
    g_collectedOnce = false;
    return true;
}

// Every instance of a wildcard counter, as (instance name, value).
std::vector<std::pair<std::wstring, double>> Values(PDH_HCOUNTER counter) {
    std::vector<std::pair<std::wstring, double>> out;
    if (!counter) return out;
    DWORD bytes = 0, count = 0;
    if (g_array(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr) != (PDH_STATUS)PDH_MORE_DATA || !bytes) return out;
    std::vector<BYTE> buf(bytes);
    auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
    if (g_array(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items) != ERROR_SUCCESS) return out;
    for (DWORD i = 0; i < count; i++)
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
            out.push_back({ items[i].szName, items[i].FmtValue.doubleValue });
    return out;
}

// ---- adapter choice (dxgi.dll) ----
struct Adapter {
    bool found = false;
    std::uint64_t luid = 0;
    LUID raw{};
    UINT vendorId = 0;
    UINT deviceId = 0;
    std::wstring name;
    std::uint64_t vramBytes = 0;
};
Adapter g_adapter;
bool g_adapterLooked;

Adapter FindAdapter() {
    Adapter best;
    HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dxgi) return best;
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    auto create = Proc<CreateFactoryFn>(dxgi, "CreateDXGIFactory1");
    IDXGIFactory1* factory = nullptr;
    if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), (void**)&factory)) && factory) {
        IDXGIAdapter1* a = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > best.vramBytes) {
                best.found = true;
                best.raw = d.AdapterLuid;
                best.luid = ((std::uint64_t)(DWORD)d.AdapterLuid.HighPart << 32) | d.AdapterLuid.LowPart;
                best.vendorId = d.VendorId;
                best.deviceId = d.DeviceId;
                best.name = d.Description;
                best.vramBytes = d.DedicatedVideoMemory;
            }
            a->Release();
        }
        factory->Release();
    }
    FreeLibrary(dxgi);  // the description is copied; nothing from dxgi.dll is kept
    return best;
}

// ---- temperature (gdi32.dll, D3DKMT; the header is not part of MinGW, so the types are declared here) ----
struct OpenFromLuid { LUID AdapterLuid; UINT hAdapter; };
struct CloseAdapterArgs { UINT hAdapter; };
struct QueryInfoArgs { UINT hAdapter; UINT Type; void* pPrivateDriverData; UINT PrivateDriverDataSize; };
struct AdapterPerfData {
    UINT PhysicalAdapterIndex;
    ULONGLONG MemoryFrequency, MaxMemoryFrequency, MaxMemoryFrequencyOC, MemoryBandwidth, PCIEBandwidth;
    ULONG FanRPM;
    ULONG Power;        // tenths of a percent of the power limit
    ULONG Temperature;  // tenths of a degree Celsius
    UCHAR PowerStateOverride;
};
constexpr UINT kAdapterPerfData = 62;  // KMTQAITYPE_ADAPTERPERFDATA
using OpenAdapterFn = LONG(WINAPI*)(OpenFromLuid*);
using QueryInfoFn = LONG(WINAPI*)(const QueryInfoArgs*);
using CloseAdapterFn = LONG(WINAPI*)(const CloseAdapterArgs*);
UINT g_kmtAdapter;
QueryInfoFn g_queryInfo;
CloseAdapterFn g_closeAdapter;

// Temperature (-1 if unknown), power share (-1 if unknown), and memory clock Hz (0 if unknown) from ADAPTERPERFDATA.
void PerfData(int& tempC, int& powerPct, std::uint64_t& memFreqHz) {
    tempC = powerPct = -1;
    memFreqHz = 0;
    if (!g_kmtAdapter) {
        HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
        auto open = Proc<OpenAdapterFn>(gdi, "D3DKMTOpenAdapterFromLuid");
        g_queryInfo = Proc<QueryInfoFn>(gdi, "D3DKMTQueryAdapterInfo");
        g_closeAdapter = Proc<CloseAdapterFn>(gdi, "D3DKMTCloseAdapter");
        OpenFromLuid args{ g_adapter.raw, 0 };
        if (!open || !g_queryInfo || !g_closeAdapter || open(&args) != 0) return;
        g_kmtAdapter = args.hAdapter;
    }
    AdapterPerfData perf{};
    QueryInfoArgs q{ g_kmtAdapter, kAdapterPerfData, &perf, sizeof(perf) };
    if (g_queryInfo(&q) != 0) return;
    if (perf.Temperature != 0 && perf.Temperature <= 1500) tempC = (int)((perf.Temperature + 5) / 10);
    if (perf.Power != 0 && perf.Power <= 3000) powerPct = (int)((perf.Power + 5) / 10);
    if (perf.MemoryFrequency > 0) memFreqHz = perf.MemoryFrequency;
}

// ---- NVIDIA management library (nvml.dll; ships with the display driver) ----
using NvmlInitFn = int(*)();
using NvmlCountFn = int(*)(unsigned*);
using NvmlHandleFn = int(*)(unsigned, void**);
using NvmlNameFn = int(*)(void*, char*, unsigned);
using NvmlPowerFn = int(*)(void*, unsigned*);
using NvmlClockFn = int(*)(void*, int, unsigned*);
HMODULE g_nvml;
NvmlInitFn g_nvmlShutdown;
NvmlPowerFn g_nvmlPower;
NvmlClockFn g_nvmlClock;
void* g_nvmlDevice;
bool g_nvmlTried;

// The NVML device whose name is the DXGI adapter's (or the only one).
void StartNvml() {
    if (g_nvmlTried) return;
    g_nvmlTried = true;
    g_nvml = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto init = Proc<NvmlInitFn>(g_nvml, "nvmlInit_v2");
    auto count = Proc<NvmlCountFn>(g_nvml, "nvmlDeviceGetCount_v2");
    auto handle = Proc<NvmlHandleFn>(g_nvml, "nvmlDeviceGetHandleByIndex_v2");
    auto name = Proc<NvmlNameFn>(g_nvml, "nvmlDeviceGetName");
    g_nvmlPower = Proc<NvmlPowerFn>(g_nvml, "nvmlDeviceGetPowerUsage");
    g_nvmlClock = Proc<NvmlClockFn>(g_nvml, "nvmlDeviceGetClockInfo");
    g_nvmlShutdown = Proc<NvmlInitFn>(g_nvml, "nvmlShutdown");
    unsigned n = 0;
    if (!init || !count || !handle || !name || init() != 0 || count(&n) != 0) return;
    std::string want;
    for (wchar_t c : g_adapter.name) want += (char)(c < 128 ? c : '?');
    for (unsigned i = 0; i < n; i++) {
        void* d = nullptr;
        char got[96] = {};
        if (handle(i, &d) != 0 || name(d, got, sizeof(got)) != 0) continue;
        if (n == 1 || want.find(got) != std::string::npos || std::string(got).find(want) != std::string::npos) {
            g_nvmlDevice = d;
            return;
        }
    }
}

int NvmlWatts() {
    StartNvml();
    unsigned mw = 0;
    if (!g_nvmlDevice || !g_nvmlPower || g_nvmlPower(g_nvmlDevice, &mw) != 0) return -1;
    return (int)((mw + 500) / 1000);
}

void NvmlClocks(int& gpuMhz, int& vramMhz) {
    StartNvml();
    if (!g_nvmlDevice || !g_nvmlClock) return;
    unsigned gfx = 0, mem = 0;
    if (g_nvmlClock(g_nvmlDevice, 0 /* NVML_CLOCK_GRAPHICS */, &gfx) == 0 && gfx > 0) gpuMhz = (int)gfx;
    if (g_nvmlClock(g_nvmlDevice, 2 /* NVML_CLOCK_MEM */, &mem) == 0 && mem > 0) vramMhz = (int)mem;
}

// ---- AMD Display Library (atiadlxx.dll, ADL2 PMLog; ships with AMD Radeon drivers) ----
using AdlMallocFn = void*(CALLBACK*)(int);
using Adl2CreateFn = int(*)(AdlMallocFn, int, void**);
using Adl2DestroyFn = int(*)(void*);
using Adl2CountFn = int(*)(void*, int*);
using Adl2ActiveFn = int(*)(void*, int, int*);
using Adl2PmLogFn = int(*)(void*, int, void*);

struct AdlSensorData {
    int supported;
    int value;
};
struct AdlPmLogOutput {
    int size;
    AdlSensorData sensors[256];
};
constexpr int kAdlClkGfxClk = 1;
constexpr int kAdlClkMemClk = 2;
constexpr int kAdlSocPower = 17;
constexpr int kAdlAsicPower = 23;
constexpr int kAdlGfxPower = 30;
constexpr int kAdlBoardPower = 73;

HMODULE g_adl;
void* g_adlCtx;
Adl2DestroyFn g_adlDestroy;
Adl2PmLogFn g_adlPmLog;
int g_adlAdapterIndex = -1;
bool g_adlTried;

void* CALLBACK AdlAlloc(int bytes) { return std::malloc((size_t)std::max(1, bytes)); }

bool IsAmdAdapter() {
    std::wstring low = g_adapter.name;
    for (wchar_t& c : low) c = (wchar_t)towlower(c);
    return low.find(L"amd") != std::wstring::npos || low.find(L"radeon") != std::wstring::npos;
}

void StartAdl() {
    if (g_adlTried) return;
    g_adlTried = true;
    if (!IsAmdAdapter()) return;
    g_adl = LoadLibraryExW(L"atiadlxx.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_adl) return;
    auto create = Proc<Adl2CreateFn>(g_adl, "ADL2_Main_Control_Create");
    g_adlDestroy = Proc<Adl2DestroyFn>(g_adl, "ADL2_Main_Control_Destroy");
    auto count = Proc<Adl2CountFn>(g_adl, "ADL2_Adapter_NumberOfAdapters_Get");
    auto active = Proc<Adl2ActiveFn>(g_adl, "ADL2_Adapter_Active_Get");
    g_adlPmLog = Proc<Adl2PmLogFn>(g_adl, "ADL2_New_QueryPMLogData_Get");
    if (!create || !g_adlDestroy || !count || !g_adlPmLog) return;
    if (create(AdlAlloc, 1, &g_adlCtx) != 0 || !g_adlCtx) { g_adlCtx = nullptr; return; }
    int n = 0;
    if (count(g_adlCtx, &n) != 0 || n <= 0) return;
    for (int i = 0; i < n; ++i) {
        int isActive = 1;
        if (active && active(g_adlCtx, i, &isActive) == 0 && !isActive) continue;
        AdlPmLogOutput out{};
        out.size = sizeof(out);
        if (g_adlPmLog(g_adlCtx, i, &out) == 0 &&
            (out.sensors[kAdlClkGfxClk].supported || out.sensors[kAdlAsicPower].supported ||
             out.sensors[kAdlBoardPower].supported || out.sensors[kAdlGfxPower].supported)) {
            g_adlAdapterIndex = i;
            return;
        }
    }
}

void QueryAdl(bool wantPower, bool wantClocks, int& watts, int& gpuMhz, int& vramMhz) {
    StartAdl();
    if (!g_adlCtx || !g_adlPmLog || g_adlAdapterIndex < 0) return;
    AdlPmLogOutput out{};
    out.size = sizeof(out);
    if (g_adlPmLog(g_adlCtx, g_adlAdapterIndex, &out) != 0) return;
    if (wantPower && watts < 0) {
        for (int idx : { kAdlBoardPower, kAdlAsicPower, kAdlGfxPower, kAdlSocPower }) {
            if (out.sensors[idx].supported && out.sensors[idx].value > 0 && out.sensors[idx].value < 2000) {
                watts = out.sensors[idx].value;
                break;
            }
        }
    }
    if (wantClocks) {
        if (gpuMhz < 0 && out.sensors[kAdlClkGfxClk].supported && out.sensors[kAdlClkGfxClk].value > 0)
            gpuMhz = out.sensors[kAdlClkGfxClk].value;
        if (vramMhz < 0 && out.sensors[kAdlClkMemClk].supported && out.sensors[kAdlClkMemClk].value > 0)
            vramMhz = out.sensors[kAdlClkMemClk].value;
    }
}

// ---- AMD ADLX (amdadlx64.dll) ----
namespace adlx_abi {
struct System;
struct GpuList;
struct Gpu;
struct PerfServices;
struct GpuMetricsSupport;
struct GpuMetrics;

struct SystemVtbl {
    int(__stdcall* GetHybridGraphicsType)(System*, int*);
    int(__stdcall* GetGPUs)(System*, GpuList**);
    int(__stdcall* QueryInterface)(System*, const wchar_t*, void**);
    int(__stdcall* GetDisplaysServices)(System*, void**);
    int(__stdcall* GetDesktopsServices)(System*, void**);
    int(__stdcall* GetGPUsChangedHandling)(System*, void**);
    int(__stdcall* EnableLog)(System*, int, int, void*, const wchar_t*);
    int(__stdcall* Get3DSettingsServices)(System*, void**);
    int(__stdcall* GetGPUTuningServices)(System*, void**);
    int(__stdcall* GetPerformanceMonitoringServices)(System*, PerfServices**);
};
struct System { const SystemVtbl* pVtbl; };

struct GpuListVtbl {
    long(__stdcall* Acquire)(GpuList*);
    long(__stdcall* Release)(GpuList*);
    int(__stdcall* QueryInterface)(GpuList*, const wchar_t*, void**);
    std::uint32_t(__stdcall* Size)(GpuList*);
    bool(__stdcall* Empty)(GpuList*);
    std::uint32_t(__stdcall* Begin)(GpuList*);
    std::uint32_t(__stdcall* End)(GpuList*);
    int(__stdcall* At)(GpuList*, std::uint32_t, void**);
    int(__stdcall* Clear)(GpuList*);
    int(__stdcall* Remove_Back)(GpuList*);
    int(__stdcall* Add_Back)(GpuList*, void*);
    int(__stdcall* At_GPUList)(GpuList*, std::uint32_t, Gpu**);
};
struct GpuList { const GpuListVtbl* pVtbl; };

struct GpuVtbl {
    long(__stdcall* Acquire)(Gpu*);
    long(__stdcall* Release)(Gpu*);
    int(__stdcall* QueryInterface)(Gpu*, const wchar_t*, void**);
    int(__stdcall* VendorId)(Gpu*, const char**);
    int(__stdcall* ASICFamilyType)(Gpu*, int*);
    int(__stdcall* Type)(Gpu*, int*);
    int(__stdcall* IsExternal)(Gpu*, bool*);
    int(__stdcall* Name)(Gpu*, const char**);
    int(__stdcall* DriverPath)(Gpu*, const char**);
    int(__stdcall* PNPString)(Gpu*, const char**);
    int(__stdcall* HasDesktops)(Gpu*, bool*);
    int(__stdcall* TotalVRAM)(Gpu*, std::uint32_t*);
    int(__stdcall* VRAMType)(Gpu*, const char**);
    int(__stdcall* BIOSInfo)(Gpu*, const char**, const char**, const char**);
    int(__stdcall* DeviceId)(Gpu*, const char**);
};
struct Gpu { const GpuVtbl* pVtbl; };

struct PerfServicesVtbl {
    long(__stdcall* Acquire)(PerfServices*);
    long(__stdcall* Release)(PerfServices*);
    int(__stdcall* QueryInterface)(PerfServices*, const wchar_t*, void**);
    void* GetSamplingIntervalRange;
    void* SetSamplingInterval;
    void* GetSamplingInterval;
    void* GetMaxPerformanceMetricsHistorySizeRange;
    void* SetMaxPerformanceMetricsHistorySize;
    void* GetMaxPerformanceMetricsHistorySize;
    void* ClearPerformanceMetricsHistory;
    void* GetCurrentPerformanceMetricsHistorySize;
    void* StartPerformanceMetricsTracking;
    void* StopPerformanceMetricsTracking;
    void* GetAllMetricsHistory;
    void* GetGPUMetricsHistory;
    void* GetSystemMetricsHistory;
    void* GetFPSHistory;
    void* GetCurrentAllMetrics;
    int(__stdcall* GetCurrentGPUMetrics)(PerfServices*, Gpu*, GpuMetrics**);
    void* GetCurrentSystemMetrics;
    void* GetCurrentFPS;
    int(__stdcall* GetSupportedGPUMetrics)(PerfServices*, Gpu*, GpuMetricsSupport**);
};
struct PerfServices { const PerfServicesVtbl* pVtbl; };

struct GpuMetricsSupportVtbl {
    long(__stdcall* Acquire)(GpuMetricsSupport*);
    long(__stdcall* Release)(GpuMetricsSupport*);
    int(__stdcall* QueryInterface)(GpuMetricsSupport*, const wchar_t*, void**);
    int(__stdcall* IsSupportedGPUUsage)(GpuMetricsSupport*, bool*);
    int(__stdcall* IsSupportedGPUClockSpeed)(GpuMetricsSupport*, bool*);
    int(__stdcall* IsSupportedGPUVRAMClockSpeed)(GpuMetricsSupport*, bool*);
    int(__stdcall* IsSupportedGPUTemperature)(GpuMetricsSupport*, bool*);
    int(__stdcall* IsSupportedGPUHotspotTemperature)(GpuMetricsSupport*, bool*);
    int(__stdcall* IsSupportedGPUPower)(GpuMetricsSupport*, bool*);
    int(__stdcall* IsSupportedGPUTotalBoardPower)(GpuMetricsSupport*, bool*);
};
struct GpuMetricsSupport { const GpuMetricsSupportVtbl* pVtbl; };

struct GpuMetricsVtbl {
    long(__stdcall* Acquire)(GpuMetrics*);
    long(__stdcall* Release)(GpuMetrics*);
    int(__stdcall* QueryInterface)(GpuMetrics*, const wchar_t*, void**);
    int(__stdcall* TimeStamp)(GpuMetrics*, std::int64_t*);
    int(__stdcall* GPUUsage)(GpuMetrics*, double*);
    int(__stdcall* GPUClockSpeed)(GpuMetrics*, int*);
    int(__stdcall* GPUVRAMClockSpeed)(GpuMetrics*, int*);
    int(__stdcall* GPUTemperature)(GpuMetrics*, double*);
    int(__stdcall* GPUHotspotTemperature)(GpuMetrics*, double*);
    int(__stdcall* GPUPower)(GpuMetrics*, double*);
    int(__stdcall* GPUTotalBoardPower)(GpuMetrics*, double*);
};
struct GpuMetrics { const GpuMetricsVtbl* pVtbl; };

using QueryFullVersionFn = int(__cdecl*)(std::uint64_t*);
using InitializeFn = int(__cdecl*)(std::uint64_t, System**);
using TerminateFn = int(__cdecl*)();
}  // namespace adlx_abi

HMODULE g_adlx = nullptr;
bool g_adlxTried = false;
bool g_adlxInitialized = false;
adlx_abi::TerminateFn g_adlxTerminate = nullptr;
adlx_abi::PerfServices* g_adlxPerf = nullptr;
adlx_abi::Gpu* g_adlxGpu = nullptr;
bool g_adlxHasBoardPower = false;
bool g_adlxHasGpuPower = false;

std::string NarrowAscii(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) s += (char)(c < 128 ? c : '?');
    return s;
}

void StopAdlx() {
    if (g_adlxGpu) { g_adlxGpu->pVtbl->Release(g_adlxGpu); g_adlxGpu = nullptr; }
    if (g_adlxPerf) { g_adlxPerf->pVtbl->Release(g_adlxPerf); g_adlxPerf = nullptr; }
    if (g_adlxInitialized && g_adlxTerminate) g_adlxTerminate();
    g_adlxInitialized = false;
    g_adlxTerminate = nullptr;
    if (g_adlx) { FreeLibrary(g_adlx); g_adlx = nullptr; }
    g_adlxHasBoardPower = g_adlxHasGpuPower = false;
    g_adlxTried = false;
}

void StartAdlx() {
    if (g_adlxTried) return;
    g_adlxTried = true;
    g_adlx = LoadLibraryExW(L"amdadlx64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_adlx) return;
    auto queryVer = Proc<adlx_abi::QueryFullVersionFn>(g_adlx, "ADLXQueryFullVersion");
    auto init = Proc<adlx_abi::InitializeFn>(g_adlx, "ADLXInitialize");
    g_adlxTerminate = Proc<adlx_abi::TerminateFn>(g_adlx, "ADLXTerminate");
    if (!queryVer || !init || !g_adlxTerminate) { StopAdlx(); g_adlxTried = true; return; }
    std::uint64_t fullVer = 0;
    adlx_abi::System* sys = nullptr;
    if (queryVer(&fullVer) != 0 || init(fullVer, &sys) != 0 || !sys) {
        StopAdlx();
        g_adlxTried = true;
        return;
    }
    g_adlxInitialized = true;
    adlx_abi::GpuList* list = nullptr;
    if (sys->pVtbl->GetPerformanceMonitoringServices(sys, &g_adlxPerf) != 0 || !g_adlxPerf ||
        sys->pVtbl->GetGPUs(sys, &list) != 0 || !list) {
        StopAdlx();
        g_adlxTried = true;
        return;
    }
    const std::uint32_t count = list->pVtbl->Size(list);
    const std::string wantName = NarrowAscii(g_adapter.name);
    for (std::uint32_t i = 0; i < count; ++i) {
        adlx_abi::Gpu* g = nullptr;
        if (list->pVtbl->At_GPUList(list, i, &g) != 0 || !g) continue;
        bool match = (count == 1);
        const char* devId = nullptr;
        if (!match && g_adapter.deviceId && g->pVtbl->DeviceId(g, &devId) == 0 && devId) {
            unsigned long parsed = std::strtoul(devId, nullptr, 16);
            if (parsed == g_adapter.deviceId) match = true;
        }
        const char* gpuName = nullptr;
        if (!match && g->pVtbl->Name(g, &gpuName) == 0 && gpuName) {
            if (wantName.find(gpuName) != std::string::npos || std::string(gpuName).find(wantName) != std::string::npos)
                match = true;
        }
        if (match && !g_adlxGpu) g_adlxGpu = g;
        else g->pVtbl->Release(g);
    }
    list->pVtbl->Release(list);
    if (!g_adlxGpu) { StopAdlx(); g_adlxTried = true; return; }
    adlx_abi::GpuMetricsSupport* sup = nullptr;
    if (g_adlxPerf->pVtbl->GetSupportedGPUMetrics(g_adlxPerf, g_adlxGpu, &sup) == 0 && sup) {
        sup->pVtbl->IsSupportedGPUTotalBoardPower(sup, &g_adlxHasBoardPower);
        sup->pVtbl->IsSupportedGPUPower(sup, &g_adlxHasGpuPower);
        sup->pVtbl->Release(sup);
    } else {
        g_adlxHasBoardPower = g_adlxHasGpuPower = true;
    }
}

int AdlxWatts() {
    StartAdlx();
    if (!g_adlxPerf || !g_adlxGpu || (!g_adlxHasBoardPower && !g_adlxHasGpuPower)) return -1;
    adlx_abi::GpuMetrics* m = nullptr;
    if (g_adlxPerf->pVtbl->GetCurrentGPUMetrics(g_adlxPerf, g_adlxGpu, &m) != 0 || !m) return -1;
    int watts = -1;
    double val = 0.0;
    if (g_adlxHasBoardPower && m->pVtbl->GPUTotalBoardPower(m, &val) == 0) watts = RoundGpuWatts(val);
    if (watts < 0 && g_adlxHasGpuPower && m->pVtbl->GPUPower(m, &val) == 0) watts = RoundGpuWatts(val);
    m->pVtbl->Release(m);
    return watts;
}

// ---- Intel IGCL (ControlLib.dll) ----
namespace igcl_abi {
struct InitArgs {
    std::uint32_t Size;
    std::uint8_t Version;
    std::uint32_t AppVersion;
    std::uint32_t flags;
    std::uint32_t SupportedVersion;
    std::uint8_t ApplicationUID[16];
};
struct FirmwareVersion { std::uint64_t major, minor, build; };
struct Bdf { std::uint8_t bus, device, function; };
struct AdapterProperties {
    std::uint32_t Size;
    std::uint8_t Version;
    void* pDeviceID;
    std::uint32_t device_id_size;
    std::uint32_t device_type;
    std::uint32_t supported_subfunction_flags;
    std::uint64_t driver_version;
    FirmwareVersion firmware_version;
    std::uint32_t pci_vendor_id;
    std::uint32_t pci_device_id;
    std::uint32_t rev_id;
    std::uint32_t num_eus_per_sub_slice;
    std::uint32_t num_sub_slices_per_slice;
    std::uint32_t num_slices;
    char name[100];
    std::uint32_t graphics_adapter_properties;
    std::uint32_t Frequency;
    std::uint16_t pci_subsys_id;
    std::uint16_t pci_subsys_vendor_id;
    Bdf adapter_bdf;
    std::uint32_t num_xe_cores;
    char reserved[108];
};
struct EnergyCounter {
    std::uint32_t Size;
    std::uint8_t Version;
    std::uint64_t energy;
    std::uint64_t timestamp;
};
using InitFn = int(__cdecl*)(InitArgs*, void**);
using CloseFn = int(__cdecl*)(void*);
using EnumDevicesFn = int(__cdecl*)(void*, std::uint32_t*, void**);
using GetPropsFn = int(__cdecl*)(void*, AdapterProperties*);
using EnumPowerDomainsFn = int(__cdecl*)(void*, std::uint32_t*, void**);
using GetEnergyFn = int(__cdecl*)(void*, EnergyCounter*);
}  // namespace igcl_abi

HMODULE g_igcl = nullptr;
bool g_igclTried = false;
void* g_igclApi = nullptr;
void* g_igclPowerDomain = nullptr;
igcl_abi::CloseFn g_ctlClose = nullptr;
igcl_abi::GetEnergyFn g_ctlGetEnergy = nullptr;
std::uint64_t g_igclPrevEnergy = 0;
std::uint64_t g_igclPrevTs = 0;

void StopIgcl() {
    if (g_igclApi && g_ctlClose) g_ctlClose(g_igclApi);
    g_igclApi = nullptr;
    g_igclPowerDomain = nullptr;
    g_ctlClose = nullptr;
    g_ctlGetEnergy = nullptr;
    g_igclPrevEnergy = 0;
    g_igclPrevTs = 0;
    if (g_igcl) { FreeLibrary(g_igcl); g_igcl = nullptr; }
    g_igclTried = false;
}

void StartIgcl() {
    if (g_igclTried) return;
    g_igclTried = true;
    g_igcl = LoadLibraryExW(L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_igcl) return;
    auto init = Proc<igcl_abi::InitFn>(g_igcl, "ctlInit");
    g_ctlClose = Proc<igcl_abi::CloseFn>(g_igcl, "ctlClose");
    auto enumDevs = Proc<igcl_abi::EnumDevicesFn>(g_igcl, "ctlEnumerateDevices");
    auto getProps = Proc<igcl_abi::GetPropsFn>(g_igcl, "ctlGetDeviceProperties");
    auto enumPwr = Proc<igcl_abi::EnumPowerDomainsFn>(g_igcl, "ctlEnumPowerDomains");
    g_ctlGetEnergy = Proc<igcl_abi::GetEnergyFn>(g_igcl, "ctlPowerGetEnergyCounter");
    if (!init || !g_ctlClose || !enumDevs || !getProps || !enumPwr || !g_ctlGetEnergy) {
        StopIgcl();
        g_igclTried = true;
        return;
    }
    igcl_abi::InitArgs args{};
    args.Size = sizeof(args);
    args.Version = 0;
    args.AppVersion = (1u << 16) | 1u;
    args.flags = 1u;
    if (init(&args, &g_igclApi) != 0 || !g_igclApi) {
        StopIgcl();
        g_igclTried = true;
        return;
    }
    std::uint32_t devCount = 0;
    if (enumDevs(g_igclApi, &devCount, nullptr) != 0 || devCount == 0) {
        StopIgcl();
        g_igclTried = true;
        return;
    }
    std::vector<void*> devs(devCount, nullptr);
    if (enumDevs(g_igclApi, &devCount, devs.data()) != 0) {
        StopIgcl();
        g_igclTried = true;
        return;
    }
    void* chosen = nullptr;
    const std::string wantName = NarrowAscii(g_adapter.name);
    for (std::uint32_t i = 0; i < devCount; ++i) {
        if (!devs[i]) continue;
        LUID devLuid{};
        igcl_abi::AdapterProperties props{};
        props.Size = sizeof(props);
        props.Version = 0;
        props.pDeviceID = &devLuid;
        props.device_id_size = sizeof(devLuid);
        if (getProps(devs[i], &props) != 0 || props.device_type != 1) continue;
        const bool luidMatch = (devLuid.LowPart || devLuid.HighPart) &&
                               devLuid.LowPart == g_adapter.raw.LowPart &&
                               devLuid.HighPart == g_adapter.raw.HighPart;
        const bool pciMatch = g_adapter.deviceId && props.pci_device_id == g_adapter.deviceId;
        const bool nameMatch = props.name[0] &&
                               (wantName.find(props.name) != std::string::npos ||
                                std::string(props.name).find(wantName) != std::string::npos);
        if (luidMatch || pciMatch || nameMatch || devCount == 1) {
            chosen = devs[i];
            break;
        }
    }
    if (!chosen) { StopIgcl(); g_igclTried = true; return; }
    std::uint32_t domCount = 0;
    if (enumPwr(chosen, &domCount, nullptr) != 0 || domCount == 0) {
        StopIgcl();
        g_igclTried = true;
        return;
    }
    std::vector<void*> domains(domCount, nullptr);
    if (enumPwr(chosen, &domCount, domains.data()) != 0) {
        StopIgcl();
        g_igclTried = true;
        return;
    }
    for (std::uint32_t i = 0; i < domCount; ++i) {
        if (!domains[i]) continue;
        igcl_abi::EnergyCounter ec{};
        ec.Size = sizeof(ec);
        ec.Version = 0;
        if (g_ctlGetEnergy(domains[i], &ec) == 0 && ec.timestamp != 0) {
            g_igclPowerDomain = domains[i];
            g_igclPrevEnergy = ec.energy;
            g_igclPrevTs = ec.timestamp;
            break;
        }
    }
    if (!g_igclPowerDomain) { StopIgcl(); g_igclTried = true; }
}

int IgclWatts() {
    StartIgcl();
    if (!g_igclPowerDomain || !g_ctlGetEnergy) return -1;
    igcl_abi::EnergyCounter ec{};
    ec.Size = sizeof(ec);
    ec.Version = 0;
    if (g_ctlGetEnergy(g_igclPowerDomain, &ec) != 0) return -1;
    const int watts = IgclWattsFromEnergyDelta(g_igclPrevEnergy, g_igclPrevTs, ec.energy, ec.timestamp);
    if (ec.timestamp > g_igclPrevTs) {
        g_igclPrevEnergy = ec.energy;
        g_igclPrevTs = ec.timestamp;
    }
    return watts;
}

int SampleVendorWatts() {
    switch (g_adapter.vendorId) {
        case 0x10DE: return NvmlWatts();
        case 0x1002: return AdlxWatts();
        case 0x8086: return IgclWatts();
        default:     return -1;
    }
}

void StopVendorLibs() {
    if (g_nvml) {
        if (g_nvmlDevice && g_nvmlShutdown) g_nvmlShutdown();
        FreeLibrary(g_nvml);
    }
    g_nvml = nullptr;
    g_nvmlDevice = nullptr;
    g_nvmlPower = nullptr;
    g_nvmlClock = nullptr;
    g_nvmlTried = false;
    StopAdlx();
    StopIgcl();
}

}  // namespace

Reading Sample(bool wantTemperature, bool wantPower, bool wantClocks) {
    Reading r;
    if (!g_adapterLooked) {
        g_adapter = FindAdapter();
        g_adapterLooked = true;
    }
    if (!g_adapter.found) return r;
    r.available = true;
    r.name = g_adapter.name;
    r.vramTotalGb = g_adapter.vramBytes / 1073741824.0;
    if (StartCounters() && g_collect(g_query) == ERROR_SUCCESS) {
        if (g_collectedOnce) {  // utilization is a rate: it needs two collections
            std::vector<EngineSample> engines;
            for (auto& [name, value] : Values(g_engines)) {
                EngineSample s;
                if (ParseInstance(name, s.luid, &s.type)) {
                    s.percent = value;
                    engines.push_back(s);
                }
            }
            r.use = AdapterUse(engines, g_adapter.luid);
            if (r.use < 0) r.use = 0;  // no engine instance: the adapter is idle (or asleep)
        }
        g_collectedOnce = true;
        double used = 0;
        bool any = false;
        for (auto& [name, value] : Values(g_memory)) {
            std::uint64_t luid = 0;
            if (ParseInstance(name, luid) && luid == g_adapter.luid) {
                used += value;
                any = true;
            }
        }
        if (any && g_adapter.vramBytes) {
            r.vramUsedGb = used / 1073741824.0;
            r.vram = (int)std::min(100.0, used * 100.0 / (double)g_adapter.vramBytes + 0.5);
        }
    }
    if (!wantPower) {
        StopAdlx();
        StopIgcl();
    }
    if (wantTemperature || wantPower || wantClocks) {
        int temp = -1, pct = -1;
        std::uint64_t memFreqHz = 0;
        PerfData(temp, pct, memFreqHz);
        if (wantTemperature) r.tempC = temp;
        if (wantPower) {
            r.powerPct = pct;
            r.watts = SampleVendorWatts();
        }
        if (wantClocks) {
            NvmlClocks(r.gpuClockMhz, r.vramClockMhz);
            if (r.vramClockMhz < 0 && memFreqHz >= 1000000ULL)
                r.vramClockMhz = (int)(memFreqHz / 1000000ULL);
        }
        if ((wantPower && r.watts < 0) || (wantClocks && (r.gpuClockMhz < 0 || r.vramClockMhz < 0))) {
            QueryAdl(wantPower, wantClocks, r.watts, r.gpuClockMhz, r.vramClockMhz);
        }
    }
    return r;
}

void Stop() {
    if (g_query && g_close) g_close(g_query);
    g_query = nullptr;
    g_engines = g_memory = nullptr;
    g_collectedOnce = false;
    if (g_kmtAdapter && g_closeAdapter) {
        CloseAdapterArgs c{ g_kmtAdapter };
        g_closeAdapter(&c);
    }
    g_kmtAdapter = 0;
    StopVendorLibs();
    if (g_adl) {
        if (g_adlCtx && g_adlDestroy) g_adlDestroy(g_adlCtx);
        FreeLibrary(g_adl);
    }
    g_adl = nullptr;
    g_adlCtx = nullptr;
    g_adlDestroy = nullptr;
    g_adlPmLog = nullptr;
    g_adlAdapterIndex = -1;
    g_adlTried = false;
    g_adapterLooked = false;  // look again next time: a GPU may have been added or switched
}

}  // namespace gpumon
