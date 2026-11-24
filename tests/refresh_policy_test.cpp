// Standalone portable tests for the startup / resume / arrival poll schedule.
#include "../src/refresh_policy.h"

#include <cassert>
#include <cstdio>

namespace {

DeviceStatus Dev(const wchar_t* key, int percent, bool connected = true, bool asleep = false) {
    DeviceStatus s;
    s.key = key;
    s.name = key;
    s.percent = percent;
    s.connected = connected;
    s.asleep = asleep;
    return s;
}

using Results = std::vector<DeviceStatus>;
constexpr unsigned long kPoll = 60000;

}  // namespace

int main() {
    using namespace refresh_policy;

    // Startup keeps the whole schedule even when the first poll looks complete: a device whose link is not
    // up yet is often missing from the results.
    {
        Monitor m;
        m.BeginStartup();
        Results ok = { Dev(L"mouse:1", 82) };
        for (unsigned long d : kStartupDelaysMs) {
            assert(m.DelayAfterPoll(ok, kPoll) == d);
            m.OnWaitFinished(true);
        }
        assert(m.DelayAfterPoll(ok, kPoll) == kPoll);
        assert(m.Current() == Mode::Idle);
        assert(m.DelayAfterPoll(ok, kPoll) == kPoll);
    }

    // Device events during startup refresh at once without using up the schedule, but stay bounded.
    {
        Monitor m;
        m.BeginStartup();
        Results none;
        assert(m.DelayAfterPoll(none, kPoll) == kStartupDelaysMs[0]);
        m.OnWaitFinished(false);
        assert(m.DelayAfterPoll(none, kPoll) == kStartupDelaysMs[0]);
        m.OnWaitFinished(true);
        assert(m.DelayAfterPoll(none, kPoll) == kStartupDelaysMs[1]);
        for (unsigned i = 0; i < kMaxEventWakesPerStep; i++) m.OnWaitFinished(false);
        assert(m.DelayAfterPoll(none, kPoll) == kStartupDelaysMs[2]);
        for (unsigned i = 0; i < kMaxEventWakesPerStep; i++) m.OnWaitFinished(false);
        assert(m.DelayAfterPoll(none, kPoll) == kPoll);
        assert(m.Current() == Mode::Idle);
    }

    // An arrival does not cut a startup check short.
    {
        Monitor m;
        m.BeginStartup();
        m.BeginArrival({});
        assert(m.Current() == Mode::Startup);
    }

    // Resume: wait until every device that was awake before is back, then stop.
    {
        Monitor m;
        m.BeginResume({ L"mouse:1", L"bt:headset" });
        Results partial = { Dev(L"mouse:1", 90) };
        assert(m.DelayAfterPoll(partial, kPoll) == kFollowUpDelaysMs[0]);
        m.OnWaitFinished(true);
        partial.push_back(Dev(L"bt:headset", 55));
        assert(m.DelayAfterPoll(partial, kPoll) == kPoll);
        assert(m.Current() == Mode::Idle);
    }

    // Resume: a device reported asleep is back; one that is connected but has no level yet is not.
    {
        Monitor m;
        m.BeginResume({ L"mouse:1" });
        Results pending = { Dev(L"mouse:1", -1) };
        assert(m.DelayAfterPoll(pending, kPoll) == kFollowUpDelaysMs[0]);
        m.OnWaitFinished(true);
        Results asleep = { Dev(L"mouse:1", -1, true, true) };
        assert(m.DelayAfterPoll(asleep, kPoll) == kPoll);
        assert(m.Current() == Mode::Idle);
    }

    // Resume with nothing awake before and nothing pending now: one poll is enough.
    {
        Monitor m;
        m.BeginResume({});
        Results offline = { Dev(L"dongle:1", -1, false) };
        assert(m.DelayAfterPoll(offline, kPoll) == kPoll);
        assert(m.Current() == Mode::Idle);
    }

    // Resume gives up once the schedule is used, e.g. a device was switched off during the sleep.
    {
        Monitor m;
        m.BeginResume({ L"mouse:1" });
        Results none;
        for (unsigned long d : kFollowUpDelaysMs) {
            assert(m.DelayAfterPoll(none, kPoll) == d);
            m.OnWaitFinished(true);
        }
        assert(m.DelayAfterPoll(none, kPoll) == kPoll);
        assert(m.Current() == Mode::Idle);
    }

    // Arrival: keep polling until a device that had no level before reports one.
    {
        Monitor m;
        m.BeginArrival({ L"mouse:1" });
        Results before = { Dev(L"mouse:1", 70) };
        assert(m.DelayAfterPoll(before, kPoll) == kFollowUpDelaysMs[0]);
        m.OnWaitFinished(true);
        Results linking = { Dev(L"mouse:1", 70), Dev(L"bt:headset", -1) };
        assert(m.DelayAfterPoll(linking, kPoll) == kFollowUpDelaysMs[1]);
        m.OnWaitFinished(true);
        linking[1].percent = 45;
        assert(m.DelayAfterPoll(linking, kPoll) == kPoll);
        assert(m.Current() == Mode::Idle);
    }

    // A new arrival restarts the arrival schedule with the devices ready at that point.
    {
        Monitor m;
        m.BeginArrival({});
        Results none;
        assert(m.DelayAfterPoll(none, kPoll) == kFollowUpDelaysMs[0]);
        m.OnWaitFinished(true);
        assert(m.DelayAfterPoll(none, kPoll) == kFollowUpDelaysMs[1]);
        m.BeginArrival({ L"mouse:1" });
        Results same = { Dev(L"mouse:1", 70) };
        assert(m.DelayAfterPoll(same, kPoll) == kFollowUpDelaysMs[0]);
    }

    // A resume replaces an arrival check.
    {
        Monitor m;
        m.BeginArrival({});
        m.BeginResume({ L"mouse:1" });
        assert(m.Current() == Mode::Resume);
    }

    // Never wait longer than the normal poll interval.
    {
        Monitor m;
        m.BeginStartup();
        Results none;
        assert(m.DelayAfterPoll(none, 2000) == 2000);
    }

    // Full-screen / busy notification state classification.
    {
        assert(!IsFullscreenOrBusy(UserNotificationState::Unknown));
        assert(!IsFullscreenOrBusy(UserNotificationState::NotPresent));
        assert(IsFullscreenOrBusy(UserNotificationState::Busy));
        assert(IsFullscreenOrBusy(UserNotificationState::RunningD3DFullScreen));
        assert(IsFullscreenOrBusy(UserNotificationState::PresentationMode));
        assert(!IsFullscreenOrBusy(UserNotificationState::AcceptsNotifications));
        assert(!IsFullscreenOrBusy(UserNotificationState::QuietTime));
        assert(!IsFullscreenOrBusy(UserNotificationState::App));
    }

    // EffectivePollIntervalMs: disabled keeps base interval across all states and durations.
    {
        const UserNotificationState allStates[] = {
            UserNotificationState::Unknown,
            UserNotificationState::NotPresent,
            UserNotificationState::Busy,
            UserNotificationState::RunningD3DFullScreen,
            UserNotificationState::PresentationMode,
            UserNotificationState::AcceptsNotifications,
            UserNotificationState::QuietTime,
            UserNotificationState::App,
        };
        const unsigned long intervals[] = { 30000, 60000, 300000, 600000, 1800000 };
        for (UserNotificationState st : allStates) {
            for (unsigned long ms : intervals)
                assert(EffectivePollIntervalMs(ms, false, st) == ms);
        }
    }

    // EffectivePollIntervalMs: enabled stretches short intervals during busy / fullscreen / presentation,
    // preserves longer intervals, and leaves non-fullscreen states untouched.
    {
        const UserNotificationState busyStates[] = {
            UserNotificationState::Busy,
            UserNotificationState::RunningD3DFullScreen,
            UserNotificationState::PresentationMode,
        };
        for (UserNotificationState st : busyStates) {
            assert(EffectivePollIntervalMs(30000, true, st) == kFullscreenMinPollIntervalMs);
            assert(EffectivePollIntervalMs(60000, true, st) == kFullscreenMinPollIntervalMs);
            assert(EffectivePollIntervalMs(300000, true, st) == 300000);
            assert(EffectivePollIntervalMs(600000, true, st) == 600000);
            assert(EffectivePollIntervalMs(1800000, true, st) == 1800000);
        }

        const UserNotificationState normalStates[] = {
            UserNotificationState::Unknown,
            UserNotificationState::NotPresent,
            UserNotificationState::AcceptsNotifications,
            UserNotificationState::QuietTime,
            UserNotificationState::App,
        };
        for (UserNotificationState st : normalStates) {
            assert(EffectivePollIntervalMs(30000, true, st) == 30000);
            assert(EffectivePollIntervalMs(60000, true, st) == 60000);
            assert(EffectivePollIntervalMs(600000, true, st) == 600000);
            assert(EffectivePollIntervalMs(1800000, true, st) == 1800000);
        }
    }

    // Active-foreground multi-monitor fullscreen detection & throttling.
    {
        const Rect mon1{ 0, 0, 1920, 1080 };
        const Rect work1{ 0, 0, 1920, 1032 };
        assert(CoversMonitor(mon1, mon1));
        assert(CoversMonitor(Rect{ -2, -2, 1922, 1082 }, mon1));
        assert(!CoversMonitor(work1, mon1));
        assert(!CoversMonitor(mon1, Rect{ 0, 0, 0, 0 }));

        // Side-monitor presentation / video (Busy / PresentationMode) without a full-screen foreground window
        // does not throttle polling while working normally on the other monitor.
        assert(!IsActiveFullscreen(false, UserNotificationState::Busy));
        assert(!IsActiveFullscreen(false, UserNotificationState::PresentationMode));
        assert(EffectivePollIntervalMs(60000, true, false, UserNotificationState::Busy) == 60000);
        assert(EffectivePollIntervalMs(60000, true, false, UserNotificationState::PresentationMode) == 60000);

        // An active full-screen foreground window or exclusive D3D full screen DOES throttle when enabled.
        assert(IsActiveFullscreen(true, UserNotificationState::AcceptsNotifications));
        assert(IsActiveFullscreen(false, UserNotificationState::RunningD3DFullScreen));
        assert(EffectivePollIntervalMs(60000, true, true, UserNotificationState::AcceptsNotifications) ==
               kFullscreenMinPollIntervalMs);
        assert(EffectivePollIntervalMs(60000, true, false, UserNotificationState::RunningD3DFullScreen) ==
               kFullscreenMinPollIntervalMs);
        assert(EffectivePollIntervalMs(60000, false, true, UserNotificationState::RunningD3DFullScreen) == 60000);
    }

    // Monitor still runs quick arrival follow-ups and falls back to stretched fullscreen interval when idle.
    {
        Monitor m;
        m.BeginArrival({});
        Results none;
        unsigned long effective = EffectivePollIntervalMs(60000, true, UserNotificationState::RunningD3DFullScreen);
        assert(effective == kFullscreenMinPollIntervalMs);
        assert(m.DelayAfterPoll(none, effective) == kFollowUpDelaysMs[0]);
        m.OnWaitFinished(true);
        Results arrived = { Dev(L"mouse:1", 80) };
        assert(m.DelayAfterPoll(arrived, effective) == kFullscreenMinPollIntervalMs);
        assert(m.Current() == Mode::Idle);
    }

    std::puts("Refresh policy tests passed (startup schedule, event wakes, resume, arrival, fullscreen interval)");
}
