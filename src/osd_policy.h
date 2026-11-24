#pragma once
// Pure, Windows-independent policy and corner placement math for the in-game OSD-lite alert.
#include "refresh_policy.h"

#include <algorithm>
#include <vector>

namespace osd_policy {

enum class OsdMode {
    Off = 0,
    FullscreenOnly = 1,  // Smart Auto: side monitor when free on 2+ screens, else game monitor
    Always = 2,
    FullscreenGame = 3,  // Always on the active full-screen game monitor
};

enum class OsdCorner {
    TopRight = 0,
    TopLeft = 1,
    BottomRight = 2,
    BottomLeft = 3,
};

inline OsdMode ClampOsdMode(int raw) {
    switch (raw) {
        case 1: return OsdMode::FullscreenOnly;
        case 2: return OsdMode::Always;
        case 3: return OsdMode::FullscreenGame;
        default: return OsdMode::Off;
    }
}

inline OsdCorner ClampOsdCorner(int raw) {
    switch (raw) {
        case 1: return OsdCorner::TopLeft;
        case 2: return OsdCorner::BottomRight;
        case 3: return OsdCorner::BottomLeft;
        default: return OsdCorner::TopRight;
    }
}

inline bool ShouldShowOsd(OsdMode mode, bool activeFullscreen) {
    switch (mode) {
        case OsdMode::FullscreenOnly:
        case OsdMode::FullscreenGame: return activeFullscreen;
        case OsdMode::Always:         return true;
        case OsdMode::Off:
        default:                      return false;
    }
}

inline bool ShouldShowOsd(OsdMode mode, refresh_policy::UserNotificationState state) {
    return ShouldShowOsd(mode, refresh_policy::IsFullscreenOrBusy(state));
}

struct MonitorCandidate {
    refresh_policy::Rect monitorRect;
    refresh_policy::Rect workRect;
    bool isPrimary = false;
    bool isGameMonitor = false;
    bool hasFullscreenWindow = false;
};

struct OsdTarget {
    int monitorIndex = -1;
    refresh_policy::Rect area{};
};

inline refresh_policy::Rect SelectAreaForMonitor(const MonitorCandidate& m, bool fullBounds) {
    const bool validWork = m.workRect.right > m.workRect.left && m.workRect.bottom > m.workRect.top;
    if (!fullBounds && validWork) return m.workRect;
    return m.monitorRect;
}

// Selects which monitor and bounds (work area on desktop/side monitor, monitor bounds on full-screen monitor)
// host the corner OSD alert.
inline OsdTarget ChooseTargetMonitor(const std::vector<MonitorCandidate>& monitors, OsdMode mode) {
    if (monitors.empty()) return {};
    int gameIdx = -1;
    int primaryIdx = 0;
    for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
        if (monitors[i].isGameMonitor && gameIdx < 0) gameIdx = i;
        if (monitors[i].isPrimary) primaryIdx = i;
    }
    if (mode == OsdMode::FullscreenGame) {
        if (gameIdx >= 0) return { gameIdx, SelectAreaForMonitor(monitors[gameIdx], true) };
        return { primaryIdx, SelectAreaForMonitor(monitors[primaryIdx], false) };
    }
    if (gameIdx >= 0) {
        if (monitors.size() >= 2) {
            if (primaryIdx != gameIdx && !monitors[primaryIdx].hasFullscreenWindow)
                return { primaryIdx, SelectAreaForMonitor(monitors[primaryIdx], false) };
            for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
                if (i != gameIdx && !monitors[i].hasFullscreenWindow)
                    return { i, SelectAreaForMonitor(monitors[i], false) };
            }
        }
        return { gameIdx, SelectAreaForMonitor(monitors[gameIdx], true) };
    }
    return { primaryIdx, SelectAreaForMonitor(monitors[primaryIdx], false) };
}

struct OsdPoint {
    int x = 0;
    int y = 0;
};

inline OsdPoint ComputeOsdOrigin(int areaLeft, int areaTop, int areaRight, int areaBottom,
                                 int width, int height, int margin, OsdCorner corner) {
    const int w = std::max(0, width);
    const int h = std::max(0, height);
    const int m = std::max(0, margin);
    const int maxX = std::max(areaLeft, areaRight - w);
    const int maxY = std::max(areaTop, areaBottom - h);
    int x = areaRight - w - m;
    int y = areaTop + m;
    switch (corner) {
        case OsdCorner::TopLeft:
            x = areaLeft + m;
            y = areaTop + m;
            break;
        case OsdCorner::BottomRight:
            x = areaRight - w - m;
            y = areaBottom - h - m;
            break;
        case OsdCorner::BottomLeft:
            x = areaLeft + m;
            y = areaBottom - h - m;
            break;
        case OsdCorner::TopRight:
        default:
            x = areaRight - w - m;
            y = areaTop + m;
            break;
    }
    return { std::clamp(x, areaLeft, maxX), std::clamp(y, areaTop, maxY) };
}

}  // namespace osd_policy
