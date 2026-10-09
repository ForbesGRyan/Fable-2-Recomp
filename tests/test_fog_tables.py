import json
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import fog_tables as ft  # noqa: E402
import texture_thumb as tt  # noqa: E402

# The game's fetch constants of the two fog tables (native_discovery_20261009_065904.jsonl, main-scene rows).
TF4_FC = [0x00804956, 0x1FAB105D, 0x0000003F, 0x00280D11, 0x00000003, 0x00000018]
TF5_FC = [0x02004802, 0x1FAB0086, 0x000000FF, 0x00280C14, 0x00000003, 0x00000018]
# The scene colour the post chain reads (the oracle capture native_discovery_20261009_034735.jsonl).
SCENE_FC = [0x88C24956, 0x199E005D, 0x0059E45F, 0x01000D11, 0x00000000, 0x00000218]


def half_be(*values):
    return struct.pack(">%de" % len(values), *values)


class DecodeTablesTest(unittest.TestCase):
    def test_tf4_is_64_big_endian_half_float_texels(self):
        data = b"".join(half_be(i, 0.5, -2.0, 1.0 + i / 64.0) for i in range(64))
        t = ft.decode_tf4(data)
        self.assertEqual(len(t), 64)
        self.assertEqual(t[0], (0.0, 0.5, -2.0, 1.0))
        self.assertEqual(t[63], (63.0, 0.5, -2.0, 1.0 + 63 / 64.0))
        # Byte order by hand: 1.0 as a half float is 0x3C00, stored high byte first.
        one = bytes([0x3C, 0x00] * 4) + bytes(504)
        self.assertEqual(ft.decode_tf4(one)[0], (1.0, 1.0, 1.0, 1.0))
        self.assertEqual(ft.decode_tf4(one)[1], (0.0, 0.0, 0.0, 0.0))

    def test_tf4_keeps_values_above_one_and_non_finite_ones(self):
        data = half_be(12.5, float("inf"), 0.0, 1.0) + bytes(504)
        r, g, b, a = ft.decode_tf4(data)[0]
        self.assertEqual((r, b, a), (12.5, 0.0, 1.0))
        self.assertTrue(math.isinf(g))
        nan = bytes([0x7E, 0x00]) + bytes(510)
        self.assertTrue(math.isnan(ft.decode_tf4(nan)[0][0]))

    def test_tf4_needs_all_512_bytes_and_ignores_what_follows(self):
        with self.assertRaises(ValueError):
            ft.decode_tf4(bytes(511))
        self.assertEqual(len(ft.decode_tf4(bytes(4096))), 64)

    def test_tf5_is_256_texels_stored_a_r_g_b(self):
        # The fetch constant's swizzle 0x60A after the 8-in-32 swap: x from the second stored byte, y from the
        # third, z from the fourth, w from the first.
        data = b"".join(bytes([0x10, i, 255 - i, 0x40]) for i in range(256))
        t = ft.decode_tf5(data)
        self.assertEqual(len(t), 256)
        self.assertEqual(t[0], (0.0, 1.0, 0x40 / 255.0, 0x10 / 255.0))
        self.assertEqual(t[255], (1.0, 0.0, 0x40 / 255.0, 0x10 / 255.0))
        self.assertEqual(t[51][0], 51 / 255.0)
        with self.assertRaises(ValueError):
            ft.decode_tf5(bytes(1023))

    def test_the_fixed_decoders_are_the_fetch_constants_reading(self):
        # decode_1d reads endian, format and swizzle from the fetch constant; on the game's two constants it
        # must give what the fixed decoders give.
        tf4 = b"".join(half_be(i / 8.0, 2.0 * i, 0.25, 1.0) for i in range(64))
        self.assertEqual(ft.decode_1d(tf4, TF4_FC), ft.decode_tf4(tf4))
        tf5 = bytes((7 * i + 3) & 0xFF for i in range(1024))
        self.assertEqual(ft.decode_1d(tf5, TF5_FC), ft.decode_tf5(tf5))

    def test_decode_1d_formats_and_refusals(self):
        # k_32_FLOAT (36), 8-in-32: big-endian floats; a one-channel format repeats its channel under the
        # identity swizzle's missing channels (the swizzle here names channel 0 three times and constant one).
        fc = [0x04000002, 0x1FAA0000 | 36 | (2 << 6), 3, ((0 | 0 << 3 | 0 << 6 | 5 << 9) << 1), 0, 0]
        data = struct.pack(">4f", 0.25, 1.5, -3.0, 100.0)
        self.assertEqual(ft.decode_1d(data, fc), [(0.25, 0.25, 0.25, 1.0), (1.5, 1.5, 1.5, 1.0),
                                                  (-3.0, -3.0, -3.0, 1.0), (100.0, 100.0, 100.0, 1.0)])
        with self.assertRaises(ValueError):
            ft.decode_1d(data[:15], fc)           # short
        with self.assertRaises(ValueError):
            ft.decode_1d(data, SCENE_FC)          # not a 1D texture
        dxt = list(fc)
        dxt[1] = 0x1FAA0000 | 18
        with self.assertRaises(ValueError):
            ft.decode_1d(bytes(64), dxt)          # a format without a texel decode
        # Swizzle constants: 4 is zero, 5 is one.
        fc5 = list(TF5_FC)
        fc5[3] = (4 | 5 << 3 | 3 << 6 | 0 << 9) << 1
        self.assertEqual(ft.decode_1d(bytes([8, 9, 10, 11]) + bytes(1020), fc5)[0], (0.0, 1.0, 8 / 255.0, 11 / 255.0))

    def test_sample_1d_is_linear_between_texel_centres_and_clamped(self):
        table = [(0.0, 0.0, 0.0, 1.0), (1.0, 2.0, 4.0, 1.0)]
        self.assertEqual(ft.sample_1d(table, 0.25), (0.0, 0.0, 0.0, 1.0))
        self.assertEqual(ft.sample_1d(table, 0.75), (1.0, 2.0, 4.0, 1.0))
        self.assertEqual(ft.sample_1d(table, 0.5), (0.5, 1.0, 2.0, 1.0))
        self.assertEqual(ft.sample_1d(table, -3.0), table[0])
        self.assertEqual(ft.sample_1d(table, 0.0), table[0])
        self.assertEqual(ft.sample_1d(table, 1.0), table[1])
        self.assertEqual(ft.sample_1d(table, 7.0), table[1])
        with self.assertRaises(ValueError):
            ft.sample_1d(table, float("nan"))


