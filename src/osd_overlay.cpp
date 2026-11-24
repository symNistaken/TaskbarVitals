#include "osd_overlay.h"

#include "memory_trim.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace osd_overlay {
namespace {

constexpr wchar_t kWindowClass[] = L"TaskbarVitalsOsdWnd";
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kDurationMs = 3500;
constexpr int kFillAlpha = 225;
constexpr int kBorderAlpha = 240;

HWND g_hwnd = nullptr;

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

struct Mask {
    int w = 0;
    int h = 0;
    std::vector<BYTE> a;
};

struct Canvas {
    int w = 0;
    int h = 0;
    std::vector<DWORD> px;  // premultiplied BGRA

    Canvas(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_, 0) {}

    void Blend(int x, int y, Rgb c, int a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        const DWORD d = px[static_cast<size_t>(y) * w + x];
        const int inv = 255 - a;
        const int da = d >> 24;
        const int dr = (d >> 16) & 255;
        const int dg = (d >> 8) & 255;
        const int db = d & 255;
        const int oa = a + da * inv / 255;
        const int orr = c.r * a / 255 + dr * inv / 255;
        const int og = c.g * a / 255 + dg * inv / 255;
        const int ob = c.b * a / 255 + db * inv / 255;
        px[static_cast<size_t>(y) * w + x] =
            (static_cast<DWORD>(oa) << 24) | (static_cast<DWORD>(orr) << 16) |
            (static_cast<DWORD>(og) << 8) | static_cast<DWORD>(ob);
    }

    void Draw(const Mask& m, int x, int y, Rgb c, int alpha) {
        for (int j = 0; j < m.h; ++j)
            for (int i = 0; i < m.w; ++i)
                Blend(x + i, y + j, c, m.a[static_cast<size_t>(j) * m.w + i] * alpha / 255);
    }
};

HFONT MakeFont(int px, int weight, const wchar_t* face, const wchar_t* fallback, int fallbackPx = 0) {
    for (const wchar_t* f : { face, fallback }) {
        const int hPx = (f == fallback && fallbackPx > 0) ? fallbackPx : px;
        HFONT h = CreateFontW(-hPx, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, f);
        if (!h) continue;
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, h);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, old);
        DeleteDC(dc);
        if (!_wcsicmp(got, f) || f == fallback) return h;
        DeleteObject(h);
    }
    return nullptr;
}

Mask RenderMask(const std::wstring& s, HFONT font, bool darkOnLight, int maxWidth = 0) {
    Mask m;
    if (s.empty() || !font) return m;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return m;
    HGDIOBJ of = SelectObject(dc, font);
    int drawW = 0;
    int drawH = 0;
    if (maxWidth > 0) {
        RECT calc{ 0, 0, maxWidth, 0 };
        DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &calc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        drawW = std::min<int>(std::max<int>(1, calc.right), maxWidth);
        drawH = std::max<int>(1, calc.bottom);
    } else {
        SIZE sz{};
        GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
        drawW = std::max<int>(1, sz.cx);
        drawH = std::max<int>(1, sz.cy);
    }
    const int W = drawW + 8;
    const int H = drawH + 8;
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        SelectObject(dc, of);
        DeleteDC(dc);
        return m;
    }
    HGDIOBJ ob = SelectObject(dc, bmp);
    memset(bits, darkOnLight ? 0xFF : 0x00, static_cast<size_t>(W) * H * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, darkOnLight ? RGB(0, 0, 0) : RGB(255, 255, 255));
    if (maxWidth > 0) {
        RECT rc{ 4, 4, 4 + drawW, 4 + drawH };
        DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &rc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        TextOutW(dc, 4, 4, s.c_str(), static_cast<int>(s.size()));
    }
    GdiFlush();
    const auto* p = static_cast<const DWORD*>(bits);
    auto cover = [&](DWORD v) -> BYTE {
        const int c = static_cast<int>((v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF)) / 3;
        return static_cast<BYTE>(darkOnLight ? 255 - c : c);
    };
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (cover(p[y * W + x])) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    if (x1 >= 0) {
        m.w = x1 - x0 + 1;
        m.h = y1 - y0 + 1;
        m.a.resize(static_cast<size_t>(m.w) * m.h);
        for (int y = 0; y < m.h; ++y)
            for (int x = 0; x < m.w; ++x)
                m.a[static_cast<size_t>(y) * m.w + x] = cover(p[(y0 + y) * W + x0 + x]);
    }
    SelectObject(dc, ob);
    SelectObject(dc, of);
    DeleteObject(bmp);
    DeleteDC(dc);
    return m;
}

