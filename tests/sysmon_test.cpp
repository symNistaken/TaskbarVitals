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
    std::puts("System monitor tests passed (CPU share, rounding, idle and busy, bad samples).");
    return 0;
}
