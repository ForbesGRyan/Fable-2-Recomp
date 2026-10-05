import contextlib
import io
import json
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import position_check as pc  # noqa: E402

IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.5, 0, 0, 0, 1]  # clip = (x, y, 0.5, 1): in clip when |x|,|y| <= 1


def be_floats(*v):
    return b"".join(struct.pack(">f", x) for x in v)


def fetch(i, slot, fmt, off, stride, swz=0x688, norm=True, signed=False, mini=False):
    return {"i": i, "slot": slot, "fmt": fmt, "off": off, "stride": stride, "mini": mini, "signed": signed,
            "norm": norm, "exp": 0, "swz": swz}


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir / "native_geo_x").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def stream(self, slot, phys, data, stride, endian=2):
        name = f"{phys:08X}_{len(data):08X}.bin"
        (self.dir / "native_geo_x" / name).write_bytes(data)
        return {"slot": slot, "phys": phys, "size": len(data), "stride": stride, "endian": endian,
                "file": f"native_geo_x/{name}"}

    def capture(self, rows):
        p = self.dir / "cap.jsonl"
        p.write_text("\n".join(json.dumps(r) for r in rows) + "\n")
        return p

    def row(self, vs, fetches, streams, idx, vconst=None):
        v = list(IDENTITY) + [0.0] * 48 if vconst is None else vconst
        return {"kind": "draw", "in_scene": True, "vs_hash": vs, "fetches": fetches, "streams": streams,
                "idx": idx, "base_vertex": 0, "vconst": v}


