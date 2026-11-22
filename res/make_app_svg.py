"""Writes res/app.svg, the Taskbar Vitals app icon: a battery outline with a pulse line inside.

One flat shape in the simple SVG subset res/make_icon.py understands (M L C Z paths, filled even-odd), so
make_icon.py can turn it into app.ico (white) and app_light.ico (black). The pulse is a stroked polyline,
expanded here into a filled polygon because make_icon.py has no strokes.

    py -3 res/make_app_svg.py && py -3 res/make_icon.py
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))

BODY = (32, 120, 448, 392)  # left, top, right, bottom of the battery body
WALL = 44                   # thickness of the outline
NUB = (448, 216, 490, 296)  # the battery's terminal, on the right
RADIUS = 52                 # corner radius of the body's outer edge
PULSE_WIDTH = 32
PULSE = [(100, 256), (166, 256), (204, 200), (254, 306), (300, 204), (326, 256), (380, 256)]
K = 0.5523  # cubic Bezier circle constant


def fmt(v):
    return f"{v:.1f}".rstrip("0").rstrip(".")


def rounded_rect(l, t, r, b, rad):
    k = rad * K
    pts = [
        f"M{fmt(l + rad)},{fmt(t)}",
        f"H{fmt(r - rad)}",
        f"C{fmt(r - rad + k)},{fmt(t)} {fmt(r)},{fmt(t + rad - k)} {fmt(r)},{fmt(t + rad)}",
        f"V{fmt(b - rad)}",
        f"C{fmt(r)},{fmt(b - rad + k)} {fmt(r - rad + k)},{fmt(b)} {fmt(r - rad)},{fmt(b)}",
        f"H{fmt(l + rad)}",
        f"C{fmt(l + rad - k)},{fmt(b)} {fmt(l)},{fmt(b - rad + k)} {fmt(l)},{fmt(b - rad)}",
        f"V{fmt(t + rad)}",
        f"C{fmt(l)},{fmt(t + rad - k)} {fmt(l + rad - k)},{fmt(t)} {fmt(l + rad)},{fmt(t)}",
        "Z",
    ]
    return " ".join(pts)


def terminal(l, t, r, b, rad):
    """The battery's terminal: flat on the left (it touches the body's edge, never overlaps it), rounded on the right."""
    k = rad * K
    return " ".join([
        f"M{fmt(l)},{fmt(t)}",
        f"H{fmt(r - rad)}",
        f"C{fmt(r - rad + k)},{fmt(t)} {fmt(r)},{fmt(t + rad - k)} {fmt(r)},{fmt(t + rad)}",
        f"V{fmt(b - rad)}",
        f"C{fmt(r)},{fmt(b - rad + k)} {fmt(r - rad + k)},{fmt(b)} {fmt(r - rad)},{fmt(b)}",
        f"H{fmt(l)}",
        "Z",
    ])


def stroke(points, width, miter_limit=1.3):
    """Outline polygon of a polyline: butt ends, mitered joins that fall back to a bevel on a sharp outer turn."""
    half = width / 2
    dirs = []
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        n = math.hypot(x1 - x0, y1 - y0)
        dirs.append(((x1 - x0) / n, (y1 - y0) / n))
    normals = [(-dy, dx) for dx, dy in dirs]  # to the left of the direction
    sides = {}
    for side in (1, -1):
        out = [(points[0][0] + side * normals[0][0] * half, points[0][1] + side * normals[0][1] * half)]
        for i in range(1, len(points) - 1):
            n0, n1 = normals[i - 1], normals[i]
            cross = dirs[i - 1][0] * dirs[i][1] - dirs[i - 1][1] * dirs[i][0]
            outer = side * cross < 0
            dot = n0[0] * n1[0] + n0[1] * n1[1]
            mx, my = (n0[0] + n1[0]) / (1 + dot), (n0[1] + n1[1]) / (1 + dot)
            px, py = points[i]
            if outer and math.hypot(mx, my) > miter_limit:
                out.append((px + side * n0[0] * half, py + side * n0[1] * half))
                out.append((px + side * n1[0] * half, py + side * n1[1] * half))
            else:
                out.append((px + side * mx * half, py + side * my * half))
        out.append((points[-1][0] + side * normals[-1][0] * half, points[-1][1] + side * normals[-1][1] * half))
        sides[side] = out
    return sides[1] + list(reversed(sides[-1]))


def polygon(pts):
    return "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in pts) + " Z"


def main():
    l, t, r, b = BODY
    paths = [
        rounded_rect(l, t, r, b, RADIUS),
        rounded_rect(l + WALL, t + WALL, r - WALL, b - WALL, max(RADIUS - WALL, 8)),
        terminal(NUB[0], NUB[1], NUB[2], NUB[3], 20),
        polygon(stroke(PULSE, PULSE_WIDTH)),
    ]
    svg = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<!-- Taskbar Vitals app icon: a battery with a pulse line. Made by res/make_app_svg.py; "
        "res/make_icon.py turns it into app.ico. -->\n"
        '<svg version="1.1" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">\n'
        '<g fill-rule="evenodd" clip-rule="evenodd">\n'
        + "".join(f'\t<path d="{p}"/>\n' for p in paths)
        + "</g>\n</svg>\n"
    )
    with open(os.path.join(HERE, "app.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(svg)
    print("wrote res/app.svg")


if __name__ == "__main__":
    main()
