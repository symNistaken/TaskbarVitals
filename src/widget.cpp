// Built twice: as namespace widget (the battery strip) and, from vitals_strip.cpp, as namespace
// vitals_strip (the system vitals strip). Each copy has its own window, state and position.
#ifndef WIDGET_NS
#define WIDGET_NS widget
#endif
#ifndef WIDGET_CLASS
#define WIDGET_CLASS L"WirelessBatteryWidget"
#endif

#include "widget.h"
#include "myfinder_layout.h"
#include "../native/composition_surface.h"
#include "../native/finder_order_guard.h"

#include "i18n.h"

#include <commctrl.h>
#include <uiautomation.h>
#include <algorithm>
#include <climits>
#include <cmath>

namespace {

HWND g_wnd, g_taskbar, g_tip, g_notify, g_rebar;
bool g_cardOn = true, g_chartOn = true;  // hover card and its live/24-hour chart; card off = plain tooltip
UINT g_clickMsg;
bool g_light, g_hover;
bool g_background;  // opaque rounded plate behind the text, for transparent / restyled taskbars
bool g_stacked;     // icon above the percentage
bool g_twoRows;     // compact 2-row grid layout when space is tight
bool g_customIcons = true;      // crisp custom supersampled vector icons instead of font glyphs
bool g_showPercentSign = true;  // '%' after numeric levels
bool g_ringStyle;   // device icon inside a ring that fills with the charge
bool g_animateCharging;  // charging icons fill green in a loop
bool g_connectionIcons;  // wireless mark after 2.4 GHz devices' levels
bool g_vertical;    // the primary taskbar is docked left or right: one block per device, stacked
bool g_makeRoom = true;  // step past other apps' widgets in the taskbar instead of covering them
WIDGET_NS::VerticalPlace g_place = WIDGET_NS::VerticalPlace::Auto;  // where the column sits on such a taskbar
bool g_farLeft;          // at the left end of the taskbar instead of next to the clock
bool g_placedFarLeft;    // the last placement on the main taskbar was at the left end
bool g_farLeftOnly;      // left end or nowhere: hidden when there is no room there (split strips mode)
bool g_beforeApps;       // just left of Start / the first task button, centred taskbar
std::vector<WidgetItem> g_items;
std::wstring g_tipText;
RECT g_lastTray{}, g_lastBar{}, g_lastWidgets{};
int g_lastW = -1, g_lastDpi = 0, g_lastLeft = -1;
int g_anchorLeft = -1;   // where the strip goes with no offset (taskbar client x), from the last Reposition
bool g_pressed = false, g_dragging = false;
POINT g_pressAt{};
int g_grabDx = 0;
SIZE g_size{};
SIZE g_paintedBarClient{};
bool g_painted = false;
HINSTANCE g_inst;
// MyDockFinder host (experimental): the strip is a separate composited popup that follows MyFinder.
bool g_myFinder = false;
CompositionSurface g_composition;
FinderOrderGuard g_orderGuard;
bool g_compositionReady = false;
ULONGLONG g_lastValidFinderPlacement = 0;

struct Mirror {
    HWND bar = nullptr, wnd = nullptr;
    bool hover = false;
    bool painted = false;
    SIZE size{};
    int dpi = 0;
    int lastLeft = INT_MIN;  // client x it was last placed at; INT_MIN = not placed (hidden)
    bool farOk = false;      // the last placement was at the left end
    std::vector<RECT> itemRects;  // each item's area in this strip (for the hover card)
};
std::vector<Mirror> g_mirrors;
std::wstring g_choice = L"all";
bool g_primaryHidden = false;
HWND g_dpiBar;  // while a mirror is painted or placed, Px() uses its taskbar's DPI

const wchar_t* kClass = WIDGET_CLASS;
const wchar_t* kBridgeClass = L"Windows.UI.Composition.DesktopWindowContentBridge";

SRWLOCK g_finderLock = SRWLOCK_INIT;
HWND g_finderHost = nullptr, g_finderResultHost = nullptr;
RECT g_finderRect{};
int g_finderEdge = -1;
myfinder::Foreground g_finderForeground = myfinder::Foreground::Unknown;
ULONGLONG g_finderUpdated = 0;
HANDLE g_finderWake = nullptr;
bool FinderAboveStrip() {
    for (HWND window = GetWindow(g_wnd, GW_HWNDPREV); window; window = GetWindow(window, GW_HWNDPREV))
        if (window == g_taskbar) return true;
    return false;
}

HWINEVENTHOOK g_finderOrderHook = nullptr, g_finderForegroundHook = nullptr;
bool g_finderOrderQueued = false;

void CALLBACK FinderOrderEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (!g_myFinder || !g_wnd || !g_taskbar || g_finderOrderQueued || !FinderAboveStrip()) return;
    g_finderOrderQueued = true;
    HWND above = GetWindow(g_taskbar, GW_HWNDPREV);
    const bool top = (GetWindowLongPtrW(g_taskbar, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    if (!above || ((GetWindowLongPtrW(above, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != top)
        above = top ? HWND_TOPMOST : HWND_TOP;
    SetWindowPos(g_wnd, above, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
    g_finderOrderQueued = false;
}

DWORD WINAPI FinderLayoutThread(LPVOID) {
    DWORD interval = 350;
    ULONGLONG activeUntil = 0;
    for (;;) {
        WaitForSingleObject(g_finderWake, interval);
        interval = 350;
        AcquireSRWLockShared(&g_finderLock);
        HWND host = g_finderHost;
        ReleaseSRWLockShared(&g_finderLock);
        if (!host || !IsWindow(host) || !IsWindowVisible(host)) continue;
        RECT rect{};
        if (!GetWindowRect(host, &rect)) continue;
        POINT cursor{};
        const auto now = GetTickCount64();
        if (GetCursorPos(&cursor) && PtInRect(&rect, cursor)) activeUntil = now + 1000;
        if (now < activeUntil) interval = 100;
        int w = rect.right - rect.left, h = rect.bottom - rect.top;
        if (w < 8 || w > 16384 || h < 4 || h > 256) continue;
        int edge = -1;
        auto foreground = myfinder::Foreground::Unknown;
        HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
        BITMAPINFO info{};
        info.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
        void* bits = nullptr;
        HBITMAP bitmap = mem ? CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        if (bitmap && bits) {
            HGDIOBJ old = SelectObject(mem, bitmap);
            memset(bits, 0, (size_t)w * h * 4);
            if (PrintWindow(host, mem, 2 /* PW_RENDERFULLCONTENT */)) {
                GdiFlush();
                edge = myfinder::GroupLeft(static_cast<const std::uint32_t*>(bits), w, h, std::max(24, h * 2));
                foreground = myfinder::GlyphForeground(static_cast<const std::uint32_t*>(bits), w, h, edge);
            }
            SelectObject(mem, old);
        }
        if (bitmap) DeleteObject(bitmap);
        if (mem) DeleteDC(mem);
        if (screen) ReleaseDC(nullptr, screen);
        AcquireSRWLockExclusive(&g_finderLock);
        if (g_finderHost == host && edge >= 0) {
            g_finderResultHost = host;
            g_finderRect = rect;
            g_finderEdge = edge;
            if (foreground != myfinder::Foreground::Unknown) g_finderForeground = foreground;
            g_finderUpdated = GetTickCount64();
        }
        ReleaseSRWLockExclusive(&g_finderLock);
    }
}

void WatchFinder(HWND host) {
    AcquireSRWLockExclusive(&g_finderLock);
    bool changed = host != g_finderHost;
    g_finderHost = host;
    if (changed) {
        g_finderResultHost = nullptr;
        g_finderEdge = -1;
        g_finderForeground = myfinder::Foreground::Unknown;
        g_finderUpdated = 0;
    }
    ReleaseSRWLockExclusive(&g_finderLock);
    if (host && !g_finderWake) {
        g_finderWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE thread = g_finderWake ? CreateThread(nullptr, 0, FinderLayoutThread, nullptr, 0, nullptr) : nullptr;
        if (thread) CloseHandle(thread);
    }
    if (changed && g_finderWake) SetEvent(g_finderWake);
}

int FinderEdge(HWND host, const RECT& rect) {
    AcquireSRWLockShared(&g_finderLock);
    int edge = g_finderResultHost == host && EqualRect(&rect, &g_finderRect) &&
               (GetTickCount64() - g_finderUpdated < 3500 || IsWindowVisible(g_wnd)) ? g_finderEdge : -1;
    ReleaseSRWLockShared(&g_finderLock);
    return edge;
}
myfinder::Foreground FinderForeground(HWND host) {
    AcquireSRWLockShared(&g_finderLock);
    auto value = g_finderResultHost == host ? g_finderForeground : myfinder::Foreground::Unknown;
    ReleaseSRWLockShared(&g_finderLock);
    return value;
}

int Dpi() {
    RECT finderRect{};  // MyFinder: scale with the bar height (24 px at 100%)
    if (g_myFinder && g_taskbar && GetClientRect(g_taskbar, &finderRect) && finderRect.bottom > 0)
        return MulDiv(std::clamp((int)finderRect.bottom, 8, 128), 96, 24);
    HWND bar = g_dpiBar ? g_dpiBar : g_taskbar;
    UINT d = bar ? GetDpiForWindow(bar) : 0;
    return d ? (int)d : 96;
}
int Px(int v) { return MulDiv(v, Dpi(), 96); }

constexpr int RecoverHorizontalWidgetX(int desired, int widgetWidth, int barWidth,
                                       int trayWidth, int lastGoodX, int gap) {
    const int rightmost = std::max(0, barWidth - widgetWidth);
    if (desired >= 0 && desired <= rightmost) return desired;
    if (lastGoodX >= 0 && lastGoodX <= rightmost) return lastGoodX;
    if (trayWidth > 0 && trayWidth < barWidth) {
        return std::clamp(barWidth - trayWidth - widgetWidth - gap, 0, rightmost);
    }
    return std::clamp(desired, 0, rightmost);
}
static_assert(RecoverHorizontalWidgetX(-61, 59, 1707, 220, 1400, 2) == 1400);
static_assert(RecoverHorizontalWidgetX(-61, 59, 1707, 220, -1, 2) == 1426);
static_assert(RecoverHorizontalWidgetX(1240, 80, 1707, 220, 900, 2) == 1240);
static_assert(RecoverHorizontalWidgetX(1720, 80, 1707, 220, -1, 2) == 1405);
static_assert(RecoverHorizontalWidgetX(-50, 120, 100, 0, -1, 2) == 0);

bool g_reserved;
bool g_fill;
COLORREF g_fillColor;

bool SampleBarColor(const RECT& bar, int screenTop, COLORREF& out) {
    HDC dc = GetDC(nullptr);
    int w = bar.right - bar.left, y = screenTop - Px(4);
    COLORREF c[3];
    for (int i = 0; i < 3; i++) c[i] = GetPixel(dc, bar.left + w * (i + 1) / 4, y);
    ReleaseDC(nullptr, dc);
    auto differs = [](COLORREF a, COLORREF b, int tol) {
        return abs(GetRValue(a) - GetRValue(b)) > tol || abs(GetGValue(a) - GetGValue(b)) > tol ||
               abs(GetBValue(a) - GetBValue(b)) > tol;
    };
    for (COLORREF v : c)
        if (v == CLR_INVALID || differs(v, c[1], 10)) return false;
    out = c[1];
    return true;
}

void ReserveBottom(int reserve) {
    HWND bridge = g_taskbar && IsWindow(g_taskbar) ? FindWindowExW(g_taskbar, nullptr, kBridgeClass, nullptr) : nullptr;
    if (!bridge || (!reserve && !g_reserved)) { g_reserved = false; return; }
    RECT client{}, br{};
    GetClientRect(g_taskbar, &client);
    GetWindowRect(bridge, &br);
    int want = client.bottom - reserve;
    if (want > 0 && br.bottom - br.top != want)
        SetWindowPos(bridge, nullptr, 0, 0, br.right - br.left, want, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    g_reserved = reserve > 0;
}

SRWLOCK g_wbLock = SRWLOCK_INIT;
RECT g_wbRect{};
HWND g_wbRectBar;
HWND g_wbTaskbar;
HANDLE g_wbWake;

struct Span {
    HWND bar = nullptr;
    bool measured = false;
    bool hasClock = false;
    int clockLeft = 0;
    bool hasWidgets = false;
    RECT widgets{};
    bool hasTasks = false;
    int tasksRight = 0;
    bool hasFirst = false;
    int firstLeft = 0;
    bool rtl = false;
    int clockRight = 0;
    bool hasRtlTasks = false;
    int rtlTasksLeft = 0;
};

bool IsVertical(const RECT& r) { return r.bottom - r.top > r.right - r.left; }
int Lo(const RECT& r, bool vertical) { return vertical ? r.top : r.left; }
int Hi(const RECT& r, bool vertical) { return vertical ? r.bottom : r.right; }
std::vector<Span> g_spans;
bool g_wbMirrors;
bool g_wbFarLeft;

IUIAutomationCondition* StringCondition(IUIAutomation* uia, PROPERTYID prop, const wchar_t* value) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(value);
    IUIAutomationCondition* c = nullptr;
    if (v.bstrVal) uia->CreatePropertyCondition(prop, v, &c);
    VariantClear(&v);
    return c;
}

Span MeasureSpan(IUIAutomation* uia, HWND bar) {
    Span s;
    s.bar = bar;
    RECT br{};
    IUIAutomationElement* root = nullptr;
    if (!GetWindowRect(bar, &br) || FAILED(uia->ElementFromHandle(bar, &root)) || !root) return s;
    s.measured = true;
    s.rtl = (GetWindowLongW(bar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    bool v = IsVertical(br);
    auto inBar = [&](const RECT& r) {
        return Hi(r, v) > Lo(r, v) && Lo(r, v) >= Lo(br, v) && Hi(r, v) <= Hi(br, v);
    };
    if (IUIAutomationCondition* clock = StringCondition(uia, UIA_AutomationIdPropertyId, L"SystemTrayIcon")) {
        IUIAutomationElementArray* all = nullptr;
        int n = 0;
        if (SUCCEEDED(root->FindAll(TreeScope_Descendants, clock, &all)) && all && SUCCEEDED(all->get_Length(&n)))
            for (int i = 0; i < n; i++) {
                IUIAutomationElement* el = nullptr;
                RECT r{};
                if (SUCCEEDED(all->GetElement(i, &el)) && el && SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && inBar(r)) {
                    if (!s.hasClock || Lo(r, v) < s.clockLeft) s.clockLeft = Lo(r, v);
                    if (!s.hasClock || r.right > s.clockRight) s.clockRight = r.right;
                    s.hasClock = true;
                }
                if (el) el->Release();
            }
        if (all) all->Release();
        clock->Release();
    }
    if (IUIAutomationCondition* wbCond = StringCondition(uia, UIA_AutomationIdPropertyId, L"WidgetsButton")) {
        IUIAutomationElement* wbEl = nullptr;
        RECT r{};
        if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, wbCond, &wbEl)) && wbEl &&
            SUCCEEDED(wbEl->get_CurrentBoundingRectangle(&r)) && inBar(r)) {
            s.widgets = r;
            s.hasWidgets = true;
        }
        if (wbEl) wbEl->Release();
        wbCond->Release();
    }
    if (IUIAutomationCondition* task = StringCondition(uia, UIA_ClassNamePropertyId, L"Taskbar.TaskListButtonAutomationPeer")) {
        IUIAutomationElementArray* all = nullptr;
        int n = 0;
        if (SUCCEEDED(root->FindAll(TreeScope_Descendants, task, &all)) && all && SUCCEEDED(all->get_Length(&n)))
            for (int i = 0; i < n; i++) {
                IUIAutomationElement* el = nullptr;
                RECT r{};
                if (SUCCEEDED(all->GetElement(i, &el)) && el && SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && inBar(r)) {
                    if (!s.hasFirst || r.left < s.firstLeft) {
                        s.firstLeft = r.left;
                        s.hasFirst = true;
                    }
                    if ((!s.hasClock || Hi(r, v) <= s.clockLeft) && (!s.hasWidgets || Hi(r, v) <= Lo(s.widgets, v)) &&
                        (!s.hasTasks || Hi(r, v) > s.tasksRight)) {
                        s.tasksRight = Hi(r, v);
                        s.hasTasks = true;
                    }
                    if (s.rtl && !v && (!s.hasClock || r.left >= s.clockRight) &&
                        (!s.hasWidgets || r.left >= s.widgets.right) && (!s.hasRtlTasks || r.left < s.rtlTasksLeft)) {
                        s.rtlTasksLeft = r.left;
                        s.hasRtlTasks = true;
                    }
                }
                if (el) el->Release();
            }
        if (all) all->Release();
        task->Release();
    }
    if (IUIAutomationCondition* start = StringCondition(uia, UIA_AutomationIdPropertyId, L"StartButton")) {
        IUIAutomationElement* el = nullptr;
        RECT r{};
        if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, start, &el)) && el &&
            SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && inBar(r) && (!s.hasFirst || r.left < s.firstLeft)) {
            s.firstLeft = r.left;
            s.hasFirst = true;
        }
        if (el) el->Release();
        start->Release();
    }
    root->Release();
    return s;
}

bool IsXamlTaskbar(HWND bar) {
    return FindWindowExW(bar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr) != nullptr;
}

struct Column {
    HWND bar = nullptr;
    bool measured = false;
    bool any = false;
    int top = 0, bottom = 0;
};
Column g_column;

bool IsVerticalBar(HWND bar) {
    RECT r{};
    return GetWindowRect(bar, &r) && r.bottom - r.top > r.right - r.left;
}

Column MeasureColumn(IUIAutomation* uia, HWND bar) {
    Column c;
    c.bar = bar;
    RECT br{}, tr{};
    HWND tray = FindWindowExW(bar, nullptr, L"TrayNotifyWnd", nullptr);
    IUIAutomationElement* root = nullptr;
    if (!tray || !GetWindowRect(tray, &tr) || !GetWindowRect(bar, &br)) return c;
    if (FAILED(uia->ElementFromHandle(bar, &root)) || !root) return c;
    VARIANT none;
    VariantInit(&none);
    none.vt = VT_I4;
    none.lVal = 0;
    IUIAutomationCondition* xaml = nullptr;
    IUIAutomationCacheRequest* req = nullptr;
    IUIAutomationElementArray* all = nullptr;
    int n = 0;
    if (SUCCEEDED(uia->CreatePropertyCondition(UIA_NativeWindowHandlePropertyId, none, &xaml)) && xaml &&
        SUCCEEDED(uia->CreateCacheRequest(&req)) && req &&
        SUCCEEDED(req->AddProperty(UIA_BoundingRectanglePropertyId)) &&
        SUCCEEDED(root->FindAllBuildCache(TreeScope_Descendants, xaml, req, &all)) && all &&
        SUCCEEDED(all->get_Length(&n))) {
        c.measured = true;
        for (int i = 0; i < n; i++) {
            IUIAutomationElement* el = nullptr;
            RECT r{};
            if (SUCCEEDED(all->GetElement(i, &el)) && el && SUCCEEDED(el->get_CachedBoundingRectangle(&r)) &&
                r.bottom > r.top && r.right > r.left && r.left >= br.left && r.right <= br.right && r.top >= br.top && r.bottom <= tr.top) {
                c.top = c.any ? std::min(c.top, (int)r.top) : (int)r.top;
                c.bottom = c.any ? std::max(c.bottom, (int)r.bottom) : (int)r.bottom;
                c.any = true;
            }
            if (el) el->Release();
        }
    }
    if (all) all->Release();
    if (req) req->Release();
    if (xaml) xaml->Release();
    root->Release();
    return c;
}

DWORD WINAPI WidgetsButtonThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* uia = nullptr;
    IUIAutomationElement* btn = nullptr;
    HWND btnBar = nullptr;
    bool column = false;
    for (;;) {
        WaitForSingleObject(g_wbWake, btnBar ? (column ? 1500 : 5000) : INFINITE);
        AcquireSRWLockShared(&g_wbLock);
        HWND bar = g_wbTaskbar;
        ReleaseSRWLockShared(&g_wbLock);
        if (btn && bar != btnBar) { btn->Release(); btn = nullptr; }
        btnBar = bar;
        RECT r{};
        if (bar && IsWindow(bar)) {
            if (!uia)
                CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia);
            if (uia && !btn) {
                IUIAutomationElement* root = nullptr;
                IUIAutomationCondition* cond = nullptr;
                VARIANT id;
                VariantInit(&id);
                id.vt = VT_BSTR;
                id.bstrVal = SysAllocString(L"WidgetsButton");
                if (SUCCEEDED(uia->ElementFromHandle(bar, &root)) && root &&
                    SUCCEEDED(uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &cond)) && cond)
                    root->FindFirst(TreeScope_Descendants, cond, &btn);
                VariantClear(&id);
                if (cond) cond->Release();
                if (root) root->Release();
            }
            if (btn && FAILED(btn->get_CurrentBoundingRectangle(&r))) {
                btn->Release();
                btn = nullptr;
                r = {};
            }
        }
        AcquireSRWLockShared(&g_wbLock);
        bool mirrors = g_wbMirrors, farLeft = g_wbFarLeft;
        ReleaseSRWLockShared(&g_wbLock);
        std::vector<Span> spans;
        if (bar && farLeft && uia && IsWindow(bar) && IsXamlTaskbar(bar)) spans.push_back(MeasureSpan(uia, bar));
        if (bar && mirrors && uia)
            for (HWND t = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); t;
                 t = FindWindowExW(nullptr, t, L"Shell_SecondaryTrayWnd", nullptr))
                if (IsXamlTaskbar(t)) spans.push_back(MeasureSpan(uia, t));
        Column col;
        column = bar && uia && IsWindow(bar) && IsVerticalBar(bar) && IsXamlTaskbar(bar);
        if (column) col = MeasureColumn(uia, bar);
        AcquireSRWLockExclusive(&g_wbLock);
        g_column = col;
        if (bar && g_wbTaskbar == bar) {
            g_wbRect = r;
            g_wbRectBar = bar;
        }
        g_spans = std::move(spans);
        ReleaseSRWLockExclusive(&g_wbLock);
    }
}

