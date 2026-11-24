#pragma once
// Translucent, click-through, non-activating corner OSD-lite alert for low-battery warnings.
#include <windows.h>

#include <string>

#include "osd_policy.h"

namespace osd_overlay {

// Returns true when the active foreground window covers its entire monitor.
// If `outGameMon` is provided and true is returned, writes that monitor's HMONITOR.
bool IsForegroundFullscreen(HMONITOR* outGameMon = nullptr);

// Shows or updates the corner overlay (using smart multi-monitor placement) and resets its auto-hide timer.
void Show(HINSTANCE inst, wchar_t glyph, const std::wstring& deviceName, int percent,
          osd_policy::OsdMode mode, osd_policy::OsdCorner corner,
          refresh_policy::UserNotificationState qunsState, bool light,
          COLORREF accentColor, bool hasAccent);

// Destroys any active overlay window on application shutdown.
void Shutdown();

}  // namespace osd_overlay
