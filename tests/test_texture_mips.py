"""The stored mip levels of a discovery capture's texture row (ledger ruling 54): the reader
(tools/xdk_sigmatch/texture_thumb.py, decode_mip_levels) and the capture's check
(tools/xdk_sigmatch/check_discovery.py, check_mip_files), on made files.

The capture writes the guest bytes at a fetch constant's mip address whole, and a "mips" member that names each
level's place (src/native/capture/discovery_format.h, FormatMipsMember; its text is held by
tests/native/test_discovery_format.cpp). Here a made DXT1 chain is laid out linear and tiled, with a packed tail,
and read back texel for texel. What this cannot see: whether the capture's layout is the console's (it is the
SDK's GetGuestTextureLayout, the one the native texture cache uploads from); the first real capture is checked by
laying each stored level beside the base level's own halving."""
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import check_discovery  # noqa: E402
import texture_thumb as tt  # noqa: E402

FC = [0x82000002, 0x14030052, 0x001FE0FF, 0x00A80D10, 0x00000103, 0x14038A18]   # the fern's: DXT1, 256 x 256, endian 1


def dxt1_block(opaque_mask):
    """A DXT1 block (as the guest stores it under endian 1: 16-bit words swapped) whose texel i is opaque white where
    bit i of the mask is set and transparent black elsewhere (c0 <= c1: the three-colour form with a hole)."""
    idx = 0
    for i in range(16):
        idx |= (1 if (opaque_mask >> i) & 1 else 3) << (2 * i)
    little = struct.pack("<HHI", 0x0000, 0xFFFF, idx)
    return tt._swap(little, 1)


def make_level(w, h, pitch, tiled, x0, y0, offset, pattern, data):
    wb, hb = (w + 3) // 4, (h + 3) // 4
    for by in range(hb):
        for bx in range(wb):
            o = offset + (tt.tiled_offset_2d(x0 + bx, y0 + by, pitch >> 3, 3) if tiled else (y0 + by) * pitch + (x0 + bx) * 8)
            if len(data) < o + 8:
                data.extend(bytes(o + 8 - len(data)))
            data[o:o + 8] = dxt1_block(pattern(bx, by))


def alpha_of(rgba, w, x, y):
    return rgba[(y * w + x) * 4 + 3]


class DecodeMipLevels(unittest.TestCase):
    def roundtrip(self, tiled):
        data = bytearray()
        levels = [{"level": 1, "offset": 0, "pitch": 256, "w": 128, "h": 128, "x": 0, "y": 0},
                  {"level": 2, "offset": 32768, "pitch": 256, "w": 64, "h": 64, "x": 0, "y": 0},
                  # two levels of a packed tail: one storage level, each at its own block offset
                  {"level": 5, "offset": 49152, "pitch": 256, "w": 8, "h": 8, "x": 4, "y": 0},
                  {"level": 6, "offset": 49152, "pitch": 256, "w": 4, "h": 4, "x": 2, "y": 0}]
        patterns = {1: lambda bx, by: 0xFFFF if (bx + by) % 2 else 0x0000,
                    2: lambda bx, by: 0x00FF if bx % 2 else 0xFF00,
                    5: lambda bx, by: 0x0001 << (bx + 2 * by),
                    6: lambda bx, by: 0x8421}
        for lv in levels:
            make_level(lv["w"], lv["h"], lv["pitch"], tiled, lv["x"], lv["y"], lv["offset"], patterns[lv["level"]], data)
        got = tt.decode_mip_levels(bytes(data), FC, {"tiled": tiled, "levels": levels})
        self.assertEqual([(n, w, h) for n, w, h, _ in got], [(1, 128, 128), (2, 64, 64), (5, 8, 8), (6, 4, 4)])
        by_level = {n: (w, rgba) for n, w, h, rgba in got}
        w, rgba = by_level[1]
        self.assertEqual(alpha_of(rgba, w, 4, 0), 255)       # block (1, 0)
        self.assertEqual(alpha_of(rgba, w, 0, 0), 0)         # block (0, 0)
        self.assertEqual(alpha_of(rgba, w, 127, 124), 0)     # block (31, 31)
        self.assertEqual(alpha_of(rgba, w, 123, 124), 255)   # block (30, 31)
        w, rgba = by_level[2]
        self.assertEqual((alpha_of(rgba, w, 0, 0), alpha_of(rgba, w, 0, 3)), (0, 255))       # block (0, 0): rows 2 and 3
        self.assertEqual((alpha_of(rgba, w, 4, 0), alpha_of(rgba, w, 4, 3)), (255, 0))       # block (1, 0): rows 0 and 1
        w, rgba = by_level[5]
        self.assertEqual([alpha_of(rgba, w, x, 0) for x in range(8)], [255, 0, 0, 0, 0, 255, 0, 0])   # blocks (0,0) bit 0, (1,0) bit 1
        self.assertEqual(alpha_of(rgba, w, 2, 4), 255)                                               # block (0, 1): bit 2
        w, rgba = by_level[6]
        self.assertEqual([alpha_of(rgba, w, i, i) for i in range(4)], [255, 255, 255, 255])           # the diagonal 0x8421
        self.assertEqual(alpha_of(rgba, w, 1, 0), 0)

    def test_linear(self):
        self.roundtrip(False)

    def test_tiled(self):
        self.roundtrip(True)

    def test_the_base_level_reads_as_before(self):
        # decode_rgba was taken apart for the levels' reader: the base level of a tiled DXT1 texture is what it was
        data = bytearray()
        make_level(256, 256, 512, True, 0, 0, 0, lambda bx, by: 0xFFFF if (bx * 3 + by) % 5 == 0 else 0x0F0F, data)
        w, h, rgba = tt.decode_rgba(bytes(data), FC)
        self.assertEqual((w, h, len(rgba)), (256, 256, 256 * 256 * 4))
        self.assertEqual(alpha_of(rgba, w, 0, 0), 255)
        self.assertEqual([alpha_of(rgba, w, 4 + i, 0) for i in range(4)], [255, 255, 255, 255])   # block (1, 0): mask 0x0F0F, row 0
        self.assertEqual([alpha_of(rgba, w, 4 + i, 1) for i in range(4)], [0, 0, 0, 0])

    def test_a_level_outside_its_file_is_refused(self):
        with self.assertRaises(ValueError):
            tt.decode_mip_levels(bytes(64), FC, {"tiled": False, "levels": [{"level": 1, "offset": 64, "pitch": 256, "w": 128, "h": 128}]})
        with self.assertRaises(ValueError):
            tt.decode_mip_levels(bytes(64), FC, {"tiled": False, "levels": [{"level": 1, "offset": 0, "pitch": 4, "w": 128, "h": 128}]})


