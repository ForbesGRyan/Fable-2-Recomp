"""The format-3 (k_1_5_5_5) decoder's Python twin (distant trees, plan Task 7): tools/xdk_sigmatch/impostor_pictures.py
against the numbers of tests/native/test_texture_decode.cpp (returns 160 to 177), and its capture readers."""
import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))
SPEC = importlib.util.spec_from_file_location("impostor_pictures", ROOT / "tools" / "xdk_sigmatch" / "impostor_pictures.py")
ip = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ip)
import texture_thumb  # noqa: E402

# tf0 of a 129F row and the texture row of the same base (capture native_discovery_20261011_074119).
FC = [0x8200D802, 0x12704043, 0x001FE0FF, 0x00A80C14, 0x000000C3, 0x12724A18]


def put(buf, offset, word):
    buf[offset] = word >> 8          # guest order: high byte first
    buf[offset + 1] = word & 0xFF


def five_texels():
    src = bytearray(131072)
    put(src, 0, 0x8000 | 31)                 # (0, 0): alpha, the low field full
    put(src, 2, 31 << 5)                     # (1, 0): the middle field, no alpha
    put(src, 16, 0x8000 | (31 << 10))        # (0, 1): the high field
    put(src, 64, 0x8000 | (16 << 5) | 1)     # (8, 0): rounding: 16 -> 132, 1 -> 8
    put(src, 130942, 0xFFFF)                 # (255, 255)
    return src


