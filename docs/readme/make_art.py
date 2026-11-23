"""Generates the README illustrations in docs/readme/ (light and dark variants).

The logo is read from res/app.svg, so any icon update appears automatically after running:
  py -X utf8 docs/readme/make_art.py
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

FONT = "'Segoe UI Variable Text','Segoe UI',system-ui,-apple-system,'Helvetica Neue',Arial,sans-serif"

THEMES = {
    "dark": dict(
        bg="#0f1115",
        bg_top="#161b24",
        edge="#252a34",
        accent="#4cc2ff",
        accent_soft="rgba(76,194,255,0.12)",
        accent_edge="rgba(76,194,255,0.32)",
        pill_bg="#181d26",
        pill_edge="#2b3240",
        bar="#1a1d23",
        bar_edge="#2d333f",
        fg="#f3f5f8",
        muted="#9aa3b2",
        low="#ff7b72",
        charge="#4cc2ff",
        full="#56d364",
        heat="#f0b429",
        divider="#2c323e",
        caption_bg="#e6edf3",
        caption_fg="#161b22",
        icon_tile="#1b2230",
        icon_tile_edge="#2f3a4f",
    ),
    "light": dict(
        bg="#f8fafc",
        bg_top="#eef4fb",
        edge="#d6dde6",
        accent="#0067c0",
        accent_soft="rgba(0,103,192,0.08)",
        accent_edge="rgba(0,103,192,0.25)",
        pill_bg="#ffffff",
        pill_edge="#d5dde7",
        bar="#e9eef5",
        bar_edge="#cdd6e2",
        fg="#161b22",
        muted="#57606a",
        low="#cf222e",
        charge="#0969da",
        full="#1a7f37",
        heat="#bf8700",
        divider="#d0d7de",
        caption_bg="#1f2328",
        caption_fg="#ffffff",
        icon_tile="#ffffff",
        icon_tile_edge="#cfd8e3",
    ),
}

# 24x24 Fluent-style stroke icons
ICONS = {
    "headset": '<path d="M4 14v-2a8 8 0 0 1 16 0v2"/><rect x="3" y="13.5" width="4.5" height="7" rx="1.6"/>'
               '<rect x="16.5" y="13.5" width="4.5" height="7" rx="1.6"/>',
    "mouse": '<rect x="6.5" y="3" width="11" height="18" rx="5.5"/><path d="M12 3.5v5.5"/>',
    "keyboard": '<rect x="2.5" y="6" width="19" height="12" rx="2"/>'
                '<path d="M6 9.6h.01M9.3 9.6h.01M12.6 9.6h.01M15.9 9.6h.01M18 9.6h.01'
                'M7.5 12.2h.01M10.8 12.2h.01M14.1 12.2h.01M16.6 12.2h.01M8 15h8"/>',
    "controller": '<path d="M7.2 7.5h9.6a4 4 0 0 1 3.9 3.2l1.1 5.1a2.4 2.4 0 0 1-4.1 2.1L15.8 16H8.2l-1.9 1.9'
                  'a2.4 2.4 0 0 1-4.1-2.1l1.1-5.1a4 4 0 0 1 3.9-3.2z"/>'
                  '<path d="M7.5 10.6v3.2M5.9 12.2h3.2M15.6 11.4h.01M17.8 13.2h.01"/>',
    "cpu": '<rect x="6.5" y="6.5" width="11" height="11" rx="1.8"/><rect x="9.8" y="9.8" width="4.4" height="4.4" rx=".6"/>'
           '<path d="M9.5 3v3.5M14.5 3v3.5M9.5 17.5V21M14.5 17.5V21M3 9.5h3.5M3 14.5h3.5M17.5 9.5H21M17.5 14.5H21"/>',
    "ram": '<rect x="2.5" y="7" width="19" height="9" rx="1.6"/><path d="M6.5 10.5v2.5M10 10.5v2.5M14 10.5v2.5M17.5 10.5v2.5'
           'M5 16v2.5M9 16v2.5M13 16v2.5M17 16v2.5"/>',
    "gpu": '<rect x="2.5" y="4.5" width="19" height="12.5" rx="1.8"/><path d="M9 20.5h6M12 17v3.5"/>',
    "chevron": '<path d="M7 14.5l5-5 5 5"/>',
}
BOLT = '<path d="M13.5 2 5 13.5h6L9.8 22 19 9.8h-6.2z"/>'
CHECK = '<path d="M4.5 12.5l4.6 4.6L19.5 6.7"/>'

ADV = {
    **{d: 0.56 for d in "0123456789"},
    "%": 0.84, ":": 0.27, ".": 0.27, " ": 0.27, "°": 0.42, "W": 0.94,
    "·": 0.28, "/": 0.39, "+": 0.68, "-": 0.38, "M": 0.88, "X": 0.64,
    "C": 0.66, "P": 0.60, "U": 0.68, "R": 0.63, "A": 0.65, "G": 0.69,
}


def text_w(s, size):
    return sum(ADV.get(c, 0.58) for c in s) * size


def icon(name, x, y, size, color, sw=1.65):
    k = size / 24
    return (
        f'<g transform="translate({x:.1f} {y:.1f}) scale({k:.4f})" fill="none" stroke="{color}" '
        f'stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round">{ICONS[name]}</g>'
    )


def glyph(path, x, y, size, color, filled):
    k = size / 24
    paint = (
        f'fill="{color}" stroke="none"'
        if filled
        else f'fill="none" stroke="{color}" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"'
    )
    return f'<g transform="translate({x:.1f} {y:.1f}) scale({k:.4f})" {paint}>{path}</g>'


def strip_item(t, x, cy, kind, pct, s, state=None, caption=None, dim=False, label=None, bar_color=None):
    """Renders one taskbar strip item starting at x. Returns (svg, width)."""
    isz, fsz = 19 * s, 12.5 * s
    parts = [icon(kind, x, cy - isz / 2, isz, t["fg"])]
    cw = 0.0
    if caption:
        cw = text_w(caption, 7.5 * s) + 4.5 * s
        cx, cyy = x + isz - cw / 2, cy + isz / 2 - 9 * s
        parts.append(
            f'<rect x="{cx:.1f}" y="{cyy:.1f}" width="{cw:.1f}" height="{10.5 * s:.1f}" rx="{2.5 * s:.1f}" '
            f'fill="{t["caption_bg"]}"/>'
            f'<text x="{cx + cw / 2:.1f}" y="{cyy + 8.0 * s:.1f}" font-size="{7.5 * s:.1f}" font-weight="700" '
            f'text-anchor="middle" fill="{t["caption_fg"]}">{caption}</text>'
        )
    tx = x + isz + (5.5 + (cw / s) / 2 if caption else 5.5) * s
    if state == "charging":
        parts.append(glyph(BOLT, tx - 1 * s, cy - 6.5 * s, 12.5 * s, t["charge"], True))
        tx += 14.5 * s
    elif state == "full":
        parts.append(glyph(CHECK, tx - 1 * s, cy - 6.5 * s, 12.5 * s, t["full"], False))
        tx += 16 * s

    text = label if label is not None else f"{pct}%"
    color = t["low"] if state == "low" else t["fg"]
    parts.append(f'<text x="{tx:.1f}" y="{cy + fsz * 0.36:.1f}" font-size="{fsz:.1f}" font-weight="500" fill="{color}">{text}</text>')
    tw = text_w(text, fsz)
    width = tx + tw - x

    if bar_color:
        bw = max(22.0, width - 2 * s)
        parts.append(
            f'<rect x="{x:.1f}" y="{cy + 14.5 * s:.1f}" width="{bw:.1f}" height="{2.4 * s:.1f}" '
            f'rx="{1.2 * s:.1f}" fill="{bar_color}" opacity="0.9"/>'
        )

    g = "".join(parts)
    if dim:
        g = f'<g opacity="0.42">{g}</g>'
    return g, width


def row(t, entries, x, cy, s, gap):
    out = []
    for kw in entries:
        g, wd = strip_item(t, x, cy, s=s, **kw)
        out.append(g)
        x += wd + gap
    return "".join(out), x - gap


def app_icon(x, y, size, color):
    src = open(os.path.join(ROOT, "res", "app.svg"), encoding="utf-8").read()
    paths = "".join(re.findall(r"<path[^>]*/>", src, re.S))
    k = size / 512
    return (
        f'<g transform="translate({x} {y}) scale({k:.4f})" fill="{color}" fill-rule="evenodd" '
        f'clip-rule="evenodd">{paths}</g>'
    )


def svg(w, h, body, title):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
        f'role="img" aria-label="{title}" font-family="{FONT}">\n<title>{title}</title>\n{body}\n</svg>\n'
    )


def pill(t, x, y, text, size=18):
    pad_x = 18
    w = text_w(text, size) + pad_x * 2
    h = 38
    el = (
        f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h}" rx="19" '
        f'fill="{t["pill_bg"]}" stroke="{t["pill_edge"]}"/>'
        f'<text x="{x + w / 2:.1f}" y="{y + 25:.1f}" text-anchor="middle" font-size="{size}" '
        f'font-weight="600" fill="{t["fg"]}">{text}</text>'
    )
    return el, w


def banner(t, theme_name):
    W, H = 1440, 500
    s = 1.9
    grad_id = f"hdrGrad_{theme_name}"
    out = [
        f'<defs><linearGradient id="{grad_id}" x1="0" y1="0" x2="1" y2="1">'
        f'<stop offset="0%" stop-color="{t["bg_top"]}"/>'
        f'<stop offset="100%" stop-color="{t["bg"]}"/>'
        f'</linearGradient></defs>',
        f'<rect x="0.5" y="0.5" width="{W - 1}" height="{H - 1}" rx="22" fill="url(#{grad_id})" stroke="{t["edge"]}"/>',
    ]

    # Top badge pill
    badge_text = "WINDOWS 10 / 11 TASKBAR TELEMETRY"
    bw = text_w(badge_text, 15) + 34
    out.append(
        f'<rect x="64" y="42" width="{bw:.1f}" height="32" rx="16" fill="{t["accent_soft"]}" stroke="{t["accent_edge"]}"/>'
        f'<text x="{64 + bw / 2:.1f}" y="63" text-anchor="middle" font-size="15" font-weight="700" '
        f'letter-spacing="0.8" fill="{t["accent"]}">{badge_text}</text>'
    )

    # App icon tile + Title + Subtitle
    out.append(
        f'<rect x="64" y="98" width="96" height="96" rx="22" fill="{t["icon_tile"]}" stroke="{t["icon_tile_edge"]}"/>'
    )
    out.append(app_icon(76, 110, 72, t["accent"]))
    out.append(
        f'<text x="184" y="142" font-size="56" font-weight="700" letter-spacing="-1.2" fill="{t["fg"]}">Taskbar Vitals</text>'
    )
    out.append(
        f'<text x="184" y="182" font-size="23" fill="{t["muted"]}">'
        f'Real-time CPU, GPU, RAM &amp; wattage telemetry + direct wireless peripheral battery monitoring</text>'
    )

    # Three feature pills
    px = 64.0
    for label in ("50+ Wireless Brands", "CPU / GPU / RAM / Wattage", "Native C++17 · Zero Bloat"):
        p_svg, pw = pill(t, px, 224, label, size=17)
        out.append(p_svg)
        px += pw + 14

    # Taskbar preview bar
    bx0, bx1, by, bh = 64, W - 64, 326, 98
    cy = by + bh / 2
    out.append(f'<text x="{bx0}" y="{by - 16}" font-size="16" font-weight="600" fill="{t["muted"]}">LIVE DUAL-STRIP TASKBAR PREVIEW</text>')
    out.append(f'<rect x="{bx0}" y="{by}" width="{bx1 - bx0}" height="{bh}" rx="14" fill="{t["bar"]}" stroke="{t["bar_edge"]}"/>')

    # Left: System Vitals strip
    gap = 22 * s / 2
    readings = [
        dict(kind="cpu", pct=0, label="24% 38W"),
        dict(kind="ram", pct=0, label="47%"),
        dict(kind="gpu", pct=0, label="62% 61° 145W"),
    ]
    g, left_end = row(t, readings, bx0 + 28, cy, s, gap)
    out.append(g)

    # Right: Wireless Batteries + chevron + clock
    csz = 11 * s
    x = bx1 - 26
    out.append(
        f'<text text-anchor="end" font-size="{csz:.1f}" fill="{t["fg"]}">'
        f'<tspan x="{x}" y="{cy - 4:.1f}">14:32</tspan>'
        f'<tspan x="{x}" y="{cy + csz + 2:.1f}" fill="{t["muted"]}">Nov 23</tspan></text>'
    )
    x -= text_w("Nov 23", csz) + 26
    x -= 16 * s
    out.append(icon("chevron", x, cy - 8 * s, 16 * s, t["fg"]))
    x -= 22 * s

    batteries = [
        dict(kind="headset", pct=84, bar_color="#00a8e8"),
        dict(kind="mouse", pct=93, bar_color="#44d62c"),
        dict(kind="keyboard", pct=12, state="low"),
        dict(kind="controller", pct=64, state="charging"),
    ]
    wd = row(t, batteries, 0, cy, s, gap)[1]
    start = x - wd
    assert start > left_end + 36, f"banner taskbar too crowded: {start:.0f} vs {left_end:.0f}"
    out.append(row(t, batteries, start, cy, s, gap)[0])

    return svg(
        W,
        H,
        "\n".join(out),
        "Taskbar Vitals preview: System Vitals (CPU, RAM, GPU) at the left end of the Windows taskbar and wireless peripheral batteries beside the clock",
    )


def states(t):
    W, H = 1280, 330
    s = 1.95
    out = [
        f'<rect x="0.5" y="0.5" width="{W - 1}" height="{H - 1}" rx="20" fill="{t["bg"]}" stroke="{t["edge"]}"/>',
        f'<text x="48" y="42" font-size="15" font-weight="700" letter-spacing="0.8" fill="{t["accent"]}">TASKBAR INDICATOR REFERENCE</text>',
    ]
    cells = [
        (dict(kind="mouse", pct=93, bar_color="#44d62c"), "Active %", "live level + brand bar"),
        (dict(kind="headset", pct=12, state="low"), "Low Alert", "15% or custom alert"),
        (dict(kind="controller", pct=64, state="charging"), "Fast Charging", "on USB cable or dock"),
        (dict(kind="keyboard", pct=100, state="full"), "Fully Charged", "100% still on power"),
        (dict(kind="mouse", pct=71, dim=True), "Standby / Asleep", "last reading retained"),
        (dict(kind="mouse", pct=80, caption="MX"), "Multi-Device Tag", "short ID for pairs"),
    ]
    bx0, bx1, by, bh = 48, W - 48, 66, 98
    cy = by + bh / 2
    out.append(f'<rect x="{bx0}" y="{by}" width="{bx1 - bx0}" height="{bh}" rx="14" fill="{t["bar"]}" stroke="{t["bar_edge"]}"/>')
    cw = (bx1 - bx0) / len(cells)
    for i, (kw, label, note) in enumerate(cells):
        cx0 = bx0 + i * cw
        mid = cx0 + cw / 2
        if i:
            out.append(f'<line x1="{cx0:.1f}" y1="{by + 20}" x2="{cx0:.1f}" y2="{by + bh - 20}" stroke="{t["divider"]}"/>')
        wd = strip_item(t, 0, cy, s=s, **kw)[1]
        out.append(strip_item(t, mid - wd / 2, cy, s=s, **kw)[0])
        out.append(
            f'<text x="{mid:.1f}" y="{by + bh + 52:.1f}" text-anchor="middle" font-size="23" '
            f'font-weight="600" fill="{t["fg"]}">{label}</text>'
        )
        out.append(
            f'<text x="{mid:.1f}" y="{by + bh + 86:.1f}" text-anchor="middle" font-size="19" '
            f'fill="{t["muted"]}">{note}</text>'
        )
    return svg(
        W,
        H,
        "\n".join(out),
        "Taskbar Vitals indicator states: active percentage, low alert, fast charging, fully charged, standby/asleep, and multi-device caption",
    )


if __name__ == "__main__":
    for name, t in THEMES.items():
        for fn, content in (("banner", banner(t, name)), ("states", states(t))):
            path = os.path.join(HERE, f"{fn}-{name}.svg")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(content)
            print("wrote", os.path.relpath(path, ROOT))
