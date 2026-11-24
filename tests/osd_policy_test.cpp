// Standalone portable tests for the in-game OSD-lite alert policy and corner placement math.
#include "../src/osd_policy.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace osd_policy;
    using refresh_policy::UserNotificationState;

    // Clamping persisted mode values.
    {
        assert(ClampOsdMode(0) == OsdMode::Off);
        assert(ClampOsdMode(1) == OsdMode::FullscreenOnly);
        assert(ClampOsdMode(2) == OsdMode::Always);
        assert(ClampOsdMode(3) == OsdMode::FullscreenGame);
        assert(ClampOsdMode(-1) == OsdMode::Off);
        assert(ClampOsdMode(4) == OsdMode::Off);
        assert(ClampOsdMode(999) == OsdMode::Off);
    }

    // Clamping persisted corner values.
    {
        assert(ClampOsdCorner(0) == OsdCorner::TopRight);
        assert(ClampOsdCorner(1) == OsdCorner::TopLeft);
        assert(ClampOsdCorner(2) == OsdCorner::BottomRight);
        assert(ClampOsdCorner(3) == OsdCorner::BottomLeft);
        assert(ClampOsdCorner(-1) == OsdCorner::TopRight);
        assert(ClampOsdCorner(4) == OsdCorner::TopRight);
        assert(ClampOsdCorner(99) == OsdCorner::TopRight);
    }

    // ShouldShowOsd across every mode and notification state.
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
        for (UserNotificationState st : allStates) {
            assert(!ShouldShowOsd(OsdMode::Off, st));
            assert(ShouldShowOsd(OsdMode::Always, st));
            const bool expectedFullscreen =
                st == UserNotificationState::Busy ||
                st == UserNotificationState::RunningD3DFullScreen ||
                st == UserNotificationState::PresentationMode;
            assert(ShouldShowOsd(OsdMode::FullscreenOnly, st) == expectedFullscreen);
            assert(ShouldShowOsd(OsdMode::FullscreenGame, st) == expectedFullscreen);
        }
        assert(!ShouldShowOsd(OsdMode::Off, true));
        assert(!ShouldShowOsd(OsdMode::FullscreenOnly, false));
        assert(ShouldShowOsd(OsdMode::FullscreenOnly, true));
        assert(!ShouldShowOsd(OsdMode::FullscreenGame, false));
        assert(ShouldShowOsd(OsdMode::FullscreenGame, true));
        assert(ShouldShowOsd(OsdMode::Always, false));
        assert(ShouldShowOsd(OsdMode::Always, true));
    }

    // ChooseTargetMonitor across single- and multi-monitor topologies.
    {
        using refresh_policy::Rect;
        const Rect m1{ 0, 0, 1920, 1080 };
        const Rect w1{ 0, 0, 1920, 1032 };
        const Rect m2{ 1920, 0, 3840, 1080 };
        const Rect w2{ 1920, 0, 3840, 1040 };

        // Empty list.
        assert(ChooseTargetMonitor({}, OsdMode::FullscreenOnly).monitorIndex == -1);

        // Single monitor fullscreen -> uses monitorRect on that screen.
        {
            std::vector<MonitorCandidate> one = {
                { m1, w1, true, true, true },
            };
            const OsdTarget t = ChooseTargetMonitor(one, OsdMode::FullscreenOnly);
            assert(t.monitorIndex == 0);
            assert(t.area.left == m1.left && t.area.bottom == m1.bottom);
        }

        // Two monitors, game on primary, side monitor free -> places OSD in side monitor's workRect.
        {
            std::vector<MonitorCandidate> two = {
                { m1, w1, true, true, true },
                { m2, w2, false, false, false },
            };
            const OsdTarget smart = ChooseTargetMonitor(two, OsdMode::FullscreenOnly);
            assert(smart.monitorIndex == 1);
            assert(smart.area.left == w2.left && smart.area.bottom == w2.bottom);

            const OsdTarget alwaysDuringGame = ChooseTargetMonitor(two, OsdMode::Always);
            assert(alwaysDuringGame.monitorIndex == 1);
            assert(alwaysDuringGame.area.left == w2.left && alwaysDuringGame.area.bottom == w2.bottom);

            const OsdTarget onGame = ChooseTargetMonitor(two, OsdMode::FullscreenGame);
            assert(onGame.monitorIndex == 0);
            assert(onGame.area.left == m1.left && onGame.area.bottom == m1.bottom);
        }

        // Two monitors, game on secondary, primary free -> prefers free primary's workRect.
        {
            std::vector<MonitorCandidate> gameOnSide = {
                { m1, w1, true, false, false },
                { m2, w2, false, true, true },
            };
            const OsdTarget t = ChooseTargetMonitor(gameOnSide, OsdMode::FullscreenOnly);
            assert(t.monitorIndex == 0);
            assert(t.area.left == w1.left && t.area.bottom == w1.bottom);
        }

        // Two monitors, both covered by full-screen windows -> falls back to active game monitor's monitorRect.
        {
            std::vector<MonitorCandidate> bothFull = {
                { m1, w1, true, true, true },
                { m2, w2, false, false, true },
            };
            const OsdTarget t = ChooseTargetMonitor(bothFull, OsdMode::FullscreenOnly);
            assert(t.monitorIndex == 0);
            assert(t.area.left == m1.left && t.area.bottom == m1.bottom);
        }

        // Desktop (no full-screen game), Always mode -> uses primary monitor's workRect.
        {
            std::vector<MonitorCandidate> desktop = {
                { m2, w2, false, false, false },
                { m1, w1, true, false, false },
            };
            const OsdTarget t = ChooseTargetMonitor(desktop, OsdMode::Always);
            assert(t.monitorIndex == 1);
            assert(t.area.left == w1.left && t.area.bottom == w1.bottom);
        }
    }

    // ComputeOsdOrigin on a standard primary monitor work area.
    {
        const int left = 0, top = 0, right = 1920, bottom = 1032;
        const int w = 240, h = 48, margin = 16;

        const OsdPoint tr = ComputeOsdOrigin(left, top, right, bottom, w, h, margin, OsdCorner::TopRight);
        assert(tr.x == 1664 && tr.y == 16);

        const OsdPoint tl = ComputeOsdOrigin(left, top, right, bottom, w, h, margin, OsdCorner::TopLeft);
        assert(tl.x == 16 && tl.y == 16);

        const OsdPoint br = ComputeOsdOrigin(left, top, right, bottom, w, h, margin, OsdCorner::BottomRight);
        assert(br.x == 1664 && br.y == 968);

        const OsdPoint bl = ComputeOsdOrigin(left, top, right, bottom, w, h, margin, OsdCorner::BottomLeft);
        assert(bl.x == 16 && bl.y == 968);
    }

    // ComputeOsdOrigin with non-zero origin (e.g. top/left docked taskbar or offset work area).
    {
        const int left = 64, top = 48, right = 2560, bottom = 1440;
        const int w = 300, h = 60, margin = 24;

        const OsdPoint tl = ComputeOsdOrigin(left, top, right, bottom, w, h, margin, OsdCorner::TopLeft);
        assert(tl.x == 88 && tl.y == 72);

        const OsdPoint br = ComputeOsdOrigin(left, top, right, bottom, w, h, margin, OsdCorner::BottomRight);
        assert(br.x == 2236 && br.y == 1356);
    }

    // ComputeOsdOrigin clamps safely when the overlay is larger than the area or margin is huge/negative.
    {
        const OsdPoint tooWide = ComputeOsdOrigin(100, 50, 220, 90, 300, 80, 16, OsdCorner::BottomRight);
        assert(tooWide.x == 100 && tooWide.y == 50);

        const OsdPoint hugeMargin = ComputeOsdOrigin(0, 0, 800, 600, 200, 50, 1000, OsdCorner::TopRight);
        assert(hugeMargin.x == 0 && hugeMargin.y == 550);

        const OsdPoint hugeMarginTl = ComputeOsdOrigin(0, 0, 800, 600, 200, 50, 1000, OsdCorner::TopLeft);
        assert(hugeMarginTl.x == 600 && hugeMarginTl.y == 550);

        const OsdPoint negMargin = ComputeOsdOrigin(0, 0, 800, 600, 200, 50, -20, OsdCorner::BottomRight);
        assert(negMargin.x == 600 && negMargin.y == 550);
    }

    std::puts("OSD policy tests passed");
}
