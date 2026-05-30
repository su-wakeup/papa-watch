#!/usr/bin/env python3
"""
Generate 3 watch-face background preview PNGs from a source photo, so the
user can pick a treatment before we commit to a ~870KB LVGL C array.

Treatments share the same pipeline (square crop → resize 466 → blur → tone →
brightness → vignette) but differ in tone and final brightness.

Usage:
    python3 gen_face_bg_previews.py <input.jpg> [out_dir]
"""
import sys
from pathlib import Path
from PIL import Image, ImageFilter, ImageOps, ImageChops, ImageDraw, ImageEnhance

SIZE = 466

def square_crop(im: Image.Image) -> Image.Image:
    w, h = im.size
    s = min(w, h)
    left = (w - s) // 2
    top  = (h - s) // 2
    return im.crop((left, top, left + s, top + s))

def radial_vignette(im: Image.Image, edge_mul: float = 0.25) -> Image.Image:
    """Multiply pixels by a radial mask: 1.0 at center → edge_mul at corner."""
    w, h = im.size
    mask = Image.new("L", (w, h), 0)
    px = mask.load()
    cx, cy = w / 2, h / 2
    max_r = (cx ** 2 + cy ** 2) ** 0.5
    for y in range(h):
        for x in range(w):
            r = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / max_r
            # smoothstep from 1.0 at r=0 to edge_mul at r=1
            t = min(1.0, r)
            val = 1.0 * (1 - t) + edge_mul * t
            px[x, y] = int(val * 255)
    # apply: out = src * (mask/255)
    bands = im.split()
    out_bands = [ImageChops.multiply(b, mask) for b in bands[:3]]
    return Image.merge("RGB", out_bands)

def tint_warm(im: Image.Image) -> Image.Image:
    """Push toward warm amber."""
    r, g, b = im.split()
    r = r.point(lambda v: min(255, int(v * 1.10 + 12)))
    g = g.point(lambda v: int(v * 0.95))
    b = b.point(lambda v: int(v * 0.70))
    return Image.merge("RGB", (r, g, b))

def tint_blue(im: Image.Image) -> Image.Image:
    """Push toward cool moonlit blue, mostly grayscaled."""
    gray = ImageOps.grayscale(im).convert("RGB")
    r, g, b = gray.split()
    r = r.point(lambda v: int(v * 0.75))
    g = g.point(lambda v: int(v * 0.90))
    b = b.point(lambda v: min(255, int(v * 1.25 + 10)))
    return Image.merge("RGB", (r, g, b))

def tint_bw(im: Image.Image) -> Image.Image:
    """Slightly warm-toned black and white."""
    gray = ImageOps.grayscale(im).convert("RGB")
    r, g, b = gray.split()
    r = r.point(lambda v: min(255, int(v * 1.05 + 6)))
    g = g.point(lambda v: int(v * 1.00 + 2))
    b = b.point(lambda v: int(v * 0.92))
    return Image.merge("RGB", (r, g, b))

def darken(im: Image.Image, factor: float) -> Image.Image:
    r, g, b = im.split()
    r = r.point(lambda v: int(v * factor))
    g = g.point(lambda v: int(v * factor))
    b = b.point(lambda v: int(v * factor))
    return Image.merge("RGB", (r, g, b))

def round_mask_preview(im: Image.Image) -> Image.Image:
    """For preview only: clip corners to black so user sees what shows on round screen."""
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, im.size[0]-1, im.size[1]-1), fill=255)
    bg = Image.new("RGB", im.size, (0, 0, 0))
    bg.paste(im, mask=mask)
    return bg

def soft_vignette(im: Image.Image, edge_mul: float = 0.55, falloff_pow: float = 1.6) -> Image.Image:
    """Smooth radial falloff: center 1.0 → edge edge_mul, no hard ring.
       falloff_pow > 1 keeps the center flat longer before darkening.
       max_r uses the VISIBLE disc radius (w/2), not corner distance — so the
       vignette reaches edge_mul at the screen edge, not 70% of the way there."""
    w, h = im.size
    mask = Image.new("L", (w, h), 0)
    px = mask.load()
    cx, cy = w / 2, h / 2
    max_r = w / 2
    for y in range(h):
        for x in range(w):
            r = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / max_r
            t = min(1.0, r) ** falloff_pow
            val = 1.0 * (1 - t) + edge_mul * t
            px[x, y] = int(val * 255)
    bands = im.split()
    out_bands = [ImageChops.multiply(b, mask) for b in bands[:3]]
    return Image.merge("RGB", out_bands)

