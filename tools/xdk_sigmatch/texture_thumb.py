"""Decode Xenos 2D texture base levels dumped by the native capture (discovery D3) and write PNG thumbnails.

Supported formats: DXT1 (18), DXT2_3 (19), DXT4_5 (20), 8888 (6). Others raise ValueError("format N").
The fetch constant is six host-order dwords (xenos::xe_gpu_texture_fetch_t).
"""
import struct
import zlib
from pathlib import Path


def tiled_offset_2d(x, y, pitch, bpb_log2):
    """Port of capture::TiledOffset2D (src/native/capture/xenos_tiling.h); x, y, pitch in blocks."""
    pitch = (pitch + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bpb_log2 + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << bpb_log2
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2)
            + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F))


def _swap(b, endian):
    """The GPU's endian swap (xenos::Endian) over a byte string."""
    b = bytearray(b)
    if endian == 1:
        b[0::2], b[1::2] = b[1::2], b[0::2]
    elif endian == 2:
        b[0::4], b[1::4], b[2::4], b[3::4] = b[3::4], b[2::4], b[1::4], b[0::4]
    elif endian == 3:
        b[0::4], b[1::4], b[2::4], b[3::4] = b[2::4], b[3::4], b[0::4], b[1::4]
    return bytes(b)


def _rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return ((r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31)


def _color_block(blk, force4):
    """8-byte DXT1 color block (little-endian after the swap) -> 16 RGBA tuples, row-major."""
    c0, c1, idx = struct.unpack("<HHI", blk)
    p0, p1 = _rgb565(c0), _rgb565(c1)
    if c0 > c1 or force4:
        pal = [p0 + (255,), p1 + (255,),
               tuple((2 * a + b) // 3 for a, b in zip(p0, p1)) + (255,),
               tuple((a + 2 * b) // 3 for a, b in zip(p0, p1)) + (255,)]
    else:
        pal = [p0 + (255,), p1 + (255,), tuple((a + b) // 2 for a, b in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
    return [pal[(idx >> (2 * i)) & 3] for i in range(16)]


def _alpha_dxt3(blk):
    v = int.from_bytes(blk, "little")
    return [((v >> (4 * i)) & 15) * 17 for i in range(16)]


def _alpha_dxt5(blk):
    a0, a1 = blk[0], blk[1]
    bits = int.from_bytes(blk[2:8], "little")
    if a0 > a1:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode_rgba(data, fc):
    """Base level of a 2D fetch constant -> (width, height, RGBA bytes). Data past the end reads as zero."""
    fmt = fc[1] & 0x3F
    endian = (fc[1] >> 6) & 3
    w = (fc[2] & 0x1FFF) + 1
    h = ((fc[2] >> 13) & 0x1FFF) + 1
    pitch = ((fc[0] >> 22) & 0x1FF) << 5
    tiled = (fc[0] >> 31) != 0
    if fmt not in (6, 18, 19, 20):
        raise ValueError(f"format {fmt}")
    if fmt == 6:
        block, bpb, log2 = 1, 4, 2
    elif fmt == 18:
        block, bpb, log2 = 4, 8, 3
    else:
        block, bpb, log2 = 4, 16, 4
    wb, hb = (w + block - 1) // block, (h + block - 1) // block
    pitch_blocks = max(pitch // block, wb)
    out = bytearray(w * h * 4)
    for by in range(hb):
        for bx in range(wb):
            o = tiled_offset_2d(bx, by, pitch_blocks, log2) if tiled else (by * pitch_blocks + bx) * bpb
            raw = data[o:o + bpb]
            raw = _swap(raw + bytes(bpb - len(raw)), endian)
            if fmt == 6:
                texels = [tuple(raw)]
            elif fmt == 18:
                texels = _color_block(raw, False)
            else:
                alpha = _alpha_dxt3(raw[:8]) if fmt == 19 else _alpha_dxt5(raw[:8])
                texels = [c[:3] + (a,) for c, a in zip(_color_block(raw[8:], True), alpha)]
            for i, t in enumerate(texels):
                x, y = bx * block + (i % block), by * block + (i // block)
                if x < w and y < h:
                    out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes(t)
    return w, h, bytes(out)


def write_png(path, w, h, rgba, max_edge=256):
    """RGBA8 PNG, nearest-neighbour downscaled so the longer edge is at most max_edge."""
    if max(w, h) > max_edge:
        s = max_edge / max(w, h)
        nw, nh = max(1, int(w * s)), max(1, int(h * s))
        rows = []
        for y in range(nh):
            sy = y * h // nh
            row = bytearray()
            for x in range(nw):
                sx = x * w // nw
                row += rgba[(sy * w + sx) * 4:(sy * w + sx) * 4 + 4]
            rows.append(bytes(row))
        w, h = nw, nh
    else:
        rows = [rgba[y * w * 4:(y + 1) * w * 4] for y in range(h)]
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    Path(path).write_bytes(png)