bool InRoundedRect(float x, float y, float left, float top, float right, float bottom, float r) {
    if (x < left || x >= right || y < top || y >= bottom) return false;
    const float clampedR = std::min(r, std::min(right - left, bottom - top) * 0.5f);
    if (clampedR <= 0.0f) return true;
    const float cx = std::clamp(x, left + clampedR, right - clampedR);
    const float cy = std::clamp(y, top + clampedR, bottom - clampedR);
    const float dx = x - cx;
    const float dy = y - cy;
    return dx * dx + dy * dy <= clampedR * clampedR;
}

void PaintRoundedCard(Canvas& cv, int radius, int border, Rgb fill, Rgb borderCol) {
    const float outerR = static_cast<float>(radius);
    const float innerR = static_cast<float>(std::max(0, radius - border));
    const float fw = static_cast<float>(cv.w);
    const float fh = static_cast<float>(cv.h);
    const float fb = static_cast<float>(border);

    for (int y = 0; y < cv.h; ++y) {
        for (int x = 0; x < cv.w; ++x) {
            int outerHits = 0;
            int innerHits = 0;
            for (int sy = 0; sy < 4; ++sy) {
                const float py = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) * 0.25f;
                for (int sx = 0; sx < 4; ++sx) {
                    const float px = static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) * 0.25f;
                    if (InRoundedRect(px, py, 0.0f, 0.0f, fw, fh, outerR)) {
                        ++outerHits;
                        if (InRoundedRect(px, py, fb, fb, fw - fb, fh - fb, innerR)) ++innerHits;
                    }
                }
            }
            if (outerHits == 0) continue;
            const int fillHits = innerHits;
            const int borderHits = outerHits - innerHits;
            const int oa = (fillHits * kFillAlpha + borderHits * kBorderAlpha) / 16;
            const int orr = (fillHits * fill.r * kFillAlpha + borderHits * borderCol.r * kBorderAlpha) / (16 * 255);
            const int og = (fillHits * fill.g * kFillAlpha + borderHits * borderCol.g * kBorderAlpha) / (16 * 255);
            const int ob = (fillHits * fill.b * kFillAlpha + borderHits * borderCol.b * kBorderAlpha) / (16 * 255);
            cv.px[static_cast<size_t>(y) * cv.w + x] =
                (static_cast<DWORD>(oa) << 24) | (static_cast<DWORD>(orr) << 16) |
                (static_cast<DWORD>(og) << 8) | static_cast<DWORD>(ob);
        }
    }
}

RECT ToWinRect(const refresh_policy::Rect& r) { return { r.left, r.top, r.right, r.bottom }; }

refresh_policy::Rect ToPolicyRect(const RECT& r) { return { r.left, r.top, r.right, r.bottom }; }

bool IsWindowCloaked(HWND w) {
    using DwmGetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
    static const DwmGetWindowAttributeFn fn = []() -> DwmGetWindowAttributeFn {
        HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return dwm ? reinterpret_cast<DwmGetWindowAttributeFn>(
                         reinterpret_cast<void*>(GetProcAddress(dwm, "DwmGetWindowAttribute")))
                   : nullptr;
    }();
    if (!fn) return false;
    DWORD cloaked = 0;
    constexpr DWORD kDwmwaCloaked = 14;
    return SUCCEEDED(fn(w, kDwmwaCloaked, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

bool IsIgnoredWindowClass(HWND w) {
    wchar_t cls[64] = {};
    if (!GetClassNameW(w, cls, ARRAYSIZE(cls))) return false;
    return !_wcsicmp(cls, L"Progman") || !_wcsicmp(cls, L"WorkerW") ||
           !_wcsicmp(cls, L"Shell_TrayWnd") || !_wcsicmp(cls, L"Shell_SecondaryTrayWnd") ||
           !_wcsicmp(cls, kWindowClass);
}

bool WindowCoversMonitor(HWND w, HMONITOR& outMon) {
    outMon = nullptr;
    if (!w || !IsWindowVisible(w) || IsIconic(w)) return false;
    if (IsIgnoredWindowClass(w) || IsWindowCloaked(w)) return false;
    RECT wr{};
    if (!GetWindowRect(w, &wr)) return false;
    HMONITOR mon = MonitorFromWindow(w, MONITOR_DEFAULTTONULL);
    if (!mon) return false;
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(mon, &mi)) return false;
    if (!refresh_policy::CoversMonitor(ToPolicyRect(wr), ToPolicyRect(mi.rcMonitor))) return false;
    outMon = mon;
    return true;
}

struct MonitorEnumItem {
    HMONITOR handle = nullptr;
    osd_policy::MonitorCandidate candidate{};
};

struct WindowScanCtx {
    HWND fg = nullptr;
    std::vector<MonitorEnumItem>* monitors = nullptr;
};

BOOL CALLBACK EnumMonProc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    auto* list = reinterpret_cast<std::vector<MonitorEnumItem>*>(lp);
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(mon, &mi)) return TRUE;
    MonitorEnumItem item;
    item.handle = mon;
    item.candidate.monitorRect = ToPolicyRect(mi.rcMonitor);
    item.candidate.workRect = ToPolicyRect(mi.rcWork);
    item.candidate.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    list->push_back(item);
    return TRUE;
}

