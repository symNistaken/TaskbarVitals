#include "panel.h"

#include <windowsx.h>

#include <algorithm>

#include "i18n.h"
#include "memory_trim.h"

namespace panel {
namespace {

constexpr int kTabs = (int)Tab::Count;
const wchar_t* const kTabNames[kTabs] = { L"Batteries", L"System", L"Appearance", L"General" };

constexpr int kWidth = 440;
constexpr int kTabH = 46;
constexpr int kFooterH = 54;
constexpr int kPad = 14;
constexpr int kCardPad = 14;

enum class HitPart { None, Tab, Footer, Row, Up, Down, More, Switch, SegLeft, SegRight };
struct Hit {
    HitPart part = HitPart::None;
    int index = -1;
    bool operator==(const Hit& o) const { return part == o.part && index == o.index; }
    bool operator!=(const Hit& o) const { return !(*this == o); }
};

struct RowLayout {
    RECT rc{};
    RECT rcUp{};
    RECT rcDown{};
    RECT rcMore{};
    RECT rcSwitch{};
    RECT rcSegLeft{};
    RECT rcSegRight{};
};

Host g_host;
HWND g_wnd = nullptr;
Model g_model;
std::vector<RowLayout> g_layout[kTabs];
std::vector<RECT> g_footerRc;
RECT g_tabRc[kTabs]{};
int g_tab = 0;
int g_scrollY[kTabs]{};
int g_contentH[kTabs]{};
Hit g_hover{};
bool g_busy = false, g_dirty = false;
UINT g_dpi = 96;
HFONT g_font = nullptr, g_bold = nullptr, g_small = nullptr, g_smallBold = nullptr;
HFONT g_icons = nullptr, g_smallIcons = nullptr;

int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

void DrawVectorIcon(HDC dc, const RECT& box, vector_icons::VectorIcon icon, COLORREF color, int sizePx) {
    auto mask = vector_icons::Render(icon, sizePx);
    if (mask.w <= 0 || mask.h <= 0 || mask.a.empty()) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = mask.w;
    bi.bmiHeader.biHeight = -mask.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(dc);
    if (!mem) return;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        DeleteDC(mem);
        return;
    }
    const int cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);
    auto* px = static_cast<DWORD*>(bits);
    const int count = mask.w * mask.h;
    for (int i = 0; i < count; ++i) {
        const BYTE a = mask.a[(size_t)i];
        const DWORD pr = (DWORD)(cr * a / 255);
        const DWORD pg = (DWORD)(cg * a / 255);
        const DWORD pb = (DWORD)(cb * a / 255);
        px[i] = ((DWORD)a << 24) | (pr << 16) | (pg << 8) | pb;
    }
    HGDIOBJ old = SelectObject(mem, dib);
    const int dx = box.left + ((box.right - box.left) - mask.w) / 2;
    const int dy = box.top + ((box.bottom - box.top) - mask.h) / 2;
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    GdiAlphaBlend(dc, dx, dy, mask.w, mask.h, mem, 0, 0, mask.w, mask.h, bf);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
}

int RowHeight(const Row& r) {
    switch (r.kind) {
        case RowKind::SectionHeader: return 28;
        case RowKind::DeviceCard:    return 56;
        case RowKind::SystemHeader:  return r.sublabel.empty() ? 48 : 54;
        case RowKind::Toggle:        return r.indented ? 38 : 42;
        case RowKind::Choice:        return 42;
        case RowKind::Action:        return 40;
        case RowKind::Segmented:     return 50;
    }
    return 40;
}

int MeasureTabHeight(int t) {
    const auto& rows = g_model.tabs[t];
    if (rows.empty()) return S(64);
    int y = S(8);
    bool inCard = false;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        if (r.kind == RowKind::SectionHeader) {
            if (inCard) y += S(8);
            inCard = false;
            y += S(RowHeight(r));
            continue;
        }
        bool startCard = !inCard || r.newCard;
        if (startCard && inCard) y += S(8);
        inCard = true;
        y += S(RowHeight(r));
    }
    if (inCard) y += S(12);
    return y;
}