class UntileSceneTest(unittest.TestCase):
    @staticmethod
    def tiled(w, h, texel):
        pitch = (w + 31) & ~31
        size = max(tt.tiled_offset_2d(x, y, pitch, 3) for y in range(h) for x in range(w)) + 8
        data = bytearray(size)
        for y in range(h):
            for x in range(w):
                o = tt.tiled_offset_2d(x, y, pitch, 3)
                data[o:o + 8] = half_be(*texel(x, y))
        return bytes(data)

    def test_layout_by_hand(self):
        # Eight-byte texels: (1, 0) lies 8 bytes in, (0, 1) at 16 and (2, 0) at 32 (rows are interleaved in
        # pairs), as capture::TiledOffset2D places them.
        data = bytearray(8192)
        data[0:8] = half_be(1.0, 0.0, 0.0, 1.0)
        data[8:16] = half_be(2.0, 0.0, 0.0, 1.0)
        data[16:24] = half_be(3.0, 0.0, 0.0, 1.0)
        data[32:40] = half_be(4.0, 0.0, 0.0, 1.0)
        px = ft.untile_scene(bytes(data), 32, 32)
        self.assertEqual(len(px), 32 * 32 * 4)
        self.assertEqual(ft.scene_pixel(px, 32, 0, 0), (1.0, 0.0, 0.0, 1.0))
        self.assertEqual(ft.scene_pixel(px, 32, 1, 0), (2.0, 0.0, 0.0, 1.0))
        self.assertEqual(ft.scene_pixel(px, 32, 0, 1), (3.0, 0.0, 0.0, 1.0))
        self.assertEqual(ft.scene_pixel(px, 32, 2, 0), (4.0, 0.0, 0.0, 1.0))
        self.assertEqual(ft.scene_pixel(px, 32, 3, 0), (0.0, 0.0, 0.0, 0.0))

    def test_every_texel_of_a_picture_wider_than_a_tile_and_not_a_multiple_of_it(self):
        w, h = 70, 37
        data = self.tiled(w, h, lambda x, y: (x, y, x * 0.25 + y, 1.0))
        px = ft.untile_scene(data, w, h)
        self.assertEqual(len(px), w * h * 4)
        for y in range(h):
            for x in range(w):
                self.assertEqual(ft.scene_pixel(px, w, x, y), (float(x), float(y), x * 0.25 + y, 1.0))

    def test_a_dump_that_misses_a_texel_is_refused(self):
        w, h = 70, 37
        data = self.tiled(w, h, lambda x, y: (1.0, 1.0, 1.0, 1.0))
        with self.assertRaises(ValueError):
            ft.untile_scene(data[:-1], w, h)
        with self.assertRaises(ValueError):
            ft.untile_scene(data, 0, h)

    def test_the_scene_size_needs_what_the_dump_writes(self):
        # 1120 x 720: 35 tiles a row; the last texel is in tile (34, 22). The capture's dump of the level is
        # (22 * 35 + 34) * 8192 + 8192 bytes (tests/native/test_texture_decode.cpp); the picture needs no more.
        self.assertLessEqual(ft.scene_bytes_needed(1120, 720), (22 * 35 + 34) * 8192 + 8192)
        self.assertGreater(ft.scene_bytes_needed(1120, 720), (22 * 35 + 34) * 8192)

    def test_png_tone(self):
        # scene_rgba8: value / scale, clamped, gamma 2.2, alpha opaque; a non-finite value is magenta.
        px = [16.0, 0.0, 4.0, 0.5, float("nan"), 1.0, 1.0, 1.0]
        out = ft.scene_rgba8(px, scale=16.0)
        self.assertEqual(out[0:4], bytes([255, 0, round(255 * 0.25 ** (1 / 2.2)), 255]))
        self.assertEqual(out[4:8], bytes([255, 0, 255, 255]))


