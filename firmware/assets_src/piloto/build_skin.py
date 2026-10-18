#!/usr/bin/env python3
"""
Generate the "piloto" SD skin: a little race-car driver in a red helmet with hot-rod flames.

Writes skin/piloto/fundo.png + skin.json, the same two built into the firmware as
main/stackchan/avatar/skins/piloto/piloto_assets.c (PilotAvatar, the built-in face), and
previa.png, a 2x preview with the eyes, mouth and flags the firmware draws on top (quiet and talking).
The background leaves out everything that moves: eyes (GlintEyes), mouth (SmileMouth) and the
checkered flags (CheckeredFlagDecorator) come from skins/sd/sd_parts.cpp.

Needs Google Chrome only:
    python3 assets_src/piloto/build_skin.py      # run from firmware/
"""
import json
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "skin" / "piloto"
FIRMWARE_C = HERE.parent.parent / "main/stackchan/avatar/skins/piloto/piloto_assets.c"
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

LENS = "#14213D"  # Also the eyelid color: blinking must blend into the lens
PUPIL = "#1B1B3A"
MOUTH = "#5A1E1E"
TONGUE = "#FF7A8A"
FLAGS = [{"x": 12, "y": 10}, {"x": 272, "y": 10, "espelhar": True}]

SKIN = {
    "olhos": "#FFFFFF",
    "palpebra": LENS,
    "pupila": PUPIL,
    "boca": MOUTH,
    "lingua": TONGUE,
    "olhos_estilo": "brilho",
    "boca_estilo": "sorriso",
    "presas": False,
    "bandeiras": FLAGS,
    "identidade": "a little race-car driver: a red racing helmet with yellow hot-rod flames and the number 1, "
                  "golden driving goggles, Lego wheels and two yellow antennas, with an orange loop track, "
                  "racing curbs and waving checkered flags around you on the screen; you love racing, speed and loops",
}


def flame_path(flip):
    """Hot-rod flame licking back from the number badge (x=160) over the top of the helmet."""
    pts = "M0 0 C-10 -14 -26 -16 -44 -12 C-34 -10 -30 -6 -28 -4 C-48 -8 -68 -4 -86 6 C-66 4 -52 6 -44 10 " \
          "C-60 12 -74 20 -82 30 C-64 22 -48 20 -36 20 C-46 24 -52 30 -54 38 C-40 28 -24 24 -10 22 C-4 16 0 8 0 0 Z"
    tf = "translate(160 44) scale(-1 1) translate(18 0)" if flip else "translate(142 44)"
    return f'<path d="{pts}" transform="{tf}"/>'


