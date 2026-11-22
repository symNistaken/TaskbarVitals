#include "sysmon.h"

#include <windows.h>

namespace sysmon {
namespace {

std::uint64_t Ticks(const FILETIME& f) { return ((std::uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

bool g_havePrevious = false;
CpuTimes g_previous;

}  // namespace

Reading Sample() {
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
    return r;
}

}  // namespace sysmon
