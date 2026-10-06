import contextlib
import io
import json
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import position_check as pc  # noqa: E402

IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.5, 0, 0, 0, 1]  # clip = (x, y, 0.5, 1): in clip when |x|,|y| <= 1


def be_floats(*v):
    return b"".join(struct.pack(">f", x) for x in v)


def fetch(i, slot, fmt, off, stride, swz=0x688, norm=True, signed=False, mini=False):
    return {"i": i, "slot": slot, "fmt": fmt, "off": off, "stride": stride, "mini": mini, "signed": signed,
            "norm": norm, "exp": 0, "swz": swz}


class Base(unittest.TestCase):
    # These captures hold a handful of draws, so the tests of the other rules run with the floor on the
    # judged draws (position_check.MIN_JUDGED_DRAWS) lowered to one draw; None keeps the real floor
    # (SkippedRowsVerdictTest).
    MIN_JUDGED = 1

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir / "native_geo_x").mkdir()
        if self.MIN_JUDGED is not None:
            floor = mock.patch.object(pc, "MIN_JUDGED_DRAWS", self.MIN_JUDGED)
            floor.start()
            self.addCleanup(floor.stop)

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
        self.assertEqual([k for k in rep if not k.startswith("_")], ["0xBBBB"])


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

    def cut_draw(self, eye, dist2, idx, cut=True):
        """Positions x = 0.6, 0.85 (copy 0) and 1.1, 1.35 (copy 1): the first two are inside the clip volume."""
        row, entry = self.instanced([0.5, 2.0, 0.0, 0.0], idx)
        row["vconst"][7 * 4] = 0.6                                          # c7.x
        row["vconst"][9 * 4:9 * 4 + 3] = eye                                # c9.xyz
        row["vconst"][13 * 4 + 2] = dist2                                   # c13.z
        if cut:
            entry["instance"]["cut"] = {"eye": "c9.xyz", "dist2": "c13.z"}
        return row, entry, pc.check(self.capture([row]), {"0xBBBB": entry})["0xBBBB"]

    def test_distance_cut_scores_the_vertices_kept(self):
        idx = [0, 2, 3, 2, 3]                                                # one vertex inside, four outside
        row, entry, r = self.cut_draw([0.5, 0.0, 0.0], 0.25, idx, cut=False)
        self.assertEqual((r["draws"], r["passed"]), (1, 0))
        self.assertNotIn("cut", r)
        # Eye at x = 0.5, radius 0.5: 0.6 is kept, 1.1 and 1.35 are cut.
        row, entry, r = self.cut_draw([0.5, 0.0, 0.0], 0.25, idx)
        self.assertEqual((r["draws"], r["passed"], r["cut"]), (1, 1, 0))
        self.assertEqual((r["kept_vertices"], r["sampled_vertices"]), (1, 5))
        # The flat positions themselves are not changed: the cut is the vertex shader's.
        self.assertAlmostEqual(pc.positions_for(row, entry, self.dir)[1][0], 1.1, places=5)
        # A kept vertex outside the clip volume still counts against the draw.
        row, entry, r = self.cut_draw([1.0, 0.0, 0.0], 0.0625, [0, 2, 2])    # keeps 1.1 only (twice)
        self.assertEqual((r["draws"], r["passed"], r["kept_vertices"], r["sampled_vertices"]), (1, 0, 2, 3))

    def test_distance_cut_is_strict_and_in_float32(self):
        # x = 0.85 is the float32 nearest 0.6f + 0.25f; from eye 0.35 that is 0.5 away up to rounding.
        x = pc.f32(pc.f32(0.25) + pc.f32(0.6))
        d = pc.f32(pc.f32(0.35) - x)
        d2 = pc.f32(d * d)
        row, entry, r = self.cut_draw([0.35, 0.0, 0.0], d2, [1])
        self.assertEqual((r["draws"], r["cut"], r["kept_vertices"]), (1, 0, 1))  # equal: kept
        below = struct.unpack("<f", struct.pack("<I", struct.unpack("<I", struct.pack("<f", d2))[0] - 1))[0]
        row, entry, r = self.cut_draw([0.35, 0.0, 0.0], below, [1])
        self.assertEqual((r["draws"], r["cut"]), (0, 1))                          # one ulp less: cut

    def test_draw_wholly_past_the_cut_leaves_the_share(self):
        row, entry, r = self.cut_draw([50.0, 0.0, 0.0], 1.0, [0, 1, 2, 3])
        self.assertEqual((r["draws"], r["passed"], r["cut"], r["bad_index"]), (0, 0, 1, 0))
        self.assertEqual((r["kept_vertices"], r["sampled_vertices"]), (0, 4))
        self.assertFalse(pc.accepted(dict(r, share=0.0), 0.5))
        line = pc.format_line("0xBBBB", r, 0.71)
        self.assertIn("cut 1", line)
        self.assertIn("kept vertices 0 of 4", line)

    def test_garbage_cut_constants_cut_nothing(self):
        for eye, dist2 in (([0.5, 0.0, 0.0], float("nan")), ([0.5, 0.0, 0.0], -1.0), ([0.5, 0.0, 0.0], float("inf")),
                           ([float("nan"), 0.0, 0.0], 0.25), ([float("inf"), 0.0, 0.0], 0.25)):
            row, entry, r = self.cut_draw(eye, dist2, [0, 1, 2, 3])
            self.assertEqual((r["draws"], r["cut"], r["kept_vertices"]), (1, 0, 4), (eye, dist2))

    def test_malformed_cut_is_an_entry_error(self):
        row, entry = self.instanced([0.5, 2.0, 0.0, 0.0], [0, 1])
        entry["instance"]["cut"] = {"eye": "c9.xy", "dist2": "c13.z"}
        r = pc.check(self.capture([row]), {"0xBBBB": entry})["0xBBBB"]
        self.assertEqual(r["kind"], "error")

    def test_garbage_instance_constants_are_bad_index(self):
        for c12 in ([0.0, 0.0, 0.0, 0.0], [0.5, 3.0, 0.0, 0.0], [math.nan, 2.0, 0.0, 0.0], [0.5, 2.0, 70000.0, 0.0]):
            row, entry = self.instanced(c12, [0, 1, 2, 3])
            rep = pc.check(self.capture([row]), {"0xBBBB": entry})
            self.assertEqual((rep["0xBBBB"]["draws"], rep["0xBBBB"]["bad_index"]), (0, 1), c12)

    def test_an_index_mapped_outside_its_copy_is_bad_index(self):
        # count 2, inv_count 0.495 (inside the 1% agreement), no rounding bias: index 2 gives
        # t = trunc(0.99) = 0, vertex 2 of a 2-vertex mesh, although the largest index (3) maps to copy 1.
        row, entry = self.instanced([0.495, 2.0, 0.0, 0.0], [0, 1, 2, 3])
        entry["instance"]["bias"] = 0.0
        self.assertEqual(pc.instance_index(3, 0.0, pc.f32(0.495), 2.0, 0.0), (1, 1))
        self.assertEqual(pc.instance_index(2, 0.0, pc.f32(0.495), 2.0, 0.0), (0, 2))
        rep = pc.check(self.capture([row]), {"0xBBBB": entry})
        self.assertEqual((rep["0xBBBB"]["draws"], rep["0xBBBB"]["bad_index"]), (0, 1))
        # With the shader's bias the same constants map every index of both copies.
        row, entry = self.instanced([0.495, 2.0, 0.0, 0.0], [0, 1, 2, 3])
        rep = pc.check(self.capture([row]), {"0xBBBB": entry})
        self.assertEqual((rep["0xBBBB"]["draws"], rep["0xBBBB"]["bad_index"]), (1, 0))

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
        self.assertEqual(text.splitlines()[0],
                         "baseline 0xECD66A10092E6562: no draws in this capture, accept >= 0.750 (fallback)")
        # One vertex and no primitive type: no edge to judge the skin by.
        self.assertIn("VS 0x00000000000000CC skin: draws 1, in-clip share 1.000 (accept >= 0.750) REJECT", text)
        self.assertIn("bone ortho share 1.0000 (2 of 2, >= 0.90), max 0.000, edge ok share n/a (>= 0.98) of 0 edges, "
                      "edge nan 0", text)
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


