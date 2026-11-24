// Pure helper tests for the laptop battery monitor (power rate parsing, signed watt formatting, remaining runtime).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../src/laptopbat.h"
#include "../src/system_order.h"

int main() {
    using namespace laptopbat;
    using namespace system_order;

    bool ok = true;
    (void)ParsePowerRateMw((std::int32_t)0x80000000, ok);
    assert(!ok);
    (void)ParsePowerRateMw(600000, ok);
    assert(!ok);

    double w = ParsePowerRateMw(-18400, ok);
    assert(ok && std::fabs(w - (-18.4)) < 1e-6);
    w = ParsePowerRateMw(45200, ok);
    assert(ok && std::fabs(w - 45.2) < 1e-6);

    assert(FormatWattsCompact(-18.4) == L"-18W");
    assert(FormatWattsCompact(45.2) == L"+45W");
    assert(FormatWattsCompact(0.1) == L"0W");

    assert(FormatWattsDetailed(-18.4) == L"-18.4 W");
    assert(FormatWattsDetailed(45.2) == L"+45.2 W");
    assert(FormatWattsDetailed(0.01) == L"0.0 W");

    assert(FormatRemaining(0).empty());
    assert(FormatRemaining(42) == L"42 min");
    assert(FormatRemaining(120) == L"2 h");
    assert(FormatRemaining(135) == L"2 h 15 min");

    // SystemOrder encoding/decoding/moving for 5, 6, 7, and 8 items.
    assert((DecodeSystemOrder(0x00003210u, 5) == std::vector<int>{ 0, 1, 2, 3, 4 }));
    const std::uint32_t encoded5 = EncodeSystemOrder({ 4, 2, 0, 3, 1 });
    assert(encoded5 == 0xFFF13024u);
    assert((DecodeSystemOrder(encoded5, 7) == std::vector<int>{ 4, 2, 0, 3, 1, 5, 6 }));
    const std::vector<int> full7{ 6, 4, 2, 0, 5, 3, 1 };
    assert(DecodeSystemOrder(EncodeSystemOrder(full7), 7) == full7);
    const std::vector<int> full8{ 7, 5, 3, 1, 6, 4, 2, 0 };
    assert(DecodeSystemOrder(EncodeSystemOrder(full8), 8) == full8);
    std::vector<int> moved{ 0, 1, 2, 3, 4, 5, 6 };
    assert(!MoveOrderItem(moved, 0, -1));
    assert(!MoveOrderItem(moved, 6, +1));
    assert(MoveOrderItem(moved, 6, -1));
    assert((moved == std::vector<int>{ 0, 1, 2, 3, 4, 6, 5 }));

    std::puts("Laptop battery and system order tests passed.");
    return 0;
}