class FitTest(unittest.TestCase):
    def test_an_exponential_table_is_recognised_with_its_coefficient(self):
        c67w = 0.0005
        k = (0.000305, 0.000394, 0.001071)
        table = []
        for i in range(256):
            d = (i + 0.5) / 256.0 / c67w
            table.append(tuple(round(255 * math.exp(-kk * d)) / 255.0 for kk in k) + (1.0,))
        fit = ft.fit_exponential(table, c67w)
        for ch in range(3):
            self.assertAlmostEqual(fit[ch]["k"], k[ch], delta=k[ch] * 0.02)
            self.assertLessEqual(fit[ch]["max_error"], 1.0 / 255.0)

    def test_a_table_that_is_not_an_exponential_shows_its_error(self):
        table = [(1.0 - i / 255.0,) * 3 + (1.0,) for i in range(256)]  # a straight line
        fit = ft.fit_exponential(table, 0.0005)
        self.assertGreater(fit[0]["max_error"], 0.05)

    def test_a_constant_table_has_no_coefficient(self):
        fit = ft.fit_exponential([(1.0, 1.0, 1.0, 1.0)] * 256, 0.0005)
        self.assertEqual(fit[0]["k"], 0.0)
        self.assertEqual(fit[0]["max_error"], 0.0)


