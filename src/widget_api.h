// The strip API, without an include guard: widget.h puts it in namespace widget (the battery strip) and
// vitals_strip.h in namespace vitals_strip (the CPU / RAM / GPU / SSD / Net strip). Include it only from those two.
void SetMyFinder(bool on);
wchar_t DeviceGlyph(DeviceKind kind, bool bluetooth);
bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme);
void Destroy();
bool Alive();
void SetItems(const std::vector<WidgetItem>& items, const std::wstring& tooltip);
void SetCardOptions(bool card, bool chart);
void SetLightTheme(bool light);
void SetBackground(bool on);
void SetStacked(bool on);
void SetTwoRows(bool on);
void SetCustomIcons(bool on);
void SetShowPercentSign(bool on);
void SetRingStyle(bool on);
void SetConnectionIcons(bool on);
std::wstring ItemsToText(const std::vector<WidgetItem>& items);
void SetAnimateCharging(bool on);
void SetMakeRoom(bool on);
enum class VerticalPlace { Auto, Top, AboveTray, Bottom };
void SetVerticalPlace(VerticalPlace place);
bool Vertical();
void SetFarLeft(bool on);
bool AtFarLeft();
void SetFarLeftOnly(bool on);
void SetBeforeApps(bool on);
void SetMonitorChoice(const std::wstring& choice);
struct MonitorOption {
    std::wstring id;     // lasting monitor identity, stored in the settings
    std::wstring label;  // "Display 3 (right)"
};
int FindMonitor(const std::vector<std::wstring>& ids, const std::wstring& choice);
std::vector<MonitorOption> OtherMonitors();
bool Reposition();