def style_sepia(im: Image.Image) -> Image.Image:
    """Warm sepia tone + overall darken + soft vignette so hands & text pop."""
    gray = ImageOps.grayscale(im)
    gray = ImageEnhance.Contrast(gray).enhance(1.10)
    r = gray.point(lambda v: min(255, int(v * 1.08 + 18)))
    g = gray.point(lambda v: int(v * 0.92 + 6))
    b = gray.point(lambda v: int(v * 0.62))
    out = Image.merge("RGB", (r, g, b))
    out = darken(out, 0.55)                          # bring whole image down
    out = soft_vignette(out, edge_mul=0.15, falloff_pow=1.4)
    return out

def style_noir(im: Image.Image) -> Image.Image:
    """High-contrast film noir B&W — faces pop, no vignette."""
    gray = ImageOps.grayscale(im)
    # S-curve: dark stay dark, bright stay bright, midtones get pushed apart
    lut = []
    for v in range(256):
        if v < 80:
            out = int(v * 0.55)
        elif v > 180:
            out = min(255, int(180 + (v - 180) * 1.4))
        else:
            t = (v - 80) / 100.0
            out = int(44 + t * (180 - 44) * 1.2)
        lut.append(max(0, min(255, out)))
    gray = gray.point(lut)
    rgb = gray.convert("RGB")
    return rgb

def style_riso(im: Image.Image) -> Image.Image:
    """Risograph-style duotone: cream highlights + dark navy shadows."""
    gray = ImageOps.grayscale(im)
    # Posterize to ~5 tonal bands for that printed-on-cheap-paper look
    gray = ImageOps.posterize(gray, 4)
    # Two-color gradient: shadow #1a2940 (deep navy), highlight #f0e6cc (cream)
    sh_r, sh_g, sh_b = 0x1A, 0x29, 0x40
    hi_r, hi_g, hi_b = 0xF0, 0xE6, 0xCC
    def lerp(a, b, t): return int(a + (b - a) * t)
    r = gray.point(lambda v: lerp(sh_r, hi_r, v / 255.0))
    g = gray.point(lambda v: lerp(sh_g, hi_g, v / 255.0))
    b = gray.point(lambda v: lerp(sh_b, hi_b, v / 255.0))
    return Image.merge("RGB", (r, g, b))

def style_painted(im: Image.Image) -> Image.Image:
    """Painterly: smooth → posterize → boost saturation slightly."""
    p = im.filter(ImageFilter.SMOOTH_MORE)
    p = p.filter(ImageFilter.SMOOTH_MORE)
    p = ImageOps.posterize(p, 5)
    p = ImageEnhance.Color(p).enhance(0.85)
    p = ImageEnhance.Contrast(p).enhance(1.05)
    return p

def make(im_base: Image.Image, style: str) -> Image.Image:
    im = im_base.copy()
    if   style == "sepia":   return style_sepia(im)
    elif style == "noir":    return style_noir(im)
    elif style == "riso":    return style_riso(im)
    elif style == "painted": return style_painted(im)
    raise ValueError(style)

def emit_lvgl_c(im: Image.Image, sym: str, out_path: Path):
    """Emit ARGB8888 LVGL 9 image descriptor."""
    w, h = im.size
    data = bytearray()
    px = im.load()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            data += bytes((b, g, r, 0xFF))  # ARGB8888 in-memory order: B, G, R, A
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    body = "\n".join(lines)
    out = f"""#include <lvgl.h>

const LV_ATTRIBUTE_MEM_ALIGN uint8_t {sym}_data[] = {{
{body}
}};

const lv_image_dsc_t {sym} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0,
        .w = {w},
        .h = {h},
        .stride = {w * 4},
        .reserved_2 = 0,
    }},
    .data_size = sizeof({sym}_data),
    .data = {sym}_data,
    .reserved = NULL,
}};
"""
    out_path.write_text(out)

def main():
    args = sys.argv[1:]
    emit_c_style = None
    if "--emit-c" in args:
        i = args.index("--emit-c")
        emit_c_style = args[i + 1]
        args = args[:i] + args[i + 2:]
    if not args:
        print(__doc__); sys.exit(1)
    src = Path(args[0])
    out_dir = Path(args[1]) if len(args) >= 2 else Path.home() / "Downloads"
    out_dir.mkdir(parents=True, exist_ok=True)

    im = Image.open(src).convert("RGB")
    im = square_crop(im)
    im = im.resize((SIZE, SIZE), Image.LANCZOS)
    # no blur, no vignette this round — user wants the photo to read clearly

    if emit_c_style:
        styled = make(im, emit_c_style)
        sym = f"papa_stanley_{emit_c_style}"
        out_path = out_dir / f"{sym}.c"
        emit_lvgl_c(styled, sym, out_path)
        print(f"wrote {out_path} ({out_path.stat().st_size // 1024} KB)")
        return

    for style in ("sepia", "noir", "riso", "painted"):
        out = make(im, style)
        out = round_mask_preview(out)
        path = out_dir / f"papa-stanley-preview-{style}.png"
        out.save(path, "PNG")
        print(f"wrote {path}")

if __name__ == "__main__":
    main()