// Computes layout rectangles for `t` at `clientW` with `scrollY` applied to body rows.
void LayoutTab(int t, int clientW, int bodyTop, int scrollY) {
    const auto& rows = g_model.tabs[t];
    auto& lay = g_layout[t];
    lay.assign(rows.size(), RowLayout{});
    const int cardLeft = S(kPad), cardRight = clientW - S(kPad);
    int y = bodyTop + S(8) - scrollY;
    bool inCard = false;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        int h = S(RowHeight(r));
        if (r.kind == RowKind::SectionHeader) {
            if (inCard) y += S(8);
            inCard = false;
            lay[i].rc = { cardLeft, y, cardRight, y + h };
            y += h;
            continue;
        }
        if (inCard && r.newCard) y += S(8);
        inCard = true;
        RECT rc{ cardLeft, y, cardRight, y + h };
        RowLayout& L = lay[i];
        L.rc = rc;
        int cy = y + h / 2;
        int innerRight = cardRight - S(kCardPad);
        int innerLeft = cardLeft + S(kCardPad);
        int btnSize = S(26);
        int btnTop = cy - btnSize / 2;

        if (r.kind == RowKind::DeviceCard) {
            L.rcSwitch = { innerRight - S(40), cy - S(10), innerRight, cy + S(10) };
            int x = L.rcSwitch.left - S(10);
            L.rcMore = { x - btnSize, btnTop, x, btnTop + btnSize };
            x = L.rcMore.left - S(4);
            L.rcDown = { x - btnSize, btnTop, x, btnTop + btnSize };
            x = L.rcDown.left - S(4);
            L.rcUp = { x - btnSize, btnTop, x, btnTop + btnSize };
        } else if (r.kind == RowKind::SystemHeader) {
            int x = innerRight;
            if (r.hasSwitch) {
                L.rcSwitch = { x - S(40), cy - S(10), x, cy + S(10) };
                x = L.rcSwitch.left - S(10);
            }
            L.rcDown = { x - btnSize, btnTop, x, btnTop + btnSize };
            x = L.rcDown.left - S(4);
            L.rcUp = { x - btnSize, btnTop, x, btnTop + btnSize };
        } else if (r.kind == RowKind::Toggle) {
            L.rcSwitch = { innerRight - S(40), cy - S(10), innerRight, cy + S(10) };
        } else if (r.kind == RowKind::Segmented) {
            RECT track{ innerLeft, y + S(7), innerRight, y + h - S(7) };
            int mid = (track.left + track.right) / 2;
            L.rcSegLeft = { track.left + S(3), track.top + S(3), mid - S(1), track.bottom - S(3) };
            L.rcSegRight = { mid + S(1), track.top + S(3), track.right - S(3), track.bottom - S(3) };
        }
        y += h;
    }
}

void ClampScroll(int t, int bodyH) {
    g_contentH[t] = MeasureTabHeight(t);
    int maxScroll = std::max(0, g_contentH[t] - bodyH);
    g_scrollY[t] = std::clamp(g_scrollY[t], 0, maxScroll);
}

void Load() {
    g_model = Model{};
    if (g_host.build) g_host.build(g_model);
    if (g_wnd) {
        RECT rc{};
        GetClientRect(g_wnd, &rc);
        int bodyH = std::max(1, (int)(rc.bottom - S(kFooterH) - S(kTabH)));
        for (int t = 0; t < kTabs; ++t) {
            ClampScroll(t, bodyH);
            LayoutTab(t, rc.right, S(kTabH), g_scrollY[t]);
        }
    }
}

// ---- drawing --------------------------------------------------------------------------------------

struct Colors {
    COLORREF bg, card, cardBorder, divider, hover, pillBg, text, secondary, accent, knob, track;
};

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
    if (light) {
        return {
            RGB(243, 243, 243),  // bg
            RGB(255, 255, 255),  // card
            RGB(224, 224, 224),  // cardBorder
            RGB(234, 234, 234),  // divider
            RGB(240, 240, 240),  // hover
            RGB(238, 238, 238),  // pillBg
            RGB(26, 26, 26),     // text
            RGB(96, 96, 96),     // secondary
            accent,
            RGB(255, 255, 255),  // knob
            RGB(140, 140, 140),  // track
        };
    }
    return {
        RGB(28, 28, 28),     // bg
        RGB(39, 39, 39),     // card
        RGB(56, 56, 56),     // cardBorder
        RGB(50, 50, 50),     // divider
        RGB(52, 52, 52),     // hover
        RGB(49, 49, 49),     // pillBg
        RGB(255, 255, 255),  // text
        RGB(172, 172, 172),  // secondary
        accent,
        RGB(0, 0, 0),        // knob
        RGB(150, 150, 150),  // track
    };
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

void DeleteFonts() {
    for (HFONT* f : { &g_font, &g_bold, &g_small, &g_smallBold, &g_icons, &g_smallIcons })
        if (*f) DeleteObject(*f), *f = nullptr;
}

