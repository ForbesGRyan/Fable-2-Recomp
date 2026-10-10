"""tools/xdk_sigmatch/foliage_replay.py and capture_replay.py: the rasteriser on triangles whose cover and depth
are known by hand (the sampling place, the top-left rule, the picture's edge, the near plane, the depth's
range), the reader of a row's indices on made files (whole, a range, cut short), the game's microcode and the
table entry on a made capture, the judgement against a stored depth on made planes, and, when it is there, a
real capture's rows."""
import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import capture_replay as cr  # noqa: E402
import foliage_replay as fr  # noqa: E402

TRI = np.array([[0, 1, 2]])
CAPTURE = cr.LOGS / "native_discovery_20261010_120714.jsonl"
NO_CAPTURE = "no capture %s" % CAPTURE


def half(z=0.5):
    """The upper left half of the picture: window corners (0, 0), (W, 0), (0, H)."""
    return np.array([[-1.0, 1.0, z, 1.0], [1.0, 1.0, z, 1.0], [-1.0, -1.0, z, 1.0]])


class Raster(unittest.TestCase):
    def test_cover_at_the_integral_position_and_at_the_centre(self):
        depth, ident = fr.raster([(half(), TRI, 7)], size=(8, 8), origin=0.0)
        self.assertEqual(int((ident == 7).sum()), 36)        # i + j < 8: the slanted edge is not top or left
        self.assertTrue(ident[0, 0] == 7 and ident[7, 0] == 7 and ident[0, 7] == 7 and ident[4, 4] == 0)
        depth, ident = fr.raster([(half(), TRI, 7)], size=(8, 8), origin=0.5)
        self.assertEqual(int((ident == 7).sum()), 28)        # i + j < 7

    def test_two_halves_share_no_pixel_and_miss_none(self):
        other = np.array([[1.0, 1.0, 0.5, 1.0], [1.0, -1.0, 0.5, 1.0], [-1.0, -1.0, 0.5, 1.0]])
        for origin in (0.0, 0.5):
            a = fr.raster([(half(), TRI, 1)], size=(8, 8), origin=origin)[1]
            b = fr.raster([(other, TRI, 2)], size=(8, 8), origin=origin)[1]
            self.assertEqual(int(((a == 1) & (b == 2)).sum()), 0)
            self.assertEqual(int(((a == 1) | (b == 2)).sum()), 64)   # the shared edge belongs to exactly one

    def test_the_nearer_draw_wins_and_depth_is_z_over_w(self):
        depth, ident = fr.raster([(half(0.5), TRI, 1), (half(0.25) * 2.0, TRI, 2)], size=(8, 8))
        self.assertTrue((ident[ident != 0] == 2).all())      # 0.5 / 2 = 0.25 is nearer than 0.5
        self.assertTrue(np.allclose(depth[ident == 2], 0.25))

    def test_winding_does_not_matter_and_a_vertex_behind_the_eye_is_clipped(self):
        back = half()[::-1].copy()
        self.assertEqual(int((fr.raster([(back, TRI, 3)], size=(8, 8))[1] == 3).sum()), 36)
        behind = half()
        behind[2] = (-1.0, -3.0, 0.5, -1.0)
        depth, ident = fr.raster([(behind, TRI, 4)], size=(8, 8))
        self.assertTrue((ident == 4).any() and np.isfinite(depth[ident == 4]).all())
        # The plan's own triangle (the third vertex (-1, -1, 0.5, -1)) has y = w at every vertex: it lies in the
        # plane of the picture's upper border, has no area on the screen and draws nothing.
        flat = half()
        flat[2, 3] = -1.0
        self.assertEqual(int((fr.raster([(flat, TRI, 4)], size=(8, 8))[1] != 0).sum()), 0)

    def test_no_number_draws_nothing(self):
        bad = half()
        bad[1, 0] = np.nan
        self.assertEqual(int((fr.raster([(bad, TRI, 5)], size=(8, 8))[1] != 0).sum()), 0)

    # --- beyond the plan's five -------------------------------------------------------------------------

    def test_window_is_pixels_from_the_corner_with_y_down(self):
        clip = np.array([[-1.0, 1.0, 0.5, 1.0], [2.0, -2.0, 1.0, 2.0], [0.0, 0.0, 3.0, 4.0]])
        x, y, zw, w = fr.window(clip)
        self.assertEqual(list(x), [0.0, 1120.0, 560.0])
        self.assertEqual(list(y), [0.0, 720.0, 360.0])
        self.assertEqual(list(zw), [0.5, 0.5, 0.75])
        self.assertEqual(list(w), [1.0, 2.0, 4.0])
        x, y, _, _ = fr.window(clip, size=(8, 4))
        self.assertEqual((list(x), list(y)), ([0.0, 8.0, 4.0], [0.0, 4.0, 2.0]))

    def test_depth_is_the_plane_through_the_vertices_at_the_sampling_place(self):
        """Window corners (0, 0), (8, 0), (0, 8) with z / w 0.2, 0.6, 0.4: at the place (x, y) the depth is
        0.2 + 0.4 x / 8 + 0.2 y / 8; pixel (i, j) is sampled at (i + origin, j + origin)."""
        tri = half()
        tri[:, 2] = (0.2, 0.6, 0.4)
        for origin in (0.0, 0.5, 0.25):
            depth, ident = fr.raster([(tri, TRI, 1)], size=(8, 8), origin=origin)
            self.assertTrue((ident == 1).any())
            for j in range(8):
                for i in range(8):
                    if ident[j, i]:
                        self.assertAlmostEqual(depth[j, i], 0.2 + 0.05 * (i + origin) + 0.025 * (j + origin), places=12)
                    else:
                        self.assertEqual(depth[j, i], np.inf)

    def test_depth_is_linear_on_the_screen_whatever_w_is(self):
        """The same window triangle with w = 1, 2, 4: z / w is 0.2, 0.6, 0.4 again and stays a plane."""
        tri = half()
        tri[:, 2] = (0.2, 0.6, 0.4)
        tri = tri * np.array([[1.0], [2.0], [4.0]])
        depth, ident = fr.raster([(tri, TRI, 1)], size=(8, 8), origin=0.0)
        self.assertEqual(int((ident == 1).sum()), 36)
        for j, i in zip(*np.nonzero(ident)):
            self.assertAlmostEqual(depth[j, i], 0.2 + 0.05 * i + 0.025 * j, places=12)

    def test_an_attribute_is_correct_in_perspective(self):
        """A fifth column is an attribute: a / w and 1 / w are linear on the screen. Along the top edge from
        (w 1, a 0) to (w 3, a 3) the attribute at the window fraction t is (t) / (1 - t + t / 3) ... by hand:
        a / w goes 0 -> 1 and 1 / w goes 1 -> 1/3, so a = t / (1 - 2 t / 3)."""
        tri = np.array([[-1.0, 1.0, 0.5, 1.0, 0.0], [3.0, 3.0, 1.5, 3.0, 3.0], [-1.0, -1.0, 0.5, 1.0, 0.0]])
        got = {}
        for t, ys, xs, depth, attrs in fr.fragments(tri, TRI, size=(8, 8), origin=0.0):
            for y, x, a in zip(ys, xs, attrs[:, 0]):
                got[(int(x), int(y))] = float(a)
        for i in range(8):
            t = i / 8.0
            self.assertAlmostEqual(got[(i, 0)], t / (1.0 - 2.0 * t / 3.0), places=12)

    def test_the_sampling_place_can_differ_by_axis(self):
        """A sample at (i, j + 0.25): the upper sample of a pixel sampled twice."""
        depth, ident = fr.raster([(half(), TRI, 1)], size=(8, 8), origin=(0.0, 0.25))
        self.assertEqual(int((ident == 1).sum()), sum(1 for i in range(8) for j in range(8) if i + j + 0.25 < 8))

    def test_the_pictures_edge_cuts_a_triangle_larger_than_the_picture(self):
        big = np.array([[-3.0, 3.0, 0.5, 1.0], [7.0, 3.0, 0.5, 1.0], [-3.0, -7.0, 0.5, 1.0]])    # the slanted edge: x - y = 4
        for origin in (0.0, 0.5):
            depth, ident = fr.raster([(big, TRI, 1)], size=(8, 6), origin=origin)
            self.assertEqual(ident.shape, (6, 8))
            self.assertEqual(int((ident == 1).sum()), 48)
            self.assertTrue(np.allclose(depth, 0.5, atol=1e-12))
        cut = np.array([[-9.0, 9.0, 0.5, 1.0], [9.0, 9.0, 0.5, 1.0], [-9.0, -9.0, 0.5, 1.0]])    # through the centre
        self.assertEqual(int((fr.raster([(cut, TRI, 1)], size=(8, 6), origin=0.5)[1] == 1).sum()), 24)
        far_off = half() + np.array([40.0, 0.0, 0.0, 0.0])
        self.assertEqual(int((fr.raster([(far_off, TRI, 1)], size=(8, 8))[1] != 0).sum()), 0)
        huge = big * np.array([1e7, 1e7, 1.0, 1.0])       # beyond the guard band: cut in clip space first
        self.assertEqual(int((fr.raster([(huge, TRI, 1)], size=(8, 6))[1] == 1).sum()), 48)

    def test_the_near_cut_agrees_with_a_test_that_cuts_nothing(self):
        """A triangle with a vertex behind the eye, against the homogeneous inside test (the place k (nx, ny, 1)
        is a mix of the three clip vertices with weights of one sign): no clipping in that test at all."""
        tri = np.array([[-0.83, 0.71, 0.3, 1.0], [0.9, 0.62, 0.5, 1.3], [0.1, -0.4, 0.2, -0.7]])
        w, h = 16, 12
        depth, ident = fr.raster([(tri, TRI, 1)], size=(w, h), origin=0.5, zclip=False)
        m = np.array([tri[:, 0], tri[:, 1], tri[:, 3]])
        inside = 0
        for j in range(h):
            for i in range(w):
                nx, ny = (i + 0.5) / w * 2.0 - 1.0, 1.0 - (j + 0.5) / h * 2.0
                lam = np.linalg.solve(m, np.array([nx, ny, 1.0]))
                if np.abs(lam).min() < 1e-9:
                    continue
                want = bool((lam > 0).all())
                inside += want
                self.assertEqual(bool(ident[j, i]), want, (i, j))
                if want:
                    self.assertAlmostEqual(depth[j, i] / float(lam @ tri[:, 2]), 1.0, places=9)
        self.assertGreater(inside, 20)
        self.assertLess(inside, w * h - 20)

    def test_depth_outside_the_clip_range_is_not_drawn(self):
        tri = half()
        tri[:, 2] = (-0.2, 0.6, 0.4)      # z / w below 0 near the first corner: in front of the near plane
        depth, ident = fr.raster([(tri, TRI, 1)], size=(8, 8))
        self.assertTrue(ident[0, 7] == 1 and ident[0, 0] == 0)
        self.assertTrue((depth[ident == 1] >= 0.0).all())
        self.assertEqual(int((fr.raster([(tri, TRI, 1)], size=(8, 8), zclip=False)[1] == 1).sum()), 36)

    def test_an_equal_depth_goes_to_the_later_draw(self):
        """The game's depth test is greater-or-equal on its reversed range (0x2200 = 0x00708766)."""
        depth, ident = fr.raster([(half(), TRI, 1), (half(), TRI, 2)], size=(8, 8))
        self.assertTrue((ident[ident != 0] == 2).all())

    def test_what_covers_no_sampling_place_draws_nothing(self):
        sliver = np.array([[-0.99, 0.99, 0.5, 1.0], [-0.90, 0.99, 0.5, 1.0], [-0.99, 0.90, 0.5, 1.0]])
        self.assertEqual(int((fr.raster([(sliver, TRI, 1)], size=(8, 8), origin=0.5)[1] != 0).sum()), 0)
        line = np.array([[-1.0, 1.0, 0.5, 1.0], [0.0, 0.0, 0.5, 1.0], [1.0, -1.0, 0.5, 1.0]])
        self.assertEqual(int((fr.raster([(line, TRI, 1)], size=(8, 8))[1] != 0).sum()), 0)

    def test_an_ident_of_zero_is_refused(self):
        with self.assertRaises(ValueError):
            fr.raster([(half(), TRI, 0)], size=(8, 8))

    def test_the_stored_depth_is_reversed(self):
        self.assertEqual(fr.stored_depth(0.25), 0.75)        # the rows' range: scale -1, offset 1
        self.assertEqual(fr.stored_depth(0.25, 1.0, 0.0), 0.25)
        self.assertGreater(fr.stored_depth(0.1), fr.stored_depth(0.9))    # the nearer, the larger