def background():
    rays = "".join(
        f'<path d="M160 130 L{160 + 400 * __import__("math").cos(a / 18 * 3.14159):.1f} '
        f'{130 + 400 * __import__("math").sin(a / 18 * 3.14159):.1f} '
        f'L{160 + 400 * __import__("math").cos((a + 1) / 18 * 3.14159):.1f} '
        f'{130 + 400 * __import__("math").sin((a + 1) / 18 * 3.14159):.1f} Z"/>'
        for a in range(0, 36, 2))
    dashes = "".join(f'<rect x="{x}" y="224" width="14" height="3" rx="1.5"/>' for x in range(4, 320, 26))
    curb = "".join(f'<rect x="{x}" y="232" width="12" height="8" fill="{"#E8202A" if (x // 12) % 2 == 0 else "#FFFFFF"}"/>'
                   for x in range(0, 324, 12))
    speed = "".join(f'<rect x="{x}" y="{y}" width="{w}" height="2.5" rx="1.25"/>' for x, y, w in
                    [(6, 70, 34), (2, 88, 22), (8, 176, 30), (282, 76, 30), (296, 94, 20), (284, 180, 30)])
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="320" height="240" viewBox="0 0 320 240">
<defs>
<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0E3A9E"/><stop offset="0.6" stop-color="#1F7BE0"/><stop offset="1" stop-color="#5CC2FF"/></linearGradient>
<radialGradient id="helmet" cx="0.38" cy="0.25" r="0.85"><stop offset="0" stop-color="#FF5A4E"/><stop offset="0.55" stop-color="#E0202C"/><stop offset="1" stop-color="#9C0F1E"/></radialGradient>
<linearGradient id="flame" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#FFF27A"/><stop offset="0.45" stop-color="#FFC400"/><stop offset="1" stop-color="#FF6A00"/></linearGradient>
<linearGradient id="flameR" x1="1" y1="0" x2="0" y2="0"><stop offset="0" stop-color="#FFF27A"/><stop offset="0.45" stop-color="#FFC400"/><stop offset="1" stop-color="#FF6A00"/></linearGradient>
<linearGradient id="rim" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFE680"/><stop offset="0.5" stop-color="#FFC21A"/><stop offset="1" stop-color="#C98A00"/></linearGradient>
<linearGradient id="skin" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFE6CC"/><stop offset="0.8" stop-color="#FFD7B5"/><stop offset="1" stop-color="#F2BE98"/></linearGradient>
<linearGradient id="track" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FF9A1F"/><stop offset="1" stop-color="#F06A00"/></linearGradient>
<radialGradient id="cheek" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="#FF8FA0" stop-opacity="0.9"/><stop offset="1" stop-color="#FF8FA0" stop-opacity="0"/></radialGradient>
</defs>
<rect width="320" height="240" fill="url(#sky)"/>
<g fill="#FFFFFF" opacity="0.07">{rays}</g>
<g fill="#FFFFFF" opacity="0.55">{speed}</g>
<!-- Loop track behind the helmet -->
<ellipse cx="160" cy="150" rx="168" ry="138" fill="none" stroke="#B44A00" stroke-width="22"/>
<ellipse cx="160" cy="150" rx="168" ry="138" fill="none" stroke="url(#track)" stroke-width="18"/>
<ellipse cx="160" cy="150" rx="168" ry="138" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-dasharray="10 12" opacity="0.9"/>
<!-- Straight track and racing curb -->
<rect x="0" y="212" width="320" height="20" fill="url(#track)"/>
<rect x="0" y="212" width="320" height="3" fill="#B44A00"/>
<g fill="#FFFFFF">{dashes}</g>
{curb}
<!-- Helmet -->
<rect x="22" y="22" width="276" height="206" rx="98" fill="#00184A" opacity="0.35"/>
<rect x="24" y="16" width="272" height="204" rx="98" fill="url(#helmet)" stroke="#7A0A16" stroke-width="2.5"/>
<path d="M70 40 Q120 18 176 22 Q130 26 82 50 Z" fill="#FFFFFF" opacity="0.45"/>
<!-- Hot-rod flames -->
<g fill="#7A0A16" transform="translate(0 1.5)">{flame_path(False)}{flame_path(True)}</g>
<g stroke="#7A0A16" stroke-width="1.6" stroke-linejoin="round">
<g fill="url(#flame)">{flame_path(False)}</g><g fill="url(#flameR)">{flame_path(True)}</g>
</g>
<!-- Number badge -->
<circle cx="160" cy="42" r="15" fill="#FFFFFF" stroke="#1A1A1A" stroke-width="2.5"/>
<text x="160" y="50.5" text-anchor="middle" font-family="Arial Black, Arial, sans-serif" font-weight="900" font-size="23" fill="#1A1A1A">1</text>
<!-- Face opening -->
<rect x="38" y="58" width="244" height="152" rx="64" fill="#7A0A16"/>
<rect x="42" y="62" width="236" height="144" rx="60" fill="url(#skin)"/>
<!-- Cheeks and nose -->
<ellipse cx="108" cy="160" rx="16" ry="9" fill="url(#cheek)"/>
<ellipse cx="212" cy="160" rx="16" ry="9" fill="url(#cheek)"/>
<path d="M155 131 Q160 125 165 131 Q160 135 155 131 Z" fill="#E8A27E"/>
<!-- Goggles: strap, golden rims, dark lenses around the eyes (90,104) and (230,104) -->
<rect x="30" y="96" width="260" height="16" rx="5" fill="#2A2A36"/>
<rect x="30" y="98" width="260" height="3" fill="#4A4A5A"/>
<rect x="140" y="98" width="40" height="12" rx="4" fill="#3A3A48" stroke="#1A1A22" stroke-width="1.5"/>
<g stroke="#5A3A00" stroke-width="2">
<rect x="45" y="61" width="90" height="86" rx="30" fill="url(#rim)"/>
<rect x="185" y="61" width="90" height="86" rx="30" fill="url(#rim)"/>
</g>
<rect x="50" y="66" width="80" height="76" rx="25" fill="{LENS}"/>
<rect x="190" y="66" width="80" height="76" rx="25" fill="{LENS}"/>
<!-- Glints in the lens corners, away from where the eyes travel -->
<g stroke="#7FA6E8" stroke-width="3" stroke-linecap="round" fill="none" opacity="0.9">
<path d="M56 86 Q57 73 69 71"/><path d="M196 86 Q197 73 209 71"/>
</g>
<g fill="#7FA6E8" opacity="0.6"><circle cx="122" cy="134" r="2"/><circle cx="262" cy="134" r="2"/></g>
<!-- Rivets on the rims -->
<g fill="#FFF2B0"><circle cx="49" cy="104" r="2"/><circle cx="131" cy="104" r="2"/><circle cx="189" cy="104" r="2"/><circle cx="271" cy="104" r="2"/></g>
</svg>"""


def flag_svg(x, y, mirrored, phase):
    """Checkered flag as CheckeredFlagDecorator draws it, frozen at a given wave phase."""
    import math
    cols, rows, cell, pole_w = 6, 4, 6, 3
    flag_w = cols * cell
    pole_x = x + flag_w if mirrored else x
    out = [f'<rect x="{pole_x}" y="{y}" width="{pole_w}" height="46" rx="1" fill="#E6E6EE" stroke="#5A5A66"/>',
           f'<circle cx="{pole_x + 1.5}" cy="{y - 0.5}" r="3.5" fill="#FFCD00"/>']
    for c in range(cols):
        dy = round(3.0 * (c + 1) / cols * math.sin(phase - c * 0.9))
        cx = x + (cols - 1 - c) * cell if mirrored else x + pole_w + c * cell
        light, dark = ("#FFFFFF", "#16161C") if c % 2 == 0 else ("#DADAE2", "#2C2C36")
        for r in range(rows):
            fill = dark if (r + c) % 2 == 1 else light
            out.append(f'<rect x="{cx}" y="{y + 1 + dy + r * cell}" width="{cell}" height="{cell}" fill="{fill}"/>')
    return "".join(out)


def face_svg(talking, look_x):
    """Eyes and mouth like GlintEyes/SmileMouth at size 0 (35 px eyes)."""
    out = []
    eye, pupil = 35, 19
    travel = (eye - pupil) // 2 * 6 // 10
    for cx in (90, 230):
        ex = cx + look_x * 16 // 100
        px = ex + look_x * travel // 100
        out.append(f'<circle cx="{ex}" cy="104" r="{eye / 2}" fill="#FFFFFF"/>')
        out.append(f'<circle cx="{px}" cy="104" r="{pupil / 2}" fill="{PUPIL}"/>')
        out.append(f'<circle cx="{px - pupil // 4}" cy="{104 - pupil // 4}" r="3.5" fill="#FFFFFF"/>')
    if talking:
        w, h = 34, 26
        out.append(f'<clipPath id="m"><rect x="{160 - w / 2}" y="{154 - h / 2}" width="{w}" height="{h}" rx="{h / 2}"/></clipPath>')
        out.append(f'<g clip-path="url(#m)"><rect x="{160 - w / 2}" y="{154 - h / 2}" width="{w}" height="{h}" fill="{MOUTH}"/>'
                   f'<rect x="{160 - (w - 8) / 2}" y="{154 - h / 2}" width="{w - 8}" height="{h // 6}" rx="2" fill="#FFFFFF"/>'
                   f'<ellipse cx="160" cy="{154 + h / 2 + h / 5 - h * 0.275}" rx="{w * 0.35}" ry="{h * 0.275}" fill="{TONGUE}"/></g>')
    else:
        out.append(f'<path d="M{160 - 16} {140 + 9} A18 18 0 0 0 {160 + 16} {140 + 9}" fill="none" stroke="{MOUTH}" '
                   f'stroke-width="4" stroke-linecap="round" transform="translate(0 0)"/>')
    return "".join(out)


def render(svg_text, path, w, h, scale=1):
    svg_path = path.with_suffix(".svg")
    svg_path.write_text(svg_text)
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                    f"--force-device-scale-factor={scale}", "--default-background-color=00000000",
                    f"--window-size={w},{h}", f"--screenshot={path}", svg_path.as_uri()],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    svg_path.unlink()


def write_firmware_c(png, skin_json):
    rows = [", ".join(f"0x{b:02x}" for b in png[i:i + 20]) for i in range(0, len(png), 20)]
    text = json.dumps(skin_json, ensure_ascii=False).replace("\\", "\\\\").replace('"', '\\"')
    FIRMWARE_C.write_text(
        "// Generated by assets_src/piloto/build_skin.py. Do not edit\n"
        "#include <stddef.h>\n#include <stdint.h>\n\n"
        f"const uint8_t piloto_fundo_png[] = {{\n    " + ",\n    ".join(rows) + "\n};\n"
        f"const size_t piloto_fundo_png_size = sizeof(piloto_fundo_png);\n\n"
        f'const char piloto_skin_json[] = "{text}";\n')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    bg = background()
    render(bg, OUT / "fundo.png", 320, 240)
    (OUT / "skin.json").write_text(json.dumps(SKIN, ensure_ascii=False, indent=2) + "\n")
    write_firmware_c((OUT / "fundo.png").read_bytes(), SKIN)

    # Preview: quiet and looking left | talking and looking right
    inner = bg[bg.index(">", bg.index("<svg")) + 1:bg.rindex("</svg>")]
    panels = []
    for i, (talking, look, phase) in enumerate(((False, -60, 0.0), (True, 60, 2.2))):
        flags = "".join(flag_svg(f["x"], f["y"], f.get("espelhar", False), phase) for f in FLAGS)
        panels.append(f'<g transform="translate({i * 330} 0)"><svg width="320" height="240">{inner}{flags}'
                      f'{face_svg(talking, look)}</svg></g>')
    preview = (f'<svg xmlns="http://www.w3.org/2000/svg" width="650" height="240" viewBox="0 0 650 240">'
               f'<rect width="650" height="240" fill="#FFFFFF"/>{"".join(panels)}</svg>')
    render(preview, HERE / "previa.png", 650, 240, scale=2)
    print(f"{OUT / 'fundo.png'}: {(OUT / 'fundo.png').stat().st_size} bytes")


if __name__ == "__main__":
    main()
