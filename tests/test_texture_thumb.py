import json
import math
import os
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


# k_DXT3A (format 58): the explicit-alpha half of a DXT3 block, 8 bytes per 4x4 block, four rows of 16 bits
# (after the endian swap, little-endian), texel i of a row in nibble i (low first), each value v -> v * 17.
# SDK: ConvertTexelDXT3AToDXT3 (conversion.cpp:80-85) and the texture_load_dxt3a compute shader.
def dxt3a_value(x, y):
    """The synthetic image's texel value (4 bits). The same rule and expected hash are in
    tests/native/test_texture_decode.cpp (the C++ decoder must give identical bytes)."""
    return (x * 5 + y * 3 + ((x * y) >> 2)) & 15


DXT3A_W, DXT3A_H, DXT3A_PITCH_BLOCKS = 70, 45, 32
DXT3A_FNV = 0x8382B24A4858EC9E  # FNV-1a 64 of the decoded R8 image, row-major


def dxt3a_guest_image(tiled, endian):
    """The synthetic image as guest memory: blocks at their tiled (or linear, pitch 32 blocks) offsets,
    each 8-byte block stored so the GPU's endian swap yields the little-endian rows."""
    wb, hb = (DXT3A_W + 3) // 4, (DXT3A_H + 3) // 4
    if tiled:
        size = max(tt.tiled_offset_2d(bx, by, DXT3A_PITCH_BLOCKS, 3) for bx in range(wb) for by in range(hb)) + 8
    else:
        size = (hb - 1) * DXT3A_PITCH_BLOCKS * 8 + wb * 8
    buf = bytearray(size)
    for by in range(hb):
        for bx in range(wb):
            post = bytearray()
            for r in range(4):
                v = sum(dxt3a_value(bx * 4 + i, by * 4 + r) << (4 * i) for i in range(4))
                post += bytes([v & 255, v >> 8])
            guest = tt._swap(bytes(post), endian)  # the swaps are their own inverses
            o = tt.tiled_offset_2d(bx, by, DXT3A_PITCH_BLOCKS, 3) if tiled else by * DXT3A_PITCH_BLOCKS * 8 + bx * 8
            buf[o:o + 8] = guest
    return bytes(buf)


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


class Dxt3aTest(unittest.TestCase):
    def test_hand_built_block_8in16(self):
        # Rows (texels 0..3): 0 1 2 3 / 4 5 6 7 / 8 9 10 11 / 12 13 14 15 -> post-swap row words
        # 0x3210 0x7654 0xBA98 0xFEDC, little-endian bytes 10 32 54 76 98 BA DC FE; guest (8in16) 32 10 76 54 BA 98 FE DC.
        guest = bytes([0x32, 0x10, 0x76, 0x54, 0xBA, 0x98, 0xFE, 0xDC])
        w, h, r8 = tt.decode_r8(guest, fc_for(58, 4, 4, 32, False, endian=1))
        self.assertEqual((w, h), (4, 4))
        self.assertEqual(r8, bytes(v * 17 for v in range(16)))

    def test_rgba_view_is_grey(self):
        guest = bytes([0x32, 0x10, 0x76, 0x54, 0xBA, 0x98, 0xFE, 0xDC])
        w, h, rgba = tt.decode_rgba(guest, fc_for(58, 4, 4, 32, False, endian=1))
        self.assertEqual(rgba[4 * 5:4 * 5 + 4], bytes([85, 85, 85, 255]))

    def test_synthetic_images_match_rule_and_hash(self):
        for tiled, endian in ((False, 1), (True, 2), (True, 0), (False, 3)):
            fc = fc_for(58, DXT3A_W, DXT3A_H, 128, tiled, endian=endian)
            w, h, r8 = tt.decode_r8(dxt3a_guest_image(tiled, endian), fc)
            self.assertEqual((w, h), (DXT3A_W, DXT3A_H))
            want = bytes(dxt3a_value(x, y) * 17 for y in range(DXT3A_H) for x in range(DXT3A_W))
            self.assertEqual(r8, want)
        self.assertEqual(fnv1a64(want), DXT3A_FNV)

    def test_real_tf18_dump_matches_cpp_decoder(self):
        """The terrain weight map, once a discovery capture has dumped it (tf18, fetch dword 1 0x1BA0207A).
        Needs FABLE2_TF18_BIN and FABLE2_TF18_FC (see tests/native/test_texture_decode.cpp) and the C++ test's
        output FABLE2_TF18_BIN + '.cpp.r8'; SKIPS (never passes silently) when any is missing."""
        bin_path, fcs = os.environ.get("FABLE2_TF18_BIN"), os.environ.get("FABLE2_TF18_FC")
        if not bin_path or not fcs or not Path(bin_path).is_file():
            self.skipTest("no tf18 dump: set FABLE2_TF18_BIN and FABLE2_TF18_FC after a discovery capture")
        cpp = Path(bin_path + ".cpp.r8")
        if not cpp.is_file():
            self.skipTest("run tests/run_native_tests.cmd with the same variables first (writes " + str(cpp) + ")")
        fc = [int(x, 16) for x in fcs.split(",")]
        w, h, r8 = tt.decode_r8(Path(bin_path).read_bytes(), fc)
        self.assertEqual(fc[1] & 0x3F, 58)
        self.assertEqual(r8, cpp.read_bytes())

    def test_format_name_in_cli_summary(self):
        self.assertEqual(tt._FORMAT_NAMES[58], "DXT3A")


