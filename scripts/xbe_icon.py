#!/usr/bin/env python3
"""The XBE's dashboard title image ($$XTIMAGE, DXT1) as a Windows .ico.

    python scripts/xbe_icon.py game_files/default.xbe src/recomp/gen/amped2.ico

Run by run_pipeline.sh (recomp step): the icon comes from the player's own disc and lands next to the generated code,
which is never committed; CMake embeds it into the Windows exe when it exists. Same decoding as src/title_image.c
(which sets the window icon at run time). Standard library only: PNG entries at 128, 64, 48, 32 and 16 pixels.
"""
import struct
import sys
import zlib


def title_image(xbe):
    base, nsec, shdr = struct.unpack_from("<I", xbe, 0x104)[0], *struct.unpack_from("<II", xbe, 0x11C)
    shdr -= base
    for i in range(nsec):
        _, _, _, raw, rsize, name = struct.unpack_from("<6I", xbe, shdr + i * 56)
        if xbe[name - base:name - base + 10] != b"$$XTIMAGE\0":
            continue
        x = xbe[raw:raw + rsize]
        hdr = struct.unpack_from("<I", x, 8)[0]
        fmt = struct.unpack_from("<I", x, 24)[0]
        if x[:4] != b"XPR0" or (fmt >> 8) & 0xFF != 0x0C:
            raise SystemExit("title image is not a DXT1 XPR texture")
        w, h = 1 << ((fmt >> 20) & 15), 1 << ((fmt >> 24) & 15)
        px = [(0, 0, 0, 0)] * (w * h)

        def c565(c):
            return ((c >> 11 & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31, 255)

        off = hdr
        for by in range(h // 4):
            for bx in range(w // 4):
                c0, c1, bits = struct.unpack_from("<HHI", x, off)
                off += 8
                a, b = c565(c0), c565(c1)
                if c0 > c1:
                    pal = [a, b, tuple((2 * p + q) // 3 for p, q in zip(a, b)),
                           tuple((p + 2 * q) // 3 for p, q in zip(a, b))]
                else:
                    pal = [a, b, tuple((p + q) // 2 for p, q in zip(a, b)), (0, 0, 0, 0)]
                for k in range(16):
                    px[(by * 4 + k // 4) * w + bx * 4 + k % 4] = pal[bits >> (2 * k) & 3]
        return w, h, px
    raise SystemExit("no $$XTIMAGE section in the XBE")


def downscale(w, h, px, size):
    f = w // size
    out = []
    for y in range(size):
        for x in range(size):
            acc = [0, 0, 0, 0]
            for dy in range(f):
                for dx in range(f):
                    p = px[(y * f + dy) * w + x * f + dx]
                    for c in range(4):
                        acc[c] += p[c]
            out.append(tuple(v // (f * f) for v in acc))
    return out


def png(size, px):
    rows = b"".join(b"\0" + bytes(c for p in px[y * size:(y + 1) * size] for c in p) for y in range(size))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main():
    xbe = open(sys.argv[1], "rb").read()
    w, h, px = title_image(xbe)
    if w != h:
        raise SystemExit("title image is not square")
    images = [(s, png(s, px if s == w else downscale(w, h, px, s))) for s in (128, 64, 48, 32, 16) if s <= w]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, data in images:
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    out += b"".join(data for _, data in images)
    with open(sys.argv[2], "wb") as f:
        f.write(out)
    print(f"icon: {sys.argv[2]} ({w}x{h} title image, {len(images)} sizes)")


if __name__ == "__main__":
    main()
