#!/usr/bin/env python3
"""
Port a LVGL 8 image descriptor (.c file) to LVGL 9 ARGB8888.

LVGL 8's `LV_IMG_CF_TRUE_COLOR_ALPHA` stored 3 bytes per pixel: color_lo,
color_hi (RGB565 little-endian under LV_COLOR_16_SWAP=1), then 1 byte alpha.

LVGL 9's `LV_COLOR_FORMAT_ARGB8888` stores 4 bytes per pixel in memory as
B, G, R, A (little-endian). It's the most robust format under image
rotation/transformation, so we pay the ~33% size penalty in exchange for
correct rendering on rotation.

Usage:
    python3 scripts/port_lvgl8_image_to_9.py <input.c> [output.c]
"""
import re
import sys
from pathlib import Path


def parse_hex_bytes(text: str) -> bytes:
    return bytes(int(m.group(1), 16) for m in re.finditer(r"0x([0-9A-Fa-f]{1,2})", text))


def find_data_block(text: str):
    return re.search(
        r"(const\s+LV_ATTRIBUTE_MEM_ALIGN\s+uint8_t\s+\w+_data\s*\[\s*\]\s*=\s*\{)"
        r"(.*?)"
        r"(\};)",
        text, re.DOTALL,
    )


def find_descriptor(text: str):
    return re.search(
        r"const\s+lv_img_dsc_t\s+(\w+)\s*=\s*\{[^}]*?\.data\s*=\s*(\w+)\s*\};",
        text, re.DOTALL,
    )


def rgb565_to_argb8888(raw_interleaved: bytes) -> bytes:
    """Convert (color_lo, color_hi, alpha) per pixel to ARGB8888 (B, G, R, A)."""
    out = bytearray()
    for i in range(0, len(raw_interleaved), 3):
        lo, hi, alpha = raw_interleaved[i], raw_interleaved[i + 1], raw_interleaved[i + 2]
        rgb565 = lo | (hi << 8)
        r5 = (rgb565 >> 11) & 0x1F
        g6 = (rgb565 >> 5) & 0x3F
        b5 = rgb565 & 0x1F
        # expand 5/6/5 to 8/8/8 via bit-replication
        r8 = (r5 << 3) | (r5 >> 2)
        g8 = (g6 << 2) | (g6 >> 4)
        b8 = (b5 << 3) | (b5 >> 2)
        out += bytes((b8, g8, r8, alpha))   # LVGL 9 ARGB8888 in-memory layout
    return bytes(out)


def emit_bytes(data: bytes, indent="    ", per_line=16) -> str:
    return "\n".join(
        indent + ", ".join(f"0x{b:02X}" for b in data[i:i + per_line]) + ","
        for i in range(0, len(data), per_line)
    )


def port(text: str) -> str:
    m_w = re.search(r"\.header\.w\s*=\s*(\d+)", text)
    m_h = re.search(r"\.header\.h\s*=\s*(\d+)", text)
    w, h = int(m_w.group(1)), int(m_h.group(1))

    data_m = find_data_block(text)
    if not data_m:
        raise SystemExit("could not find *_data array")
    prefix, body, tail = data_m.group(1), data_m.group(2), data_m.group(3)
    raw = parse_hex_bytes(body)
    if len(raw) != w * h * 3:
        sys.stderr.write(f"warning: raw {len(raw)} != expected {w*h*3}\n")

    argb = rgb565_to_argb8888(raw)
    new_body = "\n" + emit_bytes(argb) + "\n"

    out = text.replace(prefix + body + tail, prefix + new_body + tail)

    desc_m = find_descriptor(out)
    if not desc_m:
        raise SystemExit("could not find lv_img_dsc_t block")
    sym, data_sym = desc_m.group(1), desc_m.group(2)
    new_dsc = f"""const lv_image_dsc_t {sym} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0,
        .w = {w},
        .h = {h},
        .stride = {w * 4},
        .reserved_2 = 0,
    }},
    .data_size = sizeof({data_sym}),
    .data = {data_sym},
    .reserved = NULL,
}};"""
    out = find_descriptor(out).re.sub(new_dsc, out)
    out = re.sub(r'#include\s+"ui\.h"\s*\n', '#include <lvgl.h>\n', out)
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2]) if len(sys.argv) >= 3 else src.with_suffix(".v9.c")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(port(src.read_text()))
    print(f"{src.name}  →  {dst}")
