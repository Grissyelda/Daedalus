#!/usr/bin/env python3
"""Writes small test pictures for the material tests: a brick-wall set named
the way asset stores name them (colour, normal, roughness, and a height map
the editor doesn't use yet) and a lone poster. usage: make_test_pictures.py <folder>"""
import struct, zlib, os, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
def png(w, h, pixel):
    raw = b''.join(b'\x00' + b''.join(bytes(pixel(x, y)) for x in range(w)) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
def write(name, data):
    with open(os.path.join(out, name), 'wb') as f: f.write(data)
brick = lambda x, y: (150, 60, 40, 255) if (y % 8) and ((x + (y // 8) * 4) % 16) else (200, 200, 190, 255)
write('Brick_Wall_2K_Color.png', png(32, 32, brick))
write('Brick_Wall_2K_NormalGL.png', png(32, 32, lambda x, y: (128, 128, 255, 255)))
write('brick_wall_2k_roughness.png', png(16, 16, lambda x, y: (230, 230, 230, 255)))
write('brick_wall_2k_height.png', png(16, 16, lambda x, y: (128, 128, 128, 255)))
write('poster.png', png(8, 8, lambda x, y: (30, 90, 200, 255)))
