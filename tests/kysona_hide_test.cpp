#include "../src/drivers/kysona_hide.h"

#include <cassert>
#include <cstdio>

static DeviceStatus Mouse(const wchar_t* key, int percent) {
    DeviceStatus s;
    s.key = key;
    s.percent = percent;
    return s;
}

static int Visible(const std::vector<DeviceStatus>& out, const KysonaEntries& live) {
    int n = (int)live.size();
    for (auto& s : out) n += s.connected ? 1 : 0;
    return n;
}

int main() {
    const wchar_t* dongle = L"kysona:3554:F5F7";
    const wchar_t* cable = L"kysona:3554:F5F6";
    KysonaEntries lastLive;
    {   // On the dongle: one entry.
        std::vector<DeviceStatus> out;
        KysonaEntries live{ { dongle, Mouse(dongle, 30) } };
        HideSilentKysona(live, {}, lastLive, out);
        assert(out.empty() && Visible(out, live) == 1);
    }
    {   // Cable plugged in: the dongle says "not connected" and is reported off, not held.
        std::vector<DeviceStatus> out;
        KysonaEntries live{ { cable, Mouse(cable, 31) } };
        KysonaEntries off{ { dongle, Mouse(dongle, 30) } };
        HideSilentKysona(live, off, lastLive, out);
        assert(out.size() == 1 && out[0].key == dongle && !out[0].connected && out[0].percent == -1);
        assert(Visible(out, live) == 1);
    }
    {   // Cable pulled: the cable id goes silent and is reported off; the dongle is live again.
        std::vector<DeviceStatus> out;
        KysonaEntries live{ { dongle, Mouse(dongle, 32) } };
        HideSilentKysona(live, {}, lastLive, out);
        assert(out.size() == 1 && out[0].key == cable && !out[0].connected);
        assert(Visible(out, live) == 1);
    }
    {   // Mouse asleep on the dongle, nothing live: report nothing, so the tray holds the last value.
        std::vector<DeviceStatus> out;
        KysonaEntries off{ { dongle, Mouse(dongle, 32) } };
        HideSilentKysona({}, off, lastLive, out);
        assert(out.empty());
    }
    std::puts("Kysona hide tests passed (dongle, cable in, cable out, asleep).");
    return 0;
}