void WatchWidgetsButton(HWND taskbar) {
    AcquireSRWLockExclusive(&g_wbLock);
    g_wbTaskbar = taskbar;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (!g_wbWake) {
        g_wbWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE t = g_wbWake ? CreateThread(nullptr, 0, WidgetsButtonThread, nullptr, 0, nullptr) : nullptr;
        if (t) CloseHandle(t);
    }
    if (g_wbWake) SetEvent(g_wbWake);
}

RECT WidgetsButtonRect() {
    AcquireSRWLockShared(&g_wbLock);
    RECT r = g_wbRectBar && g_wbRectBar == g_wbTaskbar ? g_wbRect : RECT{};
    ReleaseSRWLockShared(&g_wbLock);
    return r;
}

bool FarLeftX(HWND bar, int width, int& x) {
    RECT br{};
    if (width <= 0 || !GetWindowRect(bar, &br)) return false;
    bool ok = false;
    AcquireSRWLockShared(&g_wbLock);
    for (const auto& s : g_spans)
        if (s.bar == bar && s.measured && s.hasFirst && g_beforeApps) {
            int edge = s.firstLeft;
            if (s.hasWidgets && s.widgets.right <= s.firstLeft && s.widgets.right + Px(4) >= s.firstLeft - width - Px(4))
                edge = s.widgets.left;
            x = edge - width - Px(4);
            ok = x >= br.left + Px(4);
        } else if (s.bar == bar && s.measured && s.hasFirst) {
            x = br.left + Px(4);
            if (s.hasWidgets && s.widgets.left < x + width + Px(4) && s.widgets.right > x) x = s.widgets.right + Px(4);
            ok = x + width + Px(4) <= s.firstLeft;
            if (!ok && s.hasTasks) {
                x = s.tasksRight + Px(4);
                int limit = s.hasClock ? s.clockLeft : (int)br.right;
                if (s.hasWidgets && s.widgets.left >= x) limit = std::min(limit, (int)s.widgets.left);
                ok = x + width + Px(4) <= limit;
            }
        }
    ReleaseSRWLockShared(&g_wbLock);
    return ok;
}

void ApplyTipTheme() {
    if (!g_tip) return;
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setTheme = ux ? (SetWindowThemeFn)GetProcAddress(ux, "SetWindowTheme") : nullptr;
    if (setTheme) {
        setTheme(g_tip, g_light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        SendMessageW(g_tip, WM_THEMECHANGED, 0, 0);
    }
}

wchar_t Glyph(DeviceKind k, bool bluetooth) {
    switch (k) {
        case DeviceKind::Headset:  return 0xE7F6;
        case DeviceKind::Mouse:    return 0xE962;
        case DeviceKind::Keyboard: return 0xE765;
        case DeviceKind::Gamepad:  return 0xE7FC;
        case DeviceKind::Phone:    return 0xE8EA;
        case DeviceKind::Microphone: return 0xE720;
        case DeviceKind::Speaker:  return 0xE7F5;
        default:                   return bluetooth ? 0xE702 : 0xE772;
    }
}
constexpr wchar_t kBolt = 0xE945;
constexpr wchar_t kCheck = 0xE73E;
constexpr wchar_t kWifi = 0xE701;
constexpr wchar_t kBatteryUnknown = 0xE996;

HFONT MakeFont(int px, int weight, const wchar_t* face, const wchar_t* fallback, int fallbackPx = 0) {
    for (const wchar_t* f : { face, fallback }) {
        int h_px = (f == fallback && fallbackPx > 0) ? fallbackPx : px;
        HFONT h = CreateFontW(-h_px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, f);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(dc, h);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, o);
        DeleteDC(dc);
        if (!_wcsicmp(got, f) || f == fallback) return h;
        DeleteObject(h);
    }
    return nullptr;
}

struct Mask {
    int w = 0, h = 0;
    std::vector<BYTE> a;
};

Mask CropAlpha(int W, int H, const std::vector<BYTE>& buf) {
    Mask m;
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (buf[(size_t)y * W + x]) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    if (x1 >= 0) {
        m.w = x1 - x0 + 1;
        m.h = y1 - y0 + 1;
        m.a.resize((size_t)m.w * m.h);
        for (int y = 0; y < m.h; y++)
            for (int x = 0; x < m.w; x++)
                m.a[(size_t)y * m.w + x] = buf[(size_t)(y0 + y) * W + x0 + x];
    }
    return m;
}

Mask RenderMask(const std::wstring& s, HFONT font, bool darkOnLight) {
    Mask m;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ of = SelectObject(dc, font);
    SIZE sz;
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    int W = sz.cx + 8, H = sz.cy + 8;
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ ob = SelectObject(dc, bmp);
    memset(bits, darkOnLight ? 0xFF : 0x00, (size_t)W * H * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, darkOnLight ? RGB(0, 0, 0) : RGB(255, 255, 255));
    TextOutW(dc, 4, 4, s.c_str(), (int)s.size());
    GdiFlush();
    auto* p = static_cast<DWORD*>(bits);
    auto cover = [&](DWORD v) -> BYTE {
        int c = (int)((v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF)) / 3;
        return (BYTE)(darkOnLight ? 255 - c : c);
    };
    std::vector<BYTE> raw((size_t)W * H);
    for (int i = 0; i < W * H; i++) raw[i] = cover(p[i]);
    SelectObject(dc, ob);
    SelectObject(dc, of);
    DeleteObject(bmp);
    DeleteDC(dc);
    return CropAlpha(W, H, raw);
}

