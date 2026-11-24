#include "../src/battery_health.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace battery_history;

static void TestSerializationAndValidation() {
    const int64_t now = 1795000000LL;
    const std::wstring key = L"Mysz:\u0105\U0001F3A7";

    DailySummary d{key, now / kSecondsPerDay, 18, 97, 14, true};
    std::string dEnc = EncodeDaily(d);
    assert(!dEnc.empty() && dEnc.back() == '\n');
    DailySummary dDec;
    assert(DecodeDaily(dEnc.substr(0, dEnc.size() - 1), dDec));
    assert(dDec.key == key && dDec.utcDay == d.utcDay && dDec.minPercent == 18 &&
           dDec.maxPercent == 97 && dDec.sampleCount == 14 && dDec.sawCharging);

    DischargeCycle c{key, now - 3600, 38.45};
    std::string cEnc = EncodeCycle(c);
    assert(!cEnc.empty() && cEnc.back() == '\n');
    DischargeCycle cDec;
    assert(DecodeCycle(cEnc.substr(0, cEnc.size() - 1), cDec));
    assert(cDec.key == key && cDec.completedAt == c.completedAt &&
           std::abs(cDec.normalizedHours - 38.45) < 0.011);

    ActiveDischarge a{key, now - 7200, 98, now - 1800, 62, true};
    std::string aEnc = EncodeActive(a);
    assert(!aEnc.empty() && aEnc.back() == '\n');
    ActiveDischarge aDec;
    assert(DecodeActive(aEnc.substr(0, aEnc.size() - 1), aDec));
    assert(aDec.active && aDec.key == key && aDec.startTime == a.startTime &&
           aDec.startPercent == 98 && aDec.lastTime == a.lastTime && aDec.lastPercent == 62);

    // Reject malformed rows across all record types.
    assert(!DecodeDaily("X\t00000041\t20775\t10\t90\t1\t0", dDec));
    assert(!DecodeDaily("D\tevil\t20775\t10\t90\t1\t0", dDec));
    assert(!DecodeDaily("D\t00000041\t20775\t95\t90\t1\t0", dDec));      // min > max
    assert(!DecodeDaily("D\t00000041\t20775\t10\t101\t1\t0", dDec));     // max > 100
    assert(!DecodeDaily("D\t00000041\t20775\t10\t90\t0\t0", dDec));       // sampleCount == 0
    assert(!DecodeDaily("D\t00000041\t20775\t10\t90\t1\t2", dDec));       // charging > 1
    assert(!DecodeDaily("D\t00000041\t20775\t10\t90\t1", dDec));          // missing field
    assert(!DecodeDaily("D\t00000041\t20775\t10\t90\t1\t0\t9", dDec));    // extra field
    assert(!DecodeDaily("D\t00000041\t100\t10\t90\t1\t0", dDec));         // day before 2000
    assert(!DecodeCycle("C\t00000041\t1795000000\t50", cDec));            // < 1.0 h
    assert(!DecodeCycle("C\t00000041\t1795000000\t999999", cDec));        // > max hours
    assert(!DecodeCycle("C\t00000000\t1795000000\t3800", cDec));          // null wchar
    assert(!DecodeActive("A\t00000041\t1795000000\t90\t1795003600\t50", aDec)); // start < 95
    assert(!DecodeActive("A\t00000041\t1795000000\t96\t1794999999\t50", aDec)); // last < start
    assert(!DecodeActive("A\t00000041\t1795000000\t96\t1795003600\t20", aDec)); // last <= 20

    HealthStore store;
    store.dailies.push_back(d);
    store.cycles.push_back(c);
    store.active.push_back(a);
    const std::string saved = store.Serialize();
    HealthStore loaded;
    loaded.Load(saved + "BROKEN\tROW\nC\tbad\t1\t2\nD\t00000041\t99999", now);
    assert(loaded.dailies.size() == 1 && loaded.cycles.size() == 1 && loaded.active.size() == 1);
    assert(loaded.dailies[0].key == key);
    assert(loaded.cycles[0].key == key);
    assert(loaded.active[0].key == key);

    // Duplicate daily rows merge cleanly, duplicate active keeps latest.
    DailySummary d2{key, d.utcDay, 12, 99, 3, false};
    ActiveDischarge a2{key, now - 3600, 99, now - 600, 45, true};
    loaded.Load(EncodeDaily(d) + EncodeDaily(d2) + EncodeActive(a) + EncodeActive(a2), now);
    assert(loaded.dailies.size() == 1);
    assert(loaded.dailies[0].minPercent == 12 && loaded.dailies[0].maxPercent == 99 &&
           loaded.dailies[0].sampleCount == 17 && loaded.dailies[0].sawCharging);
    assert(loaded.active.size() == 1 && loaded.active[0].startPercent == 99 && loaded.active[0].lastPercent == 45);
}