class Motion(unittest.TestCase):
    def test_share_inside_and_outside_a_mask(self):
        a = np.zeros((4, 4, 3), dtype=np.uint8)
        b = a.copy()
        b[0, :2] = 9      # two pixels change by 9 levels, inside
        b[3, 0] = 8       # one by exactly 8: not "more than 8"
        mask = np.zeros((4, 4), dtype=bool)
        mask[0] = True
        inside, outside = fr.motion_share(a, b, mask, levels=8)
        self.assertEqual((inside, outside), (0.5, 0.0))

    def test_a_falling_level_counts_and_one_channel_is_enough(self):
        a = np.full((2, 2, 3), 200, dtype=np.uint8)
        b = a.copy()
        b[1, 1, 2] = 100                                     # unsigned bytes: 100 - 200 must not wrap
        mask = np.zeros((2, 2), dtype=bool)
        mask[0, 0] = True
        self.assertEqual(fr.motion_share(a, b, mask), (0.0, 1 / 3))
        inside, outside = fr.motion_share(a, b, np.zeros((2, 2), dtype=bool))
        self.assertTrue(np.isnan(inside) and outside == 0.25)          # an empty side has no share

    def test_a_mask_of_another_size_is_brought_to_the_shots(self):
        mask = np.zeros((2, 4), dtype=bool)
        mask[0, 3] = True
        big = fr.fit_mask(mask, (4, 8))
        self.assertEqual(big.shape, (4, 8))
        self.assertEqual(int(big.sum()), 4)
        self.assertTrue(big[:2, 6:].all())
        with self.assertRaises(ValueError):
            fr.motion_share(np.zeros((4, 4, 3), np.uint8), np.zeros((4, 5, 3), np.uint8), np.zeros((4, 4), bool))