Mask RenderVectorMask(vector_icons::VectorIcon vi, int pxSize) {
    vector_icons::IconMask im = vector_icons::Render(vi, pxSize);
    if (im.w <= 0 || im.h <= 0) return {};
    return CropAlpha(im.w, im.h, im.a);
}

Mask RenderItemIconMask(const WidgetItem& it, int vectorSizePx, HFONT fallbackFont, bool darkOnLight) {
    if (g_customIcons) {
        vector_icons::VectorIcon vi = it.vectorIcon != vector_icons::VectorIcon::None
                                      ? it.vectorIcon
                                      : vector_icons::ForDevice(it.kind, it.bluetooth);
        Mask vm = RenderVectorMask(vi, vectorSizePx);
        if (vm.w > 0 && vm.h > 0) return vm;
    }
    wchar_t gl = it.glyph ? it.glyph : Glyph(it.kind, it.bluetooth);
    return RenderMask(std::wstring(1, gl), fallbackFont, darkOnLight);
}

struct Rgb { int r, g, b; };

struct Canvas {
    int w, h;
    std::vector<DWORD> px;  // premultiplied BGRA
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, 0) {}
    void Blend(int x, int y, Rgb c, int a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        DWORD d = px[y * w + x];
        int inv = 255 - a;
        int da = d >> 24, dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
        int oa = a + da * inv / 255;
        int orr = c.r * a / 255 + dr * inv / 255;
        int og = c.g * a / 255 + dg * inv / 255;
        int ob = c.b * a / 255 + db * inv / 255;
        px[y * w + x] = ((DWORD)oa << 24) | ((DWORD)orr << 16) | ((DWORD)og << 8) | (DWORD)ob;
    }
    void Draw(const Mask& m, int x, int y, Rgb c, int alpha, int rowsTo = -1, int rowsFrom = 0) {
        if (rowsTo < 0 || rowsTo > m.h) rowsTo = m.h;
        for (int j = std::max(0, rowsFrom); j < rowsTo; j++)
            for (int i = 0; i < m.w; i++) Blend(x + i, y + j, c, m.a[j * m.w + i] * alpha / 255);
    }
};

Mask Halo(const Mask& m, int r) {
    Mask g;
    g.w = m.w + 2 * r;
    g.h = m.h + 2 * r;
    std::vector<int> a((size_t)g.w * g.h, 0), tmp(a.size(), 0);
    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++) a[(y + r) * g.w + x + r] = m.a[y * m.w + x];
    int n = 2 * r + 1;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int s = 0;
                for (int k = -r; k <= r; k++) if (x + k >= 0 && x + k < g.w) s += a[y * g.w + x + k];
                tmp[y * g.w + x] = s / n;
            }
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int s = 0;
                for (int k = -r; k <= r; k++) if (y + k >= 0 && y + k < g.h) s += tmp[(y + k) * g.w + x];
                a[y * g.w + x] = s / n;
            }
    }
    g.a.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) g.a[i] = (BYTE)std::min(255, a[i] * 3);
    return g;
}

struct Piece {
    Mask m;
    Rgb color;
    int alpha;
    int gapAfter;
    int anchor = -1;
    int bx = 0, by = 0;
    int barPct = -1;
    Rgb barColor{};
    Mask halo;
    int haloR = 0, haloAlpha = 0;
    int px = INT_MIN, py = 0;
    bool charging = false;
    int item = -1;
    int row = 0;  // 0 or 1 in 2-row compact mode
};

void RingMasks(int d, double stroke, double fraction, Mask& track, Mask& arc) {
    const double kTwoPi = 6.283185307179586;
    track.w = track.h = arc.w = arc.h = d;
    track.a.assign((size_t)d * d, 0);
    arc.a.assign((size_t)d * d, 0);
    double R = d / 2.0, inner = R - stroke;
    for (int y = 0; y < d; y++)
        for (int x = 0; x < d; x++) {
            int t = 0, a = 0;
            for (int sy = 0; sy < 3; sy++)
                for (int sx = 0; sx < 3; sx++) {
                    double px = x + (sx + 0.5) / 3 - R, py = y + (sy + 0.5) / 3 - R;
                    double r = std::sqrt(px * px + py * py);
                    if (r > R || r < inner) continue;
                    t++;
                    double ang = std::atan2(px, -py);
                    if (ang < 0) ang += kTwoPi;
                    if (ang <= fraction * kTwoPi) a++;
                }
            track.a[(size_t)y * d + x] = (BYTE)(t * 255 / 9);
            arc.a[(size_t)y * d + x] = (BYTE)(a * 255 / 9);
        }
}