BASELINE = "0xECD66A10092E6562"
PLAIN = {"base": 0, "layout": "dot"}


class BaselineTest(Base):
    run_main = CliTest.run_main

    def draws(self, vs, passing, failing):
        """`passing` draws of one vertex inside the clip volume and `failing` draws of one outside it."""
        mesh = be_floats(0, 0, 0, 1) + be_floats(9, 9, 0, 1)
        s = self.stream(95, 0x1000, mesh, 16)
        return [self.row(vs, [fetch(0, 95, 38, 0, 4)], [s], [0 if k < passing else 1])
                for k in range(passing + failing)]

    def test_threshold_is_the_trusted_share_minus_a_tenth(self):
        rows = self.draws(BASELINE, 8, 2) + self.draws("0xAAAA", 7, 3) + self.draws("0xBBBB", 6, 4)
        rep = pc.check(self.capture(rows), {BASELINE: PLAIN, "0xAAAA": PLAIN, "0xBBBB": PLAIN})
        self.assertAlmostEqual(rep["_baseline"]["share"], 0.8)
        self.assertAlmostEqual(rep["_baseline"]["threshold"], 0.7)
        self.assertEqual((rep["_baseline"]["draws"], rep["_baseline"]["fallback"]), (10, False))
        # 0xAAAA sits exactly on the threshold (0.7): accepted.
        self.assertEqual([rep[k]["accepted"] for k in (BASELINE, "0xAAAA", "0xBBBB")], [True, True, False])

    def test_threshold_never_below_the_floor(self):
        rows = self.draws(BASELINE, 6, 4) + self.draws("0xAAAA", 11, 9)      # baseline 0.6, candidate 0.55
        rep = pc.check(self.capture(rows), {BASELINE: PLAIN, "0xAAAA": PLAIN})
        self.assertAlmostEqual(rep["_baseline"]["threshold"], 0.6)
        self.assertEqual((rep[BASELINE]["accepted"], rep["0xAAAA"]["accepted"]), (True, False))

    def test_fallback_threshold_without_baseline_draws(self):
        rows = self.draws("0xAAAA", 3, 1) + self.draws("0xBBBB", 2, 2)       # 0.75 and 0.5
        for entries in ({"0xAAAA": PLAIN, "0xBBBB": PLAIN}, {BASELINE: PLAIN, "0xAAAA": PLAIN, "0xBBBB": PLAIN}):
            rep = pc.check(self.capture(rows), entries)
            self.assertEqual(rep["_baseline"], {"share": None, "threshold": 0.75, "draws": 0, "fallback": True})
            self.assertEqual((rep["0xAAAA"]["accepted"], rep["0xBBBB"]["accepted"]), (True, False))

    def test_baseline_comes_from_the_table_not_from_the_overlay(self):
        rows = self.draws(BASELINE, 8, 2)
        wrong = {"base": 4, "layout": "dot"}                                 # c4..c7 are zero: nothing in clip
        rep = pc.check(self.capture(rows), {BASELINE: wrong}, baseline_entries={BASELINE: PLAIN})
        self.assertAlmostEqual(rep["_baseline"]["share"], 0.8)
        self.assertEqual((rep[BASELINE]["share"], rep[BASELINE]["accepted"]), (0.0, False))

    def test_baseline_is_computed_when_another_shader_is_selected(self):
        rows = self.draws(BASELINE, 8, 2) + self.draws("0xAAAA", 7, 3)
        rep = pc.check(self.capture(rows), {BASELINE: PLAIN, "0xAAAA": PLAIN}, only_vs="0xAAAA")
        self.assertEqual(sorted(rep), ["0xAAAA", "_baseline"])
        self.assertAlmostEqual(rep["_baseline"]["share"], 0.8)
        self.assertTrue(rep["0xAAAA"]["accepted"])

    def test_printed_baseline_and_verdicts(self):
        rows = self.draws(BASELINE, 8, 2) + self.draws("0x00000000000000AA", 7, 3) + \
            self.draws("0x00000000000000BB", 6, 4)
        table = self.dir / "table.json"
        table.write_text(json.dumps({BASELINE: PLAIN, "0x00000000000000AA": PLAIN, "0x00000000000000BB": PLAIN}))
        overlay = self.dir / "entry.json"
        overlay.write_text(json.dumps({BASELINE: {"base": 4, "layout": "dot"}}))
        code, text = self.run_main(self.capture(rows), "--json", table)
        self.assertEqual(code, 0)
        self.assertEqual(text.splitlines()[0], "baseline 0xECD66A10092E6562: share 0.800, accept >= 0.700")
        self.assertIn("VS 0x00000000000000AA plain: draws 10, in-clip share 0.700 (accept >= 0.700) ACCEPT", text)
        self.assertIn("VS 0x00000000000000BB plain: draws 10, in-clip share 0.600 (accept >= 0.700) REJECT", text)
        self.assertIn("accepted 2 of 3 judged", text)
        # An overlay of the trusted shader is judged against the table's entry.
        code, text = self.run_main(self.capture(rows), "--json", table, "--entry", overlay)
        self.assertEqual(text.splitlines()[0], "baseline 0xECD66A10092E6562: share 0.800, accept >= 0.700")
        self.assertIn("VS 0xECD66A10092E6562 plain: draws 10, in-clip share 0.000 (accept >= 0.700) REJECT", text)
        code, text = self.run_main(self.capture(self.draws("0x00000000000000AA", 3, 1)), "--json", table)
        self.assertEqual(text.splitlines()[0],
                         "baseline 0xECD66A10092E6562: no draws in this capture, accept >= 0.750 (fallback)")
        self.assertIn("in-clip share 0.750 (accept >= 0.750) ACCEPT", text)