static void TestDailySummariesAndRetention() {
    const int64_t now = 1795000000LL;
    const std::wstring mouse = L"razer:mouse";
    const std::wstring kb = L"logitech:kb";
    HealthStore s;

    // 364 days ago survives, 366 days ago is pruned when evaluated at `now`.
    assert(s.Record({mouse, now - 366 * kSecondsPerDay, 80, false}));
    assert(s.Record({mouse, now - 364 * kSecondsPerDay, 60, false}));
    assert(s.dailies.size() == 2);
    s.Prune(now);
    assert(s.dailies.size() == 1);
    assert(s.dailies[0].utcDay == (now - 364 * kSecondsPerDay) / kSecondsPerDay);

    // Same day aggregates min, max, sampleCount, sawCharging.
    const int64_t dayStart = (now / kSecondsPerDay) * kSecondsPerDay;
    assert(s.Record({mouse, dayStart + 1000, 85, false}));
    assert(s.Record({mouse, dayStart + 4000, 40, false}));
    assert(s.Record({mouse, dayStart + 7000, 75, true}));
    assert(s.Record({kb, dayStart + 7000, 92, false}));
    assert(s.dailies.size() == 3);
    const DailySummary* md = nullptr;
    for (const auto& d : s.dailies) {
        if (d.key == mouse && d.utcDay == dayStart / kSecondsPerDay) md = &d;
    }
    assert(md && md->minPercent == 40 && md->maxPercent == 85 && md->sampleCount == 3 && md->sawCharging);

    // Prune removes expired cycles and stale active discharges (>7 days old).
    s.cycles.push_back({mouse, now - kHealthRetentionSeconds - 10, 35.0});
    s.cycles.push_back({mouse, now - kHealthRetentionSeconds + 100, 36.0});
    s.active.push_back({kb, now - 8 * kSecondsPerDay, 98, now - kMaxDischargeGapSeconds - 1, 55, true});
    s.Prune(now);
    assert(s.cycles.size() == 1 && std::abs(s.cycles[0].normalizedHours - 36.0) < 0.001);
    assert(s.active.empty());

    // Caps are respected.
    HealthStore capped;
    for (size_t i = 0; i < kMaxDailySummaries + 100; ++i) {
        capped.dailies.push_back({std::to_wstring(i), now / kSecondsPerDay, 50, 80, 1, false});
    }
    for (size_t i = 0; i < kMaxCycles + 50; ++i) {
        capped.cycles.push_back({std::to_wstring(i), now - 100, 30.0});
    }
    capped.Prune(now);
    assert(capped.dailies.size() == kMaxDailySummaries);
    assert(capped.cycles.size() == kMaxCycles);
}

