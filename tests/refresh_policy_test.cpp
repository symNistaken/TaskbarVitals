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

    std::puts("Refresh policy tests passed (startup schedule, event wakes, resume, arrival)");
}
