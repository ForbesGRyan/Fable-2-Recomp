import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import post_tables as pt  # noqa: E402
import texture_thumb as tt  # noqa: E402


def type0(reg, *values, one=False):
    return [((len(values) - 1) << 16) | (0x8000 if one else 0) | reg] + list(values)


def type3(opcode, *data, predicate=False):
    return [0xC0000000 | ((len(data) - 1) << 16) | (opcode << 8) | (1 if predicate else 0)] + list(data)


def cmd_hex(dwords):
    return "".join("%08X" % d for d in dwords)


# A resolve as the SDK reads one: the four copy registers, the copy mode, a rectangle list of 3 indices.
# RB_COPY_DEST_INFO: endian 2, format 36 (k_32_FLOAT) in bits 7-12.
COPY_INFO = 2 | (36 << 7)
COPY_PITCH = 320 | (180 << 16)
RESOLVE = (type0(0x2208, 6) + type0(0x2318, 0x00000000, 0x12D69000, COPY_PITCH, COPY_INFO)
           + type3(0x36, 0x00030088, 0, 1, 2))


class PacketTest(unittest.TestCase):
    def test_hex_dwords_are_big_endian_groups_of_eight(self):
        self.assertEqual(pt.hex_dwords("0003231812D69000"), [0x00032318, 0x12D69000])
        self.assertEqual(pt.hex_dwords(""), [])
        with self.assertRaises(ValueError):
            pt.hex_dwords("123")

    def test_the_four_packet_types(self):
        d = type0(0x2200, 0x00708764) + [0x40000000 | 0x123 | (0x456 << 11), 7, 8] + [0x80000000] + type3(0x22, 0, 0x00030088)
        p = pt.pm4_packets(d)
        self.assertEqual([x["type"] for x in p], [0, 1, 2, 3])
        self.assertEqual(pt.register_writes(p[0]), [(0x2200, 0x00708764)])
        self.assertEqual(pt.register_writes(p[1]), [(0x123, 7), (0x456, 8)])
        self.assertEqual(pt.register_writes(p[2]), [])
        self.assertEqual((p[3]["opcode"], p[3]["data"], p[3]["predicate"]), (0x22, [0, 0x00030088], False))
        self.assertEqual([x["at"] for x in p], [0, 2, 5, 6])

    def test_a_register_run_and_the_write_one_flag(self):
        run = pt.pm4_packets(type0(0x2318, 1, 2, 3, 4))[0]
        self.assertEqual(pt.register_writes(run), [(0x2318, 1), (0x2319, 2), (0x231A, 3), (0x231B, 4)])
        one = pt.pm4_packets(type0(0x2319, 5, 6, one=True))[0]
        self.assertEqual(pt.register_writes(one), [(0x2319, 5), (0x2319, 6)])

    def test_a_packet_that_runs_past_the_dwords_is_the_last(self):
        p = pt.pm4_packets(type0(0x2000, 1) + [0x00052318, 9, 9])
        self.assertEqual(len(p), 2)
        self.assertTrue(p[1].get("truncated"))
        self.assertEqual(p[1]["values"], [9, 9])
        self.assertFalse(p[0].get("truncated"))

    def test_a_predicated_type3_header(self):
        p = pt.pm4_packets(type3(0x36, 0x00030088, predicate=True))[0]
        self.assertTrue(p["predicate"])
        self.assertEqual(p["opcode"], 0x36)