class ContradictoryEntryTest(Base):
    """parse_entry raises on the entries gen_transform_table.py refuses; check() reports them as entry errors."""

    INSTANCE = {"mesh_fetch": 3, "row_fetches": [0, 1, 2], "inv_count": "c12.x", "count": "c12.y",
                "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}

    def error_line(self, entry):
        mesh = be_floats(0, 0, 0, 1)
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [self.stream(95, 0x1000, mesh, 16)], [0])]
        r = pc.check(self.capture(rows), {"0xAAAA": entry})["0xAAAA"]
        self.assertEqual((r["kind"], r["accepted"]), ("error", False))
        return pc.format_line("0xAAAA", r, 0.75)

    def test_entry_with_both_skin_and_instance_is_an_entry_error(self):
        entry = {"base": 0, "layout": "dot", "instance": dict(self.INSTANCE),
                 "skin": {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}}
        with self.assertRaisesRegex(ValueError, "skin.*instance"):
            pc.parse_entry(entry)
        self.assertRegex(self.error_line(entry), "VS 0xAAAA entry error: ValueError: .*skin.*instance")

    def test_skin_with_pairs_but_no_weight_fetch_is_an_entry_error(self):
        skin = {"index_fetch": 1, "row_fetches": [3, 4, 5], "pairs": [["x", "z"], ["y", "y"]]}
        for extra in ({}, {"index_component": "x"}):
            entry = {"base": 0, "layout": "dot", "skin": dict(skin, **extra)}
            with self.assertRaisesRegex(ValueError, "pairs.*weight_fetch"):
                pc.parse_entry(entry)
            self.assertRegex(self.error_line(entry), "VS 0xAAAA entry error: ValueError: .*pairs.*weight_fetch")

    def test_instance_entry_without_base_is_an_entry_error(self):
        entry = {"layout": "dot", "instance": dict(self.INSTANCE)}
        with self.assertRaisesRegex(ValueError, "instance.*base"):
            pc.parse_entry(entry)
        self.assertRegex(self.error_line(entry), "VS 0xAAAA entry error: ValueError: .*instance.*base")
        # An entry without a transform and without "instance" is still no entry at all.
        self.assertIsNone(pc.parse_entry({"manual": True, "rejected": "no transform"}))


