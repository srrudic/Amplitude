#!/usr/bin/env python3
"""Generates the Amplitude logo and every icon format derived from it.

    tools/genlogo.py

The logo is a letter "A" drawn as a waveform: a signal that dips, swells
into one tall peak (the two legs of the A) and dips again, with a small sine
wave as its crossbar. The same shape is drawn inside the player by logo() in
src/skin_default.c; keep the numbers there in step with the ones here. Everything is computed from the few numbers below, so the SVG and
the rendered icons always match.

Writes:
    assets/amplitude.svg              scalable original
    assets/icons/amplitude-N.png      16 ... 256 px
    packaging/amplitude.ico           Windows icon (16, 32, 48 px)
    src/icon_data.c                   48 px icon for the X11 window property

Needs only the Python standard library.
"""
import math
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Colours shared with the built-in skin.
BACKGROUND = (0x1C, 0x20, 0x30)
WAVE = (0x6C, 0xC4, 0xFF)
CROSSBAR = (0x6C, 0xC4, 0xFF)

# Geometry on a 256 x 256 canvas.
MARGIN, CORNER = 8, 52
BASELINE, PEAK_Y = 184, 52
TAIL_START, FOOT, CENTRE = 30, 58, 128
WAVE_WIDTH, BAR_WIDTH = 20, 12
BAR_Y, BAR_AMPLITUDE = 138, 9
# How wave-like the outline is. With all of DIP, LIFT at 0 and a small FLARE
# it is a plain "A" on a flat line; larger values bend it towards a sine wave.
DIP = 6             # how far the feet sink below the baseline (a trough)
LIFT = 3            # how far the outer ends rise above it
FLARE = 22          # how long the leg runs level before it starts to climb
CREST, CREST_DROP = 22, 0   # direction the leg arrives at the peak: wide and
                            # shallow rounds the top, narrow and deep points it;
                            # a drop of 0 makes the crest perfectly smooth


def bezier(p0, p1, p2, p3, steps=40):
    points = []
    for i in range(steps + 1):
        t = i / steps
        u = 1 - t
        points.append(tuple(u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d
                            for a, b, c, d in zip(p0, p1, p2, p3)))
    return points


def leg_points(steps=40):
    """The left leg: it leaves the baseline in a short curve, like a wave
    starting to rise, then runs almost straight up to the peak."""
    foot_y = BASELINE + DIP
    return bezier((FOOT, foot_y), (FOOT + FLARE, foot_y), (CENTRE - CREST, PEAK_Y + CREST_DROP),
                  (CENTRE, PEAK_Y), steps)


def tail_points(steps=12):
    """The left end: from the outer tip down into the trough at the foot."""
    tail_y, foot_y = BASELINE - LIFT, BASELINE + DIP
    return bezier((TAIL_START, tail_y), (TAIL_START + 12, tail_y), (FOOT - 14, foot_y), (FOOT, foot_y), steps)


def wave_points():
    """Tail, a leg that leaves the baseline like a wave and rises to the
    peak, then the mirror image."""
    left = tail_points()[:-1] + leg_points()
    right = [(2 * CENTRE - x, y) for x, y in reversed(left[:-1])]
    return left + right


def crossbar_points():
    """One sine period spanning the gap between the legs."""
    left = min(leg_points(400), key=lambda p: abs(p[1] - BAR_Y))[0]
    right = 2 * CENTRE - left
    return [(left + (right - left) * i / 48, BAR_Y - BAR_AMPLITUDE * math.sin(2 * math.pi * i / 48))
            for i in range(49)]


def stamp(mask, n, points, width, scale):
    """Marks every sample within width/2 of the polyline (round caps and joins)."""
    r = width * scale / 2
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        x0, y0, x1, y1 = x0 * scale, y0 * scale, x1 * scale, y1 * scale
        dx, dy = x1 - x0, y1 - y0
        length2 = dx * dx + dy * dy or 1e-9
        for y in range(max(0, int(min(y0, y1) - r)), min(n, int(max(y0, y1) + r) + 2)):
            row = y * n
            for x in range(max(0, int(min(x0, x1) - r)), min(n, int(max(x0, x1) + r) + 2)):
                px, py = x + 0.5 - x0, y + 0.5 - y0
                t = max(0.0, min(1.0, (px * dx + py * dy) / length2))
                ex, ey = px - t * dx, py - t * dy
                if ex * ex + ey * ey <= r * r:
                    mask[row + x] = 1


