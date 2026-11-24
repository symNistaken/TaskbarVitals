// Network monitor pure helper checks (rate calculation, compact/full rate formatting, speed test Mbps).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "../src/netmon.h"
#include "../src/netmon_math.h"
#include "../src/system_order.h"

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

    // Netmon math & interface filtering checks from overnight suite.
    assert(netmon::IncludeInterface(6, netmon::kIfOperStatusUp, false));
    assert(!netmon::IncludeInterface(netmon::kIfTypeSoftwareLoopback, netmon::kIfOperStatusUp, false));
    assert(!netmon::IncludeInterface(netmon::kIfTypeTunnel, netmon::kIfOperStatusUp, false));
    assert(!netmon::IncludeInterface(6, netmon::kIfOperStatusUp, true));
    bool reset = true;
    assert(netmon::CounterDelta(0xFFFFFF00ULL, 0x00000100ULL, &reset) == 0x200ULL && !reset);
    assert(netmon::CounterDelta(0x100000000ULL, 500ULL, &reset) == 0 && reset);
    assert(netmon::FormatCompactRate(1.2 * 1024.0 * 1024.0) == L"1.2M");
    assert(netmon::FormatStripSpeed(1.2 * 1024.0 * 1024.0, 85.0 * 1024.0) == L"\u21931.2M \u219185K");
    assert((system_order::DecodeSystemOrder(system_order::EncodeSystemOrder({ 4, 2, 0, 3, 1 }), 6) ==
            std::vector<int>{ 4, 2, 0, 3, 1, 5 }));

    std::puts("Network monitor tests passed (rate calculation, filtering, counters, compact/full formatting).");
    return 0;
}