class SkippedRowsVerdictTest(Base):
    """The verdict counts what leaves the in-clip share: rows the runtime would skip reject the entry,
    and so does a share judged on too few draws. Runs with the real floor."""

    MIN_JUDGED = None
    instanced = InstanceTest.instanced

    def plain(self, endian=2, file=True):
        mesh = be_floats(0, 0, 0, 1)
        s = self.stream(95, 0x1000, mesh, 16, endian)
        if not file:
            del s["file"]
        return self.row("0x00000000000000AA", [fetch(0, 95, 38, 0, 4)], [s], [0])

    def judge(self, rows, entry=PLAIN, max_draws=200):
        r = pc.check(self.capture(rows), {"0x00000000000000AA": entry}, max_draws=max_draws)["0x00000000000000AA"]
        return r, pc.format_line("0x00000000000000AA", r, 0.75)

    def test_rows_the_runtime_would_skip_reject_the_entry(self):
        good = self.plain()
        r, line = self.judge([good] * 30)
        self.assertEqual((r["draws"], r["sampled"], r["share"], r["accepted"]), (30, 30, 1.0, True))
        self.assertIn("in-clip share 1.000 (accept >= 0.750) ACCEPT, passed 30", line)
        # One row whose position endian the decoder cannot read: the runtime would not draw it.
        r, line = self.judge([good] * 30 + [self.plain(endian=1)])
        self.assertEqual((r["draws"], r["passed"], r["unsupported"], r["share"]), (30, 30, 1, 1.0))
        self.assertFalse(r["accepted"])
        self.assertIn("in-clip share 1.000 (accept >= 0.750) REJECT (the runtime would skip 1 of 31 sampled rows), "
                      "passed 30, unreadable 0, unsupported 1, bad-index 0", line)
        # 190 of 200 draws bad-index (garbage instance constants) and the other 10 in clip.
        ok, entry = self.instanced([0.5, 2.0, 0.0, 0.0], [0, 1])
        bad, _ = self.instanced([0.0, 0.0, 0.0, 0.0], [0, 1])
        ok["vs_hash"] = bad["vs_hash"] = "0x00000000000000AA"
        r, line = self.judge([bad] * 19 + [ok] + [bad] * 171 + [ok] * 9, entry)
        self.assertEqual((r["draws"], r["passed"], r["bad_index"], r["share"]), (10, 10, 190, 1.0))
        self.assertFalse(r["accepted"])
        self.assertIn("REJECT (the runtime would skip 190 of 200 sampled rows), passed 10", line)
        # A stream dump missing from the capture says nothing about the entry: counted, printed, not judged.
        r, line = self.judge([good] * 30 + [self.plain(file=False)] * 5)
        self.assertEqual((r["draws"], r["unreadable"], r["sampled"], r["accepted"]), (30, 5, 35, True))
        self.assertIn("ACCEPT, passed 30, unreadable 5, unsupported 0, bad-index 0", line)

    def test_too_few_judged_draws_reject_the_entry(self):
        self.assertEqual((pc.MIN_JUDGED_DRAWS, pc.MIN_JUDGED_SHARE), (20, 0.10))
        good = self.plain()
        r, line = self.judge([good] * 20)
        self.assertEqual((r["draws"], r["accepted"]), (20, True))
        r, line = self.judge([good] * 19)
        self.assertEqual((r["draws"], r["share"], r["accepted"]), (19, 1.0, False))
        self.assertIn("in-clip share 1.000 (accept >= 0.750) REJECT (judged on 19 draws, fewer than 20), passed 19",
                      line)
        # Unreadable rows are no judged draws: 19 judged of 60 sampled is still too few.
        r, line = self.judge([good] * 19 + [self.plain(file=False)] * 41)
        self.assertEqual((r["draws"], r["unreadable"], r["accepted"]), (19, 41, False))
        # Wholly cut draws leave the share: an entry with a cut must be judged on a tenth of its sampled rows.
        kept, entry = self.instanced([0.5, 2.0, 0.0, 0.0], [0, 1])
        entry["instance"]["cut"] = {"eye": "c9.xyz", "dist2": "c13.z"}
        kept["vs_hash"] = "0x00000000000000AA"
        kept["vconst"][13 * 4 + 2] = 100.0                                   # eye at the origin, radius 10
        gone = json.loads(json.dumps(kept))
        gone["vconst"][9 * 4] = 50.0                                         # eye 50 away: nothing kept
        r, line = self.judge([kept] * 29 + [gone] * 271, entry, max_draws=300)
        self.assertEqual((r["draws"], r["passed"], r["cut"], r["sampled"]), (29, 29, 271, 300))
        self.assertFalse(r["accepted"])
        self.assertIn("REJECT (judged on 29 draws, fewer than 30), passed 29", line)
        r, line = self.judge([kept] * 30 + [gone] * 270, entry, max_draws=300)
        self.assertEqual((r["draws"], r["cut"], r["accepted"]), (30, 270, True))
        # No draw judged at all: the same reason, never an ACCEPT.
        r, line = self.judge([gone] * 40, entry)
        self.assertEqual((r["draws"], r["cut"], r["accepted"]), (0, 40, False))
        self.assertIn("REJECT (judged on 0 draws, fewer than 20)", line)


