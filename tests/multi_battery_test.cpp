#include "../src/devices.h"
#include <cassert>
#include <cstdio>

int main() {
    // 1. Single battery device (backward compatibility)
    {
        DeviceStatus s;
        s.key = L"logi:mouse";
        s.name = L"Logitech Mouse";
        s.kind = DeviceKind::Mouse;
        s.percent = 85;
        s.charging = false;
        assert(s.batteries.empty());
        assert(!s.hasCapacity);
    }

    // 2. Earbuds with Left, Right, and Case
    {
        DeviceStatus buds;
        buds.key = L"buds:galaxy";
        buds.name = L"Galaxy Buds";
        buds.kind = DeviceKind::Headset;

        BatteryComponent left{ L"Left", 85, false, false };
        BatteryComponent right{ L"Right", 78, false, false };
        BatteryComponent box{ L"Case", 62, true, false };

        buds.batteries.push_back(left);
        buds.batteries.push_back(right);
        buds.batteries.push_back(box);

        assert(buds.batteries.size() == 3);
        assert(buds.batteries[0].name == L"Left" && buds.batteries[0].percent == 85);
        assert(buds.batteries[1].name == L"Right" && buds.batteries[1].percent == 78);
        assert(buds.batteries[2].name == L"Case" && buds.batteries[2].percent == 62 && buds.batteries[2].charging);

        // Overall device percentage should represent the active earbuds, not the charging case
        // In our earbuds logic, percent is min(left, right) = 78%
        buds.percent = std::min(buds.batteries[0].percent, buds.batteries[1].percent);
        assert(buds.percent == 78);
    }

    // 3. Earbuds with missing component (e.g. one bud in ear, one in case)
    {
        DeviceStatus buds;
        buds.key = L"airpods:single";
        buds.name = L"AirPods";
        buds.batteries.push_back({ L"Left", 90, false, false });
        buds.batteries.push_back({ L"Right", -1, false, false }); // Right missing / unknown
        buds.batteries.push_back({ L"Case", 100, true, true });

        // Overall percentage uses the available active bud
        int overall = -1;
        if (buds.batteries[0].percent >= 0 && buds.batteries[1].percent >= 0)
            overall = std::min(buds.batteries[0].percent, buds.batteries[1].percent);
        else if (buds.batteries[0].percent >= 0)
            overall = buds.batteries[0].percent;
        else
            overall = buds.batteries[1].percent;

        assert(overall == 90);
        assert(buds.batteries[1].percent == -1);
    }

    // 4. Component-specific charging states
    {
        BatteryComponent b1{ L"Left", 100, true, true };
        BatteryComponent b2{ L"Right", 50, false, false };
        assert(b1.charging && b1.full);
        assert(!b2.charging && !b2.full);
    }

    std::puts("Multi-battery device model tests passed");
    return 0;
}
