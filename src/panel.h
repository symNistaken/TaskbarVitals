#pragma once
// Settings panel: modern Fluent card-based window opened when clicking the taskbar strip or tray icon.
// Organised into four tabs (Batteries, System, Appearance, General) with grouped cards, inline
// reorder buttons, live system badges, and drop-down choices backed by main.cpp's commands.
#include <windows.h>

#include <string>
#include <vector>

namespace panel {

enum class Tab { Batteries = 0, System = 1, Appearance = 2, General = 3, Count = 4 };

enum class RowKind {
    SectionHeader,  // Category heading above a card
    DeviceCard,     // Battery device row: icon, name, status/via, [↑] [↓] [⋯] [Switch]
    SystemHeader,   // Header of a system card (CPU, RAM, GPU, VRAM): icon, title, subtitle, value badge, [↑] [↓], optional [Switch]
    Toggle,         // Label + on/off switch (optionally indented inside a system card)
    Choice,         // Label + rounded pill with current choice + chevron down (opens popup)
    Action,         // Label + chevron right (runs command)
    Segmented,      // 2-segment toggle bar ([ left | right ])
};

struct Row {
    RowKind kind = RowKind::Action;
    bool newCard = false;      // starts a new rounded card container (unless SectionHeader)
    bool indented = false;     // slightly indented sub-row inside a card
    std::wstring label;        // primary text (or left segment label for Segmented)
    std::wstring sublabel;     // secondary text under label (or right segment label for Segmented)
    std::wstring value;        // right-side badge or current choice text
    wchar_t glyph = 0;         // Segoe Fluent Icons glyph
    COLORREF accentColor = 0;  // icon accent colour when hasAccent is true
    bool hasAccent = false;
    bool on = false;           // switch state (or true = left segment active for Segmented)
    bool hasSwitch = true;     // for SystemHeader: RAM and GPU memory have a direct switch
    bool grayed = false;
    bool dim = false;
    bool canUp = false;
    bool canDown = false;
    UINT id = 0;               // primary command id (toggle/action/choice/left segment)
    UINT id2 = 0;              // right segment command id (Segmented) or "More (⋯)" popup id (DeviceCard)
    UINT upId = 0;             // command id for [↑] Move earlier
    UINT downId = 0;           // command id for [↓] Move later
};

struct FooterButton {
    std::wstring label;
    wchar_t glyph = 0;
    UINT id = 0;
};

struct Model {
    std::vector<Row> tabs[(int)Tab::Count];
    std::vector<FooterButton> footer;
};

struct Host {
    void (*build)(Model& out) = nullptr;
    void (*run)(UINT command) = nullptr;
    UINT (*popup)(UINT id, HWND owner, POINT screenPt) = nullptr;
    bool (*light)() = nullptr;
};

// Opens the panel near the taskbar of the monitor under the cursor, or brings it to the front.
void Show(HINSTANCE inst, const Host& host);
// Reloads the model (new poll results, system samples, theme or language change). Does nothing while closed.
void Refresh();
// Closes the panel (the app is exiting).
void Close();

}  // namespace panel
