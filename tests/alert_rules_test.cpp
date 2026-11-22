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

    std::puts("Smart Battery Alerts policy tests passed");
}
