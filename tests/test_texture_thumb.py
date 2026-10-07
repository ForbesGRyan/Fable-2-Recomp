import json
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

    def test_linear_base_pitch_follows_sdk(self):
        # SDK GetGuestTextureLayout base level: row pitch = fetch pitch in blocks aligned to 32 blocks.
        # 8888 linear, 4x2, pitch 32 texels -> 128-byte rows (no 256-byte padding).
        buf = bytearray(512)
        buf[0:4] = bytes([1, 2, 3, 4])
        buf[128:132] = bytes([9, 8, 7, 6])  # row 1 starts at 128
        w, h, rgba = tt.decode_rgba(bytes(buf), fc_for(6, 4, 2, 32, False, endian=0))
        self.assertEqual(rgba[4 * 4:4 * 4 + 4], bytes([9, 8, 7, 6]))
        # DXT4_5 linear, 4x8 (two block rows), pitch 32 texels = 8 blocks -> 32 blocks * 16 = 512 bytes.
        red = bytes([255, 255]) + bytes(6) + struct.pack("<HHI", 0xF800, 0xF800, 0)
        buf = bytearray(1024)
        buf[512:528] = red  # block row 1 starts at 512, not 256
        w, h, rgba = tt.decode_rgba(bytes(buf), fc_for(20, 4, 8, 32, False, endian=0))
        self.assertEqual(rgba[(4 * 4) * 4:(4 * 4) * 4 + 4], bytes([255, 0, 0, 255]))

    def test_alpha_only_image_and_shares(self):
        # Four texels: alpha 0, 255, 128, 255 with colours that must not leak into the alpha image.
        rgba = bytes([10, 20, 30, 0, 40, 50, 60, 255, 70, 80, 90, 128, 1, 2, 3, 255])
        self.assertEqual(tt.alpha_rgba(rgba), bytes([0, 0, 0, 255, 255, 255, 255, 255,
                                                     128, 128, 128, 255, 255, 255, 255, 255]))
        self.assertEqual(tt.alpha_stats(rgba), {"zero": 0.25, "full": 0.5, "between": 0.25})
        self.assertEqual(tt.alpha_stats(b""), {"zero": 0.0, "full": 0.0, "between": 0.0})

    def test_cli_writes_colour_and_alpha_thumbnails(self):
        # A capture with one DXT4_5 texture row (alpha 200, red) and one row the filter leaves out.
        block = bytes([200, 0]) + bytes(6) + struct.pack("<HHI", 0xF800, 0xF800, 0)
        fc = fc_for(20, 4, 4, 32, False, endian=0)
        other = fc_for(18, 4, 4, 32, False)
        other[1] = (other[1] & 0xFFF) | 0x1B000000
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "native_tex_x").mkdir()
            (d / "native_tex_x" / "a.bin").write_bytes(block + bytes(256))
            (d / "native_tex_x" / "b.bin").write_bytes(bytes(256))
            rows = [{"kind": "texture", "file": "native_tex_x/a.bin", "bytes": 272, "fc": [f"0x{v:08X}" for v in fc]},
                    {"kind": "texture", "file": "native_tex_x/b.bin", "bytes": 256, "fc": [f"0x{v:08X}" for v in other]},
                    {"kind": "draw", "frame": 1}]
            cap = d / "cap.jsonl"
            cap.write_text("".join(json.dumps(r) + "\n" for r in rows))
            out = d / "out"
            lines = tt.main([str(cap), "--out", str(out), "--alpha", "--base", f"0x{fc[1]:08X}"])
            name = f"{fc[1]:08X}"
            self.assertEqual(sorted(p.name for p in out.iterdir()), [name + ".png", name + "_alpha.png"])
            self.assertEqual(len(lines), 1)
            self.assertIn("DXT4_5 4x4", lines[0])
            self.assertIn("alpha 0: 0.0%, 255: 0.0%, between: 100.0%", lines[0])
            # The alpha image is opaque grey 200; the colour image keeps the texture's own alpha.
            self.assertEqual(png_pixel(out / (name + "_alpha.png")), bytes([200, 200, 200, 255]))
            self.assertEqual(png_pixel(out / (name + ".png")), bytes([255, 0, 0, 200]))
            # Without --alpha only the colour thumbnail is written; without --base every texture is.
            out2 = d / "out2"
            self.assertEqual(len(tt.main([str(cap), "--out", str(out2)])), 2)
            self.assertEqual(len(list(out2.iterdir())), 2)
            self.assertFalse(any(p.name.endswith("_alpha.png") for p in out2.iterdir()))


def png_pixel(path):
    """First pixel of an RGBA8 PNG written by write_png."""
    raw = Path(path).read_bytes()
    at = raw.index(b"IDAT")
    size = struct.unpack(">I", raw[at - 4:at])[0]
    return zlib.decompress(raw[at + 4:at + 4 + size])[1:5]


if __name__ == "__main__":
    unittest.main()