class CheckMipFiles(unittest.TestCase):
    def rows(self, folder, bytes_in_row=4096, levels=None, write=4096):
        name = "native_tex_x/14030000_0000000000000001_mips.bin"
        os.makedirs(os.path.join(folder, "native_tex_x"), exist_ok=True)
        if write is not None:
            with open(os.path.join(folder, name.replace("/", os.sep)), "wb") as f:
                f.write(bytes(write))
        if levels is None:
            levels = [{"level": 1, "offset": 0, "pitch": 256, "w": 128, "h": 128, "x": 0, "y": 0},
                      {"level": 2, "offset": 2048, "pitch": 256, "w": 64, "h": 64, "x": 0, "y": 0}]
        return [{"kind": "texture", "file": "native_tex_x/14030000_0000000000000001.bin", "bytes": 32768,
                 "mips": {"file": name, "phys": "0x14038000", "bytes": bytes_in_row, "tiled": True, "levels": levels},
                 "fc": ["0x%08X" % v for v in FC]},
                {"kind": "texture", "file": "native_tex_x/other.bin", "bytes": 64, "fc": ["0x%08X" % v for v in FC]}]

    def test_a_whole_dump_passes(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(check_discovery.check_mip_files(self.rows(d), d), (1, []))

    def test_what_is_wrong_is_named(self):
        with tempfile.TemporaryDirectory() as d:
            named, problems = check_discovery.check_mip_files(self.rows(d, write=None), d)
            self.assertEqual((named, len(problems)), (1, 1))
            self.assertIn("missing", problems[0])
        with tempfile.TemporaryDirectory() as d:
            self.assertIn("bytes", check_discovery.check_mip_files(self.rows(d, bytes_in_row=8192), d)[1][0])
        with tempfile.TemporaryDirectory() as d:
            wrong_size = [{"level": 1, "offset": 0, "pitch": 256, "w": 64, "h": 128}]
            problems = check_discovery.check_mip_files(self.rows(d, levels=wrong_size), d)[1]
            self.assertEqual(len(problems), 1)
            self.assertIn("level 1 is 64 x 128 of a 256 x 256 texture", problems[0])
        with tempfile.TemporaryDirectory() as d:
            order = [{"level": 2, "offset": 0, "pitch": 256, "w": 64, "h": 64}, {"level": 1, "offset": 0, "pitch": 256, "w": 128, "h": 128}]
            self.assertIn("after", check_discovery.check_mip_files(self.rows(d, levels=order), d)[1][0])
        with tempfile.TemporaryDirectory() as d:
            outside = [{"level": 1, "offset": 4096, "pitch": 256, "w": 128, "h": 128}]
            self.assertIn("outside", check_discovery.check_mip_files(self.rows(d, levels=outside), d)[1][0])
        with tempfile.TemporaryDirectory() as d:
            self.assertIn("without a level", check_discovery.check_mip_files(self.rows(d, levels=[]), d)[1][0])

    def test_the_capture_writes_the_member_the_reader_reads(self):
        # the member's keys in the C++ formatter's text are the ones read here
        text = (ROOT / "src" / "native" / "capture" / "discovery_format.h").read_text(encoding="utf-8")
        for key in ("mips", "file", "phys", "bytes", "tiled", "levels", "level", "offset", "pitch", "w", "h", "x", "y"):
            self.assertIn('\\"%s\\":' % key, text)
        capture = (ROOT / "src" / "native" / "capture" / "capture.cpp").read_text(encoding="utf-8")
        self.assertEqual(capture.count("DumpTextureMips("), 2)            # its definition, and DumpTexture's one call
        self.assertIn("    if (!is_1d && err == render::FetchError::kNone) row += DumpTextureMips(t, base, identity);", capture)
        self.assertIn("  if (g_tex_dir.empty() || !t.mip_phys || t.format == render::TexFormat::kDxt3a) return {};", capture)
        json.dumps(self.rows(tempfile.gettempdir(), write=None))


if __name__ == "__main__":
    unittest.main()
