"""tools/xdk_sigmatch/post_replay.py: the replay's arithmetic on a stored 16 x 16 excerpt of a read-back capture
(tests/post_replay_excerpt.json, written once by `post_replay.py excerpt 092959 1 --at 776,576`), its file
formats, its statistics and its reader on hand-made bytes."""
import contextlib
import io
import json
import math
import struct
import sys
import tempfile
import unittest
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import post_replay as pr  # noqa: E402

EXCERPT = ROOT / "tests" / "post_replay_excerpt.json"
CAPTURE = pr.LOGS / "native_discovery_20261009_092959.jsonl"
NAN, INF = float("nan"), float("inf")


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def frame(regs=None, table=None, c20w=8.0):
    """A frame as load_frame makes it, from registers given by role."""
    fr = pr.Frame("made.jsonl", 1)
    fr.c20w = c20w
    fr.table = list(table) if table is not None else [0.0] + [0.25] * 511
    fr.table_bytes = struct.pack(">512f", *fr.table)
    fr.regs = {
        "composite": {31: (2.0, 0, 0, 0), 46: (0.25, 0, 0, 0), 47: (50.0, 0, 0, 0), 77: (-0.005, 1.1, 0, 0), 78: (0.5, 0, 0, 0)},
        "bright": {31: (1.5, 0, 0, 0), 46: (0.2, 0, 0, 0), 47: (2.0, 0, 0, 0), 77: (0.05, 0.125, 0, 0)},
        "blur-x": {46: (0.4, 0.2, 0.1, 0.0), 47: (0.0, 0.0, 0.0, 0.0)},
        "blur-y": {46: (0.2, 0.15, 0.1, 0.05), 47: (0.05, 0.03, 0.02, 9.0)},
    }
    fr.regs.update(regs or {})
    fr.rows = {role: {} for role in fr.regs}
    return fr


