// Standalone portable policy tests; no real wireless device required.
#include "../src/alert_rules.h"

#include <cassert>
#include <cstdio>
#include <map>

int main() {
    using namespace alerts;
    std::map<std::wstring, int> prefs;
    const std::wstring mouse = L"mouse:key";
    const std::wstring headset = L"headset:key";
    assert(EffectiveThreshold(prefs, mouse) == 15);
    assert(OverrideFor(prefs, mouse) == kUseDefault);
    prefs[mouse] = 20;
    prefs[headset] = kDisabled;
    assert(EffectiveThreshold(prefs, mouse) == 20);
    assert(EffectiveThreshold(prefs, headset) == kDisabled);
    assert(EffectiveThreshold(prefs, L"other:key") == 15);
    assert(ValidOverride(kDisabled));
    for (int n : kThresholds) assert(ValidOverride(n));
    assert(!ValidOverride(0) && !ValidOverride(51) && !ValidOverride(kUseDefault));

    DeviceStatus s;
    s.key = mouse;
    s.percent = 21;
    bool warned = false;
    assert(!ShouldNotify(s, 20, warned));
    s.percent = 20;
    assert(ShouldNotify(s, 20, warned));
    assert(warned);
    assert(!ShouldNotify(s, 20, warned));  // same reading must not notify again
    s.percent = 25;
    assert(!ShouldNotify(s, 20, warned));  // hysteresis prevents flapping
    s.percent = -1;
    assert(!ShouldNotify(s, 20, warned));
    assert(warned);                        // unknown reading preserves latch
    s.percent = 10;
    assert(!ShouldNotify(s, 20, warned));  // returning from unknown is not a new alert
    s.asleep = true;
    assert(!ShouldNotify(s, 20, warned));
    s.asleep = false;
    s.connected = false;
    assert(!ShouldNotify(s, 20, warned));
    s.connected = true;
    assert(!ShouldNotify(s, 20, warned));
    s.percent = 31;
    assert(!ShouldNotify(s, 20, warned));  // armed again only > threshold + 10
    assert(!warned);
    s.percent = 19;
    assert(ShouldNotify(s, 20, warned));

    s.charging = true;
    assert(!ShouldNotify(s, 20, warned));
    assert(!warned);
    s.charging = false;
    assert(ShouldNotify(s, 20, warned));

    assert(!ShouldNotify(s, kDisabled, warned));
    assert(!warned);
    assert(ShouldNotify(s, 20, warned));   // re-enabled: new low battery alert
    s.percent = 105;
    warned = false;
    assert(!ShouldNotify(s, 20, warned));  // invalid data never alerts
    s.percent = 0;
    assert(ShouldNotify(s, 20, warned));   // 0% is a valid critical reading
    DeviceStatus headphones;
    headphones.key = headset;
    headphones.percent = 9;
    bool secondWarned = false;
    assert(ShouldNotify(headphones, 15, secondWarned));
    assert(!ShouldNotify(headphones, 15, secondWarned));
    assert(warned);                        // the mouse and headphones have independent latches

    // charge limit ("unplug at 80%")
    {
        DeviceStatus c;
        c.key = L"logi:mouse";
        c.charging = true;
        bool limitWarned = false;
        c.percent = 81;
        assert(ShouldNotifyChargeLimit(c, 80, 78, limitWarned) && limitWarned);   // crossed while seen
        c.percent = 83;
        assert(!ShouldNotifyChargeLimit(c, 80, 81, limitWarned));                 // once per charge
        c.charging = false;
        assert(!ShouldNotifyChargeLimit(c, 80, 83, limitWarned) && !limitWarned); // unplugged: re-armed
        c.charging = true;
        c.percent = 90;
        assert(!ShouldNotifyChargeLimit(c, 80, -1, limitWarned));                 // no earlier reading: silent
        assert(!ShouldNotifyChargeLimit(c, 80, 85, limitWarned));                 // already above when seen
        c.percent = 100;
        assert(!ShouldNotifyChargeLimit(c, 80, 75, limitWarned));                 // straight to full
        c.percent = 86;
        assert(!ShouldNotifyChargeLimit(c, 0, 84, limitWarned));                  // off
        assert(ShouldNotifyChargeLimit(c, 85, 84, limitWarned));                  // other limit
        c.asleep = true;
        limitWarned = false;
        assert(!ShouldNotifyChargeLimit(c, 85, 84, limitWarned));                 // stale reading
    }

    {   // Tiered low-battery alerts (PR #3946)
        DeviceStatus d;
        LowLatch l;
        auto at = [&](int pct, int threshold = 20, bool tiers = true) { d.percent = pct; return EvaluateLow(d, threshold, l, tiers); };
        assert(at(50) == LowAlert::None);
        assert(at(20) == LowAlert::Low);
        assert(at(19) == LowAlert::None);
        assert(at(10) == LowAlert::Critical);
        assert(at(9) == LowAlert::None);
        assert(at(5) == LowAlert::Emergency);
        assert(at(4) == LowAlert::None);
        assert(at(31) == LowAlert::None);   // recovered past threshold + 10: re-armed
        assert(at(3) == LowAlert::Emergency);  // a drop straight to 3% says the most urgent thing once
        assert(at(10) == LowAlert::None && at(20) == LowAlert::None);
        l = {};
        assert(at(10, 10) == LowAlert::Low);   // threshold 10: no separate critical step
        assert(at(5, 10) == LowAlert::Emergency);
        l = {};
        assert(at(5, 20, false) == LowAlert::Low && at(4, 20, false) == LowAlert::None);  // tiers off
        l = {};
        d.charging = true;
        assert(at(5) == LowAlert::None);
        d.charging = false;
        d.asleep = true;
        assert(at(5) == LowAlert::None);
        d.asleep = false;
        assert(at(5, kDisabled) == LowAlert::None);
    }
    {   // "Fully charged" (PR #3946)
        DeviceStatus d;
        d.charging = true;
        bool warned = false;
        d.percent = 100;
        assert(!ShouldNotifyCharged(d, -1, warned) && warned);   // already full when first seen
        d.charging = false; d.percent = 99;
        assert(!ShouldNotifyCharged(d, 100, warned) && !warned);  // unplugged: re-armed
        d.charging = true; d.percent = 98;
        assert(!ShouldNotifyCharged(d, 99, warned));
        d.percent = 100;
        assert(ShouldNotifyCharged(d, 98, warned));
        assert(!ShouldNotifyCharged(d, 100, warned));             // once per charge
        d.charging = false; d.percent = 95;
        ShouldNotifyCharged(d, 100, warned);
        d.charging = true; d.full = true; d.percent = 96;
        assert(ShouldNotifyCharged(d, 95, warned));               // the device's own full flag counts
        d.full = false; d.charging = false; d.percent = 90;
        ShouldNotifyCharged(d, 96, warned);
        d.charging = true; d.percent = 100; d.asleep = true;
        assert(!ShouldNotifyCharged(d, 99, warned));              // stale reading
    }

    // Alert sound policy (Task 8)
    {
        assert(ClampAlertSound(0) == AlertSound::Off);
        assert(ClampAlertSound(1) == AlertSound::SystemDefault);
        assert(ClampAlertSound(-1) == AlertSound::Off);
        assert(ClampAlertSound(2) == AlertSound::Off);
        assert(!ShouldPlayAlertSound(AlertSound::Off));
        assert(ShouldPlayAlertSound(AlertSound::SystemDefault, AlertKind::BatteryLow));
        assert(ShouldPlayAlertSound(AlertSound::SystemDefault, AlertKind::BatteryCharged));
        assert(ShouldPlayAlertSound(AlertSound::SystemDefault, AlertKind::BatteryChargeLimit));
        assert(ShouldPlayAlertSound(AlertSound::SystemDefault, AlertKind::ShortRuntime));
        assert(!ShouldPlayAlertSound(AlertSound::SystemDefault, AlertKind::Updater));
    }

    std::puts("Smart Battery Alerts policy tests passed");
}
