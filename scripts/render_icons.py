#!/usr/bin/env python3
"""Render the launcher's Phosphor glyphs to 128x128 white-on-transparent PNGs.

Interim placeholders that match what's on the watch today, so the image-zoom
carousel can be built + proven before the custom icon set lands. The user's
real PNGs (same names, same 128x128 RGBA spec — see assets/icons/README.md)
just overwrite these.
"""

from PIL import Image, ImageFont, ImageDraw
import os

TTF   = "assets/fonts/phosphor/Phosphor.ttf"
OUT   = "assets/icons/mono"   # the "simple" skin; colour pack lives in assets/icons
CANVAS = 224          # match the colour set so both skins share the layout
FIT    = 188          # a touch smaller than the colour subjects — glyphs are bold

# name -> Phosphor regular codepoint (mirrors src/app_registry.cpp icon_utf8)
ICONS = {
    "watch":     0xE4E6,
    "stopwatch": 0xE492,
    "schedule":  0xE10A,
    "aichat":    0xE762,
    "papa":      0xE16C,
    "compass":   0xE1C8,
    "settings":  0xE272,
}

os.makedirs(OUT, exist_ok=True)
# Render big, then normalize each glyph to FIT — glyph metrics vary per icon.
font = ImageFont.truetype(TTF, 240)

for name, cp in ICONS.items():
    ch = chr(cp)
    # Measure the glyph's tight bounding box at the big render size.
    l, t, r, b = font.getbbox(ch)
    gw, gh = r - l, b - t
    big = Image.new("RGBA", (gw, gh), (0, 0, 0, 0))
    ImageDraw.Draw(big).text((-l, -t), ch, font=font, fill=(255, 255, 255, 255))

    # Scale the tight glyph to fit FIT x FIT, preserving aspect, center on canvas.
    scale = FIT / max(gw, gh)
    new = (max(1, round(gw * scale)), max(1, round(gh * scale)))
    glyph = big.resize(new, Image.LANCZOS)

    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    canvas.paste(glyph, ((CANVAS - new[0]) // 2, (CANVAS - new[1]) // 2), glyph)
    path = os.path.join(OUT, name + ".png")
    canvas.save(path)
    print(f"  {name:10s} U+{cp:04X}  glyph {gw}x{gh} -> {new[0]}x{new[1]}  {path}")

print("done.")
