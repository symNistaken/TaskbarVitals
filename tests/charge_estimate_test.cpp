#include "../src/charge_estimate.h"
#include <cassert>
#include <cstdio>
using namespace charge;

int main() {
    constexpr int64_t t0 = 1795000000;
    // 30% -> 50% in 20 minutes: 1 point a minute, 50 points to go
    {
        Tracker t;
        Observe(t, true, 30, t0);
        Observe(t, true, 40, t0 + 600);
        Observe(t, true, 50, t0 + 1200);
        assert(MinutesToFull(t, t0 + 1200) == 50);
    }
    // too early (under 10 minutes) or too little rise: no estimate
    {
        Tracker t;
        Observe(t, true, 30, t0);
        Observe(t, true, 40, t0 + 300);
        assert(MinutesToFull(t, t0 + 300) == -1);
        Tracker u;
        Observe(u, true, 30, t0);
        Observe(u, true, 31, t0 + 1800);
        assert(MinutesToFull(u, t0 + 1800) == -1);
    }
    // unplugging resets; a drop while charging starts a new measurement
    {
        Tracker t;
        Observe(t, true, 30, t0);
        Observe(t, true, 50, t0 + 1200);
        Observe(t, false, 50, t0 + 1300);
        assert(t.startPercent == -1 && MinutesToFull(t, t0 + 1300) == -1);
        Observe(t, true, 60, t0);
        Observe(t, true, 55, t0 + 600);
        assert(t.startPercent == 55 && t.startTime == t0 + 600);
    }
    // full: 0 minutes
    {
        Tracker t;
        Observe(t, true, 100, t0);
        assert(MinutesToFull(t, t0) == 0);
    }
    // implausible rate (a jump of 40 points in 10 minutes): no estimate
    {
        Tracker t;
        Observe(t, true, 10, t0);
        Observe(t, true, 70, t0 + 600);
        assert(MinutesToFull(t, t0 + 600) == -1);
    }
    // charge-today warning: once per discharge, not while charging, not when the low alert applies
    {
        bool warned = false;
        assert(ShouldWarnShortRuntime(8.0, false, 40, 15, warned) && warned);
        assert(!ShouldWarnShortRuntime(6.0, false, 35, 15, warned));
        assert(!ShouldWarnShortRuntime(6.0, true, 36, 15, warned) && !warned);
        assert(ShouldWarnShortRuntime(5.0, false, 33, 15, warned));
        bool w2 = false;
        assert(!ShouldWarnShortRuntime(20.0, false, 60, 15, w2));
        assert(!ShouldWarnShortRuntime(3.0, false, 12, 15, w2));
        assert(!ShouldWarnShortRuntime(-1.0, false, 50, 15, w2));
    }
    std::puts("charge_estimate_test: all passed");
    return 0;
}