def translation(dx):
    return [(1, 0, 0, dx), (0, 1, 0, 0), (0, 0, 1, 0)]


class SkinStructureTest(Base):
    ENTRY = {"base": 0, "layout": "dot", "skin": {"index_fetch": 1, "index_component": "x", "row_fetches": [2, 3, 4]}}

    def test_bone_ortho(self):
        self.assertEqual(pc.bone_ortho(translation(5)), 0.0)
        c, s = math.cos(0.3), math.sin(0.3)
        self.assertAlmostEqual(pc.bone_ortho([(c, -s, 0, 1), (s, c, 0, 2), (0, 0, 1, 3)]), 0.0, places=9)
        self.assertAlmostEqual(pc.bone_ortho([(1, 0.5, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0)]), 0.5)   # sheared: row0.row1
        self.assertAlmostEqual(pc.bone_ortho([(2, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0)]), 1.0)     # scaled: |row0| - 1
        self.assertEqual(pc.bone_ortho([(math.nan, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0)]), math.inf)

    def test_triangles_from_lists_strips_and_fans_with_a_restart(self):
        # Triangles are ordinals into the indices that are not restarts (the positions the replay returns).
        idx = [10, 11, 12, 13, -1, 20, 21, 22]
        self.assertEqual(pc.triangles(6, idx), [(0, 1, 2), (1, 2, 3), (4, 5, 6)])
        self.assertEqual(pc.triangles(5, idx), [(0, 1, 2), (0, 2, 3), (4, 5, 6)])
        self.assertEqual(pc.triangles(4, [10, 11, 12, -1, 13, 14, 15, 16]), [(0, 1, 2), (3, 4, 5)])
        self.assertEqual(pc.triangles(4, [10, 11, -1, 12, 13, 14]), [(2, 3, 4)])    # a restart cuts the list too
        self.assertEqual(pc.triangles(6, [1, 2, -1, 3]), [])
        self.assertIsNone(pc.triangles(13, idx))                                    # quad list: not judged

    def test_edge_stats(self):
        bind = [(0, 0, 0, 1), (1, 0, 0, 1), (0, 1, 0, 1), (1, 1, 0, 1)]
        moved = [(x + 5, y, z, 1.0) for x, y, z, _ in bind]
        self.assertEqual(pc.edge_stats([(0, 1, 2)], [7, 8, 9, 10], bind, moved), (3, 3, 0))
        # Two triangles sharing an edge: five distinct edges.
        self.assertEqual(pc.edge_stats([(0, 1, 2), (1, 2, 3)], [7, 8, 9, 10], bind, moved), (5, 5, 0))
        stretched = moved[:2] + [(5.0, 40.0, 0.0, 1.0)] + moved[3:]
        self.assertEqual(pc.edge_stats([(0, 1, 2)], [7, 8, 9, 10], bind, stretched), (1, 3, 0))
        halved = [(x / 2, y / 2, z / 2, 1.0) for x, y, z, _ in bind]               # ratio 0.5: still inside
        self.assertEqual(pc.edge_stats([(0, 1, 2)], [7, 8, 9, 10], bind, halved), (3, 3, 0))
        culled = moved[:2] + [(math.nan, math.nan, math.nan, 1.0)] + moved[3:]
        self.assertEqual(pc.edge_stats([(0, 1, 2)], [7, 8, 9, 10], bind, culled), (1, 1, 2))
        # A degenerate strip triangle (one vertex twice) and a zero-length bind edge are not edges.
        self.assertEqual(pc.edge_stats([(0, 1, 2)], [7, 7, 8, 10], bind, moved), (1, 1, 0))
        same = [bind[0], bind[0], bind[2], bind[3]]
        self.assertEqual(pc.edge_stats([(0, 1, 2)], [7, 8, 9, 10], same, moved), (2, 2, 0))

    def triangle(self, bones, palette, prim="0x00000004", idx=(0, 1, 2), phys=0):
        """A small right triangle; vertex k is rigidly bound to palette bone bones[k]. `phys` moves the
        row's streams, so rows with other bytes can share a capture."""
        vb = b"".join(be_floats(x, y, 0, 1) + struct.pack(">I", bone)
                      for (x, y), bone in zip(((0, 0), (0.1, 0), (0, 0.1)), bones))
        pal = b"".join(be_floats(*r) for bone in palette for r in bone)
        fetches = [fetch(0, 95, 38, 0, 5), fetch(1, 95, 6, 4, 5, norm=False, mini=True),
                   fetch(2, 92, 38, 0, 12), fetch(3, 92, 38, 4, 12, mini=True), fetch(4, 92, 38, 8, 12, mini=True)]
        row = self.row("0xCCCC", fetches, [self.stream(95, 0x4000 + phys, vb, 20),
                                           self.stream(92, 0x5000 + phys, pal, 48)], list(idx))
        row["args"] = [prim, "0x00000000", "0x00000000", "0x00000003"]
        return row

    def result(self, row, entry=None):
        return pc.check(self.capture([row]), {"0xCCCC": entry or self.ENTRY})["0xCCCC"]

    def test_rigidly_moved_triangle_is_accepted(self):
        r = self.result(self.triangle((1, 1, 1), [translation(0), translation(0.3)]))
        self.assertEqual((r["draws"], r["passed"], r["edge_draws"], r["edges"], r["edge_nan"]), (1, 1, 1, 3, 0))
        self.assertEqual((r["edge_ok_share"], r["bone_ortho_max"], r["accepted"]), (1.0, 0.0, True))

    def test_vertex_on_a_far_bone_fails_the_edge_rule(self):
        r = self.result(self.triangle((1, 1, 2), [translation(0), translation(0.3), translation(0.9)]))
        self.assertEqual((r["draws"], r["passed"]), (1, 1))             # still inside the clip volume
        self.assertAlmostEqual(r["edge_ok_share"], 1 / 3)
        self.assertEqual((r["bone_ortho_max"], r["accepted"]), (0.0, False))

    def test_sheared_bone_fails_the_orthonormality_rule(self):
        sheared = [(1, 0.5, 0, 0.3), (0, 1, 0, 0), (0, 0, 1, 0)]
        r = self.result(self.triangle((1, 1, 1), [translation(0), sheared, [(9, 9, 9, 9)] * 3]))
        self.assertAlmostEqual(r["bone_ortho_max"], 0.5)               # bone 2 is not referenced: not judged
        self.assertEqual((r["bone_uses"], r["bone_ortho_ok"], r["bone_ortho_share"]), (1, 0, 0.0))
        self.assertEqual((r["passed"], r["edge_ok_share"], r["accepted"]), (1, 1.0, False))

    def test_orthonormality_is_judged_under_the_row_swizzles(self):
        stored = [(0.3, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)]         # translation first: rows are "yzwx"
        row = self.triangle((0, 0, 0), [stored])
        entry = json.loads(json.dumps(self.ENTRY))
        self.assertGreater(self.result(row)["bone_ortho_max"], 0.05)
        entry["skin"]["row_swizzles"] = ["yzwx", "yzwx", "yzwx"]
        r = self.result(row, entry)
        self.assertEqual((r["bone_ortho_max"], r["edge_ok_share"], r["accepted"]), (0.0, 1.0, True))
        self.assertEqual((r["bone_uses"], r["bone_ortho_ok"], r["bone_ortho_share"]), (1, 1, 1.0))

    # --- Orthonormality by share of bone uses (a bone use: one palette bone that one sampled draw's
    # vertices reference with a nonzero weight) ---

    SCALED = [(0.6, 0, 0, 0.3), (0, 1, 0, 0), (0, 0, 1, 0)]      # x scaled to 0.6: deviation 0.4, edges kept

    def uses(self, good, scaled):
        """A capture of `good` draws on an orthonormal bone and `scaled` draws on the scaled one (one bone
        use each), judged as one shader."""
        palette = [translation(0.3), self.SCALED]
        rows = [self.triangle((0, 0, 0), palette)] * good + [self.triangle((1, 1, 1), palette, phys=0x100)] * scaled
        return pc.check(self.capture(rows), {"0xCCCC": self.ENTRY})["0xCCCC"]

    def test_one_scaled_bone_among_many_is_accepted_and_the_maximum_reported(self):
        r = self.uses(99, 1)
        self.assertEqual((r["draws"], r["passed"], r["edge_ok_share"]), (100, 100, 1.0))
        self.assertEqual((r["bone_uses"], r["bone_ortho_ok"]), (100, 99))
        self.assertAlmostEqual(r["bone_ortho_share"], 0.99)
        self.assertAlmostEqual(r["bone_ortho_max"], 0.4)
        self.assertTrue(r["accepted"])
        line = pc.format_line("0xCCCC", r, 0.75)
        self.assertIn("ACCEPT", line)
        self.assertIn("bone ortho share 0.9900 (99 of 100, >= 0.90), max 0.400, edge ok share 1.000", line)

    def test_most_bones_failing_is_rejected_on_the_share(self):
        # Everything else passes (in clip, edges kept): the share alone rejects.
        r = self.uses(3, 7)
        self.assertEqual((r["passed"], r["edge_ok_share"]), (10, 1.0))
        self.assertAlmostEqual(r["bone_ortho_share"], 0.3)
        self.assertFalse(r["accepted"])
        self.assertIn("REJECT", pc.format_line("0xCCCC", r, 0.75))
        # Rows stored translation first and read as fetched (scrambled): every bone use fails.
        stored = [(0.3, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)]
        rows = [self.triangle((0, 0, 0), [stored, stored]), self.triangle((1, 1, 1), [stored, stored], phys=0x100)]
        r = pc.check(self.capture(rows), {"0xCCCC": self.ENTRY})["0xCCCC"]
        self.assertEqual((r["bone_uses"], r["bone_ortho_ok"], r["bone_ortho_share"], r["accepted"]), (2, 0, 0.0, False))

    def test_the_share_boundary(self):
        self.assertEqual(pc.ORTHO_SHARE_MIN, 0.90)
        r = self.uses(9, 1)                                          # 9 of 10 = 0.90 exactly: passes
        self.assertAlmostEqual(r["bone_ortho_share"], 0.90)
        self.assertTrue(r["accepted"])
        r = self.uses(8, 1)                                          # 8 of 9 = 0.889: fails
        self.assertLess(r["bone_ortho_share"], 0.90)
        self.assertFalse(r["accepted"])
        # One scaled bone in a mesh drawn often (the bridge scene's birds: 0.972 to 0.976) passes.
        r = self.uses(39, 1)
        self.assertAlmostEqual(r["bone_ortho_share"], 0.975)
        self.assertTrue(r["accepted"])

    def test_a_bone_is_counted_once_per_draw_and_only_with_a_nonzero_weight(self):
        # Three vertices on bones 0, 0 and 1: two bone uses; palette bone 2 is not referenced.
        r = self.result(self.triangle((0, 0, 1), [translation(0.3), translation(0.3), [(9, 9, 9, 9)] * 3]))
        self.assertEqual((r["bone_uses"], r["bone_ortho_ok"]), (2, 2))

    def test_worst_bone_uses_are_listed(self):
        palette = [translation(0.3), self.SCALED]
        good = self.triangle((0, 0, 0), palette)
        bad = dict(self.triangle((1, 1, 1), palette, phys=0x100), frame=7)
        rep = pc.check(self.capture([good, bad, good]), {"0xCCCC": self.ENTRY}, worst_bones=2)
        worst = rep["0xCCCC"]["bone_worst"]
        self.assertEqual(len(worst), 2)
        self.assertAlmostEqual(worst[0]["deviation"], 0.4)
        self.assertEqual((worst[0]["frame"], worst[0]["bone"], worst[0]["palette_bones"]), (7, 1, 2))
        self.assertEqual([round(x, 3) for x in worst[0]["row_lengths"]], [0.6, 1.0, 1.0])
        self.assertEqual(worst[1]["deviation"], 0.0)
        text = pc.format_bones(worst)
        self.assertIn("deviation 0.400, frame 7, ", text[0])
        self.assertIn("bone 1 of 2, row lengths 0.600 1.000 1.000", text[0])
        self.assertNotIn("bone_worst", pc.check(self.capture([good, bad]), {"0xCCCC": self.ENTRY})["0xCCCC"])

    def test_strip_with_a_restart_index_in_the_row(self):
        row = self.triangle((1, 1, 1), [translation(0), translation(0.3)], "0x00000006", (0, 1, 2, -1, 2, 1, 0))
        row["idx_resets"] = 1
        self.assertEqual(len(pc.positions_for(row, self.ENTRY, self.dir)), 6)      # the restart is not a vertex
        r = self.result(row)
        self.assertEqual((r["passed"], r["edges"], r["edge_ok_share"], r["accepted"]), (1, 3, 1.0, True))

    def test_draws_whose_triangles_are_unknown_are_not_judged(self):
        palette = [translation(0), translation(0.3)]
        r = self.result(self.triangle((1, 1, 1), palette, "0x0000000D"))             # quad list
        self.assertEqual((r["passed"], r["edge_draws"], r["edge_ok_share"], r["accepted"]), (1, 0, None, False))
        # A capture from before restart indices were kept in "idx": a strip with restarts has lost its cuts.
        old = self.triangle((1, 1, 1), palette, "0x00000006")
        old["ib"] = {"restarts": 2, "max_index": 2}
        r = self.result(old)
        self.assertEqual((r["edge_draws"], r["accepted"]), (0, False))
        old["idx_resets"] = 0
        self.assertEqual((self.result(old)["edge_draws"], self.result(old)["accepted"]), (1, True))

    def test_plain_entries_have_no_skin_metrics(self):
        mesh = be_floats(0, 0, 0, 1)
        row = self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [self.stream(95, 0x1000, mesh, 16)], [0])
        r = pc.check(self.capture([row]), {"0xAAAA": PLAIN})["0xAAAA"]
        self.assertTrue(r["accepted"])
        self.assertNotIn("bone_ortho_max", r)
        self.assertNotIn("edge_ok_share", r)

    def test_skin_line_prints_the_three_numbers(self):
        r = self.result(self.triangle((1, 1, 2), [translation(0), translation(0.3), translation(0.9)]))
        line = pc.format_line("0xCCCC", r, 0.75)
        self.assertIn("in-clip share 1.000 (accept >= 0.750) REJECT", line)
        self.assertIn("bone ortho share 1.0000 (2 of 2, >= 0.90), max 0.000, edge ok share", line)
        self.assertIn("edge ok share 0.333 (>= 0.98) of 3 edges, edge nan 0", line)


if __name__ == "__main__":
    unittest.main()