std::vector<Piece> Layout(int barW, int barH, int& width, int& height) {
    Rgb fg = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    Rgb red = g_light ? Rgb{ 196, 30, 40 } : Rgb{ 255, 90, 90 };
    Rgb warm = g_light ? Rgb{ 205, 110, 0 } : Rgb{ 255, 170, 50 };
    Rgb charge = g_light ? Rgb{ 0, 120, 212 } : Rgb{ 96, 205, 255 };
    Rgb full = g_light ? Rgb{ 16, 124, 16 } : Rgb{ 108, 203, 95 };

    bool vertical = barH > barW;
    const bool twoRows = g_twoRows && !vertical && g_items.size() >= 2 && barH >= Px(34);
    const bool ring = g_ringStyle && !g_stacked && !twoRows && !vertical;

    int iconPx = twoRows ? Px(12) : Px(16);
    int boltPx = twoRows ? Px(9) : Px(11);
    int textPx = twoRows ? Px(11) : Px(13);
    int textFallbackPx = twoRows ? Px(10) : Px(12);
    int capPx = twoRows ? std::max(7, Px(6)) : std::max(8, Px(7));

    HFONT icons = MakeFont(iconPx, FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT bolt = MakeFont(boltPx, FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT text = MakeFont(textPx, FW_NORMAL, L"Segoe UI Variable Text", L"Segoe UI", textFallbackPx);
    HFONT caption = MakeFont(capPx, FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");
    HFONT ringIcons = ring ? MakeFont(Px(11), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets") : nullptr;

    std::vector<Piece> out;
    struct Slot { int icon, label, bolt, caption; };
    std::vector<Slot> slots;
    if (g_items.empty()) {
        Mask pm = g_customIcons ? RenderVectorMask(vector_icons::VectorIcon::BatteryUnknown, Px(16))
                                : RenderMask(std::wstring(1, kBatteryUnknown), icons, g_light);
        out.push_back({ std::move(pm), fg, 110, 0 });
    }
    for (size_t i = 0; i < g_items.size(); i++) {
        const auto& it = g_items[i];
        int a = it.dim ? 120 : 255;
        if (ring && !it.isVital && it.percent >= 0) {
            int d = Px(28);
            d += d & 1;
            Mask track, arc;
            RingMasks(d, std::max(2.0, d / 13.0), it.percent / 100.0, track, arc);
            bool branded = it.accent != Accent::None;
            BrandColor bc = AccentColor(it.accent, g_light);
            Rgb accent{ bc.r, bc.g, bc.b };
            Rgb arcColor = it.charging ? charge : it.low ? red : branded ? accent : fg;
            Rgb iconColor = (branded && !it.dim) ? accent : fg;
            int base = (int)out.size();
            out.push_back({ std::move(track), fg, a * (g_light ? 45 : 60) / 255, i + 1 < g_items.size() ? Px(10) : 0 });
            Piece arcPiece{ std::move(arc), arcColor, a, 0 };
            arcPiece.anchor = base;
            out.push_back(std::move(arcPiece));
            Piece glyph{ RenderItemIconMask(it, std::max(8, Px(11)), ringIcons, g_light), iconColor, a, 0 };
            glyph.anchor = base;
            glyph.bx = (d - glyph.m.w) / 2;
            glyph.by = (d - glyph.m.h) / 2;
            out.push_back(std::move(glyph));
            for (size_t k = (size_t)base; k < out.size(); k++) out[k].item = (int)i;
            continue;
        }
        Rgb tc = (it.low || it.heat >= 2) ? red : (it.heat == 1 ? warm : fg);
        bool mark = it.charging || it.full;
        bool link = !mark && g_connectionIcons && it.wirelessLink;
        size_t glyph = out.size();
        int capIdx = -1;
        slots.push_back({ (int)glyph, (int)glyph + 1, mark || link ? (int)glyph + 2 : -1, -1 });
        bool branded = it.accent != Accent::None;
        BrandColor bc = AccentColor(it.accent, g_light);
        Rgb accent{ bc.r, bc.g, bc.b };
        Piece icon{ RenderItemIconMask(it, twoRows ? Px(12) : Px(16), icons, g_light), branded ? accent : fg, a, Px(twoRows ? 4 : 5) };
        if (branded && !it.dim && !twoRows) {
            icon.haloR = Px(3);
            icon.halo = Halo(icon.m, icon.haloR);
            icon.haloAlpha = g_light ? 60 : 140;
        }
        icon.charging = g_animateCharging && it.charging && !it.full;
        out.push_back(std::move(icon));
        std::wstring t = !it.text.empty() ? it.text : it.percent >= 0 ? std::to_wstring(it.percent) + (g_showPercentSign ? L"%" : L"") : L"?";
        Piece label{ RenderMask(t, text, g_light), tc, a, mark || link ? Px(2) : 0 };
        if (branded && it.percent >= 0) {
            label.barPct = it.percent;
            label.barColor = (it.low || it.heat >= 2) ? red : (it.heat == 1 ? warm : accent);
        }
        out.push_back(std::move(label));
        if (mark) out.push_back({ RenderMask(std::wstring(1, it.full ? kCheck : kBolt), bolt, g_light), it.full ? full : charge, a, 0 });
        else if (link) out.push_back({ RenderMask(std::wstring(1, kWifi), bolt, g_light), fg, a * 160 / 255, 0 });
        out.back().gapAfter = i + 1 < g_items.size() ? Px(twoRows ? 10 : 14) : 0;
        if (!it.label.empty()) {
            Piece c{ RenderMask(it.label, caption, g_light), fg, a, 0 };
            c.anchor = (int)glyph;
            c.bx = out[glyph].m.w - c.m.w + Px(3);
            c.by = out[glyph].m.h - c.m.h + Px(3);
            capIdx = (int)out.size();
            out.push_back(c);
        }
        slots.back().caption = capIdx;
        for (size_t k = glyph; k < out.size(); k++) out[k].item = (int)i;
    }
    DeleteObject(icons);
    DeleteObject(bolt);
    DeleteObject(text);
    DeleteObject(caption);
    if (ringIcons) DeleteObject(ringIcons);

    height = barH;
    const int rowGap = Px(2);
    const int barRoom = (twoRows ? Px(2) : Px(3)) + std::max(2, Px(2));
    int iconH = 0, textH = 0;
    bool hasBar = false;
    for (const Slot& s : slots) {
        iconH = std::max(iconH, out[s.icon].m.h);
        textH = std::max(textH, out[s.label].m.h);
        hasBar |= out[s.label].barPct >= 0;
    }
    const int stackedBarRoom = hasBar ? barRoom : 0;
    const int stackedPadding = Px(6);
    bool stacked = !vertical && g_stacked && !twoRows && !slots.empty() &&
                   barH >= iconH + rowGap + textH + stackedBarRoom + 2 * stackedPadding;
    if (vertical) {
        width = barW;
        const int pad = Px(10), blockGap = Px(12), iconGap = Px(3);
        int y = pad;
        if (slots.empty() && g_items.empty()) {
            Piece& ic = out[0];
            ic.px = (barW - ic.m.w) / 2;
            ic.py = y;
            height = y + ic.m.h + pad;
            return out;
        }
        if (slots.empty()) {
            // Vertical fallback if ring style was off/skipped unexpectedly
            width = barW;
            height = Px(24);
            return out;
        }
        for (const Slot& s : slots) {
            Piece& ic = out[s.icon];
            Piece& lb = out[s.label];
            int boltW = s.bolt >= 0 ? Px(2) + out[s.bolt].m.w : 0;
            ic.px = (barW - ic.m.w) / 2;
            ic.py = y;
            y += ic.m.h + iconGap;
            lb.px = (barW - lb.m.w - boltW) / 2;
            lb.py = y;
            if (s.bolt >= 0) {
                out[s.bolt].px = lb.px + lb.m.w + Px(2);
                out[s.bolt].py = y + (lb.m.h - out[s.bolt].m.h) / 2;
            }
            y += lb.m.h + (lb.barPct >= 0 ? barRoom : 0) + blockGap;
        }
        height = y - blockGap + pad;
        return out;
    }
    if (twoRows && slots.size() >= 2) {
        const size_t topCount = (slots.size() + 1) / 2;
        const int cellGap = Px(10);
        const int rowItemH = std::max(iconH, textH + stackedBarRoom);
        const int totalH = 2 * rowItemH + Px(2);
        const int topPad = std::max(Px(2), (barH - totalH) / 2);

        int rowW[2] = { 0, 0 };
        for (size_t idx = 0; idx < slots.size(); ++idx) {
            int r = idx < topCount ? 0 : 1;
            const Slot& s = slots[idx];
            int slotW = out[s.icon].m.w + out[s.icon].gapAfter + out[s.label].m.w;
            if (s.bolt >= 0) slotW += out[s.label].gapAfter + out[s.bolt].m.w;
            if (rowW[r] > 0) rowW[r] += cellGap;
            rowW[r] += slotW;
        }
        int maxRowW = std::max(rowW[0], rowW[1]);
        int xs[2] = { Px(8), Px(8) };
        for (size_t idx = 0; idx < slots.size(); ++idx) {
            int r = idx < topCount ? 0 : 1;
            const Slot& s = slots[idx];
            int rowTop = topPad + r * (rowItemH + Px(2));
            Piece& ic = out[s.icon];
            Piece& lb = out[s.label];
            ic.row = lb.row = r;
            ic.px = xs[r];
            ic.py = rowTop + (rowItemH - ic.m.h) / 2;
            int xCur = ic.px + ic.m.w + ic.gapAfter;
            lb.px = xCur;
            lb.py = rowTop + (rowItemH - lb.m.h - (lb.barPct >= 0 ? barRoom : 0)) / 2;
            xCur += lb.m.w;
            if (s.bolt >= 0) {
                Piece& blt = out[s.bolt];
                blt.row = r;
                blt.px = xCur + lb.gapAfter;
                blt.py = rowTop + (rowItemH - blt.m.h) / 2;
                xCur = blt.px + blt.m.w;
            }
            if (s.caption >= 0) out[s.caption].row = r;
            xs[r] = xCur + cellGap;
        }
        width = Px(8) * 2 + maxRowW;
        return out;
    }
    if (stacked && !slots.empty()) {
        int top = (barH - (iconH + rowGap + textH + stackedBarRoom)) / 2;
        int x = Px(8);
        for (const Slot& s : slots) {
            Piece& ic = out[s.icon];
            Piece& lb = out[s.label];
            int boltW = s.bolt >= 0 ? Px(2) + out[s.bolt].m.w : 0;
            int slotW = std::max(ic.m.w + boltW, lb.m.w);
            ic.px = x + (slotW - ic.m.w - boltW) / 2;
            ic.py = top + (iconH - ic.m.h) / 2;
            if (s.bolt >= 0) {
                out[s.bolt].px = ic.px + ic.m.w + Px(2);
                out[s.bolt].py = top + (iconH - out[s.bolt].m.h) / 2;
            }
            lb.px = x + (slotW - lb.m.w) / 2;
            lb.py = top + iconH + rowGap;
            x += slotW + Px(10);
        }
        width = x - Px(10) + Px(8);
        return out;
    }
    width = Px(8) * 2;
    for (auto& p : out)
        if (p.anchor < 0) width += p.m.w + p.gapAfter;
    return out;
}

std::vector<RECT> g_itemRects;

bool PaintStrip(HWND wnd, HWND taskbar, bool hover, SIZE& size, std::vector<RECT>& itemRects) {
    if (!wnd || !taskbar || !IsWindow(wnd) || !IsWindow(taskbar)) return false;
    RECT bar{};
    if (!GetClientRect(taskbar, &bar)) return false;
    int W = 0, H = 0;
    auto pieces = Layout(bar.right - bar.left, bar.bottom - bar.top, W, H);
    if (W <= 0 || H <= 0) return false;
    Canvas cv(W, H);

    DWORD clear = 0x01000000;
    if (wnd == g_wnd && g_fill)
        clear = 0xFF000000 | ((DWORD)GetRValue(g_fillColor) << 16) | ((DWORD)GetGValue(g_fillColor) << 8) |
                GetBValue(g_fillColor);
    if (g_myFinder && wnd == g_wnd) clear = 0;
    for (auto& p : cv.px) p = clear;
    auto roundRect = [&](int inset, int r, Rgb c, int a) {
        for (int y = inset; y < H - inset; y++)
            for (int x = 0; x < W; x++) {
                int dx = x < r ? r - x : (x >= W - r ? x - (W - r - 1) : 0);
                int dy = y < inset + r ? inset + r - y : (y >= H - inset - r ? y - (H - inset - r - 1) : 0);
                if (dx * dx + dy * dy <= r * r) cv.Blend(x, y, c, a);
            }
    };
    Rgb barColor = g_light ? Rgb{ 243, 243, 243 } : Rgb{ 32, 32, 32 };
    if (g_background && !g_myFinder) roundRect(Px(4), Px(6), barColor, 240);
    if (hover && !g_myFinder) roundRect(Px(4), Px(4), g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }, g_light ? 20 : 26);

    int x = Px(8);
    Rgb track = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    std::vector<int> xs(pieces.size()), ys(pieces.size());
    bool anyTwoRow = false;
    for (size_t i = 0; i < pieces.size(); i++) {
        const Piece& p = pieces[i];
        if (p.row > 0) anyTwoRow = true;
        if (p.anchor < 0) {
            bool fixed = p.px != INT_MIN;
            xs[i] = fixed ? p.px : x;
            ys[i] = fixed ? p.py : (H - p.m.h) / 2;
            if (p.haloR) cv.Draw(p.halo, xs[i] - p.haloR, ys[i] - p.haloR, p.color, p.haloAlpha * p.alpha / 255);
            if (p.barPct >= 0) {
                int bh = std::max(2, Px(2)), gap = p.row > 0 || (g_twoRows && anyTwoRow) ? Px(2) : Px(3);
                if (!fixed) ys[i] = (H - (p.m.h + gap + bh)) / 2;
                int by = ys[i] + p.m.h + gap;
                int fill = std::max(p.barPct > 0 ? 1 : 0, (p.barPct * p.m.w + 50) / 100);
                for (int yy = by; yy < by + bh; yy++)
                    for (int xx = 0; xx < p.m.w; xx++)
                        cv.Blend(xs[i] + xx, yy, xx < fill ? p.barColor : track, xx < fill ? p.alpha : 45 * p.alpha / 255);
            }
            if (!fixed) x += p.m.w + p.gapAfter;
        } else {
            xs[i] = xs[p.anchor] + p.bx;
            ys[i] = ys[p.anchor] + p.by;
            for (int oy = -1; oy <= 1; oy++)
                for (int ox = -1; ox <= 1; ox++)
                    if (ox || oy) cv.Draw(p.m, xs[i] + ox, ys[i] + oy, barColor, 255);
        }
        if (p.charging) {
            Rgb green = g_light ? Rgb{ 0, 160, 40 } : Rgb{ 30, 230, 80 };
            double t = (GetTickCount64() % 2400) / 2400.0;
            int head = t < 0.5 ? (int)(p.m.h * t * 2) : p.m.h;
            int tail = t < 0.5 ? 0 : (int)(p.m.h * (t - 0.5) * 2);
            cv.Draw(p.m, xs[i], ys[i], p.color, p.alpha * 80 / 100);
            cv.Draw(p.m, xs[i], ys[i], green, p.alpha, p.m.h - tail, p.m.h - head);
        } else {
            cv.Draw(p.m, xs[i], ys[i], p.color, p.alpha);
        }
    }

    itemRects.assign(g_items.size(), RECT{ INT_MAX, INT_MAX, INT_MIN, INT_MIN });
    std::vector<int> itemRow(g_items.size(), 0);
    for (size_t i = 0; i < pieces.size(); i++) {
        int k = pieces[i].item;
        if (k < 0 || k >= (int)itemRects.size()) continue;
        itemRow[k] = pieces[i].row;
        RECT& r = itemRects[k];
        r.left = std::min(r.left, (LONG)xs[i]);
        r.top = std::min(r.top, (LONG)ys[i]);
        r.right = std::max(r.right, (LONG)(xs[i] + pieces[i].m.w));
        r.bottom = std::max(r.bottom, (LONG)(ys[i] + pieces[i].m.h));
    }
    if (anyTwoRow && !g_vertical) {
        for (int rowIdx = 0; rowIdx <= 1; ++rowIdx) {
            std::vector<size_t> rowItems;
            for (size_t k = 0; k < itemRects.size(); ++k)
                if (itemRects[k].left <= itemRects[k].right && itemRow[k] == rowIdx) rowItems.push_back(k);
            for (size_t m = 0; m < rowItems.size(); ++m) {
                RECT& r = itemRects[rowItems[m]];
                r.top = rowIdx == 0 ? 0 : H / 2;
                r.bottom = rowIdx == 0 ? H / 2 : H;
                if (m == 0) r.left = 0;
                else r.left = (itemRects[rowItems[m - 1]].right + r.left) / 2;
            }
            for (size_t m = 0; m + 1 < rowItems.size(); ++m)
                itemRects[rowItems[m]].right = itemRects[rowItems[m + 1]].left;
            if (!rowItems.empty()) itemRects[rowItems.back()].right = W;
        }
    } else {
        for (size_t k = 0; k < itemRects.size(); k++) {
            RECT& r = itemRects[k];
            if (r.left > r.right) { r = RECT{}; continue; }
            if (g_vertical) {
                r.left = 0;
                r.right = W;
                r.top = k == 0 ? 0 : (itemRects[k - 1].bottom + r.top) / 2;
            } else {
                r.top = 0;
                r.bottom = H;
                r.left = k == 0 ? 0 : (itemRects[k - 1].right + r.left) / 2;
            }
        }
        for (size_t k = 0; k + 1 < itemRects.size(); k++) {
            if (g_vertical) itemRects[k].bottom = itemRects[k + 1].top;
            else itemRects[k].right = itemRects[k + 1].left;
        }
        if (!itemRects.empty()) {
            if (g_vertical) itemRects.back().bottom = H;
            else itemRects.back().right = W;
        }
    }

    if (g_myFinder && wnd == g_wnd) {
        if (!g_compositionReady) return false;
        if (!g_composition.Paint(cv.px.data(), W, H, W * 4)) {
            g_compositionReady = g_composition.Initialize(g_wnd) && g_composition.Paint(cv.px.data(), W, H, W * 4);
            if (!g_compositionReady) {
                ShowWindow(g_wnd, SW_HIDE);
                return false;
            }
        }
        size = { W, H };
        g_lastW = -1;
        return true;
    }
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    if (!mem) {
        ReleaseDC(nullptr, screen);
        return false;
    }
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return false;
    }
    memcpy(bits, cv.px.data(), (size_t)W * H * 4);
    HGDIOBJ ob = SelectObject(mem, bmp);
    SIZE sz{ W, H };
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    bool updated = UpdateLayeredWindow(wnd, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA) != FALSE;
    if (updated) size = { W, H };
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return updated;
}

void Paint() {
    if (!g_wnd) return;
    RECT client{};
    g_painted = PaintStrip(g_wnd, g_taskbar, g_hover, g_size, g_itemRects);
    if (g_painted && GetClientRect(g_taskbar, &client)) {
        g_paintedBarClient = { client.right - client.left, client.bottom - client.top };
    } else {
        g_size = {};
        g_paintedBarClient = {};
    }
    g_lastW = -1;
}

void PaintMirror(Mirror& m) {
    g_dpiBar = m.bar;
    m.dpi = Dpi();
    m.painted = PaintStrip(m.wnd, m.bar, m.hover, m.size, m.itemRects);
    if (!m.painted) m.size = {};
    g_dpiBar = nullptr;
    m.lastLeft = INT_MIN;
}

void HideMirror(Mirror& m) {
    if (m.lastLeft != INT_MIN || IsWindowVisible(m.wnd)) ShowWindow(m.wnd, SW_HIDE);
    m.lastLeft = INT_MIN;
}

bool Shown(HWND w) { return (GetWindowLongW(w, GWL_STYLE) & WS_VISIBLE) != 0; }

void PositionMirror(Mirror& m) {
    RECT bar{};
    if (!GetWindowRect(m.bar, &bar)) { HideMirror(m); return; }
    bool v = IsVertical(bar);
    const bool rtl = (GetWindowLongW(m.bar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    if (rtl && v) { HideMirror(m); return; }
    g_dpiBar = m.bar;
    if (!m.painted || Dpi() != m.dpi || m.size.cx <= 0 ||
        (v ? m.size.cx != bar.right - bar.left : m.size.cy != bar.bottom - bar.top)) {
        PaintMirror(m);
        g_dpiBar = m.bar;
    }
    if (!m.painted || m.size.cx <= 0 || m.size.cy <= 0) {
        g_dpiBar = nullptr;
        HideMirror(m);
        return;
    }
    int length = v ? m.size.cy : m.size.cx;
    int right = 0, minLeft = Lo(bar, v);
    bool known = false;
    if (rtl) {
        int edge = 0, limit = bar.right;
        HWND rtray = FindWindowExW(m.bar, nullptr, L"TrayNotifyWnd", nullptr);
        RECT rt{};
        if (rtray && IsWindowVisible(rtray) && GetWindowRect(rtray, &rt) && rt.right > rt.left && rt.left >= bar.left &&
            rt.right < bar.right && rt.bottom > bar.top && rt.top < bar.bottom) {
            edge = rt.right;
            known = true;
        } else if (!IsXamlTaskbar(m.bar)) {
            HWND clock = FindWindowExW(m.bar, nullptr, L"ClockButton", nullptr);
            RECT cr{};
            edge = clock && IsWindowVisible(clock) && GetWindowRect(clock, &cr) ? cr.right : bar.left + Px(8);
            for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
                HWND feeds = FindWindowExW(m.bar, nullptr, cls, nullptr);
                RECT fr{};
                if (feeds && IsWindowVisible(feeds) && GetWindowRect(feeds, &fr) && fr.right > edge && fr.right > fr.left)
                    edge = fr.right;
            }
            known = true;
        } else {
            AcquireSRWLockShared(&g_wbLock);
            for (const auto& s : g_spans)
                if (s.bar == m.bar && s.measured && s.rtl) {
                    edge = s.hasClock ? s.clockRight : bar.left + Px(8);
                    if (s.hasWidgets && s.widgets.right > edge && s.widgets.left < edge + length + Px(2))
                        edge = s.widgets.right;
                    if (s.hasRtlTasks) limit = s.rtlTasksLeft - Px(4);
                    known = true;
                }
            ReleaseSRWLockShared(&g_wbLock);
        }
        DWORD off = 0, osz = sizeof(off);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &off,
                         &osz) == ERROR_SUCCESS)
            edge += Px((int)off);
        int left = edge + Px(2);
        g_dpiBar = nullptr;
        if (g_farLeftOnly || !known || length <= 0 || left + length > limit) { HideMirror(m); return; }
        POINT p{ left + length, bar.top };
        ScreenToClient(m.bar, &p);
        int along = p.x;
        bool covered = GetWindow(m.wnd, GW_HWNDPREV) != nullptr;
        if (!covered && Shown(m.wnd) && along == m.lastLeft) return;
        m.lastLeft = along;
        SetWindowPos(m.wnd, HWND_TOP, p.x, 0, m.size.cx, m.size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }
    HWND tray = FindWindowExW(m.bar, nullptr, L"TrayNotifyWnd", nullptr);
    RECT tr{};
    if (tray && IsWindowVisible(tray) && GetWindowRect(tray, &tr) &&
        Lo(tr, v) > Lo(bar, v) && Hi(tr, v) <= Hi(bar, v) && Hi(tr, v) > Lo(tr, v) &&
        Hi(tr, !v) > Lo(bar, !v) && Lo(tr, !v) < Hi(bar, !v)) {
        right = Lo(tr, v);
        known = true;
    } else if (!IsXamlTaskbar(m.bar)) {
        HWND clock = FindWindowExW(m.bar, nullptr, L"ClockButton", nullptr);
        RECT cr{};
        right = clock && IsWindowVisible(clock) && GetWindowRect(clock, &cr) ? Lo(cr, v) : Hi(bar, v) - Px(8);
        for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
            HWND feeds = FindWindowExW(m.bar, nullptr, cls, nullptr);
            RECT fr{};
            if (feeds && IsWindowVisible(feeds) && GetWindowRect(feeds, &fr) && Lo(fr, v) < right && Hi(fr, v) > Lo(fr, v))
                right = Lo(fr, v);
        }
        known = true;
    } else {
        AcquireSRWLockShared(&g_wbLock);
        for (const auto& s : g_spans)
            if (s.bar == m.bar && s.measured) {
                right = s.hasClock ? s.clockLeft : Hi(bar, v) - Px(8);
                if (s.hasWidgets && Lo(s.widgets, v) < right && Hi(s.widgets, v) > right - length - Px(2))
                    right = Lo(s.widgets, v);
                if (s.hasTasks) minLeft = s.tasksRight + Px(4);
                known = true;
            }
        ReleaseSRWLockShared(&g_wbLock);
    }
    DWORD offset = 0, sz = sizeof(offset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset, &sz) ==
        ERROR_SUCCESS)
        right -= Px((int)offset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"SecondaryTaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset, &sz) ==
        ERROR_SUCCESS)
        right -= Px((int)offset);
    int left = right - length - Px(2), farX = 0;
    bool atFarLeft = !v && (g_farLeft || g_beforeApps) && FarLeftX(m.bar, m.size.cx, farX);
    if (atFarLeft) left = farX;
    m.farOk = atFarLeft;
    g_dpiBar = nullptr;
    if (g_farLeftOnly && !atFarLeft) { HideMirror(m); return; }
    if (!atFarLeft && (!known || length <= 0 || left < minLeft)) { HideMirror(m); return; }
    POINT p = v ? POINT{ bar.left, left } : POINT{ left, bar.top };
    ScreenToClient(m.bar, &p);
    int along = v ? p.y : p.x;
    bool covered = GetWindow(m.wnd, GW_HWNDPREV) != nullptr;
    if (!covered && Shown(m.wnd) && along == m.lastLeft) return;
    m.lastLeft = along;
    RECT client{};
    GetClientRect(m.bar, &client);
    int x = v ? (int)std::max<LONG>(0, (client.right - m.size.cx) / 2) : p.x;
    SetWindowPos(m.wnd, HWND_TOP, x, v ? p.y : 0, m.size.cx, m.size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void SetSummary(HWND w) {
    const wchar_t* text = g_tipText.empty() ? L"Taskbar Vitals" : g_tipText.c_str();
    SetWindowTextW(w, text);
    if (!g_tip) return;
    TTTOOLINFOW ti{ sizeof(ti) };
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g_notify;
    ti.uId = (UINT_PTR)w;
    ti.lpszText = const_cast<wchar_t*>(text);
    TTTOOLINFOW probe{ sizeof(probe) };
    probe.hwnd = g_notify;
    probe.uId = (UINT_PTR)w;
    bool known = SendMessageW(g_tip, TTM_GETTOOLINFOW, 0, (LPARAM)&probe) != 0;
    SendMessageW(g_tip, known ? TTM_UPDATETIPTEXTW : TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void DestroyMirror(Mirror& m) {
    if (g_tip) {
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.hwnd = g_notify;
        ti.uId = (UINT_PTR)m.wnd;
        SendMessageW(g_tip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    if (m.wnd && IsWindow(m.wnd)) DestroyWindow(m.wnd);
    m.wnd = nullptr;
}

void SetMeasureMirrors(bool on) {
    AcquireSRWLockExclusive(&g_wbLock);
    bool changed = g_wbMirrors != on;
    g_wbMirrors = on;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (changed && on && g_wbWake) SetEvent(g_wbWake);
}

std::vector<HWND> SecondaryTaskbars() {
    std::vector<HWND> out;
    for (HWND t = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); t;
         t = FindWindowExW(nullptr, t, L"Shell_SecondaryTrayWnd", nullptr)) {
        if (IsWindowVisible(t)) out.push_back(t);
    }
    return out;
}

std::wstring MonitorId(HWND bar) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(bar, MONITOR_DEFAULTTONULL);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return L"";
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME) && dd.DeviceID[0]) return dd.DeviceID;
    return mi.szDevice;
}

std::wstring MonitorModel(const std::wstring& id) {
    size_t a = id.find(L"DISPLAY#");
    if (a == std::wstring::npos) return L"";
    a += 8;
    size_t b = id.find(L'#', a);
    return b == std::wstring::npos ? L"" : id.substr(a, b - a);
}

std::wstring MonitorLabel(HWND bar) {
    MONITORINFOEXW mi{}, pm{};
    mi.cbSize = pm.cbSize = sizeof(MONITORINFOEXW);
    HMONITOR mon = MonitorFromWindow(bar, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(mon, &mi)) return T(L"Other monitor");
    std::wstring dev = mi.szDevice;
    size_t d = dev.find(L"DISPLAY");
    std::wstring label = d != std::wstring::npos ? TF(L"Display {number}", { { L"number", dev.substr(d + 7) } }) : T(L"Other monitor");
    POINT origin{ 0, 0 };
    if (GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &pm)) {
        int dx = (mi.rcMonitor.left + mi.rcMonitor.right - pm.rcMonitor.left - pm.rcMonitor.right) / 2;
        int dy = (mi.rcMonitor.top + mi.rcMonitor.bottom - pm.rcMonitor.top - pm.rcMonitor.bottom) / 2;
        label += std::wstring(L" (") + T(abs(dx) >= abs(dy) ? (dx < 0 ? L"left" : L"right") : (dy < 0 ? L"above" : L"below")) + L")";
    }
    return label;
}

void SetPrimaryHidden(bool hide) {
    if (hide == g_primaryHidden || !g_wnd) return;
    g_primaryHidden = hide;
    if (hide) {
        ShowWindow(g_wnd, SW_HIDE);
        ReserveBottom(0);
        if (g_rebar && IsWindow(g_rebar)) {
            RECT r;
            GetClientRect(g_taskbar, &r);
            PostMessageW(g_taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
        }
    }
    g_lastW = -1;
}

void SyncMirrors() {
    if (g_myFinder) return;
    std::vector<HWND> all = g_wnd ? SecondaryTaskbars() : std::vector<HWND>{}, bars;
    bool chosenFound = false;
    if (g_choice == L"all") {
        bars = all;
    } else if (g_choice != L"main") {
        std::vector<std::wstring> ids;
        for (HWND b : all) ids.push_back(MonitorId(b));
        int i = WIDGET_NS::FindMonitor(ids, g_choice);
        if (i >= 0) {
            bars.push_back(all[i]);
            chosenFound = true;
        }
    }
    SetPrimaryHidden(chosenFound);
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (std::find(bars.begin(), bars.end(), it->bar) == bars.end() || !IsWindow(it->wnd)) {
            DestroyMirror(*it);
            it = g_mirrors.erase(it);
        } else {
            ++it;
        }
    }
    for (HWND b : bars) {
        if (std::any_of(g_mirrors.begin(), g_mirrors.end(), [b](const Mirror& m) { return m.bar == b; })) continue;
        Mirror m;
        m.bar = b;
        m.wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, b, nullptr,
                                g_inst, nullptr);
        if (!m.wnd) continue;
        SetSummary(m.wnd);
        g_mirrors.push_back(m);
        PaintMirror(g_mirrors.back());
    }
    SetMeasureMirrors(!g_mirrors.empty());
    for (auto& m : g_mirrors) PositionMirror(m);
}

