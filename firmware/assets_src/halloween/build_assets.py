#!/usr/bin/env python3
"""
Generate the Halloween panda avatar images.

SVG sources are written next to this script, rasterized with headless Chrome and converted to
LVGL .bin files in bin/, which go on the SD card in stackchan/imagens/halloween/ (web page upload).

Needs a Python with `pypng` and `lz4` (for LVGLImage.py) and Google Chrome:
    python3 -m venv /tmp/imgenv && /tmp/imgenv/bin/pip install pypng lz4
    /tmp/imgenv/bin/python assets_src/halloween/build_assets.py      # run from firmware/
"""
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parent.parent
OUT_C = HERE / "bin"  # Upload to the SD card: stackchan/imagens/halloween/
CONVERTER = FIRMWARE / "managed_components/78__xiaozhi-fonts/LVGLImage.py"
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

PATCH = "#141018"
ORANGE = "#FF8A00"
CANDLE = "#FFD34D"

# ---------------------------------------------------------------------------------------------
# Background: everything that never moves. Eyes, mouth, fangs, bats, pumpkins and spider are
# drawn by the firmware on top of it. Eyes sit at (90,104)/(230,104), mouth at (160,154)
# ---------------------------------------------------------------------------------------------
BG = f"""<svg xmlns="http://www.w3.org/2000/svg" width="320" height="240" viewBox="0 0 320 240">
<defs>
<radialGradient id="sky" cx="0.85" cy="0.1" r="1.1"><stop offset="0" stop-color="#4A2A73"/><stop offset="0.55" stop-color="#241238"/><stop offset="1" stop-color="#110818"/></radialGradient>
<radialGradient id="glow" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="{ORANGE}" stop-opacity="0.45"/><stop offset="1" stop-color="{ORANGE}" stop-opacity="0"/></radialGradient>
<radialGradient id="moonglow" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="#FFF1C2" stop-opacity="0.35"/><stop offset="1" stop-color="#FFF1C2" stop-opacity="0"/></radialGradient>
</defs>
<rect width="320" height="240" fill="url(#sky)"/>
<g fill="#FFF6D8"><circle cx="100" cy="9" r="1.2"/><circle cx="226" cy="12" r="1"/><circle cx="306" cy="110" r="1.3"/><circle cx="9" cy="140" r="1"/><circle cx="252" cy="4" r="0.9"/><circle cx="312" cy="168" r="0.9"/><circle cx="18" cy="182" r="1.1"/><circle cx="300" cy="140" r="0.8"/><circle cx="6" cy="112" r="0.8"/></g>
<circle cx="292" cy="26" r="34" fill="url(#moonglow)"/>
<circle cx="292" cy="26" r="18" fill="#FFF1C2"/>
<circle cx="286" cy="22" r="3.5" fill="#EAD9A0"/><circle cx="297" cy="32" r="2.5" fill="#EAD9A0"/><circle cx="294" cy="17" r="1.6" fill="#EAD9A0"/>
<g stroke="#D9CCF0" stroke-opacity="0.55" stroke-width="1" fill="none">
<path d="M0 0 L72 0 M0 0 L66 28 M0 0 L52 52 M0 0 L28 66 M0 0 L0 72"/>
<path d="M22 0 Q20 8 20 9 Q16 15 15 15 Q8 20 8 21 Q1 22 0 22"/>
<path d="M42 0 Q39 15 39 17 Q31 29 30 30 Q17 37 16 39 Q2 42 0 42"/>
<path d="M62 0 Q58 22 57 25 Q46 43 44 45 Q26 55 24 58 Q4 62 0 62"/>
</g>
<circle cx="64" cy="58" r="32" fill="{PATCH}"/>
<circle cx="256" cy="58" r="32" fill="{PATCH}"/>
<ellipse cx="160" cy="132" rx="130" ry="100" fill="#F8F5FC"/>
<ellipse cx="160" cy="206" rx="104" ry="26" fill="#E2D9EE" opacity="0.6"/>
<ellipse cx="90" cy="106" rx="34" ry="42" fill="{PATCH}" transform="rotate(30 90 106)"/>
<ellipse cx="230" cy="106" rx="34" ry="42" fill="{PATCH}" transform="rotate(-30 230 106)"/>
<circle cx="90" cy="104" r="30" fill="url(#glow)"/>
<circle cx="230" cy="104" r="30" fill="url(#glow)"/>
<ellipse cx="60" cy="152" rx="17" ry="9" fill="#9B4DCA" opacity="0.45"/>
<ellipse cx="260" cy="152" rx="17" ry="9" fill="#9B4DCA" opacity="0.45"/>
<path d="M146 118 Q160 112 174 118 Q172 128 160 132 Q148 128 146 118 Z" fill="{PATCH}"/>
<ellipse cx="155" cy="118" rx="3" ry="1.6" fill="#ffffff" opacity="0.5"/>
<ellipse cx="160" cy="44" rx="76" ry="13" fill="#3A1C5C"/>
<path d="M118 42 C128 20 140 6 152 -10 L194 -10 C188 10 196 28 204 42 Z" fill="#4A2470"/>
<path d="M121 33 L201 33 L204 42 L118 42 Z" fill="{ORANGE}"/>
<rect x="154" y="32" width="14" height="11" rx="2" fill="none" stroke="{CANDLE}" stroke-width="2"/>
<path d="M150 -10 C160 0 176 2 194 -10" fill="#5A2E86" opacity="0.6"/>
</svg>"""

