#include "battery_history.h"
#include "history_model.h"
#include "demo_history.h"
#include "runtime_estimate.h"
#include "i18n.h"
#include "memory_trim.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace battery_history {
namespace {

Store g_store;
std::set<std::wstring> g_keysAtStartup;  // see KeysAtStartup()
bool g_demo = false;
std::wstring g_file;
HWND g_view = nullptr;
HWND g_btn24 = nullptr, g_btn7 = nullptr, g_btn30 = nullptr, g_btnClear = nullptr;
std::wstring g_key, g_name;
int g_days = 7;
constexpr int k24Hours = 201, k7Days = 202, k30Days = 203, kClear = 204;
constexpr int64_t kSecondsPerDay = 86400;
constexpr DWORD kMaxFileBytes = 4 * 1024 * 1024;
constexpr DWORD kCompactAfterBytes = 1024 * 1024;
const wchar_t kWindowClass[] = L"WirelessBatteryHistoryViewer";

int64_t UtcNow() { return static_cast<int64_t>(std::time(nullptr)); }

// Plain ASCII rows only, with a hex-encoded device key. No telemetry is uploaded.
std::wstring HistoryPath() {
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    std::wstring folder = std::wstring(appdata) + L"\\WirelessBattery";
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
    return folder + L"\\battery_history.tsv";
}

bool WriteAll(HANDLE file, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        DWORD written = 0;
        DWORD count = static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1 << 20));
        if (!WriteFile(file, data.data() + offset, count, &written, nullptr) || !written) return false;
        offset += written;
    }
    return true;
}

