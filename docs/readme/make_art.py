"""Generates the README illustrations in docs/readme/ (light and dark variants).

The logo is read from res/app.svg, so a new icon shows up here after a re-run.
Run from the repo root:  py -X utf8 docs/readme/make_art.py
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

FONT = "'Segoe UI Variable Text','Segoe UI',system-ui,-apple-system,'Helvetica Neue',Arial,sans-serif"

THEMES = {
    "dark": dict(bg="#17181a", edge="#26282b", bar="#202020", bar_edge="#303030",
                 fg="#f3f3f3", muted="#a3a3a3", low="#ff8a80", charge="#4cc2ff",
                 full="#6ccb5f", divider="#333333", caption_bg="#f3f3f3", caption_fg="#202020"),
    "light": dict(bg="#ffffff", edge="#d8dadd", bar="#eeeeee", bar_edge="#d4d4d4",
                  fg="#1b1b1b", muted="#5a5a5a", low="#c42b1c", charge="#005fb8",
                  full="#0f7b0f", divider="#d0d0d0", caption_bg="#1b1b1b", caption_fg="#ffffff"),
}

# 24x24 stroke icons, drawn in one weight to match the taskbar's Fluent glyphs.
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

# Approximate Segoe UI advance widths (em) so text can be laid out without a renderer.
ADV = {**{d: 0.559 for d in "0123456789"}, "%": 0.83, ":": 0.26, ".": 0.26, " ": 0.27, "°": 0.4, "W": 0.9}


def text_w(s, size):
    return sum(ADV.get(c, 0.58) for c in s) * size


def icon(name, x, y, size, color, sw=1.6, extra=""):
    k = size / 24
    return (f'<g transform="translate({x:.1f} {y:.1f}) scale({k:.4f})" fill="none" stroke="{color}" '
            f'stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round"{extra}>{ICONS[name]}</g>')


def glyph(path, x, y, size, color, filled):
    k = size / 24
    paint = f'fill="{color}" stroke="none"' if filled else \
        f'fill="none" stroke="{color}" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"'
    return f'<g transform="translate({x:.1f} {y:.1f}) scale({k:.4f})" {paint}>{path}</g>'


def device(t, x, cy, kind, pct, s, state=None, caption=None, dim=False, label=None):
    """One taskbar entry (icon, optional mark, reading) starting at x. Returns (svg, width)."""
    isz, fsz = 20 * s, 13 * s
    parts = [icon(kind, x, cy - isz / 2, isz, t["fg"])]
    cw = 0
    if caption:
        cw = text_w(caption, 7.5 * s) + 4 * s
        cx, cyy = x + isz - cw / 2, cy + isz / 2 - 9 * s
        parts.append(f'<rect x="{cx:.1f}" y="{cyy:.1f}" width="{cw:.1f}" height="{10.5 * s:.1f}" rx="{2.5 * s:.1f}" '
                     f'fill="{t["caption_bg"]}"/>'
                     f'<text x="{cx + cw / 2:.1f}" y="{cyy + 8.1 * s:.1f}" font-size="{7.5 * s:.1f}" font-weight="700" '
                     f'text-anchor="middle" fill="{t["caption_fg"]}">{caption}</text>')
    tx = x + isz + (5 + (cw / s) / 2 if caption else 5) * s
    if state == "charging":
        parts.append(glyph(BOLT, tx - 1 * s, cy - 6.5 * s, 13 * s, t["charge"], True))
        tx += 15 * s
    elif state == "full":
        parts.append(glyph(CHECK, tx - 1 * s, cy - 6.5 * s, 13 * s, t["full"], False))
        tx += 17 * s
    text = label if label is not None else f"{pct}%"
    color = t["low"] if state == "low" else t["fg"]
    parts.append(f'<text x="{tx:.1f}" y="{cy + fsz * 0.36:.1f}" font-size="{fsz:.1f}" fill="{color}">{text}</text>')
    width = tx + text_w(text, fsz) - x
    g = "".join(parts)
    if dim:
        g = f'<g opacity="0.42">{g}</g>'
    return g, width


def svg(w, h, body, title):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
            f'role="img" aria-label="{title}" font-family="{FONT}">\n<title>{title}</title>\n{body}\n</svg>\n')


def app_icon(x, y, size, color):
    src = open(os.path.join(ROOT, "res", "app.svg"), encoding="utf-8").read()
    paths = "".join(re.findall(r"<path[^>]*/>", src, re.S))
    k = size / 512
    return (f'<g transform="translate({x} {y}) scale({k:.4f})" fill="{color}" fill-rule="evenodd" '
            f'clip-rule="evenodd">{paths}</g>')


def row(t, entries, x, cy, s, gap):
    """Lays entries out left to right from x. Returns (svg, end_x)."""
    out = []
    for kw in entries:
        g, wd = device(t, x, cy, s=s, **kw)
        out.append(g)
        x += wd + gap
    return "".join(out), x - gap


def banner(t):
    # Leave enough room for the system strip at the left and four battery devices by the clock.
    W, H = 1440, 470
    s = 2.0
    out = [f'<rect x="0.5" y="0.5" width="{W - 1}" height="{H - 1}" rx="18" fill="{t["bg"]}" stroke="{t["edge"]}"/>']
    out.append(app_icon(72, 62, 84, t["fg"]))
    out.append(f'<text x="180" y="127" font-size="60" font-weight="600" letter-spacing="-1" '
               f'fill="{t["fg"]}">Taskbar Vitals</text>')
    out.append(f'<text font-size="26" fill="{t["muted"]}"><tspan x="72" y="200">Battery levels of your wireless '
               f'devices, plus CPU, RAM and GPU readings,</tspan><tspan x="72" y="236">right on the Windows '
               f'taskbar.</tspan></text>')

    bx0, bx1, by, bh = 72, W - 72, 290, 48 * s
    cy = by + bh / 2
    out.append(f'<rect x="{bx0}" y="{by}" width="{bx1 - bx0}" height="{bh:.1f}" rx="12" fill="{t["bar"]}" '
               f'stroke="{t["bar_edge"]}"/>')

    # Left end: the CPU / RAM / GPU strip.
    gap = 28 * s / 2
    readings = [dict(kind="cpu", pct=0, label="20%"), dict(kind="ram", pct=0, label="47%"),
                dict(kind="gpu", pct=0, label="1% 59°")]
    g, left_end = row(t, readings, bx0 + 28, cy, s, gap)
    out.append(g)

    # Right end: the batteries, overflow chevron and clock.
    csz = 11 * s
    x = bx1 - 28
    out.append(f'<text text-anchor="end" font-size="{csz:.1f}" fill="{t["fg"]}">'
               f'<tspan x="{x}" y="{cy - 4:.1f}">14:32</tspan>'
               f'<tspan x="{x}" y="{cy + csz + 2:.1f}">23.11.2026</tspan></text>')
    x -= text_w("23.11.2026", csz) + 30
    x -= 16 * s
    out.append(icon("chevron", x, cy - 8 * s, 16 * s, t["fg"]))
    x -= 26 * s
    batteries = [dict(kind="headset", pct=30), dict(kind="mouse", pct=93),
                 dict(kind="keyboard", pct=12, state="low"), dict(kind="controller", pct=64, state="charging")]
    wd = row(t, batteries, 0, cy, s, gap)[1]
    start = x - wd
    assert start > left_end + 40, f"banner taskbar too crowded: {start:.0f} vs {left_end:.0f}"
    out.append(row(t, batteries, start, cy, s, gap)[0])
    return svg(W, H, "\n".join(out), "Taskbar Vitals: CPU, RAM and GPU readings at the left end of the Windows "
                                      "taskbar and battery levels of a headset, mouse, keyboard and controller "
                                      "next to the clock")


def states(t):
    W, H = 1280, 310
    s = 2.0
    out = [f'<rect x="0.5" y="0.5" width="{W - 1}" height="{H - 1}" rx="18" fill="{t["bg"]}" stroke="{t["edge"]}"/>']
    cells = [
        (dict(kind="mouse", pct=93), "Normal", "level in percent"),
        (dict(kind="headset", pct=12, state="low"), "Low battery", "15% or less"),
        (dict(kind="controller", pct=64, state="charging"), "Charging", "blue bolt"),
        (dict(kind="keyboard", pct=100, state="full"), "Charged", "full, still on power"),
        (dict(kind="mouse", pct=71, dim=True), "Asleep", "last value, dimmed"),
        (dict(kind="mouse", pct=80, caption="MX"), "Two of a kind", "caption on the icon"),
    ]
    bx0, bx1, by, bh = 48, W - 48, 60, 48 * s
    cy = by + bh / 2
    out.append(f'<rect x="{bx0}" y="{by}" width="{bx1 - bx0}" height="{bh:.1f}" rx="12" fill="{t["bar"]}" '
               f'stroke="{t["bar_edge"]}"/>')
    cw = (bx1 - bx0) / len(cells)
    for i, (kw, label, note) in enumerate(cells):
        cx0 = bx0 + i * cw
        mid = cx0 + cw / 2
        if i:
            out.append(f'<line x1="{cx0:.1f}" y1="{by + 22}" x2="{cx0:.1f}" y2="{by + bh - 22:.1f}" '
                       f'stroke="{t["divider"]}"/>')
        wd = device(t, 0, cy, s=s, **kw)[1]
        out.append(device(t, mid - wd / 2, cy, s=s, **kw)[0])
        out.append(f'<text x="{mid:.1f}" y="{by + bh + 52:.1f}" text-anchor="middle" font-size="25" '
                   f'font-weight="600" fill="{t["fg"]}">{label}</text>')
        out.append(f'<text x="{mid:.1f}" y="{by + bh + 88:.1f}" text-anchor="middle" font-size="21" '
                   f'fill="{t["muted"]}">{note}</text>')
    return svg(W, H, "\n".join(out), "What the taskbar entries look like: normal, low battery, charging, "
                                      "charged, asleep, and two devices of one kind")


if __name__ == "__main__":
    for name, t in THEMES.items():
        for fn, make in (("banner", banner), ("states", states)):
            path = os.path.join(HERE, f"{fn}-{name}.svg")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(make(t))
            print("wrote", os.path.relpath(path, ROOT))