void PaintAll() {
    Paint();
    for (auto& m : g_mirrors) PaintMirror(m);
}

// ---- hover card ----------------------------------------------------------------------------------
HWND g_card;
int g_cardItem = -1;
HWND g_cardStrip;
int g_hoverItem = -1;
constexpr int kCardMessage = -2;
constexpr UINT_PTR kCardTimer = 1;
constexpr UINT_PTR kFinderTimer = 3;
constexpr UINT_PTR kChargeTimer = 2;
constexpr UINT kCardDelayMs = 400;
const wchar_t* kCardClass = WIDGET_CLASS L"Card";

const std::vector<RECT>* RectsOf(HWND strip) {
    if (strip == g_wnd) return &g_itemRects;
    for (auto& m : g_mirrors)
        if (m.wnd == strip) return &m.itemRects;
    return nullptr;
}

int ItemAt(HWND strip, POINT p) {
    if (g_items.empty()) return kCardMessage;
    const auto* rects = RectsOf(strip);
    if (!rects) return -1;
    for (size_t i = 0; i < rects->size(); i++)
        if (PtInRect(&(*rects)[i], p)) return (int)i;
    return -1;
}

Mask RenderLine(const std::wstring& s, HFONT font) {
    Mask m;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ of = SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    if (sz.cx > 0 && sz.cy > 0) {
        int W = sz.cx + 2, H = sz.cy;
        BITMAPINFO bi{};
        bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ ob = SelectObject(dc, bmp);
        memset(bits, 0, (size_t)W * H * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 0, 0, s.c_str(), (int)s.size());
        GdiFlush();
        auto* p = static_cast<DWORD*>(bits);
        m.w = W;
        m.h = H;
        m.a.resize((size_t)W * H);
        for (int i = 0; i < W * H; i++) m.a[i] = (BYTE)(((p[i] & 0xFF) + ((p[i] >> 8) & 0xFF) + ((p[i] >> 16) & 0xFF)) / 3);
        SelectObject(dc, ob);
        DeleteObject(bmp);
    }
    SelectObject(dc, of);
    DeleteDC(dc);
    return m;
}

std::wstring Fit(std::wstring s, HFONT font, int maxW) {
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ of = SelectObject(dc, font);
    auto width = [&](const std::wstring& t) { SIZE z{}; GetTextExtentPoint32W(dc, t.c_str(), (int)t.size(), &z); return (int)z.cx; };
    if (width(s) > maxW) {
        while (s.size() > 1 && width(s + L"…") > maxW) s.pop_back();
        s += L"…";
    }
    SelectObject(dc, of);
    DeleteDC(dc);
    return s;
}

void FillRound(Canvas& cv, int x0, int y0, int w, int h, int r, Rgb c, int alpha) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) {
            double dx = x < x0 + r ? x0 + r - (x + 0.5) : (x >= x0 + w - r ? (x + 0.5) - (x0 + w - r) : 0);
            double dy = y < y0 + r ? y0 + r - (y + 0.5) : (y >= y0 + h - r ? (y + 0.5) - (y0 + h - r) : 0);
            double cover = (dx > 0 && dy > 0) ? std::min(1.0, std::max(0.0, r - std::sqrt(dx * dx + dy * dy) + 0.5)) : 1.0;
            if (cover > 0) cv.Blend(x, y, c, (int)(alpha * cover));
        }
}

void HideCard() {
    g_cardItem = -1;
    if (g_card) ShowWindow(g_card, SW_HIDE);
}

