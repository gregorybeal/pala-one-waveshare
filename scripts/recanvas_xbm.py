#!/usr/bin/env python3
"""Re-center a C-header XBM bitmap onto a canvas of a different size.

Used to move the built-in sleep image (src/ui/pala_one_sleep_black_icon_v4.h)
between panel sizes without resampling: the artwork is cropped / padded
around its centre, so it stays pixel-exact as long as the drawn content
fits the new canvas.

    python3 scripts/recanvas_xbm.py IN.h OUT.h WIDTH HEIGHT [--png PREVIEW.png]

The XBM is LSB-first, rows padded to whole bytes (Adafruit_GFX drawXBitmap
format). Padding added around the image copies the input's corner pixel, so
the background stays continuous. --png writes a 3x preview (set bit = white)
with only the standard library.
"""

import argparse
import re
import struct
import sys
import zlib


def read_xbm(path):
    text = open(path).read()
    w = int(re.search(r"#define\s+\w+_width\s+(\d+)", text).group(1))
    h = int(re.search(r"#define\s+\w+_height\s+(\d+)", text).group(1))
    name = re.search(r"#define\s+(\w+)_width", text).group(1)
    data = [int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", text)]
    row = (w + 7) // 8
    if len(data) != row * h:
        sys.exit(f"{path}: expected {row * h} bytes for {w}x{h}, found {len(data)}")
    pixels = [[(data[y * row + x // 8] >> (x & 7)) & 1 for x in range(w)] for y in range(h)]
    return name, w, h, pixels


def recanvas(pixels, w, h, new_w, new_h):
    bg = pixels[0][0]
    dx = (new_w - w) // 2
    dy = (new_h - h) // 2
    out = []
    for y in range(new_h):
        sy = y - dy
        out.append([
            pixels[sy][x - dx] if 0 <= sy < h and 0 <= x - dx < w else bg
            for x in range(new_w)
        ])
    return out


def write_xbm(path, name, w, h, pixels):
    row = (w + 7) // 8
    data = []
    for y in range(h):
        for bx in range(row):
            byte = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < w and pixels[y][x]:
                    byte |= 1 << bit
            data.append(byte)
    guard = name.upper() + "_H"
    lines = [f"#ifndef {guard}", f"#define {guard}", "",
             f"#define {name}_width {w}", f"#define {name}_height {h}",
             f"static const unsigned char {name}_bits[] PROGMEM = {{"]
    for i in range(0, len(data), 16):
        lines.append("".join(f"0x{b:02x}, " for b in data[i:i + 16]))
    lines += ["};", "", f"#endif  // {guard}", ""]
    open(path, "w").write("\n".join(lines))


def write_png(path, w, h, pixels, scale=3):
    raw = b"".join(
        b"\x00" + bytes(255 if pixels[y // scale][x // scale] else 0 for x in range(w * scale))
        for y in range(h * scale)
    )

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w * scale, h * scale, 8, 0, 0, 0, 0)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("width", type=int)
    ap.add_argument("height", type=int)
    ap.add_argument("--png")
    args = ap.parse_args()

    name, w, h, pixels = read_xbm(args.src)
    out = recanvas(pixels, w, h, args.width, args.height)
    write_xbm(args.dst, name, args.width, args.height, out)
    if args.png:
        write_png(args.png, args.width, args.height, out)


if __name__ == "__main__":
    main()