class Decode1555(unittest.TestCase):
    def test_the_five_texels_of_the_native_test(self):
        for lo in (0, 2):
            hi = 2 - lo
            out = ip.decode_1555(bytes(five_texels()), 256, 256, 256, True, 1, lo)
            self.assertEqual(out.shape, (256, 256, 4))
            self.assertEqual(out.dtype, np.uint8)
            px = lambda x, y: [int(v) for v in out[y, x]]   # noqa: E731
            self.assertEqual((px(0, 0)[lo], px(0, 0)[1], px(0, 0)[hi], px(0, 0)[3]), (255, 0, 0, 255))
            self.assertEqual((px(1, 0)[lo], px(1, 0)[1], px(1, 0)[hi], px(1, 0)[3]), (0, 255, 0, 0))
            self.assertEqual((px(0, 1)[lo], px(0, 1)[1], px(0, 1)[hi], px(0, 1)[3]), (0, 0, 255, 255))
            self.assertEqual((px(8, 0)[lo], px(8, 0)[1], px(8, 0)[hi]), (8, 132, 0))
            self.assertEqual(px(255, 255), [255, 255, 255, 255])
            self.assertEqual(px(2, 0), [0, 0, 0, 0])
            self.assertEqual(int(out.astype(np.int64).sum()), 510 + 255 + 510 + (8 + 132 + 255) + 1020)   # nothing else is set

    def test_the_low_field_is_the_sdks_red(self):
        self.assertEqual(ip.LOW_FIELD_CHANNEL, 0)
        text = (ROOT / "src" / "native" / "render" / "texture_decode.h").read_text(encoding="utf-8")
        self.assertIn("inline constexpr uint32_t k1555LowFieldChannel = %d;" % ip.LOW_FIELD_CHANNEL, text)
        # the default is the constant
        out = ip.decode_1555(bytes(five_texels()), 256, 256, 256, True, 1)
        self.assertEqual([int(v) for v in out[0, 0]], [255, 0, 0, 255])

    def test_a_short_source_is_refused(self):
        src = bytes(five_texels())
        with self.assertRaises(ValueError):
            ip.decode_1555(src[:130943], 256, 256, 256, True, 1, 0)          # one byte short
        with self.assertRaises(ValueError):
            ip.decode_1555(src, 0, 256, 256, True, 1, 0)                     # an empty image
        with self.assertRaises(ValueError):
            ip.decode_1555(src, 256, 256, 128, True, 1, 0)                   # a pitch below the width
        with self.assertRaises(ValueError):
            ip.decode_1555(src, 8193, 1, 8224, True, 1, 0)
        with self.assertRaises(ValueError):
            ip.decode_1555(src, 256, 256, 256, True, 1, 1)                   # the low field is red or blue

    def test_linear_and_the_endian(self):
        lin = bytearray(4 * 512)
        lin[512 * 1 + 2 * 3] = 31            # (3, 1): the low field, as stored (endian 0: the low byte first)
        lin[512 * 1 + 2 * 3 + 1] = 0x80      # and alpha
        for endian in (0, 2, 3):             # only endian 1 exchanges a texel's two bytes
            out = ip.decode_1555(bytes(lin), 4, 4, 256, False, endian, 0)
            self.assertEqual(out.shape, (4, 4, 4))
            self.assertEqual([int(v) for v in out[1, 3]], [255, 0, 0, 255])
            self.assertEqual(int(out.astype(np.int64).sum()), 510)
        out = ip.decode_1555(bytes(lin), 4, 4, 256, False, 1, 0)             # the word 0x1F80
        self.assertEqual([int(v) for v in out[1, 3]], [0, 230, 58, 0])
        self.assertEqual(ip.decode_1555(bytes(lin[:3 * 512 + 8]), 4, 4, 256, False, 0, 0).shape, (4, 4, 4))
        with self.assertRaises(ValueError):
            ip.decode_1555(bytes(lin[:3 * 512 + 7]), 4, 4, 256, False, 0, 0)

    def test_the_tiled_offsets_are_the_scalar_ports(self):
        for w, h, pitch in ((256, 256, 256), (128, 128, 128), (64, 64, 64), (32, 32, 32), (40, 24, 64)):
            got = ip.tiled_offsets(w, h, pitch, 1)
            for y in range(0, h, 7):
                for x in range(0, w, 5):
                    self.assertEqual(int(got[y, x]), texture_thumb.tiled_offset_2d(x, y, pitch, 1), (w, h, x, y))
        self.assertEqual(int(ip.tiled_offsets(256, 256, 256, 1)[255, 255]), 130942)   # the native test's offsets
        self.assertEqual([int(ip.tiled_offsets(256, 256, 256, 1)[y, x]) for x, y in ((1, 0), (0, 1), (8, 0))], [2, 16, 64])

    def test_every_field_value(self):
        # all 32 values of each field through the rule (v * 255 + 15) // 31, both alpha bits
        src = bytearray(2 * 32 * 32 * 2)
        for y in range(32):
            for x in range(32):
                word = ((y & 1) << 15) | (x << 10) | (((x * 7) & 31) << 5) | ((x * 11) & 31)
                o = texture_thumb.tiled_offset_2d(x, y, 32, 1)
                put(src, o, word)
        out = ip.decode_1555(bytes(src), 32, 32, 32, True, 1, 0)
        for y in (0, 1, 31):
            for x in range(32):
                want = [((x * 11) & 31) * 255 + 15, ((x * 7) & 31) * 255 + 15, x * 255 + 15]
                self.assertEqual([int(v) for v in out[y, x]], [v // 31 for v in want] + [255 * (y & 1)])


class Mean(unittest.TestCase):
    def test_the_mean_of_a_made_pair(self):
        a = bytearray(2 * 32 * 2)
        put(a, 0, 0x8000 | 31)                # one covered texel of (31, 0, 0)
        put(a, 2, 31 << 10)                   # blue, not covered: not counted
        b = bytearray(2 * 32 * 2)
        put(b, 0, 0x8000 | (31 << 5))         # one covered texel of (0, 31, 0)
        pictures = [ip.fields_1555(bytes(p), 2, 1, 32, False, 1) for p in (a, b)]
        mean, count = ip.mean(pictures)
        self.assertEqual(count, 2)
        self.assertEqual(tuple(mean), (0.5, 0.5, 0.0))
        # the low field as blue: the same texels read (0, 0, 31) and (0, 31, 0)
        self.assertEqual(tuple(ip.mean(pictures, low_channel=2)[0]), (0.0, 0.5, 0.5))
        # nothing covered: no mean
        self.assertEqual(ip.mean([ip.fields_1555(bytes(4), 2, 1, 32, False, 1)]), (None, 0))

    def test_the_swizzle_of_a_fetch_constant(self):
        self.assertEqual(ip.swizzle(FC), (2, 1, 0, 3))                       # the shader's red is channel 2
        self.assertEqual(ip.swizzled((0.25, 0.5, 0.75), (2, 1, 0, 3)), (0.75, 0.5, 0.25))
        self.assertEqual(ip.swizzled((0.25, 0.5, 0.75), (0, 1, 4, 5)), (0.25, 0.5, 0.0))   # 4 is zero, 5 is one
        self.assertEqual(ip.swizzled((0.25, 0.5, 0.75), (5, 5, 5, 3)), (1.0, 1.0, 1.0))
        f = ip.fetch(FC)
        self.assertEqual((f["width"], f["height"], f["pitch"], f["tiled"], f["endian"], f["base"], f["mip"], f["mip_max"]),
                         (256, 256, 256, True, 1, 0x12704000, 0x12724000, 3))
        with self.assertRaises(ValueError):
            ip.fetch([FC[0], (FC[1] & ~0x3F) | 4] + FC[2:])                  # another format


class Capture(unittest.TestCase):
    """A made capture: one picture whole with a level file, one cut at half (a capture from before the format had a
    dump layout), the rows of a 129F draw."""

    def make(self, d):
        d = Path(d)
        (d / "native_tex_x").mkdir()
        base = bytearray(131072)
        put(base, 0, 0x8000 | 31)
        put(base, 130942, 0x8000 | (31 << 10))
        (d / "native_tex_x" / "12704000_AA.bin").write_bytes(base)
        mips = bytearray(32768 + 8192 + 4096)
        put(mips, 2, 0x8000 | (31 << 5))                                      # level 1, texel (1, 0)
        put(mips, 32768 + 16, 0x8000 | 31)                                    # level 2, texel (0, 1)
        put(mips, 32768 + 8192 + texture_thumb.tiled_offset_2d(31, 31, 32, 1), 0x8000 | (31 << 10))   # level 3, (31, 31)
        (d / "native_tex_x" / "12704000_AA_BB_mips.bin").write_bytes(mips)
        (d / "native_tex_x" / "1272F000_CC.bin").write_bytes(bytes(base[:65536]))
        fc = ["0x%08X" % v for v in FC]
        fc2 = list(fc)
        fc2[1] = "0x1272F043"
        rows = [
            {"kind": "texture", "file": "native_tex_x/12704000_AA.bin", "bytes": 131072, "frame": 1, "content": "0x00000000000000BB",
             "mips": {"file": "native_tex_x/12704000_AA_BB_mips.bin", "phys": "0x12724000", "bytes": len(mips), "tiled": True,
                      "levels": [{"level": 1, "offset": 0, "pitch": 256, "w": 128, "h": 128, "x": 0, "y": 0},
                                 {"level": 2, "offset": 32768, "pitch": 128, "w": 64, "h": 64, "x": 0, "y": 0},
                                 {"level": 3, "offset": 40960, "pitch": 64, "w": 32, "h": 32, "x": 0, "y": 0}]},
             "fc": fc},
            {"kind": "texture", "file": "native_tex_x/1272F000_CC.bin", "bytes": 65536, "fc": fc2},
            {"kind": "texture", "file": "native_tex_x/19720000_DD.bin", "bytes": 16, "fc": ["0x90000002", "0x19720052", "0x007FE7FF", "0x00A80D10", "0x000002C3", "0x19820A18"]},
            {"kind": "verts", "frame": 1, "in_scene": True, "n": 1, "vs": {"hash": "0x129F7F2793DA9084"}, "ps_hash": "0x4B1D3E6E8E58D3AC",
             "tf": {"0": fc, "16": fc}, "tfc": {"0": "0x00000000000000BB"}},
            {"kind": "verts", "frame": 1, "in_scene": True, "n": 2, "vs": {"hash": "0x129F7F2793DA9084"}, "ps_hash": "0x4B1D3E6E8E58D3AC",
             "tf": {"0": fc2}},
            {"kind": "verts", "frame": 2, "in_scene": True, "n": 3, "vs": {"hash": "0x129F7F2793DA9084"}, "ps_hash": "0x4B1D3E6E8E58D3AC",
             "tf": {"0": fc}, "tfc": {"0": "0x00000000000000EE"}},
            {"kind": "verts", "frame": 1, "in_scene": False, "vs": {"hash": "0x129F7F2793DA9084"}, "tf": {"0": fc}},
            {"kind": "verts", "frame": 1, "in_scene": True, "n": 4, "vs": {"hash": "0x0000000000000001"}, "tf": {"0": fc}},
        ]
        (d / "c.jsonl").write_text("\n".join(json.dumps(r) for r in rows) + "\n")
        return d, rows

    def test_picture_reads_a_base_and_its_levels(self):
        with tempfile.TemporaryDirectory() as tmp:
            d, rows = self.make(tmp)
            base = ip.picture(d, rows[0], 0)
            self.assertEqual(base.shape, (256, 256, 4))
            self.assertEqual([int(v) for v in base[0, 0]], [255, 0, 0, 255])
            self.assertEqual([int(v) for v in base[255, 255]], [0, 0, 255, 255])
            l1, l2, l3 = (ip.picture(d, rows[0], n) for n in (1, 2, 3))
            self.assertEqual((l1.shape, l2.shape, l3.shape), ((128, 128, 4), (64, 64, 4), (32, 32, 4)))
            self.assertEqual([int(v) for v in l1[0, 1]], [0, 255, 0, 255])
            self.assertEqual([int(v) for v in l2[1, 0]], [255, 0, 0, 255])
            self.assertEqual([int(v) for v in l3[31, 31]], [0, 0, 255, 255])
            self.assertEqual(int(l1.astype(np.int64).sum()) + int(l2.astype(np.int64).sum()) + int(l3.astype(np.int64).sum()), 3 * 510)
            with self.assertRaises(ValueError):
                ip.picture(d, rows[0], 4)                                     # no such level in the row
            with self.assertRaises(ValueError):
                ip.picture(d, rows[1], 1)                                     # a row without a level file
            with self.assertRaises(ValueError):
                ip.picture(d, rows[1], 0)                                     # half a picture is no picture
            half, have = ip.picture(d, rows[1], 0, partial=True)              # unless asked for: what is there, and where
            self.assertEqual([int(v) for v in half[0, 0]], [255, 0, 0, 255])
            self.assertTrue(bool(have[0, 0]) and not bool(have[255, 255]))
            self.assertAlmostEqual(float(have.mean()), 0.5, places=6)
            with self.assertRaises(ValueError):
                ip.picture(d, rows[2], 0)                                     # not format 3

    def test_the_pictures_of_a_frame(self):
        with tempfile.TemporaryDirectory() as tmp:
            d, rows = self.make(tmp)
            self.assertEqual([r["file"] for r in ip.format3_rows(rows)], ["native_tex_x/12704000_AA.bin", "native_tex_x/1272F000_CC.bin"])
            got = ip.frame_pictures(rows, 1)
            # the first by its content, the second (a capture without contents for this format) by its address
            self.assertEqual([(n, t["file"] if t else None) for n, t in got],
                             [(1, "native_tex_x/12704000_AA.bin"), (2, "native_tex_x/1272F000_CC.bin")])
            # frame 2's draw names a content no texture row has: no picture, said so
            self.assertEqual(ip.frame_pictures(rows, 2), [(3, None)])
            self.assertEqual(ip.frame_pictures(rows, 9), [])

    def test_cli_mean_sheet_and_compare(self):
        with tempfile.TemporaryDirectory() as tmp:
            d, rows = self.make(tmp)
            # frame 1 has a picture that is not whole: the mean refuses the frame
            self.assertEqual(ip.main(["mean", str(d / "c.jsonl"), "1"]), 1)
            whole = [r for r in rows if not (r.get("kind") == "verts" and r.get("n") == 2)]
            (d / "w.jsonl").write_text("\n".join(json.dumps(r) for r in whole) + "\n")
            self.assertEqual(ip.main(["mean", str(d / "w.jsonl"), "1"]), 0)
            self.assertEqual(ip.main(["mean", str(d / "w.jsonl"), "2"]), 1)   # a draw without its picture
            out = d / "sheet.png"
            self.assertEqual(ip.main(["sheet", str(d / "c.jsonl"), "--out", str(out)]), 0)
            self.assertEqual(out.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")
            # compare: the C++ tool mode's file against this decoder
            bin_path = d / "native_tex_x" / "12704000_AA.bin"
            fcs = ",".join("%08X" % v for v in FC)
            self.assertEqual(ip.main(["compare", str(bin_path), fcs]), 2)     # no .cpp.rgba yet
            rgba = ip.decode_1555(bin_path.read_bytes(), 256, 256, 256, True, 1)
            Path(str(bin_path) + ".cpp.rgba").write_bytes(rgba.tobytes())
            self.assertEqual(ip.main(["compare", str(bin_path), fcs]), 0)
            wrong = bytearray(rgba.tobytes())
            wrong[5] ^= 1
            Path(str(bin_path) + ".cpp.rgba").write_bytes(wrong)
            self.assertEqual(ip.main(["compare", str(bin_path), fcs]), 1)

    def test_real_dump_matches_cpp(self):
        """A dumped base level of an impostor's picture through this decoder and through the C++ one. Needs
        FABLE2_1555_BIN and FABLE2_1555_FC (see tests/native/test_texture_decode.cpp) and the C++ test's output
        FABLE2_1555_BIN + '.cpp.rgba'; SKIPS (never passes silently) when any is missing."""
        bin_path, fcs = os.environ.get("FABLE2_1555_BIN"), os.environ.get("FABLE2_1555_FC")
        if not bin_path or not fcs:
            self.skipTest("no format-3 dump: set FABLE2_1555_BIN and FABLE2_1555_FC after a discovery capture")
        if not Path(bin_path + ".cpp.rgba").exists():
            self.skipTest("run tests/run_native_tests.cmd with the same variables first (writes " + bin_path + ".cpp.rgba)")
        self.assertEqual(ip.main(["compare", bin_path, fcs]), 0)


class FlatColour(unittest.TestCase):
    """The literal of src/native/render/clay_logic.h (plan Task 7's last step): the mean of capture D1's covered
    texels as the fetch gives it to the shader."""

    D1 = ROOT / "out" / "native-evidence" / "distant-trees" / "native_discovery_20261011_155547.jsonl"

    def literal(self):
        import re
        text = (ROOT / "src" / "native" / "render" / "clay_logic.h").read_text(encoding="utf-8")
        m = re.search(r"kImpostorFlatColour\[3\] = \{([0-9.]+)f, ([0-9.]+)f, ([0-9.]+)f\};", text)
        self.assertIsNotNone(m, "no kImpostorFlatColour literal")
        s = re.search(r"kImpostorFlatSwizzle = 0x([0-9A-Fa-f]+)u;", text)
        self.assertIsNotNone(s, "no kImpostorFlatSwizzle literal")
        return tuple(float(x) for x in m.groups()), int(s.group(1), 16)

    def test_the_literal_is_the_swizzled_triple(self):
        colour, field = self.literal()
        self.assertEqual(colour, (0.145376, 0.250169, 0.050493))
        self.assertEqual(field, (FC[3] >> 1) & 0xFFF)
        self.assertEqual(ip.swizzle(FC), (2, 1, 0, 3))
        self.assertEqual(ip.swizzled((0.050493, 0.250169, 0.145376), ip.swizzle(FC)), colour)

    def test_the_literal_is_the_capture_s_mean(self):
        if not self.D1.is_file():
            self.skipTest("capture D1 is not in out/native-evidence/distant-trees")
        rows = ip.read_rows(self.D1)
        draws = ip.frame_pictures(rows, 1)
        self.assertEqual(len(draws), 14)
        pictures = [ip._level_fields(self.D1.parent, t, 0)[0] for _, t in draws]
        m, count = ip.mean(pictures)
        self.assertEqual(count, 172125)
        colour, _ = self.literal()
        for got, want in zip(ip.swizzled(m, ip.swizzle(draws[0][1]["fc"])), colour):
            self.assertAlmostEqual(got, want, delta=5e-7)


def made_base():
    """An 8 x 8 base of fields (field 0 alone carries numbers; fields 1 and 2 are field 0 + 1 and 31 - field 0).
    Its 2 x 2 blocks, as (alpha of the four parents; field 0 of the four), parents in the order (0,0) (1,0) (0,1) (1,1):
      A (0,0): 1 1 0 0;  10 20 30  4      B (1,0): 1 1 0 0;   1  2  2  2
      C (0,1): 1 1 0 0;   1  1  1  2      D (1,1): 1 0 0 0;   8  0  0  0
      E (2,0): 1 1 1 1;   1  1  2  2      F (3,0): 1 1 1 0;  31 31 31  0
      the lower half (rows 4 to 7): alpha 0, field 0 = 0."""
    f = np.zeros((8, 8, 4), dtype=np.uint8)
    f[..., 1] = 1
    f[..., 2] = 31
    blocks = {(0, 0): ((1, 1, 0, 0), (10, 20, 30, 4)), (1, 0): ((1, 1, 0, 0), (1, 2, 2, 2)), (0, 1): ((1, 1, 0, 0), (1, 1, 1, 2)),
              (1, 1): ((1, 0, 0, 0), (8, 0, 0, 0)), (2, 0): ((1, 1, 1, 1), (1, 1, 2, 2)), (3, 0): ((1, 1, 1, 0), (31, 31, 31, 0))}
    for (bx, by), (alpha, value) in blocks.items():
        for k, (dx, dy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
            f[by * 2 + dy, bx * 2 + dx] = (value[k], min(31, value[k] + 1), 31 - value[k], alpha[k])
    return f


class Levels(unittest.TestCase):
    """make_levels, the Python twin of render::MakeImpostorLevels (plan Task 13), each rule of the fitted family on
    made_base() against numbers worked by hand."""

    def test_sizes_and_the_refusals(self):
        levels = ip.make_levels(made_base(), levels=3)
        self.assertEqual([l.shape for l in levels], [(4, 4, 4), (2, 2, 4), (1, 1, 4)])
        with self.assertRaises(ValueError):
            ip.make_levels(made_base(), source="sideways")
        with self.assertRaises(ValueError):
            ip.make_levels(made_base(), colour="none")
        with self.assertRaises(ValueError):
            ip.make_levels(made_base(), count=5)
        with self.assertRaises(ValueError):
            ip.make_levels(np.zeros((6, 8, 4), dtype=np.uint8))          # not a power of two square

    def test_the_colour_of_all_four_parents(self):
        l1 = ip.make_levels(made_base(), source="above", colour="all", count=2)[0]
        # A: (10 + 20 + 30 + 4) / 4 = 16; B: 7 / 4 = 1.75 -> 2; C: 5 / 4 = 1.25 -> 1; D: 8 / 4 = 2; E: 6 / 4 = 1.5 -> 2
        # (a half goes up); F: 93 / 4 = 23.25 -> 23
        self.assertEqual([int(l1[0, 0, 0]), int(l1[0, 1, 0]), int(l1[1, 0, 0]), int(l1[1, 1, 0]), int(l1[0, 2, 0]), int(l1[0, 3, 0])],
                         [16, 2, 1, 2, 2, 23])
        self.assertEqual(int(l1[0, 0, 1]), 17)       # field 1 of A: (11 + 21 + 31 + 5) / 4 = 17
        self.assertEqual(int(l1[0, 0, 2]), 15)       # field 2 of A: (21 + 11 + 1 + 27) / 4 = 15

    def test_the_colour_of_the_covered_parents_alone(self):
        l1 = ip.make_levels(made_base(), source="above", colour="covered", count=1)[0]
        # A: (10 + 20) / 2 = 15; B: 3 / 2 = 1.5 -> 2; C: 2 / 2 = 1; D: 8; E: 6 / 4 -> 2; F: 93 / 3 = 31
        self.assertEqual([int(l1[0, 0, 0]), int(l1[0, 1, 0]), int(l1[1, 0, 0]), int(l1[1, 1, 0]), int(l1[0, 2, 0]), int(l1[0, 3, 0])],
                         [15, 2, 1, 8, 2, 31])
        self.assertEqual([int(v) for v in l1[3, 3]], [0, 0, 0, 0])        # no covered parent: zero

    def test_the_alpha_at_each_count(self):
        # covered parents: A 2, B 2, C 2, D 1, E 4, F 3, the lower half 0
        want = {1: [1, 1, 1, 1, 1, 1], 2: [1, 1, 1, 0, 1, 1], 3: [0, 0, 0, 0, 1, 1], 4: [0, 0, 0, 0, 1, 0]}
        for count, alphas in want.items():
            l1 = ip.make_levels(made_base(), count=count)[0]
            got = [int(l1[0, 0, 3]), int(l1[0, 1, 3]), int(l1[1, 0, 3]), int(l1[1, 1, 3]), int(l1[0, 2, 3]), int(l1[0, 3, 3])]
            self.assertEqual(got, alphas, count)
            self.assertEqual(int(l1[2:, :, 3].sum()), 0)

    def test_a_level_from_the_level_above_or_from_the_base(self):
        # level 2's texel (0, 0) covers A, B, C, D: seven covered base texels of sixteen. From the level above at
        # count 2: three of its four parents are covered -> covered. From the base: 7 of 16 is below 2 of 4 -> not.
        above = ip.make_levels(made_base(), source="above", colour="all", count=2)
        base = ip.make_levels(made_base(), source="base", colour="all", count=2)
        self.assertEqual((int(above[1][0, 0, 3]), int(base[1][0, 0, 3])), (1, 0))
        # its colour: from the level above the mean of 16, 2, 1, 2 = 5.25 -> 5; from the base 84 / 16 = 5.25 -> 5;
        # texel (1, 0) covers E, F and two empty blocks: above (2 + 23 + 0 + 0) / 4 = 6.25 -> 6, base 99 / 16 = 6.19 -> 6
        self.assertEqual((int(above[1][0, 0, 0]), int(base[1][0, 0, 0])), (5, 5))
        self.assertEqual((int(above[1][0, 1, 0]), int(base[1][0, 1, 0])), (6, 6))
        # E and F: 7 covered of 16 from the base (not covered at 2); above: both covered of four parents -> covered
        self.assertEqual((int(above[1][0, 1, 3]), int(base[1][0, 1, 3])), (1, 0))
        # level 3, the one texel: above 2 of 4 parents covered -> covered; base 14 of 64 -> not, nor at count 1 (below a quarter)
        self.assertEqual((int(above[2][0, 0, 3]), int(base[2][0, 0, 3])), (1, 0))
        self.assertEqual(int(ip.make_levels(made_base(), source="base", count=1)[2][0, 0, 3]), 0)
        self.assertEqual(int(above[1][1, 0, 3]) + int(above[1][1, 1, 3]), 0)

    def test_the_fitted_rule_is_the_default(self):
        fitted = ip.make_levels(made_base())
        named = ip.make_levels(made_base(), source=ip.LEVEL_RULE[0], colour=ip.LEVEL_RULE[1], count=ip.LEVEL_RULE[2])
        for a, b in zip(fitted, named):
            self.assertTrue(np.array_equal(a, b))

    def test_the_native_constants_are_the_tool_s(self):
        import re
        text = (ROOT / "src" / "native" / "render" / "impostor_picture.h").read_text(encoding="utf-8")
        got = {}
        for name in ("kImpostorLevelAlphaCount", "kImpostorRingClearMin", "kImpostorCoverMin", "kImpostorCoverMax"):
            m = re.search(name + r" = (\d+)[;,]", text)
            self.assertIsNotNone(m, name)
            got[name] = int(m.group(1))
        self.assertEqual(got, {"kImpostorLevelAlphaCount": ip.LEVEL_RULE[2], "kImpostorRingClearMin": ip.RING_CLEAR_MIN,
                               "kImpostorCoverMin": ip.COVER_MIN, "kImpostorCoverMax": ip.COVER_MAX})


class Content(unittest.TestCase):
    """content_stats and picture_usable, the twins of render::MeasureImpostorPicture and ImpostorPictureUsable."""

    def test_the_counts_of_a_made_picture(self):
        f = np.zeros((256, 256, 4), dtype=np.uint8)
        self.assertEqual(ip.content_stats(f), (1020, 0))
        f[..., 3] = 1
        self.assertEqual(ip.content_stats(f), (0, 65536))
        f[0, 0, 3] = f[0, 255, 3] = f[255, 0, 3] = f[100, 255, 3] = f[100, 100, 3] = 0   # four on the ring, one inside
        self.assertEqual(ip.content_stats(f), (4, 65531))
        with self.assertRaises(ValueError):
            ip.content_stats(np.zeros((128, 128, 4), dtype=np.uint8))

    def test_usable(self):
        self.assertFalse(ip.picture_usable(1020, 0))                                   # empty
        self.assertFalse(ip.picture_usable(0, 65536))                                  # covered to its edge
        self.assertTrue(ip.picture_usable(1020, (ip.COVER_MIN + ip.COVER_MAX) // 2))
        self.assertFalse(ip.picture_usable(1020, ip.COVER_MAX + 1))
        self.assertFalse(ip.picture_usable(1020, ip.COVER_MIN - 1))
        self.assertFalse(ip.picture_usable(ip.RING_CLEAR_MIN - 1, ip.COVER_MIN))
        self.assertTrue(ip.picture_usable(ip.RING_CLEAR_MIN, ip.COVER_MIN))
        self.assertTrue(ip.picture_usable(ip.RING_CLEAR_MIN, ip.COVER_MAX))


class RealLevels(unittest.TestCase):
    """render::MakeImpostorLevels and MeasureImpostorPicture against the Python twins on a real dumped base."""

    def test_real_dump_levels_match_cpp(self):
        """FABLE2_1555_BIN and FABLE2_1555_FC as the decoder's real-dump test, and the output of
        tests/native/test_impostor_picture.cpp's tool mode (<bin>.cpp.levels, <bin>.cpp.stats); SKIPS (never passes
        silently) when any is missing."""
        bin_path, fcs = os.environ.get("FABLE2_1555_BIN"), os.environ.get("FABLE2_1555_FC")
        if not bin_path or not fcs or not Path(bin_path + ".cpp.levels").is_file() or not Path(bin_path + ".cpp.stats").is_file():
            self.skipTest("no made levels: set FABLE2_1555_BIN and FABLE2_1555_FC and run the two native tests first")
        f = ip.fetch([int(v, 16) for v in fcs.split(",")])
        fields = ip.fields_1555(Path(bin_path).read_bytes(), f["width"], f["height"], f["pitch"], f["tiled"], f["endian"])
        mine = b"".join(ip._rgba(level, ip.LOW_FIELD_CHANNEL).tobytes() for level in ip.make_levels(fields))
        self.assertEqual(len(mine), (128 * 128 + 64 * 64 + 32 * 32) * 4)
        self.assertEqual(mine, Path(bin_path + ".cpp.levels").read_bytes())
        ring, covered = ip.content_stats(fields)
        self.assertEqual(Path(bin_path + ".cpp.stats").read_text().split(), [str(ring), str(covered), "1" if ip.picture_usable(ring, covered) else "0"])


class Used(unittest.TestCase):
    """`used` (plan Task 14, criterion 6): the pictures the pass wrote beside a scene dump (native_scene_<stamp>.f2np)
    against the paired capture's dumps of the same bases."""

    def make(self, d, change=None, base2=0x1272F000):
        d = Path(d)
        (d / "native_tex_x").mkdir()
        one = bytearray(131072)
        put(one, 0, 0x8000 | 31)
        put(one, 130942, 0x8000 | (31 << 10))
        two = bytearray(one)
        put(two, 2, 0x8000 | (31 << 5))            # frame 2's picture of the same base: one more texel
        (d / "native_tex_x" / "12704000_AA.bin").write_bytes(one)
        (d / "native_tex_x" / "12704000_AA_EE.bin").write_bytes(two)
        fc = ["0x%08X" % v for v in FC]
        rows = [
            {"kind": "texture", "file": "native_tex_x/12704000_AA.bin", "bytes": 131072, "frame": 1, "content": "0x00000000000000BB", "fc": fc},
            {"kind": "texture", "file": "native_tex_x/12704000_AA_EE.bin", "bytes": 131072, "frame": 2, "content": "0x00000000000000EE", "fc": fc},
            {"kind": "verts", "frame": 1, "in_scene": True, "n": 1, "vs": {"hash": "0x129F7F2793DA9084"}, "tf": {"0": fc}, "tfc": {"0": "0x00000000000000BB"}},
            {"kind": "frame", "frame": 1, "scene_frame": 413},
            {"kind": "verts", "frame": 2, "in_scene": True, "n": 2, "vs": {"hash": "0x129F7F2793DA9084"}, "tf": {"0": fc}, "tfc": {"0": "0x00000000000000EE"}},
            {"kind": "frame", "frame": 2, "scene_frame": 414},
        ]
        (d / "c.jsonl").write_text("\n".join(json.dumps(r) for r in rows) + "\n")
        rgba = bytearray(ip.decode_1555(bytes(one), 256, 256, 256, True, 1).tobytes())
        if change:
            change(rgba)
        import struct
        body = struct.pack("<IIQII", 0x504E3246, 1, 413, 2, 0)
        body += struct.pack("<IIQ", 0x12704000, 422, 0x1122334455667788) + bytes(rgba)
        body += struct.pack("<IIQ", base2, 423, 7) + bytes(rgba)
        (d / "p.f2np").write_bytes(body)
        return d

    def test_parse(self):
        with tempfile.TemporaryDirectory() as d:
            d = self.make(d)
            head, pictures = ip.read_used(d / "p.f2np")
            self.assertEqual((head["scene_frame"], head["count"]), (413, 2))
            self.assertEqual([(p["base"], p["place"], p["hash"]) for p in pictures], [(0x12704000, 422, 0x1122334455667788), (0x1272F000, 423, 7)])
            self.assertEqual(pictures[0]["rgba"].shape, (256, 256, 4))
            data = (d / "p.f2np").read_bytes()
            for bad in (data[:-1], data + b"\0", b"F2NS" + data[4:], data[:16] + b"\x21\0\0\0" + data[20:]):
                (d / "bad.f2np").write_bytes(bad)
                with self.assertRaises(ValueError):
                    ip.read_used(d / "bad.f2np")

    def test_equal_one_texel_off_and_a_missing_base(self):
        with tempfile.TemporaryDirectory() as d:
            d = self.make(d)
            result = ip.used(d / "p.f2np", d / "c.jsonl")
            self.assertEqual(result["paired_frame"], 1)
            first, second = result["pictures"]
            # the picture is frame 1's dump texel for texel, and frame 2's but for one texel
            self.assertEqual(first["equal"], {1: 1.0, 2: 65535 / 65536})
            self.assertEqual(first["best_frame"], 1)
            self.assertEqual(second["equal"], {})               # the capture has no dump of the second base
            self.assertIsNone(second["best_frame"])
            self.assertEqual(ip.main(["used", str(d / "p.f2np"), str(d / "c.jsonl")]), 1)      # a picture without a dump: not held
        with tempfile.TemporaryDirectory() as d:
            def one_off(rgba):
                rgba[4 * (5 * 256 + 9) + 1] ^= 0xFF
            d = self.make(d, change=one_off, base2=0x12704000)
            result = ip.used(d / "p.f2np", d / "c.jsonl")
            self.assertEqual(result["pictures"][0]["equal"], {1: 65535 / 65536, 2: 65534 / 65536})
            self.assertEqual(ip.main(["used", str(d / "p.f2np"), str(d / "c.jsonl")]), 0)      # both within the bound of 98%

    def test_a_scene_frame_the_capture_does_not_have(self):
        with tempfile.TemporaryDirectory() as d:
            d = self.make(d)
            data = bytearray((d / "p.f2np").read_bytes())
            data[8:16] = (999).to_bytes(8, "little")
            (d / "p.f2np").write_bytes(bytes(data))
            result = ip.used(d / "p.f2np", d / "c.jsonl")
            self.assertIsNone(result["paired_frame"])
            self.assertEqual(result["pictures"][0]["equal"], {1: 1.0, 2: 65535 / 65536})


class Feed(unittest.TestCase):
    """`feed`: a capture's dumped bases as the file of the pass's diagnostic feed (render::ParseImpostorFeed)."""

    def test_the_file_of_a_made_capture(self):
        import struct
        with tempfile.TemporaryDirectory() as d:
            d = Used().make(d)
            self.assertEqual(ip.main(["feed", str(d / "c.jsonl"), str(d / "out.f2pf")]), 0)
            data = (d / "out.f2pf").read_bytes()
            self.assertEqual(struct.unpack_from("<IIII", data, 0), (0x46503246, 1, 2, 1))        # "F2PF", two sets, one picture
            self.assertEqual(len(data), 16 + 2 * (8 + 131072))
            self.assertEqual(struct.unpack_from("<II", data, 16), (0x12704000, 0))
            self.assertEqual(data[24:24 + 131072], (d / "native_tex_x" / "12704000_AA.bin").read_bytes())
            second = 16 + 8 + 131072
            self.assertEqual(struct.unpack_from("<II", data, second), (0x12704000, 0))
            self.assertEqual(data[second + 8:], (d / "native_tex_x" / "12704000_AA_EE.bin").read_bytes())
            self.assertEqual(ip.main(["feed", str(d / "c.jsonl"), str(d / "one.f2pf"), "--frames", "2"]), 0)
            self.assertEqual(struct.unpack_from("<IIII", (d / "one.f2pf").read_bytes(), 0), (0x46503246, 1, 1, 1))
            self.assertEqual(ip.main(["feed", str(d / "c.jsonl"), str(d / "none.f2pf"), "--frames", "7"]), 1)   # no such frame: no file
            self.assertFalse((d / "none.f2pf").exists())


if __name__ == "__main__":
    unittest.main()
