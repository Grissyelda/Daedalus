#!/usr/bin/env python3
"""Daedalus app icon: an emerald square labyrinth on dark slate.

Writes
  neo/sys/win32/rc/res/daedalus.ico   (16 to 256 px, for Windows)
  content/editor/daedalus_icon.png    (256 px, the window icon elsewhere)

The design is drawn here from scratch, so the repository owns it outright.
Run from the repository root: python3 scripts/content/make_icon.py
"""
import os
from PIL import Image, ImageDraw

SLATE = (20, 24, 29, 255)
EMERALD = (52, 211, 153, 255)
HIGHLIGHT = (110, 231, 183, 255)


def labyrinth(size, rings):
    """A square spiral running inward from the left edge: the Labyrinth, seen from above."""
    big = size * 8                      # draw large, then scale down smoothly
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    radius = big * 0.2
    d.rounded_rectangle([0, 0, big - 1, big - 1], radius=radius, fill=SLATE)

    margin = big * 0.17
    span = big - 2 * margin
    # each turn of the spiral steps in by one wall and one path, each "gap / 2" wide
    gap = span / (2 * rings + 1)
    width = int(gap * 0.5)
    left, top, right, bottom = margin, margin, big - margin, big - margin

    # a square spiral from the bottom left, clockwise and inward: the Labyrinth, seen from above
    pts = [(left, bottom)]
    for _ in range(rings + 1):
        pts += [(left, top), (right, top), (right, bottom)]
        left += gap
        if right - left < gap * 0.6:
            break
        pts.append((left, bottom))
        top += gap
        right -= gap
        bottom -= gap
    for a, b in zip(pts, pts[1:]):
        d.line([a, b], fill=EMERALD, width=width)
    for p in pts:   # square joints
        h = width / 2
        d.rectangle([p[0] - h, p[1] - h, p[0] + h, p[1] + h], fill=EMERALD)
    end = pts[-1]
    # the heart of the maze, where the spiral ends
    r = width * 0.9
    d.rectangle([end[0] - r, end[1] - r, end[0] + r, end[1] + r], fill=HIGHLIGHT)
    return img.resize((size, size), Image.LANCZOS)


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    # fewer rings at small sizes, so the walls stay readable
    images = [labyrinth(s, 1 if s <= 24 else (2 if s <= 64 else 3)) for s in sizes]
    os.makedirs("neo/sys/win32/rc/res", exist_ok=True)
    images[-1].save("neo/sys/win32/rc/res/daedalus.ico", sizes=[(s, s) for s in sizes], append_images=images[:-1])
    os.makedirs("content/editor", exist_ok=True)
    images[-1].save("content/editor/daedalus_icon.png")
    print("wrote neo/sys/win32/rc/res/daedalus.ico and content/editor/daedalus_icon.png")


if __name__ == "__main__":
    main()