# Pointed vampire fang with a dark outline, so it reads on the white fur and inside the mouth
FANG = f"""<svg xmlns="http://www.w3.org/2000/svg" width="10" height="16" viewBox="0 0 10 16">
<path d="M1 1 L9 1 Q8.6 8 5 15 Q1.4 8 1 1 Z" fill="#FFFFFF" stroke="{PATCH}" stroke-width="1.4" stroke-linejoin="round"/>
</svg>"""


def bat(tip_y, low1, low2):
    """Bat facing the viewer. tip_y: wing tip height; low1/low2: scallop points under the wing"""
    wing = (f"M16 9 Q9 {min(tip_y, 9) - 1} 1 {tip_y} Q5 {low1 - 1} 7 {low1} "
            f"Q9 {low2 - 2} 11 {low2} Q13 {low2 - 1} 16 13 Z")
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="36" height="22" viewBox="0 0 36 22">
<g fill="#0B0610" stroke="#6B4A9A" stroke-width="0.8" stroke-linejoin="round">
<path d="{wing}"/>
<path d="{wing}" transform="translate(36 0) scale(-1 1)"/>
<ellipse cx="18" cy="12" rx="4" ry="5"/>
<path d="M15.2 7.5 L15.4 2.5 L17 5.6 L19 5.6 L20.6 2.5 L20.8 7.5 Q18 10 15.2 7.5 Z"/>
</g>
<circle cx="16.8" cy="7" r="0.9" fill="{ORANGE}"/><circle cx="19.2" cy="7" r="0.9" fill="{ORANGE}"/>
</svg>"""


BAT_UP = bat(2, 10, 12)
BAT_DOWN = bat(19, 15, 14)


def pumpkin(w, h, cx, cy, scale, lit, big):
    face_color = CANDLE if lit else "#7A3206"
    glow = (f'<ellipse cx="0" cy="2" rx="20" ry="13" fill="{CANDLE}" opacity="0.18"/>' if lit else "")
    if big:
        face = (f'<path d="M-17 -6 L-9 -14 L-5 -4 Z M5 -4 L9 -14 L17 -6 Z" fill="{face_color}"/>'
                f'<path d="M-18 4 L-12 8 L-8 4 L-3 9 L2 4 L7 9 L12 4 L18 4 Q10 16 0 16 Q-10 16 -18 4 Z" fill="{face_color}"/>')
    else:
        face = (f'<path d="M-15 -8 A5 5 0 1 1 -5 -8 Z M5 -8 A5 5 0 1 1 15 -8 Z" fill="{face_color}"/>'
                f'<path d="M-14 6 Q0 18 14 6 Q0 11 -14 6 Z" fill="{face_color}"/>')
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">
<defs><radialGradient id="pk" cx="0.4" cy="0.35" r="0.7"><stop offset="0" stop-color="#FFA23A"/><stop offset="1" stop-color="#D9560A"/></radialGradient></defs>
<g transform="translate({cx} {cy}) scale({scale})">
<ellipse cx="0" cy="0" rx="31" ry="24" fill="url(#pk)"/>
<path d="M-12 -22 Q-20 0 -12 22 M0 -24 L0 24 M12 -22 Q20 0 12 22" stroke="#C24E08" stroke-width="1.5" fill="none" opacity="0.7"/>
<path d="M-2 -23 Q0 -32 6 -34" stroke="#4E7A2A" stroke-width="4" fill="none" stroke-linecap="round"/>
{glow}{face}
</g>
</svg>"""


