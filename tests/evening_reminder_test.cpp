// Standalone portable tests for evening charge reminder policy and formatting.
#include "../src/evening_reminder.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>

int main() {
    using namespace evening_reminder;

    // Hour clamping: only 20..23 are active, everything else falls back to Off (0).
    {
        for (int h : kHours) assert(ClampReminderHour(h) == h);
        for (int bad : { -1, 0, 1, 3, 12, 19, 24, 999 }) assert(ClampReminderHour(bad) == 0);
    }

    // Civil date packing and PreviousDay across months, leap years, and year boundaries.
    {
        assert(PackDateKey(2026, 5, 15) == 20260515);
        assert(PackDateKey(2024, 2, 29) == 20240229);
        assert(PackDateKey(2026, 2, 29) == 0);
        assert(PackDateKey(2026, 13, 1) == 0);

        assert(PreviousDay(2026, 5, 15) == 20260514);
        assert(PreviousDay(2026, 5, 1) == 20260430);
        assert(PreviousDay(2026, 8, 1) == 20260731);
        assert(PreviousDay(2024, 3, 1) == 20240229);
        assert(PreviousDay(2026, 3, 1) == 20260228);
        assert(PreviousDay(2000, 3, 1) == 20000229);
        assert(PreviousDay(1900, 3, 1) == 19000228);
        assert(PreviousDay(2026, 1, 1) == 20251231);
        assert(PreviousDay(2026, 2, 29) == 0);
    }

    // ActiveCycleDate across daytime, evening window, and pre-03:00 wake catch-up.
    {
        assert(ActiveCycleDate({ 2026, 3, 1, 22 }, 0) == 0);
        assert(ActiveCycleDate({ 2026, 3, 1, 22 }, 19) == 0);
        assert(ActiveCycleDate({ 2026, 3, 1, 21 }, 22) == 0);
        assert(ActiveCycleDate({ 2026, 3, 1, 22 }, 22) == 20260301);
        assert(ActiveCycleDate({ 2026, 3, 1, 23 }, 22) == 20260301);
        assert(ActiveCycleDate({ 2026, 3, 1, 0 }, 22) == 20260228);
        assert(ActiveCycleDate({ 2026, 3, 1, 2 }, 22) == 20260228);
        assert(ActiveCycleDate({ 2026, 3, 1, 3 }, 22) == 0);
        assert(ActiveCycleDate({ 2024, 3, 1, 1 }, 22) == 20240229);
        assert(ActiveCycleDate({ 2026, 1, 1, 1 }, 20) == 20251231);
        assert(ActiveCycleDate({ 2026, 5, 1, 20 }, 20) == 20260501);
        assert(ActiveCycleDate({ 2026, 5, 1, 22 }, 23) == 0);
        assert(ActiveCycleDate({ 2026, 5, 1, 23 }, 23) == 20260501);
    }

    // NeedsChargeTonight filtering rules.
    {
        DeviceCandidate low{ L"Mouse", 29, true, false, false, false, false, 0.0 };
        assert(NeedsChargeTonight(low));

        DeviceCandidate atThreshold{ L"Mouse", 30, true, false, false, false, false, 0.0 };
        assert(!NeedsChargeTonight(atThreshold));

        DeviceCandidate zeroPct{ L"Mouse", 0, true, false, false, false, false, 0.0 };
        assert(NeedsChargeTonight(zeroPct));

        DeviceCandidate shortEta{ L"Headset", 55, true, false, false, false, true, 24.0 };
        assert(NeedsChargeTonight(shortEta));

        DeviceCandidate longEta{ L"Headset", 55, true, false, false, false, true, 24.1 };
        assert(!NeedsChargeTonight(longEta));

        DeviceCandidate badEta{ L"Headset", 55, true, false, false, false, true,
                               std::numeric_limits<double>::quiet_NaN() };
        assert(!NeedsChargeTonight(badEta));

        DeviceCandidate negEta{ L"Headset", 55, true, false, false, false, true, -1.0 };
        assert(!NeedsChargeTonight(negEta));

        DeviceCandidate disabledEta{ L"Headset", 55, true, false, false, false, false, 2.0 };
        assert(!NeedsChargeTonight(disabledEta));

        DeviceCandidate charging{ L"Mouse", 10, true, false, true, false, true, 1.0 };
        assert(!NeedsChargeTonight(charging));

        DeviceCandidate asleep{ L"Mouse", 10, true, true, false, false, true, 1.0 };
        assert(!NeedsChargeTonight(asleep));

        DeviceCandidate disconnected{ L"Mouse", 10, false, false, false, false, true, 1.0 };
        assert(!NeedsChargeTonight(disconnected));

        DeviceCandidate hidden{ L"Mouse", 10, true, false, false, true, true, 1.0 };
        assert(!NeedsChargeTonight(hidden));

        DeviceCandidate invalidPct{ L"Mouse", -1, true, false, false, false, true, 1.0 };
        assert(!NeedsChargeTonight(invalidPct));
    }

    // Message formatting: matches spec example and handles rounding and placeholder safety.
    {
        assert(FormatReminderMessage({}).empty());

        const std::vector<DeviceCandidate> list = {
            { L"Mouse", 22, true, false, false, false, true, 8.8 },
            { L"Headset", 31, true, false, false, false, false, 0.0 },
        };
        assert(FormatReminderMessage(list) ==
               L"Charge tonight: Mouse 22% (about 9 h left), Headset 31%.");

        // Sub-hour ETA rounds up to at least 1 h; >24h ETA is omitted even when <30%.
        const std::vector<DeviceCandidate> edgeList = {
            { L"Buds", 12, true, false, false, false, true, 0.2 },
            { L"Keyboard", 25, true, false, false, false, true, 30.0 },
        };
        assert(FormatReminderMessage(edgeList) ==
               L"Charge tonight: Buds 12% (about 1 h left), Keyboard 25%.");

        // Single-pass substitution prevents device names with "{percent}" from double-expanding.
        const std::vector<DeviceCandidate> literalTag = {
            { L"Pad {percent}", 18, true, false, false, false, false, 0.0 },
        };
        assert(FormatReminderMessage(literalTag) == L"Charge tonight: Pad {percent} 18%.");
    }

    // End-to-end Evaluate decisions.
    {
        const std::vector<DeviceCandidate> devices = {
            { L"Mouse", 22, true, false, false, false, true, 9.0 },
            { L"Headset", 31, true, false, false, false, true, 18.0 },
            { L"ChargingPad", 15, true, false, true, false, true, 2.0 },
            { L"SleepingKey", 10, true, true, false, false, false, 0.0 },
            { L"FullMouse", 80, true, false, false, false, true, 48.0 },
        };

        // Disabled -> silent.
        assert(!Evaluate({ 2026, 3, 1, 22 }, 0, 0, devices).notify);

        // Before reminder hour -> silent.
        assert(!Evaluate({ 2026, 3, 1, 21 }, 22, 0, devices).notify);

        // Evening trigger -> notifies once with expected cycle date and text.
        const ReminderDecision d1 = Evaluate({ 2026, 3, 1, 22 }, 22, 0, devices);
        assert(d1.notify);
        assert(d1.cycleDate == 20260301);
        assert(d1.message == L"Charge tonight: Mouse 22% (about 9 h left), Headset 31% (about 18 h left).");

        // Duplicate suppression later the same evening and after midnight (< 03:00).
        assert(!Evaluate({ 2026, 3, 1, 23 }, 22, d1.cycleDate, devices).notify);
        assert(!Evaluate({ 2026, 3, 2, 1 }, 22, d1.cycleDate, devices).notify);

        // PC slept across 22:00 on 2024-02-29 (leap day) and woke at 01:30 on 2024-03-01 -> catch-up once.
        const ReminderDecision catchUp = Evaluate({ 2024, 3, 1, 1 }, 22, 20240228, devices);
        assert(catchUp.notify);
        assert(catchUp.cycleDate == 20240229);
        assert(!Evaluate({ 2024, 3, 1, 2 }, 22, catchUp.cycleDate, devices).notify);

        // Next evening (2024-03-01 22:00) still notifies since lastCycleDate was 20240229.
        const ReminderDecision nextEvening = Evaluate({ 2024, 3, 1, 22 }, 22, catchUp.cycleDate, devices);
        assert(nextEvening.notify);
        assert(nextEvening.cycleDate == 20240301);

        // Waking at 03:00 or later misses the catch-up window.
        assert(!Evaluate({ 2024, 3, 2, 3 }, 22, 20240229, devices).notify);

        // No qualifying devices -> silent.
        const std::vector<DeviceCandidate> none = {
            { L"ChargingPad", 15, true, false, true, false, false, 0.0 },
            { L"SleepingKey", 10, true, true, false, false, false, 0.0 },
            { L"OfflineMouse", 5, false, false, false, false, false, 0.0 },
            { L"HiddenMouse", 5, true, false, false, true, false, 0.0 },
            { L"HealthyMouse", 75, true, false, false, false, true, 40.0 },
        };
        assert(!Evaluate({ 2026, 3, 1, 22 }, 22, 0, none).notify);
    }

    std::puts("Evening charge reminder policy tests passed");
    return 0;
}
