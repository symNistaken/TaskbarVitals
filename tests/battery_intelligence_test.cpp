#include "../src/battery_intelligence.h"
#include <cassert>
#include <cstdio>
#include <cmath>

int main() {
    using namespace battery_intelligence;
    const int64_t now = 1795000000;
    const std::wstring mouseKey = L"mouse:test1";
    const std::wstring mouseName = L"Gaming Mouse";

    auto sample = [&](int hoursAgo, int pct, bool chg = false) {
        return battery_history::Sample{ mouseKey, now - static_cast<int64_t>(hoursAgo) * 3600, pct, chg };
    };

    // 1. Normal battery discharge
    {
        std::vector<battery_history::Sample> s = {
            sample(6, 80), sample(4, 74), sample(2, 68), sample(0, 62)
        };
        auto r = CalculateDrainRate(mouseKey, mouseName, s, 62, false, now);
        assert(r.hasData);
        assert(r.sampleCount == 4);
        assert(r.percentDropped == 18);
        assert(std::abs(r.percentPerHour - 3.0) < 0.05); // 18% in 6 hours = 3%/hr
        assert(std::abs(r.percentPerDay - 72.0) < 0.1);
        assert(std::abs(r.hoursRemaining - (62.0 / 3.0)) < 0.1);
        assert(r.isMeasured);
        assert(!r.summaryMessage.empty());
    }

    // 2. Insufficient data
    {
        std::vector<battery_history::Sample> s = { sample(1, 50) };
        auto r = CalculateDrainRate(mouseKey, mouseName, s, 50, false, now);
        assert(!r.hasData);
    }

    // 3. Insufficient observation duration (< 1 hour)
    {
        std::vector<battery_history::Sample> s = {
            { mouseKey, now - 1200, 80, false },
            { mouseKey, now - 600, 79, false },
            { mouseKey, now, 78, false }
        };
        auto r = CalculateDrainRate(mouseKey, mouseName, s, 78, false, now);
        assert(!r.hasData);
    }

    // 4. Stale readings (latest sample > 3 hours old)
    {
        std::vector<battery_history::Sample> s = {
            sample(8, 80), sample(6, 75), sample(4, 70)
        };
        auto r = CalculateDrainRate(mouseKey, mouseName, s, 70, false, now);
        assert(!r.hasData);
        assert(r.statusReason.find(L"recent") != std::wstring::npos || r.statusReason.find(L"Stale") != std::wstring::npos);
    }

    // 5. Currently charging
    {
        std::vector<battery_history::Sample> s = {
            sample(6, 80), sample(4, 74), sample(2, 68), sample(0, 65, true)
        };
        auto r = CalculateDrainRate(mouseKey, mouseName, s, 65, true, now);
        assert(!r.hasData);
    }

    // 6. Charging in observation period does not pollute current discharge
    {
        std::vector<battery_history::Sample> s = {
            sample(12, 30), sample(10, 80, true), sample(8, 100, true),
            sample(6, 95), sample(4, 89), sample(2, 83), sample(0, 77)
        };
        auto r = CalculateDrainRate(mouseKey, mouseName, s, 77, false, now);
        assert(r.hasData);
        assert(r.sampleCount == 4);
        assert(r.percentDropped == 18);
        assert(std::abs(r.percentPerHour - 3.0) < 0.05);
    }

    // 7. Large gap (> 4 hours, e.g. PC slept overnight)
    {
        std::vector<battery_history::Sample> s = {
            sample(16, 90), sample(14, 87), sample(12, 84), // gap of 10 hours
            sample(2, 80), sample(1, 78), sample(0, 76)
        };
        auto r = CalculateDrainRate(mouseKey, mouseName, s, 76, false, now);
        // The segment breaks at the 10-hour gap; only the 3 recent samples remain (2 hours, drop 4%)
        assert(r.hasData);
        assert(r.sampleCount == 3);
        assert(r.percentDropped == 4);
        assert(std::abs(r.percentPerHour - 2.0) < 0.05);
    }

    // 8. Slight sensor noise / 1% fluctuations are tolerated, but big jumps break segment
    {
        std::vector<battery_history::Sample> s = {
            sample(6, 80), sample(4, 81), sample(2, 77), sample(0, 74) // 80 -> 81 is +1% noise
        };
        auto r = CalculateDrainRate(mouseKey, mouseName, s, 74, false, now);
        assert(r.hasData);
    }

    // 9. Full-charge runtime estimation
    {
        std::vector<battery_history::Sample> s = {
            sample(8, 90), sample(6, 86), sample(4, 82), sample(2, 78), sample(0, 74)
        };
        auto full = EstimateFullChargeRuntime(mouseKey, s, now);
        assert(full.hasEstimate);
        assert(full.averageHours > 0.0);
        // Rate is (90 - 74) / 8 = 2%/hr -> 100% / 2 = 50 hours
        assert(std::abs(full.averageHours - 50.0) < 1.0);
        assert(!full.displayLabel.empty());
    }

    // 10. Battery health tracking
    {
        DeviceStatus dev;
        dev.key = mouseKey;
        // Case A: No capacity exposed
        dev.hasCapacity = false;
        auto h1 = GetBatteryHealth(mouseKey, dev);
        assert(!h1.supported);
        assert(h1.healthPercent == -1);

        // Case B: Zero or invalid capacity
        dev.hasCapacity = true;
        dev.designCapacityMwh = 0;
        dev.fullChargeCapacityMwh = 500;
        auto h2 = GetBatteryHealth(mouseKey, dev);
        assert(!h2.supported);

        // Case C: Valid design and full charge capacity
        dev.designCapacityMwh = 1000;
        dev.fullChargeCapacityMwh = 920;
        auto h3 = GetBatteryHealth(mouseKey, dev);
        assert(h3.supported);
        assert(h3.healthPercent == 92);
        assert(h3.displayText == L"92%");
    }

    // 11. Charging history recording
    {
        ChargingSession sess;
        sess.deviceKey = mouseKey;
        sess.deviceName = mouseName;
        sess.startTime = now - 3600;
        sess.endTime = now;
        sess.startPercent = 20;
        sess.endPercent = 100;
        sess.durationMinutes = 60;
        sess.completed = true;

        RecordChargingSession(sess);
        auto history = GetChargingHistory(mouseKey);
        assert(!history.empty());
        assert(history[0].deviceKey == mouseKey);
        assert(history[0].startPercent == 20);
        assert(history[0].endPercent == 100);
        assert(history[0].completed == true);
        ClearChargingHistory(mouseKey);
        assert(GetChargingHistory(mouseKey).empty());
    }

    // 12. Battery consumption comparison
    {
        DeviceStatus m1;
        m1.key = L"mouse:1";
        m1.name = L"Mouse A";
        m1.kind = DeviceKind::Mouse;
        m1.percent = 80;

        DeviceStatus m2;
        m2.key = L"mouse:2";
        m2.name = L"Mouse B";
        m2.kind = DeviceKind::Mouse;
        m2.percent = 70;

        DeviceStatus h1;
        h1.key = L"headset:1";
        h1.name = L"Headset A";
        h1.kind = DeviceKind::Headset;
        h1.percent = 60;

        battery_history::Store store;
        for (int i = 6; i >= 0; i -= 2) {
            store.Record({ m1.key, now - i * 3600, 90 - (6 - i) * 2, false }); // 2%/hr = 48%/day
            store.Record({ m2.key, now - i * 3600, 90 - (6 - i) * 4, false }); // 4%/hr = 96%/day
            store.Record({ h1.key, now - i * 3600, 90 - (6 - i) * 5, false }); // 5%/hr = 120%/day
        }

        std::vector<DeviceStatus> all = { m1, m2, h1 };
        auto compMice = CompareConsumption(all, store, now, DeviceKind::Mouse);
        assert(compMice.size() == 2);
        assert(compMice[0].key == m1.key); // Mouse A is more efficient (2%/hr vs 4%/hr)
        assert(compMice[0].percentPerHour < compMice[1].percentPerHour);

        auto compAll = CompareConsumption(all, store, now, DeviceKind::Other);
        assert(compAll.size() == 3);
    }

    std::puts("Battery Intelligence unit tests passed");
    return 0;
}
