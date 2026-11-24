#pragma once
// A small transparent strip on the taskbar, just left of the notification area, showing
// "[icon] 30%  [icon] 93%" and live hardware vitals. Implemented as a layered child window of the
// taskbar (Shell_TrayWnd), with a copy on each secondary-monitor taskbar (Shell_SecondaryTrayWnd).
#include <windows.h>
#include <string>
#include <utility>
#include <vector>
#include "brands.h"
#include "devices.h"
#include "vector_icons.h"

struct WidgetItem {
    DeviceKind kind = DeviceKind::Other;
    int  percent  = -1;     // -1 = unknown
    std::wstring text;      // shown instead of the percentage when set (coarse levels or system metric text)
    bool charging = false;
    bool full     = false;  // on power and fully charged: a green check mark instead of the bolt
    bool dim      = false;  // asleep / stale value
    bool low      = false;
    bool bluetooth = false;     // picks the Bluetooth icon when the device type is unknown
    bool wirelessLink = false;  // on a 2.4 GHz receiver: small wireless mark after the level (opt-in)
    std::wstring label;         // tiny caption on the icon; set only when two devices share a kind
    Accent accent  = Accent::None;  // brand/system color for the icon, its glow and the level bar
    wchar_t glyph  = 0;             // Segoe Fluent fallback glyph for system readings (CPU, RAM, GPU, Disk, Net)
    vector_icons::VectorIcon vectorIcon = vector_icons::VectorIcon::None;  // sharp custom vector glyph
    int heat       = 0;             // temperature level: 1 = elevated (orange text), 2 = high (red text)
    bool isVital   = false;         // true for CPU / RAM / GPU / VRAM / SSD / Network readings

    // Shown in the hover card that opens when the mouse rests on this item in the strip.
    std::wstring name;               // display name (custom name when renamed)
    std::wstring status;             // e.g. "Charging", "42 C · 18 W"
    std::vector<std::wstring> info;  // extra lines: estimated time to empty, connection, I/O, VRAM
    std::vector<std::pair<long long, int>> trend;  // {seconds ago (<= 0), value}
    int trendSpanSec = 24 * 3600;                  // X-axis past span in seconds (24h batteries, 180s vitals)
    int trendMax = 100;                            // Y-axis top value (100 for %, dynamic for MB/s)
    std::wstring trendTitle;                       // chart heading override
    std::wstring trendAxisLeft;                    // left X-axis label override
    double hoursLeft = -1;      // estimated time to empty (the chart's dashed forecast), -1 = none
    std::wstring hoursLeftText; // the same, formatted ("~3 d 4 h")
};

namespace widget {
#include "widget_api.h"
}
