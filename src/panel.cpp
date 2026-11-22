#include "panel.h"

#include <windowsx.h>

#include <algorithm>

#include "i18n.h"
#include "memory_trim.h"

namespace panel {
namespace {

// ---- model ----------------------------------------------------------------------------------------

struct Row {
    Kind type = Kind::Action;
    std::wstring label, value;  // value: the device status, or the current choice
    wchar_t glyph = 0;
    bool on = false, grayed = false, dim = false;
    UINT id = 0;                // command id, or the first command id of a submenu
    std::wstring key;           // device rows
    RECT rc{};                  // where it was drawn (client coordinates)
};

constexpr int kTabs = 4;  // Devices, System, Appearance, General; the footer is kept apart
const wchar_t* const kTabNames[kTabs] = { L"Devices", L"System", L"Appearance", L"General" };

Host g_host;
HWND g_wnd;
std::vector<Row> g_rows[kTabs + 1];  // [kTabs] = footer buttons
int g_tab = 0;
int g_hover = -1;                    // row index in the current tab, or 100 + tab index, or 200 + footer index
RECT g_tabRc[kTabs]{};
bool g_busy, g_dirty;                // a click is running a command: refreshes wait until it is done
UINT g_dpi = 96;
HFONT g_font, g_bold, g_icons;

int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

std::wstring ItemText(HMENU m, int pos) {
    wchar_t buf[256] = {};
    MENUITEMINFOW mi{ sizeof(mi) };
    mi.fMask = MIIM_STRING;
    mi.dwTypeData = buf;
    mi.cch = ARRAYSIZE(buf) - 1;
    GetMenuItemInfoW(m, pos, TRUE, &mi);
    return buf;
}

// The first command id in a submenu (searching nested submenus too).
UINT FirstId(HMENU m) {
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        MENUITEMINFOW mi{ sizeof(mi) };
        mi.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(m, i, TRUE, &mi) || (mi.fType & MFT_SEPARATOR)) continue;
        if (mi.hSubMenu) {
            if (UINT id = FirstId(mi.hSubMenu)) return id;
        } else if (mi.wID) {
            return mi.wID;
        }
    }
    return 0;
}

// The text of the chosen entry of a choice submenu: its checked radio item (else any checked item).
std::wstring CheckedText(HMENU m) {
    int n = GetMenuItemCount(m);
    std::wstring any;
    for (int i = 0; i < n; i++) {
        MENUITEMINFOW mi{ sizeof(mi) };
        mi.fMask = MIIM_FTYPE | MIIM_STATE;
        if (!GetMenuItemInfoW(m, i, TRUE, &mi) || !(mi.fState & MFS_CHECKED)) continue;
        if (mi.fType & MFT_RADIOCHECK) return ItemText(m, i);
        if (any.empty()) any = ItemText(m, i);
    }
    return any;
}

struct Item {
    int pos;
    UINT id;
    HMENU sub;
    UINT state;
    UINT type;
};

std::vector<Item> TopItems(HMENU m) {
    std::vector<Item> out;
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        MENUITEMINFOW mi{ sizeof(mi) };
        mi.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(m, i, TRUE, &mi) || (mi.fType & MFT_SEPARATOR)) continue;
        UINT id = mi.hSubMenu ? FirstId(mi.hSubMenu) : mi.wID;
        if (id) out.push_back({ i, id, mi.hSubMenu, mi.fState, mi.fType });
    }
    return out;
}

