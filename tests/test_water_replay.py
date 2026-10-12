"""tools/xdk_sigmatch/water_replay.py (the water sub-project, plan Task 6): the offline replays of the lake's surface
on a made capture (tests/water_made.py): the cover, criterion 4's measure with its two controls, the level a fetch
takes, the mirrored sky's horizon, the sets of the edge and the glint."""
import math
import copy
import json
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import light_formulas as lf  # noqa: E402
import water_made  # noqa: E402
import water_replay as wr  # noqa: E402

# A quad of 40 x 20 pixels about the picture's centre under water_made.M (x and y are the world's / 100).
QUAD = ((-20.0 / 5.6, -10.0 / 3.6), (20.0 / 5.6, 10.0 / 3.6))
MASK = (0, 0, 255, 255)                                  # the left half water, the right half land
UPPER = bytes([51, 102, 204, 255] * 8 + [0, 0, 0, 255] * 8)        # a 4 x 4 picture: upper half (0.2, 0.4, 0.8), lower half 0


def made(folder, **kw):
    kw.setdefault("quad", QUAD)
    kw.setdefault("mask", MASK)
    kw.setdefault("picture", UPPER)
    return water_made.capture(folder, **kw)


class FramePictures(unittest.TestCase):
    def capture(self, folder):
        path = made(folder, picture=bytes([51, 102, 204, 255] * 16), pixel=NO_SUN)
        rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        picture = next(r for r in rows if r.get("file") == "tex/picture.bin")
        picture["frame"] = 1
        normal = next(r for r in rows if r.get("file") == "tex/normal.bin")
        normal["frame"] = 1
        second = copy.deepcopy(picture)
        second.update(frame=2, file="tex/picture2.bin")
        data = bytearray(Path(folder, picture["file"]).read_bytes())
        for y in range(4):
            data[y * 128:y * 128 + 16] = bytes([204, 102, 51, 255] * 4)
        Path(folder, second["file"]).write_bytes(data)
        rows.append(second)
        for row in list(rows):
            if row.get("kind") in ("draw", "frame") and row.get("frame") == 1:
                duplicate = copy.deepcopy(row)
                duplicate["frame"] = 2
                rows.append(duplicate)
        path.write_text("".join(water_made._json(r) + "\n" for r in rows), encoding="utf-8", newline="\n")
        return path, tuple(int(v, 16) for v in picture["fc"])

    def test_each_scene_replay_samples_its_own_frame_picture(self):
        with tempfile.TemporaryDirectory() as folder:
            path, fc = self.capture(folder)
            first = wr.replay(path, 1, place=(0.0, 0.0))
            second = wr.replay(path, 2, place=(0.0, 0.0))
        self.assertGreater(int(first.kept.sum()), 0)
        self.assertTrue(np.array_equal(first.kept, second.kept))
        self.assertGreater(float(np.max(np.abs(first.colour[first.kept] - second.colour[second.kept]))), 0.1)

    def test_rows_check_samples_each_rows_frame_picture(self):
        seen = {}
        original = wr.wc.RowSetup

        def observe(capture, textures, row, text):
            setup = original(capture, textures, row, text)
            seen[row["frame"]] = setup.mirror(0.5, 0.5)
            return setup

        with tempfile.TemporaryDirectory() as folder:
            path, fc = self.capture(folder)
            with mock.patch.object(wr.wc, "RowSetup", side_effect=observe):
                result = wr.wc.check_rows(path, positions=2, mutations=False)
        self.assertEqual((result.rows, result.unread, result.failed), (2, 0, 0))
        self.assertEqual(set(seen), {1, 2})
        self.assertEqual(seen[1], [51 / 255, 102 / 255, 204 / 255])
        self.assertEqual(seen[2], [204 / 255, 102 / 255, 51 / 255])

    def test_frame_selection_preserves_global_and_legacy_first_rows(self):
        with tempfile.TemporaryDirectory() as folder:
            path, fc = self.capture(folder)
            legacy = wr.wc.read_capture(path)
            first = wr.wc.read_capture(path, 1)
            second = wr.wc.read_capture(path, 2)
            self.assertEqual(legacy["textures"][fc]["file"], "tex/picture.bin")
            self.assertEqual(first["textures"][fc]["file"], "tex/picture.bin")
            self.assertEqual(second["textures"][fc]["file"], "tex/picture2.bin")
            normal = wr.wc._fc(first["water"][0], 2)
            self.assertEqual(second["textures"][normal]["file"], "tex/normal.bin")
            mask = wr.wc._fc(first["water"][0], 1)
            self.assertEqual(second["textures"][mask]["file"], "tex/mask.bin")
            self.assertEqual(wr.wc.read_capture(path)["textures"][fc]["file"], "tex/picture.bin")


class Cover(unittest.TestCase):
    def test_the_left_half_is_kept_and_none_of_the_right(self):
        with tempfile.TemporaryDirectory() as folder:
            r = wr.replay(made(folder), 1, place=(0.0, 0.0))
        ys, xs = np.nonzero(r.kept)
        self.assertEqual(len(ys), 20 * 20)
        self.assertEqual((int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max())), (540, 559, 350, 369))
        self.assertTrue((r.alpha[r.kept] > wr.reference_of(0x3D70F0F2)).all())
        self.assertEqual(int(r.kept[:, 560:].sum()), 0)
        judged = r.kept & (r.alpha >= wr.JUDGED_ALPHA)
        self.assertEqual(int(judged.sum()), 16 * 20)                 # the mask is 0 up to u = 0.375: sixteen columns
        self.assertTrue((r.base[r.kept] == 0x13401000).all())
        self.assertTrue((r.distance[r.kept] > 170.0).all())
        self.assertEqual(r.rows, 1)
        self.assertEqual(r.problems, [])

    def test_the_depth_copy_hides_what_lies_behind_it(self):
        with tempfile.TemporaryDirectory() as folder:
            path = made(folder)
            near = np.zeros((720, 1120))
            near[:, :550] = 0.9                                      # stored 1 - z / w: the quad stands at 0.5
            r = wr.replay(path, 1, place=(0.0, 0.0), depth=near)
        ys, xs = np.nonzero(r.kept)
        self.assertEqual((int(xs.min()), int(xs.max())), (550, 559))
        self.assertEqual(r.hidden, 10 * 20)


NO_SUN = {142: (0.1, 0.05, 0.99, water_made.GARBAGE)}       # c142 points FROM the sun: a sun below the level, no glint


