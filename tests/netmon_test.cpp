// Network monitor pure helper checks (rate calculation, compact/full rate formatting, speed test Mbps).
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/netmon.h"

int main() {
    assert(netmon::RateBytesPerSec(100, 100, 0) < 0.0);
    assert(netmon::RateBytesPerSec(500, 100, 1000) < 0.0);
    assert(std::fabs(netmon::RateBytesPerSec(1000, 3000, 2000) - 1000.0) < 1e-6);

    assert(netmon::FormatRateCompact(-1.0) == L"-");
    assert(netmon::FormatRateCompact(512.0 * 1024.0) == L"512K");
    assert(netmon::FormatRateCompact(4.2 * 1024.0 * 1024.0) == L"4.2M");
    assert(netmon::FormatRateCompact(125.0 * 1024.0 * 1024.0) == L"125M");

    assert(netmon::FormatRateFull(420.0 * 1024.0) == L"420 KB/s");
    assert(netmon::FormatRateFull(4.2 * 1024.0 * 1024.0) == L"4.2 MB/s");

    assert(netmon::FormatMbps(-1.0) == L"-");
    assert(netmon::FormatMbps(8.4) == L"8.4 Mbps");
    assert(netmon::FormatMbps(94.2) == L"94 Mbps");

    std::puts("Network monitor tests passed (rate calculation, compact/full formatting, Mbps formatting).");
    return 0;
}