def render(size, oversample=4):
    """Returns size*size RGBA tuples."""
    n = size * oversample
    scale = n / 256
    # Small icons need proportionally heavier strokes to stay readable.
    weight = 1 + max(0, 48 - size) / 48 * 0.7
    background, wave, bar = bytearray(n * n), bytearray(n * n), bytearray(n * n)

    lo, hi, c = MARGIN * scale, (256 - MARGIN) * scale, CORNER * scale
    for y in range(n):
        for x in range(n):
            px, py = x + 0.5, y + 0.5
            if not (lo <= px <= hi and lo <= py <= hi):
                continue
            # Inside unless in a corner square but outside its quarter circle.
            cx = min(max(px, lo + c), hi - c)
            cy = min(max(py, lo + c), hi - c)
            if (px - cx) ** 2 + (py - cy) ** 2 <= c * c:
                background[y * n + x] = 1
    stamp(wave, n, wave_points(), WAVE_WIDTH * weight, scale)
    stamp(bar, n, crossbar_points(), BAR_WIDTH * weight, scale)

    pixels = []
    total = oversample * oversample
    for y in range(size):
        for x in range(size):
            sums, alpha = [0, 0, 0], 0
            for sy in range(oversample):
                row = (y * oversample + sy) * n + x * oversample
                for i in range(row, row + oversample):
                    if not background[i]:
                        continue    # everything is clipped to the rounded square
                    colour = WAVE if wave[i] else CROSSBAR if bar[i] else BACKGROUND
                    alpha += 1
                    for k in range(3):
                        sums[k] += colour[k]
            pixels.append(tuple(v // alpha for v in sums) + (alpha * 255 // total,) if alpha else (0, 0, 0, 0))
    return pixels


def png_bytes(size, pixels):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    raw = b"".join(b"\0" + b"".join(bytes(p) for p in pixels[y * size:(y + 1) * size]) for y in range(size))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico_image(size, pixels):
    """A 32-bit bitmap entry (BGRA, bottom-up) with a 1-bit mask, the form
    that every Windows version back to 95 can read."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    colour, mask = b"", b""
    mask_stride = (size + 31) // 32 * 4
    for y in reversed(range(size)):
        row = pixels[y * size:(y + 1) * size]
        colour += b"".join(bytes((b, g, r, a)) for r, g, b, a in row)
        bits = bytearray(mask_stride)
        for x, p in enumerate(row):
            if p[3] < 128:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bytes(bits)
    return header + colour + mask


def svg():
    def path(points):
        return "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in points)
    def rgb(c):
        return "#%02X%02X%02X" % c
    side = 256 - 2 * MARGIN
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <title>Amplitude</title>
  <rect x="{MARGIN}" y="{MARGIN}" width="{side}" height="{side}" rx="{CORNER}" fill="{rgb(BACKGROUND)}"/>
  <g fill="none" stroke-linecap="round" stroke-linejoin="round">
    <path d="{path(crossbar_points())}" stroke="{rgb(CROSSBAR)}" stroke-width="{BAR_WIDTH}"/>
    <path d="{path(wave_points())}" stroke="{rgb(WAVE)}" stroke-width="{WAVE_WIDTH}"/>
  </g>
</svg>
'''


def main():
    open(os.path.join(ROOT, "assets/amplitude.svg"), "w").write(svg())

    rendered = {}
    for size in (16, 24, 32, 48, 64, 128, 256):
        rendered[size] = render(size)
        open(os.path.join(ROOT, f"assets/icons/amplitude-{size}.png"), "wb").write(png_bytes(size, rendered[size]))

    sizes = (16, 32, 48)
    images = [ico_image(s, rendered[s]) for s in sizes]
    offset = 6 + 16 * len(sizes)
    ico = struct.pack("<HHH", 0, 1, len(sizes))
    for s, image in zip(sizes, images):
        ico += struct.pack("<BBBBHHII", s, s, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
    open(os.path.join(ROOT, "packaging/amplitude.ico"), "wb").write(ico + b"".join(images))

    with open(os.path.join(ROOT, "src/icon_data.c"), "w") as out:
        out.write("/* Generated by tools/genlogo.py. Do not edit. */\n")
        out.write('#include "icon.h"\n\n')
        out.write("const uint32_t app_icon[APP_ICON_SIZE * APP_ICON_SIZE] = {\n")
        for y in range(48):
            row = rendered[48][y * 48:(y + 1) * 48]
            out.write("    " + ",".join("0x%02X%02X%02X%02X" % (a, r, g, b) for r, g, b, a in row) + ",\n")
        out.write("};\n")


if __name__ == "__main__":
    main()