void ShowCard(HWND strip, int item) {
    const auto* rects = RectsOf(strip);
    bool message = item == kCardMessage;
    if (!rects || (!message && (item < 0 || item >= (int)g_items.size() || item >= (int)rects->size()))) {
        HideCard();
        return;
    }
    if (!g_card) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{ sizeof(wc) };
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = g_inst;
            wc.lpszClassName = kCardClass;
            registered = RegisterClassExW(&wc) != 0;
        }
        g_card = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, kCardClass, L"",
                                 WS_POPUP, 0, 0, 1, 1, g_notify, nullptr, g_inst, nullptr);
        if (!g_card) return;
    }
    HWND bar = GetParent(strip);
    g_dpiBar = bar;

    Rgb bg = g_light ? Rgb{ 249, 249, 249 } : Rgb{ 44, 44, 44 };
    Rgb border = g_light ? Rgb{ 222, 222, 222 } : Rgb{ 66, 66, 66 };
    Rgb fg = g_light ? Rgb{ 27, 27, 27 } : Rgb{ 242, 242, 242 };
    Rgb sub = g_light ? Rgb{ 96, 96, 96 } : Rgb{ 170, 170, 170 };
    Rgb track = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    Rgb red = g_light ? Rgb{ 196, 30, 40 } : Rgb{ 255, 90, 90 };
    Rgb warm = g_light ? Rgb{ 205, 110, 0 } : Rgb{ 255, 170, 50 };
    Rgb charge = g_light ? Rgb{ 0, 120, 212 } : Rgb{ 96, 205, 255 };
    Rgb green = g_light ? Rgb{ 16, 124, 16 } : Rgb{ 108, 203, 95 };
    HFONT icons = MakeFont(Px(18), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT title = MakeFont(Px(14), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");
    HFONT big = MakeFont(Px(28), FW_SEMIBOLD, L"Segoe UI Variable Display", L"Segoe UI");
    HFONT body = MakeFont(Px(12), FW_NORMAL, L"Segoe UI Variable Text", L"Segoe UI");

    const int pad = Px(16), minW = Px(240), maxW = Px(380), textMax = maxW - 2 * pad;
    struct Line { Mask m; Rgb c; int x, y; };
    std::vector<Line> lines;
    Mask glyph;
    int W = minW, y = pad, barY = -1, pct = -1, barAlpha = 255, chartY = -1, chartNow = -1, axisY = 0;
    double forecastHours = -1, pastSpanSec = 24.0 * 3600.0, scaleMax = 100.0;
    bool chartArea = false;
    std::vector<std::pair<long long, int>> chart;
    Mask axisLeft, axisNow, axisEnd;
    Rgb barColor = fg;
    if (message) {
        size_t start = 0;
        while (start <= g_tipText.size()) {
            size_t end = g_tipText.find(L'\n', start);
            std::wstring t = g_tipText.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            Mask m = RenderLine(Fit(t, body, textMax), body);
            W = std::max(W, m.w + 2 * pad);
            lines.push_back({ std::move(m), fg, pad, y });
            y += lines.back().m.h + Px(2);
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        y += pad - Px(2);
    } else {
        const WidgetItem& it = g_items[item];
        glyph = RenderItemIconMask(it, Px(18), icons, false);
        int nameX = pad + glyph.w + Px(10);
        Mask name = RenderLine(Fit(it.name.empty() ? L"Taskbar Vitals" : it.name, title, maxW - pad - nameX), title);
        int rowH = std::max(glyph.h, name.h);
        W = std::max(W, nameX + name.w + pad);
        int nameY = y + (rowH - name.h) / 2;
        lines.push_back({ std::move(name), fg, nameX, nameY });
        int glyphY = y + (rowH - glyph.h) / 2;
        y += rowH + Px(8);

        std::wstring level = !it.text.empty() ? it.text : it.percent >= 0 ? std::to_wstring(it.percent) + L"%" : L"?";
        Mask lv = RenderLine(Fit(level, big, textMax), big);
        int lvY = y;
        W = std::max(W, lv.w + 2 * pad);
        Rgb levelColor = (it.low || it.heat >= 2) ? red : (it.heat == 1 ? warm : fg);
        lines.push_back({ std::move(lv), levelColor, pad, lvY });
        y += lines.back().m.h + Px(4);

        pct = it.percent;
        barAlpha = it.dim ? 150 : 255;
        barColor = (it.low || it.heat >= 2) ? red
                 : (it.heat == 1)           ? warm
                 : it.charging              ? charge
                 : it.full                  ? green
                 : it.accent != Accent::None
                     ? Rgb{ AccentColor(it.accent, g_light).r, AccentColor(it.accent, g_light).g, AccentColor(it.accent, g_light).b }
                     : green;
        if (pct >= 0) {
            barY = y;
            y += Px(6) + Px(12);
        } else {
            y += Px(6);
        }

        std::vector<std::wstring> rest;
        if (!it.status.empty()) rest.push_back(it.status);
        rest.insert(rest.end(), it.info.begin(), it.info.end());
        for (const auto& t : rest) {
            Mask m = RenderLine(Fit(t, body, textMax), body);
            W = std::max(W, m.w + 2 * pad);
            lines.push_back({ std::move(m), sub, pad, y });
            y += lines.back().m.h + Px(2);
        }

        forecastHours = (!it.isVital && it.percent > 0) ? it.hoursLeft : -1;
        if (g_chartOn && ((it.percent >= 0 && (!it.trend.empty() || forecastHours > 0)) || (it.isVital && !it.trend.empty()))) {
            y += Px(8);
            std::wstring heading = !it.trendTitle.empty()
                                     ? it.trendTitle
                                     : std::wstring(T(forecastHours > 0 ? L"Last 24 hours and forecast" : L"Last 24 hours"));
            Mask cap = RenderLine(Fit(heading, body, textMax), body);
            W = std::max(W, cap.w + 2 * pad);
            lines.push_back({ std::move(cap), sub, pad, y });
            y += lines.back().m.h + Px(6);
            chartY = y;
            chart = it.trend;
            chartNow = it.percent >= 0 ? it.percent : (!chart.empty() ? chart.back().second : 0);
            pastSpanSec = (double)std::max(10, it.trendSpanSec);
            scaleMax = (double)std::max(1, it.trendMax);
            chartArea = it.isVital;
            y += Px(56) + Px(4);
            std::wstring leftTxt = !it.trendAxisLeft.empty() ? it.trendAxisLeft : std::wstring(T(L"24h ago"));
            axisLeft = RenderLine(leftTxt, body);
            axisNow = RenderLine(T(L"Now"), body);
            if (forecastHours > 0) axisEnd = RenderLine(it.hoursLeftText, body);
            axisY = y;
            y += axisLeft.h;
        }
        y += pad - Px(2);
        Rgb iconColor = it.accent != Accent::None
                      ? Rgb{ AccentColor(it.accent, g_light).r, AccentColor(it.accent, g_light).g, AccentColor(it.accent, g_light).b }
                      : fg;
        lines.insert(lines.begin(), Line{ std::move(glyph), iconColor, pad, glyphY });
    }
    W = std::min(W, maxW);
    int H = y;
    DeleteObject(icons);
    DeleteObject(title);
    DeleteObject(big);
    DeleteObject(body);

    Canvas cv(W, H);
    int radius = Px(8);
    FillRound(cv, 0, 0, W, H, radius, border, 255);
    FillRound(cv, 1, 1, W - 2, H - 2, std::max(1, radius - 1), bg, 255);
    if (barY >= 0) {
        int bw = W - 2 * pad, bh = Px(6);
        FillRound(cv, pad, barY, bw, bh, bh / 2, track, g_light ? 28 : 40);
        if (pct > 0) FillRound(cv, pad, barY, std::max(bh, bw * std::min(pct, 100) / 100), bh, bh / 2, barColor, barAlpha);
    }
    if (chartY >= 0) {
        const int cx = pad, cw = W - 2 * pad, ch = Px(56);
        const double past = pastSpanSec, maxFuture = 90.0 * 24 * 3600.0;
        const double future = forecastHours > 0 ? std::min(forecastHours * 3600.0, maxFuture) : 0;
        const double pastW = future > 0 ? (cw - 1) * 0.45 : (cw - 1);
        auto X = [&](double t) {
            return t <= 0 ? cx + (1.0 + std::max(t, -past) / past) * pastW : cx + pastW + std::min(t, future) / future * ((cw - 1) - pastW);
        };
        auto Y = [&](double p) { return chartY + (scaleMax - std::max(0.0, std::min(p, scaleMax))) / scaleMax * (ch - 1); };
        for (int yy : { chartY, chartY + ch / 2, chartY + ch - 1 })
            for (int xx = cx; xx < cx + cw; xx++) cv.Blend(xx, yy, track, g_light ? 18 : 24);
        const int nowX = (int)std::lround(X(0));
        for (int yy = chartY; yy < chartY + ch; yy += 3) cv.Blend(nowX, yy, track, g_light ? 50 : 70);
        auto dot = [&](double fx, double fy, int alpha) {
            int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            double ax = fx - x0, ay = fy - y0;
            cv.Blend(x0, y0, barColor, (int)(alpha * (1 - ax) * (1 - ay)));
            cv.Blend(x0 + 1, y0, barColor, (int)(alpha * ax * (1 - ay)));
            cv.Blend(x0, y0 + 1, barColor, (int)(alpha * (1 - ax) * ay));
            cv.Blend(x0 + 1, y0 + 1, barColor, (int)(alpha * ax * ay));
        };
        auto line = [&](double x1, double y1, double x2, double y2, int alpha, bool dashed, bool area) {
            if (area)
                for (int xx = (int)std::ceil(x1); xx <= (int)std::floor(x2); xx++) {
                    double k = x2 > x1 ? (xx - x1) / (x2 - x1) : 0;
                    for (int yy = (int)std::ceil(y1 + (y2 - y1) * k); yy < chartY + ch; yy++)
                        cv.Blend(xx, yy, barColor, g_light ? 26 : 36);
                }
            double len = std::max(1.0, std::hypot(x2 - x1, y2 - y1));
            for (double s = 0; s <= len; s += 0.25) {
                if (dashed && std::fmod(s, (double)Px(7)) > Px(4)) continue;
                double fx = x1 + (x2 - x1) * s / len, fy = y1 + (y2 - y1) * s / len;
                dot(fx, fy - 0.5, alpha);
                dot(fx, fy + 0.5, alpha);
            }
        };
        auto disc = [&](double fx, double fy, int r, int alpha) {
            for (int yy = -r; yy <= r; yy++)
                for (int xx = -r; xx <= r; xx++)
                    if (xx * xx + yy * yy <= r * r + 1) cv.Blend((int)std::lround(fx) + xx, (int)std::lround(fy) + yy, barColor, alpha);
        };
        std::vector<std::pair<double, double>> pts;
        for (const auto& c : chart) pts.push_back({ (double)c.first, (double)c.second });
        if (!chartArea || pts.empty()) pts.push_back({ 0.0, (double)chartNow });
        else if (pts.back().first < 0.0) pts.push_back({ 0.0, (double)chartNow });
        const double maxGap = chartArea ? std::max(30.0, past / 4.0) : 12.0 * 3600.0;
        for (size_t i = 0; i + 1 < pts.size(); i++) {
            if (pts[i + 1].first - pts[i].first > maxGap) { disc(X(pts[i].first), Y(pts[i].second), 1, 200); continue; }
            line(X(pts[i].first), Y(pts[i].second), X(pts[i + 1].first), Y(pts[i + 1].second), chartArea ? 180 : 110, false, chartArea);
        }
        if (future > 0) {
            double endPct = chartNow * (1.0 - future / (forecastHours * 3600.0));
            line(X(0), Y(chartNow), X(future), Y(endPct), 90, true, false);
            disc(X(future), Y(endPct), Px(2), 140);
        }
        disc(X(0), Y(chartNow), Px(2) + 1, 255);
        lines.push_back({ std::move(axisLeft), sub, cx, axisY });
        int nowLabelX = std::max(cx, std::min(nowX - axisNow.w / 2, cx + cw - axisNow.w));
        if (future > 0) nowLabelX = std::max(nowLabelX, cx + lines.back().m.w + Px(6));
        int endX = cx + cw - axisEnd.w;
        if (future > 0 && axisEnd.w > 0) nowLabelX = std::min(nowLabelX, endX - axisNow.w - Px(6));
        if (future > 0 || nowX > cx + lines.back().m.w + Px(6)) lines.push_back({ std::move(axisNow), sub, nowLabelX, axisY });
        if (future > 0 && axisEnd.w > 0) lines.push_back({ std::move(axisEnd), fg, endX, axisY });
    }
    for (const auto& l : lines) cv.Draw(l.m, l.x, l.y, l.c, 255);

    RECT ir{};
    if (message) GetClientRect(strip, &ir);
    else ir = (*rects)[item];
    MapWindowPoints(strip, nullptr, reinterpret_cast<POINT*>(&ir), 2);
    RECT br{};
    GetWindowRect(bar, &br);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(bar, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& mon = mi.rcMonitor;
    int gap = Px(8), x, top;
    bool vertical = br.bottom - br.top > br.right - br.left;
    if (vertical) {
        top = (ir.top + ir.bottom) / 2 - H / 2;
        x = br.left - mon.left < mon.right - br.right ? br.right + gap : br.left - gap - W;
    } else {
        x = (ir.left + ir.right) / 2 - W / 2;
        top = br.top - mon.top > mon.bottom - br.bottom ? br.top - gap - H : br.bottom + gap;
    }
    x = std::max((int)mon.left + gap, std::min(x, (int)mon.right - gap - W));
    top = std::max((int)mon.top + gap, std::min(top, (int)mon.bottom - gap - H));
    g_dpiBar = nullptr;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    memcpy(bits, cv.px.data(), (size_t)W * H * 4);
    HGDIOBJ ob = SelectObject(mem, bmp);
    SIZE sz{ W, H };
    POINT src{ 0, 0 }, dst{ x, top };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_card, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    SetWindowPos(g_card, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_cardItem = item;
    g_cardStrip = strip;
}

void SetHover(HWND h, bool on) {
    if (h == g_wnd) {
        if (g_hover == on) return;
        g_hover = on;
        Paint();
        WIDGET_NS::Reposition();
        return;
    }
    for (auto& m : g_mirrors)
        if (m.wnd == h && m.hover != on) {
            m.hover = on;
            PaintMirror(m);
            PositionMirror(m);
        }
}

bool CanDrag(HWND h) {
    return h == g_wnd && !g_myFinder && !g_vertical && !g_farLeft && !g_beforeApps && g_anchorLeft >= 0 &&
           !(GetWindowLongW(g_taskbar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL);
}

void BeginDrag(HWND h) {
    g_dragging = true;
    KillTimer(h, kCardTimer);
    g_hoverItem = -1;
    HideCard();
    RECT r;
    GetWindowRect(h, &r);
    POINT c;
    GetCursorPos(&c);
    g_grabDx = c.x - r.left;
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32644)));
}

void DragTo(HWND h, POINT c) {
    RECT bar;
    GetClientRect(g_taskbar, &bar);
    POINT p{ c.x - g_grabDx, 0 };
    ScreenToClient(g_taskbar, &p);
    int x = std::max(0, std::min((int)p.x, std::min(g_anchorLeft, (int)bar.right - (int)g_size.cx)));
    SetWindowPos(h, HWND_TOP, x, 0, g_size.cx, g_size.cy, SWP_NOACTIVATE);
}

void Drop(HWND h) {
    g_dragging = false;
    RECT r;
    GetWindowRect(h, &r);
    POINT p{ r.left, r.top };
    ScreenToClient(g_taskbar, &p);
    int px = std::max(0, MulDiv(g_anchorLeft - (int)p.x, 96, Dpi()));
    DWORD v = (DWORD)px;
    if (px) RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", REG_DWORD, &v, sizeof(v));
    else RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset");
    g_lastLeft = -1;
    WIDGET_NS::Reposition();
}

LRESULT CALLBACK WidgetProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_LBUTTONDOWN:
            if (CanDrag(h)) {
                g_pressed = true;
                GetCursorPos(&g_pressAt);
                SetCapture(h);
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (g_dragging && (HWND)lp != h) Drop(h);
            g_pressed = false;
            return 0;
        case WM_MOUSEMOVE: {
            if (g_pressed) {
                POINT c;
                GetCursorPos(&c);
                if (!g_dragging && (abs(c.x - g_pressAt.x) > GetSystemMetrics(SM_CXDRAG) ||
                                    abs(c.y - g_pressAt.y) > GetSystemMetrics(SM_CYDRAG)))
                    BeginDrag(h);
                if (g_dragging) {
                    DragTo(h, c);
                    return 0;
                }
            }
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            SetHover(h, true);
            if (!g_cardOn) return 0;
            POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
            int k = ItemAt(h, p);
            if (k != g_hoverItem || h != g_cardStrip) {
                g_hoverItem = k;
                if (g_cardItem != -1 && k != -1) {
                    ShowCard(h, k);
                } else {
                    KillTimer(h, kCardTimer);
                    if (k == -1) HideCard();
                    else SetTimer(h, kCardTimer, kCardDelayMs, nullptr);
                }
            }
            return 0;
        }
        case WM_TIMER:
            if (wp == kCardTimer) {
                KillTimer(h, kCardTimer);
                if (g_hoverItem != -1) ShowCard(h, g_hoverItem);
            } else if (wp == kChargeTimer) {
                PaintAll();
                WIDGET_NS::Reposition();
            } else if (wp == kFinderTimer) {
                WIDGET_NS::Reposition();
            }
            return 0;
        case WM_MOUSELEAVE:
            SetHover(h, false);
            KillTimer(h, kCardTimer);
            g_hoverItem = -1;
            HideCard();
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            KillTimer(h, kCardTimer);
            HideCard();
            bool dragged = g_dragging;
            g_pressed = false;
            if (dragged) Drop(h);
            if (msg == WM_LBUTTONUP && GetCapture() == h) ReleaseCapture();
            if (!dragged) PostMessageW(g_notify, g_clickMsg, 0, 0);
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_SHOWWINDOW:
            if (g_myFinder && lp != 0) return 0;
            break;
        case WM_NCHITTEST:
            if (g_myFinder) return HTCLIENT;
            break;
        case WM_NCLBUTTONDBLCLK:
            if (g_myFinder) return 0;
            break;
        case WM_SYSCOMMAND:
            if (g_myFinder) {
                auto command = wp & 0xfff0;
                if (command == SC_MAXIMIZE || command == SC_MINIMIZE || command == SC_RESTORE || command == SC_MOVE ||
                    command == SC_SIZE)
                    return 0;
            }
            break;
        case WM_ERASEBKGND:
            if (g_myFinder && g_compositionReady) return 1;
            break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace WIDGET_NS {

void SetMyFinder(bool on) {
    if (g_myFinder == on) return;
    Destroy();
    g_myFinder = on;
}

wchar_t DeviceGlyph(DeviceKind kind, bool bluetooth) { return Glyph(kind, bluetooth); }

bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme) {
    Destroy();
    g_taskbar = FindWindowW(g_myFinder ? L"MyFinderApp" : L"Shell_TrayWnd", nullptr);
    if (!g_taskbar || (!g_myFinder && !FindWindowExW(g_taskbar, nullptr, L"TrayNotifyWnd", nullptr))) {
        g_taskbar = nullptr;
        return false;
    }
    RECT tb;
    GetWindowRect(g_taskbar, &tb);
    g_vertical = tb.bottom - tb.top > tb.right - tb.left;
    g_rebar = g_myFinder || FindWindowExW(g_taskbar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)
                  ? nullptr
                  : FindWindowExW(g_taskbar, nullptr, L"ReBarWindow32", nullptr);
    g_notify = notifyWnd;
    g_clickMsg = clickMsg;
    g_light = lightTheme;
    g_inst = inst;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WidgetProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    DWORD exStyle = WS_EX_NOACTIVATE | (g_myFinder ? WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP : WS_EX_LAYERED);
    DWORD style = g_myFinder ? WS_POPUP : WS_CHILD | WS_CLIPSIBLINGS;
    g_wnd = CreateWindowExW(exStyle, kClass, L"", style, 0, 0, 1, 1, g_myFinder ? nullptr : g_taskbar, nullptr, inst, nullptr);
    if (!g_wnd) { g_taskbar = nullptr; return false; }
    if (g_myFinder) {
        g_compositionReady = g_composition.Initialize(g_wnd);
        if (!g_compositionReady) {
            Destroy();
            return false;
        }
        g_finderOrderHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE, nullptr, FinderOrderEvent, 0, 0,
                                            WINEVENT_OUTOFCONTEXT);
        g_finderForegroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, FinderOrderEvent,
                                                 0, 0, WINEVENT_OUTOFCONTEXT);
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                            notifyWnd, nullptr, inst, nullptr);
    if (g_tip) {
        ApplyTipTheme();
        SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 600);
        SendMessageW(g_tip, TTM_ACTIVATE, !g_cardOn, 0);
    }
    SetSummary(g_wnd);
    if (!g_myFinder) WatchWidgetsButton(g_taskbar);
    g_lastDpi = Dpi();
    Paint();
    g_lastW = -1;
    if (g_myFinder) {
        g_orderGuard.Start(g_wnd, g_taskbar);
        WatchFinder(g_taskbar);
        SetTimer(g_wnd, kFinderTimer, 125, nullptr);
    }
    Reposition();
    return true;
}

