#!/usr/bin/env python3
"""Import the custom 466x466 icon pack -> 128x128 launcher PNGs.

Crops each to its alpha bounding box, scales so the longest side is FIT px,
and centers it on a 128x128 transparent canvas — same on-screen footprint as
the Phosphor placeholders the layout was approved with. Run once when a new
icon pack arrives, then scripts/convert (LVGLImage) to regenerate src/icons.
"""

from PIL import Image
import os, sys

SRC    = sys.argv[1] if len(sys.argv) > 1 else "/tmp/iconpack"
OUT    = "assets/icons"
CANVAS = 128
FIT    = 104          # longest glyph side, leaving ~12px margin

# source filename (in pack) -> our icon name
MAP = {
    "WATCH":     "watch",
    "STOPWATCH": "stopwatch",
    "SCHEDULE":  "schedule",
    "AI_CHAT":   "aichat",
    "PAPA":      "papa",
    "SUNDIAL":   "compass",
    "SETTINGS":  "settings",
}

os.makedirs(OUT, exist_ok=True)
for src_name, out_name in MAP.items():
    src = os.path.join(SRC, src_name + ".png")
    im = Image.open(src).convert("RGBA")
    bbox = im.getbbox()                       # tight box of non-transparent area
    sub = im.crop(bbox)
    w, h = sub.size
    scale = FIT / max(w, h)
    new = (max(1, round(w * scale)), max(1, round(h * scale)))
    sub = sub.resize(new, Image.LANCZOS)
    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    canvas.paste(sub, ((CANVAS - new[0]) // 2, (CANVAS - new[1]) // 2), sub)
    dst = os.path.join(OUT, out_name + ".png")
    canvas.save(dst)
    print(f"  {src_name:10s} {im.size} crop{sub.size} -> {dst}")

print("done.")
