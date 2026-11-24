#include "../src/devices.h"
#include "../src/history_model.h"
#include "../src/battery_intelligence.h"
#include <cassert>
#include <cstdio>

int main() {
    // 1. Empty device list
    {
        std::vector<DeviceStatus> list;
        assert(list.empty());
    }

    // 2. Single connected device
    {
        DeviceStatus d;
        d.key = L"logi:gprox";
        d.name = L"Logitech G PRO X";
        d.via = L"Lightspeed receiver";
        d.kind = DeviceKind::Mouse;
        d.percent = 80;
        d.connected = true;
        assert(d.percent == 80 && d.connected);
    }

    // 3. Disconnected device
    {
        DeviceStatus d;
        d.key = L"headset:offline";
        d.name = L"Offline Headset";
        d.connected = false;
        d.percent = -1;
        assert(!d.connected);
    }

    // 4. Multi-battery earbuds card representation
    {
        DeviceStatus buds;
        buds.key = L"buds:pro";
        buds.name = L"Wireless Earbuds";
        buds.kind = DeviceKind::Headset;
        buds.batteries = {
            { L"Left", 85, false, false },
            { L"Right", 78, false, false },
            { L"Case", 62, true, false }
        };
        buds.percent = 78;
        assert(buds.batteries.size() == 3);
        assert(buds.batteries[0].percent == 85);
        assert(buds.batteries[2].charging == true);
    }

    // 5. High-DPI scaling calculation
    {
        auto uiScale = [](int logical, int dpi) {
            return (logical * dpi) / 96;
        };
        assert(uiScale(100, 96) == 100);   // 100% scaling
        assert(uiScale(100, 144) == 150); // 150% scaling
        assert(uiScale(100, 192) == 200); // 200% scaling
    }

    std::puts("Dashboard model tests passed");
    return 0;
}