def write_indices(folder, name, words, index32=False):
    """An index buffer as the guest holds it: big-endian words."""
    (folder / "geo").mkdir(exist_ok=True)
    (folder / "geo" / name).write_bytes(struct.pack(">%d%s" % (len(words), "I" if index32 else "H"), *words))
    return "geo/" + name


class Indices(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def row(self, ib, idx):
        return {"kind": "draw", "args": ["0x00000006", "0x00000000", "0x%08X" % ib.get("start", 0), "0x%08X" % ib["count"]],
                "ib": ib, "idx": idx}

    def test_a_whole_buffer_from_its_file(self):
        words = [9, 9, 0, 1, 2, 0xFFFF, 2, 1, 3, 7]
        name = write_indices(self.dir, "a.bin", words)
        ib = {"index32": False, "endian": 1, "start": 2, "count": 7, "size": 20, "file": name, "file_offset": 0, "file_bytes": 20}
        idx, state, note = fr.draw_indices(self.row(ib, [0, 1]), self.dir)
        self.assertEqual((idx, state), ([0, 1, 2, -1, 2, 1, 3], "whole"))     # the reset index is -1, as in "idx"

    def test_the_draws_own_range_from_its_file(self):
        name = write_indices(self.dir, "b.bin", [5, 6, 7, 0xFFFFFFFF, 8], index32=True)
        ib = {"index32": True, "endian": 2, "start": 100, "count": 5, "size": 4 << 20, "file": name, "file_offset": 400, "file_bytes": 20}
        idx, state, note = fr.draw_indices(self.row(ib, []), self.dir)
        self.assertEqual((idx, state), ([5, 6, 7, -1, 8], "whole"))

    def test_todays_row_with_every_index_is_whole_and_one_cut_short_is_partial(self):
        ib = {"index32": False, "endian": 1, "start": 0, "count": 6, "size": 12}
        self.assertEqual(fr.draw_indices(self.row(ib, [0, 1, 2, 2, 1, 3]), self.dir)[:2], ([0, 1, 2, 2, 1, 3], "whole"))
        ib = dict(ib, count=18722)
        idx, state, note = fr.draw_indices(self.row(ib, list(range(512))), self.dir)
        self.assertEqual((len(idx), state), (512, "partial"))
        self.assertIn("512 of 18722", note)

    def test_a_file_that_does_not_hold_the_range_is_not_read(self):
        """Fewer bytes than the row says, a range outside the file, a missing file: the row's own indices, and
        the note says why."""
        name = write_indices(self.dir, "c.bin", [0, 1, 2])
        for ib in ({"file": name, "file_offset": 0, "file_bytes": 12},            # the file has 6 bytes
                   {"file": name, "file_offset": 4, "file_bytes": 6},             # the draw starts before the file
                   {"file": "geo/none.bin", "file_offset": 0, "file_bytes": 12},
                   {"file": name, "file_offset": 0, "file_bytes": 6, "endian": 3}):
            ib = dict({"index32": False, "endian": 1, "start": 0, "count": 6, "size": 12}, **ib)
            idx, state, note = fr.draw_indices(self.row(ib, [0, 1, 2]), self.dir)
            self.assertEqual((idx, state), ([0, 1, 2], "partial"), ib)
            self.assertIn("file", note)
        # a file that would hold the draw's range and is not of the size the row gives: not the bytes the row means
        name = write_indices(self.dir, "d.bin", [0, 1, 2, 3, 4, 5])
        ib = {"index32": False, "endian": 1, "start": 0, "count": 3, "size": 12, "file": name, "file_offset": 0, "file_bytes": 8}
        idx, state, note = fr.draw_indices(self.row(ib, [0, 1]), self.dir)
        self.assertEqual((idx, state), ([0, 1], "partial"))
        self.assertIn("12 bytes, not the row's 8", note)

    def test_a_draw_without_an_index_buffer_lists_its_vertices(self):
        row = {"kind": "draw", "args": ["0x00000004", "0x00000003", "0x00000006"], "ib": None, "idx": [3, 4]}
        self.assertEqual(fr.draw_indices(row, self.dir)[:2], ([3, 4, 5, 6, 7, 8], "whole"))

    def test_triangles_are_of_distinct_vertices(self):
        idx = [4, 5, 6, -1, 6, 5, 9]
        ids, tris = fr.triangles_of(6, idx)
        self.assertEqual(list(ids), [4, 5, 6, 9])
        self.assertEqual([tuple(t) for t in tris], [(0, 1, 2), (2, 1, 3)])
        self.assertIsNone(fr.triangles_of(13, idx))


VS = "00000000000000AA"
MICROCODE = """/*    0.0 */       exec
/*    3   */          vfetch_full r2.xyz1, r0.x, vf0, DataFormat=FMT_32_32_32_FLOAT, Stride=3, Signed=true, NumFormat=integer, PrefetchCount=3
/*    0.1 */       alloc position
/*    1.0 */       exece
/*    5   */          dp4 oPos.x___, c0.xyzw, r2.xyzw
/*    6   */          dp4 oPos._y__, c1.xyzw, r2.xyzw
/*    7   */          dp4 oPos.__z_, c2.xyzw, r2.xyzw
/*    8   */          dp4 oPos.___w, c3.xyzw, r2.xyzw
"""
ENTRY = {"0x" + VS: {"base": 0, "layout": "dot", "pos_fetch": -1}}


class MadeCapture(unittest.TestCase):
    """A quad of four vertices over the picture's left half, drawn by a made shader oPos = c0..c3 . position."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir / "dumps").mkdir()
        (self.dir / "dumps" / ("shader_%s.ucode.vert" % VS)).write_text(MICROCODE)
        (self.dir / "geo").mkdir()
        corners = [(-1.0, 1.0, 0.25), (0.0, 1.0, 0.25), (-1.0, -1.0, 0.25), (0.0, -1.0, 0.25)]
        (self.dir / "geo" / "v.bin").write_bytes(b"".join(struct.pack(">3f", *c) for c in corners))
        bank = [0.0] * 1024
        bank[0:16] = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        self.row = {
            "kind": "draw", "frame": 1, "line": 1, "in_scene": True, "vs_hash": "0x" + VS,
            "args": ["0x00000006", "0x00000000", "0x00000000", "0x00000004"],
            "ib": {"index32": False, "endian": 1, "start": 0, "count": 4, "size": 8},
            "idx": [0, 1, 2, 3], "base_vertex": 0, "bank": bank, "vs_lit": [],
            "rs": {"2200": "0x00708766", "2202": "0x00000004", "2113": "0xBF800000", "2114": "0x3F800000"},
            "fetches": [{"i": 0, "instr": 0, "slot": 95, "reg": 2, "fmt": 57, "off": 0, "stride": 3, "mini": False,
                         "signed": True, "norm": False, "exp": 0, "swz": 2696}],
            "streams": [{"slot": 95, "phys": 0, "size": 48, "stride": 12, "endian": 2, "file": "geo/v.bin"}],
        }

    def tearDown(self):
        self.tmp.cleanup()

    def geometry(self, **kw):
        return fr.row_geometry(self.row, self.dir, dumps=self.dir / "dumps", table=ENTRY, **kw)

    def test_the_microcode_and_the_table_entry_give_the_same_quad(self):
        for wind in (True, False):
            g = self.geometry(wind=wind)
            self.assertEqual((g.state, g.reason), ("whole", None))
            self.assertEqual(g.clip.shape, (4, 4))
            self.assertTrue(np.array_equal(g.clip, np.array([[-1, 1, 0.25, 1], [0, 1, 0.25, 1], [-1, -1, 0.25, 1], [0, -1, 0.25, 1.0]])))
            depth, ident = fr.raster([(g.clip, g.tris, 1)], size=(8, 8))
            self.assertEqual(int((ident == 1).sum()), 32)
            self.assertTrue(ident[:, :4].all() and not ident[:, 4:].any())
        clip = fr.row_clip(self.row, self.dir, wind=True, dumps=self.dir / "dumps", table=ENTRY)
        self.assertEqual(clip.shape, (4, 4))                # one for each index that is not a reset

    def test_the_constants_of_the_row_move_it(self):
        self.row["bank"][3] = 1.0                           # c0.w: x + 1, the quad over the right half
        for wind in (True, False):
            g = self.geometry(wind=wind)
            ident = fr.raster([(g.clip, g.tris, 1)], size=(8, 8))[1]
            self.assertTrue(ident[:, 4:].all() and not ident[:, :4].any())

    def test_the_shaders_literals_lie_over_the_bank(self):
        self.row["vs_lit"] = [{"reg": 0, "v": [1.0, 0.0, 0.0, 1.0]}]      # c0 again, with x + 1
        ident = fr.raster([(self.geometry().clip, self.geometry().tris, 1)], size=(8, 8))[1]
        self.assertTrue(ident[:, 4:].all() and not ident[:, :4].any())

    def test_the_base_vertex_is_added(self):
        self.row.update(idx=[0, 1, 2], base_vertex=1, args=["0x00000006", "0x00000001", "0x00000000", "0x00000003"])
        self.row["ib"]["count"] = 3
        for wind in (True, False):
            g = self.geometry(wind=wind)
            self.assertEqual(list(g.ids), [1, 2, 3])
            self.assertTrue(np.array_equal(g.clip[:, :2], np.array([[0.0, 1.0], [-1.0, -1.0], [0.0, -1.0]])), wind)
            self.assertEqual([tuple(t) for t in g.tris], [(0, 1, 2)])

    def test_what_cannot_be_replayed_says_why(self):
        row = dict(self.row, streams=[])
        self.assertEqual(self.geometry_of(row).reason, "no streams in the row")
        row = dict(self.row, args=["0x0000000D", "0x00000000", "0x00000000", "0x00000004"])
        self.assertEqual(self.geometry_of(row).reason, "primitive type 13")
        row = dict(self.row, vs_hash="0x00000000000000BB")
        self.assertEqual(self.geometry_of(row).reason, "no microcode dump")
        self.assertEqual(self.geometry_of(row, wind=False).reason, "no table entry")
        row = dict(self.row, idx=[0, 1, 2, 7], ib=dict(self.row["ib"]))
        self.assertEqual(self.geometry_of(row).reason, "a fetch outside the stream's dump")
        row = dict(self.row, kind="tess")
        self.assertEqual(self.geometry_of(row).reason, "a tess row (terrain)")
        for g in (self.geometry_of(dict(self.row, streams=[])), self.geometry_of(dict(self.row, kind="tess"))):
            self.assertIsNone(g.clip)

    def geometry_of(self, row, wind=True):
        return fr.row_geometry(row, self.dir, wind=wind, dumps=self.dir / "dumps", table=ENTRY)

    def test_a_draw_cut_short_is_partial_and_never_judged(self):
        self.row["ib"]["count"] = 600
        self.row["args"][3] = "0x00000258"
        g = self.geometry()
        self.assertEqual(g.state, "partial")
        self.assertIn("4 of 600", g.note)
        self.assertFalse(fr.judged(self.row, g, "still"))
        self.row["ib"]["count"] = 4
        g = self.geometry()
        self.assertTrue(fr.judged(self.row, g, "still"))
        for kind in ("trunk", "leaf", "plant", "moving", "skinned", "unlisted"):            # a swaying shader, bones, no entry
            self.assertFalse(fr.judged(self.row, g, kind), kind)
        self.assertFalse(fr.judged(dict(self.row, rs=dict(self.row["rs"], **{"2202": "0x0000000C"})), g, "still"))   # alpha test
        self.assertFalse(fr.judged(dict(self.row, rs=dict(self.row["rs"], **{"2202": "0x87000014"})), g, "still"))   # alpha-to-mask
        self.assertFalse(fr.judged(dict(self.row, rs=dict(self.row["rs"], **{"2200": "0x00708762"})), g, "still"))   # no depth write

    def test_the_kinds_of_draws(self):
        table = {"0x" + VS: {"base": 0, "layout": "dot"},
                 "0x475EC9F795E5EDBB": {"base": 0, "layout": "dot", "deformed": True},
                 "0x7C5710DEF3EE33C4": {"base": 0, "layout": "dot", "deformed": True},
                 "0x00000000000000CC": {"base": 0, "layout": "dot", "skin": {}},
                 "0x00000000000000DD": {"rejected": "no transform"}}
        want = {VS: "still", "475EC9F795E5EDBB": "trunk", "7C5710DEF3EE33C4": "moving", "00000000000000CC": "skinned",
                "00000000000000DD": "unlisted", "00000000000000EE": "unlisted"}
        for h, kind in want.items():
            self.assertEqual(fr.kind_of({"vs_hash": "0x" + h}, table), kind, h)
        self.assertTrue(fr.moving({"vs_hash": "0x475EC9F795E5EDBB"}, table))
        self.assertFalse(fr.moving({"vs_hash": "0x" + VS}, table))

    def test_a_position_that_depends_on_a_vertex_texture_is_refused_and_a_cut_vertex_does_not_look_like_one(self):
        fetch = "/*    4   */          tfetch2D r3, r2.xy, tf16, UseComputedLOD=false\n"
        (self.dir / "dumps" / ("shader_%s.ucode.vert" % VS)).write_text(
            MICROCODE.replace("/*    0.1 */", fetch + "/*    0.1 */").replace("dp4 oPos.x___, c0.xyzw, r2.xyzw", "dp4 oPos.x___, c0.xyzw, r3.xyzw"))
        self.assertEqual(self.geometry().reason, "a position that depends on a vertex texture")
        (self.dir / "dumps" / ("shader_%s.ucode.vert" % VS)).write_text(MICROCODE.replace("/*    0.1 */", fetch + "/*    0.1 */"))
        self.assertIsNone(self.geometry().reason)                # fetched and not used for the position
        (self.dir / "dumps" / ("shader_%s.ucode.vert" % VS)).write_text(MICROCODE)
        self.row["bank"][0] = float("nan")                       # every x is no number: the shader's own doing
        g = self.geometry()
        self.assertIsNone(g.reason)
        self.assertEqual(int((fr.raster([(g.clip, g.tris, 1)], size=(8, 8))[1] != 0).sum()), 0)

    def test_the_reader_of_rows(self):
        path = self.dir / "made.jsonl"
        lines = [{"kind": "texture", "file": "t.bin", "bytes": 4, "fc": []},
                 {"kind": "draw", "frame": 1, "positions": [1, 2], "x": 1},
                 {"kind": "draw", "frame": 2, "x": 2},
                 {"kind": "cmdgap", "frame": 1, "x": 3},
                 {"kind": "draw", "frame": 11, "x": 4}]
        path.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in lines) + '{"kind":"draw","frame":1,"x":')
        got = cr.rows(path, ("draw", "texture"), frame=1)
        self.assertEqual([(r["kind"], r["line"]) for r in got], [("texture", 1), ("draw", 2)])
        self.assertNotIn("positions", got[1])
        self.assertEqual([r["x"] for r in cr.rows(path, ("draw",))], [1, 2, 4])           # the cut last line is left out

    def test_elements(self):
        self.assertEqual(cr.element(struct.pack(">2f", 1.5, -2.0), 0, {"DataFormat": "FMT_32_32_FLOAT"}), [1.5, -2.0, 0.0, 1.0])
        self.assertEqual(cr.element(struct.pack(">4e", 1.0, 2.0, 3.0, 4.0), 0, {"DataFormat": "FMT_16_16_16_16_FLOAT"}), [2.0, 1.0, 4.0, 3.0])
        self.assertEqual(cr.element(struct.pack(">I", 0x04030201), 0, {"DataFormat": "FMT_8_8_8_8", "NumFormat": "integer"}), [1.0, 2.0, 3.0, 4.0])
        self.assertEqual(cr.element(struct.pack(">I", 0xFF000000), 0, {"DataFormat": "FMT_8_8_8_8"}), [0.0, 0.0, 0.0, 1.0])
        with self.assertRaises(NotImplementedError):
            cr.element(bytes(8), 0, {"DataFormat": "FMT_16_16"})


class Judgement(unittest.TestCase):
    """Two made draws and a stored depth: draw 1 the upper left half at z / w 0.25, draw 2 the whole picture
    behind it at 0.75."""

    def setUp(self):
        whole = np.array([[-1.0, 1.0, 0.75, 1.0], [1.0, 1.0, 0.75, 1.0], [-1.0, -1.0, 0.75, 1.0], [1.0, -1.0, 0.75, 1.0]])
        self.depth, self.ident = fr.raster([(half(0.25), TRI, 1), (whole, np.array([[0, 1, 2], [2, 1, 3]]), 2)], size=(8, 8))
        self.stored = fr.stored_depth(self.depth)

    def test_the_replays_own_depth_agrees_wholly(self):
        a = fr.agreement(self.stored, self.depth, self.ident, [1, 2], 1e-9)
        self.assertEqual((a.pixels, a.within, a.nearer, a.farther), (64, 64, 0, 0))
        self.assertEqual(a.share, 1.0)
        self.assertEqual(fr.agreement(self.stored, self.depth, self.ident, [1], 1e-9).pixels, 36)

    def test_something_in_front_and_something_missing_are_told_apart(self):
        stored = self.stored.copy()
        stored[0, 0] = 0.9            # the game has something nearer there (reversed: larger)
        stored[7, 7] = 0.0            # the game has nothing there
        stored[7, 6] += 5e-4          # within a tolerance of 1e-3
        a = fr.agreement(stored, self.depth, self.ident, [1, 2], 1e-3)
        self.assertEqual((a.pixels, a.within, a.nearer, a.farther), (64, 62, 1, 1))
        self.assertAlmostEqual(a.share, 62 / 64)
        a = fr.agreement(stored, self.depth, self.ident, [1, 2], 1e-4)
        self.assertEqual((a.within, a.nearer, a.farther), (61, 2, 1))
        self.assertEqual(fr.agreement(stored, self.depth, self.ident, [9], 1e-3).pixels, 0)
        self.assertTrue(np.isnan(fr.agreement(stored, self.depth, self.ident, [9], 1e-3).share))

    def test_the_relation_is_found_and_the_other_form_is_far_off(self):
        ids = [1, 2]
        rel = fr.relations(self.stored, self.depth, self.ident, ids)
        self.assertEqual(sorted(rel), ["1 - z / w", "z / w"])
        self.assertLess(rel["1 - z / w"].tolerance, 1e-12)
        self.assertGreater(rel["z / w"].tolerance, 0.4)
        self.assertEqual(fr.best_relation(rel), "1 - z / w")
        rel = fr.relations(1.0 - self.stored, self.depth, self.ident, ids)
        self.assertEqual(fr.best_relation(rel), "z / w")
        hidden = self.stored.copy()
        hidden[self.ident == 2] = 0.9                         # 28 of 64 pixels hidden: the percentile is of no use
        rel = fr.relations(hidden, self.depth, self.ident, ids)
        self.assertGreater(rel["1 - z / w"].tolerance, 0.4)
        self.assertEqual(fr.best_relation(rel), "1 - z / w")   # ... and the form is still found
        self.assertEqual(fr.best_relation(fr.relations(self.stored, self.depth, self.ident, [9])), "1 - z / w")

    def test_the_tolerance_is_the_99th_percentile(self):
        stored = self.stored.copy()
        flat = np.flatnonzero(self.ident.ravel() == 2)
        stored.ravel()[flat[:2]] += 0.5                      # 2 of 28 pixels of draw 2 far off: above the 99th percentile
        rel = fr.relations(stored, self.depth, self.ident, [2])["1 - z / w"]
        self.assertEqual(rel.pixels, 28)
        self.assertGreater(rel.tolerance, 0.2)
        self.assertEqual(rel.at_tolerance.within, 28)        # 99% by construction: it proves nothing alone
        self.assertEqual((rel.fixed.within, rel.fixed.nearer), (26, 2))
        self.assertFalse(fr.relation_verdict(rel)[0])        # 92.9% at the fixed tolerance

    def test_the_bound_is_95_percent_at_the_fixed_tolerance_and_can_fail(self):
        self.assertEqual(fr.FIXED_TOLERANCE, 16 * 2.0 ** -24)
        ids = [1, 2]
        whole = fr.relations(self.stored, self.depth, self.ident, ids)["1 - z / w"]
        self.assertEqual(fr.relation_verdict(whole)[0], True)
        flat = np.flatnonzero(self.ident.ravel() != 0)
        for off, met in ((3, True), (4, False)):             # 61 of 64 is 95.3%, 60 of 64 is 93.75%
            for by in (2.0 * fr.FIXED_TOLERANCE, -2.0 * fr.FIXED_TOLERANCE):
                stored = self.stored.copy()
                stored.ravel()[flat[:off]] += by
                rel = fr.relations(stored, self.depth, self.ident, ids)["1 - z / w"]
                verdict, text = fr.relation_verdict(rel)
                self.assertEqual(verdict, met, text)
                self.assertIn("95", text)
        stored = self.stored.copy()
        stored.ravel()[flat[:8]] += 0.5 * fr.FIXED_TOLERANCE      # inside the tolerance: nothing is off
        self.assertEqual(fr.relations(stored, self.depth, self.ident, ids)["1 - z / w"].fixed.share, 1.0)
        verdict, text = fr.relation_verdict(fr.relations(self.stored, self.depth, self.ident, [9])["1 - z / w"])
        self.assertFalse(verdict)                             # no pixel: no verdict
        self.assertIn("not decidable", text)

    def test_the_share_among_the_pixels_with_nothing_nearer(self):
        stored = self.stored.copy()
        flat = np.flatnonzero(self.ident.ravel() != 0)
        stored.ravel()[flat[:16]] = 0.99                      # 16 hidden behind something the replay did not draw
        stored.ravel()[flat[16:20]] = 0.0                     # 4 where the game has nothing
        a = fr.agreement(stored, self.depth, self.ident, [1, 2], fr.FIXED_TOLERANCE)
        self.assertEqual((a.within, a.nearer, a.farther), (44, 16, 4))
        self.assertAlmostEqual(a.share, 44 / 64)
        self.assertAlmostEqual(a.share_unhidden, 44 / 48)


class AlphaCover(unittest.TestCase):
    def row(self, word="0x8700001C", ref="0x00000000", fade=1.0):
        pconst = [0.0] * 64
        pconst[63] = fade
        return {"rs": {"2202": word, "210E": ref}, "pconst": pconst, "ps_hash": "0x7CD57B81550F19E3"}

    def texture(self, alpha):
        a = np.asarray(alpha, dtype=np.float64)
        return fr.Alpha(a, wrap=True)

    def test_two_samples_under_the_dither_and_under_AA(self):
        """Sample 0 at alpha >= 0.5 - o / 8, sample 1 at alpha >= 1 - o / 8; o by the pixel's parity, index
        (x & 1) * 2 + (y & 1), two bits each from bit 24 of 0x2202. Under 87: o = 3, 1, 0, 2."""
        tex = self.texture([[0.4]])                           # one texel: every coordinate reads 0.4
        uv = np.full((4, 2), 0.5)
        parity = np.arange(4)
        first, second = fr.alpha_cover(self.row(), uv, tex, parity, samples=2, scale=1.0)
        self.assertEqual(list(first), [True, True, False, True])      # thresholds 0.125, 0.375, 0.5, 0.25
        self.assertEqual(list(second), [False, False, False, False])  # 0.625, 0.875, 1, 0.75
        first, second = fr.alpha_cover(self.row(), uv, self.texture([[0.3]]), parity, samples=2, scale=1.0)
        self.assertEqual(list(first), [True, False, False, True])     # the step is an eighth: 0.3 is below 0.375
        first, second = fr.alpha_cover(self.row(), uv, self.texture([[0.8]]), parity, samples=2, scale=1.0)
        self.assertEqual((list(first), list(second)), ([True] * 4, [True, False, False, True]))
        first, second = fr.alpha_cover(self.row("0xAA00001C"), uv, self.texture([[0.75]]), parity, samples=2, scale=1.0)
        self.assertEqual((list(first), list(second)), ([True] * 4, [True] * 4))       # 0.25 and 0.75 everywhere
        first, second = fr.alpha_cover(self.row("0xAA00001C"), uv, self.texture([[1.0]]), parity, samples=2, scale=0.5)
        self.assertEqual((list(first), list(second)), ([True] * 4, [False] * 4))      # the late draws: one of two

    def test_the_scale_is_the_rows_fade_word_times_c26w(self):
        row = self.row(fade=0.5)
        self.assertEqual(fr.alpha_scale(row, c26w=1.0), 0.5)
        self.assertEqual(fr.alpha_scale(row, c26w=0.5), 0.25)
        with self.assertRaises(cr.Unreadable):
            fr.alpha_scale(row)                               # no pixel registers in the row: c26.w is not guessed

    def test_one_sample_keeps_only_alpha_exactly_one_at_a_scale_of_a_half(self):
        """No multisampling: one sample, alpha >= 1 - o / 4; under AA 0.5, on an alpha of at most 0.5."""
        tex = self.texture([[1.0, 254.0 / 255.0]])
        uv = np.array([[0.25, 0.5], [0.75, 0.5]])             # the two texels' centres
        first, second = fr.alpha_cover(self.row("0xAA00001C"), uv, tex, np.zeros(2, int), samples=1, scale=0.5)
        self.assertEqual((list(first), list(second)), ([True, False], [False, False]))

    def test_bilinear_at_the_base_level(self):
        tex = self.texture([[0.0, 1.0]])
        self.assertTrue(np.allclose(tex.sample(np.array([[0.25, 0.5], [0.5, 0.5], [0.75, 0.5], [0.0, 0.5]])), [0.0, 0.5, 1.0, 0.5]))
        clamp = fr.Alpha(np.array([[0.0, 1.0]]), wrap=False)
        self.assertTrue(np.allclose(clamp.sample(np.array([[0.0, 0.5], [1.0, 0.5], [9.0, 0.5]])), [0.0, 1.0, 1.0]))
        self.assertEqual(list(tex.sample(np.array([[np.nan, 0.5]]))), [0.0])

    def test_the_alpha_test_comes_first_and_only_greater_is_read(self):
        tex = self.texture([[0.3]])
        uv, parity = np.full((1, 2), 0.5), np.zeros(1, int)
        ref = "0x%08X" % struct.unpack(">I", struct.pack(">f", 0.35))[0]
        first, _ = fr.alpha_cover(self.row("0x8700001C", ref), uv, tex, parity, samples=2, scale=1.0)
        self.assertEqual(list(first), [False])                # 0.3 passes the mask (0.125) and fails the test
        first, _ = fr.alpha_cover(self.row("0x87000014", ref), uv, tex, parity, samples=2, scale=1.0)
        self.assertEqual(list(first), [True])                 # the test off
        first, _ = fr.alpha_cover(self.row("0x8700000C", "0x00000000"), uv, tex, parity, samples=2, scale=1.0)
        self.assertEqual(list(first), [True])                 # no alpha-to-mask: the test alone
        first, second = fr.alpha_cover(self.row("0x8700000C", "0x00000000"), uv, self.texture([[0.0]]), parity, samples=2, scale=1.0)
        self.assertEqual((list(first), list(second)), ([False], [False]))     # greater: an alpha at the reference fails
        with self.assertRaises(cr.Unreadable):
            fr.alpha_cover(self.row("0x8700001B"), uv, tex, parity, samples=2, scale=1.0)   # function 3
        with self.assertRaises(ValueError):
            fr.alpha_cover(self.row(), uv, tex, parity, samples=4, scale=1.0)


class DepthCopy(unittest.TestCase):
    def test_the_copy_is_the_last_write_of_0x2318_with_source_4_inside_the_scene(self):
        def gap(line, words):
            return {"kind": "cmdgap", "line": line, "cmd": "".join("%08X" % w for w in words)}
        scene = {"kind": "draw", "in_scene": True}
        rows = [gap(1, [0x00002318, 4]),                                  # before the scene's first row: not it
                dict(scene, line=2), gap(3, [0x00002318, 0x00000300]),    # a colour copy
                dict(scene, line=4), gap(5, [0x00012317, 7, 0x00000304]), # two registers from 0x2317: the second is 0x2318
                dict(scene, line=6)]
        self.assertEqual(fr.depth_copy_line(rows), 5)
        self.assertIsNone(fr.depth_copy_line(rows[:4]))


class Twin(unittest.TestCase):
    def test_the_twin_has_the_mesh_and_the_place(self):
        """A mesh drawn twice in the scene (two trees of one model): the twin is the one with the layer row's
        c0..c3."""
        def draw(line, c0x, count=18722, vb=0x1C90E640):
            return {"kind": "draw", "line": line, "ib": {"phys_addr": 0x1C2E9380, "start": 0, "count": count},
                    "vb": {"phys_addr": vb}, "bank": [c0x] + [0.0] * 15 + [9.0]}
        scene = [draw(10, 1.0), draw(11, 2.0, count=9319), draw(12, 2.0, vb=1), draw(13, 2.0), draw(14, 2.0)]
        layer = draw(99, 2.0)
        layer["bank"][16] = 5.0                              # beyond c3: not compared
        self.assertEqual(fr.twin_of(scene, layer)["line"], 13)
        self.assertIsNone(fr.twin_of(scene, draw(99, 3.0)))
        self.assertIsNone(fr.twin_of(scene, {"kind": "draw", "line": 99, "bank": [2.0] * 16}))      # no index buffer
        self.assertIsNone(fr.twin_of(scene, dict(draw(99, 2.0), bank=[])))


class SelfTest(unittest.TestCase):
    def test_the_selftest_passes_and_exits_zero(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = fr.main(["selftest"])
        self.assertEqual(code, 0, out.getvalue())
        self.assertIn("selftest: ok", out.getvalue())

    def test_a_capture_that_is_not_there_exits_two(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = fr.main(["depth-relation", "no-such-capture-000000", "1"])
        self.assertEqual(code, 2)


@unittest.skipUnless(CAPTURE.is_file() and cr.DUMPS.is_dir(), NO_CAPTURE)
class RealRows(unittest.TestCase):
    """Rows of today's bridge capture (512 indices a draw): the reader, and the microcode against the table
    entry on draws that do not move."""

    @classmethod
    def setUpClass(cls):
        cls.frame = fr.load_frame(CAPTURE, 1, depth=False)

    def test_the_frames_rows_and_the_copys_place(self):
        f = self.frame
        self.assertEqual(len([r for r in f.scene if r["kind"] in ("draw", "tess")]), 957)   # research B6
        self.assertEqual(len(f.before_copy), 423)
        self.assertEqual(sum(1 for r in f.scene if r["kind"] == "draw"), 390)

    def test_small_draws_are_whole_and_large_ones_are_said_to_be_cut(self):
        states = {"whole": 0, "partial": 0}
        for r in self.frame.scene:
            if r["kind"] == "draw":
                idx, state, note = fr.draw_indices(r, CAPTURE.parent)
                states[state] += 1
                if not r.get("ib"):                       # DrawVertices: its vertices are listed, never cut
                    self.assertEqual(state, "whole")
                    continue
                self.assertEqual(state == "whole", r["ib"]["count"] <= 512)
                if state == "partial":
                    self.assertIn("512 of %d" % r["ib"]["count"], note)
        self.assertGreater(states["whole"], 50)
        self.assertGreater(states["partial"], 50)

    def test_the_microcode_and_the_table_entry_agree_on_draws_that_do_not_move(self):
        table = fr.load_table()
        done = 0
        for r in self.frame.scene:
            if r["kind"] != "draw" or fr.kind_of(r, table) != "still" or not r.get("ib") or r["ib"]["count"] > 120:
                continue
            a = fr.row_geometry(r, CAPTURE.parent, wind=True, table=table)
            b = fr.row_geometry(r, CAPTURE.parent, wind=False, table=table)
            if a.reason or b.reason:
                continue
            self.assertEqual(a.clip.shape, b.clip.shape)
            scale = np.abs(a.clip).max()
            self.assertLess(np.abs(a.clip - b.clip).max(), 1e-4 * scale, r["line"])
            done += 1
            if done == 12:
                break
        self.assertGreaterEqual(done, 6)

    def test_two_unmoving_draws_in_plain_view_hold_the_stored_depth(self):
        """Lines 3487 and 3490 of the capture: two small props of the trusted plain shader far off in open view
        (out/native-evidence/foliage/t3_depth_relation_120714_f1.txt). Their replayed depth is the game's stored
        depth, reversed, within 16 steps of 2^-24 in nine pixels of ten (3487) and in seven of ten (3490, drawn
        alone here: other draws stand before a part of it); the other form is nowhere near."""
        f = fr.load_frame(CAPTURE, 1)
        self.assertEqual(f.depth.shape, (720, 1120))
        self.assertTrue(0.0 <= f.depth.min() and f.depth.max() <= 1.0)
        self.assertEqual((f.zscale, f.zoffset), (-1.0, 1.0))
        table = fr.load_table()
        for line, share in ((3487, 0.9), (3490, 0.7)):
            row = next(r for r in f.scene if r["line"] == line)
            self.assertEqual((cr.vs_hash(row), fr.kind_of(row, table)), ("ECD66A10092E6562", "still"))
            g = fr.row_geometry(row, CAPTURE.parent, table=table)
            self.assertTrue(fr.judged(row, g, "still"))
            depth, ident = fr.raster([(g.clip, g.tris, 1)])
            rel = fr.relations(f.depth, depth, ident, [1])
            self.assertGreater(rel["1 - z / w"].pixels, 100)
            self.assertGreater(rel["1 - z / w"].fixed.share, share, line)
            self.assertEqual(rel["z / w"].fixed.within, 0)
            self.assertEqual(fr.best_relation(rel), "1 - z / w")
            shifted = fr.raster([(g.clip + np.array([0.0, 0.0, 0.0, 0.01]) * g.clip[:, 3:4], g.tris, 1)])    # w 1% larger
            self.assertLess(fr.relations(f.depth, shifted[0], shifted[1], [1])["1 - z / w"].fixed.share, 0.2)


if __name__ == "__main__":
    unittest.main()