class Criterion4(unittest.TestCase):
    def run_variants(self, pixel=NO_SUN):
        """Without the made sun: its glint is the same in every variant (it does not read the picture) and, where it is
        most of a pixel's variation, carries the correlation of an upside-down picture above the bound: 0.74 on this
        made frame with the sun, -0.999 without (test_the_glint_can_carry_the_correlation holds both)."""
        with tempfile.TemporaryDirectory() as folder:
            path = made(folder, pixel=pixel)
            own = wr.replay(path, 1, place=(0.0, 0.0))
            flip = wr.replay(path, 1, place=(0.0, 0.0), flip=True)
            swap = wr.replay(path, 1, place=(0.0, 0.0), swap_rb=True)
        return own, flip, swap

    def test_the_replay_against_itself_passes_and_each_control_misses_its_bound(self):
        own, flip, swap = self.run_variants()
        game = own.colour.copy()
        m = wr.measure(own, game, judged_min=100)[None]
        self.assertEqual(m.judged, 320)
        self.assertEqual(m.verdict, "met")
        for ratio in m.ratio:
            self.assertAlmostEqual(ratio, 1.0, places=9)
        self.assertAlmostEqual(m.correlation, 1.0, places=6)
        f = wr.measure(flip, game, judged_min=100)[None]
        self.assertLess(f.correlation, wr.CORRELATION_MIN)           # the picture upside down: the luminance runs the other way
        self.assertEqual(f.verdict, "not met")
        self.assertIn("correlation", f.missed)
        s = wr.measure(swap, game, judged_min=100)[None]
        self.assertTrue(s.ratio[0] > wr.BAND[1] and s.ratio[2] < wr.BAND[0])       # red and blue exchanged
        self.assertEqual(s.verdict, "not met")
        self.assertIn("red", s.missed)
        self.assertIn("blue", s.missed)

    def test_the_glint_can_carry_the_correlation_and_the_paired_measure_still_tells(self):
        """Review focus 1 (a measure that cannot tell): with the made sun the upside-down picture keeps a correlation
        above the bound, so the bounds alone would pass it. The paired measure asks only the pixels at which two
        variants differ, and which of the two the game is nearer to."""
        own, flip, swap = self.run_variants(pixel=None)
        game = own.colour.copy()
        f = wr.measure(flip, game, judged_min=100)[None]
        self.assertGreater(f.correlation, wr.CORRELATION_MIN)
        self.assertEqual(f.verdict, "met")                           # the bounds do not tell here
        p = wr.prefer(own, flip, game)
        self.assertGreater(p.differ, 100)
        self.assertEqual(p.first, p.differ)                          # every pixel at which they differ is nearer the first
        self.assertEqual(p.second, 0)
        self.assertEqual(wr.prefer_verdict(p, least=100), "the first")
        self.assertEqual(wr.prefer_verdict(p), "cannot tell")          # the made quad has fewer than PREFER_MIN such pixels
        back = wr.prefer(flip, own, game)
        self.assertEqual((back.first, back.second), (0, p.differ))
        self.assertEqual(wr.prefer_verdict(back, least=100), "the second")
        same = wr.prefer(own, own, game)
        self.assertEqual(same.differ, 0)
        self.assertEqual(wr.prefer_verdict(same), "cannot tell")     # no pixel at which they differ
        # a game picture halfway between the two tells nothing
        half = wr.prefer(own, flip, 0.5 * (own.colour + flip.colour))
        self.assertEqual(wr.prefer_verdict(wr.Prefer(1000, 520, 480, half.gap)), "cannot tell")
        self.assertEqual(wr.prefer_verdict(wr.Prefer(1000, 800, 200, half.gap)), "the first")
        self.assertEqual(wr.prefer_verdict(wr.Prefer(100, 100, 0, half.gap)), "cannot tell")   # too few pixels differ

    def test_a_judged_set_below_5000_pixels_is_not_decidable_never_met(self):
        own, _, _ = self.run_variants()
        self.assertEqual(wr.JUDGED_MIN, 5000)
        m = wr.measure(own, own.colour.copy())[None]
        self.assertEqual(m.judged, 320)
        self.assertEqual(m.verdict, "not decidable")

    def test_the_rule_of_the_four_variants(self):
        """The verdict's rule, as the plan states it before any number: the variant that meets both bounds on the
        deciding base in every capture while each of the other three misses one in at least one of them."""
        met, miss = "met", "not met"
        names = ("as the constant reads it", "flip", "swap-rb", "flip and swap-rb")
        one = wr.decide({names[0]: [met, met], names[1]: [met, miss], names[2]: [miss, miss], names[3]: [miss, met]})
        self.assertEqual(one, (names[0], "decided"))
        two = wr.decide({names[0]: [met, met], names[1]: [met, met], names[2]: [miss, miss], names[3]: [miss, miss]})
        self.assertEqual(two[0], None)
        self.assertIn("does not discriminate", two[1])
        self.assertIn(names[1], two[1])
        none = wr.decide({name: [miss, met] for name in names})
        self.assertEqual(none[0], None)
        self.assertIn("does not close", none[1])
        open_ = wr.decide({names[0]: [met, "not decidable"], names[1]: [miss, miss], names[2]: [miss, miss], names[3]: [miss, miss]})
        self.assertEqual(open_[0], None)
        self.assertIn("not decidable", open_[1])

    def test_the_command_exits_1_without_the_game_s_scene(self):
        with tempfile.TemporaryDirectory() as folder:
            self.assertEqual(wr.main(["scene", str(made(folder)), "1", "--place", "0", "0"], say=lambda *a: None), 1)


class Level(unittest.TestCase):
    FETCH = {"width": 256, "height": 256, "lod_bias": 0.0, "mip_min": 0, "mip_max": 8, "levels": 9}

    def test_one_texel_a_pixel_is_level_0_and_four_texels_level_2(self):
        self.assertEqual(wr.level(1.0 / 256.0, 0.0, 0.0, 1.0 / 256.0, self.FETCH), 0.0)
        self.assertAlmostEqual(wr.level(4.0 / 256.0, 0.0, 0.0, 1.0 / 256.0, self.FETCH), 2.0)
        self.assertAlmostEqual(wr.level(0.0, 1.0 / 256.0, 0.0, 4.0 / 256.0, self.FETCH), 2.0)      # the larger of the two screen axes
        self.assertAlmostEqual(wr.level(3.0 / 256.0, 4.0 / 256.0, 0.0, 0.0, self.FETCH), math.log2(5.0))   # an axis's length

    def test_magnified_is_the_base_and_the_range_and_the_bias_hold(self):
        self.assertEqual(wr.level(0.1 / 256.0, 0.0, 0.0, 0.1 / 256.0, self.FETCH), 0.0)
        self.assertEqual(wr.level(1.0, 1.0, 1.0, 1.0, self.FETCH), 8.0)
        self.assertEqual(wr.level(1.0, 1.0, 1.0, 1.0, dict(self.FETCH, levels=1)), 0.0)
        self.assertAlmostEqual(wr.level(4.0 / 256.0, 0.0, 0.0, 0.0, dict(self.FETCH, lod_bias=-1.0)), 1.0)
        self.assertEqual(wr.level(math.nan, 0.0, 0.0, 0.0, self.FETCH), 0.0)

    def test_a_level_between_two_is_their_blend(self):
        base = np.zeros((4, 4, 2))
        base[..., 0] = 1.0
        levels = [base, np.full((2, 2, 2), 0.5), np.full((1, 1, 2), 0.25)]
        self.assertEqual(wr.sample_levels(levels, 0.3, 0.7, 0.0)[0], 1.0)
        self.assertAlmostEqual(wr.sample_levels(levels, 0.3, 0.7, 0.5)[0], 0.75)
        self.assertAlmostEqual(wr.sample_levels(levels, 0.3, 0.7, 1.5)[0], 0.375)
        self.assertAlmostEqual(wr.sample_levels(levels, 0.3, 0.7, 9.0)[0], 0.25)


