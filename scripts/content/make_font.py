#!/usr/bin/env python3
"""Generate Daedalus engine fonts from a TrueType font.

Writes, under the given content folder:
  newfonts/<Name>/48.dat   glyph metrics in the BFG font format (version 42)
  newfonts/<Name>/48.tga   glyph atlas, white with coverage in alpha
  textures/bigchars.tga    16x16 ASCII grid used by the console (with --console-font)

Usage:
  make_font.py --ttf DejaVuSans.ttf --name DaedalusSans --out content \
               [--console-font DejaVuSansMono.ttf]

Needs Pillow (pip install pillow).
"""

import argparse
import os
import struct

from PIL import Image, ImageDraw, ImageFont

POINT_SIZE = 48
ATLAS_WIDTH = 1024
CELL_ALIGN = 16          # the engine builds 4 mip levels, so glyph cells start on 16-pixel boundaries
PADDING = 2

# printable ASCII and Latin-1 supplement; the engine expects charIndex sorted ascending
CHARSET = list(range(32, 127)) + list(range(160, 256))


def align(v, a):
    return (v + a - 1) // a * a


def build_font(ttf, name, out):
    font = ImageFont.truetype(ttf, POINT_SIZE)
    ascent, descent = font.getmetrics()

    glyphs = []
    for code in CHARSET:
        ch = chr(code)
        left, top, right, bottom = font.getbbox(ch, anchor="ls")   # relative to the baseline
        width = max(0, right - left)
        height = max(0, bottom - top)
        advance = int(round(font.getlength(ch)))
        glyphs.append({
            "code": code, "ch": ch,
            "bbox": (left, top, right, bottom),
            "width": width, "height": height,
            "top": -top,                 # pixels from baseline up to the glyph's top edge
            "left": left,
            "xskip": advance,
        })

    # shelf-pack the glyphs into the atlas
    x = y = 0
    shelf = 0
    for g in glyphs:
        cw = align(g["width"] + PADDING * 2, CELL_ALIGN)
        ch = align(g["height"] + PADDING * 2, CELL_ALIGN)
        if x + cw > ATLAS_WIDTH:
            x = 0
            y += shelf
            shelf = 0
        g["s"] = x + PADDING
        g["t"] = y + PADDING
        x += cw
        shelf = max(shelf, ch)
    atlas_height = 1
    while atlas_height < y + shelf:
        atlas_height *= 2

    image = Image.new("RGBA", (ATLAS_WIDTH, atlas_height), (255, 255, 255, 0))
    coverage = Image.new("L", (ATLAS_WIDTH, atlas_height), 0)
    draw = ImageDraw.Draw(coverage)
    for g in glyphs:
        if g["width"] == 0 or g["height"] == 0:
            continue
        left, top, _, _ = g["bbox"]
        # draw so the glyph's bounding box lands at (s, t)
        draw.text((g["s"] - left, g["t"] - top), g["ch"], font=font, fill=255, anchor="ls")
    image.putalpha(coverage)

    folder = os.path.join(out, "newfonts", name)
    os.makedirs(folder, exist_ok=True)
    image.save(os.path.join(folder, "48.tga"))

    magic = 42 | (ord("i") << 24) | (ord("d") << 16) | (ord("f") << 8)
    with open(os.path.join(folder, "48.dat"), "wb") as f:
        f.write(struct.pack(">I", magic))
        f.write(struct.pack(">hhhh", POINT_SIZE, ascent, -descent, len(glyphs)))
        for g in glyphs:
            # matches idFont::glyphInfo_t: 5 bytes, 1 pad byte, then two uint16 (little endian)
            f.write(struct.pack("<BBbbBxHH",
                                min(g["width"], 255), min(g["height"], 255),
                                max(-128, min(127, g["top"])), max(-128, min(127, g["left"])),
                                min(g["xskip"], 255), g["s"], g["t"]))
        for g in glyphs:
            f.write(struct.pack("<I", g["code"]))

    print(f"{name}: {len(glyphs)} glyphs, atlas {ATLAS_WIDTH}x{atlas_height}, ascent {ascent}, descent {-descent}")


def build_console_font(ttf, out):
    """16x16 grid of 16x16-pixel ASCII cells, as the console's textures/bigchars expects."""
    cell = 16
    size = 256
    font = ImageFont.truetype(ttf, 14)
    coverage = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(coverage)
    for code in range(32, 127):
        cx = (code % 16) * cell
        cy = (code // 16) * cell
        draw.text((cx + cell / 2, cy + cell - 3), chr(code), font=font, fill=255, anchor="ms")
    image = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    image.putalpha(coverage)
    folder = os.path.join(out, "textures")
    os.makedirs(folder, exist_ok=True)
    image.save(os.path.join(folder, "bigchars.tga"))
    print("console font: textures/bigchars.tga")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ttf", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--console-font")
    args = ap.parse_args()

    build_font(args.ttf, args.name, args.out)
    if args.console_font:
        build_console_font(args.console_font, args.out)


if __name__ == "__main__":
    main()