def smooth_value(x, y):
    """A smooth 4-bit image: neighbouring texels differ by at most 1."""
    return int(round(7.5 + 7.5 * math.sin(x / 9.0) * math.cos(y / 7.0)))


def smooth_guest_image(w, h, nibble=lambda i: i, row=lambda r: r):
    """The smooth image as linear 8in16 guest memory (pitch 32 blocks). `nibble` and `row` say where
    texel i of a row and row r of a block are stored; the identity is the format's own layout."""
    wb, hb = (w + 3) // 4, (h + 3) // 4
    buf = bytearray(hb * 32 * 8)
    for by in range(hb):
        for bx in range(wb):
            words = [0, 0, 0, 0]
            for r in range(4):
                for i in range(4):
                    words[row(r)] |= smooth_value(bx * 4 + i, by * 4 + r) << (4 * nibble(i))
            post = b"".join(bytes([v & 255, v >> 8]) for v in words)
            o = by * 32 * 8 + bx * 8
            buf[o:o + 8] = tt._swap(post, 1)
    return bytes(buf)


class BlockStepTest(unittest.TestCase):
    """block_step_stats: the mean absolute step between neighbouring texels inside a 4x4 block and across a
    block edge, horizontally and vertically. A decode with the right layout gives about the same value on
    both sides; a wrong nibble order shows in the horizontal pair, a wrong row order in the vertical one."""
    W, H = 96, 64

    def _stats(self, **order):
        w, h, r8 = tt.decode_r8(smooth_guest_image(self.W, self.H, **order), fc_for(58, self.W, self.H, 128, False))
        return tt.block_step_stats(w, h, r8)

    def test_hand_counted_steps(self):
        # 8 x 1: 0 17 34 51 | 68 85 102 255: three in-block steps of 17 per block side (six in all), one across of 17.
        s = tt.block_step_stats(8, 1, bytes([0, 17, 34, 51, 68, 85, 102, 255]))
        self.assertAlmostEqual(s["h_in"], (5 * 17 + 153) / 6)
        self.assertAlmostEqual(s["h_cross"], 17.0)
        self.assertEqual((s["v_in"], s["v_cross"]), (0.0, 0.0))
        # 1 x 8 with the same values: the vertical pair instead.
        s = tt.block_step_stats(1, 8, bytes([0, 17, 34, 51, 68, 85, 102, 255]))
        self.assertAlmostEqual(s["v_in"], (5 * 17 + 153) / 6)
        self.assertAlmostEqual(s["v_cross"], 17.0)
        self.assertEqual((s["h_in"], s["h_cross"]), (0.0, 0.0))

    def test_correct_layout_is_even(self):
        s = self._stats()
        self.assertGreater(s["h_in"], 0)
        self.assertGreater(s["v_in"], 0)
        self.assertLess(abs(s["h_cross"] / s["h_in"] - 1), 0.25)
        self.assertLess(abs(s["v_cross"] / s["v_in"] - 1), 0.25)

    def test_wrong_nibble_order_shows_horizontally(self):
        good = self._stats()
        mirrored = self._stats(nibble=lambda i: 3 - i)     # texel i in nibble 3 - i
        pairs = self._stats(nibble=lambda i: i ^ 1)        # the two nibbles of each byte exchanged
        for s in (mirrored, pairs):
            self.assertGreater(s["h_cross"] / s["h_in"], 1.5)
            self.assertLess(abs(s["v_cross"] / s["v_in"] - 1), 0.25)
        # Mirrored texels keep the steps inside a block and tear the block edges; exchanged pairs raise both.
        self.assertAlmostEqual(mirrored["h_in"], good["h_in"])
        self.assertGreater(pairs["h_in"], 1.3 * good["h_in"])
        self.assertGreater(mirrored["h_cross"], 3 * good["h_cross"])

    def test_wrong_row_order_shows_vertically(self):
        good = self._stats()
        mirrored = self._stats(row=lambda r: 3 - r)
        pairs = self._stats(row=lambda r: r ^ 1)           # the two 16-bit rows of each dword exchanged
        for s in (mirrored, pairs):
            self.assertGreater(s["v_cross"] / s["v_in"], 1.5)
            self.assertLess(abs(s["h_cross"] / s["h_in"] - 1), 0.25)
        self.assertAlmostEqual(mirrored["v_in"], good["v_in"])
        self.assertGreater(pairs["v_in"], 1.3 * good["v_in"])

    def test_cli_block_stat_line(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "tex").mkdir()
            fc = fc_for(58, self.W, self.H, 128, False)
            (root / "tex" / "w.bin").write_bytes(smooth_guest_image(self.W, self.H))
            row = {"kind": "texture", "file": "tex/w.bin", "bytes": 1, "fc": [f"0x{x:08X}" for x in fc]}
            (root / "cap.jsonl").write_text(json.dumps(row) + chr(10))
            lines = tt.main([str(root / "cap.jsonl"), "--out", str(root / "out"), "--block-stat"])
        stat = [line for line in lines if "block steps" in line]
        self.assertEqual(len(stat), 1)
        self.assertRegex(stat[0], r"horizontal in-block [0-9.]+ across [0-9.]+ \(ratio [0-9.]+\), "
                                  r"vertical in-block [0-9.]+ across [0-9.]+ \(ratio [0-9.]+\)")


def png_pixel(path):
    """First pixel of an RGBA8 PNG written by write_png."""
    raw = Path(path).read_bytes()
    at = raw.index(b"IDAT")
    size = struct.unpack(">I", raw[at - 4:at])[0]
    return zlib.decompress(raw[at + 4:at + 4 + size])[1:5]


if __name__ == "__main__":
    unittest.main()