void MakeFonts() {
    DeleteFonts();
    g_font = Font(S(14), FW_NORMAL, L"Segoe UI Variable Text", L"Segoe UI");
    g_bold = Font(S(14), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");
    g_small = Font(S(12), FW_NORMAL, L"Segoe UI Variable Text", L"Segoe UI");
    g_smallBold = Font(S(11), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");
    g_icons = Font(S(16), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    g_smallIcons = Font(S(12), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
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
    HPEN p = border == CLR_INVALID ? (HPEN)GetStockObject(NULL_PEN) : CreatePen(PS_SOLID, std::max(1, S(1)), border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    if (border != CLR_INVALID) DeleteObject(p);
}

void Ellipse(HDC dc, int cx, int cy, int r, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = border == CLR_INVALID ? (HPEN)GetStockObject(NULL_PEN) : CreatePen(PS_SOLID, std::max(1, S(1)), border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    ::Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    if (border != CLR_INVALID) DeleteObject(p);
}

void DrawSwitch(HDC dc, const RECT& track, bool on, bool grayed, const Colors& c) {
    int cy = (track.top + track.bottom) / 2;
    COLORREF fill = on ? c.accent : c.card;
    COLORREF border = on ? c.accent : c.track;
    if (grayed) fill = on ? c.cardBorder : c.card, border = c.cardBorder;
    RoundBox(dc, track, S(20), fill, border);
    int knob = on ? track.right - S(10) : track.left + S(10);
    COLORREF kc = on ? c.knob : (grayed ? c.cardBorder : c.track);
    Ellipse(dc, knob, cy, S(6), kc, kc);
}

void DrawMiniButton(HDC dc, const RECT& rc, wchar_t glyph, bool enabled, bool hovered, const Colors& c) {
    COLORREF bg = hovered && enabled ? c.hover : c.pillBg;
    RoundBox(dc, rc, S(6), bg, c.cardBorder);
    Text(dc, std::wstring(1, glyph), rc, enabled ? c.text : c.cardBorder, g_smallIcons, DT_CENTER);
}

void Paint(HDC dc, const RECT& client) {
    Colors c = Palette();
    Fill(dc, client, c.bg);
    SetBkMode(dc, TRANSPARENT);
    const UINT align = i18n::Rtl() ? DT_RIGHT | DT_RTLREADING : DT_LEFT;

    const int bodyTop = S(kTabH);
    const int bodyBottom = client.bottom - S(kFooterH);
    const int bodyH = std::max(1, bodyBottom - bodyTop);
    ClampScroll(g_tab, bodyH);
    LayoutTab(g_tab, client.right, bodyTop, g_scrollY[g_tab]);

    // ---- Scrollable body ----
    int saved = SaveDC(dc);
    IntersectClipRect(dc, 0, bodyTop, client.right, bodyBottom);

    const auto& rows = g_model.tabs[g_tab];
    const auto& lay = g_layout[g_tab];

    // 1. Draw rounded card backgrounds for contiguous non-header groups.
    struct CardSpan { size_t first, last; int top, bottom; };
    std::vector<CardSpan> cards;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].kind == RowKind::SectionHeader) continue;
        if (cards.empty() || rows[i].newCard || rows[i - 1].kind == RowKind::SectionHeader) {
            cards.push_back({ i, i, lay[i].rc.top, lay[i].rc.bottom });
        } else {
            cards.back().last = i;
            cards.back().bottom = lay[i].rc.bottom;
        }
    }
    const int cardLeft = S(kPad), cardRight = client.right - S(kPad);
    for (const auto& cs : cards) {
        RoundBox(dc, { cardLeft, cs.top, cardRight, cs.bottom }, S(10), c.card, c.cardBorder);
        for (size_t i = cs.first; i < cs.last; ++i) {
            int dy = lay[i].rc.bottom;
            Fill(dc, { cardLeft + S(kCardPad), dy - 1, cardRight - S(kCardPad), dy }, c.divider);
        }
    }

    // 2. Draw row contents.
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const RowLayout& L = lay[i];
        const RECT& rc = L.rc;
        if (rc.bottom < bodyTop || rc.top > bodyBottom) continue;

        if (r.kind == RowKind::SectionHeader) {
            Text(dc, r.label, { rc.left + S(4), rc.top + S(4), rc.right - S(4), rc.bottom },
                 c.secondary, g_smallBold, align | DT_END_ELLIPSIS);
            continue;
        }

        bool rowHovered = g_hover.index == (int)i &&
                          (g_hover.part == HitPart::Row || g_hover.part == HitPart::Up ||
                           g_hover.part == HitPart::Down || g_hover.part == HitPart::More ||
                           g_hover.part == HitPart::Switch);
        if (rowHovered && !r.grayed && r.kind != RowKind::Segmented) {
            RoundBox(dc, { rc.left + S(4), rc.top + S(2), rc.right - S(4), rc.bottom - S(2) },
                     S(6), c.hover, CLR_INVALID);
        }

        COLORREF mainText = (r.grayed || r.dim) ? c.secondary : c.text;
        int innerLeft = rc.left + S(kCardPad) + (r.indented ? S(28) : 0);
        int innerRight = rc.right - S(kCardPad);

        if (r.kind == RowKind::DeviceCard) {
            // Left icon badge
            int iconBox = S(34);
            int iconTop = rc.top + (rc.bottom - rc.top - iconBox) / 2;
            RECT iconRc{ innerLeft, iconTop, innerLeft + iconBox, iconTop + iconBox };
            RoundBox(dc, iconRc, S(8), c.pillBg, CLR_INVALID);
            COLORREF glyphCol = (r.hasAccent && !r.dim && !r.grayed) ? r.accentColor : mainText;
            if (r.vectorIcon != vector_icons::VectorIcon::None)
                DrawVectorIcon(dc, iconRc, r.vectorIcon, glyphCol, S(18));
            else if (r.glyph)
                Text(dc, std::wstring(1, r.glyph), iconRc, glyphCol, g_icons, DT_CENTER);

            // Right controls: [↑] [↓] [⋯] [Switch]
            bool hovUp = g_hover.index == (int)i && g_hover.part == HitPart::Up;
            bool hovDn = g_hover.index == (int)i && g_hover.part == HitPart::Down;
            bool hovMr = g_hover.index == (int)i && g_hover.part == HitPart::More;
            DrawMiniButton(dc, L.rcUp, 0xE74A, r.canUp && !r.grayed, hovUp, c);
            DrawMiniButton(dc, L.rcDown, 0xE74B, r.canDown && !r.grayed, hovDn, c);
            DrawMiniButton(dc, L.rcMore, 0xE712, !r.grayed && r.id2 != 0, hovMr, c);
            DrawSwitch(dc, L.rcSwitch, r.on, r.grayed, c);

            int textLeft = iconRc.right + S(12);
            int textRight = L.rcUp.left - S(10);
            int midY = (rc.top + rc.bottom) / 2;
            Text(dc, r.label, { textLeft, rc.top + S(7), textRight, midY + S(1) },
                 mainText, g_bold, align | DT_END_ELLIPSIS);
            Text(dc, r.sublabel, { textLeft, midY + S(1), textRight, rc.bottom - S(7) },
                 c.secondary, g_small, align | DT_END_ELLIPSIS);
        } else if (r.kind == RowKind::SystemHeader) {
            int iconBox = S(30);
            int iconTop = rc.top + (rc.bottom - rc.top - iconBox) / 2;
            RECT iconRc{ innerLeft, iconTop, innerLeft + iconBox, iconTop + iconBox };
            RoundBox(dc, iconRc, S(8), c.pillBg, CLR_INVALID);
            COLORREF glyphCol = (r.hasAccent && !r.grayed) ? r.accentColor : mainText;
            if (r.vectorIcon != vector_icons::VectorIcon::None)
                DrawVectorIcon(dc, iconRc, r.vectorIcon, glyphCol, S(17));
            else if (r.glyph)
                Text(dc, std::wstring(1, r.glyph), iconRc, glyphCol, g_icons, DT_CENTER);

            bool hovUp = g_hover.index == (int)i && g_hover.part == HitPart::Up;
            bool hovDn = g_hover.index == (int)i && g_hover.part == HitPart::Down;
            DrawMiniButton(dc, L.rcUp, 0xE74A, r.canUp && !r.grayed, hovUp, c);
            DrawMiniButton(dc, L.rcDown, 0xE74B, r.canDown && !r.grayed, hovDn, c);
            if (r.hasSwitch) DrawSwitch(dc, L.rcSwitch, r.on, r.grayed, c);

            int badgeRight = L.rcUp.left - S(8);
            int textLeft = iconRc.right + S(10);
            int textRight = badgeRight;
            if (!r.value.empty()) {
                SelectObject(dc, g_small);
                SIZE sz{};
                GetTextExtentPoint32W(dc, r.value.c_str(), (int)r.value.size(), &sz);
                int maxBadgeW = std::max(S(48), (badgeRight - textLeft) / 2);
                int badgeW = std::min<int>(sz.cx + S(16), maxBadgeW);
                int badgeH = S(24);
                int cy = (rc.top + rc.bottom) / 2;
                RECT brc{ badgeRight - badgeW, cy - badgeH / 2, badgeRight, cy + badgeH / 2 };
                RoundBox(dc, brc, S(6), c.pillBg, c.cardBorder);
                Text(dc, r.value, { brc.left + S(6), brc.top, brc.right - S(6), brc.bottom },
                     r.dim ? c.secondary : c.text, g_small, DT_CENTER | DT_END_ELLIPSIS);
                textRight = brc.left - S(8);
            }
            if (r.sublabel.empty()) {
                Text(dc, r.label, { textLeft, rc.top, textRight, rc.bottom },
                     mainText, g_bold, align | DT_END_ELLIPSIS);
            } else {
                int midY = (rc.top + rc.bottom) / 2;
                Text(dc, r.label, { textLeft, rc.top + S(6), textRight, midY + S(1) },
                     mainText, g_bold, align | DT_END_ELLIPSIS);
                Text(dc, r.sublabel, { textLeft, midY + S(1), textRight, rc.bottom - S(6) },
                     c.secondary, g_small, align | DT_END_ELLIPSIS);
            }
        } else if (r.kind == RowKind::Toggle) {
            int textRight = L.rcSwitch.left - S(12);
            Text(dc, r.label, { innerLeft, rc.top, textRight, rc.bottom },
                 mainText, r.indented ? g_small : g_font, align | DT_END_ELLIPSIS);
            DrawSwitch(dc, L.rcSwitch, r.on, r.grayed, c);
        } else if (r.kind == RowKind::Choice) {
            int textRight = innerRight - S(12);
            if (!r.value.empty()) {
                SelectObject(dc, g_small);
                SIZE sz{};
                GetTextExtentPoint32W(dc, r.value.c_str(), (int)r.value.size(), &sz);
                int maxPillW = std::max(S(70), (innerRight - innerLeft) / 2);
                int pillW = std::min<int>(sz.cx + S(32), maxPillW);
                int pillH = S(28);
                int cy = (rc.top + rc.bottom) / 2;
                RECT prc{ innerRight - pillW, cy - pillH / 2, innerRight, cy + pillH / 2 };
                RoundBox(dc, prc, S(6), c.pillBg, c.cardBorder);
                Text(dc, r.value, { prc.left + S(10), prc.top, prc.right - S(22), prc.bottom },
                     mainText, g_small, DT_LEFT | DT_END_ELLIPSIS);
                Text(dc, std::wstring(1, 0xE70D), { prc.right - S(20), prc.top, prc.right - S(6), prc.bottom },
                     c.secondary, g_smallIcons, DT_CENTER);
                textRight = prc.left - S(12);
            } else {
                Text(dc, std::wstring(1, 0xE70D), { innerRight - S(18), rc.top, innerRight, rc.bottom },
                     c.secondary, g_smallIcons, DT_CENTER);
                textRight = innerRight - S(24);
            }
            Text(dc, r.label, { innerLeft, rc.top, textRight, rc.bottom },
                 mainText, g_font, align | DT_END_ELLIPSIS);
        } else if (r.kind == RowKind::Action) {
            int chevronW = r.id ? S(22) : 0;
            if (r.id != 0)
                Text(dc, std::wstring(1, i18n::Rtl() ? 0xE76B : 0xE76C),
                     { innerRight - S(16), rc.top, innerRight, rc.bottom }, c.secondary, g_smallIcons, DT_CENTER);
            int textRight = innerRight - chevronW;
            if (!r.value.empty()) {
                SelectObject(dc, g_small);
                SIZE sz{};
                GetTextExtentPoint32W(dc, r.value.c_str(), (int)r.value.size(), &sz);
                int maxBadgeW = std::max(S(48), (textRight - innerLeft) / 2);
                int badgeW = std::min<int>(sz.cx + S(16), maxBadgeW);
                int badgeH = S(24);
                int cy = (rc.top + rc.bottom) / 2;
                int badgeRight = textRight - S(4);
                RECT brc{ badgeRight - badgeW, cy - badgeH / 2, badgeRight, cy + badgeH / 2 };
                RoundBox(dc, brc, S(6), c.pillBg, c.cardBorder);
                Text(dc, r.value, { brc.left + S(6), brc.top, brc.right - S(6), brc.bottom },
                     r.dim ? c.secondary : c.text, g_small, DT_CENTER | DT_END_ELLIPSIS);
                textRight = brc.left - S(8);
            }
            Text(dc, r.label, { innerLeft, rc.top, textRight, rc.bottom },
                 mainText, r.indented ? g_small : g_font, align | DT_END_ELLIPSIS);
        } else if (r.kind == RowKind::Segmented) {
            RECT track{ innerLeft, rc.top + S(7), innerRight, rc.bottom - S(7) };
            RoundBox(dc, track, S(8), c.pillBg, c.cardBorder);
            auto drawSeg = [&](const RECT& segRc, const std::wstring& txt, bool active, bool hov) {
                if (active) RoundBox(dc, segRc, S(6), c.accent, c.accent);
                else if (hov) RoundBox(dc, segRc, S(6), c.hover, CLR_INVALID);
                Text(dc, txt, segRc, active ? c.knob : c.text, active ? g_bold : g_font, DT_CENTER | DT_END_ELLIPSIS);
            };
            bool hovL = g_hover.index == (int)i && g_hover.part == HitPart::SegLeft;
            bool hovR = g_hover.index == (int)i && g_hover.part == HitPart::SegRight;
            drawSeg(L.rcSegLeft, r.label, r.on, hovL);
            drawSeg(L.rcSegRight, r.sublabel, !r.on, hovR);
        }
    }

    // Subtle scrollbar thumb when content exceeds viewport.
    int maxScroll = std::max(0, g_contentH[g_tab] - bodyH);
    if (maxScroll > 0 && g_contentH[g_tab] > 0) {
        int trackH = bodyH - S(8);
        int thumbH = std::max(S(24), MulDiv(trackH, bodyH, g_contentH[g_tab]));
        int thumbY = bodyTop + S(4) + MulDiv(trackH - thumbH, g_scrollY[g_tab], maxScroll);
        RoundBox(dc, { client.right - S(5), thumbY, client.right - S(2), thumbY + thumbH },
                 S(2), c.track, CLR_INVALID);
    }

    RestoreDC(dc, saved);

    // ---- Sticky header tabs ----
    Fill(dc, { 0, 0, client.right, bodyTop }, c.card);
    int x = S(kPad);
    for (int t = 0; t < kTabs; ++t) {
        std::wstring name = T(kTabNames[t]);
        SelectObject(dc, g_bold);
        SIZE sz{};
        GetTextExtentPoint32W(dc, name.c_str(), (int)name.size(), &sz);
        g_tabRc[t] = { x - S(8), S(6), x + sz.cx + S(8), bodyTop - S(6) };
        bool active = t == g_tab;
        if (g_hover.part == HitPart::Tab && g_hover.index == t && !active)
            RoundBox(dc, g_tabRc[t], S(6), c.hover, CLR_INVALID);
        Text(dc, name, { g_tabRc[t].left, 0, g_tabRc[t].right, bodyTop },
             active ? c.text : c.secondary, active ? g_bold : g_font, DT_CENTER);
        if (active) RoundBox(dc, { x, bodyTop - S(3), x + sz.cx, bodyTop }, S(2), c.accent, CLR_INVALID);
        x += sz.cx + S(20);
    }
    Fill(dc, { 0, bodyTop - 1, client.right, bodyTop }, c.cardBorder);

    // ---- Sticky footer ----
    Fill(dc, { 0, bodyBottom, client.right, client.bottom }, c.card);
    Fill(dc, { 0, bodyBottom, client.right, bodyBottom + 1 }, c.cardBorder);
    int bx = S(kPad);
    const int right = client.right - S(kPad);
    g_footerRc.resize(g_model.footer.size());
    for (size_t i = 0; i < g_model.footer.size(); ++i) {
        const FooterButton& fb = g_model.footer[i];
        SelectObject(dc, g_font);
        SIZE sz{};
        GetTextExtentPoint32W(dc, fb.label.c_str(), (int)fb.label.size(), &sz);
        int iconW = fb.glyph ? S(20) : 0;
        int w = sz.cx + iconW + S(26);
        bool last = i + 1 == g_model.footer.size() && g_model.footer.size() > 1;
        RECT b = last ? RECT{ right - w, bodyBottom + S(10), right, client.bottom - S(10) }
                      : RECT{ bx, bodyBottom + S(10), bx + w, client.bottom - S(10) };
        g_footerRc[i] = b;
        bool hov = g_hover.part == HitPart::Footer && g_hover.index == (int)i;
        RoundBox(dc, b, S(8), hov ? c.hover : c.pillBg, c.cardBorder);
        if (fb.glyph) {
            Text(dc, std::wstring(1, fb.glyph), { b.left + S(10), b.top, b.left + S(26), b.bottom },
                 c.text, g_smallIcons, DT_CENTER);
            Text(dc, fb.label, { b.left + S(28), b.top, b.right - S(8), b.bottom }, c.text, g_font, DT_LEFT);
        } else {
            Text(dc, fb.label, b, c.text, g_font, DT_CENTER);
        }
        bx = b.right + S(8);
    }
}

// ---- interaction ----------------------------------------------------------------------------------

Hit HitTest(POINT pt) {
    for (int t = 0; t < kTabs; ++t)
        if (PtInRect(&g_tabRc[t], pt)) return { HitPart::Tab, t };
    for (size_t i = 0; i < g_footerRc.size(); ++i)
        if (PtInRect(&g_footerRc[i], pt)) return { HitPart::Footer, (int)i };

    RECT client{};
    if (!g_wnd || !GetClientRect(g_wnd, &client)) return {};
    int bodyTop = S(kTabH), bodyBottom = client.bottom - S(kFooterH);
    if (pt.y < bodyTop || pt.y >= bodyBottom) return {};

    const auto& rows = g_model.tabs[g_tab];
    const auto& lay = g_layout[g_tab];
    for (size_t i = 0; i < rows.size() && i < lay.size(); ++i) {
        if (rows[i].kind == RowKind::SectionHeader) continue;
        if (!PtInRect(&lay[i].rc, pt)) continue;
        const Row& r = rows[i];
        const RowLayout& L = lay[i];
        if (r.kind == RowKind::DeviceCard) {
            if (PtInRect(&L.rcUp, pt)) return { HitPart::Up, (int)i };
            if (PtInRect(&L.rcDown, pt)) return { HitPart::Down, (int)i };
            if (PtInRect(&L.rcMore, pt)) return { HitPart::More, (int)i };
            if (PtInRect(&L.rcSwitch, pt)) return { HitPart::Switch, (int)i };
            return { HitPart::Row, (int)i };
        }
        if (r.kind == RowKind::SystemHeader) {
            if (PtInRect(&L.rcUp, pt)) return { HitPart::Up, (int)i };
            if (PtInRect(&L.rcDown, pt)) return { HitPart::Down, (int)i };
            if (r.hasSwitch && PtInRect(&L.rcSwitch, pt)) return { HitPart::Switch, (int)i };
            return { HitPart::Row, (int)i };
        }
        if (r.kind == RowKind::Segmented) {
            if (PtInRect(&L.rcSegLeft, pt)) return { HitPart::SegLeft, (int)i };
            if (PtInRect(&L.rcSegRight, pt)) return { HitPart::SegRight, (int)i };
            return {};
        }
        return { HitPart::Row, (int)i };
    }
    return {};
}

void Reload() {
    if (!g_wnd) return;
    Load();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void RunCommand(UINT cmd) {
    if (!cmd) return;
    g_busy = true;
    if (g_host.run) g_host.run(cmd);
    g_busy = false;
    if (g_wnd) {
        g_dirty = false;
        Reload();
    }
}

void OpenPopup(UINT popupId, POINT clientPt) {
    if (!popupId || !g_host.popup) return;
    g_busy = true;
    ClientToScreen(g_wnd, &clientPt);
    SetForegroundWindow(g_wnd);
    UINT cmd = g_host.popup(popupId, g_wnd, clientPt);
    if (cmd && g_host.run) g_host.run(cmd);
    g_busy = false;
    if (g_wnd) {
        g_dirty = false;
        Reload();
    }
}

void HandleHit(const Hit& hit) {
    if (hit.part == HitPart::Tab) {
        g_tab = hit.index;
        g_hover = {};
        InvalidateRect(g_wnd, nullptr, FALSE);
        return;
    }
    if (hit.part == HitPart::Footer && hit.index >= 0 && hit.index < (int)g_model.footer.size()) {
        RunCommand(g_model.footer[hit.index].id);
        return;
    }
    const auto& rows = g_model.tabs[g_tab];
    const auto& lay = g_layout[g_tab];
    if (hit.index < 0 || hit.index >= (int)rows.size() || hit.index >= (int)lay.size()) return;
    const Row& r = rows[hit.index];
    const RowLayout& L = lay[hit.index];
    if (r.grayed) return;

    switch (hit.part) {
        case HitPart::Up:
            if (r.canUp) RunCommand(r.upId);
            break;
        case HitPart::Down:
            if (r.canDown) RunCommand(r.downId);
            break;
        case HitPart::More:
            if (r.id2) OpenPopup(r.id2, { L.rcMore.right, L.rcMore.bottom });
            break;
        case HitPart::Switch:
        case HitPart::SegLeft:
            RunCommand(r.id);
            break;
        case HitPart::SegRight:
            RunCommand(r.id2);
            break;
        case HitPart::Row:
            if (r.kind == RowKind::Choice) {
                OpenPopup(r.id, { i18n::Rtl() ? L.rc.left + S(kCardPad) : L.rc.right - S(kCardPad), L.rc.bottom });
            } else if (r.kind == RowKind::DeviceCard) {
                if (r.id2) OpenPopup(r.id2, { i18n::Rtl() ? L.rc.left + S(kCardPad) : L.rc.right - S(kCardPad), L.rc.bottom });
            } else if (r.kind == RowKind::SystemHeader) {
                if (r.hasSwitch) RunCommand(r.id);
            } else if (r.kind == RowKind::Toggle || r.kind == RowKind::Action) {
                RunCommand(r.id);
            }
            break;
        case HitPart::None:
        case HitPart::Tab:
        case HitPart::Footer:
            break;
    }
}

void Measure(HWND h) {
    int most = 0;
    for (int t = 0; t < kTabs; ++t) most = std::max(most, MeasureTabHeight(t));
    int bodyH = std::clamp(most, S(300), S(540));
    RECT rc{ 0, 0, S(kWidth), S(kTabH) + bodyH + S(kFooterH) };
    AdjustWindowRectExForDpi(&rc, GetWindowLongW(h, GWL_STYLE), FALSE, GetWindowLongW(h, GWL_EXSTYLE), g_dpi);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    int w = rc.right - rc.left;
    int hgt = std::min<int>(rc.bottom - rc.top, mi.rcWork.bottom - mi.rcWork.top - S(16));
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
            Hit hit = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            if (hit != g_hover) {
                g_hover = hit;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_hover.part != HitPart::None) {
                g_hover = {};
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            RECT rc{};
            GetClientRect(h, &rc);
            int bodyH = std::max(1, (int)(rc.bottom - S(kFooterH) - S(kTabH)));
            int maxScroll = std::max(0, g_contentH[g_tab] - bodyH);
            int next = std::clamp(g_scrollY[g_tab] - MulDiv(delta, S(44), WHEEL_DELTA), 0, maxScroll);
            if (next != g_scrollY[g_tab]) {
                g_scrollY[g_tab] = next;
                LayoutTab(g_tab, rc.right, S(kTabH), g_scrollY[g_tab]);
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(h, &pt);
                g_hover = HitTest(pt);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            HandleHit(HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }));
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) DestroyWindow(h);
            if (wp == VK_TAB || wp == VK_RIGHT || wp == VK_LEFT) {
                bool back = wp == VK_LEFT || (wp == VK_TAB && GetKeyState(VK_SHIFT) < 0);
                g_tab = (g_tab + (back ? kTabs - 1 : 1)) % kTabs;
                g_hover = {};
                InvalidateRect(h, nullptr, FALSE);
            } else if (wp == VK_DOWN || wp == VK_UP || wp == VK_NEXT || wp == VK_PRIOR) {
                RECT rc{};
                GetClientRect(h, &rc);
                int bodyH = std::max(1, (int)(rc.bottom - S(kFooterH) - S(kTabH)));
                int maxScroll = std::max(0, g_contentH[g_tab] - bodyH);
                int step = (wp == VK_NEXT || wp == VK_PRIOR) ? bodyH - S(40) : S(40);
                int dir = (wp == VK_DOWN || wp == VK_NEXT) ? 1 : -1;
                int next = std::clamp(g_scrollY[g_tab] + dir * step, 0, maxScroll);
                if (next != g_scrollY[g_tab]) {
                    g_scrollY[g_tab] = next;
                    InvalidateRect(h, nullptr, FALSE);
                }
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
            g_model = Model{};
            for (auto& v : g_layout) v.clear();
            g_footerRc.clear();
            DeleteFonts();
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
    Reload();
    ShowWindow(h, SW_SHOWNORMAL);
    SetForegroundWindow(h);
}

void Refresh() {
    if (!g_wnd) return;
    if (g_busy) {
        g_dirty = true;
        return;
    }
    Reload();
}

void Close() {
    if (g_wnd) DestroyWindow(g_wnd);
}

}  // namespace panel