static void TestCycleDetectionStateMachine() {
    const int64_t t0 = 1795000000LL;
    const std::wstring dev = L"pulsar:mouse";
    const std::wstring other = L"keychron:kb";

    // 1. Clean cycle with topped-up idle hold at 98%->100%, then 32h discharge from 100% to 20% -> 40.0h.
    {
        HealthStore s;
        assert(s.Record({dev, t0, 98, false}));
        assert(s.Record({dev, t0 + 5 * 3600, 100, false}));  // refreshes startTime to t0 + 5h
        assert(s.Record({dev, t0 + 10 * 3600, 100, false})); // refreshes startTime to t0 + 10h
        assert(s.Record({dev, t0 + 26 * 3600, 60, false}));
        assert(s.Record({dev, t0 + 42 * 3600, 20, false}));  // 32h for 80% -> 40.0h
        assert(s.cycles.size() == 1);
        assert(std::abs(s.cycles[0].normalizedHours - 40.0) < 0.001);
        assert(s.active.empty());

        // Further decline 20% -> 15% -> 10% must not double-count.
        assert(s.Record({dev, t0 + 44 * 3600, 15, false}));
        assert(s.Record({dev, t0 + 46 * 3600, 10, false}));
        assert(s.cycles.size() == 1 && s.active.empty());
    }

    // 2. Charging interruption aborts active cycle; charging at >=95% does not start until unplugged.
    {
        HealthStore s;
        assert(s.Record({dev, t0, 98, false}));
        assert(s.Record({dev, t0 + 10 * 3600, 65, false}));
        assert(s.Record({dev, t0 + 11 * 3600, 75, true}));  // plugged in -> abort
        assert(s.active.empty());
        assert(s.Record({dev, t0 + 12 * 3600, 99, true}));  // still charging -> do not start
        assert(s.active.empty());
        assert(s.Record({dev, t0 + 13 * 3600, 95, false})); // unplugged at 95% -> starts
        assert(s.active.size() == 1);
        assert(s.Record({dev, t0 + 35 * 3600 + 1800, 20, false})); // 22.5h for 75% -> 30.0h
        assert(s.cycles.size() == 1);
        assert(std::abs(s.cycles[0].normalizedHours - 30.0) < 0.001);
    }

    // 3. Rebound <= 3% keeps cycle alive; rebound > 3% aborts (and restarts if >= 95%).
    {
        HealthStore s;
        assert(s.Record({dev, t0, 96, false}));
        assert(s.Record({dev, t0 + 12 * 3600, 50, false}));
        assert(s.Record({dev, t0 + 13 * 3600, 53, false})); // +3% allowed
        assert(s.active.size() == 1);
        assert(s.Record({dev, t0 + 32 * 3600, 16, false})); // 32h for 80% -> 40.0h
        assert(s.cycles.size() == 1);
        assert(std::abs(s.cycles[0].normalizedHours - 40.0) < 0.001);

        // Unrecorded mid-cycle recharge (+4% from 50% to 54%) aborts cycle.
        assert(s.Record({dev, t0 + 40 * 3600, 96, false}));
        assert(s.Record({dev, t0 + 50 * 3600, 50, false}));
        assert(s.Record({dev, t0 + 51 * 3600, 54, false})); // +4% -> abort
        assert(s.active.empty());
        assert(s.Record({dev, t0 + 60 * 3600, 15, false}));
        assert(s.cycles.size() == 1);

        // Unrecorded full recharge (+30% directly to 98% with charging=false) restarts cleanly.
        assert(s.Record({dev, t0 + 70 * 3600, 96, false}));
        assert(s.Record({dev, t0 + 80 * 3600, 68, false}));
        assert(s.Record({dev, t0 + 82 * 3600, 98, false})); // aborts old, starts new at 98%
        assert(s.active.size() == 1 && s.active[0].startPercent == 98 && s.active[0].startTime == t0 + 82 * 3600);
        assert(s.Record({dev, t0 + 106 * 3600, 18, false})); // 24h for 80% -> 30.0h
        assert(s.cycles.size() == 2);
        assert(std::abs(s.cycles[1].normalizedHours - 30.0) < 0.001);
    }

    // 4. Sleep / offline gap <= 7 days stays alive; > 7 days aborts.
    {
        HealthStore s;
        assert(s.Record({dev, t0, 100, false}));
        assert(s.Record({dev, t0 + 5 * kSecondsPerDay, 50, false})); // 5-day gap allowed
        assert(s.active.size() == 1);
        assert(s.Record({dev, t0 + 6 * kSecondsPerDay, 20, false})); // 144h for 80% -> 180.0h
        assert(s.cycles.size() == 1);
        assert(std::abs(s.cycles[0].normalizedHours - 180.0) < 0.001);

        assert(s.Record({dev, t0 + 7 * kSecondsPerDay, 100, false}));
        assert(s.Record({dev, t0 + 14 * kSecondsPerDay + 2, 50, false})); // > 7-day gap -> abort
        assert(s.active.empty());
        assert(s.Record({dev, t0 + 15 * kSecondsPerDay, 10, false}));
        assert(s.cycles.size() == 1);
    }

    // 5. Out-of-order timestamps, sub-1h bogus drops, and multi-device isolation + Clear().
    {
        HealthStore s;
        assert(s.Record({dev, t0, 100, false}));
        assert(s.Record({other, t0, 96, false}));
        assert(s.Record({dev, t0 + 1800, 20, false})); // 0.5h < 1.0h -> rejected, active cleared
        assert(s.cycles.empty());
        assert(s.active.size() == 1 && s.active[0].key == other);

        // Out-of-order timestamp aborts dev's active cycle without touching other.
        assert(s.Record({dev, t0 + 5000, 98, false}));
        assert(s.Record({dev, t0 + 4000, 70, false})); // non-monotonic -> cleared
        assert(s.active.size() == 1 && s.active[0].key == other);

        assert(s.Record({other, t0 + 38 * 3600, 20, false})); // 38h for 76% -> 50.0h
        assert(s.cycles.size() == 1 && s.cycles[0].key == other);

        s.Clear(dev);
        assert(s.cycles.size() == 1 && s.cycles[0].key == other);
        for (const auto& d : s.dailies) assert(d.key == other);
    }
}

