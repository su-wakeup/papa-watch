#!/usr/bin/env python3
"""Derive the boot ceremony's LittleFS frames from the source pack.

Tightly crops each source frame to the coin (drops the outer ring's black
corners), downscales, and writes 26 evenly-sampled frames to data/boot/.
boot_anim then upscales them to fill the panel. Re-run + `pio run -t uploadfs`
after changing the source pack. Source pack: assets/boot/<pack>/frames_png/.
"""
from PIL import Image
import os, sys, glob

SRC  = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bootpack/frames_png"
OUT  = "data/boot"
CROP = 330      # px cropped from the 466 source — coin + inner ring only
SIZE = 330      # stored px (boot_anim scales up to the panel)
N    = 26       # frames kept (the 40-frame source is overkill for a fade-in)

src_frames = sorted(glob.glob(os.path.join(SRC, "*.png")))
os.makedirs(OUT, exist_ok=True)
for f in glob.glob(os.path.join(OUT, "*.png")):
    os.remove(f)
idx = sorted(set(round(i * (len(src_frames) - 1) / (N - 1)) for i in range(N)))
for k, i in enumerate(idx):
    im = Image.open(src_frames[i]).convert("RGB")
    w, h = im.size; cx, cy = w // 2, h // 2
    im = im.crop((cx - CROP // 2, cy - CROP // 2, cx + CROP // 2, cy + CROP // 2))
    im = im.resize((SIZE, SIZE), Image.LANCZOS)
    im.save(os.path.join(OUT, f"b{k:02d}.png"), optimize=True)
print(f"wrote {len(idx)} frames to {OUT}/")