bool Rewrite() {
    if (g_file.empty()) return false;
    // Bound the on-disk file even if device keys are unusually long.
    std::string bytes = g_store.Serialize();
    while (bytes.size() > 2 * 1024 * 1024 && !g_store.samples.empty()) {
        const size_t drop = std::max<size_t>(1, g_store.samples.size() / 8);
        g_store.samples.erase(g_store.samples.begin(), g_store.samples.begin() + drop);
        bytes = g_store.Serialize();
    }
    const std::wstring temp = g_file + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool ok = WriteAll(file, bytes) && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temp.c_str(), g_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

void Append(const Sample& s) {
    if (g_file.empty()) return;
    const std::string line = Encode(s);
    HANDLE file = CreateFileW(g_file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const bool ok = WriteAll(file, line);
    LARGE_INTEGER size{};
    const bool compact = ok && GetFileSizeEx(file, &size) && size.QuadPart > kCompactAfterBytes;
    CloseHandle(file);
    if (compact) Rewrite();
}

void Load() {
    if (g_file.empty()) return;
    HANDLE file = CreateFileW(g_file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return; // first run
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 || length.QuadPart > kMaxFileBytes) {
        CloseHandle(file); g_file.clear(); return; // leave an unexpectedly large file untouched
    }
    std::string data(static_cast<size_t>(length.QuadPart), '\0');
    size_t offset = 0;
    bool ok = true;
    while (offset < data.size()) {
        DWORD got = 0;
        DWORD count = static_cast<DWORD>(std::min<size_t>(data.size()-offset, 1 << 20));
        if (!ReadFile(file, &data[offset], count, &got, nullptr) || !got) { ok = false; break; }
        offset += got;
    }
    CloseHandle(file);
    if (!ok) return;
    g_store.Load(data, UtcNow());
    if (data != g_store.Serialize()) Rewrite(); // remove expired/malformed/incomplete records at startup
}

std::vector<Sample> Visible(int64_t now) {
    const int64_t since = now - static_cast<int64_t>(g_days) * kSecondsPerDay;
    if (!g_demo) return g_store.For(g_key, since, now);
    auto points = DemoSamples(g_key, now);
    points.erase(std::remove_if(points.begin(), points.end(), [since](const Sample& s) {
        return s.timestamp < since;
    }), points.end());
    return points;
}

// Battery history viewer: DPI-aware, dark, compact metrics and conservative discharge ETA.
// Follows the Windows app theme (AppsUseLightTheme), like the rest of the app.
struct Palette {
    COLORREF bg, panel, chart, border, grid, text, muted, blue, green, red, btnDown, btnActive, btnDisabledText;
};
constexpr Palette kDark = { RGB(12, 17, 28), RGB(22, 30, 45), RGB(15, 23, 37), RGB(47, 63, 83), RGB(42, 55, 74),
                            RGB(232, 240, 252), RGB(165, 182, 202), RGB(91, 166, 255), RGB(59, 215, 158),
                            RGB(255, 132, 147), RGB(38, 59, 83), RGB(31, 67, 112), RGB(102, 117, 136) };
constexpr Palette kLight = { RGB(243, 245, 248), RGB(255, 255, 255), RGB(250, 251, 253), RGB(209, 215, 224),
                             RGB(228, 232, 238), RGB(24, 32, 44), RGB(92, 104, 120), RGB(0, 103, 192),
                             RGB(16, 137, 62), RGB(196, 43, 28), RGB(214, 226, 240), RGB(224, 236, 252),
                             RGB(160, 168, 180) };
bool g_light = false;
const Palette& P() { return g_light ? kLight : kDark; }

bool AppsUseLightTheme() {
    DWORD v = 1, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v != 0;
}
constexpr int64_t kMaxConnectedGap = 4 * 3600;

HFONT g_uiFont = nullptr;
HFONT g_uiBold = nullptr;
HFONT g_uiTitle = nullptr;
HFONT g_uiValue = nullptr;

int Ui(HWND w, int logical) {
    const UINT dpi = w ? GetDpiForWindow(w) : 96;
    return MulDiv(logical, dpi ? static_cast<int>(dpi) : 96, 96);
}

void ApplyTheme(HWND w) {
    g_light = AppsUseLightTheme();
    HMODULE lib = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib) return;
    using AttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto fn = reinterpret_cast<AttributeFn>(GetProcAddress(lib, "DwmSetWindowAttribute"));
    if (fn) {
        BOOL dark = !g_light;
        if (FAILED(fn(w, 20, &dark, sizeof(dark)))) fn(w, 19, &dark, sizeof(dark));
    }
    FreeLibrary(lib);
}

HFONT CreateUiFont(HWND w, int points, int weight) {
    HDC dc = GetDC(w);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(w, dc);
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void SetControlFont(HWND h) {
    if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(
        g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

void Layout(HWND w) {
    RECT rect{};
    GetClientRect(w, &rect);
    const int right = rect.right;
    const int margin = Ui(w, 24);
    const int y = Ui(w, 18);
    const int height = Ui(w, 38);
    const int gap = Ui(w, 10);
    const int buttonWidth = Ui(w, 150);
    if (g_btn24) MoveWindow(g_btn24, margin, y, buttonWidth, height, TRUE);
    if (g_btn7) MoveWindow(g_btn7, margin + buttonWidth + gap, y, buttonWidth, height, TRUE);
    if (g_btn30) MoveWindow(g_btn30, margin + 2 * (buttonWidth + gap), y, buttonWidth, height, TRUE);
    if (g_btnClear) MoveWindow(g_btnClear, right - margin - Ui(w, 192), y, Ui(w, 192), height, TRUE);
}

void Label(HDC dc, const std::wstring& str, RECT r, COLORREF color,
           UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, str.c_str(), static_cast<int>(str.size()), &r, flags | DT_NOPREFIX);
}

void RoundedPanel(HDC dc, const RECT& r, COLORREF fill, COLORREF outline, int radius = 14) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, outline);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawHistoryButton(const DRAWITEMSTRUCT* item) {
    if (!item || item->CtlType != ODT_BUTTON) return;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool down = (item->itemState & ODS_SELECTED) != 0;
    const bool focused = (item->itemState & ODS_FOCUS) != 0;
    const bool active = (item->CtlID == k24Hours && g_days == 1) ||
                        (item->CtlID == k7Days && g_days == 7) ||
                        (item->CtlID == k30Days && g_days == 30);
    const bool clear = item->CtlID == kClear;
    const COLORREF background = disabled ? P().bg : down ? P().btnDown :
                                active ? P().btnActive : P().panel;
    const COLORREF outline = disabled ? P().grid : (active || focused) ? P().blue : P().border;
    const COLORREF textColor = disabled ? P().btnDisabledText : clear ? P().red : active ? P().text : P().muted;
    HBRUSH corners = CreateSolidBrush(P().bg);  // behind the rounded corners, instead of the button's white
    FillRect(item->hDC, &item->rcItem, corners);
    DeleteObject(corners);
    RoundedPanel(item->hDC, item->rcItem, background, outline, 12);
    wchar_t text[160]{};
    GetWindowTextW(item->hwndItem, text, 160);
    HFONT oldFont = static_cast<HFONT>(SelectObject(item->hDC,
        g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT)));
    RECT label = item->rcItem;
    InflateRect(&label, -Ui(item->hwndItem, 8), 0);
    Label(item->hDC, text, label, textColor, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(item->hDC, oldFont);
    // The blue border above is the keyboard-focus indicator, so DrawFocusRect is not needed.
}

void DrawStat(HWND w, HDC dc, const RECT& r, const wchar_t* title,
              const std::wstring& value, COLORREF valueColor) {
    RoundedPanel(dc, r, P().panel, P().border, Ui(w, 12));
    const int pad = Ui(w, 14);
    Label(dc, T(title), RECT{r.left + pad, r.top + Ui(w, 7), r.right - pad, r.top + Ui(w, 34)}, P().muted);
    HFONT old = static_cast<HFONT>(SelectObject(dc, g_uiValue ? g_uiValue : GetStockObject(DEFAULT_GUI_FONT)));
    Label(dc, value, RECT{r.left + pad, r.top + Ui(w, 35), r.right - pad, r.bottom - Ui(w, 7)}, valueColor);
    SelectObject(dc, old);
}

std::wstring FormatRemaining(double hours) {
    // Rounded whole hours. Avoid giving a minute-accurate forecast from integer battery percentages.
    const int total = static_cast<int>(std::round(hours));
    const int days = total / 24;
    const int hrs = total % 24;
    if (days > 0) return TF(L"~{days} d {hours} h", {
        {L"days", std::to_wstring(days)}, {L"hours", std::to_wstring(hrs)}});
    return TF(L"~{hours} h", {{L"hours", std::to_wstring(total)}});
}

void Paint(HWND w) {
    PAINTSTRUCT ps{};
    HDC screen = BeginPaint(w, &ps);
    RECT bounds{};
    GetClientRect(w, &bounds);
    const int width = bounds.right, height = bounds.bottom;
    if (width <= 0 || height <= 0) { EndPaint(w, &ps); return; }

    // Avoid GDI flickering when changing tab or resizing.
    HDC buffer = CreateCompatibleDC(screen);
    HBITMAP bitmap = buffer ? CreateCompatibleBitmap(screen, width, height) : nullptr;
    HBITMAP oldBitmap = (bitmap && buffer) ? static_cast<HBITMAP>(SelectObject(buffer, bitmap)) : nullptr;
    HDC dc = oldBitmap ? buffer : screen;
    HBRUSH bg = CreateSolidBrush(P().bg);
    FillRect(dc, &bounds, bg);
    DeleteObject(bg);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT)));
    SetBkMode(dc, TRANSPARENT);

    const int margin = Ui(w, 24);
    const int64_t now = UtcNow();
    const auto points = Visible(now);
    const Stats stats = Summarize(points);
    const auto forecastPoints = g_demo ? DemoSamples(g_key, now) : g_store.For(g_key, now - kRetentionSeconds, now);
    const RuntimeEstimate forecast = EstimateRuntime(forecastPoints, now);

    // Headline and a separate subtitle row: they never overlap statistic cards.
    HFONT saved = static_cast<HFONT>(SelectObject(dc, g_uiTitle ? g_uiTitle : GetStockObject(DEFAULT_GUI_FONT)));
    Label(dc, std::wstring(T(L"Battery history")) + L"  •  " + g_name,
          RECT{margin, Ui(w, 73), width - margin, Ui(w, 113)}, P().text);
    SelectObject(dc, saved);
    if (g_demo) {
        Label(dc, T(L"Demo readings (not saved)"),
              RECT{margin, Ui(w, 111), width - margin, Ui(w, 139)}, P().muted);
    }

    const int gap = Ui(w, 10);
    const int cardsAvailable = width - 2 * margin - 4 * gap;
    const int cardWidth = cardsAvailable / 5;
    std::wstring delta = L"—";
    COLORREF deltaColor = P().text;
    if (stats.count >= 2) {
        const int change = stats.last - stats.first;
        delta = (change > 0 ? L"+" : L"") + std::to_wstring(change) + L" " + T(L"pp");
        deltaColor = change < 0 ? P().red : change > 0 ? P().green : P().text;
    }
    const std::wstring values[] = {
        stats.count ? std::to_wstring(stats.last) + L"%" : L"—",
        stats.count ? std::to_wstring(stats.lowest) + L"%" : L"—",
        stats.count ? std::to_wstring(stats.highest) + L"%" : L"—",
        delta,
        std::to_wstring(stats.count)
    };
    const wchar_t* headings[] = {L"Latest", L"Lowest", L"Highest", L"Change", L"Readings"};
    for (int i = 0; i < 5; ++i) {
        const int x = margin + i * (cardWidth + gap);
        RECT card{x, Ui(w, 148), x + cardWidth, Ui(w, 224)};
        DrawStat(w, dc, card, headings[i], values[i], i == 3 ? deltaColor : i == 0 ? P().blue : P().text);
    }

    RECT eta{margin, Ui(w, 237), width - margin, Ui(w, 318)};
    RoundedPanel(dc, eta, P().panel, P().border, Ui(w, 12));
    Label(dc, T(L"Estimated time to empty"),
          RECT{eta.left + Ui(w, 16), eta.top + Ui(w, 7), eta.right - Ui(w, 20), eta.top + Ui(w, 34)}, P().muted);
    std::wstring etaValue, etaDetails;
    COLORREF etaColor = P().blue;
    switch (forecast.state) {
        case RuntimeState::Ready:
            etaValue = FormatRemaining(forecast.hoursRemaining);
            etaDetails = TF(L"Recent discharge: {rate} pp/day · {count} readings", {
                {L"rate", std::to_wstring(static_cast<int>(std::round(forecast.percentPerHour * 240.0)) / 10) +
                          L"." + std::to_wstring(static_cast<int>(std::round(forecast.percentPerHour * 240.0)) % 10)},
                {L"count", std::to_wstring(forecast.sampleCount)}});
            break;
        case RuntimeState::Charging:
            etaValue = T(L"Currently charging");
            etaDetails = T(L"No remaining-time estimate while charging.");
            etaColor = P().green;
            break;
        case RuntimeState::Stale:
            etaValue = T(L"No recent readings");
            etaDetails = T(L"Connect the device to resume forecasting.");
            break;
        case RuntimeState::Flat:
            etaValue = T(L"Insufficient decline");
            etaDetails = T(L"A measurable discharge trend is required.");
            break;
        default:
            etaValue = T(L"Collecting data");
            etaDetails = T(L"Requires 4 readings, 2 hours and 3 percentage points of decline.");
            break;
    }
    saved = static_cast<HFONT>(SelectObject(dc, g_uiValue ? g_uiValue : GetStockObject(DEFAULT_GUI_FONT)));
    // The estimate gets its own second line and does not compete for space with the heading.
    Label(dc, etaValue, RECT{eta.left + Ui(w, 16), eta.top + Ui(w, 34),
                              eta.left + (eta.right - eta.left) / 2 - Ui(w, 8), eta.bottom - Ui(w, 7)}, etaColor);
    SelectObject(dc, saved);
    // Rate and method notes in the right half, avoiding truncation on smaller windows.
    Label(dc, etaDetails, RECT{eta.left + (eta.right - eta.left) / 2, eta.top + Ui(w, 43),
                               eta.right - Ui(w, 16), eta.bottom - Ui(w, 5)}, P().muted);

    RECT panel{margin, Ui(w, 332), width - margin, height - Ui(w, 22)};
    if (panel.bottom > panel.top + Ui(w, 140)) {
        RoundedPanel(dc, panel, P().panel, P().border, Ui(w, 14));
        Label(dc, T(L"Blue: recorded battery; green: measured increase"),
              RECT{panel.left + Ui(w, 18), panel.top + Ui(w, 8), panel.right - Ui(w, 12), panel.top + Ui(w, 42)}, P().muted);
        RECT plot{panel.left + Ui(w, 78), panel.top + Ui(w, 53),
                  panel.right - Ui(w, 21), panel.bottom - Ui(w, 61)};
        if (plot.right - plot.left > Ui(w, 120) && plot.bottom - plot.top > Ui(w, 68)) {
            RoundedPanel(dc, plot, P().chart, P().chart, Ui(w, 6));
            const int graphW = plot.right - plot.left, graphH = plot.bottom - plot.top;
            HPEN grid = CreatePen(PS_SOLID, 1, P().grid);
            HPEN oldPen = static_cast<HPEN>(SelectObject(dc, grid));
            for (int i = 0; i <= 4; ++i) {
                const int y = plot.top + graphH * i / 4;
                MoveToEx(dc, plot.left, y, nullptr);
                LineTo(dc, plot.right, y);
                // The label gutter is wide enough for 100%, including DPI scaling.
                Label(dc, std::to_wstring(100 - i * 25) + L"%",
                      RECT{panel.left + Ui(w, 9), y - Ui(w, 13), plot.left - Ui(w, 10), y + Ui(w, 13)},
                      P().muted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
                const int x = plot.left + graphW * i / 4;
                MoveToEx(dc, x, plot.top, nullptr);
                LineTo(dc, x, plot.bottom);
            }
            SelectObject(dc, oldPen);
            DeleteObject(grid);

            if (!points.empty()) {
                HPEN dataPen = CreatePen(PS_SOLID, Ui(w, 2), P().blue);
                HPEN increasePen = CreatePen(PS_SOLID, Ui(w, 2), P().green);
                const int64_t since = now - static_cast<int64_t>(g_days) * kSecondsPerDay;
                const int64_t span = static_cast<int64_t>(g_days) * kSecondsPerDay;
                int previousX = 0, previousY = 0, previousLevel = -1;
                int64_t previousTime = -1;
                for (size_t i = 0; i < points.size(); ++i) {
                    const Sample& sample = points[i];
                    int x = plot.left + static_cast<int>(static_cast<int64_t>(graphW) *
                        std::max<int64_t>(0, sample.timestamp - since) / span);
                    const int y = plot.bottom - graphH * sample.percent / 100;
                    x = std::max(static_cast<int>(plot.left), std::min(x, static_cast<int>(plot.right)));
                    const bool joined = previousTime >= 0 && sample.timestamp > previousTime &&
                        sample.timestamp - previousTime <= kMaxConnectedGap &&
                        std::abs(sample.percent - previousLevel) <= 25;
                    const bool rising = joined && sample.percent > previousLevel;
                    if (joined) {
                        HGDIOBJ pen = SelectObject(dc, rising ? increasePen : dataPen);
                        MoveToEx(dc, previousX, previousY, nullptr);
                        LineTo(dc, x, y);
                        SelectObject(dc, pen);
                    }
                    // Sparse markers at 30 days avoid a heavy dotted line.
                    if (i + 1 == points.size() || points.size() <= 70 || i % 10 == 0) {
                        HBRUSH dot = CreateSolidBrush(rising ? P().green : P().blue);
                        HGDIOBJ oldP = SelectObject(dc, GetStockObject(NULL_PEN));
                        HGDIOBJ oldB = SelectObject(dc, dot);
                        const int radius = Ui(w, i + 1 == points.size() ? 4 : 2);
                        Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
                        SelectObject(dc, oldB);
                        SelectObject(dc, oldP);
                        DeleteObject(dot);
                    }
                    previousX = x; previousY = y;
                    previousTime = sample.timestamp; previousLevel = sample.percent;
                }
                DeleteObject(dataPen);
                DeleteObject(increasePen);
            } else {
                Label(dc, T(L"No readings in this period"),
                      RECT{plot.left + Ui(w, 10), plot.top, plot.right - Ui(w, 10), plot.bottom},
                      P().muted, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }
            const std::wstring start = g_days == 1 ? std::wstring(T(L"24h ago")) :
                TF(L"{days} days ago", {{L"days", std::to_wstring(g_days)}});
            Label(dc, start, RECT{plot.left, plot.bottom + Ui(w, 5), plot.left + Ui(w, 160), plot.bottom + Ui(w, 31)}, P().muted);
            Label(dc, T(L"Now"), RECT{plot.right - Ui(w, 65), plot.bottom + Ui(w, 5),
                                      plot.right, plot.bottom + Ui(w, 31)}, P().muted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
            Label(dc, T(L"Trend estimates are approximate; missing samples are not inferred."),
                  RECT{plot.left, plot.bottom + Ui(w, 31), plot.right, plot.bottom + Ui(w, 57)},
                  P().muted, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
    }
    SelectObject(dc, oldFont);
    if (dc == buffer && oldBitmap) {
        BitBlt(screen, 0, 0, width, height, dc, 0, 0, SRCCOPY);
        SelectObject(buffer, oldBitmap);
    }
    if (bitmap) DeleteObject(bitmap);
    if (buffer) DeleteDC(buffer);
    EndPaint(w, &ps);
}

LRESULT CALLBACK ViewProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SETTINGCHANGE:
            if (lp && !lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet")) {
                ApplyTheme(w);
                InvalidateRect(w, nullptr, TRUE);
                for (HWND b : {g_btn24, g_btn7, g_btn30, g_btnClear}) if (b) InvalidateRect(b, nullptr, TRUE);
            }
            break;
        case WM_CREATE: {
            ApplyTheme(w);
            g_uiFont = CreateUiFont(w, 10, FW_NORMAL);
            g_uiBold = CreateUiFont(w, 13, FW_SEMIBOLD);
            g_uiTitle = CreateUiFont(w, 17, FW_SEMIBOLD);
            g_uiValue = CreateUiFont(w, 16, FW_SEMIBOLD);
            const HINSTANCE inst = GetModuleHandleW(nullptr);
            g_btn24 = CreateWindowExW(0, L"BUTTON", T(L"Last 24 hours"),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 110, 28,
                                     w, reinterpret_cast<HMENU>(static_cast<INT_PTR>(k24Hours)), inst, nullptr);
            g_btn7 = CreateWindowExW(0, L"BUTTON", T(L"Last 7 days"),
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 110, 28,
                                    w, reinterpret_cast<HMENU>(static_cast<INT_PTR>(k7Days)), inst, nullptr);
            g_btn30 = CreateWindowExW(0, L"BUTTON", T(L"Last 30 days"),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 110, 28,
                                     w, reinterpret_cast<HMENU>(static_cast<INT_PTR>(k30Days)), inst, nullptr);
            g_btnClear = CreateWindowExW(0, L"BUTTON", T(L"Clear device history"),
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 145, 28,
                                        w, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClear)), inst, nullptr);
            for (HWND b : {g_btn24, g_btn7, g_btn30, g_btnClear}) SetControlFont(b);
            EnableWindow(g_btnClear, g_demo ? FALSE : TRUE);
            Layout(w);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            if (info) { info->ptMinTrackSize.x = Ui(w, 880); info->ptMinTrackSize.y = Ui(w, 620); }
            return 0;
        }
        case WM_SIZE: Layout(w); InvalidateRect(w, nullptr, TRUE); return 0;
        case WM_DRAWITEM:
            if (lp && reinterpret_cast<DRAWITEMSTRUCT*>(lp)->CtlType == ODT_BUTTON) {
                DrawHistoryButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
                return TRUE;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case k24Hours: g_days = 1; break;
                case k7Days:   g_days = 7; break;
                case k30Days:  g_days = 30; break;
                case kClear:
                    if (!g_demo && MessageBoxW(w, T(L"Clear battery history for this device?"),
                                               T(L"Battery history"), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        Store before = g_store;
                        g_store.Clear(g_key);
                        if (!Rewrite()) {
                            g_store = std::move(before);
                            MessageBoxW(w, T(L"Could not save history changes."),
                                        T(L"Battery history"), MB_OK | MB_ICONWARNING);
                        }
                    }
                    break;
                default: return 0;
            }
            InvalidateRect(w, nullptr, TRUE);
            return 0;
        case WM_PAINT: Paint(w); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_CLOSE: DestroyWindow(w); return 0;
        case WM_DESTROY:
            g_view = nullptr;
            g_btn24 = g_btn7 = g_btn30 = g_btnClear = nullptr;
            if (g_uiFont) DeleteObject(g_uiFont);
            if (g_uiBold) DeleteObject(g_uiBold);
            if (g_uiTitle) DeleteObject(g_uiTitle);
            if (g_uiValue) DeleteObject(g_uiValue);
            g_uiFont = g_uiBold = g_uiTitle = g_uiValue = nullptr;
            memory_trim::Soon();
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

} // namespace

void Initialize(bool demo) {
    g_demo = demo;
    g_store = Store{};
    g_file.clear();
    if (!demo) {
        g_file = HistoryPath();
        Load();
    }
    g_keysAtStartup.clear();
    for (const auto& s : g_store.samples) g_keysAtStartup.insert(s.key);
}

const std::set<std::wstring>& KeysAtStartup() { return g_keysAtStartup; }

void Observe(const std::wstring& key, int percent, bool charging) {
    if (g_demo) return;
    Sample s{key, UtcNow(), percent, charging};
    if (g_store.Record(s)) {
        Append(s);
        if (g_view && key == g_key) InvalidateRect(g_view, nullptr, FALSE);
    }
}

double HoursRemaining(const std::wstring& key) {
    if (key.empty()) return -1;
    const int64_t now = UtcNow();
    const auto points = g_demo ? DemoSamples(key, now) : g_store.For(key, now - kRetentionSeconds, now);
    const RuntimeEstimate forecast = EstimateRuntime(points, now);
    return forecast.state == RuntimeState::Ready ? forecast.hoursRemaining : -1;
}

std::wstring RemainingText(const std::wstring& key) {
    const double hours = HoursRemaining(key);
    return hours >= 0 ? FormatRemaining(hours) : L"";
}

std::wstring FormatHours(double hours) { return FormatRemaining(hours); }

double RoughHoursRemaining(const std::wstring& key) {
    if (key.empty()) return -1;
    const int64_t now = UtcNow();
    auto points = g_demo ? DemoSamples(key, now) : g_store.For(key, now - kRetentionSeconds, now);
    if (points.size() < 2) return -1;
    std::stable_sort(points.begin(), points.end(), [](const Sample& a, const Sample& b) { return a.timestamp < b.timestamp; });
    const Sample& latest = points.back();
    if (latest.charging || latest.percent <= 0 || now - latest.timestamp > 24 * 3600) return -1;
    // the current discharge: back from the latest reading while not charging and not rising
    size_t start = points.size() - 1;
    while (start > 0 && !points[start - 1].charging && points[start - 1].percent >= points[start].percent) --start;
    const double hours = (latest.timestamp - points[start].timestamp) / 3600.0;
    const int drop = points[start].percent - latest.percent;
    if (hours < 1.0 || drop < 1) return -1;
    const double perHour = drop / hours;
    const double left = latest.percent / perHour;
    return std::isfinite(left) && left <= 90.0 * 24.0 ? left : -1;
}

std::vector<Point> Recent(const std::wstring& key, int hours) {
    std::vector<Point> out;
    if (key.empty() || hours <= 0) return out;
    const int64_t now = UtcNow();
    const auto points = g_demo ? DemoSamples(key, now) : g_store.For(key, now - (int64_t)hours * 3600, now);
    for (const auto& s : points)
        if (s.timestamp >= now - (int64_t)hours * 3600) out.push_back({ s.timestamp - now, s.percent, s.charging });
    std::sort(out.begin(), out.end(), [](const Point& a, const Point& b) { return a.secondsAgo < b.secondsAgo; });
    return out;
}

std::wstring TimeToEmpty(const std::wstring& key) {
    if (g_demo) return {};
    const int64_t now = UtcNow();
    const RuntimeEstimate forecast = EstimateRuntime(g_store.For(key, now - kRetentionSeconds, now), now);
    if (forecast.state != RuntimeState::Ready || forecast.hoursRemaining <= 0) return {};
    return FormatRemaining(forecast.hoursRemaining);
}

std::wstring EstimatedRuntimeText(const std::wstring& key) {
    if (key.empty()) return {};
    const int64_t now = UtcNow();
    const auto samples = g_demo ? DemoSamples(key, now) : g_store.For(key, now - kRetentionSeconds, now);
    return CompactRuntimeText(EstimateRuntime(samples, now));
}

void Show(HWND owner, const std::wstring& key, const std::wstring& displayName) {
    if (key.empty()) return;
    g_key = key;
    g_name = displayName;
    std::wstring title = std::wstring(T(L"Battery history")) + L" - " + displayName;
    if (g_view && IsWindow(g_view)) {
        SetWindowTextW(g_view, title.c_str());
        ShowWindow(g_view, SW_RESTORE);
        SetForegroundWindow(g_view);
        InvalidateRect(g_view, nullptr, TRUE);
        return;
    }
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = inst;
    wc.lpfnWndProc = ViewProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // WM_PAINT fills the whole window
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    g_days = 7;
    RECT bounds{0, 0, 1020, 710};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);
    g_view = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, bounds.right-bounds.left, bounds.bottom-bounds.top,
                             nullptr, nullptr, inst, nullptr);
    if (!g_view) return;
    ShowWindow(g_view, SW_SHOWNORMAL);
    UpdateWindow(g_view);
    SetForegroundWindow(g_view);
    (void)owner; // viewer is independent of the hidden tray message window
}
const Store& GetStore() { return g_store; }
} // namespace battery_history
