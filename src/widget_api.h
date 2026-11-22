// The strip API, without an include guard: widget.h puts it in namespace widget (the battery strip) and
// vitals_strip.h in namespace vitals_strip (the CPU / RAM / GPU strip). Include it only from those two.
// The Segoe Fluent Icons glyph the strip uses for a kind of device (also used by the menu).
wchar_t DeviceGlyph(DeviceKind kind, bool bluetooth);
// Creates the widget on the primary taskbar. Clicks are reported by posting `clickMsg` to `notifyWnd`.
bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme);
void Destroy();
bool Alive();
void SetItems(const std::vector<WidgetItem>& items, const std::wstring& tooltip);
void SetLightTheme(bool light);
// Draws a rounded plate in the taskbar's colours behind the text (kept across Create/Destroy).
void SetBackground(bool on);
// Icon above the percentage instead of beside it, when the taskbar is tall enough (kept across Create/Destroy).
void SetStacked(bool on);
// Steps past other apps' widgets in the taskbar (e.g. a network graph or a countdown) that sit where the
// strip would go, so both stay readable (kept across Create/Destroy).
void SetMakeRoom(bool on);
// Where the column sits on a left / right taskbar (kept across Create/Destroy).
// Auto: above the tray, at the top while task buttons reach it. Bottom (Windows 11): at the very bottom,
// with the taskbar's own content (task buttons and tray) moved up to make room.
enum class VerticalPlace { Auto, Top, AboveTray, Bottom };
void SetVerticalPlace(VerticalPlace place);
// True while the widget is on a left / right taskbar.
bool Vertical();
// At the left end of a Windows 11 taskbar (right of the Widgets button) instead of next to the clock, where
// there is room; otherwise next to the clock as usual (kept across Create/Destroy).
void SetFarLeft(bool on);
// True while the strip actually sits at the left end (SetFarLeft on, and there was room on the main taskbar).
bool AtFarLeft();
// Left end or nowhere: with no room there, the strip hides instead of going next to the clock (used when the
// battery and CPU / RAM / GPU strips sit apart, so the two never overlap).
void SetFarLeftOnly(bool on);
// Which taskbars show the strip: L"all" (default), L"main", or the id of one other monitor from
// OtherMonitors() (then only that monitor shows it). Kept across Create/Destroy.
void SetMonitorChoice(const std::wstring& choice);
struct MonitorOption {
    std::wstring id;     // lasting monitor identity, stored in the settings
    std::wstring label;  // "Display 3 (right)"
};
// Index of the chosen monitor in `ids` (MonitorOption::id values), or -1 when it is not connected. A saved id
// that matches no monitor exactly still matches the only connected monitor of the same model: a laptop panel's
// id changes when a MUX / Advanced Optimus switch moves it to the other GPU.
int FindMonitor(const std::vector<std::wstring>& ids, const std::wstring& choice);
// The monitors other than the main one that have a horizontal taskbar (empty with one monitor).
std::vector<MonitorOption> OtherMonitors();
// Re-checks the taskbar layout and moves the widget if needed; call about once a second.
// Returns false when the taskbar it was attached to is gone.
bool Reposition();