BOOL CALLBACK EnumTopWinProc(HWND w, LPARAM lp) {
    auto* ctx = reinterpret_cast<WindowScanCtx*>(lp);
    if (w != ctx->fg) {
        const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        if ((ex & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) != 0) return TRUE;
    }
    HMONITOR mon = nullptr;
    if (WindowCoversMonitor(w, mon)) {
        for (auto& m : *ctx->monitors) {
            if (m.handle == mon) {
                m.candidate.hasFullscreenWindow = true;
                break;
            }
        }
    }
    return TRUE;
}

RECT SelectTargetMonitorAndArea(osd_policy::OsdMode mode, refresh_policy::UserNotificationState qunsState,
                                HMONITOR& outMon) {
    std::vector<MonitorEnumItem> items;
    EnumDisplayMonitors(nullptr, nullptr, EnumMonProc, reinterpret_cast<LPARAM>(&items));
    if (items.empty()) {
        outMon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{ sizeof(mi) };
        if (outMon && GetMonitorInfoW(outMon, &mi)) {
            if (mi.rcWork.right > mi.rcWork.left && mi.rcWork.bottom > mi.rcWork.top) return mi.rcWork;
            if (mi.rcMonitor.right > mi.rcMonitor.left && mi.rcMonitor.bottom > mi.rcMonitor.top) return mi.rcMonitor;
        }
        return { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }

    HMONITOR gameMon = nullptr;
    const HWND fg = GetForegroundWindow();
    if (!WindowCoversMonitor(fg, gameMon) &&
        qunsState == refresh_policy::UserNotificationState::RunningD3DFullScreen) {
        for (const auto& m : items) {
            if (m.candidate.isPrimary) {
                gameMon = m.handle;
                break;
            }
        }
        if (!gameMon) gameMon = items[0].handle;
    }
    if (gameMon) {
        for (auto& m : items) {
            if (m.handle == gameMon) {
                m.candidate.isGameMonitor = true;
                m.candidate.hasFullscreenWindow = true;
                break;
            }
        }
    }

    WindowScanCtx scan{ fg, &items };
    EnumWindows(EnumTopWinProc, reinterpret_cast<LPARAM>(&scan));

    std::vector<osd_policy::MonitorCandidate> candidates;
    candidates.reserve(items.size());
    for (const auto& m : items) candidates.push_back(m.candidate);
    const osd_policy::OsdTarget target = osd_policy::ChooseTargetMonitor(candidates, mode);
    if (target.monitorIndex >= 0 && target.monitorIndex < static_cast<int>(items.size())) {
        outMon = items[target.monitorIndex].handle;
        return ToWinRect(target.area);
    }
    outMon = items[0].handle;
    return ToWinRect(items[0].candidate.monitorRect);
}

LRESULT CALLBACK OsdProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wp == kTimerId) {
                KillTimer(h, kTimerId);
                DestroyWindow(h);
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(h, kTimerId);
            if (g_hwnd == h) g_hwnd = nullptr;
            memory_trim::Soon();
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool EnsureWindow(HINSTANCE inst, const RECT& area, HMONITOR mon) {
    if (g_hwnd && IsWindow(g_hwnd)) {
        if (mon && MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONULL) != mon) {
            SetWindowPos(g_hwnd, nullptr, area.left, area.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return true;
    }
    g_hwnd = nullptr;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = OsdProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kWindowClass, L"", WS_POPUP,
        area.left, area.top, 1, 1,
        nullptr, nullptr, inst, nullptr);
    return g_hwnd != nullptr;
}

}  // namespace

bool IsForegroundFullscreen(HMONITOR* outGameMon) {
    HMONITOR mon = nullptr;
    if (!WindowCoversMonitor(GetForegroundWindow(), mon)) return false;
    if (outGameMon) *outGameMon = mon;
    return true;
}