class PlainTest(Base):
    def test_plain_float4_in_and_out_of_clip(self):
        mesh = be_floats(0, 0, 0, 1) + be_floats(0.5, 0.5, 0, 1) + be_floats(9, 9, 0, 1)
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [self.stream(95, 0x1000, mesh, 16)], [0, 1, 2])]
        entry = {"0xAAAA": {"base": 0, "layout": "dot", "pos_fetch": -1}}
        rep = pc.check(self.capture(rows), entry)
        self.assertEqual(rep["0xAAAA"]["kind"], "plain")
        self.assertEqual(rep["0xAAAA"]["draws"], 1)
        self.assertEqual(rep["0xAAAA"]["passed"], 1)          # 2 of 3 vertices inside
        self.assertAlmostEqual(rep["0xAAAA"]["share"], 1.0)

    def test_draw_with_most_vertices_outside_fails(self):
        mesh = be_floats(0, 0, 0, 1) + be_floats(5, 0, 0, 1) + be_floats(9, 9, 0, 1)
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [self.stream(95, 0x1000, mesh, 16)], [0, 1, 2, 7])]
        rep = pc.check(self.capture(rows), {"0xAAAA": {"base": 0, "layout": "dot", "pos_fetch": -1}})
        self.assertEqual((rep["0xAAAA"]["draws"], rep["0xAAAA"]["passed"]), (1, 0))   # index 7: past the stream, NaN
        self.assertAlmostEqual(rep["0xAAAA"]["share"], 0.0)

    def test_half4_pair_swap_swizzle_and_base_vertex(self):
        # 8in32 on 16-bit components: the source components are (m1, m0, m3, m2).
        vertex = struct.pack(">4e", 0.25, 0.5, 2.0, -0.75)
        vb = bytes(8) + vertex                                   # the draw's vertex is index 0 + base vertex 1
        row = self.row("0xAAAA", [fetch(0, 95, 32, 0, 2, swz=0x688)], [self.stream(95, 0x1000, vb, 8)], [0])
        row["base_vertex"] = 1
        self.assertEqual(pc.positions_for(row, {"base": 0, "layout": "dot"}, self.dir)[0], (0.5, 0.25, -0.75, 2.0))
        # "yxw1": x <- source y, y <- source x, z <- source w, w = 1 (the A1F7 position).
        swizzled = pc.positions_for(row, {"base": 0, "layout": "dot", "pos_swizzle": "yxw1"}, self.dir)[0]
        self.assertEqual(swizzled, (0.25, 0.5, 2.0, 1.0))
        # 8in16 (endian 1): memory order.
        row["streams"][0]["endian"] = 1
        self.assertEqual(pc.positions_for(row, {"base": 0, "layout": "dot"}, self.dir)[0], (0.25, 0.5, 2.0, -0.75))

    def test_short4_signed_normalized_and_float3(self):
        vb = struct.pack(">4h", 32767, -32768, 0, 16384)
        row = self.row("0xAAAA", [fetch(0, 95, 26, 0, 2, signed=True)], [self.stream(95, 0x1000, vb, 8, endian=1)], [0])
        p = pc.positions_for(row, {"base": 0, "layout": "dot"}, self.dir)[0]
        self.assertEqual(p[:3], (1.0, -1.0, 0.0))
        self.assertAlmostEqual(p[3], 16384 / 32767, places=6)
        row = self.row("0xAAAA", [fetch(0, 95, 57, 0, 3)], [self.stream(95, 0x2000, be_floats(1, 2, 3), 12)], [0])
        self.assertEqual(pc.positions_for(row, {"base": 0, "layout": "dot"}, self.dir)[0], (1.0, 2.0, 3.0, 1.0))

    def test_endian_the_decoder_cannot_read_is_unsupported(self):
        mesh = be_floats(0, 0, 0, 1)
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [self.stream(95, 0x1000, mesh, 16, endian=1)], [0])]
        rep = pc.check(self.capture(rows), {"0xAAAA": {"base": 0, "layout": "dot", "pos_fetch": -1}})
        self.assertEqual((rep["0xAAAA"]["draws"], rep["0xAAAA"]["unsupported"]), (0, 1))

    def test_combine_layout_and_base_register(self):
        mesh = be_floats(-4.5, 0, 0, 1)
        vconst = [0.0] * 64
        # Columns at c4..c7: clip = x * c4 + y * c5 + z * c6 + w * c7 = (0.5, 0, 0.5, 1).
        vconst[16:32] = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 0, 0.5, 1]
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [self.stream(95, 0x1000, mesh, 16)], [0], vconst)]
        rep = pc.check(self.capture(rows), {"0xAAAA": {"base": 4, "layout": "combine"}})
        self.assertEqual((rep["0xAAAA"]["draws"], rep["0xAAAA"]["passed"]), (1, 1))
        rep = pc.check(self.capture(rows), {"0xAAAA": {"base": 4, "layout": "dot"}})   # w = -21.5: outside
        self.assertEqual((rep["0xAAAA"]["draws"], rep["0xAAAA"]["passed"]), (1, 0))

    def test_shaders_without_an_entry_are_listed_and_rows_out_of_scene_ignored(self):
        mesh = be_floats(0, 0, 0, 1)
        s = self.stream(95, 0x1000, mesh, 16)
        out = self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [s], [0])
        out["in_scene"] = False
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [s], [0]), out,
                self.row("0xDDDD", [fetch(0, 95, 38, 0, 4)], [s], [0])]
        rep = pc.check(self.capture(rows), {"0xAAAA": {"base": 0, "layout": "dot"}})
        self.assertEqual(rep["0xAAAA"]["rows"], 1)
        self.assertEqual(rep["0xAAAA"]["draws"], 1)
        self.assertEqual(pc.unlisted(self.capture(rows), {"0xAAAA": {"base": 0, "layout": "dot"}}),
                         {"0x000000000000DDDD": 1})

    def test_max_draws_samples_evenly_and_only_vs_filters(self):
        mesh = be_floats(0, 0, 0, 1)
        s = self.stream(95, 0x1000, mesh, 16)
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [s], [0]) for _ in range(10)]
        rows.append(self.row("0xBBBB", [fetch(0, 95, 38, 0, 4)], [s], [0]))
        entries = {"0xAAAA": {"base": 0, "layout": "dot"}, "0xBBBB": {"base": 0, "layout": "dot"}}
        rep = pc.check(self.capture(rows), entries, max_draws=4)
        self.assertEqual((rep["0xAAAA"]["rows"], rep["0xAAAA"]["draws"]), (10, 4))
        rep = pc.check(self.capture(rows), entries, only_vs="0xbbbb")
        self.assertEqual(list(rep), ["0xBBBB"])