class ResolveTest(unittest.TestCase):
    def test_decode_copy_fields(self):
        c = pt.decode_copy(0x00300000 | 0x300 | (2 << 4) | 4, 0x12D69000, COPY_PITCH, COPY_INFO | (1 << 24))
        self.assertEqual((c["base"], c["pitch"], c["height"], c["format"], c["endian"]), (0x12D69000, 320, 180, 36, 2))
        self.assertEqual((c["command"], c["src_select"], c["sample_select"]), ("null", 4, 2))
        self.assertTrue(c["color_clear"] and c["depth_clear"] and c["swap"])
        none = pt.decode_copy(None, None, None, None)
        self.assertIsNone(none["base"])
        self.assertNotIn("pitch", none)
        self.assertNotIn("format", none)

    def test_a_resolve_is_a_draw_with_its_copy_registers(self):
        ev = pt.draw_events(RESOLVE)
        self.assertEqual(len(ev), 1)
        e = ev[0]
        self.assertTrue(e["resolve"])
        self.assertEqual((e["opcode"], e["prim"], e["indices"], e["mode"]), (0x36, 8, 3, 6))
        self.assertEqual((e["copy"]["base"], e["copy"]["pitch"], e["copy"]["format"]), (0x12D69000, 320, 36))
        self.assertEqual(e["fresh"], [0x2318, 0x2319, 0x231A, 0x231B])
        self.assertTrue(pt.forced_by_sdk(e))
        self.assertIn("base 0x12D69000 pitch 320 height 180 format 36", pt.describe_event(e))
        self.assertIn("PM4_DRAW_INDX_2", pt.describe_event(e))

    def test_an_ordinary_draw_is_no_resolve_and_draw_indx_has_its_initiator_second(self):
        # PM4_DRAW_INDX: a viz query token first, then the initiator (a triangle list of 6 indices).
        ev = pt.draw_events(type0(0x2208, 4) + type3(0x22, 0, 0x00060004))
        self.assertEqual(len(ev), 1)
        self.assertFalse(ev[0]["resolve"])
        self.assertEqual((ev[0]["prim"], ev[0]["indices"]), (4, 6))
        self.assertIsNone(ev[0]["copy"]["base"])
        self.assertFalse(pt.forced_by_sdk(ev[0]))

    def test_copy_registers_count_for_the_next_draw_only(self):
        # Two draws: the copy registers were written before the first; the second is in colour mode again.
        d = RESOLVE + type0(0x2208, 4) + type3(0x22, 0, 0x00030008)
        ev = pt.draw_events(d)
        self.assertEqual([e["resolve"] for e in ev], [True, False])
        self.assertEqual(ev[1]["fresh"], [])
        # Without a mode word in the run, freshly written copy registers alone say resolve.
        ev = pt.draw_events(type0(0x2319, 0x12D69000) + type3(0x36, 0x00030088))
        self.assertTrue(ev[0]["resolve"])
        self.assertEqual(ev[0]["copy"]["base"], 0x12D69000)
        self.assertIsNone(ev[0]["copy"]["control"])
        self.assertIn("pitch ? height ?", pt.describe_event(ev[0]))
        # The copy mode alone (the registers written before the run) says resolve too; the base is unknown.
        ev = pt.draw_events(type0(0x2208, 6) + type3(0x36, 0x00030088))
        self.assertTrue(ev[0]["resolve"])
        self.assertIsNone(ev[0]["copy"]["base"])

    def test_copy_registers_reset_after_a_resolve_make_no_second_resolve(self):
        # The game's own stream (run 1, native_discovery_20261009_205049.jsonl): after the resolve the copy
        # control and info are written back to 0 and the mode to colour, then the next rectangle is drawn.
        d = RESOLVE + type0(0x2318, 0) + type0(0x231B, 0) + type0(0x2208, 4) + type3(0x22, 0, 0x00030088)
        ev = pt.draw_events(d)
        self.assertEqual([e["resolve"] for e in ev], [True, False])
        self.assertEqual(ev[1]["fresh"], [0x2318, 0x231B])
        self.assertEqual(ev[1]["copy"]["base"], 0x12D69000)

    def test_a_resolve_issued_as_draw_indx_is_not_what_the_sdk_forces(self):
        d = type0(0x2318, 0, 0x12D69000, COPY_PITCH, COPY_INFO) + type3(0x22, 0, 0x00030088)
        e = pt.draw_events(d)[0]
        self.assertTrue(e["resolve"])
        self.assertFalse(pt.forced_by_sdk(e))
        # Four indices, or another primitive: not forced either.
        e = pt.draw_events(type0(0x2319, 1) + type3(0x36, 0x00040088))[0]
        self.assertFalse(pt.forced_by_sdk(e))
        e = pt.draw_events(type0(0x2319, 1) + type3(0x36, 0x00030084))[0]
        self.assertFalse(pt.forced_by_sdk(e))