void Destroy() {
    g_lastValidFinderPlacement = 0;
    g_orderGuard.Stop();
    if (g_finderOrderHook) UnhookWinEvent(g_finderOrderHook);
    if (g_finderForegroundHook) UnhookWinEvent(g_finderForegroundHook);
    g_finderOrderHook = g_finderForegroundHook = nullptr;
    g_finderOrderQueued = false;
    WatchFinder(nullptr);
    g_anchorLeft = -1;
    if (g_rebar && IsWindow(g_rebar) && g_taskbar && IsWindow(g_taskbar)) {
        RECT r;
        GetClientRect(g_taskbar, &r);
        PostMessageW(g_taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
    }
    g_rebar = nullptr;
    ReserveBottom(0);
    g_fill = false;
    for (auto& m : g_mirrors) DestroyMirror(m);
    g_mirrors.clear();
    SetMeasureMirrors(false);
    g_primaryHidden = false;
    if (g_wbWake) WatchWidgetsButton(nullptr);
    if (g_card) { DestroyWindow(g_card); g_card = nullptr; }
    if (g_tip) { DestroyWindow(g_tip); g_tip = nullptr; }
    g_cardItem = g_hoverItem = -1;
    g_cardStrip = nullptr;
    g_composition.Reset();
    g_compositionReady = false;
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = nullptr; }
    g_taskbar = nullptr;
    g_hover = false;
    g_painted = false;
    g_placedFarLeft = false;
    g_size = {};
    g_paintedBarClient = {};
}

bool Alive() { return g_wnd && IsWindow(g_wnd) && g_taskbar && IsWindow(g_taskbar) && (!g_myFinder || g_compositionReady); }

void UpdateChargeTimer() {
    if (!g_wnd) return;
    bool any = false;
    for (const auto& it : g_items) any |= it.charging && !it.full;
    if (g_animateCharging && any) SetTimer(g_wnd, kChargeTimer, 100, nullptr);
    else KillTimer(g_wnd, kChargeTimer);
}

void SetAnimateCharging(bool on) {
    g_animateCharging = on;
    UpdateChargeTimer();
    PaintAll();
    if (g_wnd) Reposition();
}

void SetItems(const std::vector<WidgetItem>& items, const std::wstring& tooltip) {
    g_items = items;
    if (tooltip != g_tipText) {
        g_tipText = tooltip;
        if (g_wnd) SetSummary(g_wnd);
        for (const auto& m : g_mirrors) SetSummary(m.wnd);
    }
    PaintAll();
    Reposition();
    UpdateChargeTimer();
    if (g_cardItem != -1 && g_cardStrip && IsWindow(g_cardStrip)) {
        if (g_cardItem == kCardMessage ? g_items.empty() : g_cardItem < (int)g_items.size()) ShowCard(g_cardStrip, g_cardItem);
        else HideCard();
    }
}

void SetCardOptions(bool card, bool chart) {
    g_cardOn = card;
    g_chartOn = chart;
    if (g_tip) SendMessageW(g_tip, TTM_ACTIVATE, !card, 0);
    if (!card) HideCard();
    else if (g_cardItem != -1 && g_cardStrip && IsWindow(g_cardStrip)) ShowCard(g_cardStrip, g_cardItem);
}

void SetLightTheme(bool light) {
    if (g_myFinder) {
        auto foreground = FinderForeground(g_taskbar);
        if (foreground != myfinder::Foreground::Unknown) light = foreground == myfinder::Foreground::Dark;
    }
    g_light = light;
    ApplyTipTheme();
    PaintAll();
    if (g_cardItem != -1 && g_cardStrip && IsWindow(g_cardStrip)) ShowCard(g_cardStrip, g_cardItem);
}

void SetBackground(bool on) {
    g_background = on;
    PaintAll();
}

std::wstring ItemsToText(const std::vector<WidgetItem>& items) {
    std::wstring out;
    for (const auto& it : items) {
        if (!out.empty()) out += L"  ";
        out += it.glyph ? it.glyph : Glyph(it.kind, it.bluetooth);
        out += L' ';
        if (it.dim && it.percent >= 0) out += L'~';
        out += !it.text.empty() ? it.text : it.percent >= 0 ? std::to_wstring(it.percent) + L"%" : L"?";
        if (it.charging) {
            out += L' ';
            out += kBolt;
        }
        if (it.low) out += L'!';
    }
    return out;
}

void SetConnectionIcons(bool on) {
    g_connectionIcons = on;
    PaintAll();
    if (g_wnd) Reposition();
}

void SetRingStyle(bool on) {
    g_ringStyle = on;
    PaintAll();
    if (g_wnd) Reposition();
}

void SetShowPercentSign(bool on) {
    g_showPercentSign = on;
    PaintAll();
    if (g_wnd) Reposition();
}

void SetStacked(bool on) {
    g_stacked = on;
    PaintAll();
    if (g_wnd) Reposition();
}

void SetTwoRows(bool on) {
    g_twoRows = on;
    PaintAll();
    if (g_wnd) Reposition();
}

void SetCustomIcons(bool on) {
    g_customIcons = on;
    PaintAll();
    if (g_wnd) Reposition();
}

void SetMakeRoom(bool on) {
    g_makeRoom = on;
    g_lastW = -1;
    if (g_wnd) Reposition();
}

void SetVerticalPlace(VerticalPlace place) {
    g_place = place;
    if (g_wnd) Reposition();
}

bool Vertical() { return g_wnd && g_vertical; }

void ApplyLeftSpot() {
    bool on = g_farLeft || g_beforeApps;
    AcquireSRWLockExclusive(&g_wbLock);
    g_wbFarLeft = on;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (on && g_wbWake) SetEvent(g_wbWake);
    if (g_wnd) Reposition();
}

void SetFarLeft(bool on) {
    g_farLeft = on;
    ApplyLeftSpot();
}

bool AtFarLeft() {
    if (!Alive()) return false;
    if (!g_primaryHidden) return g_placedFarLeft;
    for (const auto& m : g_mirrors)
        if (Shown(m.wnd)) return m.farOk;
    return false;
}

void SetFarLeftOnly(bool on) {
    g_farLeftOnly = on;
    g_lastW = -1;
    if (g_wnd) Reposition();
}

void SetBeforeApps(bool on) {
    g_beforeApps = on;
    ApplyLeftSpot();
}

