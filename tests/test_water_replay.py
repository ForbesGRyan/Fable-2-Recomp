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


if __name__ == "__main__":
    unittest.main()