class TableTest(unittest.TestCase):
    def test_decode_table_is_512_big_endian_floats(self):
        data = struct.pack(">512f", *[i / 1024.0 for i in range(512)])
        t = pt.decode_table(data)
        self.assertEqual(len(t), 512)
        self.assertEqual((t[0], t[1], t[511]), (0.0, 1 / 1024.0, 511 / 1024.0))
        # Byte order by hand: 1.0 is 3F 80 00 00.
        one = bytes([0x3F, 0x80, 0, 0]) + bytes(2044)
        self.assertEqual(pt.decode_table(one)[:2], [1.0, 0.0])
        with self.assertRaises(ValueError):
            pt.decode_table(data[:2047])
        # More bytes than the table: the first 2,048 are read.
        self.assertEqual(pt.decode_table(data + b"\xff" * 8), t)

    def test_table_texel_is_floor_128_l_clamped(self):
        self.assertEqual([pt.table_texel(x) for x in (0, 0.0078, 0.0079, 0.01, 1, 3.99, 4, 100, -1)],
                         [0, 0, 1, 1, 128, 510, 511, 511, 0])
        t = [float(i) for i in range(512)]
        self.assertEqual(pt.table_at(t, 3), 384.0)

    def test_block_violations(self):
        comp = (2.973, 0.25, 50.0, -0.0052, 1.1026, 0.45455)
        bright = (1.4865, 0.2, 2.0, 0.0869, 0.11846)
        table = [0.0] + [0.2] * 511
        self.assertEqual(pt.block_violations(comp, bright, table, pt.TABLE_FC), [])
        # Each bound of plan Task 4, one at a time.
        for i, v in ((0, 65.0), (1, 0.0), (2, 2e6), (3, 4.5), (4, 0.0), (5, 1.5), (5, 0.0)):
            c = list(comp)
            c[i] = v
            self.assertEqual(len(pt.block_violations(c, bright, table, pt.TABLE_FC)), 1, (i, v))
        for i, v in ((0, -1.0), (1, 65.0), (2, 0.0), (3, -4.5), (4, 65.0)):
            b = list(bright)
            b[i] = v
            self.assertEqual(len(pt.block_violations(comp, b, table, pt.TABLE_FC)), 1, (i, v))
        # A register that is no number or was not read stops at the register rule.
        bad = pt.block_violations((float("nan"),) + comp[1:], bright, table, pt.TABLE_FC)
        self.assertEqual(len(bad), 1)
        self.assertIn("register", bad[0])
        self.assertIn("register", pt.block_violations(comp, (None,) * 5, table, pt.TABLE_FC)[0])
        # Texels: 4 is outside, a negative one and an infinity too; counted together.
        t = list(table)
        t[5], t[6], t[7] = 4.0, -0.001, float("inf")
        bad = pt.block_violations(comp, bright, t, pt.TABLE_FC)
        self.assertEqual(len(bad), 1)
        self.assertIn("3 texels", bad[0])
        self.assertIn("first at 5", bad[0])
        # Another fetch constant (a linear filter), and no table.
        fc = list(pt.TABLE_FC)
        fc[3] |= 1 << 19
        self.assertIn("fetch constant", pt.block_violations(comp, bright, table, fc)[0])
        self.assertIn("table-bytes", pt.block_violations(comp, bright, None, pt.TABLE_FC)[0])


def ramp_line(clay, valid, table):
    return "i> 0000 [native] ramp: read at clay frame %d: valid %d, %s\n" % (
        clay, valid, "".join("%03X%03X%03X" % e for e in table))