class Excerpt(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = json.loads(EXCERPT.read_text(encoding="ascii"))
        cls.table = list(struct.unpack(">512f", bytes.fromhex(cls.doc["table"])))
        c = cls.doc["regs"]["composite"]
        cls.regs = (c["c31"][0], c["c46"][0], c["c47"][0], (c["c77"][0], c["c77"][1]), c["c78"][0])

    def test_it_is_a_block_of_the_read_back_capture(self):
        d = self.doc
        self.assertEqual((d["capture"], d["frame"], d["size"]), ("native_discovery_20261009_092959.jsonl", 1, 16))
        for key in ("scene", "bloom", "A", "B", "N", "front"):
            self.assertEqual(len(d[key]), 256, key)
            self.assertTrue(all(len(p) == 3 and all(math.isfinite(v) for v in p) for p in d[key]), key)
        self.assertAlmostEqual(d["c20w"], 8.44137, delta=1e-5)
        # The pairing of spec Design 4: the bright pass's c77.y is 1 / c20.w.
        self.assertAlmostEqual(d["regs"]["bright"]["c77"][1] * d["c20w"], 1.0, delta=1e-3)

    def test_the_table_is_the_captured_one(self):
        self.assertEqual(self.table[0], 0.0)
        self.assertAlmostEqual(self.table[128], 0.2427, delta=5e-4)      # research section 3: T at L = 1
        self.assertAlmostEqual(self.table[13], 0.2008, delta=2e-3)       # L = 0.1
        self.assertTrue(all(math.isfinite(t) and 0.0 <= t < 4.0 for t in self.table))

    def test_variant_a_is_the_composite_without_bloom(self):
        for s, want in zip(self.doc["scene"], self.doc["A"]):
            got = lf.post_composite(s, (0.0, 0.0, 0.0), self.table, *self.regs)
            for g, w in zip(got, want):
                self.assertAlmostEqual(g, w, delta=1e-12)

    def test_variant_b_is_the_composite_with_the_stored_bloom(self):
        for s, b, want in zip(self.doc["scene"], self.doc["bloom"], self.doc["B"]):
            got = lf.post_composite(s, b, self.table, *self.regs)
            for g, w in zip(got, want):
                self.assertAlmostEqual(g, w, delta=1e-12)

    def test_variant_n_is_todays_mapping(self):
        for s, want in zip(self.doc["scene"], self.doc["N"]):
            for g, w in zip(pr.native_mapping(s, self.doc["c20w"]), want):
                self.assertAlmostEqual(g, w, delta=1e-12)
        s = self.doc["scene"][0]
        self.assertAlmostEqual(self.doc["N"][0][1], math.sqrt(min(s[1] / self.doc["c20w"] * 2.22, 1.0)), delta=1e-12)

    def levels(self, key):
        return sorted(abs(a - b) * 255.0 for p, q in zip(self.doc[key], self.doc["front"]) for a, b in zip(p, q))

    def test_against_the_games_front_buffer(self):
        # The block holds a figure's edge, so it is worse than the picture's 0.73 and 3.14 (research section 4);
        # what the test holds is the order: B is near the game, A is darker by the bloom, N is far.
        a, b, n = self.levels("A"), self.levels("B"), self.levels("N")
        self.assertLess(pr.percentile(b, 50), 2.0)
        self.assertLess(pr.percentile(b, 50), pr.percentile(a, 50))
        self.assertLess(pr.percentile(a, 50), pr.percentile(n, 50))
        self.assertGreater(sum(1 for v in b if v <= 8.0) / len(b), 0.95)

    def test_the_block_reaches_the_tables_steep_part_and_its_far_part(self):
        texels = {lf.post_table_texel(self.regs[1] * lf.post_lum(s)) for s in self.doc["scene"]}
        self.assertLess(min(texels), 13)
        self.assertGreater(max(texels), 128)
        self.assertGreater(len(texels), 100)

    def test_the_other_addressing_gives_other_values(self):
        # floor(u * 512), the research replay's reading, differs from the reference's in the dark texels.
        floor = pr.table_reader(self.table, 0.0)
        differ = 0
        for s, want in zip(self.doc["scene"], self.doc["A"]):
            got = lf.post_composite(s, (0.0, 0.0, 0.0), floor, *self.regs)
            differ += any(abs(g - w) > 1e-4 for g, w in zip(got, want))
        self.assertGreater(differ, 50)


class Arithmetic(unittest.TestCase):
    def test_q10(self):
        self.assertEqual(pr.q10(0.0), 0.0)
        self.assertEqual(pr.q10(1.7), 1.0)
        self.assertEqual(pr.q10(-0.2), 0.0)
        self.assertEqual(pr.q10(NAN), 0.0)
        self.assertEqual(pr.q10(0.5), 512.0 / 1023.0)          # 511.5 rounds to the even 512
        self.assertEqual(pr.q10(100.4 / 1023.0), 100.0 / 1023.0)

    def test_the_table_reader(self):
        table = [float(i) for i in range(512)]
        self.assertIs(pr.table_reader(table, 0.5), table)
        floor = pr.table_reader(table, 0.0)
        self.assertEqual([floor(u) for u in (0.0, 0.9 / 512, 1.0 / 512, 128.7 / 512, 1.0, 7.0, -1.0, NAN, INF)],
                         [0.0, 0.0, 1.0, 128.0, 511.0, 511.0, 0.0, 0.0, 511.0])
        half = pr.table_reader(table, 0.4999)
        self.assertEqual(half(128.7 / 512), 129.0)

    def test_the_native_mapping(self):
        got = pr.native_mapping((8.0 / 2.22, 2.0 / 2.22, 100.0), 8.0)
        self.assertAlmostEqual(got[0], 1.0, delta=1e-12)
        self.assertAlmostEqual(got[1], 0.5, delta=1e-12)
        self.assertEqual(got[2], 1.0)
        self.assertEqual(pr.native_mapping((NAN, -1.0, 0.0), 8.0), [0.0, 0.0, 0.0])

    def test_a_flat_texture_is_light_formulas_texture(self):
        w, h = 5, 4
        rows = [[(x + 10.0 * y, x * y * 0.5, 1.0) for x in range(w)] for y in range(h)]
        flat = array("d", [v for row in rows for t in row for v in t])
        for linear in (False, True):
            a, b = pr.flat_texture(flat, w, h, linear), lf.post_texture(rows, linear)
            for px, py in ((0.5, 0.5), (2.0, 1.0), (4.9, 3.9), (-3.0, 9.0), (1.25, 2.75), (NAN, 1.0), (INF, -INF)):
                for g, want in zip(a(px, py), b(px, py)):
                    self.assertAlmostEqual(g, want, delta=1e-12)

    def test_the_bloom_is_the_references_steps_with_the_targets_ten_bits(self):
        w, h = 32, 16
        flat = array("d", [((x * 7 + y * 3 + c) % 11) * 1.5 for y in range(h) for x in range(w) for c in range(3)])
        fr = frame()
        bw, bh, rows = pr.bloom_picture(flat, w, h, fr)
        self.assertEqual((bw, bh, len(rows), len(rows[0])), (8, 4, 4, 8))
        scene = pr.flat_texture(flat, w, h, True)
        q = lambda t: [round(lf.sat(v) * 1023.0) / 1023.0 for v in t]      # noqa: E731
        bright = [[q(lf.post_bright(lf.post_downsample(scene, x, y, 0.0), (0, 0, 0), (0, 0, 0), 1.5, 0.2, 2.0, (0.05, 0.125)))
                   for x in range(bw)] for y in range(bh)]
        along_x = [[q(lf.post_blur13(lf.post_texture(bright), x, y, [0.4, 0.2, 0.1, 0.0, 0.0, 0.0, 0.0], False))
                    for x in range(bw)] for y in range(bh)]
        want = [[q(lf.post_blur13(lf.post_texture(along_x), x, y, [0.2, 0.15, 0.1, 0.05, 0.05, 0.03, 0.02], True))
                 for x in range(bw)] for y in range(bh)]
        self.assertEqual(rows, want)
        self.assertTrue(any(v > 0.0 for row in rows for t in row for v in t))
        self.assertTrue(all(v * 1023.0 == round(v * 1023.0) for row in rows for t in row for v in t))
        with self.assertRaises(pr.ReplayError):
            pr.bloom_picture(flat[:30 * 16 * 3], 30, 16, fr)

    def test_the_bloom_sampled_with_the_centres_aligned_is_kept(self):
        rows = [[(float(x), float(y), 0.0) for x in range(4)] for y in range(2)]
        at = pr.bloom_sampler(4, 2, rows, 16, 8, place="aligned")
        self.assertEqual(list(at(2, 2)[:2]), [0.125, 0.125])            # (2.5 / 4 - 0.5, 2.5 / 4 - 0.5)
        self.assertEqual(list(at(0, 0)[:2]), [0.0, 0.0])                # clamped to the edge
        self.assertEqual(list(at(15, 7)[:2]), [3.0, 1.0])

    def test_the_bloom_is_sampled_at_the_composites_own_fetch(self):
        # 37B1 instruction 6: tfetch2D tf2 at the scene's interpolated coordinate (x / w at pixel x), OffsetX =
        # OffsetY = 0.5 texels of the bloom: bloom texel coordinate x * bw / w + 0.5. Pixel 0 reads texel 0
        # itself, pixel 4 texel 1, pixel 2 the mean of the two: 0.375 of a bloom texel further on than the
        # centres aligned. It is what the native composite does (clay_logic.h, kPostBloomFetch) and the
        # default of replay and predict.
        rows = [[(float(x), float(y), 0.0) for x in range(4)] for y in range(2)]
        at = pr.bloom_sampler(4, 2, rows, 16, 8, place="fetch")
        self.assertEqual(list(at(0, 0)[:2]), [0.0, 0.0])
        self.assertEqual(list(at(4, 4)[:2]), [1.0, 1.0])
        self.assertEqual(list(at(2, 2)[:2]), [0.5, 0.5])
        self.assertEqual(list(at(15, 7)[:2]), [3.0, 1.0])               # clamped to the edge
        aligned = pr.bloom_sampler(4, 2, rows, 16, 8, place="aligned")
        self.assertEqual(list(aligned(2, 2)[:2]), [0.125, 0.125])
        self.assertAlmostEqual(at(6, 0)[0] - aligned(6, 0)[0], 0.375, delta=1e-12)
        self.assertEqual(list(pr.bloom_sampler(4, 2, rows, 16, 8)(2, 2)[:2]), [0.5, 0.5])       # (the function's own default: fetch)
        self.assertEqual((pr.BLOOM_PLACES, pr.BLOOM_PLACE), (("fetch", "aligned"), "fetch"))
        with self.assertRaises(pr.ReplayError):
            pr.bloom_sampler(4, 2, rows, 16, 8, place="corner")
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                pr.main(["replay", "no_such_capture_stamp", "1", "--bloom-place", "corner"])

    def test_a_variants_picture(self):
        fr = frame()
        scene = array("d", [0.4, 0.4, 0.4, 2.0, 1.0, 0.5, 0.0, 0.0, 0.0, 0.001, 0.001, 0.001])
        a = pr.variant_picture("A", scene, 2, 2, fr)
        regs = (2.0, 0.25, 50.0, (-0.005, 1.1), 0.5)
        for i in range(4):
            self.assertEqual(list(a[3 * i:3 * i + 3]), lf.post_composite(scene[3 * i:3 * i + 3], (0, 0, 0), fr.table, *regs))
        self.assertEqual(list(a[6:12]), [0.0] * 6)                      # black, and texel 0 of the table
        b = pr.variant_picture("B", scene, 2, 2, fr, bloom=lambda x, y: (0.1 * x, 0.1 * y, 0.0))
        self.assertEqual(list(b[9:12]), lf.post_composite(scene[9:12], (0.1, 0.1, 0.0), fr.table, *regs))
        self.assertEqual(list(b[0:3]), list(a[0:3]))
        n = pr.variant_picture("N", scene, 2, 2, fr)
        self.assertAlmostEqual(n[0], math.sqrt(0.4 / 8.0 * 2.22), delta=1e-12)
        with self.assertRaises(pr.ReplayError):
            pr.variant_picture("B", scene, 2, 2, fr)
        with self.assertRaises(pr.ReplayError):
            pr.variant_picture("C", scene, 2, 2, fr)

    def test_resampling_the_front_buffers_width(self):
        src = array("d", [float(x) for x in range(8) for _ in range(3)])
        out = pr.resample_width(src, 8, 7, 1)
        for i in range(7):
            want = min(max((i + 0.5) * 8 / 7 - 0.5, 0.0), 7.0)
            self.assertAlmostEqual(out[3 * i], want, delta=1e-12)
        self.assertEqual(list(pr.resample_width(src, 8, 8, 1)), list(src))


class Ramp(unittest.TestCase):
    def test_the_measured_stand_in_holds_the_measured_pairs(self):
        ramp = pr.measured_ramp()
        self.assertEqual([len(c) for c in ramp], [256, 256, 256])
        for level, shot in ((23, 10), (47, 32), (63, 47), (124, 110), (156, 144), (204, 197), (236, 232), (252, 250)):
            self.assertAlmostEqual(ramp[0][level] * 255.0, shot, delta=1e-9)
        self.assertEqual((ramp[1][0], ramp[2][255]), (0.0, 1.0))
        self.assertTrue(all(a <= b for a, b in zip(ramp[0], ramp[0][1:])))

    def line(self, entries, valid=1):
        return "i> 0001 [native] ramp: read at clay frame 12: valid %d, %s\n" % (valid, " ".join(entries))

    def test_a_logs_ramp_line(self):
        entries = ["%03X%03X%03X" % (i * 4, min(i * 4 + 1, 1023), 1023 - i * 4) for i in range(256)]
        text = "noise\n" + self.line(["000000000"] * 256) + self.line(entries) + "[native] ramp: changes 1\n"
        ramp = pr.parse_ramp_line(text)
        self.assertEqual(ramp[0][1], 4 / 1023.0)
        self.assertEqual(ramp[1][1], 5 / 1023.0)
        self.assertEqual(ramp[2][1], 1019 / 1023.0)
        self.assertEqual(ramp[0][255], 1020 / 1023.0)
        # The product's own form: the entries run together (FormatRampHex), as the game's log has them.
        together = "i> 0001 [native] ramp: read at clay frame 0: valid 1, %s\n" % "".join(entries)
        self.assertEqual(pr.parse_ramp_line(together), ramp)
        with self.assertRaises(pr.ReplayError):
            pr.parse_ramp_line(together.replace("".join(entries), "".join(entries)[:-9]))
        # (and one entry too long: 2,313 digits are no table either)
        with self.assertRaises(pr.ReplayError):
            pr.parse_ramp_line(together.replace("".join(entries), "".join(entries) + entries[0]))

    def test_a_ramp_line_that_is_not_one(self):
        for text in ("", self.line(["000000000"] * 255), self.line(["000000000"] * 256, valid=0),
                     self.line(["3FF3FF400"] + ["000000000"] * 255)):
            with self.assertRaises(pr.ReplayError):
                pr.parse_ramp_line(text)

    def test_load_ramp(self):
        self.assertIsNone(pr.load_ramp(None))
        self.assertEqual(pr.load_ramp("measured"), pr.measured_ramp())
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "fable_2.log"
            log.write_text(self.line(["3FF000200"] * 256), encoding="utf-8")
            ramp = pr.load_ramp(str(log))
        self.assertEqual((ramp[0][7], ramp[1][7], ramp[2][7]), (1.0, 0.0, 512 / 1023.0))

    def test_bytes_with_and_without_the_ramp(self):
        picture = array("d", [0.0, 0.5, 1.0, NAN, 1.7, 124 / 255.0])
        self.assertEqual(pr.to_bytes(picture), bytes([0, 128, 255, 255, 0, 255, 124, 255]))
        ramp = pr.measured_ramp()
        self.assertEqual(pr.to_bytes(picture, ramp)[4:8], bytes([0, 255, 110, 255]))
        swap = [[1.0 - i / 255.0 for i in range(256)], [0.0] * 256, [1.0] * 256]
        self.assertEqual(pr.to_bytes(picture, swap), bytes([255, 0, 255, 255, 255, 0, 255, 255]))


class Files(unittest.TestCase):
    def test_a_picture_dump_round_trips(self):
        rgba = bytes(range(24))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "p.rgba"
            pr.write_dump(path, 3, 2, rgba)
            data = path.read_bytes()
            self.assertEqual(data[:12], b"F2OD" + struct.pack("<II", 3, 2))
            self.assertEqual(pr.read_dump(path), (3, 2, rgba))
            for bad in (b"", b"F2OX" + data[4:], data[:-1], data + b"\x00", b"F2OD" + struct.pack("<II", 0, 0)):
                path.write_bytes(bad)
                with self.assertRaises(pr.ReplayError):
                    pr.read_dump(path)
            with self.assertRaises(pr.ReplayError):
                pr.write_dump(path, 3, 2, rgba[:-1])

    def test_the_oracle_scene(self):
        scene = array("d", [0.5, 1.5, f32(0.1), 31.875, 0.0, 2.0])
        data = pr.pack_scene(scene, 2, 1)
        self.assertEqual(data[:12], b"F2OS" + struct.pack("<II", 2, 1))
        self.assertEqual(struct.unpack("<6f", data[12:]), tuple(scene))
        with self.assertRaises(pr.ReplayError):
            pr.pack_scene(scene, 2, 2)

    def test_values_a_half_float_texture_changes(self):
        # The pass uploads the scene as halves (post_oracle.h, OracleHalf): bounded to 65504, nearest even.
        scene = array("d", [0.0, 0.5, 31.875, f32(0.1), 1e9, -1e9, 65504.0, 1.0 + 2.0 ** -10, 1.0 + 2.0 ** -11])
        self.assertEqual(pr.half_changed(scene), 4)     # 0.1, both beyond the largest half, 1 + 2^-11
        self.assertEqual(pr.half_changed(array("d")), 0)

    def test_the_formats_are_the_pass_parsers(self):
        # src/native/render/post_oracle.h: the magics, the version, the block's floats, the dump's frame.
        text = (ROOT / "src" / "native" / "render" / "post_oracle.h").read_text(encoding="utf-8")
        for magic in ('"F2OS"', '"F2OB"', '"F2OD"'):
            self.assertIn("std::memcmp(data, %s, 4)" % magic if magic != '"F2OD"' else "std::memcpy(p, %s, 4)" % magic, text)
        self.assertIn("kOracleBlockFloats = %d;" % pr.BLOCK_FLOATS, text)
        self.assertIn("kOracleBlockVersion = 1;", text)
        self.assertIn("kOracleWidth = %d, kOracleHeight = %d;" % (pr.SCENE_W, pr.SCENE_H), text)
        self.assertEqual(len(pr.pack_block(frame())), 2180)
        self.assertIn("kOracleBlockBytes == 2180", text)
        self.assertEqual(pr.pack_block(frame())[4:8], struct.pack("<I", 1))

    def test_the_oracle_block(self):
        fr = frame()
        data = pr.pack_block(fr)
        self.assertEqual(len(data), 4 + 4 + 4 + 541 * 4 + 4)
        self.assertEqual(data[:8], b"F2OB" + struct.pack("<I", 1))
        v = struct.unpack("<542f", data[8:-4])
        near = lambda got, want: [self.assertAlmostEqual(g, f32(w), delta=0) for g, w in zip(got, want)]     # noqa: E731
        self.assertEqual(v[0], 8.0)                                         # c20.w
        near(v[1:7], (2.0, 0.25, 50.0, -0.005, 1.1, 0.5))                   # composite: c31.x c46.x c47.x c77.x c77.y c78.x
        near(v[7:12], (1.5, 0.2, 2.0, 0.05, 0.125))                         # bright: c31.x c46.x c47.x c77.x c77.y
        near(v[12:19], (0.4, 0.2, 0.1, 0.0, 0.0, 0.0, 0.0))                 # EA83: c46.xyzw c47.xyz
        near(v[19:26], (0.2, 0.15, 0.1, 0.05, 0.05, 0.03, 0.02))            # 28DB
        self.assertEqual(v[26:30], (0.0, 0.0, 0.0, 0.0))                    # no radial row
        self.assertEqual(v[30:], tuple(f32(t) for t in fr.table))
        self.assertEqual(struct.unpack("<I", data[-4:])[0], pr.PART_BLUR)

    def test_the_blocks_parts(self):
        radial = {"radial": {31: (0.38, 0.39, 0, 0), 46: (0.87, 0, 0, 0), 47: (0.935, 0, 0, 0)}}
        floats, parts = pr.block_floats(frame(radial))
        self.assertEqual(parts, pr.PART_BLUR | pr.PART_RADIAL)
        self.assertEqual(floats[25:29], [0.38, 0.39, 0.87, 0.935])
        self.assertEqual(len(floats), 541)
        # Weights that do not sum to 1, a radial register outside its bound: the part is not set.
        self.assertEqual(pr.block_floats(frame({"blur-x": {46: (0.5, 0.2, 0.1, 0.0), 47: (0.0, 0.0, 0.0, 0.0)}}))[1], 0)
        self.assertEqual(pr.block_floats(frame({"blur-y": {46: (1.2, -0.1, 0.0, 0.0), 47: (0.0, 0.0, 0.0, 0.0)}}))[1], 0)
        outside = {"radial": {31: (5.0, 0.39, 0, 0), 46: (0.87, 0, 0, 0), 47: (0.935, 0, 0, 0)}}
        self.assertEqual(pr.block_floats(frame(outside))[1], pr.PART_BLUR)
        zero = {"radial": {31: (0.5, 0.5, 0, 0), 46: (0.0, 0, 0, 0), 47: (0.935, 0, 0, 0)}}
        self.assertEqual(pr.block_floats(frame(zero))[1], pr.PART_BLUR)


class Statistics(unittest.TestCase):
    def test_percentile(self):
        self.assertEqual(pr.percentile([1.0, 2.0, 3.0, 4.0], 50), 2.5)
        self.assertEqual(pr.percentile([1.0, 2.0, 3.0], 50), 2.0)
        self.assertAlmostEqual(pr.percentile([0.0, 10.0], 90), 9.0, delta=1e-12)
        self.assertEqual(pr.percentile([7.0], 99), 7.0)
        self.assertTrue(math.isnan(pr.percentile([], 50)))

    def test_hue(self):
        self.assertEqual([pr.hue(c) for c in ((1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0))], [0.0, 120.0, 240.0, 60.0])
        self.assertTrue(math.isnan(pr.hue((0.5, 0.5, 0.5))))
        self.assertEqual(pr.hue_difference(350.0, 10.0), 20.0)

    def test_the_regions_are_the_bridges_26(self):
        regions = pr.regions("bridge")
        names = [r[0] for r in regions]
        self.assertEqual(len(names), 26)
        self.assertEqual(len(set(names)), 26)
        for name in ("dirt patch", "pole averted side", "far peak", "sky above the ridge", "lake"):
            self.assertIn(name, names)
        for _, rect, _ in regions:
            self.assertTrue(0.0 <= rect[0] < rect[2] <= 1.0 and 0.0 <= rect[1] < rect[3] <= 1.0, rect)
        self.assertEqual(pr.regions("no such spot"), [])

    def test_a_regions_mean(self):
        w, h = 10, 10
        picture = array("d", [float(x) if c == 0 else float(y) if c == 1 else 1.0 for y in range(h) for x in range(w) for c in range(3)])
        self.assertEqual(pr.region_box([0.2, 0.3, 0.5, 0.6], w, h), (2, 3, 5, 6))
        self.assertEqual(pr.region_mean(picture, w, h, [0.2, 0.3, 0.5, 0.6]), [3.0, 4.0, 1.0])
        self.assertEqual(pr.region_box([0.5, 0.5, 0.5, 0.5], w, h), (5, 5, 6, 6))        # at least one pixel
        rgba = bytes(v for y in range(h) for x in range(w) for v in (x, y, 7, 255))
        self.assertEqual(pr.region_mean(rgba, w, h, [0.2, 0.3, 0.5, 0.6], channels=4), [3.0, 4.0, 7.0])

    def test_the_hud_is_left_out(self):
        spans = pr.outside_hud(1120, 720)
        self.assertEqual(spans[0], [])
        self.assertEqual(spans[117], [])
        self.assertEqual(spans[118], [(0, 1120)])
        self.assertEqual(spans[600], [(0, 50), (168, 1120)])
        self.assertEqual(spans[700], [(0, 1120)])

    def test_difference_statistics(self):
        a = array("d", [0.0, 0.0, 0.0] * 4)
        b = array("d", [0.0, 0.0, 0.0, 1 / 255, 0.0, 0.0, 0.0, 2 / 255, 0.0, 9 / 255, 9 / 255, 9 / 255])
        s = pr.difference_stats(a, b, 2, 2)
        self.assertEqual(s["pixels"], 4)
        self.assertAlmostEqual(s["median"], 0.0, delta=1e-9)
        self.assertAlmostEqual(s["mean"], 30.0 / 12, delta=1e-9)
        self.assertEqual([s["within"][k] for k in (1, 2, 4, 8)], [0.5, 0.75, 0.75, 0.75])
        # The spans: only the second row.
        s = pr.difference_stats(a, b, 2, 2, spans=[[], [(1, 2)]])
        self.assertEqual((s["pixels"], s["within"][8]), (1, 0.0))
        self.assertAlmostEqual(s["median"], 9.0, delta=1e-9)
        self.assertIsNone(pr.difference_stats(a, b, 2, 2, spans=[[], []]))

    def test_the_comparison_of_two_dumps(self):
        w, h = 4, 4
        a = bytes([10, 20, 30, 255] * 16)
        b = bytearray(a)
        b[0] = 13               # one pixel 3 levels off in red
        out = []
        whole = pr.compare_report(a, bytes(b), w, h, None, say=out.append)
        self.assertEqual(whole["pixels"], 16)
        self.assertEqual(whole["within"][2], 15 / 16)
        self.assertEqual(whole["within"][8], 1.0)
        self.assertEqual(len(out), 2)
        self.assertIn("whole picture", out[0])
        self.assertIn("within 1 level 93.75%", out[0])

    def test_the_largest_difference_and_the_oracle_verdict(self):
        # Spec criterion 6: within 1 level on at least 99.9% of the pixels and within 2 on all.
        w, h = 50, 40                                   # 2,000 pixels: two may be 2 levels off
        a = bytes([100, 100, 100, 255] * (w * h))
        b = bytearray(a)
        b[4 * 7 + 1] = 102
        b[4 * 900 + 2] = 98
        s = pr.difference_stats(a, bytes(b), w, h, channels=4, scale=1.0)
        self.assertEqual((s["max"], s["beyond"]), (2.0, 0))
        self.assertEqual(s["within"][1], 1998 / 2000)
        ok, text = pr.oracle_verdict(s)
        self.assertTrue(ok)
        self.assertIn("held", text)
        self.assertNotIn("NOT", text)
        # the mean signed difference by channel, the first picture less the second
        self.assertEqual(s["signed"], [0.0, -2 / 2000, 2 / 2000])
        b[4 * 901] = 101                                # a third pixel off by one: still held (1 level counts as within)
        self.assertTrue(pr.oracle_verdict(pr.difference_stats(a, bytes(b), w, h, channels=4, scale=1.0))[0])
        b[4 * 902] = 102                                # a third pixel 2 levels off: 99.85% within 1
        s = pr.difference_stats(a, bytes(b), w, h, channels=4, scale=1.0)
        ok, text = pr.oracle_verdict(s)
        self.assertFalse(ok)
        self.assertIn("NOT held", text)
        b[4 * 902] = 100
        b[4 * 7 + 1] = 103                              # one pixel 3 levels off: not all within 2
        s = pr.difference_stats(a, bytes(b), w, h, channels=4, scale=1.0)
        self.assertEqual((s["max"], s["beyond"]), (3.0, 1))
        self.assertFalse(pr.oracle_verdict(s)[0])
        self.assertFalse(pr.oracle_verdict(None)[0])
        # the command prints it
        with tempfile.TemporaryDirectory() as tmp:
            pa, pb = Path(tmp) / "a.rgba", Path(tmp) / "b.rgba"
            pr.write_dump(pa, w, h, a)
            pr.write_dump(pb, w, h, bytes(b))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(pr.main(["compare", str(pa), str(pb), "--spot", "none"]), 0)
            self.assertIn("largest 3", out.getvalue())
            self.assertIn("criterion 6", out.getvalue())
            self.assertIn("NOT held", out.getvalue())

    def test_the_command_line_compares_two_dumps(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b, c = Path(tmp) / "a.rgba", Path(tmp) / "b.rgba", Path(tmp) / "c.rgba"
            pr.write_dump(a, 2, 2, bytes([5, 5, 5, 255] * 4))
            pr.write_dump(b, 2, 2, bytes([5, 6, 5, 255] * 4))
            pr.write_dump(c, 2, 1, bytes([5, 6, 5, 255] * 2))
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                self.assertEqual(pr.main(["compare", str(a), str(b), "--spot", "none"]), 0)
                self.assertEqual(pr.main(["compare", str(a), str(c), "--spot", "none"]), 2)
                self.assertEqual(pr.main(["compare", str(a), str(Path(tmp) / "none.rgba")]), 2)
            self.assertIn("median 0.00", out.getvalue())
            self.assertIn("within 1 level 100.00%", out.getvalue())
            self.assertIn("2 x 2 against 2 x 1", err.getvalue())


# --- the reader, on a hand-made capture -------------------------------------------------------------------

def pfull(regs):
    """A row's "pfull": 256 registers of four big-endian floats as hex."""
    return "".join(struct.pack(">4f", *regs.get(r, (0.0, 0.0, 0.0, 0.0))).hex().upper() for r in range(256))


TABLE_FC = ["0x04000956", "0x1FAA00A4", "0x000001FF", "0x010016D1", "0x00000000", "0x00000018"]


class Reader(unittest.TestCase):
    def capture(self, tmp, drop=None, frame=1):
        table = [0.0] + [0.125 + i / 4096.0 for i in range(1, 512)]
        tex = Path(tmp) / "native_tex"
        tex.mkdir(exist_ok=True)
        (tex / "table.bin").write_bytes(struct.pack(">512f", *table))
        rows = [
            {"kind": "texture", "file": "native_tex/other.bin", "bytes": 2048, "frame": frame, "content": "0x0000000000000001",
             "dim": 1, "fc": TABLE_FC},
            {"kind": "texture", "file": "native_tex/table.bin", "bytes": 2048, "frame": frame, "content": "0x00000000000000AA",
             "dim": 1, "fc": TABLE_FC},
            {"kind": "draw", "frame": frame, "in_scene": True, "ps_hash": "0x8D900846800943C8",
             "pfull": pfull({20: (0.01, 0.02, 0.03, 8.5)})},
            {"kind": "draw", "frame": frame, "in_scene": True, "ps_hash": "0x8D900846800943C8",
             "pfull": pfull({20: (0.0, 0.0, 0.0, 99.0)})},
            {"kind": "verts", "frame": frame, "in_scene": False, "ps_hash": "0xEE922CFB4426EA83",
             "pfull": pfull({31: (1.5, 0, 0, 0), 46: (0.2, 0, 0, 0), 47: (2.0, 0, 0, 0), 77: (0.09, 0.125, 0, 0)})},
            {"kind": "verts", "frame": frame, "in_scene": False, "ps_hash": "0xEA83C7E7E24EC4C8",
             "pfull": pfull({46: (0.4, 0.2, 0.1, 0.0), 47: (0.0, 0.0, 0.0, 0.5)})},
            {"kind": "verts", "frame": frame, "in_scene": False, "ps_hash": "0x28DBE18F313350BF",
             "pfull": pfull({46: (0.2, 0.15, 0.1, 0.05), 47: (0.05, 0.03, 0.02, 0.0)})},
            {"kind": "verts", "frame": frame, "in_scene": False, "ps_hash": "0x37B186F4BD3288F7",
             "tfc": {"0": "0x00000000000000BB", "1": "0x00000000000000AA"},
             "pfull": pfull({31: (3.0, 0, 0, 0), 46: (0.25, 0, 0, 0), 47: (50.0, 0, 0, 0), 77: (-0.005, 1.1, 0, 0), 78: (0.45, 0, 0, 0)})},
            {"kind": "verts", "frame": frame, "in_scene": False, "ps_hash": "0x37B186F4BD3288F7",
             "tfc": {"0": "0x00000000000000BB", "1": "0x0000000000000001"}, "pfull": pfull({31: (77.0, 0, 0, 0)})},
            {"kind": "verts", "frame": frame + 1, "in_scene": False, "ps_hash": "0xEE922CFB4426EA83", "pfull": pfull({31: (55.0, 0, 0, 0)})},
            {"kind": "frame", "frame": frame},
        ]
        path = Path(tmp) / "native_discovery_test.jsonl"
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("not a row\n")
            for r in rows:
                if drop is None or not drop(r):
                    f.write(json.dumps(r, separators=(",", ":")) + "\n")
        return path, table

    def test_a_frames_rows_registers_and_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            path, table = self.capture(tmp)
            fr = pr.load_frame(path, 1)
        self.assertEqual(fr.c20w, 8.5)                                   # the first main-scene row
        c31x, c46x, c47x, c77, c78x = fr.composite()                     # the first composite row
        self.assertEqual((c31x, c46x, c47x), (3.0, 0.25, 50.0))
        self.assertEqual(c77, (f32(-0.005), f32(1.1)))
        self.assertEqual(c78x, f32(0.45))
        self.assertEqual(fr.bright(), (1.5, f32(0.2), 2.0, (f32(0.09), 0.125)))
        self.assertEqual(fr.weights("blur-x"), [f32(v) for v in (0.4, 0.2, 0.1, 0.0, 0.0, 0.0, 0.0)])
        self.assertEqual(fr.weights("blur-y"), [f32(v) for v in (0.2, 0.15, 0.1, 0.05, 0.05, 0.03, 0.02)])
        self.assertEqual(fr.table, [f32(t) for t in table])              # by the content the composite's row names
        self.assertEqual(len(fr.table_bytes), 2048)
        self.assertNotIn("radial", fr.regs)

    def test_what_is_missing_is_said(self):
        cases = (
            (lambda r: r.get("ps_hash") == "0xEE922CFB4426EA83", "no bright row"),
            (lambda r: r.get("ps_hash") == "0x28DBE18F313350BF", "no blur-y row"),
            (lambda r: r.get("in_scene") is True, "no main-scene row"),
            (lambda r: r.get("content") == "0x00000000000000AA", "no dump of 0x1FAA0000"),
        )
        for drop, message in cases:
            with tempfile.TemporaryDirectory() as tmp:
                path, _ = self.capture(tmp, drop)
                with self.assertRaises(pr.ReplayError) as e:
                    pr.load_frame(path, 1)
                self.assertIn(message, str(e.exception))
        with tempfile.TemporaryDirectory() as tmp:
            path, _ = self.capture(tmp)
            with self.assertRaises(pr.ReplayError):
                pr.load_frame(path, 3)                                   # no such frame
            fr = pr.load_frame(path, 1)
            with self.assertRaises(pr.ReplayError) as e:
                pr.load_scene(fr)                                        # the scene's content was not dumped
            self.assertIn("0x199E0000", str(e.exception))

    def test_a_capture_is_found_by_its_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            path, _ = self.capture(tmp)
            self.assertEqual(pr.find_capture(str(path)), path)
        with self.assertRaises(pr.ReplayError):
            pr.find_capture("no_such_capture_stamp")

    def test_the_command_line_says_what_is_wrong(self):
        err = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
            self.assertEqual(pr.main(["replay", "no_such_capture_stamp", "1"]), 2)
        self.assertIn("no_such_capture_stamp", err.getvalue())


@unittest.skipUnless(CAPTURE.is_file(), f"no capture at {CAPTURE}")
class RealCapture(unittest.TestCase):
    def test_the_excerpt_is_this_captures_frame(self):
        fr = pr.load_frame(CAPTURE, 1)
        doc = json.loads(EXCERPT.read_text(encoding="ascii"))
        self.assertEqual(fr.c20w, doc["c20w"])
        self.assertEqual(fr.table_bytes.hex(), doc["table"])
        self.assertEqual(list(fr.regs["composite"][77]), doc["regs"]["composite"]["c77"])
        self.assertEqual(fr.weights("blur-x"), doc["regs"]["blur-x"]["c46"] + doc["regs"]["blur-x"]["c47"][:3])
        self.assertEqual(pr.block_floats(fr)[1], pr.PART_BLUR | pr.PART_RADIAL)

    def test_the_checks_literals_are_the_rows(self):
        # post_check.LITERALS is typed in: each shader's c255 is the last four floats of its first row's
        # "ps_lit" (16 floats from register 508: c252 to c255), and a shader without one has no such row entry.
        import post_check as pc
        fr = pr.load_frame(CAPTURE, 1)
        for s in lf.POST_SHADERS:
            lit = fr.rows[s.role].get("ps_lit")
            if s.hash not in pc.LITERALS:
                self.assertEqual(lit, [], s.role)
                continue
            self.assertEqual([e["reg"] for e in lit], [508], s.role)
            self.assertEqual([f32(v) for v in lit[0]["v"][12:16]], [f32(v) for v in pc.LITERALS[s.hash]], s.role)


if __name__ == "__main__":
    unittest.main()
