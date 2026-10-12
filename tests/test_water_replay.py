"""tools/xdk_sigmatch/water_replay.py (the water sub-project, plan Task 6): the offline replays of the lake's surface
on a made capture (tests/water_made.py): the cover, criterion 4's measure with its two controls, the level a fetch
takes, the mirrored sky's horizon, the sets of the edge and the glint."""
import math
import sys
import tempfile
import unittest
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


if __name__ == "__main__":
    unittest.main()