class RampTest(unittest.TestCase):
    def test_parse_ramp_lines(self):
        a = [(i * 4, i * 4, min(1023, i * 4 + 1)) for i in range(256)]
        b = [(i, i, i) for i in range(256)]
        text = "noise\n" + ramp_line(12, 1, a) + "[native] ramp: changes 0 (reads 300, without a table 0)\n" + ramp_line(900, 1, b)
        reads = pt.parse_ramp_lines(text)
        self.assertEqual([(r[0], r[1]) for r in reads], [(12, 1), (900, 1)])
        self.assertEqual(reads[0][2], a)
        self.assertEqual(reads[1][2][255], (255, 255, 255))
        # A line cut short is no table.
        self.assertEqual(pt.parse_ramp_lines(ramp_line(1, 1, a)[:-20]), [])

    def test_ramp_level_and_validity(self):
        self.assertEqual(pt.ramp_level(1023), 255.0)
        self.assertAlmostEqual(pt.ramp_level(441), 109.93, places=2)
        up = [(i, i * 2, i * 4) for i in range(256)]
        self.assertTrue(pt.ramp_valid(up))
        down = list(up)
        down[200] = (198, 400, 800)
        self.assertFalse(pt.ramp_valid(down))
        big = list(up)
        big[255] = (255, 510, 1024)
        self.assertFalse(pt.ramp_valid(big))

    def test_parse_pairs_reads_the_research_file_lines(self):
        text = ("flat pixels used: 100716\nlevel -> median shot level (quartiles, samples)\n"
                " 23 ->  10.0 ( 10.0.. 15.0,    236)  g 1.346\n124 -> 110.0 (109.0..111.0,    253)  g 1.166\n"
                "least-squares single exponent over levels 16..240: 1.2648\n")
        self.assertEqual(pt.parse_pairs(text), [(23, 10.0), (124, 110.0)])

    def test_report_ramp_checks_the_eight_pairs(self):
        # A table made to pass through the eight pairs (linear between them), and one that is the identity.
        pts = [(0, 0)] + list(pt.RAMP_PAIRS) + [(255, 255)]
        good = []
        for i in range(256):
            for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
                if x0 <= i <= x1:
                    v = round((y0 + (y1 - y0) * (i - x0) / (x1 - x0)) * 1023 / 255)
                    good.append((v, v, v))
                    break
        ident = [(round(i * 1023 / 255),) * 3 for i in range(256)]
        with tempfile.TemporaryDirectory() as d:
            for name, table, status, word in (("good.log", good, 0, "met"), ("ident.log", ident, 1, "NOT met")):
                p = Path(d) / name
                p.write_text(ramp_line(40, 1, table) + "[native] ramp: changes 0 (reads 5, without a table 0)\n", encoding="utf-8")
                out = io.StringIO()
                with redirect_stdout(out):
                    self.assertEqual(pt.report_ramp(p), status)
                self.assertIn("criterion 4 (the eight pairs within 1 level): %s" % word, out.getvalue())
                self.assertIn("the last says 0", out.getvalue())
            p = Path(d) / "none.log"
            p.write_text("[native] ramp: no reader registered; the scan-out ramp is not read\n", encoding="utf-8")
            out = io.StringIO()
            with redirect_stdout(out):
                self.assertEqual(pt.report_ramp(p), 1)
            self.assertIn("no reader was registered", out.getvalue())