static void TestSeedAndWearEvaluation() {
    const int64_t now = 1795000000LL;
    const std::wstring key = L"logitech:gpx";
    const std::wstring other = L"razer:vv3";

    // Migration from unsorted 30-day samples (also prunes stale >7d active discharges).
    {
        std::vector<Sample> raw = {
            {key, now - 10 * 3600, 20, false},
            {key, now - 42 * 3600, 100, false},
            {other, now - 3 * 3600, 97, false},
            {L"stale:dev", now - 8 * kSecondsPerDay, 99, false},
            {key, now - 26 * 3600, 60, false},
        };
        HealthStore migrated;
        migrated.SeedFromHistory(raw, now);
        assert(migrated.cycles.size() == 1);
        assert(std::abs(migrated.cycles[0].normalizedHours - 40.0) < 0.001);
        assert(migrated.active.size() == 1 && migrated.active[0].key == other);
        assert(!migrated.Record({L"", now, 50, false}));
        assert(!migrated.Record({key, now, 101, false}));
    }

    // Fewer than 3 cycles -> Insufficient.
    {
        HealthStore s;
        assert(EvaluateWear(s, key).state == WearState::Insufficient);
        s.cycles.push_back({key, now - 100 * kSecondsPerDay, 38.0});
        s.cycles.push_back({key, now - 95 * kSecondsPerDay, 38.0});
        s.cycles.push_back({other, now - 10 * kSecondsPerDay, 20.0});
        auto w = EvaluateWear(s, key);
        assert(w.state == WearState::Insufficient && w.cycleCount == 2);
    }

    // Multi-month comparison: 3 early (~38h) 4 months ago vs 3 recent (~29h) -> -24%.
    {
        HealthStore s;
        s.cycles = {
            {key, now - 122 * kSecondsPerDay, 38.2},
            {key, now - 117 * kSecondsPerDay, 37.9},
            {key, now - 112 * kSecondsPerDay, 38.0},
            {key, now - 16 * kSecondsPerDay, 29.1},
            {key, now - 10 * kSecondsPerDay, 28.9},
            {key, now - 4 * kSecondsPerDay, 29.0},
            {other, now - 1 * kSecondsPerDay, 12.0},
        };
        auto w = EvaluateWear(s, key);
        assert(w.state == WearState::Ready && w.cycleCount == 6);
        assert(w.earlyHours == 38 && w.recentHours == 29 && w.changePercent == -24);
        assert(FormatChangePercent(w.changePercent) == L"\u221224%");
    }

    // Many month-1 cycles + single later cycle: recent prefers post-first-month cycle(s).
    {
        HealthStore s;
        for (int i = 0; i < 6; ++i) s.cycles.push_back({key, now - (100 - i) * kSecondsPerDay, 40.0});
        s.cycles.push_back({key, now - 5 * kSecondsPerDay, 30.0});
        auto w = EvaluateWear(s, key);
        assert(w.state == WearState::Ready && w.earlyHours == 40 && w.recentHours == 30 && w.changePercent == -25);
    }

    // All cycles within 30 days -> split earliest n vs latest n cleanly (for n=1 on 3 cycles and n=2 on 6 cycles).
    {
        HealthStore s3;
        s3.cycles = {
            {key, now - 20 * kSecondsPerDay, 40.0},
            {key, now - 10 * kSecondsPerDay, 36.0},
            {key, now - 1 * kSecondsPerDay, 30.0},
        };
        auto w3 = EvaluateWear(s3, key);
        assert(w3.state == WearState::Ready && w3.earlyHours == 40 && w3.recentHours == 30 && w3.changePercent == -25);

        HealthStore s;
        s.cycles = {
            {key, now - 25 * kSecondsPerDay, 40.0},
            {key, now - 20 * kSecondsPerDay, 40.0},
            {key, now - 15 * kSecondsPerDay, 36.0},
            {key, now - 10 * kSecondsPerDay, 35.0},
            {key, now - 5 * kSecondsPerDay, 32.0},
            {key, now - 1 * kSecondsPerDay, 32.0},
        };
        auto w = EvaluateWear(s, key);
        assert(w.state == WearState::Ready && w.earlyHours == 40 && w.recentHours == 32 && w.changePercent == -20);
    }

    assert(FormatChangePercent(-24) == L"\u221224%");
    assert(FormatChangePercent(5) == L"+5%");
    assert(FormatChangePercent(0) == L"0%");
}

int main() {
    TestSerializationAndValidation();
    TestDailySummariesAndRetention();
    TestCycleDetectionStateMachine();
    TestSeedAndWearEvaluation();
    puts("Battery health tests passed");
    return 0;
}
