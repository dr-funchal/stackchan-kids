#!/usr/bin/env python3
"""
Generate the big toy-style letters for the Papa-Letras game.

Renders one SVG per letter with headless Chrome and converts it to an LVGL .bin in bin/,
which goes on the SD card in stackchan/imagens/letras/ (web page upload). Same tooling as assets_src/halloween/build_assets.py:
    /tmp/imgenv/bin/python assets_src/papa_letras/build_letters.py      # run from firmware/
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parent.parent
OUT_C = HERE / "bin"  # Upload to the SD card: stackchan/imagens/letras/
CONVERTER = FIRMWARE / "managed_components/78__xiaozhi-fonts/LVGLImage.py"
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

# Letters used with 4-6 year olds (no K, Q, W, X, Y). Keep in sync with papa_letras.cpp
LETTERS = "ABCDEFGHIJLMNOPRSTUVZ"
SIZE = 96
# Bright candy colors, cycled per letter
COLORS = ["#FF5A5F", "#FFB400", "#3EC300", "#00A6ED", "#9B5DE5", "#FF8A00", "#F15BB5"]


def letter_svg(letter, color):
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{SIZE}" height="{SIZE}" viewBox="0 0 {SIZE} {SIZE}">
<text x="50%" y="54%" text-anchor="middle" dominant-baseline="middle"
 font-family="Arial Rounded MT Bold, Arial Black, Helvetica, sans-serif" font-weight="900" font-size="88"
 fill="{color}" stroke="#141018" stroke-width="7" paint-order="stroke" stroke-linejoin="round">{letter}</text>
<text x="50%" y="54%" text-anchor="middle" dominant-baseline="middle"
 font-family="Arial Rounded MT Bold, Arial Black, Helvetica, sans-serif" font-weight="900" font-size="88"
 fill="none" stroke="#FFFFFF" stroke-opacity="0.45" stroke-width="2" transform="translate(-2 -2)">{letter}</text>
</svg>"""


def main():
    svg_dir = HERE / "svg"
    png_dir = HERE / "png"
    for d in (svg_dir, png_dir, OUT_C):
        d.mkdir(parents=True, exist_ok=True)

    for i, letter in enumerate(LETTERS):
        name = f"papa_letra_{letter}"
        svg_path = svg_dir / f"{name}.svg"
        png_path = png_dir / f"{name}.png"
        svg_path.write_text(letter_svg(letter, COLORS[i % len(COLORS)]))
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                        "--force-device-scale-factor=1", "--default-background-color=00000000",
                        f"--window-size={SIZE},{SIZE}", f"--screenshot={png_path}", svg_path.as_uri()],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run([sys.executable, str(CONVERTER), "--ofmt", "BIN", "--cf", "RGB565A8", "-o", str(OUT_C),
                        str(png_path)], check=True, stdout=subprocess.DEVNULL)
        print(name)


if __name__ == "__main__":
    main()