class PlaneTest(unittest.TestCase):
    W, H = 40, 36

    def plane(self):
        return [0.01 * ((x * 7 + y * 13) % 31) + 0.002 * x for y in range(self.H) for x in range(self.W)]

    def place(self, mem, base, values, pitch, tiled, code):
        for i, v in enumerate(values):
            x, y = i % self.W, i // self.W
            o = tt.tiled_offset_2d(x, y, pitch, 2) if tiled else (y * pitch + x) * 4
            struct.pack_into(code, mem, base + o, v)

    def test_layouts(self):
        self.assertEqual(pt.plane_layouts(280), [("linear pitch 288", 288, False), ("linear pitch 320", 320, False),
                                                 ("tiled pitch 288", 288, True), ("tiled pitch 320", 320, True)])

    def test_finds_a_linear_big_endian_plane_at_its_page(self):
        ref = self.plane()
        mem = bytearray(0x8000)
        # Stored at 2.5 times the reference plus an offset: a correlation does not mind either.
        self.place(mem, 0x3000, [2.5 * v + 1 for v in ref], 64, False, ">f")
        found = pt.find_plane(bytes(mem), ref, self.W, self.H)
        self.assertTrue(found)
        r, off, name, order = found[0]
        self.assertEqual((off, name, order), (0x3000, "linear pitch 64", "big-endian"))
        self.assertGreater(r, 0.9999)
        # No other base, layout or byte order reaches the threshold.
        self.assertEqual(len(found), 1)

    def test_finds_a_tiled_plane_and_a_little_endian_one(self):
        ref = self.plane()
        mem = bytearray(0x10000)
        self.place(mem, 0x2000, ref, 64, True, ">f")
        self.place(mem, 0xA000, ref, 96, False, "<f")
        found = {(off, name, order) for _, off, name, order in pt.find_plane(bytes(mem), ref, self.W, self.H)}
        self.assertEqual(found, {(0x2000, "tiled pitch 64", "big-endian"), (0xA000, "linear pitch 96", "little-endian")})

    def test_finds_nothing_in_other_memory(self):
        ref = self.plane()
        mem = bytearray(0x8000)
        self.assertEqual(pt.find_plane(bytes(mem), ref, self.W, self.H), [])
        # Another picture, and the right picture half a page off its page.
        other = [0.01 * ((x * 3 + y * 5) % 17) for y in range(self.H) for x in range(self.W)]
        self.place(mem, 0x1000, other, 64, False, ">f")
        self.place(mem, 0x4800, ref, 64, False, ">f")
        self.assertEqual(pt.find_plane(bytes(mem), ref, self.W, self.H), [])
        # A plane holding a value that is no number does not count.
        mem = bytearray(0x8000)
        self.place(mem, 0x3000, ref, 64, False, ">f")
        struct.pack_into(">f", mem, 0x3000 + 5 * 4, float("nan"))
        self.assertEqual(pt.find_plane(bytes(mem), ref, self.W, self.H), [])
        with self.assertRaises(ValueError):
            pt.find_plane(bytes(mem), ref[:-1], self.W, self.H)