class CaptureTest(unittest.TestCase):
    """load_capture on a made-up capture: texture rows kept by content, the rows that name them."""

    def make(self, d):
        d = Path(d)
        (d / "native_tex_x").mkdir()
        hx = lambda fc: ["0x%08X" % v for v in fc]
        rows = []
        pfull = ["00000000" * 4] * 256
        pfull[67] = struct.pack(">4f", 20.0, 1900.0, 1.0, 0.0005).hex().upper()
        for frame in (1, 2):
            tf4 = [TF4_FC[0], (TF4_FC[1] & 0xFFF) | (0x1FAB1000 if frame == 1 else 0x1FAB5000)] + TF4_FC[2:]
            name4 = "%08X_AAAA.bin" % (tf4[1] & 0xFFFFF000)
            (d / "native_tex_x" / name4).write_bytes(b"".join(half_be(frame, i, 0.0, 1.0) for i in range(64)))
            rows.append({"kind": "texture", "file": "native_tex_x/" + name4, "bytes": 512, "frame": frame,
                         "content": "0x%016X" % frame, "dim": 1, "fc": hx(tf4)})
            name5 = "1FAB0000_BBBB%s.bin" % ("" if frame == 1 else "_2")
            (d / "native_tex_x" / name5).write_bytes(bytes([255, 10 * frame, 20, 30]) * 256)
            rows.append({"kind": "texture", "file": "native_tex_x/" + name5, "bytes": 1024, "frame": frame,
                         "content": "0x%016X" % (16 + frame), "dim": 1, "fc": hx(TF5_FC)})
            rows.append({"kind": "draw", "frame": frame, "in_scene": True, "ps_hash": "0x8D90000000000000",
                         "tf": {"4": hx(tf4), "5": hx(TF5_FC), "0": None},
                         "tfc": {"4": "0x%016X" % frame, "5": "0x%016X" % (16 + frame)}, "pfull": "".join(pfull)})
            rows.append({"kind": "frame", "frame": frame})
        # A 2D texture row of an ordinary texture: not a table.
        rows.insert(0, {"kind": "texture", "file": "native_tex_x/none.bin", "bytes": 16,
                        "fc": hx([0x80800002, 0x1A000052, 0x0003E01F, 0, 0, 0x218])})
        cap = d / "native_discovery_x.jsonl"
        cap.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in rows))
        return cap

    def test_tables_per_frame(self):
        with tempfile.TemporaryDirectory() as d:
            cap = ft.load_capture(self.make(d))
            self.assertEqual(sorted(cap.frames), [1, 2])
            f1, f2 = cap.frames[1], cap.frames[2]
            self.assertEqual(f1.tf4[5], (1.0, 5.0, 0.0, 1.0))
            self.assertEqual(f2.tf4[5], (2.0, 5.0, 0.0, 1.0))
            self.assertEqual(f1.tf5[0], (10 / 255.0, 20 / 255.0, 30 / 255.0, 1.0))
            self.assertEqual(f2.tf5[0], (20 / 255.0, 20 / 255.0, 30 / 255.0, 1.0))
            self.assertEqual(f1.tf4_base, 0x1FAB1000)
            self.assertEqual(f2.tf4_base, 0x1FAB5000)
            self.assertEqual(f1.regs[67], struct.unpack(">4f", struct.pack(">4f", 20.0, 1900.0, 1.0, 0.0005)))
            self.assertEqual(len(cap.textures), 5)
            self.assertEqual(sum(1 for t in cap.textures if t.dim == 1), 4)

    def test_a_frame_whose_table_was_not_dumped_has_none(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.make(d)
            rows = [json.loads(line) for line in p.read_text().splitlines()]
            rows = [r for r in rows if not (r.get("kind") == "texture" and r.get("frame") == 2 and r["bytes"] == 512)]
            p.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in rows))
            cap = ft.load_capture(p)
            self.assertIsNotNone(cap.frames[1].tf4)
            self.assertIsNone(cap.frames[2].tf4)      # the content its rows name has no dump: never another one
            self.assertIsNotNone(cap.frames[2].tf5)
            self.assertIn("no dump of this frame's table", "\n".join(ft.report_tables(cap)))

    def test_a_capture_from_before_fog_discovery_has_fetch_constants_and_no_tables(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.make(d)
            rows = [json.loads(line) for line in p.read_text().splitlines()]
            rows = [r for r in rows if not (r.get("kind") == "texture" and r.get("dim") == 1)]
            for r in rows:
                r.pop("tfc", None)
            p.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in rows))
            cap = ft.load_capture(p)
            self.assertFalse(cap.has_tfc)
            self.assertEqual(cap.frames[1].tf4_base, 0x1FAB1000)
            self.assertIsNone(cap.frames[1].tf4)
            self.assertIsNone(cap.frames[1].tf5)
            self.assertEqual(cap.frames[2].regs[67][0], 20.0)

    def make_full(self, d):
        """The capture of make() plus, in frame 1: a tree row whose vertex shader samples tf16 (the bytes of
        tf5) and a 2D tf17 whose two rows are c68 and tf4; the sky's "indx" row with its data row; a 32 x 32
        scene colour sampled by a post rectangle."""
        p = self.make(d)
        d = Path(d)
        hx = lambda fc: ["0x%08X" % v for v in fc]
        rows = [json.loads(line) for line in p.read_text().splitlines()]
        c68 = (0.25, 0.5, 0.75, 0.0)
        for r in rows:
            if r.get("kind") == "draw":
                pf = [r["pfull"][i:i + 32] for i in range(0, 8192, 32)]
                pf[68] = struct.pack(">4f", *c68).hex().upper()
                r["pfull"] = "".join(pf)
        extra = []
        tf16 = [TF5_FC[0], (TF5_FC[1] & 0xFFF) | 0x1FAC0000] + TF5_FC[2:]
        (d / "native_tex_x" / "tf16.bin").write_bytes(bytes([255, 10, 20, 30]) * 256)
        extra.append({"kind": "texture", "file": "native_tex_x/tf16.bin", "bytes": 1024, "frame": 1,
                      "content": "0x%016X" % 0x51, "dim": 1, "fc": hx(tf16)})
        # tf17: 64 x 2 half floats, linear, pitch 64: row 0 the near colour, row 1 the frame's tf4.
        tf17 = [0x00804802 | 0, 0x1FAD0000 | 29 | (1 << 6), 63 | (1 << 13), 0x688 << 1, 0, 0x200]
        row0 = half_be(*c68) * 64
        row1 = b"".join(half_be(1, i, 0.0, 1.0) for i in range(64))
        (d / "native_tex_x" / "tf17.bin").write_bytes(row0 + row1)
        extra.append({"kind": "texture", "file": "native_tex_x/tf17.bin", "bytes": 1024, "frame": 1,
                      "content": "0x%016X" % 0x52, "fc": hx(tf17)})
        extra.append({"kind": "draw", "frame": 1, "in_scene": True, "ps_hash": "0x00E0000000000000",
                      "vs_tex_slots": [16, 17], "tf": {"0": None, "16": hx(tf16), "17": hx(tf17)},
                      "tfc": {"16": "0x%016X" % 0x51, "17": "0x%016X" % 0x52}})
        sky_tex = [0x80800002, 0x1A000052, 0x0003E01F, 0, 0, 0x218]
        pfull = ["00000000" * 4] * 256
        pfull[47] = struct.pack(">4f", 0.125, 0, 0, 0).hex().upper()
        extra.append({"kind": "indx", "frame": 1, "func": "DrawIndx:82217EE8", "prim": 4, "base": 0, "vertices": 8,
                      "indices": 36, "index32": False, "stride": 12, "in_scene": True, "n": 7, "storage_ok": True,
                      "rs": {"2200": "0x00708762"}, "ps_hash": "0xF7601FBE485936FD",
                      "tf": {"4": hx(TF4_FC), "13": hx(sky_tex), "14": hx(sky_tex)}, "tfc": {"4": "0x%016X" % 1},
                      "pfull": "".join(pfull), "vs": {"hash": "0x8262E1111C273B50"},
                      "bank": [float(i) for i in range(1024)], "fetches": []})
        extra.append({"kind": "indxdata", "frame": 1, "n": 7, "stride": 12, "vbytes": 24,
                      "verts": struct.pack(">6f", 0, 0, 0, 1, 1, 1).hex().upper(), "idx": [0, 1, 2]})
        scene_fc = [0x80800002 | (1 << 22), 0x199E0000 | 29 | (1 << 6), 31 | (31 << 13), 0x688 << 1, 0, 0x218]
        (d / "native_tex_x" / "scene.bin").write_bytes(half_be(32.0, 16.0, 8.0, 1.0) * 1024)
        extra.append({"kind": "texture", "file": "native_tex_x/scene.bin", "bytes": 8192, "frame": 1,
                      "content": "0x%016X" % 0x99, "fc": hx(scene_fc)})
        extra.append({"kind": "verts", "frame": 1, "in_scene": False, "ps_hash": "0x87E8000000000000",
                      "tf": {"0": hx(scene_fc)}, "tfc": {"0": "0x%016X" % 0x99}})
        at = next(i for i, r in enumerate(rows) if r.get("kind") == "frame")
        rows[at:at] = extra
        p.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in rows))
        return p

    def test_the_tree_report_compares_tf16_with_tf5_and_tf17_with_c68_and_tf4(self):
        with tempfile.TemporaryDirectory() as d:
            text = "\n".join(ft.report_trees(ft.load_capture(self.make_full(d))))
            self.assertIn("tf16 in 1 draw rows", text)
            self.assertIn("against tf5 texel by texel: largest difference 0, mean 0", text)
            self.assertIn("same content hash as the frame's tf5: False", text)   # another address, equal bytes
            self.assertIn("64 x 2 texels", text)
            self.assertIn("row 0 against c68 (0.25, 0.5, 0.75): largest difference 0, mean 0", text)
            self.assertIn("row 1 against tf4 at the same coordinate: largest difference 0, mean 0", text)
            self.assertIn("frame 2: no draw row", text)

    def test_the_sky_report_shows_the_row_and_its_cube(self):
        with tempfile.TemporaryDirectory() as d:
            text = "\n".join(ft.report_sky(ft.load_capture(self.make_full(d))))
            self.assertIn("frame 1: 1 indx rows: F760 x 1", text)
            self.assertIn("vertices 8 indices 36 stride 12", text)
            self.assertIn("rs: 2200=0x00708762", text)
            self.assertIn("tf13: base 0x1A000000 2D 32 x 32 format 18", text)
            self.assertIn("c47 = (0.125, 0, 0, 0)", text)
            self.assertIn("vertex c9 = (36, 37, 38, 39)", text)
            self.assertIn("vertex 1 as big-endian floats: (1, 1, 1)", text)
            self.assertIn("indices: [0, 1, 2]", text)

    def test_the_scene_report_untiles_the_content_the_post_chain_sampled(self):
        with tempfile.TemporaryDirectory() as d:
            png = Path(d) / "scene.png"
            cap = self.make_full(d)
            text = "\n".join(ft.report_scene(ft.load_capture(cap), png=str(png)))
            self.assertIn("pixel shader 87E8, slot 0: base 0x199E0000 32 x 32", text)
            self.assertIn("r: finite 1024 of 1024, min 32, median 32, mean 32, max 32", text)
            self.assertIn("share of texels that are all zero: 0.0000", text)
            self.assertTrue(png.read_bytes().startswith(b"\x89PNG"))
            # Another frame, or a capture without the texture: said, not guessed.
            text = "\n".join(ft.report_scene(ft.load_capture(cap), frame=2))
            self.assertIn("no row with a kept content of the texture at 0x199E0000 in frame 2", text)

    def test_the_table_report_runs(self):
        with tempfile.TemporaryDirectory() as d:
            lines = ft.report_tables(ft.load_capture(self.make(d)))
            text = "\n".join(lines)
            self.assertIn("frame 1", text)
            self.assertIn("tf4", text)
            self.assertIn("tf5", text)


if __name__ == "__main__":
    unittest.main()
