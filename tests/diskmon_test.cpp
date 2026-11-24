// Pure helper checks for the SSD/HDD monitor (heat thresholds, clamping, capacity formatting).
#include <cassert>
#include <cstdio>

#include "../src/diskmon.h"

int main() {
    assert(diskmon::HeatLevel(-1) == 0);
    assert(diskmon::HeatLevel(64) == 0);
    assert(diskmon::HeatLevel(65) == 1);
    assert(diskmon::HeatLevel(74) == 1);
    assert(diskmon::HeatLevel(75) == 2);

    assert(diskmon::ClampPercent(-1.0) == -1);
    assert(diskmon::ClampPercent(0.2) == 0);
    assert(diskmon::ClampPercent(45.6) == 46);
    assert(diskmon::ClampPercent(134.0) == 100);

    assert(diskmon::FormatCapacityGb(480.0) == L"480 GB");
    assert(diskmon::FormatCapacityGb(2048.0) == L"2.0 TB");

    std::puts("Disk monitor tests passed (heat levels, utilization clamp, capacity formatting).");
    return 0;
}