class DownsampleTest(unittest.TestCase):
    def scene(self, w, h):
        px = []
        for y in range(h):
            for x in range(w):
                px += [float(x), float(y), float(x * y), 1.0]
        return px

    def test_four_texels_integral_and_centre(self):
        w, h = 8, 8
        s = self.scene(w, h)
        d0 = pt.downsample_four(s, w, h, 0)
        self.assertEqual(len(d0), 2 * 2 * 4)
        # Block (0, 0): texels x, y in {0, 2}: mean x 1, mean y 1, mean x y = (0 + 0 + 0 + 4) / 4.
        self.assertEqual(d0[0:4], [1.0, 1.0, 1.0, 1.0])
        # Block (1, 1): texels x, y in {4, 6}: mean 5, 5, (16 + 24 + 24 + 36) / 4.
        self.assertEqual(d0[12:16], [5.0, 5.0, 25.0, 1.0])
        d2 = pt.downsample_four(s, w, h, 2)
        # Block (0, 0): texels in {2, 4}: mean 3, 3, (4 + 8 + 8 + 16) / 4.
        self.assertEqual(d2[0:4], [3.0, 3.0, 9.0, 1.0])
        # Block (1, 1): texels {6, 8}, the 8 clamped to 7: mean 6.5, 6.5, (36 + 42 + 42 + 49) / 4.
        self.assertEqual(d2[12:16], [6.5, 6.5, 42.25, 1.0])

    def test_box(self):
        w, h = 8, 4
        d = pt.downsample_box(self.scene(w, h), w, h)
        # Block (1, 0): x in 4..7 (mean 5.5), y in 0..3 (mean 1.5), x y mean 5.5 * 1.5.
        self.assertEqual(d[4:8], [5.5, 1.5, 8.25, 1.0])

    def test_colour_difference_tells_the_forms_apart(self):
        w, h = 16, 16
        s = self.scene(w, h)
        buf = pt.downsample_four(s, w, h, 0)
        med0, p90, worst, level = pt.colour_difference(buf, pt.downsample_four(s, w, h, 0))
        self.assertEqual((med0, p90, worst), (0.0, 0.0, 0.0))
        self.assertGreater(level, 0)
        self.assertGreater(pt.colour_difference(buf, pt.downsample_four(s, w, h, 2))[0], 1.0)
        self.assertGreater(pt.colour_difference(buf, pt.downsample_box(s, w, h))[0], 0.4)
        # Alpha is not compared.
        other = list(buf)
        other[3::4] = [9.0] * (len(other) // 4)
        self.assertEqual(pt.colour_difference(other, buf)[2], 0.0)

    def test_luminance_weights(self):
        self.assertAlmostEqual(pt.luminance(1, 1, 1), 1.0)
        self.assertAlmostEqual(pt.luminance(1, 0, 0), 0.2125)
        self.assertAlmostEqual(pt.luminance(0, 0, 1), 0.0721)


def pfull(regs):
    """A row's "pfull": 256 registers, those of `regs` (number -> four floats) set, the others zero."""
    out = []
    for r in range(256):
        out.append(struct.pack(">4f", *regs.get(r, (0.0, 0.0, 0.0, 0.0))).hex().upper())
    return "".join(out)


def fc_hex(fc):
    return ["0x%08X" % x for x in fc]


class CaptureTest(unittest.TestCase):
    """A hand-made capture of two frames: a main-scene row, the luminance pass, a "cmdgap" row with its
    resolve, the bright pass, the composite with its table, a clear's "resolve" row, the frame row."""

    A = 0x12D69000

    def write(self, d, frames=2, lum_resolves=1, base_step=0, composite_after_frame=False, scale=8.0):
        d = Path(d)
        tex = d / "native_tex_x"
        tex.mkdir()
        lines = []
        for n in range(1, frames + 1):
            table = [0.0] + [0.2 + 0.0001 * i + 0.01 * n for i in range(1, 512)]
            name = "native_tex_x/1FAA0000_T%d.bin" % n
            (d / name).write_bytes(struct.pack(">512f", *table))
            content = 0x1000 + n
            lines.append({"kind": "draw", "frame": n, "in_scene": True, "ps_hash": "0x1111111111111111",
                          "pfull": pfull({20: (0.0, 0.0, 0.0, scale)})})
            lines.append({"kind": "verts", "frame": n, "in_scene": False, "ps_hash": "0x" + pt.LUMINANCE_PS,
                          "tf": {}, "tfc": {}, "pfull": pfull({})})
            for k in range(lum_resolves):
                gap = (type0(0x2208, 6) + type0(0x2318, 0, self.A + base_step * (n - 1) + 0x1000 * k, COPY_PITCH, COPY_INFO)
                       + type3(0x36, 0x00030088) + type0(0x2208, 4) + type3(0x22, 0, 0x00030008))
                lines.append({"kind": "cmdgap", "frame": n, "n": k + 1, "func": "D3DDevice_BeginVertices?",
                              "from": "0xFF93F790", "dwords": len(gap), "cmd": cmd_hex(gap)})
            lines.append({"kind": "verts", "frame": n, "in_scene": False, "ps_hash": "0x" + pt.BRIGHT_PS,
                          "tf": {}, "tfc": {},
                          "pfull": pfull({31: (1.4865, 0, 0, 0), 46: (0.2, 0, 0, 0), 47: (2.0, 0, 0, 0),
                                          77: (0.0869, 1.0 / 8.0, 0, 0)})})
            comp = {"kind": "verts", "frame": n, "in_scene": False, "ps_hash": "0x" + pt.COMPOSITE_PS,
                    "tf": {"1": fc_hex(pt.TABLE_FC)}, "tfc": {"1": "0x%016X" % content},
                    "pfull": pfull({31: (2.973, 0, 0, 0), 46: (0.25, 0, 0, 0), 47: (50.0, 0, 0, 0),
                                    77: (-0.0052, 1.1026, 0, 0), 78: (0.45455, 0, 0, 0)})}
            texture = {"kind": "texture", "file": name, "bytes": 2048, "frame": n, "content": "0x%016X" % content,
                       "dim": 1, "fc": fc_hex(pt.TABLE_FC)}
            clear = type0(0x2318, 0x00300100, 0x12AE1000, 560 | (360 << 16), 2 | (6 << 7)) + type3(0x36, 0x00030088)
            resolve = {"kind": "resolve", "frame": n, "n": lum_resolves + 1, "args": ["0x00000001"] * 7,
                       "surface": "0x09000230", "cmd_ok": True, "from": "0xFF940000", "cmd": cmd_hex(clear)}
            failed = {"kind": "resolve", "frame": n, "n": lum_resolves + 2, "args": ["0x00000001"] * 7, "cmd_ok": False}
            frame_row = {"kind": "frame", "frame": n, "light": {"valid": 1, "out_scale": scale}}
            tail = [frame_row, texture, comp] if composite_after_frame else [texture, comp, resolve, failed, frame_row]
            lines += tail
        p = d / "native_discovery_x.jsonl"
        p.write_text("".join(json.dumps(x, separators=(",", ":")) + "\n" for x in lines), encoding="utf-8")
        return p

    def run_report(self, fn, *args):
        out = io.StringIO()
        with redirect_stdout(out):
            status = fn(*args)
        return status, out.getvalue()

    def test_load_capture(self):
        with tempfile.TemporaryDirectory() as d:
            cap = pt.load_capture(self.write(d))
            self.assertEqual(sorted(cap.frames), [1, 2])
            fr = cap.frames[1]
            self.assertEqual(fr.out_scale, 8.0)
            self.assertEqual(fr.out_scale_from, "the frame row's light block")
            self.assertEqual(len(fr.rows[pt.COMPOSITE_PS]), 1)
            self.assertLess(fr.row_lines[pt.COMPOSITE_PS][0], fr.frame_line)
            res = pt.frame_resolves(fr)
            self.assertEqual([(e["copy"]["base"], last[1]) for e, last, _ in res],
                             [(self.A, pt.LUMINANCE_PS), (0x12AE1000, pt.COMPOSITE_PS)])
            self.assertEqual([src for _, _, src in res], ["cmdgap 1", "resolve 2"])
            table, fc, content, why = pt.frame_table(cap, fr)
            self.assertIsNone(why)
            self.assertAlmostEqual(table[128], 0.2 + 0.0128 + 0.01, places=6)
            self.assertEqual(tuple(fc), pt.TABLE_FC)

    def test_resolves_names_the_luminance_resolve(self):
        with tempfile.TemporaryDirectory() as d:
            status, text = self.run_report(pt.report_resolves, pt.load_capture(self.write(d)))
            self.assertEqual(status, 0)
            self.assertIn("outcome (i): one resolve after the luminance pass in each of 2 frames, one base: A = 0x12D69000", text)
            self.assertIn("pitch 320 height 180 format 36", text)
            self.assertIn("the SDK's forced read-back takes it (PM4_DRAW_INDX_2, rectangles, 3 indices): yes", text)
            self.assertIn("no other resolve has its base in [A, A + 64 KiB)", text)
            self.assertIn('rows: 4 "resolve" (2 with dwords), 2 "cmdgap"', text)

    def test_resolves_fails_without_one_base(self):
        with tempfile.TemporaryDirectory() as d:
            # The base moves from frame to frame (the game's: two addresses in turn): each is named.
            status, text = self.run_report(pt.report_resolves, pt.load_capture(self.write(d, base_step=0x36000)))
            self.assertEqual(status, 0)
            self.assertIn("outcome (i), with a ring: one resolve after the luminance pass in each of 2 frames, "
                          "2 bases: 0x12D69000, 0x12D9F000", text)
            self.assertIn("by frame: 1:12D69000 2:12D9F000", text)
            self.assertIn("A = 0x12D69000 in 1 frames", text)
            self.assertIn("A = 0x12D9F000 in 1 frames", text)
            self.assertNotIn("NOT reached", text)
        with tempfile.TemporaryDirectory() as d:
            # No resolve after the luminance pass at all.
            status, text = self.run_report(pt.report_resolves, pt.load_capture(self.write(d, lum_resolves=0)))
            self.assertEqual(status, 1)
            self.assertIn("after the luminance pass 5821: 0 resolves, bases none", text)
        with tempfile.TemporaryDirectory() as d:
            # Two resolves after it, 4 KiB apart: not one.
            status, text = self.run_report(pt.report_resolves, pt.load_capture(self.write(d, lum_resolves=2)))
            self.assertEqual(status, 1)

    def test_resolves_lists_a_neighbour_within_64_kib(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d, frames=1)
            near = type0(0x2318, 0, self.A + 0x8000, COPY_PITCH, COPY_INFO) + type3(0x36, 0x00030088)
            far = type0(0x2318, 0, self.A + 0x10000, COPY_PITCH, COPY_INFO) + type3(0x36, 0x00030088)
            rows = p.read_text(encoding="utf-8").splitlines()
            extra = [json.dumps({"kind": "cmdgap", "frame": 1, "n": 9, "func": "f", "dwords": len(x), "cmd": cmd_hex(x)},
                                separators=(",", ":")) for x in (near, far)]
            p.write_text("\n".join(rows[:-1] + extra + rows[-1:]) + "\n", encoding="utf-8")
            status, text = self.run_report(pt.report_resolves, pt.load_capture(p))
            self.assertEqual(status, 0)
            self.assertIn("another resolve within 64 KiB above A: 0x12D71000, 1 times", text)
            self.assertNotIn("0x12D79000,", text)

    def test_table_report(self):
        with tempfile.TemporaryDirectory() as d:
            status, text = self.run_report(pt.report_table, pt.load_capture(self.write(d)))
            self.assertEqual(status, 0)
            self.assertIn("frame 1 c20.w 8.00000 (the frame row's light block)", text)
            self.assertIn("c77.y * c20.w 1.000000 |", text)
            self.assertIn("MakePostBlock's rules: all hold", text)
            self.assertIn("change from the previous frame's table: max |d| 0.01", text)
            self.assertIn("the composite's row precedes the frame row of the same frame in 2 of 2 frames", text)
            self.assertIn("T(3) / T(0.1)", text)

    def test_table_report_says_when_the_pairing_or_the_order_fails(self):
        with tempfile.TemporaryDirectory() as d:
            status, text = self.run_report(pt.report_table, pt.load_capture(self.write(d, scale=8.1)))
            self.assertIn("(OFF 1 by more than 1e-3)", text)
            self.assertIn("largest |c77.y * c20.w - 1| over the frames: 0.0125", text)
        with tempfile.TemporaryDirectory() as d:
            status, text = self.run_report(pt.report_table, pt.load_capture(self.write(d, composite_after_frame=True)))
            self.assertIn("composite row NOT before the frame row", text)
            self.assertIn("in 0 of 2 frames", text)

    def test_out_scale_falls_back_to_the_first_scene_row(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d, frames=1)
            rows = [json.loads(x) for x in p.read_text(encoding="utf-8").splitlines()]
            for r in rows:
                if r["kind"] == "frame":
                    r.pop("light")
            p.write_text("".join(json.dumps(x, separators=(",", ":")) + "\n" for x in rows), encoding="utf-8")
            cap = pt.load_capture(p)
            self.assertEqual(cap.frames[1].out_scale, 8.0)
            self.assertEqual(cap.frames[1].out_scale_from, "c20.w of the first main-scene row")


if __name__ == "__main__":
    unittest.main()
