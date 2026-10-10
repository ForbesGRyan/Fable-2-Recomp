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

    def write(self, d, frames=2, lum_resolves=1, base_step=0, base_ring=0, composite_after_frame=False, scale=8.0):
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
                step = (n - 1) % base_ring if base_ring else n - 1
                gap = (type0(0x2208, 6) + type0(0x2318, 0, self.A + base_step * step + 0x1000 * k, COPY_PITCH, COPY_INFO)
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

    def test_resolves_names_a_ring(self):
        with tempfile.TemporaryDirectory() as d:
            # The base moves from frame to frame and comes back (the game's: two addresses in turn): a ring,
            # each base named.
            status, text = self.run_report(pt.report_resolves,
                                           pt.load_capture(self.write(d, frames=4, base_step=0x36000, base_ring=2)))
            self.assertEqual(status, 0)
            self.assertIn("outcome (i), with a ring: one resolve after the luminance pass in each of 4 frames, "
                          "2 bases in turn: 0x12D69000, 0x12D9F000", text)
            self.assertIn("by frame: 1:12D69000 2:12D9F000 3:12D69000 4:12D9F000", text)
            self.assertIn("A = 0x12D69000 in 2 frames", text)
            self.assertIn("A = 0x12D9F000 in 2 frames", text)
            self.assertNotIn("NOT reached", text)

    def test_resolves_fails_on_bases_without_a_period(self):
        with tempfile.TemporaryDirectory() as d:
            # Another base in every frame: no ring, whatever the number of bases.
            status, text = self.run_report(pt.report_resolves, pt.load_capture(self.write(d, frames=4, base_step=0x36000)))
            self.assertEqual(status, 1)
            self.assertIn("4 bases in 4 frames that do not repeat with a period", text)
            self.assertIn("outcome (i) NOT reached", text)
        with tempfile.TemporaryDirectory() as d:
            # Two frames, two bases: no base was seen twice, so no period was seen.
            status, text = self.run_report(pt.report_resolves, pt.load_capture(self.write(d, base_step=0x36000)))
            self.assertEqual(status, 1)
            self.assertIn("2 bases in 2 frames that do not repeat with a period", text)

    def test_ring_period(self):
        self.assertEqual(pt.ring_period([5, 5, 5]), 1)
        self.assertEqual(pt.ring_period([5]), 1)
        self.assertEqual(pt.ring_period([1, 2, 1, 2, 1]), 2)
        self.assertEqual(pt.ring_period([1, 2, 3, 1, 2, 3]), 3)
        self.assertIsNone(pt.ring_period([1, 2]))             # nothing seen twice
        self.assertIsNone(pt.ring_period([1, 2, 3, 1, 2]))    # the third base was seen once
        self.assertIsNone(pt.ring_period([1, 2, 1, 1]))
        self.assertIsNone(pt.ring_period([1, 2, 3, 4]))
        self.assertIsNone(pt.ring_period([]))

    def test_resolves_counts_the_stretches_not_looked_at(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d, frames=1)
            rows = p.read_text(encoding="utf-8").splitlines()
            extra = [json.dumps({"kind": "cmdgap", "frame": 1, "n": 9 + i, "func": "f", "skipped": why, "from": "0xFF940004",
                                 "to": "0xFF949000", "dwords": 9216}, separators=(",", ":"))
                     for i, why in enumerate(("long", "long", "back", "unreadable"))]
            p.write_text("\n".join(rows[:-1] + extra + rows[-1:]) + "\n", encoding="utf-8")
            status, text = self.run_report(pt.report_resolves, pt.load_capture(p))
            self.assertEqual(status, 0)
            # They are no "cmdgap" rows with dwords.
            self.assertIn('rows: 2 "resolve" (1 with dwords), 1 "cmdgap"', text)
            self.assertIn("frame 1: 2 resolves in 2 rows with command dwords", text)
            self.assertIn("stretches not looked at (a resolve issued inside one is in no row): 4 "
                          "(back 1, long 2, unreadable 1), in 1 of 1 frames", text)
            self.assertIn("no other resolve has its base in [A, A + 64 KiB) among the stretches the capture holds", text)
        with tempfile.TemporaryDirectory() as d:
            status, text = self.run_report(pt.report_resolves, pt.load_capture(self.write(d)))
            self.assertIn("stretches not looked at (a resolve issued inside one is in no row): none recorded "
                          "(a capture made before the \"skipped\" rows existed does not record them)", text)

    def test_resolves_fails_without_one_resolve_a_frame(self):
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


BLUR_X = ((0.2, 0.18, 0.12, 0.06), (0.03, 0.008, 0.002, 0.0))      # 0.2 + 2 * 0.4 = 1
BLUR_Y = ((0.18, 0.165, 0.12, 0.07), (0.035, 0.015, 0.005, 0.0))   # 0.18 + 2 * 0.41 = 1


class BlurTest(unittest.TestCase):
    def rows(self, x=BLUR_X, y=BLUR_Y):
        return {"pfull": pfull({46: x[0], 47: x[1]})}, {"pfull": pfull({46: y[0], 47: y[1]})}

    def test_blur_registers_are_the_centre_and_six_taps(self):
        bx, _ = self.rows()
        got = pt.blur_registers(bx)
        self.assertEqual(len(got), 7)
        for a, b in zip(got, (0.2, 0.18, 0.12, 0.06, 0.03, 0.008, 0.002)):
            self.assertAlmostEqual(a, b, places=7)
        self.assertIsNone(pt.blur_registers(None))
        self.assertIsNone(pt.blur_registers({"pfull": "-" * 32 * 256}))

    def test_blur_part_is_makepostblocks_rule(self):
        self.assertTrue(pt.blur_part(*self.rows()))
        self.assertFalse(pt.blur_part(self.rows()[0], None))               # both blurs
        self.assertFalse(pt.blur_part(None, self.rows()[1]))
        off = ((0.203, 0.18, 0.12, 0.06), BLUR_X[1])                       # the sum 0.003 off
        self.assertFalse(pt.blur_part(*self.rows(x=off)))
        near = ((0.2005, 0.18, 0.12, 0.06), BLUR_X[1])                     # within 1e-3
        self.assertTrue(pt.blur_part(*self.rows(x=near)))
        negative = ((0.2, 0.18, 0.12, 0.06), (0.04, -0.002, 0.002, 0.0))   # sums to 1 with a weight below 0
        self.assertFalse(pt.blur_part(*self.rows(y=negative)))
        above = ((1.5, 0.0, 0.0, 0.0), (-0.25, 0.0, 0.0, 0.0))
        self.assertFalse(pt.blur_part(*self.rows(x=above)))
        nan = ((float("nan"), 0.18, 0.12, 0.06), BLUR_X[1])
        self.assertFalse(pt.blur_part(*self.rows(x=nan)))
        w = ((0.2, 0.18, 0.12, 0.06), (0.03, 0.008, 0.002, 7.0))           # c47.w is no tap's weight
        self.assertTrue(pt.blur_part(*self.rows(x=w)))


class BlockTest(unittest.TestCase):
    """CaptureTest's hand-made capture with the blurs' rows and, in each frame row, the "post" member a build
    with plan Task 4 writes, made of the same values."""

    def write(self, d, change=None, blur=True, drop_post=False):
        base = CaptureTest("test_load_capture")
        p = base.write(d)
        rows = [json.loads(x) for x in p.read_text(encoding="utf-8").splitlines()]
        out = []
        for r in rows:
            if r["kind"] == "verts" and r["ps_hash"] == "0x" + pt.COMPOSITE_PS and blur:
                for ps, w in ((pt.BLUR_X_PS, BLUR_X), (pt.BLUR_Y_PS, BLUR_Y)):
                    out.append({"kind": "verts", "frame": r["frame"], "in_scene": False, "ps_hash": "0x" + ps, "tf": {},
                                "tfc": {}, "pfull": pfull({46: w[0], 47: w[1]})})
            if r["kind"] == "frame" and not drop_post:
                n = r["frame"]
                table = [0.0] + [0.2 + 0.0001 * i + 0.01 * n for i in range(1, 512)]
                post = {"on": 1, "valid": 1, "reason": "none", "age": 0, "tries": 0, "parts": 1 if blur else 0,
                        "composite": [2.973, 0.25, 50.0, -0.0052, 1.1026, 0.45455],
                        "bright": [1.4865, 0.2, 2.0, 0.0869, 0.125],
                        "blur": [x for w in (BLUR_X, BLUR_Y) for x in (w[0] + w[1][:3])] if blur else [0.0] * 14,
                        "table": {"base": "0x1FAA0000", "content": "0x%016X" % (0x1000 + n)}, "texels": table}
                if change:
                    change(n, post)
                r["post"] = post
            out.append(r)
        p.write_text("".join(json.dumps(x, separators=(",", ":")) + "\n" for x in out), encoding="utf-8")
        return p

    def report(self, d, **kw):
        out = io.StringIO()
        with redirect_stdout(out):
            status = pt.report_block(pt.load_capture(self.write(d, **kw)))
        return status, out.getvalue()

    def test_a_block_equal_to_its_rows(self):
        with tempfile.TemporaryDirectory() as d:
            status, text = self.report(d)
            self.assertEqual(status, 0, text)
            self.assertIn("frame 1: post block equal to the rows: age 0, 6 + 5 registers, 14 blur weights (parts 1), "
                          "table 0x1FAA0000 / 0000000000001001, 512 texels equal to the dump's, c77.y * c20.w 1.000000", text)
            self.assertIn("post block valid, at age 0 and equal to the frame's rows and dump in 2 of 2 frames", text)

    def test_a_block_without_the_blurs_rows(self):
        with tempfile.TemporaryDirectory() as d:
            status, text = self.report(d, blur=False)
            self.assertEqual(status, 0, text)
            self.assertIn("no blur weights (parts 0: 0 and 0 blur rows)", text)

    def test_a_capture_from_before_the_block(self):
        with tempfile.TemporaryDirectory() as d:
            status, text = self.report(d, drop_post=True)
            self.assertEqual(status, 1)
            self.assertIn("frame 1: no post block in the frame row", text)
            self.assertIn("in 0 of 2 frames", text)

    def faults(self, change, **kw):
        with tempfile.TemporaryDirectory() as d:
            status, text = self.report(d, change=change, **kw)
            self.assertEqual(status, 1, text)
            self.assertIn("in 1 of 2 frames", text)
            return text

    def test_a_register_one_bit_off(self):
        def change(n, post):
            if n == 2:
                bits = struct.unpack(">I", struct.pack(">f", post["composite"][4]))[0] + 1
                post["composite"][4] = struct.unpack(">f", struct.pack(">I", bits))[0]
        self.assertIn("frame 2: POST composite: block", self.faults(change))

        def bright(n, post):
            if n == 1:
                post["bright"][3] = 0.087
        self.assertIn("frame 1: POST bright: block", self.faults(bright))

    def test_a_texel_or_the_content_differs(self):
        def texel(n, post):
            if n == 1:
                post["texels"][300] += 1e-6
        self.assertIn("frame 1: POST table: 512 texels, the dump 512; 1 differ (the first: texel 300", self.faults(texel))

        def content(n, post):
            if n == 2:
                post["table"]["content"] = "0x0000000000000BAD"
        self.assertIn("frame 2: POST table: the block read 0x1FAA0000 / 0000000000000BAD, the composite's row names "
                      "0x1FAA0000 / 0000000000001002", self.faults(content))

        def short(n, post):
            if n == 2:
                post["texels"] = post["texels"][:511]
        self.assertIn("frame 2: POST table: 511 texels, the dump 512", self.faults(short))

    def test_an_aged_or_an_invalid_block(self):
        def aged(n, post):
            if n == 2:
                post["age"] = 1
        self.assertIn("frame 2: POST age 1: not the frame's own block", self.faults(aged))

        def invalid(n, post):
            if n == 1:
                post.clear()
                post.update({"on": 1, "valid": 0, "reason": "table-texel", "age": 0, "tries": 3, "parts": 0})
        self.assertIn("frame 1: POST block invalid(table-texel), on 1, tries 3", self.faults(invalid))

    def test_the_blur_part(self):
        def weight(n, post):
            if n == 1:
                post["blur"][9] = 0.12001
        self.assertIn("frame 1: POST blur: block", self.faults(weight))

        def parts(n, post):
            if n == 2:
                post["parts"] = 0
                post["blur"] = [0.0] * 14
        self.assertIn("frame 2: POST parts 0, but the blurs' rows give the part by MakePostBlock's rule", self.faults(parts))

        def claimed(n, post):
            if n == 2:
                post["parts"] = 1
        self.assertIn("frame 2: POST parts 1, but the blurs' rows do not give the part", self.faults(claimed, blur=False))

    def test_the_pairing(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.write(d)
            rows = [json.loads(x) for x in p.read_text(encoding="utf-8").splitlines()]
            for r in rows:
                if r["kind"] == "frame" and r["frame"] == 1:
                    r["light"]["out_scale"] = 8.1
            p.write_text("".join(json.dumps(x, separators=(",", ":")) + "\n" for x in rows), encoding="utf-8")
            out = io.StringIO()
            with redirect_stdout(out):
                status = pt.report_block(pt.load_capture(p))
            self.assertEqual(status, 1)
            self.assertIn("frame 1: POST pairing: c77.y * c20.w is 1.012500", out.getvalue())


# --- the sun layer's graph, the furniture's census, the occluders, the sign fields (plan Task 10) ------------------

def fc2d(base, w, h, fmt, tiled=True, signs=(0, 0, 0, 0), endian=2):
    """A 2D texture fetch constant as the rows hold it (six hex words)."""
    d0 = 2 | (0x80000000 if tiled else 0) | sum(s << (2 + 2 * i) for i, s in enumerate(signs))
    words = [d0, base | fmt | (endian << 6), (w - 1) | ((h - 1) << 13), 0x00A80D10, 0, 1 << 9]
    return ["0x%08X" % x for x in words]


def row(name, line, reads=(), pitch=560, in_scene=False):
    return {"type": "row", "line": line, "name": name, "pitch": pitch, "in_scene": in_scene,
            "reads": [(slot, pt.fetch_texture([int(x, 16) for x in fc])) for slot, fc in reads]}


def resolve(line, base, pitch, height, fmt):
    return {"type": "resolve", "line": line, "base": base, "pitch": pitch, "height": height, "format": fmt, "source": 0}


def clear(line, mode=4, colour_format=0):
    return {"type": "clear", "line": line, "mode": mode, "colour_format": colour_format}


A, B, C, D, E = 0x12AE1000, 0x12CFD000, 0x12CC7000, 0x12BB9000, 0x12D9F000

# The sun layer as the captures have it (frame-map section 17, "Sun layer discovery"), bases as there.
LAYER_EVENTS = [
    row("beam-source", 10, [(13, fc2d(0x14DE0000, 256, 256, 20))]),
    resolve(11, A, 576, 360, 7),
    clear(12), clear(13, mode=5),
    resolve(14, B, 576, 360, 6),
    row("reduce", 15, [(0, fc2d(B, 560, 360, 6))], pitch=320),
    resolve(16, C, 288, 180, 6),
    clear(17, colour_format=2), clear(18, mode=5, colour_format=2),
    row("flare", 19), row("moon-glow", 20), row("sun-disc", 21),
    resolve(22, B, 576, 360, 7),
    row("reduce", 23, [(0, fc2d(B, 560, 360, 54))], pitch=320),
    resolve(24, D, 288, 180, 7),
    row("reduce", 25, [(0, fc2d(A, 560, 360, 54))], pitch=320),
    resolve(26, B, 288, 180, 7),
    row("radial", 27, [(0, fc2d(B, 280, 180, 7))], pitch=320),
    resolve(28, B, 288, 180, 7),
    row("radial", 29, [(0, fc2d(B, 280, 180, 7))], pitch=320),
    resolve(30, B, 288, 180, 7),
    row("bright", 31, [(0, fc2d(0x12D33000, 280, 180, 29)), (1, fc2d(B, 280, 180, 54)), (2, fc2d(D, 280, 180, 54))], pitch=320),
    resolve(32, E, 288, 180, 7),
    row("composite", 33, [(2, fc2d(E, 280, 180, 54))], pitch=1280),
]


class FetchTextureTest(unittest.TestCase):
    def test_a_2d_constant(self):
        t = pt.fetch_texture([int(x, 16) for x in fc2d(0x12CFD000, 560, 360, 54, signs=(3, 3, 3, 0))])
        self.assertEqual((t["base"], t["width"], t["height"], t["format"], t["tiled"], t["signs"]),
                         (0x12CFD000, 560, 360, 54, True, (3, 3, 3, 0)))

    def test_a_1d_constant(self):
        t = pt.fetch_texture(list(pt.TABLE_FC))
        self.assertEqual((t["base"], t["width"], t["height"], t["format"], t["tiled"], t["signs"]),
                         (0x1FAA0000, 512, 1, 36, False, (1, 1, 1, 1)))


class Packed10Test(unittest.TestCase):
    def test_the_three_fields(self):
        # r in bits 0 to 9, g in 10 to 19, b in 20 to 29 of the word as the GPU reads it; stored 8-in-32.
        word = 1023 | (512 << 10) | (1 << 20) | (3 << 30)
        self.assertEqual(pt.packed10(word), (1.0, 512 / 1023.0, 1 / 1023.0))
        self.assertEqual(pt.packed10(0), (0.0, 0.0, 0.0))

    def test_a_linear_picture(self):
        words = [1023, 1023 << 10, 1023 << 20] + [0] * 29     # one row of the pitch, 32 texels
        data = struct.pack(">64I", *(words + words))
        fc = [int(x, 16) for x in fc2d(0x12CFD000, 3, 2, 7, tiled=False)]
        w, h, texels = pt.decode_packed10(data, fc)
        self.assertEqual((w, h), (3, 2))
        self.assertEqual(texels[:3], [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)])
        self.assertEqual(texels[3:], texels[:3])

    def test_another_format_or_too_few_bytes_is_refused(self):
        with self.assertRaises(ValueError):
            pt.decode_packed10(bytes(256), [int(x, 16) for x in fc2d(0x12CFD000, 3, 2, 6, tiled=False)])
        with self.assertRaises(ValueError):
            pt.decode_packed10(bytes(8), [int(x, 16) for x in fc2d(0x12CFD000, 3, 2, 7, tiled=False)])


class StreamStepsTest(unittest.TestCase):
    def test_a_clear_a_resolve_and_an_indirect_buffer(self):
        d = (type0(0x2000, 0x09000230, 0x00000000) + type0(0x2208, 4) + type3(0x36, 0x00030088)
             + type3(0x3F, 0x1FA98980, 0x30) + RESOLVE)
        steps = pt.stream_steps(d)
        self.assertEqual([s["type"] for s in steps], ["draw", "indirect", "resolve"])
        self.assertEqual((steps[0]["mode"], steps[0]["colour_format"], steps[0]["surface"]), (4, 0, 0x09000230))
        self.assertEqual((steps[1]["address"], steps[1]["dwords"]), (0x1FA98980, 0x30))
        self.assertEqual((steps[2]["base"], steps[2]["pitch"], steps[2]["height"], steps[2]["format"]),
                         (0x12D69000, 320, 180, 36))

    def test_the_colour_format_is_of_the_draw_not_of_the_run(self):
        d = (type0(0x2001, 0x00020000) + type0(0x2208, 4) + type3(0x36, 0x00030088)
             + type0(0x2001, 0x00000000) + type3(0x36, 0x00030088))
        self.assertEqual([s["colour_format"] for s in pt.stream_steps(d)], [2, 0])

    def test_a_run_without_the_registers_says_none(self):
        steps = pt.stream_steps(type3(0x36, 0x00030088))
        self.assertEqual((steps[0]["type"], steps[0]["mode"], steps[0]["colour_format"]), ("draw", None, None))


class GraphTest(unittest.TestCase):
    def setUp(self):
        self.nodes = pt.build_graph(LAYER_EVENTS)

    def node_writing(self, line):
        return next(n for n in self.nodes if n["dest"] and n["dest"]["line"] == line)

    def test_every_resolve_ends_a_node_and_the_rest_is_the_last(self):
        self.assertEqual(len(self.nodes), 10)   # nine resolves and the composite after the last
        self.assertIsNone(self.nodes[-1]["dest"])
        self.assertEqual([d["name"] for d in self.nodes[-1]["draws"]], ["composite"])

    def test_several_draws_share_one_resolve(self):
        self.assertEqual([d["name"] for d in self.node_writing(22)["draws"]], ["flare", "moon-glow", "sun-disc"])

    def test_a_resolve_after_clears_alone_is_a_cleared_picture(self):
        n = self.node_writing(14)
        # One colour clear; the depth-only rectangle (mode 5) beside it is not one.
        self.assertEqual((n["draws"], len(n["clears"]), pt.node_kind(n)), ([], 1, "cleared"))

    def test_a_resolve_after_nothing_is_the_picture_again(self):
        nodes = pt.build_graph([row("beam-source", 1), resolve(2, A, 576, 360, 7), resolve(3, B, 576, 360, 6)])
        self.assertEqual([pt.node_kind(n) for n in nodes], ["drawn", "again"])
        self.assertEqual(nodes[1]["again"], 0)

    def test_a_depth_clear_alone_leaves_the_colour(self):
        nodes = pt.build_graph([row("beam-source", 1), resolve(2, A, 576, 360, 7), clear(3, mode=5), resolve(4, B, 576, 360, 6)])
        self.assertEqual(pt.node_kind(nodes[1]), "again")

    def test_draws_before_a_colour_clear_are_not_in_the_picture(self):
        # The layer's depth and its occluders, then the colour clear, then the beam source: one resolve.
        nodes = pt.build_graph([row("layer-depth", 1, [(13, fc2d(0x1A02A000, 1120, 720, 23))]), row("cube", 2), clear(3, mode=5),
                                clear(4), row("beam-source", 5), resolve(6, A, 576, 360, 7)])
        self.assertEqual(([d["name"] for d in nodes[0]["draws"]], [d["name"] for d in nodes[0]["earlier"]]),
                         (["beam-source"], ["layer-depth", "cube"]))
        self.assertEqual((pt.node_name(nodes[0]), nodes[0]["reads"], len(nodes[0]["earlier_reads"])), ("beam-source", [], 1))

    def test_a_scene_that_is_cleared_away_is_not_the_picture(self):
        nodes = pt.build_graph([row("ps 9C21", 1, in_scene=True, pitch=1120), clear(2), resolve(3, B, 576, 360, 6)])
        self.assertEqual(pt.node_kind(nodes[0]), "cleared")

    def test_a_read_names_the_last_writer_of_its_base_and_size(self):
        bright = self.node_writing(32)
        by_slot = {slot: producer for _, slot, _, producer, _ in bright["reads"]}
        self.assertEqual(self.nodes[by_slot[1]]["dest"]["line"], 30)   # the second radial blur
        self.assertEqual(self.nodes[by_slot[2]]["dest"]["line"], 24)   # the reduction of layer 2
        self.assertIsNone(by_slot[0])                                  # nothing in these events wrote 12D33000

    def test_a_read_of_another_size_than_the_last_write_is_said(self):
        nodes = pt.build_graph([row("x", 1), resolve(2, B, 576, 360, 7), row("reduce", 3, [(0, fc2d(B, 280, 180, 7))])])
        _, _, _, producer, note = nodes[1]["reads"][0]
        self.assertEqual((producer, note), (0, "the last resolve there is 576 x 360, the row reads 280 x 180"))

    def test_tf1_is_the_beams_and_tf2_the_second_layer(self):
        bright = self.node_writing(32)
        by_slot = {slot: producer for _, slot, _, producer, _ in bright["reads"]}
        self.assertEqual(pt.chain_names(self.nodes, by_slot[1]), ["radial", "radial", "reduce", "beam-source"])
        self.assertEqual(pt.chain_names(self.nodes, by_slot[2]), ["reduce", "flare + moon-glow + sun-disc"])

    def test_the_first_reduction_reads_the_cleared_picture_and_nobody_reads_it(self):
        c1 = self.node_writing(16)
        self.assertEqual(pt.chain_names(self.nodes, c1["index"]), ["reduce", "(cleared, nothing drawn)"])
        self.assertEqual([self.nodes[i]["dest"]["base"] for i in pt.unread_nodes(self.nodes)], [C])

    def test_exchanged_inputs_are_seen(self):
        events = list(LAYER_EVENTS)
        at = next(i for i, e in enumerate(events) if e.get("name") == "bright")
        events[at] = row("bright", 31, [(1, fc2d(D, 280, 180, 54)), (2, fc2d(B, 280, 180, 54))], pitch=320)
        nodes = pt.build_graph(events)
        bright = next(n for n in nodes if n["dest"] and n["dest"]["line"] == 32)
        by_slot = {slot: producer for _, slot, _, producer, _ in bright["reads"]}
        self.assertEqual(pt.chain_names(nodes, by_slot[1])[-1], "flare + moon-glow + sun-disc")
        self.assertEqual(pt.chain_names(nodes, by_slot[2])[-1], "beam-source")

    def test_main_scene_rows_are_counted_not_listed(self):
        nodes = pt.build_graph([row("ps 9C21", 1, in_scene=True, pitch=1120), row("ps 9C21", 2, in_scene=True, pitch=1120),
                                resolve(3, 0x1A02A000, 1120, 720, 6)])
        self.assertEqual((nodes[0]["draws"], nodes[0]["scene_rows"], pt.node_kind(nodes[0])), ([], 2, "drawn"))


def census_row(frame, role, **kw):
    r = {"frame": frame, "role": role, "kind": "indx", "func": "DrawIndx:82217EE8", "pitch": 560, "in_scene": False,
         "depth": 0x00708762, "blend": 0x01010101, "mask": 0xF, "colorcontrol": 0x4, "alpha_ref": 0x3F008081,
         "vertices": 4, "indices": 6}
    r.update(kw)
    return r


class FurnitureCensusTest(unittest.TestCase):
    def test_counts_by_frame_role_and_target(self):
        rows = [census_row(1, "flare"), census_row(1, "flare"), census_row(1, "flare", pitch=280), census_row(2, "flare")]
        c = pt.furniture_census(rows)
        self.assertEqual(c["counts"][("flare", 560, False)], {1: 2, 2: 1})
        self.assertEqual(c["counts"][("flare", 280, False)], {1: 1})

    def test_the_alpha_test_is_the_enable_bit_with_the_reference(self):
        rows = [census_row(1, "sun-disc", colorcontrol=0xC), census_row(2, "sun-disc", colorcontrol=0xC),
                census_row(1, "flare")]
        c = pt.furniture_census(rows)
        self.assertEqual(c["alpha"][("sun-disc", 560, False)], {(True, 4, 0x3F008081): [1, 2]})
        # Off: the reference register's stale word is not a state of the draw.
        self.assertEqual(c["alpha"][("flare", 560, False)], {(False, 4, None): [1]})

    def test_a_state_that_changes_between_frames_shows_as_two(self):
        rows = [census_row(1, "moon", colorcontrol=0xC, alpha_ref=0x3B808081), census_row(2, "moon", colorcontrol=0xC, alpha_ref=0x3C008081)]
        self.assertEqual(len(pt.furniture_census(rows)["alpha"][("moon", 560, False)]), 2)

    def test_another_builder_or_row_kind_is_named(self):
        rows = [census_row(1, "glare"), census_row(1, "glare", kind="verts", func="D3DDevice_BeginVertices?")]
        self.assertEqual(pt.furniture_census(rows)["builders"]["glare"],
                         {("indx", "DrawIndx:82217EE8"): 1, ("verts", "D3DDevice_BeginVertices?"): 1})

    def test_the_words(self):
        rows = [census_row(1, "glare", depth=0x00708760), census_row(2, "glare", depth=0x00708762)]
        self.assertEqual(sorted(pt.furniture_census(rows)["words"][("glare", 560, False)]),
                         [(0x00708760, 0x01010101, 0xF), (0x00708762, 0x01010101, 0xF)])

    def test_the_shape(self):
        rows = [census_row(1, "glare"), census_row(1, "cube", vertices=8, indices=30)]
        c = pt.furniture_census(rows)
        self.assertEqual(c["shapes"]["glare"], {(4, 6): 1})
        self.assertEqual(c["shapes"]["cube"], {(8, 30): 1})


def draw_row(line, vs, ps, key, pitch=1120, in_scene=True, count=100):
    return {"line": line, "vs": vs, "ps": ps, "buffers": key, "pitch": pitch, "in_scene": in_scene, "indices": count}


class OccluderTest(unittest.TestCase):
    def test_a_tree_occluder_is_matched_by_its_buffers(self):
        rows = [draw_row(10, "A584", "7CD5", (1, 300, 2)), draw_row(20, "A584", "7CD5", (3, 500, 4)),
                draw_row(30, "A584", "7CD5", (5, 700, 6)),
                draw_row(40, "4325", "F1EB", (5, 700, 6), pitch=560, in_scene=False),
                draw_row(41, "4325", "F1EB", (9, 9, 9), pitch=560, in_scene=False),
                draw_row(5, "4325", "F1EB", (1, 300, 2), pitch=1040, in_scene=False)]
        m = pt.match_tree_occluders(rows, layer_pitch=560, depth_copy_line=25)
        self.assertEqual([(o["line"], [r["line"] for r in same]) for o, same in m["matches"]], [(40, [30]), (41, [])])
        self.assertEqual((m["scene_leaf_before"], m["scene_leaf_after"]), (2, 1))
        self.assertEqual(m["after_all_matched"], True)

    def test_a_late_leaf_draw_without_an_occluder_is_seen(self):
        rows = [draw_row(30, "A584", "7CD5", (5, 700, 6)), draw_row(31, "A584", "7CD5", (7, 700, 8))]
        m = pt.match_tree_occluders(rows + [draw_row(40, "4325", "F1EB", (5, 700, 6), pitch=560, in_scene=False)],
                                    layer_pitch=560, depth_copy_line=25)
        self.assertEqual(m["after_all_matched"], False)

    def test_no_depth_copy_line_means_no_before_and_after(self):
        m = pt.match_tree_occluders([draw_row(30, "A584", "7CD5", (5, 700, 6))], layer_pitch=560, depth_copy_line=None)
        self.assertEqual((m["scene_leaf_before"], m["scene_leaf_after"], m["after_all_matched"]), (None, None, None))


class SignCensusTest(unittest.TestCase):
    def test_fields_that_are_not_zero_are_counted_by_kind_slot_and_format(self):
        tess = {"kind": "tess", "tf": {"3": fc2d(0x17000000, 512, 512, 18, signs=(3, 3, 3, 0)), "0": fc2d(0x17100000, 64, 64, 18)}}
        post = {"kind": "verts", "tf": {"1": ["0x%08X" % x for x in pt.TABLE_FC]}}
        other = {"kind": "draw", "tf": {"2": fc2d(0x18000000, 64, 64, 6, signs=(0, 2, 0, 0))}}
        c = pt.sign_census([tess, tess, post, other])
        self.assertEqual(c["textures"], 6)
        self.assertEqual(c["fields"][("tess", 3, 18, (3, 3, 3, 0))], 2)
        self.assertEqual(c["fields"][("verts", 1, 36, (1, 1, 1, 1))], 1)
        self.assertEqual(c["fields"][("draw", 2, 6, (0, 2, 0, 0))], 1)
        self.assertEqual(len(c["fields"]), 3)

    def test_known_and_unknown(self):
        # Known: the terrain's base map (tess slot 3, gamma on r g b: the pass applies the curve since 5d0f974) and
        # the float formats with all four fields 1, whose values are their own whatever the fields say. Anything
        # else is to be reported.
        self.assertTrue(pt.sign_known("tess", 3, 18, (3, 3, 3, 0)))
        self.assertTrue(pt.sign_known("draw", 4, 29, (1, 1, 1, 1)))
        self.assertTrue(pt.sign_known("verts", 1, 36, (1, 1, 1, 1)))
        self.assertFalse(pt.sign_known("tess", 0, 18, (3, 3, 3, 0)))
        self.assertFalse(pt.sign_known("tess", 3, 18, (3, 3, 3, 3)))
        self.assertFalse(pt.sign_known("draw", 2, 6, (0, 2, 0, 0)))
        self.assertFalse(pt.sign_known("draw", 2, 6, (1, 1, 1, 1)))

    def test_exponent_adjust_is_counted(self):
        fc = fc2d(0x17000000, 512, 512, 18)
        fc[3] = "0x%08X" % (int(fc[3], 16) | (2 << 13))
        self.assertEqual(pt.sign_census([{"kind": "tess", "tf": {"3": fc}}])["exp"], {("tess", 3, 18, 2): 1})


# --- quads --check: the capture's quad records against the frame's rows (spec criterion 17) ---------------------

QUAD_KINDS = {"moon": ("18D39DCEBEE4C2FC", "81BADE63A169E3DC", 0, (20, 31)),
              "cloud": ("61976295517D7015", "C304C6A1AF18B46F", 0, (20, 31, 46, 47, 51, 69, 70, 71, 77, 78, 79)),
              "glare": ("04985A0F7296E130", "81BADE63A169E3DC", 0, (31,)),
              "beam-source": ("8E4DBB9E9A63EF67", "81BADE63A169E3DC", 1, (31,)),
              "flare": ("D72FD0903CCE7DD6", "81BADE63A169E3DC", 1, (31,)),
              "moon-glow": ("4B58837D42F08C89", "81BADE63A169E3DC", 1, ()),
              "sun-disc": ("3E77B1AD4BC5B900", "81BADE63A169E3DC", 1, (20, 31)),
              "cloud-occluder": ("3FF79524FCDB71C5", "C304C6A1AF18B46F", 1, (31, 46, 47, 77))}
QUAD_FC = fc_hex((0x82000002, 0x14E80054, 0x001FE0FF, 0x00A80D10, 0x00000103, 0x14E90A18))


def f32_hex(values):
    return "".join(struct.pack(">f", v).hex().upper() for v in values)


def quad_row(kind, n, mark=1.0, in_scene=None, pitch=None, colorcontrol=0x4, alpha_ref=0x3F008081, blend=0x01010101,
             depth=0x00708762, mask=0xF, func="DrawIndx:82217EE8", idx=(0, 1, 2, 1, 3, 2)):
    """An "indx" row of a furniture quad with its "indxdata" row under "data", as post_tables reads a frame's rows:
    four corners told apart by `mark`, registers c<r> = (r + mark, 2, 3, 4), the matrix 1..16 + mark."""
    ps, vs, layer, registers = QUAD_KINDS[kind]
    in_scene = (layer == 0) if in_scene is None else in_scene
    pitch = (1120 if layer == 0 else 560) if pitch is None else pitch
    corners = [float(mark + 10 * c + k) for c in range(4) for k in range(5)]
    return {"kind": "indx", "frame": 1, "func": func, "n": n, "in_scene": in_scene, "ps_hash": "0x" + ps,
            "vs": {"hash": "0x" + vs}, "surface_info": "0x%08X" % (0x09000000 | pitch), "vertices": 4, "indices": 6, "stride": 20,
            "bank": [float(i + 1 + mark) for i in range(16)] + [0.0] * 1008,
            "pfull": pfull({r: (r + mark, 2.0, 3.0, 4.0) for r in registers}), "tf": {"13": list(QUAD_FC)},
            "rs": {"2104": "0x%08X" % mask, "210E": "0x%08X" % alpha_ref, "2200": "0x%08X" % depth, "2201": "0x%08X" % blend,
                   "2202": "0x%08X" % colorcontrol},
            "data": {"kind": "indxdata", "frame": 1, "n": n, "stride": 20, "vbytes": 80, "verts": f32_hex(corners),
                     "idx": list(idx)}}


def quad_record(row, seq):
    """The record a build with plan Task 12 writes for `row` (the frame row's "quads"."records" entry), made by
    hand of the row's values."""
    kind = next(k for k, v in QUAD_KINDS.items() if "0x" + v[0] == row["ps_hash"])
    ps, vs, layer, registers = QUAD_KINDS[kind]
    regs = "".join(row["pfull"][32 * r:32 * r + 32] for r in registers) + "00000000" * (4 * (12 - len(registers)))
    control = int(row["rs"]["2202"], 16) & 0xF
    return {"kind": kind, "layer": layer, "seq": seq, "ps": "0x" + ps, "vs": "0x" + vs, "corners": row["data"]["verts"],
            "view_proj": f32_hex(row["bank"][:16]), "regs": regs,
            "fetch": [list(QUAD_FC), ["0x00000000"] * 6, ["0x00000000"] * 6],
            "blend": row["rs"]["2201"], "depth": row["rs"]["2200"], "mask": int(row["rs"]["2104"], 16) & 0xF,
            "alpha_control": control, "alpha_ref": row["rs"]["210E"] if control & 8 else "0x00000000"}


def quads_of(rows, **kw):
    q = {"on": 1, "reason": "none", "count": len(rows), "seen": len(rows), "taken": len(rows), "over": 0,
         "records": [quad_record(r, i) for i, r in enumerate(rows)]}
    q.update(kw)
    return q


def other_row(kind="verts", ps=None, in_scene=False, pitch=560):
    return {"kind": kind, "frame": 1, "func": "D3DDevice_BeginVertices?" if kind == "verts" else "DrawIndx:82217EE8",
            "in_scene": in_scene, "ps_hash": "0x" + (ps or "0A96000000000000"), "surface_info": "0x%08X" % pitch}


class QuadCheckTest(unittest.TestCase):
    def frame(self):
        """A frame as the game issues it: the water's reflection views with the flare's shader at a quarter of the
        scene's pitch, the main scene with moon, clouds and the glares, the sun layer, the post rectangles, the HUD."""
        scene = [quad_row("moon", 35, 1.0, colorcontrol=0xC, alpha_ref=0x3B808081, blend=0x07060706),
                 quad_row("cloud", 36, 2.0, colorcontrol=0xC, alpha_ref=0x3CA0A0A1, blend=0x07060706, depth=0x00708766),
                 quad_row("glare", 43, 3.0, depth=0x00708760), quad_row("glare", 44, 4.0, depth=0x00708760)]
        layer = [quad_row("cloud-occluder", 47, 5.0, colorcontrol=0xC, alpha_ref=0x3E189899, blend=0x07060706, mask=0,
                          depth=0x00708766),
                 quad_row("beam-source", 53, 6.0, blend=0x00010001), quad_row("flare", 54, 7.0),
                 quad_row("sun-disc", 55, 8.0, colorcontrol=0xC)]
        rows = ([quad_row("flare", 5, 9.0, pitch=280), quad_row("flare", 6, 9.5, pitch=280)]
                + [other_row("indx", in_scene=True, pitch=1120)] + scene
                + [other_row()] + layer
                + [other_row(ps=pt.RADIAL_PS), other_row(ps=pt.COMPOSITE_PS, pitch=1280),
                   other_row("indx", ps="7B22000000000000", pitch=1280)])
        return rows, scene + layer

    def test_the_rows_the_capture_reads(self):
        rows, want = self.frame()
        self.assertEqual([r["n"] for r in pt.quad_rows_expected(rows)], [r["n"] for r in want])

    def test_a_layer_kind_is_read_at_half_the_scenes_pitch_only(self):
        rows, want = self.frame()
        # The reflection views' flares come before the scene; one at a quarter pitch after it is no layer quad
        # either, nor a scene kind outside the scene, nor a layer kind inside it.
        extra = [quad_row("flare", 60, 1.0, pitch=280), quad_row("glare", 61, 1.0, in_scene=False, pitch=560),
                 quad_row("moon", 62, 1.0, in_scene=False, pitch=1120), quad_row("flare", 63, 1.0, in_scene=True, pitch=1120)]
        at = next(i for i, r in enumerate(rows) if r.get("n") == 55) + 1
        self.assertEqual([r["n"] for r in pt.quad_rows_expected(rows[:at] + extra + rows[at:])], [r["n"] for r in want])
        # ... and nothing of the layer's before the frame's main scene has been seen, or after its composite.
        early = [quad_row("flare", 1, 1.0, pitch=560)]
        late = [quad_row("flare", 99, 1.0, pitch=560)]
        self.assertEqual([r["n"] for r in pt.quad_rows_expected(early + rows + late)], [r["n"] for r in want])
        # Half of another scene's pitch is that scene's layer.
        wide = [quad_row("moon", 1, 1.0, pitch=1280, colorcontrol=0xC), quad_row("flare", 2, 1.0, pitch=640),
                quad_row("flare", 3, 1.0, pitch=560)]
        self.assertEqual([r["n"] for r in pt.quad_rows_expected(wide)], [1, 2])

    def test_another_builder_is_not_read(self):
        rows, want = self.frame()
        rows.insert(4, quad_row("glare", 70, 1.0, func="D3DDevice_DrawIndexedVertices?"))
        self.assertEqual([r["n"] for r in pt.quad_rows_expected(rows)], [r["n"] for r in want])

    def test_records_equal_to_their_rows(self):
        rows, want = self.frame()
        self.assertEqual(pt.frame_quad_faults(quads_of(want), rows), [])

    def faults(self, change):
        rows, want = self.frame()
        quads = quads_of(want)
        change(quads, rows)
        return pt.frame_quad_faults(quads, rows)

    def test_one_bit_of_a_corner(self):
        def change(quads, rows):
            c = quads["records"][7]["corners"]
            quads["records"][7]["corners"] = c[:79] + ("0" if c[79] != "0" else "1") + c[80:]
        f = self.faults(change)
        self.assertEqual(len(f), 1)
        self.assertIn("record 7 (sun-disc, row n 55): corners", f[0])

    def test_a_register_the_matrix_a_fetch_constant_a_state_word(self):
        def reg(quads, rows):
            r = quads["records"][1]["regs"]
            quads["records"][1]["regs"] = r[:32 * 10 + 7] + ("0" if r[32 * 10 + 7] != "0" else "1") + r[32 * 10 + 8:]
        self.assertIn("regs: c79", self.faults(reg)[0])

        def padding(quads, rows):
            quads["records"][2]["regs"] = quads["records"][2]["regs"][:32] + "3F800000" + "00000000" * 43
        self.assertIn("regs: not zero after the kind's 1", self.faults(padding)[0])

        def matrix(quads, rows):
            quads["records"][0]["view_proj"] = "3F800001" + quads["records"][0]["view_proj"][8:]
        self.assertIn("view_proj", self.faults(matrix)[0])

        def fetch(quads, rows):
            quads["records"][3]["fetch"][0][1] = "0x14E80055"
        self.assertIn("fetch 0 (slot 13)", self.faults(fetch)[0])

        def unused(quads, rows):
            quads["records"][3]["fetch"][2][5] = "0x00000001"
        self.assertIn("fetch 2: not zero", self.faults(unused)[0])
        for key, value in (("blend", "0x07060706"), ("depth", "0x00708766"), ("mask", 7), ("alpha_control", 4),
                           ("alpha_ref", "0x3F000000"), ("layer", 0), ("seq", 9), ("vs", "0xC304C6A1AF18B46F")):
            def state(quads, rows, key=key, value=value):
                quads["records"][7][key] = value
            f = self.faults(state)
            self.assertEqual(len(f), 1, (key, f))
            self.assertIn(key, f[0])

    def test_the_alpha_test_off_has_reference_zero(self):
        # The stale register (0.502 at a glare's call) is not the record's.
        def stale(quads, rows):
            quads["records"][2]["alpha_ref"] = "0x3F008081"
        self.assertIn("alpha_ref", self.faults(stale)[0])

    def test_the_order_and_the_count(self):
        def swapped(quads, rows):
            r = quads["records"]
            r[2], r[3] = r[3], r[2]
        f = self.faults(swapped)
        self.assertTrue(any("record 2 (glare, row n 43)" in x for x in f), f)

        def missing(quads, rows):
            del quads["records"][6]
            quads["count"] = 7
        f = self.faults(missing)
        self.assertIn("7 records, 8 furniture rows", f[0])

        def another_kind(quads, rows):
            quads["records"][6]["kind"] = "sun-disc"
        self.assertIn("kind sun-disc, the row's shader is flare", self.faults(another_kind)[0])

    def test_a_frame_without_a_set(self):
        rows, want = self.frame()
        f = pt.frame_quad_faults({"on": 1, "reason": "pending", "count": 0, "seen": 8, "taken": 7, "over": 0}, rows)
        self.assertEqual(f, ["no set (pending, seen 8, taken 7, over 0) for 8 furniture rows"])
        f = pt.frame_quad_faults({"on": 0, "reason": "off", "count": 0, "seen": 0, "taken": 0, "over": 0}, rows)
        self.assertEqual(f, ["no set (off, seen 0, taken 0, over 0) for 8 furniture rows"])
        # A frame without furniture rows has no set and no fault; one that claims records then has.
        plain = [other_row("indx", in_scene=True, pitch=1120), other_row(ps=pt.COMPOSITE_PS)]
        self.assertEqual(pt.frame_quad_faults({"on": 1, "reason": "no-draw", "count": 0, "seen": 0, "taken": 0, "over": 0}, plain), [])
        self.assertEqual(pt.frame_quad_faults(None, plain), ["no \"quads\" in the frame row"])
        self.assertIn("1 records, 0 furniture rows", pt.frame_quad_faults(quads_of(want[:1]), plain)[0])

    def test_the_bound(self):
        rows = [quad_row("moon", 1, 1.0, colorcontrol=0xC)] + [quad_row("flare", 2 + i, float(i)) for i in range(24)]
        f = pt.frame_quad_faults(quads_of(rows[:24], seen=25, over=1), rows)
        self.assertTrue(any("over the bound" in x for x in f), f)
        # 24 is a set.
        self.assertEqual(pt.frame_quad_faults(quads_of(rows[:24]), rows[:24]), [])

    def test_a_row_without_its_data_cannot_be_held(self):
        def change(quads, rows):
            next(r for r in rows if r.get("n") == 53)["data"] = None
        self.assertIn("record 5 (beam-source, row n 53): the row has no \"indxdata\"", self.faults(change)[0])

    def test_the_indices(self):
        def change(quads, rows):
            next(r for r in rows if r.get("n") == 54)["data"]["idx"] = [1, 0, 2, 2, 3, 1]
        self.assertIn("indices [1, 0, 2, 2, 3, 1]", self.faults(change)[0])

    def write(self, d, frames):
        """A capture file: per frame its rows (each "indx" row followed, one row later, by its "indxdata") and the
        frame row with `quads`."""
        lines = []
        for n, (rows, quads) in enumerate(frames, 1):
            pending = None
            for r in rows:
                r = dict(r, frame=n)
                data = r.pop("data", None)
                lines.append(r)
                if pending:
                    lines.append(dict(pending, frame=n))
                pending = data
            if pending:
                lines.append(dict(pending, frame=n))
            fr = {"kind": "frame", "frame": n}
            if quads is not None:
                fr["quads"] = quads
            lines.append(fr)
        p = Path(d) / "native_discovery_test.jsonl"
        p.write_text("".join(json.dumps(x, separators=(",", ":")) + "\n" for x in lines), encoding="utf-8")
        return p

    def report(self, frames):
        with tempfile.TemporaryDirectory() as d:
            out = io.StringIO()
            with redirect_stdout(out):
                status = pt.report_quads_check(self.write(d, frames))
            return status, out.getvalue()

    def test_the_report(self):
        rows, want = self.frame()
        status, text = self.report([(rows, quads_of(want)), (rows, quads_of(want))])
        self.assertEqual(status, 0, text)
        self.assertIn("frame 1: 8 quad records equal to their rows (moon 1, cloud 1, glare 2, beam-source 1, flare 1, "
                      "sun-disc 1, cloud-occluder 1)", text)
        self.assertIn("quad records equal to the frame's rows in 2 of 2 frames; none over the bound of 24", text)

    def test_the_report_fails_on_a_fault_and_on_a_capture_without_records(self):
        rows, want = self.frame()
        bad = quads_of(want)
        bad["records"][0]["blend"] = "0x01010101"
        status, text = self.report([(rows, quads_of(want)), (rows, bad)])
        self.assertEqual(status, 1, text)
        self.assertIn("frame 2: QUADS record 0 (moon, row n 35): blend", text)
        self.assertIn("in 1 of 2 frames", text)
        status, text = self.report([(rows, None)])
        self.assertEqual(status, 1)
        self.assertIn("frame 1: QUADS no \"quads\" in the frame row", text)
        # A capture in which no frame has a furniture row checks nothing.
        plain = [other_row("indx", in_scene=True, pitch=1120)]
        status, text = self.report([(plain, {"on": 1, "reason": "no-draw", "count": 0, "seen": 0, "taken": 0, "over": 0})])
        self.assertEqual(status, 1)
        self.assertIn("no frame of the capture has a quad record", text)


if __name__ == "__main__":
    unittest.main()