void SetMonitorChoice(const std::wstring& choice) {
    g_choice = choice.empty() ? L"all" : choice;
    SyncMirrors();
    if (g_wnd) Reposition();
}

int FindMonitor(const std::vector<std::wstring>& ids, const std::wstring& choice) {
    for (size_t i = 0; i < ids.size(); i++)
        if (ids[i] == choice) return (int)i;
    std::wstring model = MonitorModel(choice);
    int found = -1;
    if (!model.empty())
        for (size_t i = 0; i < ids.size(); i++)
            if (MonitorModel(ids[i]) == model) {
                if (found >= 0) return -1;
                found = (int)i;
            }
    return found;
}

std::vector<MonitorOption> OtherMonitors() {
    std::vector<MonitorOption> out;
    for (HWND b : SecondaryTaskbars()) out.push_back({ MonitorId(b), MonitorLabel(b) });
    std::sort(out.begin(), out.end(), [](const MonitorOption& a, const MonitorOption& b) { return a.label < b.label; });
    return out;
}

int StepPastOtherWidgets(int edge, const RECT& bar, bool vertical, bool rtl) {
    if (!g_makeRoom || !g_wnd) return edge;
    DWORD explorer = 0, pid = 0;
    GetWindowThreadProcessId(g_taskbar, &explorer);
    const DWORD self = GetCurrentProcessId();
    const LONG span = vertical ? bar.bottom - bar.top : bar.right - bar.left;
    std::vector<RECT> others;
    auto consider = [&](HWND c, bool topLevel) {
        if (c == g_wnd || !IsWindowVisible(c)) return;
        GetWindowThreadProcessId(c, &pid);
        RECT r;
        if (pid == explorer || pid == self || !GetWindowRect(c, &r) || r.right <= r.left || r.bottom <= r.top) return;
        if (topLevel && (!(GetWindowLongW(c, GWL_EXSTYLE) & WS_EX_TOPMOST) || r.left < bar.left || r.right > bar.right ||
                         r.top < bar.top || r.bottom > bar.bottom))
            return;
        if ((vertical ? r.bottom - r.top : r.right - r.left) > span / 3) return;
        others.push_back(r);
    };
    for (HWND c = GetWindow(g_taskbar, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) consider(c, false);
    for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT)) consider(w, true);
    for (int pass = 0; pass < 8; pass++) {
        bool moved = false;
        for (const RECT& r : others) {
            if (vertical) {
                if (r.right > bar.left && r.left < bar.right && r.bottom >= edge - Px(16) && r.bottom <= edge + Px(4) &&
                    r.top < edge)
                    edge = r.top, moved = true;
            } else if (r.bottom > bar.top && r.top < bar.bottom) {
                if (!rtl && r.right >= edge - Px(16) && r.right <= edge + Px(4) && r.left < edge)
                    edge = r.left, moved = true;
                else if (rtl && r.left <= edge + Px(16) && r.left >= edge - Px(4) && r.right > edge)
                    edge = r.right, moved = true;
            }
        }
        if (!moved) break;
    }
    return edge;
}

bool Reposition() {
    if (!Alive()) return false;
    if (g_dragging) return true;
    SyncMirrors();
    if (g_primaryHidden) return true;
    if (g_myFinder) {
        if (Dpi() != g_lastDpi) { g_lastDpi = Dpi(); Paint(); }
        auto foreground = FinderForeground(g_taskbar);
        if (foreground != myfinder::Foreground::Unknown && g_light != (foreground == myfinder::Foreground::Dark)) {
            g_light = foreground == myfinder::Foreground::Dark;
            ApplyTipTheme();
            Paint();
        }
        RECT bar{};
        const auto now = GetTickCount64();
        const bool keepPlacement = IsWindowVisible(g_wnd) && g_lastValidFinderPlacement && now - g_lastValidFinderPlacement < 400;
        if (!GetClientRect(g_taskbar, &bar) || !IsWindowVisible(g_taskbar) || IsIconic(g_taskbar) || bar.right <= 0 ||
            bar.bottom <= 0) {
            if (!keepPlacement && IsWindowVisible(g_wnd)) ShowWindow(g_wnd, SW_HIDE);
            return true;
        }
        if (!g_painted || bar.bottom != g_size.cy) Paint();
        RECT screenBar;
        GetWindowRect(g_taskbar, &screenBar);
        int edge = FinderEdge(g_taskbar, screenBar);
        int left = edge - g_size.cx - Px(8);
        if (edge < 0 || left < 0) {
            if (!keepPlacement && IsWindowVisible(g_wnd)) ShowWindow(g_wnd, SW_HIDE);
            return true;
        }
        g_lastValidFinderPlacement = now;
        POINT origin{ left, 0 };
        ClientToScreen(g_taskbar, &origin);
        const bool hostTopmost = (GetWindowLongPtrW(g_taskbar, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        const bool stripTopmost = (GetWindowLongPtrW(g_wnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        if (hostTopmost != stripTopmost)
            SetWindowPos(g_wnd, hostTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        HWND above = GetWindow(g_taskbar, GW_HWNDPREV);
        UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
        if (!IsWindowVisible(g_wnd)) flags |= SWP_SHOWWINDOW;
        if (!FinderAboveStrip()) flags |= SWP_NOZORDER;
        else if (!above || ((GetWindowLongPtrW(above, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != hostTopmost)
            above = hostTopmost ? HWND_TOPMOST : HWND_TOP;
        RECT current{};
        GetWindowRect(g_wnd, &current);
        if (current.left == origin.x && current.top == origin.y) flags |= SWP_NOMOVE;
        if (current.right - current.left == g_size.cx && current.bottom - current.top == g_size.cy) flags |= SWP_NOSIZE;
        if ((flags & (SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER)) != (SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER) ||
            (flags & SWP_SHOWWINDOW))
            SetWindowPos(g_wnd, above, origin.x, origin.y, g_size.cx, g_size.cy, flags);
        return true;
    }
    RECT bar{}, client{};
    if (!GetWindowRect(g_taskbar, &bar) || !GetClientRect(g_taskbar, &client)) return true;
    int clientW = client.right - client.left, clientH = client.bottom - client.top;
    if (clientW <= 0 || clientH <= 0) return true;
    bool vertical = bar.bottom - bar.top > bar.right - bar.left;
    int dpi = Dpi();
    if (!g_painted || g_size.cx <= 0 || g_size.cy <= 0 || dpi != g_lastDpi || vertical != g_vertical ||
        g_paintedBarClient.cx != clientW || g_paintedBarClient.cy != clientH) {
        g_lastDpi = dpi;
        g_vertical = vertical;
        Paint();
    }
    if (!g_painted || g_size.cx <= 0 || g_size.cy <= 0) return true;
    HWND tray = FindWindowExW(g_taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    RECT tr{};
    if (!tray || !GetWindowRect(tray, &tr)) return true;
    if (tr.right <= tr.left || tr.bottom <= tr.top) return true;
    if (tr.right <= bar.left || tr.left >= bar.right || tr.bottom <= bar.top || tr.top >= bar.bottom)
        return true;
    if (g_vertical) {
        if (g_farLeftOnly) {
            g_placedFarLeft = false;
            if (Shown(g_wnd)) ShowWindow(g_wnd, SW_HIDE);
            return true;
        }
        POINT anchor{ bar.left, StepPastOtherWidgets(tr.top, bar, true, false) };
        ScreenToClient(g_taskbar, &anchor);
        int top = anchor.y - g_size.cy - Px(2);
        DWORD offset = 0, osz = sizeof(offset);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset,
                         &osz) == ERROR_SUCCESS)
            top -= Px((int)offset);
        top = std::clamp(top, 0, (int)std::max<LONG>(0, client.bottom - g_size.cy));
        AcquireSRWLockShared(&g_wbLock);
        Column col = g_column;
        ReleaseSRWLockShared(&g_wbLock);
        bool bottom = g_place == WIDGET_NS::VerticalPlace::Bottom && !g_rebar && g_size.cy > 0 &&
                      g_size.cy < client.bottom / 2;
        bool wasReserved = g_reserved;
        ReserveBottom(bottom ? g_size.cy : 0);
        COLORREF fill = 0;
        bool sampled = bottom && wasReserved && SampleBarColor(bar, bar.bottom - g_size.cy, fill);
        bool changed = sampled && (!g_fill || abs(GetRValue(fill) - GetRValue(g_fillColor)) > 4 ||
                                   abs(GetGValue(fill) - GetGValue(g_fillColor)) > 4 ||
                                   abs(GetBValue(fill) - GetBValue(g_fillColor)) > 4);
        if (changed || (!bottom && g_fill)) {
            g_fill = bottom;
            g_fillColor = fill;
            Paint();
        }
        if (bottom) {
            top = client.bottom - g_size.cy;
        } else if (g_place == WIDGET_NS::VerticalPlace::Top) {
            top = 0;
        } else if (g_place == WIDGET_NS::VerticalPlace::Auto && !g_rebar && col.bar == g_taskbar && col.measured &&
                   col.any) {
            POINT first{ bar.left, col.top }, last{ bar.left, col.bottom };
            ScreenToClient(g_taskbar, &first);
            ScreenToClient(g_taskbar, &last);
            if (last.y > top && first.y < top + g_size.cy && g_size.cy + Px(2) <= first.y) top = 0;
        }
        int left = (int)std::max<LONG>(0, (client.right - g_size.cx) / 2);
        if (g_rebar && IsWindow(g_rebar)) {
            RECT rr{};
            GetWindowRect(g_rebar, &rr);
            POINT tl{ rr.left, rr.top };
            ScreenToClient(g_taskbar, &tl);
            if (tl.y < top && tl.y + (rr.bottom - rr.top) > top)
                MoveWindow(g_rebar, tl.x, tl.y, rr.right - rr.left, top - tl.y, TRUE);
        }
        bool covered = GetWindow(g_wnd, GW_HWNDPREV) != nullptr;
        if (!covered && Shown(g_wnd) && EqualRect(&tr, &g_lastTray) && EqualRect(&bar, &g_lastBar) &&
            g_lastW == g_size.cy && g_lastLeft == top)
            return true;
        g_lastTray = tr;
        g_lastBar = bar;
        g_lastW = g_size.cy;
        g_lastLeft = top;
        SetWindowPos(g_wnd, HWND_TOP, left, top, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return true;
    }
    ReserveBottom(0);
    if (g_fill) { g_fill = false; Paint(); }
    bool rtl = GetWindowLongW(g_taskbar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL;
    int edge = rtl ? tr.right : tr.left;
    if (!rtl)
        for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
            HWND feeds = FindWindowExW(g_taskbar, nullptr, cls, nullptr);
            if (!feeds || !IsWindowVisible(feeds)) continue;
            RECT fr;
            GetWindowRect(feeds, &fr);
            if (fr.left < edge && fr.right > fr.left) edge = fr.left;
        }
    RECT wb = WidgetsButtonRect();
    if (wb.right > wb.left && wb.top < bar.bottom && wb.bottom > bar.top &&
        (rtl ? wb.left < edge + g_size.cx + Px(2) && wb.right > edge : wb.right > edge - g_size.cx - Px(2) && wb.left < edge))
        edge = rtl ? wb.right : wb.left;
    edge = StepPastOtherWidgets(edge, bar, false, rtl);
    POINT p{ edge, bar.top };
    ScreenToClient(g_taskbar, &p);
    int left = p.x - g_size.cx - Px(2);
    g_anchorLeft = left;

    DWORD customOffset = 0, sz = sizeof(customOffset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset",
                     RRF_RT_REG_DWORD, nullptr, &customOffset, &sz) == ERROR_SUCCESS) {
        left -= Px((int)customOffset);
    }

    RECT taskbarClient{};
    if (GetClientRect(g_taskbar, &taskbarClient)) {
        left = RecoverHorizontalWidgetX(left, g_size.cx, taskbarClient.right - taskbarClient.left,
                                        tr.right - tr.left, g_lastLeft, Px(2));
    } else if (left < 0 && g_lastLeft >= 0) {
        left = g_lastLeft;
    }

    int farX = 0;
    bool atFarLeft = !rtl && (g_farLeft || g_beforeApps) && FarLeftX(g_taskbar, g_size.cx, farX);
    g_placedFarLeft = atFarLeft;
    if (g_farLeftOnly && !atFarLeft) {
        if (Shown(g_wnd)) ShowWindow(g_wnd, SW_HIDE);
        g_lastLeft = -1;
        return true;
    }
    if (atFarLeft) {
        POINT fp{ farX, bar.top };
        ScreenToClient(g_taskbar, &fp);
        left = fp.x;
    }

    if (!atFarLeft && g_rebar && IsWindow(g_rebar)) {
        RECT rr;
        GetWindowRect(g_rebar, &rr);
        POINT tl{ rtl ? rr.right : rr.left, rr.top };
        ScreenToClient(g_taskbar, &tl);
        if (tl.x + (rr.right - rr.left) > left && left > tl.x)
            MoveWindow(g_rebar, tl.x, tl.y, left - tl.x, rr.bottom - rr.top, TRUE);
    }

    bool covered = GetWindow(g_wnd, GW_HWNDPREV) != nullptr;
    if (!covered && Shown(g_wnd) && EqualRect(&tr, &g_lastTray) && EqualRect(&bar, &g_lastBar) &&
        EqualRect(&wb, &g_lastWidgets) && g_lastW == g_size.cx && g_lastLeft == left)
        return true;
    g_lastTray = tr;
    g_lastWidgets = wb;
    g_lastBar = bar;
    g_lastW = g_size.cx;
    g_lastLeft = left;
    SetWindowPos(g_wnd, HWND_TOP, left, 0, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

}  // namespace WIDGET_NS
