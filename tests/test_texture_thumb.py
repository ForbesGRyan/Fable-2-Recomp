import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import texture_thumb as tt  # noqa: E402


def fc_for(fmt, w, h, pitch, tiled, endian=1):
    return [2 | ((pitch >> 5) << 22) | (int(tiled) << 31), fmt | (endian << 6) | 0x1A000000,
            (w - 1) | ((h - 1) << 13), 0x688 << 1, 0, (1 << 9)]


class ThumbTest(unittest.TestCase):
    def test_tiled_offset_matches_cpp(self):
        # Values from capture::TiledOffset2D (tests/native/test_terrain_patch.cpp uses the same function).
        self.assertEqual(tt.tiled_offset_2d(0, 0, 32, 2), 0)
        self.assertEqual(tt.tiled_offset_2d(1, 0, 32, 2), 4)
        self.assertEqual(tt.tiled_offset_2d(0, 1, 32, 2), 16)
        self.assertEqual(tt.tiled_offset_2d(8, 0, 32, 2), 64)

    def test_dxt1_solid_red(self):
        # One 4x4 DXT1 block, linear, 8in16: color0 = color1 = red (0xF800), indices 0.
        block = struct.pack(">HHI", 0xF800, 0xF800, 0)  # big-endian words; 8in16 makes them LE
        data = block + bytes(256 - len(block))
        w, h, rgba = tt.decode_rgba(data, fc_for(18, 4, 4, 32, False))
        self.assertEqual((w, h), (4, 4))
        self.assertEqual(rgba[0:4], bytes([255, 0, 0, 255]))

    def test_8888_tiled(self):
        pitch = 32
        size = max(tt.tiled_offset_2d(x, y, pitch, 2) for x in range(32) for y in range(32)) + 4
        buf = bytearray(size)
        for y in range(32):
            for x in range(32):
                o = tt.tiled_offset_2d(x, y, pitch, 2)
                buf[o:o + 4] = bytes([255, x, y, 7])  # 8in32 swaps to (7, y, x, 255)
        w, h, rgba = tt.decode_rgba(bytes(buf), fc_for(6, 32, 32, 32, True, endian=2))
        i = (5 * 32 + 3) * 4
        self.assertEqual(rgba[i:i + 4], bytes([7, 5, 3, 255]))

    def test_unsupported_format(self):
        with self.assertRaises(ValueError):
            tt.decode_rgba(bytes(64), fc_for(7, 4, 4, 32, False))

    def test_png(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "t.png"
            tt.write_png(p, 2, 1, bytes([1, 2, 3, 255, 4, 5, 6, 255]))
            raw = p.read_bytes()
            self.assertEqual(raw[:8], b"\x89PNG\r\n\x1a\n")
            self.assertIn(b"IDAT", raw)

    def test_dxt45_alpha_and_downscale(self):
        # alpha0 = 200, alpha1 = 0, indices 0 -> alpha 200; red color block; endian 0 (no swap).
        block = bytes([200, 0]) + bytes(6) + struct.pack("<HHI", 0xF800, 0xF800, 0)
        w, h, rgba = tt.decode_rgba(block + bytes(256), fc_for(20, 4, 4, 32, False, endian=0))
        self.assertEqual(rgba[0:4], bytes([255, 0, 0, 200]))
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "big.png"
            tt.write_png(p, 512, 512, bytes(512 * 512 * 4), max_edge=256)
            self.assertEqual(struct.unpack(">II", p.read_bytes()[16:24]), (256, 256))


if __name__ == "__main__":
    unittest.main()