SPIDER = f"""<svg xmlns="http://www.w3.org/2000/svg" width="22" height="20" viewBox="0 0 22 20">
<g stroke="#0B0610" stroke-width="1.2" stroke-linecap="round" fill="none">
<path d="M11 12 L3 6 M11 12 L2 13 M11 13 L3 20 M11 12 L19 6 M11 12 L20 13 M11 13 L19 20"/>
</g>
<circle cx="11" cy="13" r="5" fill="#0B0610"/><circle cx="11" cy="7" r="3" fill="#0B0610"/>
<circle cx="9.8" cy="6.5" r="0.8" fill="{ORANGE}"/><circle cx="12.2" cy="6.5" r="0.8" fill="{ORANGE}"/>
</svg>"""

# name -> (svg, width, height, LVGL color format)
IMAGES = {
    "halloween_bg": (BG, 320, 240, "RGB565"),
    "halloween_fang": (FANG, 10, 16, "RGB565A8"),
    "halloween_bat_up": (BAT_UP, 36, 22, "RGB565A8"),
    "halloween_bat_down": (BAT_DOWN, 36, 22, "RGB565A8"),
    "halloween_pumpkin_big_on": (pumpkin(66, 60, 33, 36, 1.0, True, True), 66, 60, "RGB565A8"),
    "halloween_pumpkin_big_off": (pumpkin(66, 60, 33, 36, 1.0, False, True), 66, 60, "RGB565A8"),
    "halloween_pumpkin_small_on": (pumpkin(48, 44, 24, 26, 0.72, True, False), 48, 44, "RGB565A8"),
    "halloween_pumpkin_small_off": (pumpkin(48, 44, 24, 26, 0.72, False, False), 48, 44, "RGB565A8"),
    "halloween_spider": (SPIDER, 22, 20, "RGB565A8"),
}


def main():
    svg_dir = HERE / "svg"
    png_dir = HERE / "png"
    svg_dir.mkdir(exist_ok=True)
    png_dir.mkdir(exist_ok=True)
    OUT_C.mkdir(exist_ok=True)

    for name, (svg, w, h, cf) in IMAGES.items():
        svg_path = svg_dir / f"{name}.svg"
        png_path = png_dir / f"{name}.png"
        svg_path.write_text(svg)
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                        "--force-device-scale-factor=1", "--default-background-color=00000000",
                        f"--window-size={w},{h}", f"--screenshot={png_path}", svg_path.as_uri()],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run([sys.executable, str(CONVERTER), "--ofmt", "BIN", "--cf", cf, "-o", str(OUT_C),
                        str(png_path)], check=True)
        print(f"{name}: {w}x{h} {cf}")


if __name__ == "__main__":
    os.chdir(FIRMWARE)
    main()