class InstanceTest(Base):
    def test_instanced_rows_and_offset(self):
        mesh = be_floats(0, 0, 0, 1) + be_floats(0.25, 0, 0, 1)                      # 2 vertices per copy
        inst = b"".join(be_floats(1, 0, 0, dx) + be_floats(0, 1, 0, 0) + be_floats(0, 0, 1, 0) for dx in (0.0, 0.5))
        vconst = list(IDENTITY) + [0.0] * 48
        vconst[12 * 4:12 * 4 + 4] = [0.5, 2.0, 0.0, 0.0]                             # c12 = (1/2, 2, first 0)
        vconst[7 * 4:7 * 4 + 3] = [0.1, 0.0, 0.0]                                    # c7 offset
        fetches = [fetch(0, 94, 38, 0, 12), fetch(1, 94, 38, 4, 12, mini=True), fetch(2, 94, 38, 8, 12, mini=True),
                   fetch(3, 95, 38, 0, 4)]
        rows = [self.row("0xBBBB", fetches, [self.stream(94, 0x2000, inst, 48), self.stream(95, 0x3000, mesh, 16)],
                         [0, 1, 2, 3], vconst)]
        entry = {"0xBBBB": {"base": 0, "layout": "dot",
                            "instance": {"mesh_fetch": 3, "row_fetches": [0, 1, 2], "inv_count": "c12.x",
                                         "count": "c12.y", "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}}}
        rep = pc.check(self.capture(rows), entry)
        r = rep["0xBBBB"]
        self.assertEqual((r["kind"], r["draws"], r["passed"]), ("instance", 1, 1))
        self.assertEqual(r["copies_max"], 1)
        # index 3 -> copy 1, vertex 1 -> x = 0.25 + 0.5 + 0.1
        self.assertAlmostEqual(pc.positions_for(rows[0], entry["0xBBBB"], self.dir)[3][0], 0.85, places=5)
        self.assertEqual((r["copies_available"], r["vertices_per_copy"]), (2, [2, 2]))

    def instanced(self, vconst12, idx):
        mesh = be_floats(0, 0, 0, 1) + be_floats(0.25, 0, 0, 1)
        inst = b"".join(be_floats(1, 0, 0, dx) + be_floats(0, 1, 0, 0) + be_floats(0, 0, 1, 0) for dx in (0.0, 0.5))
        vconst = list(IDENTITY) + [0.0] * 48
        vconst[48:52] = vconst12
        fetches = [fetch(0, 94, 38, 0, 12), fetch(1, 94, 38, 4, 12, mini=True), fetch(2, 94, 38, 8, 12, mini=True),
                   fetch(3, 95, 38, 0, 4)]
        row = self.row("0xBBBB", fetches, [self.stream(94, 0x2000, inst, 48), self.stream(95, 0x3000, mesh, 16)],
                       idx, vconst)
        entry = {"base": 0, "layout": "dot",
                 "instance": {"mesh_fetch": 3, "row_fetches": [0, 1, 2], "inv_count": "c12.x", "count": "c12.y",
                              "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}}
        return row, entry

    def test_garbage_instance_constants_are_bad_index(self):
        for c12 in ([0.0, 0.0, 0.0, 0.0], [0.5, 3.0, 0.0, 0.0], [math.nan, 2.0, 0.0, 0.0], [0.5, 2.0, 70000.0, 0.0]):
            row, entry = self.instanced(c12, [0, 1, 2, 3])
            rep = pc.check(self.capture([row]), {"0xBBBB": entry})
            self.assertEqual((rep["0xBBBB"]["draws"], rep["0xBBBB"]["bad_index"]), (0, 1), c12)

    def test_copy_past_the_instance_stream_is_bad_index(self):
        row, entry = self.instanced([0.5, 2.0, 0.0, 0.0], [0, 1, 4])     # index 4 -> copy 2 of 2
        rep = pc.check(self.capture([row]), {"0xBBBB": entry})
        self.assertEqual((rep["0xBBBB"]["draws"], rep["0xBBBB"]["bad_index"]), (0, 1))

    def test_first_copy_and_rows_in_the_mesh_stream_rejected(self):
        row, entry = self.instanced([0.5, 2.0, 1.0, 0.0], [0, 1])        # first = 1: copy 1
        self.assertAlmostEqual(pc.positions_for(row, entry, self.dir)[1][0], 0.75, places=5)
        entry["instance"]["row_fetches"] = [3, 3, 3]                     # the mesh's own stream
        rep = pc.check(self.capture([row]), {"0xBBBB": entry})
        self.assertEqual((rep["0xBBBB"]["draws"], rep["0xBBBB"]["unsupported"]), (0, 1))


class SkinTest(Base):
    def test_weighted_two_bones(self):
        # Vertex: float4 position (16 bytes), index word, weight word (8in32: big-endian words).
        vb = be_floats(0.2, 0, 0, 1) + struct.pack(">I", 0x00000100) + struct.pack(">I", 0x00007F80)
        pal = b"".join(be_floats(1, 0, 0, dx) + be_floats(0, 1, 0, 0) + be_floats(0, 0, 1, 0) for dx in (0.0, 0.5))
        fetches = [fetch(0, 95, 38, 0, 6), fetch(1, 95, 6, 4, 6, norm=False, mini=True),
                   fetch(2, 95, 6, 5, 6, norm=True, mini=True),
                   fetch(3, 92, 38, 0, 12), fetch(4, 92, 38, 4, 12, mini=True), fetch(5, 92, 38, 8, 12, mini=True)]
        rows = [self.row("0xCCCC", fetches, [self.stream(95, 0x4000, vb, 24), self.stream(92, 0x5000, pal, 48)], [0])]
        entry = {"0xCCCC": {"base": 0, "layout": "dot", "pos_fetch": -1,
                            "skin": {"index_fetch": 1, "weight_fetch": 2, "row_fetches": [3, 4, 5],
                                     "pairs": [["x", "x"], ["y", "y"]]}}}
        rep = pc.check(self.capture(rows), entry)
        r = rep["0xCCCC"]
        self.assertEqual((r["kind"], r["draws"], r["passed"]), ("skin", 1, 1))
        self.assertAlmostEqual(r["weight_sum_median"], 1.0, places=2)
        self.assertEqual(r["bone_max"], 1)
        self.assertEqual(r["palette_bones"], 2)
        self.assertAlmostEqual(pc.positions_for(rows[0], entry["0xCCCC"], self.dir)[0][0],
                               0.2 + 0.5 * 127 / 255, places=4)

    def skinned(self, index_word, weight_word, index_swz=0x688):
        vb = be_floats(0.2, 0, 0, 1) + struct.pack(">I", index_word) + struct.pack(">I", weight_word)
        pal = b"".join(be_floats(1, 0, 0, dx) + be_floats(0, 1, 0, 0) + be_floats(0, 0, 1, 0) for dx in (0.0, 0.5))
        fetches = [fetch(0, 95, 38, 0, 6), fetch(1, 95, 6, 4, 6, swz=index_swz, norm=False, mini=True),
                   fetch(2, 95, 6, 5, 6, norm=True, mini=True),
                   fetch(3, 92, 38, 0, 12), fetch(4, 92, 38, 4, 12, mini=True), fetch(5, 92, 38, 8, 12, mini=True)]
        return self.row("0xCCCC", fetches, [self.stream(95, 0x4000, vb, 24), self.stream(92, 0x5000, pal, 48)], [0])

    def test_rigid_one_bone_through_the_index_swizzle(self):
        # Register z of the index fetch <- source x (swizzle "__x_"): the low byte of the word.
        row = self.skinned(0xFFFFFF01, 0, index_swz=swz("__x_"))
        entry = {"base": 0, "layout": "dot", "skin": {"index_fetch": 1, "index_component": "z", "row_fetches": [3, 4, 5]}}
        self.assertAlmostEqual(pc.positions_for(row, entry, self.dir)[0][0], 0.7, places=5)
        rep = pc.check(self.capture([row]), {"0xCCCC": entry})
        r = rep["0xCCCC"]
        self.assertEqual((r["draws"], r["bone_max"], r["palette_bones"]), (1, 1, 2))
        self.assertEqual((r["weight_sum_min"], r["weight_sum_max"]), (1.0, 1.0))
        # A component the fetch does not write: the runtime rejects the layout (skin-unsupported).
        entry["skin"]["index_component"] = "x"
        rep = pc.check(self.capture([row]), {"0xCCCC": entry})
        self.assertEqual((rep["0xCCCC"]["draws"], rep["0xCCCC"]["unsupported"]), (0, 1))

    def test_bone_past_the_palette_and_zero_weights(self):
        weighted = {"base": 0, "layout": "dot", "skin": {"index_fetch": 1, "weight_fetch": 2, "row_fetches": [3, 4, 5],
                                                        "pairs": [["x", "x"], ["y", "y"]]}}
        # Influence 1 names bone 9 with weight 0: ignored. Influence 0: bone 1, weight 1.
        p = pc.positions_for(self.skinned(0x00000901, 0x000000FF), weighted, self.dir)[0]
        self.assertAlmostEqual(p[0], 0.7, places=5)
        # Bone 9 with a nonzero weight: culled (NaN).
        self.assertTrue(math.isnan(pc.positions_for(self.skinned(0x00000901, 0x000001FE), weighted, self.dir)[0][0]))
        # Every weight zero: culled, not left at the origin.
        self.assertTrue(math.isnan(pc.positions_for(self.skinned(0x00000001, 0), weighted, self.dir)[0][0]))

    def test_skin_word_endian(self):
        self.assertEqual(pc.skin_word(pc.Stream(bytes([1, 2, 3, 4]), 4, 2), 0, 2), 0x01020304)
        self.assertEqual(pc.skin_word(pc.Stream(bytes([1, 2, 3, 4]), 4, 0), 0, 0), 0x04030201)
        self.assertIsNone(pc.skin_word(pc.Stream(bytes([1, 2, 3, 4]), 4, 2), 1, 2))

    def test_missing_stream_file_is_unreadable(self):
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)],
                         [{"slot": 95, "phys": 1, "size": 16, "stride": 16, "endian": 2}], [0])]
        rep = pc.check(self.capture(rows), {"0xAAAA": {"base": 0, "layout": "dot", "pos_fetch": -1}})
        self.assertEqual(rep["0xAAAA"]["unreadable"], 1)
        self.assertEqual(rep["0xAAAA"]["draws"], 0)

    def test_read_past_a_truncated_dump_is_unreadable(self):
        mesh = be_floats(0, 0, 0, 1)
        s = self.stream(95, 0x1000, mesh, 16)
        s["total"] = 64                                         # the stream is longer than its dump
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [s], [0, 2])]
        rep = pc.check(self.capture(rows), {"0xAAAA": {"base": 0, "layout": "dot"}})
        self.assertEqual((rep["0xAAAA"]["draws"], rep["0xAAAA"]["unreadable"]), (0, 1))


