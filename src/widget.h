#pragma once
// A small transparent strip on the taskbar, just left of the notification area, showing
// "[icon] 30%  [icon] 93%". Implemented as a layered child window of the taskbar (Shell_TrayWnd), with a
// copy on each secondary-monitor taskbar (Shell_SecondaryTrayWnd) next to its clock.
#include <windows.h>
#include <string>
#include <vector>
#include "brands.h"
#include "devices.h"

struct WidgetItem {
    DeviceKind kind = DeviceKind::Other;
    int  percent  = -1;     // -1 = unknown
    std::wstring text;      // shown instead of the percentage when set (coarse levels like "Low")
    bool charging = false;
    bool full     = false;  // on power and fully charged: a green check mark instead of the bolt
    bool dim      = false;  // asleep / stale value
    bool low      = false;
    bool bluetooth = false;  // picks the Bluetooth icon when the device type is unknown
    std::wstring label;      // tiny caption on the icon; set only when two devices of one kind are shown
    Accent accent  = Accent::None;  // brand color for the icon, its glow and the level bar
    wchar_t glyph  = 0;      // Segoe Fluent icon to draw instead of the device kind's (system readings: CPU, RAM)
    int heat       = 0;      // temperature level of a reading: 1 = elevated (orange text), 2 = high (red text)
};

namespace widget {
#include "widget_api.h"
}
