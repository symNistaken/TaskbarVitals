"""Renders Windows 11 Fluent multi-size icons (res/app.ico and res/app_light.ico) at 8x supersampling.

Usage:
    py -3 res/make_icon.py
Requires Python 3 and Pillow.
"""
import io
import math
import os
import struct
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
SUPER = 8
SCALE = 512.0


def lerp(c0, c1, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(c0[i] + (c1[i] - c0[i]) * t + 0.5) for i in range(len(c0)))


def rounded_mask(px_size, x0, y0, x1, y1, r):
    mask = Image.new("L", (px_size, px_size), 0)
    d = ImageDraw.Draw(mask)
    d.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=255)
    return mask


def vertical_gradient(px_size, c_top, c_bot, y_start, y_end):
    strip = Image.new("RGBA", (1, px_size))
    px = strip.load()
    span = max(1.0, float(y_end - y_start))
    for y in range(px_size):
        t = (y - y_start) / span
        px[0, y] = lerp(c_top, c_bot, t) + (255,)
    return strip.resize((px_size, px_size), Image.NEAREST)


def horizontal_gradient(px_size, c_left, c_mid, c_right, x_start, x_end):
    strip = Image.new("RGBA", (px_size, 1))
    px = strip.load()
    span = max(1.0, float(x_end - x_start))
    for x in range(px_size):
        t = max(0.0, min(1.0, (x - x_start) / span))
        col = lerp(c_left, c_mid, t / 0.55) if t < 0.55 else lerp(c_mid, c_right, (t - 0.55) / 0.45)
        px[x, 0] = col + (255,)
    return strip.resize((px_size, px_size), Image.NEAREST)


def draw_polyline_mask(px_size, pts, width):
    mask = Image.new("L", (px_size, px_size), 0)
    d = ImageDraw.Draw(mask)
    r = width / 2.0
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        d.line([(x0, y0), (x1, y1)], fill=255, width=max(1, int(round(width))))
    for (x, y) in pts:
        d.ellipse([x - r, y - r, x + r, y + r], fill=255)
    return mask


def render_icon(size, light_mode=False):
    big = size * SUPER
    k = big / SCALE
    s = lambda v: v * k

    # Colors: rich Fluent slate tile for default/dark, frosted light-card tile for light theme dialogs.
    if not light_mode:
        tile_top, tile_bot = (34, 46, 66), (14, 20, 31)
        tile_border = (54, 72, 98, 255)
        bat_stroke = (248, 250, 252, 255)
        bat_fill_top, bat_fill_bot = (56, 189, 248, 38), (52, 211, 153, 18)
        p0, p1, p2 = (56, 189, 248), (45, 212, 191), (52, 211, 153)
        glow_alpha = 72
    else:
        tile_top, tile_bot = (255, 255, 255), (226, 232, 240)
        tile_border = (148, 163, 184, 255)
        bat_stroke = (15, 23, 42, 255)
        bat_fill_top, bat_fill_bot = (2, 132, 199, 26), (5, 150, 105, 16)
        p0, p1, p2 = (2, 132, 199), (13, 148, 136), (5, 150, 105)
        glow_alpha = 40

    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))

    # 1. Squircle tile base + subtle border rim
    tile_m = rounded_mask(big, s(32), s(32), s(480), s(480), s(108))
    tile_grad = vertical_gradient(big, tile_top, tile_bot, s(32), s(480))
    tile_grad.putalpha(tile_m)
    img.alpha_composite(tile_grad)

    # Subtle border
    border_img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    bd = ImageDraw.Draw(border_img)
    bd.rounded_rectangle([s(32), s(32), s(480), s(480)], radius=s(108), outline=tile_border, width=max(1, int(round(s(12)))))
    img.alpha_composite(border_img)

    # 2. Inner translucent battery chamber fill
    in_m = rounded_mask(big, s(96), s(160), s(392), s(352), s(34))
    fill_layer = vertical_gradient(big, bat_fill_top[:3], bat_fill_bot[:3], s(160), s(352))
    alpha_scale = bat_fill_top[3] / 255.0
    in_scaled = in_m.point(lambda a: int(a * alpha_scale))
    fill_layer.putalpha(in_scaled)
    img.alpha_composite(fill_layer)

    # 3. Battery frame stroke & terminal nub (thicker stroke at tiny icon sizes for crispness)
    stroke_w = s(34 if size <= 20 else 28)
    bat_img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    bdr = ImageDraw.Draw(bat_img)
    bdr.rounded_rectangle([s(84), s(148), s(402), s(364)], radius=s(44), outline=bat_stroke, width=max(2, int(round(stroke_w))))
    bdr.rounded_rectangle([s(400), s(212), s(446), s(300)], radius=s(18), fill=bat_stroke)
    # Square off the left join of the nub cleanly
    bdr.rectangle([s(396), s(212), s(416), s(300)], fill=bat_stroke)
    img.alpha_composite(bat_img)

    # 4. Luminous vitals pulse wave across the battery chamber
    pts = [(s(x), s(y)) for (x, y) in [(122, 260), (174, 260), (206, 198), (246, 316), (284, 192), (314, 260), (366, 260)]]
    pulse_w = s(34 if size <= 20 else 26)
    pulse_grad = horizontal_gradient(big, p0, p1, p2, s(122), s(366))

    # Soft halo under pulse
    halo_m = draw_polyline_mask(big, pts, pulse_w * 1.9).point(lambda a: int(a * (glow_alpha / 255.0)))
    halo_layer = pulse_grad.copy()
    halo_layer.putalpha(halo_m)
    img.alpha_composite(halo_layer)

    # Crisp main pulse line
    pulse_m = draw_polyline_mask(big, pts, pulse_w)
    pulse_grad.putalpha(pulse_m)
    img.alpha_composite(pulse_grad)

    return img.resize((size, size), Image.LANCZOS)


def write_ico(filename, light_mode):
    pngs = []
    for s in SIZES:
        buf = io.BytesIO()
        render_icon(s, light_mode=light_mode).save(buf, "PNG")
        pngs.append(buf.getvalue())
    out = io.BytesIO()
    out.write(struct.pack("<HHH", 0, 1, len(SIZES)))
    offset = 6 + 16 * len(SIZES)
    for s, p in zip(SIZES, pngs):
        d = 0 if s >= 256 else s
        out.write(struct.pack("<BBBBHHII", d, d, 0, 0, 1, 32, len(p), offset))
        offset += len(p)
    for p in pngs:
        out.write(p)
    with open(os.path.join(HERE, filename), "wb") as f:
        f.write(out.getvalue())
    print("wrote", filename + ":", ", ".join(map(str, SIZES)))


def main():
    write_ico("app.ico", light_mode=False)
    write_ico("app_light.ico", light_mode=True)


if __name__ == "__main__":
    main()
