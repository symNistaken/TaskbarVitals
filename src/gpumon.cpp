#include "gpumon.h"

#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
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
    if (wantTemperature || wantPower || wantClocks) {
        int temp = -1, pct = -1;
        std::uint64_t memFreqHz = 0;
        PerfData(temp, pct, memFreqHz);
        if (wantTemperature) r.tempC = temp;
        if (wantPower) {
            r.powerPct = pct;
            r.watts = NvmlWatts();
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
    if (g_nvml) {
        if (g_nvmlDevice && g_nvmlShutdown) g_nvmlShutdown();
        FreeLibrary(g_nvml);
    }
    g_nvml = nullptr;
    g_nvmlDevice = nullptr;
    g_nvmlPower = nullptr;
    g_nvmlClock = nullptr;
    g_nvmlTried = false;
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