def camera(pitch_down, fx=0.9774, fy=1.7376, eye=(82.0, 157.0, 50.0)):
    """A view-projection looking along +x, `pitch_down` radians below the horizon (rows 0, 1 and 3 are what a ray
    needs; row 2 is the captured form)."""
    c, s = math.cos(pitch_down), math.sin(pitch_down)
    forward, right, up = (c, 0.0, -s), (0.0, -1.0, 0.0), (s, 0.0, c)

    def row(axis, scale):
        return [scale * axis[0], scale * axis[1], scale * axis[2], -scale * sum(a * e for a, e in zip(axis, eye))]
    w = row(forward, 1.0)
    return [row(right, fx), row(up, fy), [1.00002 * x for x in w[:3]] + [1.00002 * w[3] - 0.1], w]


class Sky(unittest.TestCase):
    BLOCK = {"c31x": 150.0, "c46x": 100.0, "c47x": 0.0, "c64": (0.0003, 0.0004, 0.0011), "c65": (0.0053, 0.006, 0.0099),
             "c66": (-0.83, 0.09, 0.55), "c67y": 1900.0}

    def test_the_first_row_with_content_is_the_horizon_s(self):
        pitch = math.radians(10.0)
        view = wr.view_rows(camera(pitch), (2.0 / 3.0, 2.0 / 3.0), 20.0)
        picture = wr.sky_picture(view, self.BLOCK, lambda a: (0.3, 0.4, 0.5))
        self.assertEqual(picture.shape, (256, 256, 3))
        self.assertEqual(picture.dtype, np.uint8)
        # by hand: the main camera's horizon stands tan(pitch) * fy above the centre; the view's field is 1.5 times
        # as wide, and the mirror leaves the picture's place of a direction where the main camera sees its mirror
        want = (1.0 - math.tan(pitch) * 1.7376 * 2.0 / 3.0) / 2.0 * 256.0
        self.assertAlmostEqual(wr.horizon_row(view), want, places=6)
        content = np.nonzero(picture.reshape(256, -1).max(axis=1))[0]
        self.assertEqual(int(content.min()), math.ceil(want - 0.5))            # the first texel centre below the line
        self.assertEqual(int(content.max()), 255)
        self.assertEqual(int(picture[:int(content.min())].max()), 0)           # the half below the water plane stays 0

    def test_two_planes_have_one_sky(self):
        """The ray needs rows 0, 1 and 3, which do not hold the plane's height: two bodies' pictures are equal while
        the sky is all they hold (spec Design 7)."""
        base = camera(math.radians(10.0))
        a = wr.sky_picture(wr.view_rows(base, (2.0 / 3.0, 2.0 / 3.0), 20.1024), self.BLOCK, lambda a: (0.3, 0.4, 0.5))
        b = wr.sky_picture(wr.view_rows(base, (2.0 / 3.0, 2.0 / 3.0), 40.0), self.BLOCK, lambda a: (0.3, 0.4, 0.5))
        self.assertTrue((a == b).all())

    def test_the_sky_texels_are_far_from_anything_darker(self):
        picture = np.zeros((64, 64, 3))
        picture[20:] = (0.2, 0.3, 0.5)
        picture[40:44, 30:34] = (0.05, 0.05, 0.05)                    # a hill
        sky = wr.sky_texels(picture, 19.5)
        self.assertFalse(sky[:23].any())                              # the rows above the horizon and three below it
        self.assertTrue(sky[23, 0])
        self.assertFalse(sky[40:44, 30:34].any())
        self.assertFalse(sky[37, 30] or sky[41, 27] or sky[46, 33])   # within three texels of the hill
        self.assertTrue(sky[36, 30] and sky[41, 26] and sky[47, 33])


class Sets(unittest.TestCase):
    def test_the_edge_and_the_glint_of_a_made_frame(self):
        with tempfile.TemporaryDirectory() as folder:
            r = wr.replay(made(folder), 1, place=(0.0, 0.0))
        e = wr.edge_sets(r)
        self.assertEqual(e["near"], 0)                                # nothing within 20 units: the eye stands 180 above
        self.assertAlmostEqual(e["nearest"], 180.0, delta=1.0)
        self.assertEqual(e["fading"], 400 - 320)                      # kept with an alpha below 0.99
        g = wr.glint_set(r)
        self.assertEqual(g["kept"], 400)
        self.assertGreaterEqual(g["above"], 0)
        with tempfile.TemporaryDirectory() as folder:
            dark = wr.replay(made(folder), 1, place=(0.0, 0.0), glint=False)
        self.assertEqual(wr.glint_set(dark)["above"], 0)
        self.assertEqual(float(dark.glint.max()), 0.0)

    def test_the_lookup_lands_about_the_picture_s_centre(self):
        with tempfile.TemporaryDirectory() as folder:
            r = wr.replay(made(folder), 1, place=(0.0, 0.0))
        at = r.lookup[r.kept]
        self.assertTrue((np.abs(at - 0.5) < 0.2).all())               # the screen place scaled about the centre, plus the normal's sway


