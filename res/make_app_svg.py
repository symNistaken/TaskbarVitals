"""Writes res/app.svg: Taskbar Vitals modern Fluent squircle emblem (battery + vitals pulse).

Run alongside res/make_icon.py to regenerate app.svg, app.ico and app_light.ico:
    py -3 res/make_app_svg.py && py -3 res/make_icon.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

SVG = """<?xml version="1.0" encoding="utf-8"?>
<!-- Taskbar Vitals app icon: Fluent dark-slate squircle with battery outline and glowing vitals pulse. -->
<svg version="1.1" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">
  <defs>
    <linearGradient id="tvTile" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#222E42"/>
      <stop offset="100%" stop-color="#0E141F"/>
    </linearGradient>
    <linearGradient id="tvPulse" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%" stop-color="#38BDF8"/>
      <stop offset="55%" stop-color="#2DD4BF"/>
      <stop offset="100%" stop-color="#34D399"/>
    </linearGradient>
    <linearGradient id="tvFill" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#38BDF8" stop-opacity="0.18"/>
      <stop offset="100%" stop-color="#34D399" stop-opacity="0.06"/>
    </linearGradient>
  </defs>
  <rect x="32" y="32" width="448" height="448" rx="108" fill="url(#tvTile)" stroke="#334155" stroke-width="10"/>
  <rect x="86" y="152" width="316" height="208" rx="42" fill="url(#tvFill)" stroke="#F8FAFC" stroke-width="26"/>
  <path d="M402,216 H422 C434,216 444,226 444,238 V274 C444,286 434,296 422,296 H402 Z" fill="#F8FAFC"/>
  <path d="M126,262 H178 L207,200 L246,316 L284,194 L314,262 H362"
        fill="none" stroke="url(#tvPulse)" stroke-width="26" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
"""


def main():
    out = os.path.join(HERE, "app.svg")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(SVG)
    print("wrote res/app.svg")


if __name__ == "__main__":
    main()
