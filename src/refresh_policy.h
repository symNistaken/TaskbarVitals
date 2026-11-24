#pragma once
// When to poll again after sign-in, a resume or a device arrival, while battery levels are often not
// readable yet. Plain C++ so it can be tested without Windows.
#include "devices.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace refresh_policy {

// Sign-in: a receiver can enumerate before its wireless link is up, and many drivers leave a device that
// does not answer out of the results, so there is nothing to wait for. Always poll at these times.
constexpr unsigned long kStartupDelaysMs[] = { 3000, 7000, 15000 };
// Resume and arrivals: poll again after these delays until the devices waited for report a level.
constexpr unsigned long kFollowUpDelaysMs[] = { 3000, 5000, 10000, 15000 };
// Device events end a wait early. After this many in one step the step counts as used anyway.
constexpr unsigned kMaxEventWakesPerStep = 4;
// Minimum battery poll interval while the user is in a full-screen game, busy, or presenting.
constexpr unsigned long kFullscreenMinPollIntervalMs = 5UL * 60 * 1000;

struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

inline bool CoversMonitor(const Rect& wnd, const Rect& mon) {
    return mon.right > mon.left && mon.bottom > mon.top &&
           wnd.left <= mon.left && wnd.top <= mon.top &&
           wnd.right >= mon.right && wnd.bottom >= mon.bottom;
}

// Mirrors Win32 QUERY_USER_NOTIFICATION_STATE without requiring Windows headers.
enum class UserNotificationState {
    Unknown = 0,
    NotPresent = 1,
    Busy = 2,
    RunningD3DFullScreen = 3,
    PresentationMode = 4,
    AcceptsNotifications = 5,
    QuietTime = 6,
    App = 7,
};

inline bool IsFullscreenOrBusy(UserNotificationState state) {
    return state == UserNotificationState::Busy ||
           state == UserNotificationState::RunningD3DFullScreen ||
           state == UserNotificationState::PresentationMode;
}

// True when the active foreground window fills its monitor or exclusive D3D full-screen is active.
// A full-screen video on a side monitor while working elsewhere does not trigger this.
inline bool IsActiveFullscreen(bool foregroundCoversMonitor, UserNotificationState qunsState) {
    return foregroundCoversMonitor || qunsState == UserNotificationState::RunningD3DFullScreen;
}

inline unsigned long EffectivePollIntervalMs(unsigned long baseIntervalMs, bool pollLessFullscreen,
                                             UserNotificationState state) {
    if (pollLessFullscreen && IsFullscreenOrBusy(state))
        return std::max(baseIntervalMs, kFullscreenMinPollIntervalMs);
    return baseIntervalMs;
}

inline unsigned long EffectivePollIntervalMs(unsigned long baseIntervalMs, bool pollLessFullscreen,
                                             bool foregroundCoversMonitor, UserNotificationState qunsState) {
    if (pollLessFullscreen && IsActiveFullscreen(foregroundCoversMonitor, qunsState))
        return std::max(baseIntervalMs, kFullscreenMinPollIntervalMs);
    return baseIntervalMs;
}

enum class Mode { Idle, Startup, Resume, Arrival };

// Connected and awake, but no level yet (the link or the device's report is not ready).
inline bool Pending(const DeviceStatus& s) { return s.connected && !s.asleep && s.percent < 0; }

// After a resume: every device that was awake before is back (with a level, or reported asleep).
inline bool ResumeDone(const std::vector<DeviceStatus>& results, const std::set<std::wstring>& awakeBefore) {
    std::set<std::wstring> back;
    for (const auto& s : results) {
        if (Pending(s)) return false;
        if (s.connected) back.insert(s.key);
    }
    for (const auto& key : awakeBefore)
        if (!back.count(key)) return false;
    return true;
}

// After an arrival: a device that had no level before has one now.
inline bool ArrivalDone(const std::vector<DeviceStatus>& results, const std::set<std::wstring>& readyBefore) {
    bool fresh = false;
    for (const auto& s : results) {
        if (Pending(s)) return false;
        if (s.connected && s.percent >= 0 && !readyBefore.count(s.key)) fresh = true;
    }
    return fresh;
}

class Monitor {
public:
    void BeginStartup() { Begin(Mode::Startup, {}); }
    // awakeBefore: keys of the devices that were connected and awake before the sleep.
    void BeginResume(std::set<std::wstring> awakeBefore) { Begin(Mode::Resume, std::move(awakeBefore)); }
    // readyBefore: keys of the devices that already had a level. A startup or resume check in progress
    // already polls often enough and is kept.
    void BeginArrival(std::set<std::wstring> readyBefore) {
        if (mode_ == Mode::Startup || mode_ == Mode::Resume) return;
        Begin(Mode::Arrival, std::move(readyBefore));
    }

    Mode Current() const { return mode_; }

    // After each poll: how long to wait before the next one.
    unsigned long DelayAfterPoll(const std::vector<DeviceStatus>& results, unsigned long pollIntervalMs) {
        if (mode_ == Mode::Idle) return pollIntervalMs;
        bool done = (mode_ == Mode::Resume && ResumeDone(results, keys_)) ||
                    (mode_ == Mode::Arrival && ArrivalDone(results, keys_));
        bool startup = mode_ == Mode::Startup;
        size_t steps = startup ? std::size(kStartupDelaysMs) : std::size(kFollowUpDelaysMs);
        if (done || step_ >= steps) {
            Begin(Mode::Idle, {});
            return pollIntervalMs;
        }
        return std::min(startup ? kStartupDelaysMs[step_] : kFollowUpDelaysMs[step_], pollIntervalMs);
    }

    // When the wait after a poll ends: timedOut, or early because a device event asked for a refresh.
    void OnWaitFinished(bool timedOut) {
        if (mode_ == Mode::Idle) return;
        if (timedOut || ++eventWakes_ >= kMaxEventWakesPerStep) {
            ++step_;
            eventWakes_ = 0;
        }
    }

private:
    void Begin(Mode mode, std::set<std::wstring> keys) {
        mode_ = mode;
        step_ = 0;
        eventWakes_ = 0;
        keys_ = std::move(keys);
    }

    Mode mode_ = Mode::Idle;
    size_t step_ = 0;
    unsigned eventWakes_ = 0;
    std::set<std::wstring> keys_;
};

}  // namespace refresh_policy