def _quad(x0, y0, x1, y1, zw, z=None):
    """A quad in clip space with w = 1 (x and y in -1..1, y up), its two triangles; with `z`, the world place of
    every vertex is (x, y, z)."""
    clip = [[x, y, zw, 1.0] + ([x, y, z] if z is not None else []) for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    return np.array(clip), np.array([[0, 1, 2], [0, 2, 3]])


class ViewShare(unittest.TestCase):
    """Plan Task 15: which draw a reflection view has on top at the game's texels that are neither sky nor black."""
    SIZE = (16, 16)

    def test_a_quad_wholly_behind_another_has_no_share(self):
        back = _quad(-0.5, -0.5, 0.5, 0.5, 0.6) + ("export",)
        front = _quad(-0.5, -0.5, 0.5, 0.5, 0.2) + ("terrain",)
        for order in ((back, front), (front, back)):
            top = wr.view_top(order, self.SIZE)
            game = top.kind != 0
            self.assertEqual(int(game.sum()), 64)
            shares = wr.view_shares(game, top)
            self.assertEqual((shares["export"], shares["terrain"], shares["nothing"]), (0.0, 1.0, 0.0))

    def test_beside_it_its_share_is_one(self):
        left = _quad(-0.75, -0.5, -0.25, 0.5, 0.6) + ("export",)
        right = _quad(0.25, -0.5, 0.75, 0.5, 0.2) + ("terrain",)
        top = wr.view_top((left, right), self.SIZE)
        game = np.zeros((16, 16), dtype=bool)
        game[4:12, 2:6] = True                                      # the left quad's texels
        self.assertEqual(wr.view_shares(game, top)["export"], 1.0)
        game[4:12, 6:8] = True                                      # and two columns nothing covers
        shares = wr.view_shares(game, top)
        self.assertAlmostEqual(shares["export"], 2.0 / 3.0)
        self.assertAlmostEqual(shares["nothing"], 1.0 / 3.0)
        self.assertEqual(wr.view_shares(np.zeros((16, 16), dtype=bool), top), {})

    def test_what_lies_below_the_plane_is_not_drawn(self):
        under = _quad(-0.5, -0.5, 0.5, 0.5, 0.2, z=1.0) + ("terrain",)
        over = _quad(-0.5, -0.5, 0.5, 0.5, 0.6, z=3.0) + ("object",)
        top = wr.view_top((under, over), self.SIZE, plane=2.0)
        self.assertEqual(set(np.unique(top.kind)), {0, wr.VIEW_KINDS.index("object")})
        self.assertTrue((top.world[top.kind != 0][:, 2] == 3.0).all())
        self.assertEqual(int((wr.view_top((under, over), self.SIZE).kind == wr.VIEW_KINDS.index("terrain")).sum()), 64)

    def test_the_ground_shows_its_upper_side_or_nothing(self):
        points = np.array([[0.0, 0.0, 5.0], [1.0, 0.0, 5.0], [1.0, 1.0, 5.0], [0.0, 1.0, 5.0]])
        tris = np.array([[0, 1, 2], [0, 2, 3]])                     # counter-clockwise seen from above
        self.assertEqual(wr.facing(points, tris, [0.5, 0.5, 9.0]).tolist(), [True, True])
        self.assertEqual(wr.facing(points, tris, [0.5, 0.5, 1.0]).tolist(), [False, False])     # the mirrored eye under it
        self.assertEqual(wr.facing(points, tris, [40.0, 0.5, 5.0]).tolist(), [False, False])    # edge on: not drawn
        slope = points + np.array([[0.0, 0.0, 0.0], [0.0, 0.0, 3.0], [0.0, 0.0, 3.0], [0.0, 0.0, 0.0]])   # rising towards +x
        self.assertEqual(wr.facing(slope, tris, [-40.0, 0.5, 0.0]).tolist(), [True, True])      # a slope facing an eye below its foot
        self.assertEqual(wr.facing(slope, tris, [40.0, 0.5, 0.0]).tolist(), [False, False])

    def test_an_unknown_kind_is_refused(self):
        with self.assertRaises(ValueError):
            wr.view_top((_quad(-0.5, -0.5, 0.5, 0.5, 0.6) + ("cloud",),), self.SIZE)


class ViewPictures(unittest.TestCase):
    def test_the_blur_keeps_a_flat_picture_and_spreads_a_texel_over_the_hexagon(self):
        offsets = [(a / 16.0, b / 16.0) for a, b in lf.WATER_BLUR_TEXELS]
        flat = np.full((16, 16, 3), 0.25)
        self.assertTrue(np.allclose(wr.view_blur(flat, offsets, 1.0 / 7.0), 0.25))
        one = np.zeros((16, 16, 3))
        one[8, 8] = 7.0
        got = wr.view_blur(one, offsets, 1.0 / 7.0)
        self.assertAlmostEqual(float(got[..., 0].sum()), 7.0)           # nothing is lost inside the picture
        # a tap at an integral position lies on a texel corner: the centre tap gives a quarter to each of four texels
        self.assertAlmostEqual(float(got[8, 8, 0]), 0.25)
        self.assertAlmostEqual(float(got[9, 9, 0]), 0.25)
        self.assertAlmostEqual(float(got[8, 11, 0]), 0.25)              # the tap two texels to the LEFT of that pixel
        self.assertEqual(float(got[8, 3, 0]), 0.0)
        # the formula, pixel for pixel
        want = lf.water_blur7(lambda u, v: wr.wc.bilinear(one, u, v, (2, 2)), 9.0 / 16.0, 9.0 / 16.0, offsets, 1.0 / 7.0)
        self.assertTrue(np.allclose(got[9, 9], want[:3]))

    def test_near_share_forgives_one_texel_and_not_three(self):
        a = np.zeros((32, 32), dtype=bool)
        a[10:20, 10:20] = True
        for shift, want in ((1, 1.0), (3, 0.8)):
            b = np.roll(a, shift, axis=1)
            self.assertAlmostEqual(wr.near_share(a, b), want)
            self.assertAlmostEqual(wr.near_share(b, a), want)
        self.assertEqual(wr.near_share(a, np.zeros_like(a)), 0.0)
        self.assertTrue(math.isnan(wr.near_share(np.zeros_like(a), a)))

    def test_a_given_picture_takes_the_dumps_place_in_the_replay(self):
        with tempfile.TemporaryDirectory() as folder:
            path = made(folder)
            own = wr.replay(path, 1, place=(0.0, 0.0))
            base = int(np.unique(own.base[own.kept])[0])
            grey = wr.replay(path, 1, place=(0.0, 0.0), pictures={base: np.full((4, 4, 3), 0.5)})
            dark = wr.replay(path, 1, place=(0.0, 0.0), pictures={base: np.full((4, 4, 3), 0.25)})
            none = wr.replay(path, 1, place=(0.0, 0.0), pictures={base + 0x1000: np.full((4, 4, 3), 0.5)})
        self.assertTrue(np.array_equal(none.colour, own.colour))        # another base's picture changes nothing
        self.assertTrue(np.array_equal(grey.kept, own.kept))
        self.assertFalse(np.allclose(grey.colour[own.kept], own.colour[own.kept]))
        self.assertTrue((grey.colour[own.kept] >= dark.colour[own.kept]).all())      # the reflection enters with a positive weight
        self.assertGreater(float((grey.colour[own.kept] - dark.colour[own.kept]).max()), 0.0)


class StoredScene(unittest.TestCase):
    """The measures of the stored native scene's water (plan Tasks 11 and 12; ledger ruling 44), on made arrays."""

    def frame(self):
        judged = np.zeros((40, 60), dtype=bool)
        judged[10:30, 10:50] = True                                  # a lake of 40 x 20 pixels
        rng = np.random.default_rng(3)
        replayed = np.zeros((40, 60, 3))
        replayed[judged] = 0.2 + 0.1 * rng.random((int(judged.sum()), 3))
        return judged, replayed

    def test_the_main_by_value_holds_the_interior_and_sees_a_wrong_program(self):
        judged, replayed = self.frame()
        self.assertEqual((wr.MAIN_BAND, wr.MAIN_SPAN, wr.MAIN_EDGE), ((0.99, 1.01), (0.95, 1.08), 3))
        m = wr.main_measure(replayed.copy(), replayed, judged)
        self.assertEqual((m["pixels"], m["verdict"], m["missed"]), (34 * 14, "met", []))     # more than 3 pixels inside: 34 x 14
        self.assertTrue(all(abs(v - 1.0) < 1e-12 for v in m["ratio"]) and m["median"] == 1.0)
        # the edge band is not judged: garbage there changes nothing
        wrong = replayed.copy()
        wrong[10:13, 10:50] = 5.0
        wrong[5, 5] = 9.0
        self.assertEqual(wr.main_measure(wrong, replayed, judged)["verdict"], "met")
        # a main that is 2% off in one channel (a wrong scale, a picture read elsewhere) misses the ratio of the means
        off = replayed.copy()
        off[..., 2] *= 1.02
        m = wr.main_measure(off, replayed, judged)
        self.assertEqual(m["verdict"], "not met as written")
        self.assertIn("the ratio of the means", m["missed"])
        # one whose mean is right and whose pixels are scattered (a lookup with another distortion) misses the span
        scattered = replayed.copy()
        k = np.arange(judged.sum()) % 2 == 0
        vals = scattered[judged]
        vals[k] *= 1.12
        vals[~k] *= 0.88
        scattered[judged] = vals
        m = wr.main_measure(scattered, replayed, judged)
        self.assertEqual(m["verdict"], "not met as written")
        self.assertIn("the 5th to 95th percentile span", m["missed"])
        # a lake without an interior, and no judged pixel at all: not decidable, never met
        thin = np.zeros((40, 60), dtype=bool)
        thin[10:14, 10:50] = True
        self.assertEqual(wr.main_measure(replayed, replayed, thin)["verdict"], "not decidable")
        self.assertEqual(wr.main_measure(replayed, replayed, np.zeros((40, 60), dtype=bool))["pixels"], 0)

    def test_the_cover_s_two_shares(self):
        judged, _ = self.frame()
        self.assertEqual(wr.cover_measure(judged, judged)["verdict"], "met")
        shifted = np.roll(judged, 1, axis=1)                          # one pixel aside: within reach
        m = wr.cover_measure(shifted, judged)
        self.assertEqual((m["native"], m["replay"], m["verdict"]), (0.0, 0.0, "met"))
        hole = judged.copy()
        hole[15:25, 20:40] = False                                    # a hole to the bed in the middle of the lake
        m = wr.cover_measure(hole, judged)
        self.assertEqual(m["native"], 0.0)
        self.assertAlmostEqual(m["replay"], (8 * 18) / 800.0)         # the hole's pixels farther than one pixel from its rim
        self.assertEqual(m["verdict"], "not met as written")
        spill = judged.copy()
        spill[2:8, 2:20] = True                                       # native water where the game has none
        m = wr.cover_measure(spill, judged)
        self.assertGreater(m["native"], wr.COVER_FAR)
        self.assertEqual(m["verdict"], "not met as written")
        self.assertEqual(wr.cover_measure(np.zeros_like(judged), judged)["verdict"], "not decidable")
        self.assertEqual(wr.far_share(judged, np.zeros_like(judged)), 1.0)

    def test_criterion_8_s_figures_and_verdict(self):
        judged, replayed = self.frame()
        game = replayed * [0.4, 0.8, 1.6]                             # a blue lake: its hue is a hue
        m = wr.colour_measure(game * [1.05, 1.0, 0.95], game, judged)
        self.assertEqual(m["pixels"], 800)
        for got, want in zip(m["ratio"], (1.05, 1.0, 0.95)):
            self.assertAlmostEqual(got, want)
        self.assertLess(abs(m["hue"]), wr.HUE_BOUND)
        self.assertEqual(wr.colour_verdict(m, least=500), "met")
        self.assertEqual(wr.colour_verdict(m), "not decidable")       # 800 pixels are below the criterion's 5,000
        red = wr.colour_measure(game * [1.35, 1.0, 0.85], game, judged)       # the placeholder's gap of before
        self.assertEqual(wr.colour_verdict(red, least=500), "not met as written")
        # every channel inside the band and the hue turned by 20 degrees: not met by the hue alone
        flat = np.zeros((40, 60, 3)) + [0.30, 0.30, 0.20]
        turned = wr.colour_measure(np.zeros((40, 60, 3)) + [0.32, 0.28, 0.20], flat, judged)
        self.assertTrue(all(wr.COLOUR_BAND[0] <= v <= wr.COLOUR_BAND[1] for v in turned["ratio"]))
        self.assertAlmostEqual(turned["hue"], -20.0)
        self.assertEqual(wr.colour_verdict(turned, least=500), "not met as written")
        self.assertEqual(wr.colour_measure(game, game, np.zeros_like(judged))["pixels"], 0)

    def test_the_targets_as_pictures_and_their_equality(self):
        a = (np.arange(256 * 256 * 4) % 251).astype(np.uint8).reshape(256, 256, 4)
        b = a.copy()
        b[..., 3] = 7                                                 # the alpha is no part of what a draw samples
        water = {"targets": [(0x13401000, 20.1, a), (0x13501000, 40.0, b)], "pre_water": None, "water": None}
        pictures = wr.target_pictures(water)
        self.assertEqual(sorted(pictures), [0x13401000, 0x13501000])
        self.assertEqual(pictures[0x13401000].shape, (256, 256, 3))
        self.assertAlmostEqual(float(pictures[0x13401000][0, 1, 0]), float(a[0, 1, 0]) / 255.0)
        self.assertTrue(wr.targets_equal(water))
        b[100, 100, 1] ^= 1
        self.assertFalse(wr.targets_equal(water))
        self.assertFalse(wr.targets_equal({"targets": []}))


class TargetFormat(unittest.TestCase):
    """The stored plane's number format (ledger ruling 46, the review of Tasks 11 and 12, I3): the linear target is
    R11G11B10 float, 6 bits of mantissa in red and green and 5 in blue under a 5-bit exponent, no sign."""

    def test_the_format_is_the_pass_s(self):
        self.assertEqual((wr.TARGET_MANTISSA, wr.ROUNDINGS, wr.PASS_MAX), ((6, 6, 5), ("nearest", "toward zero"), 31.875))
        dump = (ROOT / "src" / "native" / "render" / "scene_dump.h").read_text(encoding="utf-8")
        self.assertIn("  out[0] = DecodeUnsignedFloat(word & 0x7FFu, 6);\n  out[1] = DecodeUnsignedFloat((word >> 11) & 0x7FFu, 6);\n"
                      "  out[2] = DecodeUnsignedFloat(word >> 22, 5);\n", dump)
        self.assertIn("  if (exponent == 0) return std::ldexp(fraction, -14);\n", dump)
        self.assertIn("  return std::ldexp(1.0f + fraction, int(exponent) - 15);\n", dump)
        pass_text = (ROOT / "src" / "native" / "render" / "clay_pass.cpp").read_text(encoding="utf-8")
        self.assertIn("constexpr nrhi::Format kLinearFormat = nrhi::Format::kR11G11B10_FLOAT;", pass_text)
        shaders = (ROOT / "src" / "native" / "fable2_native_shaders.h").read_text(encoding="utf-8")
        self.assertIn("  v = (v > 0.0) ? min(v, 31.875) : float3(0.0, 0.0, 0.0);\n  return float4(v, alpha);\n", shaders)   # the surface's own clamp
        # the decode's twin, by hand
        self.assertEqual(wr.unsigned_float(15 << 6, 6), 1.0)
        self.assertEqual(wr.unsigned_float((15 << 6) | 1, 6), 1.0 + 1.0 / 64.0)
        self.assertEqual(wr.unsigned_float((16 << 5) | 16, 5), 3.0)
        self.assertEqual(wr.unsigned_float(1, 6), 2.0 ** -14 / 64.0)                 # a denormal
        self.assertEqual(wr.unsigned_float(31 << 6, 6), math.inf)
        self.assertTrue(math.isnan(wr.unsigned_float((31 << 5) | 1, 5)))
        self.assertEqual(wr.r11g11b10((15 << 6) | ((14 << 6) << 11) | ((16 << 5) << 22)), (1.0, 0.5, 2.0))

    def test_every_value_the_format_holds_is_kept_and_what_lies_between_goes_to_its_neighbours(self):
        for bits in (6, 5):
            held = np.array([wr.unsigned_float(k, bits) for k in range(31 << bits)])
            self.assertTrue((np.diff(held) > 0).all())
            for rounding in wr.ROUNDINGS:
                self.assertTrue(np.array_equal(wr.quantise_channel(held, bits, rounding), held), (bits, rounding))
            a, b = held[:-1], held[1:]
            quarter, three = a + 0.25 * (b - a), a + 0.75 * (b - a)
            self.assertTrue(np.array_equal(wr.quantise_channel(quarter, bits, "toward zero"), a))
            self.assertTrue(np.array_equal(wr.quantise_channel(three, bits, "toward zero"), a))
            self.assertTrue(np.array_equal(wr.quantise_channel(quarter, bits, "nearest"), a))
            self.assertTrue(np.array_equal(wr.quantise_channel(three, bits, "nearest"), b))
            # a half goes to the even neighbour
            half = wr.quantise_channel(0.5 * (a + b), bits, "nearest")
            even = np.where(np.arange(len(a)) % 2 == 0, a, b)
            self.assertTrue(np.array_equal(half, even))
            # what the format does not hold: nothing below 0, no number; the largest value above it
            top = held[-1]
            self.assertEqual(top, 2.0 ** 15 * (2.0 - 2.0 ** -bits))
            for rounding in wr.ROUNDINGS:
                got = wr.quantise_channel(np.array([-1.0, 0.0, math.nan, 1e9, math.inf, top * 1.001]), bits, rounding)
                self.assertEqual(got.tolist(), [0.0, 0.0, 0.0, top, top, top])

    def test_blue_has_half_the_steps_of_red_and_green(self):
        got = wr.quantise(np.array([[1.02, 1.02, 1.02], [0.3, 0.3, 0.3]]), "toward zero")
        self.assertEqual(got[0].tolist(), [1.015625, 1.015625, 1.0])
        self.assertEqual(got[1].tolist(), [0.296875, 0.296875, 0.296875])
        got = wr.quantise(np.array([[1.02, 1.02, 1.02], [0.3, 0.3, 0.3]]), "nearest")
        self.assertEqual(got[0].tolist(), [1.015625, 1.015625, 1.03125])
        self.assertEqual(got[1].tolist(), [0.30078125, 0.30078125, 0.296875])
        with self.assertRaises(ValueError):
            wr.quantise(np.zeros((2, 3)), "up")


class Rounded(unittest.TestCase):
    """Ruling 44's bound on the replay brought to the stored plane's format, both roundings computed, the bound applied
    to the one that lies nearer the plane (ledger ruling 46, I3)."""

    def frame(self):
        judged = np.zeros((40, 60), dtype=bool)
        judged[10:30, 10:50] = True
        rng = np.random.default_rng(5)
        replayed = np.zeros((40, 60, 3))
        replayed[judged] = 0.26 + 0.11 * rng.random((int(judged.sum()), 3))     # one binade: steps of 1 / 256 (red, green), 1 / 128 (blue)
        return judged, replayed

    def test_a_plane_that_truncates_is_met_by_the_truncated_replay_and_missed_by_the_exact_one(self):
        judged, replayed = self.frame()
        plane = wr.quantise(replayed, "toward zero")
        m = wr.main_rounded(plane, replayed, judged)
        self.assertEqual(m["nearer"], "toward zero")
        self.assertEqual((m["toward zero"]["verdict"], m["toward zero"]["distance"]), ("met", 0.0))
        self.assertTrue(all(v == 1.0 for v in m["toward zero"]["ratio"]))
        self.assertGreater(m["nearest"]["distance"], 0.0)
        # the review's failure: the exact replay against a truncating target reads about 0.985 in blue against 0.99
        self.assertEqual(m["exact"]["verdict"], "not met as written")
        self.assertIn("the ratio of the means", m["exact"]["missed"])
        self.assertTrue(0.98 < m["exact"]["ratio"][2] < 0.99 and 0.99 < m["exact"]["ratio"][0] < 1.0)
        self.assertEqual(m["verdict"], "met")                           # the bound is the nearer rounding's

    def test_a_plane_that_rounds_to_nearest_names_that_rounding(self):
        judged, replayed = self.frame()
        plane = wr.quantise(replayed, "nearest")
        m = wr.main_rounded(plane, replayed, judged)
        self.assertEqual((m["nearer"], m["verdict"], m["nearest"]["distance"]), ("nearest", "met", 0.0))
        self.assertGreater(m["toward zero"]["distance"], 0.0)
        self.assertTrue(m["toward zero"]["ratio"][2] > 1.01)             # the truncated replay lies below such a plane
        self.assertEqual(m["toward zero"]["verdict"], "not met as written")

    def test_a_wrong_program_is_missed_under_either_rounding(self):
        judged, replayed = self.frame()
        for rounding in wr.ROUNDINGS:
            wrong = wr.quantise(replayed * [1.0, 1.0, 1.04], rounding)
            m = wr.main_rounded(wrong, replayed, judged)
            self.assertEqual(m["verdict"], "not met as written", rounding)
            self.assertIn("the ratio of the means", m[m["nearer"]]["missed"])
            self.assertEqual(m["nearest"]["verdict"], "not met as written")
            self.assertEqual(m["toward zero"]["verdict"], "not met as written")

    def test_no_interior_names_no_rounding(self):
        _, replayed = self.frame()
        m = wr.main_rounded(replayed, replayed, np.zeros((40, 60), dtype=bool))
        self.assertEqual((m["nearer"], m["verdict"]), (None, "not decidable"))

    def test_a_tie_is_said_as_one(self):
        judged, _ = self.frame()
        held = np.zeros((40, 60, 3)) + [0.25, 0.5, 1.0]                # values the format holds: both roundings give them
        m = wr.main_rounded(held, held, judged)
        self.assertEqual((m["nearer"], m["verdict"], m["tie"]), ("nearest", "met", True))


EXPOSURE = 2.22                                                       # render::kLitExposure: a frame without post
TARGET_BASE = 0x13401000


def made_target():
    y, x = np.mgrid[0:256, 0:256]
    return np.stack([60 + y // 2, 90 + x // 4, 200 - y // 3, np.full_like(x, 255)], axis=-1).astype(np.uint8)


def made_pair(folder, rounding="toward zero", change=None, flags=8 | 16 | 32, hide=True):
    """A made capture and the stored scene the pass would write of it: one target, the depth before the water step (far,
    with a bar nearer than the lake over its upper ten rows), and the scene after the step as the replay fed the
    same target gives it at the frame's exposure, brought to the target's format with `rounding`. `change(plane)`:
    makes one thing wrong. Returns (capture path, dump path, the replay)."""
    folder = Path(folder)
    path = made(folder)
    target = made_target()
    pre = np.full((720, 1120), 16777215, dtype=np.uint32)
    if hide:
        pre[350:360, :] = 16777215 // 4                               # z / w 0.25: nearer than the quad's 0.5
    r = wr.replay(path, 1, place=wr.PASS_PLACE, depth=wr.pass_depth(pre), pictures={TARGET_BASE: target[..., :3] / 255.0},
                  near=wr.no_near, out_scale=EXPOSURE)
    plane = wr.quantise(np.clip(r.colour, 0.0, wr.PASS_MAX), rounding).astype(np.float32)
    if hide:
        plane[350:360, 540:580] = 9.0                                 # what stands in front of the lake there
    if change:
        change(plane)
    dump = folder / "native_scene_made.f2ns"
    dump.write_bytes(water_made.scene_dump(
        1120, 720, flags, plane, pre, exposure=EXPOSURE, scene_frame=7, asked_frame=7,
        pre_water=pre if flags & 8 else None, water=plane if flags & 32 else None, targets=[(TARGET_BASE, 20.0, target)] if flags & 16 else ()))
    return path, dump, r


class MainOnMadeDumps(unittest.TestCase):
    """`main` on a made capture and a made stored scene (ledger ruling 46, the review of Tasks 11 and 12, I2): the
    depth test and the judged set are the stored scene's own depth before the water step, the pictures the stored
    targets, the scale the stored exposure. The made capture has no depth copy and no scene of the game."""

    def run_main(self, *args, **kw):
        said = []
        with tempfile.TemporaryDirectory() as folder:
            path, dump, r = made_pair(folder, *args, **kw)
            code = wr.main(["main", str(path), str(dump)], say=said.append)
        return code, said, r

    def test_it_needs_nothing_of_the_game_and_names_the_rounding(self):
        for rounding in wr.ROUNDINGS:
            code, said, r = self.run_main(rounding)
            text = "\n".join(said)
            self.assertEqual(code, 0, text)
            self.assertNotIn("NOT READ", text)
            self.assertNotIn("NOT COMPARED", text)
            self.assertIn("applied to the rounding that lies nearer the stored plane (%s): met" % rounding, said[-1])
            for name in wr.ROUNDINGS:
                self.assertTrue(any(("rounded %s:" % name) in line for line in said), name)
            self.assertTrue(any("not rounded (reported, not bound)" in line for line in said))
            # the judged set is the stored depth's: ten rows of the lake stand behind the bar
            self.assertEqual(int(r.kept.sum()), 20 * 10)
            self.assertEqual(r.hidden, 40 * 10)
            self.assertEqual(int((r.kept & (r.alpha >= wr.JUDGED_ALPHA)).sum()), 15 * 10)      # (the mask at the pixel centres: fifteen columns)
            self.assertTrue(any("judged pixels 150, of which 36 lie more than 3 pixels" in line for line in said), text)
            self.assertTrue(any("hidden by the stored depth 400" in line for line in said), text)

    def test_a_wrong_surface_is_not_met(self):
        def blue(plane):
            plane[..., 2] *= 1.04
        code, said, _ = self.run_main("toward zero", change=blue)
        self.assertEqual(code, 1)
        self.assertIn("not met as written", said[-1])
        self.assertIn("the ratio of the means", said[-1])

    def test_without_the_depth_before_the_step_nothing_is_compared(self):
        code, said, _ = self.run_main("toward zero", flags=16 | 32, hide=False)
        self.assertEqual(code, 1)
        self.assertTrue(any("NOT COMPARED" in line and "pre_water" in line for line in said), "\n".join(said))

    def test_the_replay_at_the_pass_s_scale_and_without_slot_14(self):
        with tempfile.TemporaryDirectory() as folder:
            path = made(folder)
            pictures = {TARGET_BASE: made_target()[..., :3] / 255.0}
            own = wr.replay(path, 1, place=(0.0, 0.0), pictures=pictures)
            scaled = wr.replay(path, 1, place=(0.0, 0.0), pictures=pictures, near=wr.no_near, out_scale=EXPOSURE)
            # (the made slot 14 is empty, so only the scale differs: c20.w is the last factor)
            c20w = water_made.PIXEL[20][3]
            self.assertTrue(np.allclose(scaled.colour[own.kept], own.colour[own.kept] * (EXPOSURE / c20w), rtol=1e-6, atol=0.0))
            self.assertTrue(np.array_equal(scaled.kept, own.kept))
            # with the pass's inputs the capture's own dumps of slots 13 and 14 are not read at all
            (Path(folder) / "tex" / "picture.bin").unlink()
            (Path(folder) / "tex" / "near.bin").unlink()
            wr.wc._captures.clear()
            again = wr.replay(path, 1, place=(0.0, 0.0), pictures=pictures, near=wr.no_near, out_scale=EXPOSURE)
            self.assertEqual(again.problems, [])
            self.assertTrue(np.array_equal(again.colour, scaled.colour))
            # and a row whose base has no target is not replayed, said
            none = wr.replay(path, 1, place=(0.0, 0.0), pictures={TARGET_BASE + 0x1000: pictures[TARGET_BASE]}, near=wr.no_near, out_scale=EXPOSURE)
            self.assertEqual(int(none.kept.sum()), 0)
            self.assertTrue(any("no target" in p for p in none.problems))
        self.assertEqual(wr.no_near(0.3, 0.7), (0.0, 0.0, 0.0))
        self.assertEqual(wr.PASS_PLACE, (0.5, 0.5))
        levels = np.array([[0, 16777215, 16777215 // 4]], dtype=np.uint32)
        self.assertEqual(wr.pass_depth(levels).tolist(), [[1.0, 0.0, 1.0 - (16777215 // 4) / 16777215.0]])


class TargetsOnTheSoftwareAdapter(unittest.TestCase):
    """`targets` holds the native targets against the game's own pictures: a real-GPU check. On the software adapter the
    read-back leaves the game's pictures empty (sub-project 12's ledger): the command says so and judges only what
    needs no game picture; it never reads "not met" for that (ledger ruling 46, I2)."""

    def run_targets(self, picture):
        said = []
        M = camera(math.radians(10.0))
        with tempfile.TemporaryDirectory() as folder:
            path = made(folder, matrix=M, picture=picture)
            M32 = np.array(water_made.bank(M, {})[:16]).reshape(4, 4)
            view = wr.view_rows(M32, water_made.PIXEL[31][:2], water_made.HEIGHT)
            sky = wr.sky_picture(view, Sky.BLOCK, lambda a: (0.3, 0.4, 0.5))
            target = np.concatenate([sky, np.full((256, 256, 1), 255, dtype=np.uint8)], axis=-1)
            dump = Path(folder) / "native_scene_made.f2ns"
            dump.write_bytes(water_made.scene_dump(4, 2, 16, np.zeros((2, 4, 3)), np.zeros((2, 4)), targets=[(TARGET_BASE, water_made.HEIGHT, target)]))
            code = wr.main(["targets", str(path), str(dump)], say=said.append)
        return code, said

    def test_empty_pictures_are_said_and_not_read_as_not_met(self):
        code, said = self.run_targets(bytes(64))
        text = "\n".join(said)
        self.assertEqual(code, 1)
        self.assertTrue(any("EMPTY" in line and "real-GPU" in line for line in said), text)
        self.assertTrue(said[-1].startswith("criterion 10 (13b): NOT DECIDED on this pair"), text)
        self.assertIn("what needs no picture of the game: met", said[-1])
        self.assertNotIn("not met", text.lower())

    def test_a_picture_with_content_is_compared(self):
        """(The made capture's picture is 4 x 4, which no target can be held to: said, and a verdict all the same.)"""
        code, said = self.run_targets(None)
        text = "\n".join(said)
        self.assertEqual(code, 1)
        self.assertNotIn("EMPTY", text)
        self.assertNotIn("NOT DECIDED", text)
        self.assertTrue(any("is 4 x 4, the target 256 x 256: NOT COMPARED" in line for line in said), text)
        self.assertEqual(said[-1], "criterion 10 (13b): not met as written")


class MadeDumpLayout(unittest.TestCase):
    def test_the_made_dump_writer_writes_the_pass_s_layout(self):
        """The made dumps of the tests above are written by tests/water_made.py, the real ones by render::PackSceneDump:
        both pack the same made planes (a 4 x 2 scene with every flag and two targets) and hold one number."""
        import foliage_trace as ft
        data = water_made.pinned_dump(wr.r11g11b10)
        self.assertEqual(len(data), 48 + 8 * 16 + 2 * 2 * 4 + 8 * 4 + 8 * 12 + 4 + 2 * (8 + 262144))
        self.assertNotEqual(water_made.DUMP_LAYOUT_PIN, 0)
        self.assertEqual(water_made.fnv1a64(data), water_made.DUMP_LAYOUT_PIN)
        text = (ROOT / "tests" / "native" / "test_scene_dump.cpp").read_text(encoding="utf-8")
        self.assertIn("0x%016Xull" % water_made.DUMP_LAYOUT_PIN, text)
        self.assertEqual(water_made.fnv1a64(b""), 0xCBF29CE484222325)
        self.assertEqual(water_made.fnv1a64(b"a"), 0xAF63DC4C8601EC8C)                      # the published test vector
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "pinned.f2ns"
            p.write_bytes(data)
            head, scene, depth, layers = ft.read_dump(p)
            water = ft.read_dump_water(p)
        self.assertEqual((head["version"], head["flags"], head["scene_frame"], head["asked_frame"]), (2, 63, 77, 76))
        self.assertEqual(scene[0, 0].tolist(), [float(np.float32(v)) for v in wr.r11g11b10(water_made.pinned_words(0)[0])])
        self.assertEqual(water["water"][1, 3].tolist(), [float(np.float32(v)) for v in wr.r11g11b10(water_made.pinned_words(8)[7])])
        self.assertEqual(int(water["pre_water"][0, 1]), (7654321 + 3) & 0xFFFFFF)
        self.assertEqual([b for b, _, _ in water["targets"]], [0x13401000, 0x13501000])
        self.assertEqual(int(layers[1][0, 1]), 4099 + 17 + 1)
        with self.assertRaises(ValueError):
            water_made.scene_dump(4, 2, 8, np.zeros((2, 4, 3)), np.zeros((2, 4)))           # a flag without its plane


if __name__ == "__main__":
    unittest.main()