void Load() {
    for (auto& v : g_rows) v.clear();
    std::vector<Device> devices;
    HMENU m = g_host.build(devices);
    size_t device = 0;
    for (const Item& it : TopItems(m)) {
        Place p = g_host.place(it.id);
        if (p.section == Section::Skip) continue;
        Row r;
        r.id = it.id;
        r.label = ItemText(m, it.pos);
        r.on = it.state & MFS_CHECKED;
        r.grayed = it.state & MFS_GRAYED;
        if (p.kind == Kind::Device) {
            if (device >= devices.size()) continue;
            const Device& d = devices[device++];
            r.type = Kind::Device;
            r.key = d.key;
            r.label = d.name;
            r.value = d.status;
            r.glyph = d.glyph;
            r.dim = d.hidden;
        } else if (it.sub) {
            r.type = Kind::Choice;
            r.value = CheckedText(it.sub);
        } else {
            r.type = p.kind == Kind::Device || p.kind == Kind::Choice ? Kind::Action : p.kind;
        }
        int section = p.section == Section::Footer ? kTabs : (int)p.section - (int)Section::Devices;
        g_rows[std::clamp(section, 0, kTabs)].push_back(r);
    }
    g_host.release();
}

// ---- drawing --------------------------------------------------------------------------------------

struct Colors {
    COLORREF bg, text, secondary, line, hover, accent, knob, track;
};

// The Windows accent color as the Settings app uses it on switches: a lighter shade on dark backgrounds and
// a darker one on light backgrounds (AccentPalette holds 8 RGBA shades, lightest first).
COLORREF Accent(bool light) {
    BYTE palette[32] = {};
    DWORD size = sizeof(palette);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette",
                     RRF_RT_REG_BINARY, nullptr, palette, &size) == ERROR_SUCCESS && size >= 32) {
        const BYTE* c = palette + (light ? 4 : 1) * 4;
        return RGB(c[0], c[1], c[2]);
    }
    return light ? RGB(0, 95, 184) : RGB(76, 194, 255);
}

Colors Palette() {
    bool light = g_host.light && g_host.light();
    COLORREF accent = Accent(light);
    if (light) return { RGB(249, 249, 249), RGB(26, 26, 26), RGB(96, 96, 96), RGB(229, 229, 229), RGB(237, 237, 237), accent, RGB(255, 255, 255), RGB(140, 140, 140) };
    return { RGB(32, 32, 32), RGB(255, 255, 255), RGB(170, 170, 170), RGB(56, 56, 56), RGB(45, 45, 45), accent, RGB(0, 0, 0), RGB(150, 150, 150) };
}

