#include "gpumon.h"

#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cwchar>
#include <map>

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

int Temperature() {
    if (!g_kmtAdapter) {
        HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
        auto open = Proc<OpenAdapterFn>(gdi, "D3DKMTOpenAdapterFromLuid");
        g_queryInfo = Proc<QueryInfoFn>(gdi, "D3DKMTQueryAdapterInfo");
        g_closeAdapter = Proc<CloseAdapterFn>(gdi, "D3DKMTCloseAdapter");
        OpenFromLuid args{ g_adapter.raw, 0 };
        if (!open || !g_queryInfo || !g_closeAdapter || open(&args) != 0) return -1;
        g_kmtAdapter = args.hAdapter;
    }
    AdapterPerfData perf{};
    QueryInfoArgs q{ g_kmtAdapter, kAdapterPerfData, &perf, sizeof(perf) };
    if (g_queryInfo(&q) != 0 || perf.Temperature == 0 || perf.Temperature > 1500) return -1;
    return (int)((perf.Temperature + 5) / 10);
}

}  // namespace

Reading Sample(bool wantTemperature) {
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
    if (wantTemperature) r.tempC = Temperature();
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
    g_adapterLooked = false;  // look again next time: a GPU may have been added or switched
}

}  // namespace gpumon