def swz(text):
    return sum({"x": 0, "y": 1, "z": 2, "w": 3, "0": 4, "1": 5, "_": 7}[c] << (3 * i) for i, c in enumerate(text))


class CliTest(Base):
    def run_main(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = pc.main([str(a) for a in argv])
        return code, out.getvalue()

    def test_report_lines_overlay_and_fixture(self):
        vb = be_floats(0.2, 0, 0, 1) + struct.pack(">I", 0x00000100) + struct.pack(">I", 0x00007F80)
        vb = bytes(24) * 3 + vb                                  # the draw's vertex is guest vertex 3
        pal = b"".join(be_floats(1, 0, 0, dx) + be_floats(0, 1, 0, 0) + be_floats(0, 0, 1, 0) for dx in (0.0, 0.5))
        fetches = [fetch(0, 95, 38, 0, 6), fetch(1, 95, 6, 4, 6, norm=False, mini=True),
                   fetch(2, 95, 6, 5, 6, norm=True, mini=True),
                   fetch(3, 92, 38, 0, 12), fetch(4, 92, 38, 4, 12, mini=True), fetch(5, 92, 38, 8, 12, mini=True)]
        rows = [self.row("0x00000000000000CC", fetches,
                         [self.stream(95, 0x4000, vb, 24), self.stream(92, 0x5000, pal, 48)], [3]),
                self.row("0x00000000000000DD", fetches, [], [0])]
        cap = self.capture(rows)
        table = self.dir / "table.json"
        table.write_text(json.dumps({"0x00000000000000CC": {"rejected": "no transform"}}))
        overlay = self.dir / "entry.json"
        overlay.write_text(json.dumps({"0x00000000000000CC": {
            "base": 0, "layout": "dot", "skin": {"index_fetch": 1, "weight_fetch": 2, "row_fetches": [3, 4, 5],
                                                 "pairs": [["x", "x"], ["y", "y"]]}}}))
        code, text = self.run_main(cap, "--json", table)
        self.assertEqual(code, 0)
        self.assertIn("VS 0x00000000000000CC no entry: rows 1", text)
        code, text = self.run_main(cap, "--json", table, "--entry", overlay, "--cpp-fixture", "0x00000000000000CC")
        self.assertEqual(code, 0)
        self.assertIn("VS 0x00000000000000CC skin: draws 1, in-clip share 1.000 (accept >= 0.90)", text)
        self.assertIn("weight sum min/median/max 1.000/1.000/1.000", text)
        self.assertIn("bone index max 1 / palette bones 2", text)
        self.assertIn("VS 0x00000000000000DD no entry: rows 1", text)
        # The fixture holds only the bytes used: one vertex (24 bytes, re-indexed to 0) and two bones.
        self.assertIn("static const uint8_t kFixtureStream95[24] = {", text)
        self.assertIn("static const uint8_t kFixtureStream92[96] = {", text)
        self.assertIn("static const uint32_t kFixtureIndices[1] = {0};", text)
        self.assertIn("static const float kFixtureVconst[64] = {", text)
        self.assertIn("static const float kFixtureExpected[1][4] = {", text)
        self.assertIn("0.449019611f", text)                     # 0.2 + 0.5 * 127 / 255 as a float32

    def test_fixture_for_an_instanced_draw_rebases_the_first_copy(self):
        mesh = be_floats(0, 0, 0, 1) + be_floats(0.25, 0, 0, 1)
        inst = b"".join(be_floats(1, 0, 0, dx) + be_floats(0, 1, 0, 0) + be_floats(0, 0, 1, 0)
                        for dx in (0.0, 0.5, 0.75))
        vconst = list(IDENTITY) + [0.0] * 48
        vconst[48:52] = [0.5, 2.0, 2.0, 0.0]                     # first copy 2
        fetches = [fetch(0, 94, 38, 0, 12), fetch(1, 94, 38, 4, 12, mini=True), fetch(2, 94, 38, 8, 12, mini=True),
                   fetch(3, 95, 38, 0, 4)]
        rows = [self.row("0x00000000000000BB", fetches,
                         [self.stream(94, 0x2000, inst, 48), self.stream(95, 0x3000, mesh, 16)], [0, 1], vconst)]
        table = self.dir / "table.json"
        table.write_text(json.dumps({"0x00000000000000BB": {
            "base": 0, "layout": "dot",
            "instance": {"mesh_fetch": 3, "row_fetches": [0, 1, 2], "inv_count": "c12.x", "count": "c12.y",
                         "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}}}))
        code, text = self.run_main(self.capture(rows), "--json", table, "--cpp-fixture", "0x00000000000000BB")
        self.assertEqual(code, 0)
        self.assertIn("copies max 2 / available 3, vertices per copy 2", text)
        self.assertIn("static const uint8_t kFixtureStream94[48] = {", text)   # copy 2 only
        self.assertIn("first copy rebased from 2 to 0", text)
        self.assertIn("{0.75f, 0.0f, 0.0f, 1.0f},", text)
        self.assertIn("{1.0f, 0.0f, 0.0f, 1.0f},", text)


if __name__ == "__main__":
    unittest.main()
