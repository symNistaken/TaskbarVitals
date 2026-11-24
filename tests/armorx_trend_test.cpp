#include "../src/drivers/armorx_trend.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

static bool Feed(ArmorX::Trend& t, std::initializer_list<int> readings, int* last = nullptr) {
    bool charging = false;
    int level = -1;
    for (int r : readings) level = t.Add(r, charging);
    if (last) *last = level;
    return charging;
}

int main() {
    int level = 0;
    {   // Discharging slowly: never charging.
        ArmorX::Trend t;
        assert(!Feed(t, { 80, 80, 79, 79, 78, 78, 77 }, &level));
        assert(level == 78);
    }
    {   // Plugged in: a steady rise flags charging once the trend window is full.
        ArmorX::Trend t;
        bool charging = false;
        t.Add(60, charging);
        assert(!charging);
        assert(Feed(t, { 61, 62, 63, 64, 65, 66 }));
    }
    {   // One jittery high reading is smoothed away, not taken as charging.
        ArmorX::Trend t;
        assert(!Feed(t, { 70, 70, 76, 70, 70, 70 }, &level));
        assert(level == 70);
    }
    {   // Topping off near full is "full", not charging.
        ArmorX::Trend t;
        assert(!Feed(t, { 96, 97, 98, 99, 100, 100 }));
    }
    {   // Unplugged after charging: the flag clears once the rise stops.
        ArmorX::Trend t;
        assert(Feed(t, { 50, 52, 54, 56, 58, 60 }));
        assert(!Feed(t, { 60, 60, 60, 60, 60 }));
    }
    std::puts("Armor X trend tests passed (discharge, rise, jitter, full, unplug).");
    return 0;
}