HFONT Font(int px, int weight, const wchar_t* face, const wchar_t* fallback) {
    for (const wchar_t* f : { face, fallback }) {
        HFONT h = CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, f);
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

void MakeFonts() {
    for (HFONT* f : { &g_font, &g_bold, &g_icons })
        if (*f) DeleteObject(*f), *f = nullptr;
    g_font = Font(S(14), FW_NORMAL, L"Segoe UI Variable Text", L"Segoe UI");
    g_bold = Font(S(14), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");
    g_icons = Font(S(16), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
}

constexpr int kWidth = 400, kTabH = 44, kRowH = 40, kDeviceH = 48, kFooterH = 56, kPad = 16;

int RowHeight(const Row& r) { return r.type == Kind::Device ? kDeviceH : kRowH; }

int ContentHeight() {
    int most = 0;
    for (int t = 0; t < kTabs; t++) {
        int h = 0;
        for (const Row& r : g_rows[t]) h += RowHeight(r);
        most = std::max(most, h);
    }
    return std::max(most, kRowH * 3);
}

void Text(HDC dc, const std::wstring& s, RECT rc, COLORREF c, HFONT f, UINT flags) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), (int)s.size(), &rc, flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

void Fill(HDC dc, const RECT& rc, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

void RoundBox(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, std::max(1, S(1)), border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

void Ellipse(HDC dc, int cx, int cy, int r, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, std::max(1, S(1)), border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    ::Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

void DrawSwitch(HDC dc, int right, int cy, bool on, bool grayed, const Colors& c) {
    RECT track{ right - S(40), cy - S(10), right, cy + S(10) };
    COLORREF fill = on ? c.accent : c.bg;
    COLORREF border = on ? c.accent : c.track;
    if (grayed) fill = on ? c.line : c.bg, border = c.line;
    RoundBox(dc, track, S(20), fill, border);
    int knob = on ? track.right - S(10) : track.left + S(10);
    Ellipse(dc, knob, cy, S(6), on ? c.knob : (grayed ? c.line : c.track), on ? c.knob : (grayed ? c.line : c.track));
}

void Paint(HDC dc, const RECT& client) {
    Colors c = Palette();
    Fill(dc, client, c.bg);
    SetBkMode(dc, TRANSPARENT);
    const UINT align = i18n::Rtl() ? DT_RIGHT | DT_RTLREADING : DT_LEFT;

    // Tabs.
    int x = S(kPad);
    for (int t = 0; t < kTabs; t++) {
        std::wstring name = T(kTabNames[t]);
        SelectObject(dc, g_bold);
        SIZE sz{};
        GetTextExtentPoint32W(dc, name.c_str(), (int)name.size(), &sz);
        g_tabRc[t] = { x - S(6), 0, x + sz.cx + S(6), S(kTabH) };
        bool active = t == g_tab;
        if (g_hover == 100 + t && !active) Fill(dc, { g_tabRc[t].left, S(8), g_tabRc[t].right, S(kTabH) - S(8) }, c.hover);
        Text(dc, name, g_tabRc[t], active ? c.text : c.secondary, active ? g_bold : g_font, DT_CENTER);
        if (active) Fill(dc, { x, S(kTabH) - S(3), x + sz.cx, S(kTabH) - S(1) }, c.accent);
        x += sz.cx + S(20);
    }
    Fill(dc, { 0, S(kTabH) - S(1), client.right, S(kTabH) }, c.line);

    // Rows of the current tab.
    int y = S(kTabH) + S(4);
    const int left = S(kPad), right = client.right - S(kPad);
    auto& rows = g_rows[g_tab];
    if (rows.empty())
        Text(dc, T(L"No devices with battery info found"), { left, y, right, y + S(kRowH) }, c.secondary, g_font, align);
    for (size_t i = 0; i < rows.size(); i++) {
        Row& r = rows[i];
        int h = S(RowHeight(r));
        r.rc = { 0, y, client.right, y + h };
        if ((int)i == g_hover && !r.grayed) Fill(dc, { S(6), y + S(2), client.right - S(6), y + h - S(2) }, c.hover);
        COLORREF text = r.grayed || r.dim ? c.secondary : c.text;
        int cy = y + h / 2;
        int textRight = right;
        switch (r.type) {
            case Kind::Toggle:
                DrawSwitch(dc, right, cy, r.on, r.grayed, c);
                textRight = right - S(52);
                break;
            case Kind::Radio:
                Ellipse(dc, right - S(10), cy, S(9), c.bg, r.on ? c.accent : c.track);
                if (r.on) Ellipse(dc, right - S(10), cy, S(4), c.accent, c.accent);
                textRight = right - S(30);
                break;
            case Kind::Choice:
            case Kind::Device:
            case Kind::Action: {
                wchar_t chevron = r.type == Kind::Choice ? 0xE70D : (i18n::Rtl() ? 0xE76B : 0xE76C);  // ChevronDown / ChevronLeft / Right
                Text(dc, std::wstring(1, chevron), { right - S(16), y, right, y + h }, c.secondary, g_icons, DT_CENTER);
                textRight = right - S(24);
                if (!r.value.empty()) {
                    SelectObject(dc, g_font);
                    SIZE sz{};
                    GetTextExtentPoint32W(dc, r.value.c_str(), (int)r.value.size(), &sz);
                    int w = std::min<int>(sz.cx, (textRight - left) / 2);
                    Text(dc, r.value, { textRight - w, y, textRight, y + h }, c.secondary, g_font, DT_RIGHT | DT_END_ELLIPSIS);
                    textRight -= w + S(12);
                }
                break;
            }
        }
        int textLeft = left;
        if (r.type == Kind::Device && r.glyph) {
            Text(dc, std::wstring(1, r.glyph), { left, y, left + S(24), y + h }, text, g_icons, DT_LEFT);
            textLeft += S(32);
        }
        Text(dc, r.label, { textLeft, y, textRight, y + h }, text, g_font, align | DT_END_ELLIPSIS);
        y += h;
    }

    // Footer: plain buttons (Refresh now, Exit).
    int fy = client.bottom - S(kFooterH);
    Fill(dc, { 0, fy, client.right, fy + S(1) }, c.line);
    int bx = left;
    auto& foot = g_rows[kTabs];
    for (size_t i = 0; i < foot.size(); i++) {
        Row& r = foot[i];
        SelectObject(dc, g_font);
        SIZE sz{};
        GetTextExtentPoint32W(dc, r.label.c_str(), (int)r.label.size(), &sz);
        int w = sz.cx + S(28);
        bool last = i + 1 == foot.size() && foot.size() > 1;
        RECT b = last ? RECT{ right - w, fy + S(12), right, client.bottom - S(12) } : RECT{ bx, fy + S(12), bx + w, client.bottom - S(12) };
        r.rc = b;
        RoundBox(dc, b, S(8), g_hover == 200 + (int)i ? c.hover : c.bg, c.line);
        Text(dc, r.label, b, c.text, g_font, DT_CENTER);
        bx = b.right + S(8);
    }
}

// ---- interaction ----------------------------------------------------------------------------------

int HitTest(POINT pt) {
    for (int t = 0; t < kTabs; t++)
        if (PtInRect(&g_tabRc[t], pt)) return 100 + t;
    auto& rows = g_rows[g_tab];
    for (size_t i = 0; i < rows.size(); i++)
        if (PtInRect(&rows[i].rc, pt)) return (int)i;
    auto& foot = g_rows[kTabs];
    for (size_t i = 0; i < foot.size(); i++)
        if (PtInRect(&foot[i].rc, pt)) return 200 + (int)i;
    return -1;
}

void Reload() {
    if (!g_wnd) return;
    Load();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// Runs a row: builds the menu now, finds the row's item in it, runs its command or opens its submenu.
void Act(const Row& row) {
    if (row.grayed) return;
    g_busy = true;
    std::vector<Device> devices;
    HMENU m = g_host.build(devices);
    HMENU sub = nullptr;
    UINT cmd = 0;
    size_t device = 0;
    for (const Item& it : TopItems(m)) {
        Place p = g_host.place(it.id);
        if (p.kind == Kind::Device && p.section != Section::Skip) {
            bool match = device < devices.size() && devices[device].key == row.key;
            device++;
            if (row.type == Kind::Device && match) {
                sub = it.sub;
                break;
            }
            continue;
        }
        if (row.type != Kind::Device && it.id == row.id) {
            if (it.sub) sub = it.sub;
            else cmd = it.id;
            break;
        }
    }
    if (sub) {
        // The submenu opens under the row, right-aligned (left-aligned for right-to-left languages).
        POINT pt{ i18n::Rtl() ? row.rc.left + S(kPad) : row.rc.right - S(kPad), row.rc.bottom };
        ClientToScreen(g_wnd, &pt);
        SetForegroundWindow(g_wnd);
        cmd = TrackPopupMenuEx(sub, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_TOPALIGN |
                                        (i18n::Rtl() ? TPM_LEFTALIGN | TPM_LAYOUTRTL : TPM_RIGHTALIGN),
                               pt.x, pt.y, g_wnd, nullptr);
    }
    if (cmd) g_host.run(cmd);  // may close the panel (Exit)
    g_host.release();
    g_busy = false;
    if (g_wnd) {
        g_dirty = false;
        Reload();
    }
}

void Measure(HWND h) {
    RECT rc{ 0, 0, S(kWidth), S(kTabH) + S(4) + S(ContentHeight()) + S(8) + S(kFooterH) };
    AdjustWindowRectExForDpi(&rc, GetWindowLongW(h, GWL_STYLE), FALSE, GetWindowLongW(h, GWL_EXSTYLE), g_dpi);
    // Bottom-right corner of the work area of the monitor the panel is on (bottom-left for right-to-left).
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    int w = rc.right - rc.left, hgt = std::min<int>(rc.bottom - rc.top, mi.rcWork.bottom - mi.rcWork.top);
    int x = i18n::Rtl() ? mi.rcWork.left + S(12) : mi.rcWork.right - w - S(12);
    int y = mi.rcWork.bottom - hgt - S(12);
    SetWindowPos(h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ApplyFrameTheme(HWND h) {
    using SetAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto set = dwm ? reinterpret_cast<SetAttrFn>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute"))) : nullptr;
    if (!set) return;
    BOOL dark = !(g_host.light && g_host.light());
    set(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    HINSTANCE inst = GetModuleHandleW(nullptr);
    LPARAM icon = (LPARAM)LoadImageW(inst, MAKEINTRESOURCEW(dark ? 1 : 2), IMAGE_ICON, GetSystemMetricsForDpi(SM_CXSMICON, g_dpi),
                                     GetSystemMetricsForDpi(SM_CYSMICON, g_dpi), LR_SHARED);
    SendMessageW(h, WM_SETICON, ICON_SMALL, icon);
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_wnd = h;
            g_dpi = GetDpiForWindow(h);
            MakeFonts();
            ApplyFrameTheme(h);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            Paint(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            int hit = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            if (hit != g_hover) {
                g_hover = hit;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_hover != -1) {
                g_hover = -1;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            int hit = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            if (hit >= 100 && hit < 100 + kTabs) {
                g_tab = hit - 100;
                g_hover = -1;
                InvalidateRect(h, nullptr, FALSE);
            } else if (hit >= 200 && hit - 200 < (int)g_rows[kTabs].size()) {
                Row r = g_rows[kTabs][hit - 200];
                Act(r);
            } else if (hit >= 0 && hit < (int)g_rows[g_tab].size()) {
                Row r = g_rows[g_tab][hit];
                Act(r);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) DestroyWindow(h);
            if (wp == VK_TAB || wp == VK_RIGHT || wp == VK_LEFT) {
                bool back = wp == VK_LEFT || (wp == VK_TAB && GetKeyState(VK_SHIFT) < 0);
                g_tab = (g_tab + (back ? kTabs - 1 : 1)) % kTabs;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED: {
            g_dpi = HIWORD(wp);
            MakeFonts();
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lp && !lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet")) {
                ApplyFrameTheme(h);
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        case WM_DESTROY:
            g_wnd = nullptr;
            for (auto& v : g_rows) v.clear();
            for (HFONT* f : { &g_font, &g_bold, &g_icons })
                if (*f) DeleteObject(*f), *f = nullptr;
            memory_trim::Soon();
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

const wchar_t kClass[] = L"WirelessBatteryPanel";

}  // namespace

void Show(HINSTANCE inst, const Host& host) {
    g_host = host;
    if (g_wnd) {
        Reload();
        ShowWindow(g_wnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_wnd);
        return;
    }
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    Load();
    // Created at the cursor so it opens on that monitor, then sized and placed for that monitor's DPI.
    POINT pt;
    GetCursorPos(&pt);
    HWND h = CreateWindowExW(0, kClass, L"Taskbar Vitals", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                             pt.x, pt.y, 100, 100, nullptr, nullptr, inst, nullptr);
    if (!h) return;
    Measure(h);
    ShowWindow(h, SW_SHOWNORMAL);
    SetForegroundWindow(h);
}

void Refresh() {
    if (!g_wnd) return;
    if (g_busy) {  // a command is running from a click: reload once it is done
        g_dirty = true;
        return;
    }
    Reload();
}

void Close() {
    if (g_wnd) DestroyWindow(g_wnd);
}

}  // namespace panel
