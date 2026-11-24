// Pure helper tests for the laptop battery monitor (power rate parsing, signed watt formatting, remaining runtime).
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/laptopbat.h"

int main() {
    using namespace laptopbat;

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

    std::puts("Laptop battery tests passed (rate parsing, watt formatting, remaining runtime).");
    return 0;
}
