#!/usr/bin/env python3
"""Builds the images for the website in website/img.

    tools/genwebsite.py <directory with the PPM files from build/webshots>

(Run it through "make website", which renders the screenshots first.)

Writes:
    player.png, colors.png, details.png   screenshots, converted from PPM
    background.svg                        the wave pattern behind the page
    social.png                            1200x630 preview for shared links
    logo.svg, logo-256.png, favicon.png   copies of the application icon

Needs only the Python standard library.
"""
import math
import os
import re
import shutil
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "website", "img")

BACKDROP = (0x12, 0x15, 0x1F)
ACCENT = (0x6C, 0xC4, 0xFF)
MUTED = (0x8A, 0x94, 0xAC)
TAGLINE = "Inspired by the past. Built for the future."


def read_ppm(path):
    data = open(path, "rb").read()
    header, pixels = data.split(b"255\n", 1)
    width, height = map(int, header.split()[1:3])
    return width, height, bytearray(pixels)


def read_png_rgba(path):
    """Reads one of our own 8-bit RGBA icon files (no filtering, as written
    by tools/genlogo.py)."""
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
        elif tag == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = width * 4 + 1
    return width, height, [raw[y * stride + 1:(y + 1) * stride] for y in range(height)]


def write_png(path, width, height, pixels):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))
    rows = b"".join(b"\0" + bytes(pixels[y * width * 3:(y + 1) * width * 3]) for y in range(height))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" +
                           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


class Font:
    """Just enough of a BDF reader to set a line of text."""

    def __init__(self, path):
        text = open(path, encoding="latin-1").read()
        self.width, self.height = map(int, re.search(r"^FONTBOUNDINGBOX (\d+) (\d+)", text, re.M).groups())
        self.glyphs = {}
        for char in text.split("STARTCHAR")[1:]:
            code = int(re.search(r"^ENCODING (-?\d+)", char, re.M).group(1))
            if 32 <= code < 127:
                rows = char.split("BITMAP\n")[1].split("ENDCHAR")[0].split()
                self.glyphs[code] = [int(row, 16) << 16 >> (len(row) * 4) for row in rows]

    def draw(self, pixels, canvas_width, x, y, text, colour, k):
        """Each font pixel becomes a k*k block."""
        for i, ch in enumerate(text):
            for row, bits in enumerate(self.glyphs.get(ord(ch), [])):
                for col in range(self.width):
                    if bits & (0x8000 >> col):
                        for dy in range(k):
                            start = ((y + row * k + dy) * canvas_width + x + (i * self.width + col) * k) * 3
                            pixels[start:start + 3 * k] = bytes(colour) * k


def background_svg():
    """A few slow sine waves in the accent colour, fading towards the bottom."""
    width, height = 1600, 900
    waves = []
    for i, (base, amplitude, period, phase, opacity, stroke) in enumerate([
            (230, 70, 520, 0.0, 0.22, 2.0), (300, 46, 380, 1.3, 0.14, 1.5), (380, 96, 760, 2.4, 0.10, 2.5),
            (470, 34, 300, 0.7, 0.08, 1.2), (560, 60, 640, 3.1, 0.06, 2.0)]):
        points = " L".join(f"{x},{base + amplitude * math.sin(2 * math.pi * x / period + phase):.1f}"
                           for x in range(-20, width + 21, 10))
        waves.append(f'  <path d="M{points}" fill="none" stroke="#6CC4FF" stroke-width="{stroke}" '
                     f'stroke-opacity="{opacity}" stroke-linecap="round"/>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
            f'preserveAspectRatio="xMidYMin slice">\n' + "\n".join(waves) + "\n</svg>\n")


def social_card(shots):
    """Logo, name and tagline above the player screenshot."""
    width, height = 1200, 630
    pixels = bytearray(bytes(BACKDROP) * (width * height))
    font = Font(os.path.join(ROOT, "third_party", "fonts", "10x20.bdf"))

    icon_w, icon_h, icon = read_png_rgba(os.path.join(ROOT, "assets", "icons", "amplitude-128.png"))
    left, top = 50, 26
    for y in range(icon_h):
        for x in range(icon_w):
            r, g, b, a = icon[y][x * 4:x * 4 + 4]
            at = ((top + y) * width + left + x) * 3
            pixels[at:at + 3] = bytes((c * a + bg * (255 - a)) // 255 for c, bg in zip((r, g, b), BACKDROP))
    font.draw(pixels, width, 200, 34, "AMPLITUDE PLAYER", ACCENT, 4)
    font.draw(pixels, width, 202, 118, TAGLINE, MUTED, 1)

    shot_w, shot_h, shot = read_ppm(os.path.join(shots, "player.ppm"))
    left, top = (width - shot_w) // 2, 166
    for y in range(min(shot_h, height - top)):
        at = ((top + y) * width + left) * 3
        pixels[at:at + shot_w * 3] = shot[y * shot_w * 3:(y + 1) * shot_w * 3]
    return width, height, pixels


def main():
    shots = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    for name in ("player", "colors", "details"):
        width, height, pixels = read_ppm(os.path.join(shots, name + ".ppm"))
        write_png(os.path.join(OUT, name + ".png"), width, height, pixels)
    open(os.path.join(OUT, "background.svg"), "w").write(background_svg())
    write_png(os.path.join(OUT, "social.png"), *social_card(shots))
    shutil.copy(os.path.join(ROOT, "assets", "amplitude.svg"), os.path.join(OUT, "logo.svg"))
    shutil.copy(os.path.join(ROOT, "assets", "icons", "amplitude-256.png"), os.path.join(OUT, "logo-256.png"))
    shutil.copy(os.path.join(ROOT, "assets", "icons", "amplitude-32.png"), os.path.join(OUT, "favicon.png"))


if __name__ == "__main__":
    main()
