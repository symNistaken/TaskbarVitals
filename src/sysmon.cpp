#include "sysmon.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <vector>

namespace sysmon {
namespace {

std::uint64_t Ticks(const FILETIME& f) { return ((std::uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

bool g_havePrevious = false;
CpuTimes g_previous;

// ---- processor power: "\Energy Meter(*)\Power" (milliwatts), pdh.dll loaded on first use ----
template <class F>
F Proc(HMODULE m, const char* name) {
    return m ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
}
using OpenQueryFn = PDH_STATUS(WINAPI*)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
using AddCounterFn = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
using CollectFn = PDH_STATUS(WINAPI*)(PDH_HQUERY);
using ArrayFn = PDH_STATUS(WINAPI*)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
using CloseQueryFn = PDH_STATUS(WINAPI*)(PDH_HQUERY);
HMODULE g_pdh;
CollectFn g_collect;
ArrayFn g_array;
CloseQueryFn g_close;
PDH_HQUERY g_query;
PDH_HCOUNTER g_power;
bool g_powerMissing;  // the machine has no package meter: stop asking

bool StartPower() {
    if (g_query) return true;
    if (g_powerMissing) return false;
    if (!g_pdh) g_pdh = LoadLibraryExW(L"pdh.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto open = Proc<OpenQueryFn>(g_pdh, "PdhOpenQueryW");
    auto add = Proc<AddCounterFn>(g_pdh, "PdhAddEnglishCounterW");
    g_collect = Proc<CollectFn>(g_pdh, "PdhCollectQueryData");
    g_array = Proc<ArrayFn>(g_pdh, "PdhGetFormattedCounterArrayW");
    g_close = Proc<CloseQueryFn>(g_pdh, "PdhCloseQuery");
    if (!open || !add || !g_collect || !g_array || !g_close || open(nullptr, 0, &g_query) != ERROR_SUCCESS) {
        g_query = nullptr;
        g_powerMissing = true;
        return false;
    }
    if (add(g_query, L"\\Energy Meter(*)\\Power", 0, &g_power) != ERROR_SUCCESS) {
        g_close(g_query);
        g_query = nullptr;
        g_powerMissing = true;
        return false;
    }
    return true;
}

int PackageWatts() {
    if (!StartPower() || g_collect(g_query) != ERROR_SUCCESS) return -1;
    DWORD bytes = 0, count = 0;
    if (g_array(g_power, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr) != (PDH_STATUS)PDH_MORE_DATA || !bytes) return -1;
    std::vector<BYTE> buf(bytes);
    auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
    if (g_array(g_power, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items) != ERROR_SUCCESS) return -1;
    for (DWORD i = 0; i < count; i++) {
        bool valid = items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA;
        if (valid && IsPackageMeter(items[i].szName) && items[i].FmtValue.doubleValue >= 0)
            return (int)(items[i].FmtValue.doubleValue / 1000.0 + 0.5);
    }
    return -1;
}

}  // namespace

Reading Sample(bool wantPower) {
    Reading r;
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        CpuTimes now{ Ticks(idle), Ticks(kernel), Ticks(user) };
        if (g_havePrevious) r.cpu = CpuPercent(g_previous, now);
        g_previous = now;
        g_havePrevious = true;
    }
    MEMORYSTATUSEX m{ sizeof(m) };
    if (GlobalMemoryStatusEx(&m) && m.ullTotalPhys) {
        r.ram = (int)m.dwMemoryLoad;
        r.ramTotalGb = m.ullTotalPhys / 1073741824.0;
        r.ramUsedGb = (m.ullTotalPhys - m.ullAvailPhys) / 1073741824.0;
    }
    if (wantPower) r.cpuWatts = PackageWatts();
    return r;
}

void StopPower() {
    if (g_query && g_close) g_close(g_query);
    g_query = nullptr;
    g_power = nullptr;
}

}  // namespace sysmon