void Show(HINSTANCE inst, wchar_t glyph, const std::wstring& deviceName, int percent,
          osd_policy::OsdMode mode, osd_policy::OsdCorner corner,
          refresh_policy::UserNotificationState qunsState, bool light,
          COLORREF accentColor, bool hasAccent) {
    HMONITOR mon = nullptr;
    const RECT area = SelectTargetMonitorAndArea(mode, qunsState, mon);
    if (!EnsureWindow(inst, area, mon)) return;

    const UINT rawDpi = GetDpiForWindow(g_hwnd);
    const int dpi = rawDpi ? static_cast<int>(rawDpi) : 96;
    auto px = [dpi](int v) { return MulDiv(v, dpi, 96); };

    HFONT iconFont = MakeFont(px(18), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT nameFont = MakeFont(px(13), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI", px(12));
    HFONT pctFont = MakeFont(px(14), FW_BOLD, L"Segoe UI Variable Text", L"Segoe UI", px(13));

    const wchar_t iconChar = glyph ? glyph : 0xE772;
    const Mask iconMask = RenderMask(std::wstring(1, iconChar), iconFont, light);
    const Mask nameMask = RenderMask(deviceName, nameFont, light, px(220));
    const int clampedPct = std::clamp(percent, 0, 100);
    const Mask pctMask = RenderMask(std::to_wstring(clampedPct) + L"%", pctFont, light);

    if (iconFont) DeleteObject(iconFont);
    if (nameFont) DeleteObject(nameFont);
    if (pctFont) DeleteObject(pctFont);

    const int padX = px(14);
    const int padY = px(11);
    const int iconGap = (iconMask.w > 0 && nameMask.w > 0) ? px(10) : 0;
    const int pctGap = (iconMask.w > 0 || nameMask.w > 0) ? px(12) : 0;
    const int contentW = iconMask.w + iconGap + nameMask.w + pctGap + pctMask.w;
    const int contentH = std::max({ iconMask.h, nameMask.h, pctMask.h, px(18) });
    const int W = std::max(px(120), padX * 2 + contentW);
    const int H = std::max(px(40), padY * 2 + contentH);
    const int radius = px(10);
    const int border = std::max(1, px(1));

    const Rgb fill = light ? Rgb{ 246, 246, 246 } : Rgb{ 32, 32, 32 };
    const Rgb borderCol = light ? Rgb{ 205, 205, 205 } : Rgb{ 72, 72, 72 };
    const Rgb fg = light ? Rgb{ 26, 26, 26 } : Rgb{ 255, 255, 255 };
    const Rgb red = light ? Rgb{ 196, 30, 40 } : Rgb{ 255, 90, 90 };
    const Rgb glyphCol =
        hasAccent ? Rgb{ GetRValue(accentColor), GetGValue(accentColor), GetBValue(accentColor) } : fg;

    Canvas cv(W, H);
    PaintRoundedCard(cv, radius, border, fill, borderCol);

    int x = padX;
    if (iconMask.w > 0) {
        cv.Draw(iconMask, x, (H - iconMask.h) / 2, glyphCol, 255);
        x += iconMask.w + iconGap;
    }
    if (nameMask.w > 0) {
        cv.Draw(nameMask, x, (H - nameMask.h) / 2, fg, 255);
    }
    if (pctMask.w > 0) {
        const int pctX = W - padX - pctMask.w;
        cv.Draw(pctMask, pctX, (H - pctMask.h) / 2, red, 255);
    }

    const osd_policy::OsdPoint origin =
        osd_policy::ComputeOsdOrigin(area.left, area.top, area.right, area.bottom, W, H, px(16), corner);

    HDC screen = GetDC(nullptr);
    if (!screen) return;
    HDC mem = CreateCompatibleDC(screen);
    if (!mem) {
        ReleaseDC(nullptr, screen);
        return;
    }
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool updated = false;
    if (bmp && bits) {
        memcpy(bits, cv.px.data(), static_cast<size_t>(W) * H * 4);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        POINT ptDst{ origin.x, origin.y };
        SIZE sz{ W, H };
        POINT ptSrc{ 0, 0 };
        BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        updated = UpdateLayeredWindow(g_hwnd, screen, &ptDst, &sz, mem, &ptSrc, 0, &bf, ULW_ALPHA) != FALSE;
        SelectObject(mem, oldBmp);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    if (updated) {
        SetWindowPos(g_hwnd, HWND_TOPMOST, origin.x, origin.y, W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetTimer(g_hwnd, kTimerId, kDurationMs, nullptr);
    }
}

void Shutdown() {
    if (g_hwnd) {
        KillTimer(g_hwnd, kTimerId);
        if (IsWindow(g_hwnd)) DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
}

}  // namespace osd_overlay
