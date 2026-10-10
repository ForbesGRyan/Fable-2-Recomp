"""The post chain's closed formulas (tools/xdk_sigmatch/light_formulas.py, the part "the post chain"), their
machine check against the microcode (post_check.py) and the numbers the C++ mirrors are pinned to
(post_pins.py). Hand cases first; the real dumps last, skipped where there are none."""
import contextlib
import io
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import post_check as pc  # noqa: E402
import post_pins  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"
NAN, INF = float("nan"), float("inf")

# The composite's registers of frame 1 of capture 092959 (research section 1), rounded.
REAL = dict(c31x=2.973, c46x=0.25, c47x=50.0, c77=(-0.0052, 1.1026), c78x=1.0 / 2.2)


PINS_DIGEST = "1c3c6df5dd5fb3c4e7605a755b7ce1379cdd9734c1b02d700c7a9d7b19c98db9"


def near(test, got, want, tol=1e-12):
    test.assertEqual(len(got), len(want))
    for g, w in zip(got, want):
        test.assertAlmostEqual(g, w, delta=tol * max(1.0, abs(w)))


def numbers(test, values, lo=0.0, hi=1.0):
    for v in values:
        test.assertTrue(math.isfinite(v), values)
        test.assertGreaterEqual(v, lo)
        test.assertLessEqual(v, hi)


def composite(scene, bloom=(0.0, 0.0, 0.0), table=None, **over):
    regs = dict(REAL, **over)
    return lf.post_composite(scene, bloom, table if table is not None else [0.25] * 512, regs["c31x"], regs["c46x"],
                             regs["c47x"], regs["c77"], regs["c78x"])


# --- small pieces -------------------------------------------------------------------------------------------

class Luminance(unittest.TestCase):
    def test_the_weights_are_the_shaders_literal(self):
        self.assertEqual(lf.POST_LUM, (0.2125, 0.7154, 0.0721))
        self.assertAlmostEqual(sum(lf.POST_LUM), 1.0, delta=1e-12)
        self.assertAlmostEqual(lf.post_lum((1.0, 2.0, 4.0)), 0.2125 + 2 * 0.7154 + 4 * 0.0721, delta=1e-12)

    def test_the_scene_clamp(self):
        self.assertEqual(lf.POST_SCENE_MAX, 31.875)
        self.assertEqual([lf.post_scene(v) for v in (0.5, 31.875, 40.0, INF, 0.0, -0.0, -3.0, -INF)],
                         [0.5, 31.875, 31.875, 31.875, 0.0, 0.0, 0.0, 0.0])
        self.assertEqual(lf.post_scene(NAN), 0.0)


class TableTexel(unittest.TestCase):
    """The composite's fetch is `tfetch1D ..., MagFilter=point, MinFilter=point, OffsetX=0.5`: by the
    interpreter's rule for a point fetch (ucode_eval.point_texel) the texel is floor(u * 512 + 0.5)."""

    def test_the_pins_of_the_plan(self):
        # u = k / 512 -+ 1e-6 lies well inside texel k under this convention: both sides read k.
        for k in (0, 1, 2, 12, 128, 511):
            self.assertEqual(lf.post_table_texel(k / 512 - 1e-6), k, k)
            self.assertEqual(lf.post_table_texel(k / 512), k, k)
            self.assertEqual(lf.post_table_texel(k / 512 + 1e-6), k, k)
        for u in (512 / 512 - 1e-6, 1.0, 1.0 + 1e-6):
            self.assertEqual(lf.post_table_texel(u), 511)

    def test_the_boundaries_lie_half_a_texel_from_k_over_512(self):
        for k in (0, 1, 2, 12, 128, 510):
            self.assertEqual(lf.post_table_texel((k + 0.5) / 512 - 1e-6), k, k)
            self.assertEqual(lf.post_table_texel((k + 0.5) / 512 + 1e-6), k + 1, k)
        self.assertEqual(lf.post_table_texel(511.5 / 512 + 1e-6), 511)      # the clamp

    def test_it_is_the_interpreters_point_texel_with_the_fetchs_own_offset(self):
        import ucode_eval as ue
        for i in range(2000):
            u = i / 1999.0 * 1.1 - 0.05
            want = min(max(ue.point_texel((u,), (512,), {"OffsetX": "0.5"})[0], 0), 511)
            self.assertEqual(lf.post_table_texel(u), want, u)

    def test_no_coordinate_leaves_the_table(self):
        for u in (NAN, INF, -INF, -1.0, -1e-30, 0.0, 1e30, 2.5):
            t = lf.post_table_texel(u)
            self.assertIsInstance(t, int)
            self.assertTrue(0 <= t <= 511, u)
        self.assertEqual(lf.post_table_texel(NAN), 0)
        self.assertEqual(lf.post_table_texel(-INF), 0)
        self.assertEqual(lf.post_table_texel(INF), 511)

    def test_another_size(self):
        self.assertEqual([lf.post_table_texel(u, 4) for u in (0.0, 0.124, 0.126, 0.5, 0.874, 0.876, 1.0)],
                         [0, 0, 1, 2, 3, 3, 3])
        with self.assertRaises(ValueError):
            lf.post_table_texel(0.5, 0)


class UnlitScale(unittest.TestCase):
    def test_it_is_the_reciprocal_of_the_gain_at_scene_luminance_one(self):
        table = [0.0] * 512
        table[128] = 0.2427
        self.assertAlmostEqual(lf.post_unlit_scale(1.1026, table), 1.0 / (1.1026 * 0.2427), delta=1e-12)

    def test_it_is_bounded_at_64(self):
        self.assertEqual(lf.post_unlit_scale(1.0, [0.0] * 512), 64.0)
        self.assertEqual(lf.post_unlit_scale(1.0, [1e-9] * 512), 64.0)
        self.assertEqual(lf.post_unlit_scale(NAN, [0.2] * 512), 64.0)
        self.assertEqual(lf.post_unlit_scale(1.0, [NAN] * 512), 64.0)
        self.assertEqual(lf.post_unlit_scale(-1.0, [0.2] * 512), 64.0)
        self.assertAlmostEqual(lf.post_unlit_scale(INF, [0.2] * 512), 64.0)

    def test_an_unlit_value_of_one_then_leaves_the_composites_gain_as_one(self):
        # A grey unlit pixel written as U goes through e = c77.y * T[128] as e * U = 1.
        table = [0.24] * 512
        U = lf.post_unlit_scale(REAL["c77"][1], table)
        got = composite((U, U, U), table=table, c77=(0.0, REAL["c77"][1]), c78x=1.0)
        near(self, got, [1.0, 1.0, 1.0])


class Ramp(unittest.TestCase):
    def setUp(self):
        self.ramp = [i / 255.0 * 0.9 for i in range(256)]

    def test_the_index_is_the_front_targets_8_bit_value(self):
        for level in (0, 1, 23, 124, 254, 255):
            self.assertEqual(lf.post_ramp_index(level / 255.0), level)
            self.assertEqual(lf.post_ramp(level / 255.0, self.ramp), self.ramp[level])

    def test_it_rounds_to_the_nearest_level(self):
        self.assertEqual(lf.post_ramp_index(100.49 / 255.0), 100)
        self.assertEqual(lf.post_ramp_index(100.51 / 255.0), 101)

    def test_no_value_leaves_the_ramp(self):
        for v, want in ((NAN, 0), (-1.0, 0), (-INF, 0), (1.0, 255), (1.2, 255), (INF, 255), (1e30, 255)):
            self.assertEqual(lf.post_ramp_index(v), want, v)
            self.assertEqual(lf.post_ramp(v, self.ramp), self.ramp[want])

    def test_a_ramp_of_another_length_is_refused(self):
        with self.assertRaises(ValueError):
            lf.post_ramp(0.5, [0.0] * 255)


# --- the composite ------------------------------------------------------------------------------------------

class Composite(unittest.TestCase):
    def test_a_flat_table_is_one_gain(self):
        # Above the grey fade (c47.x * L >= 1) and without the saturation step: out = (c77.y * T * scene)^c78.x.
        scene = (0.5, 0.8, 0.3)
        got = composite(scene, table=[0.25] * 512, c77=(0.0, 1.2))
        near(self, got, [(1.2 * 0.25 * x) ** (1.0 / 2.2) for x in scene])

    def test_the_gain_is_read_at_the_scenes_luminance(self):
        table = [0.0] * 512
        scene = (0.4, 0.4, 0.4)
        table[lf.post_table_texel(0.25 * lf.post_lum(scene))] = 0.5
        near(self, composite(scene, table=table, c77=(0.0, 1.0), c78x=1.0), [0.2, 0.2, 0.2])
        self.assertEqual(lf.post_table_texel(0.25 * 0.4), 51)

    def test_a_table_of_zeros_is_black(self):
        self.assertEqual(composite((0.5, 0.8, 0.3), table=[0.0] * 512), [0.0, 0.0, 0.0])

    def test_no_bloom_leaves_the_exposed_colour(self):
        scene = (0.5, 0.8, 0.3)
        c = [1.1 * 0.25 * x for x in scene]
        near(self, composite(scene, bloom=(0.0, 0.0, 0.0), c77=(0.0, 1.1), c78x=1.0), c)

    def test_the_bloom_is_a_screen_blend(self):
        scene, bloom = (0.5, 0.8, 0.3), (0.1, 0.02, 0.3)
        c = [1.1 * 0.25 * x for x in scene]
        b = [min(x * 2.0, 1.0) for x in bloom]
        want = [c[i] + b[i] * (1.0 - c[i]) for i in range(3)]
        near(self, composite(scene, bloom=bloom, c31x=2.0, c77=(0.0, 1.1), c78x=1.0), want)
        # A bloom that saturates gives white whatever the scene is.
        near(self, composite(scene, bloom=(9.0, 9.0, 9.0), c77=(0.0, 1.1), c78x=1.0), [1.0, 1.0, 1.0])

    def test_the_colour_fades_to_grey_in_the_dark(self):
        # c47.x * L below 1: the colour is L + c47.x * L * (scene - L).
        scene = (0.02, 0.005, 0.001)
        L = lf.post_lum(scene)
        k = 50.0 * L
        self.assertLess(k, 1.0)
        want = [0.25 * (L + k * (x - L)) for x in scene]
        near(self, composite(scene, c77=(0.0, 1.0), c78x=1.0), want)

    def test_the_saturation_step(self):
        scene = (0.9, 0.5, 0.2)
        c = [0.25 * x for x in scene]
        L2 = lf.post_lum(c)
        want = [x + 0.3 * (x - L2) for x in c]
        near(self, composite(scene, c77=(0.3, 1.0), c78x=1.0), want)

    def test_the_power_is_of_the_absolute_value(self):
        # A strong saturation step drives the smallest channel below zero; the shader takes |t|^c78.x.
        scene = (2.0, 0.1, 0.1)
        c = [min(0.25 * x, 1.0) for x in scene]
        L2 = lf.post_lum(c)
        t = [x + 4.0 * (x - L2) for x in c]
        self.assertLess(t[1], 0.0)
        got = composite(scene, c77=(4.0, 1.0), c78x=0.5)
        near(self, got, [min(abs(x) ** 0.5, 1.0) for x in t])

    def test_the_result_is_clamped_to_the_front_targets_range_unless_asked(self):
        scene = (3.9, 0.0, 0.0)
        args = ((0.0, 0.0, 0.0), [0.25] * 512, 1.0, 0.25, 50.0, (4.0, 1.0), 1.0)
        raw = lf.post_composite(scene, *args, clamp=False)
        self.assertGreater(raw[0], 1.0)         # the saturation step overshoots: 0.975 + 4 * (0.975 - L2)
        numbers(self, lf.post_composite(scene, *args))

    def test_values_that_are_no_numbers(self):
        # Spec Design 5: a reader of the scene takes a value that is no number as 0 and clamps to 31.875.
        table = [0.25] * 512
        for scene in ((NAN, 0.5, 0.5), (INF, 0.5, 0.5), (-1.0, 0.5, 0.5), (-INF, NAN, INF), (NAN, NAN, NAN),
                      (1e30, 1e30, 1e30), (0.0, 0.0, 0.0)):
            numbers(self, composite(scene, table=table))
        near(self, composite((NAN, 0.5, 0.5), c77=(0.0, 1.0), c78x=1.0), composite((0.0, 0.5, 0.5), c77=(0.0, 1.0), c78x=1.0))
        near(self, composite((-2.0, 0.5, 0.5), c77=(0.0, 1.0), c78x=1.0), composite((0.0, 0.5, 0.5), c77=(0.0, 1.0), c78x=1.0))
        near(self, composite((INF, 0.5, 0.5), c77=(0.0, 1.0), c78x=1.0), composite((31.875, 0.5, 0.5), c77=(0.0, 1.0), c78x=1.0))
        for bloom in ((NAN, 0.1, 0.1), (INF, -INF, 0.1), (-1.0, -1.0, -1.0)):
            numbers(self, composite((0.5, 0.5, 0.5), bloom=bloom))
        near(self, composite((0.5, 0.5, 0.5), bloom=(NAN, -1.0, 0.0)), composite((0.5, 0.5, 0.5)))
        # A table texel that is no number reads 0; a register that is no number gives black.
        self.assertEqual(composite((0.5, 0.5, 0.5), table=[NAN] * 512), [0.0, 0.0, 0.0])
        for name in ("c31x", "c46x", "c47x", "c78x"):
            self.assertEqual(composite((0.5, 0.5, 0.5), **{name: NAN}), [0.0, 0.0, 0.0], name)
        self.assertEqual(composite((0.5, 0.5, 0.5), c77=(NAN, 1.0)), [0.0, 0.0, 0.0])
        self.assertEqual(composite((0.5, 0.5, 0.5), c77=(0.0, INF)), [0.0, 0.0, 0.0])

    def test_a_table_at_its_bound_and_a_scene_at_its(self):
        numbers(self, composite((31.875, 31.875, 31.875), table=[3.999] * 512))
        numbers(self, composite((31.875, 0.0, 0.0), table=[3.999] * 512, c46x=64.0, c47x=1e6, c77=(-4.0, 64.0), c78x=1e-6))
        numbers(self, composite((1e-30, 0.0, 0.0), table=[3.999] * 512, c46x=1e-30, c47x=0.0, c77=(4.0, 1e-30)))

    def test_a_callable_table_gets_the_coordinate(self):
        seen = []

        def table(u):
            seen.append(u)
            return 0.25

        near(self, composite((0.4, 0.4, 0.4), table=table, c77=(0.0, 1.0), c78x=1.0), [0.1, 0.1, 0.1])
        self.assertEqual(len(seen), 1)
        self.assertAlmostEqual(seen[0], 0.25 * 0.4, delta=1e-12)


# --- the bloom's steps --------------------------------------------------------------------------------------

def image(w, h, fn):
    return [[fn(x, y) for x in range(w)] for y in range(h)]


class Texture(unittest.TestCase):
    def setUp(self):
        self.rows = image(4, 3, lambda x, y: (float(x), float(10 * y), 1.0))

    def test_point(self):
        t = lf.post_texture(self.rows)
        self.assertEqual(t(0.5, 0.5), [0.0, 0.0, 1.0])
        self.assertEqual(t(2.99, 1.01), [2.0, 10.0, 1.0])
        self.assertEqual(t(-5.0, 99.0), [0.0, 20.0, 1.0])        # clamp to edge
        self.assertEqual(t(NAN, NAN), [0.0, 0.0, 1.0])

    def test_linear_has_its_centres_at_half(self):
        t = lf.post_texture(self.rows, linear=True)
        near(self, t(1.5, 1.5), [1.0, 10.0, 1.0])
        near(self, t(2.0, 1.0), [1.5, 5.0, 1.0])                 # the corner of four texels: their mean
        near(self, t(1.75, 0.5), [1.25, 0.0, 1.0])
        near(self, t(0.0, 0.0), [0.0, 0.0, 1.0])                 # clamp to edge
        near(self, t(9.0, 9.0), [3.0, 20.0, 1.0])
        near(self, t(INF, -INF), [3.0, 0.0, 1.0])

    def test_an_empty_picture_is_refused(self):
        with self.assertRaises(ValueError):
            lf.post_texture([])


class Downsample(unittest.TestCase):
    def setUp(self):
        self.rows = image(16, 8, lambda x, y: ((x + 16 * y) / 8.0, 1.0, 0.5))
        self.tex = lf.post_texture(self.rows, linear=True)

    def test_with_the_position_integral_it_is_four_texels_of_the_sixteen(self):
        # Texels (0, 0), (2, 0), (0, 2), (2, 2) of the pixel's 4 x 4 block.
        got = lf.post_downsample(self.tex, 1, 1, 0.0)
        want = (self.rows[4][4][0] + self.rows[4][6][0] + self.rows[6][4][0] + self.rows[6][6][0]) / 4
        near(self, got, [want, 1.0, 0.5])

    def test_at_the_pixels_centre_it_is_four_texels_two_further_on(self):
        got = lf.post_downsample(self.tex, 1, 0, 0.5)
        want = (self.rows[2][6][0] + self.rows[2][8][0] + self.rows[4][6][0] + self.rows[4][8][0]) / 4
        near(self, got, [want, 1.0, 0.5])

    def test_the_position_is_taken_absolute(self):
        near(self, lf.post_downsample(self.tex, -2, -1, 0.0), lf.post_downsample(self.tex, 2, 1, 0.0))

    def test_the_four_coordinates(self):
        seen = []

        def tex(px, py):
            seen.append((px, py))
            return (1.0, 2.0, 3.0)

        near(self, lf.post_downsample(tex, 3, 5, 0.0), [1.0, 2.0, 3.0])
        self.assertEqual(seen, [(14.5, 22.5), (14.5, 20.5), (12.5, 20.5), (12.5, 22.5)])

    def test_values_that_are_no_numbers(self):
        for bad, counts_as in ((NAN, 0.0), (-4.0, 0.0), (INF, 31.875), (100.0, 31.875), (-INF, 0.0)):
            rows = image(8, 8, lambda x, y: (bad if (x, y) == (0, 0) else 1.0, 1.0, 1.0))
            got = lf.post_downsample(lf.post_texture(rows, linear=True), 0, 0, 0.0)
            near(self, got, [(counts_as + 3.0) / 4, 1.0, 1.0])
            numbers(self, got, 0.0, 31.875)
        numbers(self, lf.post_downsample(self.tex, NAN, INF, 0.0), 0.0, 31.875)


class Bright(unittest.TestCase):
    REGS = dict(c31x=1.4865, c46x=0.2, c47x=2.0, c77=(0.0869, 1.0 / 8.44))

    def bright(self, d, tf1=(0.0, 0.0, 0.0), tf2=(0.0, 0.0, 0.0), **over):
        r = dict(self.REGS, **over)
        return lf.post_bright(d, tf1, tf2, r["c31x"], r["c46x"], r["c47x"], r["c77"], **{k: v for k, v in over.items() if k == "clamp"})

    def test_the_part_of_the_luminance_above_the_threshold_in_the_pixels_own_colour(self):
        d = (2.0, 1.5, 1.0)
        L = lf.post_lum(d)
        k = (L / 8.44 - 0.0869) / L
        near(self, self.bright(d), [x * k * 1.4865 / 2.0 for x in d])

    def test_below_the_threshold_the_scene_gives_nothing(self):
        self.assertEqual(self.bright((0.5, 0.5, 0.5)), [0.0, 0.0, 0.0])

    def test_the_two_other_inputs(self):
        got = self.bright((0.5, 0.5, 0.5), tf1=(0.1, 0.2, 0.3), tf2=(0.05, 0.0, 0.4))
        near(self, got, [(0.1 * 6 * 0.2 + 0.05) / 2, (0.2 * 6 * 0.2) / 2, (0.3 * 6 * 0.2 + 0.4) / 2])

    def test_a_black_pixel_gives_no_scene_term(self):
        # L = 0: the microcode computes 0 * (1 / 0); the reference's scene term is 0.
        self.assertEqual(self.bright((0.0, 0.0, 0.0)), [0.0, 0.0, 0.0])
        near(self, self.bright((0.0, 0.0, 0.0), tf2=(0.5, 0.5, 0.5)), [0.25, 0.25, 0.25])
        self.assertEqual(self.bright((0.0, 0.0, 0.0), c77=(-1.0, 1.0)), [0.0, 0.0, 0.0])

    def test_the_result_is_clamped_to_the_targets_range_unless_asked(self):
        d, tf2 = (30.0, 30.0, 30.0), (3.0, 3.0, 3.0)
        self.assertGreater(self.bright(d, tf2=tf2, clamp=False)[0], 1.0)
        self.assertEqual(self.bright(d, tf2=tf2), [1.0, 1.0, 1.0])

    def test_values_that_are_no_numbers(self):
        for d in ((NAN, 1.0, 1.0), (INF, 1.0, 1.0), (-1.0, -1.0, -1.0), (NAN, NAN, NAN), (INF, INF, INF)):
            numbers(self, self.bright(d))
        for tf in ((NAN, 0.1, 0.1), (INF, 0.1, 0.1), (-INF, -1.0, 0.1)):
            numbers(self, self.bright((2.0, 2.0, 2.0), tf1=tf))
            numbers(self, self.bright((2.0, 2.0, 2.0), tf2=tf))
        for over in (dict(c47x=0.0), dict(c47x=NAN), dict(c47x=-1.0), dict(c31x=INF), dict(c46x=NAN), dict(c77=(NAN, 1.0)),
                     dict(c77=(0.0, INF))):
            self.assertEqual(self.bright((2.0, 2.0, 2.0), tf2=(0.5, 0.5, 0.5), **over), [0.0, 0.0, 0.0], over)
        # Registers at their bounds (MakePostBlock's ranges).
        numbers(self, self.bright((31.875, 31.875, 31.875), c31x=64.0, c46x=64.0, c47x=1e-3, c77=(-4.0, 64.0)))
        numbers(self, self.bright((1e-30, 0.0, 0.0), c31x=64.0, c47x=1e-3, c77=(-4.0, 64.0)))


class Blur(unittest.TestCase):
    W = (0.20738, 0.1812, 0.12088, 0.06157, 0.02394, 0.00711, 0.00161)

    def test_thirteen_taps_along_x(self):
        rows = image(32, 4, lambda x, y: (float(x == 10), float(x), float(y)))
        tex = lf.post_texture(rows)
        for x in range(2, 20):
            got = lf.post_blur13(tex, x, 1, self.W, False, clamp=False)
            w = self.W[abs(x - 10)] if abs(x - 10) <= 6 else 0.0
            self.assertAlmostEqual(got[0], w, delta=1e-12)
        total = self.W[0] + 2 * sum(self.W[1:])
        got = lf.post_blur13(tex, 15, 2, self.W, False, clamp=False)
        self.assertAlmostEqual(got[1], 15.0 * total, delta=1e-9)      # symmetric weights keep a ramp
        self.assertAlmostEqual(got[2], 2.0 * total, delta=1e-9)

    def test_thirteen_taps_along_y(self):
        rows = image(4, 32, lambda x, y: (float(y == 10), float(x), 0.0))
        tex = lf.post_texture(rows)
        for y in range(2, 20):
            got = lf.post_blur13(tex, 1, y, self.W, True, clamp=False)
            w = self.W[abs(y - 10)] if abs(y - 10) <= 6 else 0.0
            self.assertAlmostEqual(got[0], w, delta=1e-12)

    def test_the_taps_are_the_texel_centres(self):
        seen = []

        def tex(px, py):
            seen.append((px, py))
            return (0.0, 0.0, 0.0)

        lf.post_blur13(tex, 7, 3, self.W, False)
        self.assertEqual(sorted(seen), [(7.5 + k, 3.5) for k in range(-6, 7)])
        del seen[:]
        lf.post_blur13(tex, 7, 3, self.W, True)
        self.assertEqual(sorted(seen), [(7.5, 3.5 + k) for k in range(-6, 7)])

    def test_the_edge_is_clamped_by_the_texture(self):
        rows = image(8, 1, lambda x, y: (1.0, 1.0, 1.0))
        got = lf.post_blur13(lf.post_texture(rows), 0, 0, self.W, False)
        near(self, got, [self.W[0] + 2 * sum(self.W[1:])] * 3)

    def test_values_that_are_no_numbers(self):
        rows = image(16, 1, lambda x, y: (NAN, INF, -1.0) if x == 8 else (0.5, 0.5, 0.5))
        got = lf.post_blur13(lf.post_texture(rows), 8, 0, self.W, False)
        numbers(self, got)
        for w in ([NAN] * 7, [INF] * 7, [1.0] * 7, [0.0] * 7, [-1.0] * 7):
            numbers(self, lf.post_blur13(lf.post_texture(rows), 8, 0, w, False))
        with self.assertRaises(ValueError):
            lf.post_blur13(lf.post_texture(rows), 8, 0, self.W[:6], False)


class Radial(unittest.TestCase):
    def test_fourteen_taps_towards_the_centre(self):
        taps = lf.post_radial_taps(0.9, 0.2, (0.5, 0.5), 0.87, 0.935)
        self.assertEqual(len(taps), 14)
        s = 1.0 + (4.0 / 7.0) * (0.935 - 1.0)
        g = 1.0 + (4.0 / 7.0) * (0.87 - 1.0)
        for k, ((u, v), w) in enumerate(taps):
            self.assertAlmostEqual(u, 0.5 + 0.4 * s ** k, delta=1e-12)
            self.assertAlmostEqual(v, 0.5 - 0.3 * s ** k, delta=1e-12)
            self.assertAlmostEqual(w, 0.5 * g ** k, delta=1e-12)
        self.assertEqual(taps[0][0], (0.9, 0.2))
        self.assertAlmostEqual(s, 0.9629, delta=1e-4)
        self.assertAlmostEqual(g, 0.9257, delta=1e-4)

    def test_the_sum_times_four_sevenths(self):
        got = lf.post_radial(lambda u, v: (1.0, 0.5, 0.0), 0.3, 0.3, (0.5, 0.5), 0.87, 0.935, clamp=False)
        total = sum(w for _, w in lf.post_radial_taps(0.3, 0.3, (0.5, 0.5), 0.87, 0.935)) * 4.0 / 7.0
        near(self, got, [total, total / 2, 0.0])
        self.assertAlmostEqual(total, 2.54, delta=0.005)             # research section 2: "2.54 a pass"
        self.assertEqual(lf.post_radial(lambda u, v: (1.0, 0.5, 0.0), 0.3, 0.3, (0.5, 0.5), 0.87, 0.935)[0], 1.0)

    def test_at_the_centre_every_tap_is_the_centre(self):
        seen = []

        def tex(u, v):
            seen.append((u, v))
            return (0.1, 0.1, 0.1)

        lf.post_radial(tex, 0.25, 0.75, (0.25, 0.75), 0.87, 0.935)
        self.assertEqual(set(seen), {(0.25, 0.75)})

    def test_a_centre_outside_the_picture_and_values_that_are_no_numbers(self):
        tex = lf.post_texture(image(8, 8, lambda x, y: (NAN, INF, 0.5) if (x + y) % 3 == 0 else (0.2, 0.2, -1.0)))
        norm = lambda u, v: tex(u * 8, v * 8)       # noqa: E731
        for centre in ((-3.0, 0.5), (0.0, 0.0), (1.0, 4.0), (0.5, 0.5)):
            numbers(self, lf.post_radial(norm, 0.9, 0.1, centre, 0.87, 0.935))
        for bad in (dict(c31=(NAN, 0.5)), dict(c46x=NAN), dict(c47x=INF)):
            args = dict(c31=(0.5, 0.5), c46x=0.87, c47x=0.935)
            args.update(bad)
            self.assertEqual(lf.post_radial(norm, 0.9, 0.1, **args), [0.0, 0.0, 0.0])


class Reduce(unittest.TestCase):
    def test_one_linear_fetch_is_the_mean_of_a_two_by_two_block(self):
        rows = image(8, 6, lambda x, y: (float(x), float(10 * y), float(x * y)))
        tex = lf.post_texture(rows, linear=True)
        for x, y in ((0, 0), (1, 2), (3, 1)):
            block = [rows[2 * y + j][2 * x + i] for j in (0, 1) for i in (0, 1)]
            near(self, lf.post_reduce(tex, x, y, clamp=False), [sum(t[c] for t in block) / 4 for c in range(3)])
        self.assertEqual(lf.post_reduce(tex, 3, 1), [1.0, 1.0, 1.0])       # the 10-bit target's range

    def test_values_that_are_no_numbers(self):
        rows = image(4, 4, lambda x, y: (NAN, INF, -2.0))
        numbers(self, lf.post_reduce(lf.post_texture(rows, linear=True), 0, 0))


class PostShaders(unittest.TestCase):
    def test_the_eight_hashes_and_their_roles(self):
        self.assertEqual([(s.hash, s.role) for s in lf.POST_SHADERS], [
            ("0A96B3FD5121E331", "reduce"), ("87E85199E1E9B260", "downsample"), ("5821366EE8FDE893", "luminance"),
            ("EE922CFB4426EA83", "bright"), ("EA83C7E7E24EC4C8", "blur-x"), ("28DBE18F313350BF", "blur-y"),
            ("4BE4344C27016E04", "radial"), ("37B186F4BD3288F7", "composite")])
        for s in lf.POST_SHADERS:
            self.assertRegex(s.hash, r"^[0-9A-F]{16}$")

    def test_every_shader_has_a_check(self):
        self.assertEqual(set(pc.CHECKS), {s.hash for s in lf.POST_SHADERS})


# --- the check on synthetic microcode ---------------------------------------------------------------------

# 5821's two instructions with another offset, and a composite whose table fetch has no offset.
LUMINANCE = """/*    0.0 */       exec
/*    2   */          tfetch2D r0.xyz_, r0.xy, tf0, OffsetX=0.5, OffsetY=0.5
/*    0.1 */       alloc colors
/*    1.0 */       exece
/*    3   */          dp3 oC0, r0.zxyy, c255.xyzz
/*    1.1 */       cnop
"""


class SyntheticDump(unittest.TestCase):
    def test_a_luminance_shader_agrees(self):
        res = pc.check_text("5821366EE8FDE893", LUMINANCE, trials=50)
        self.assertTrue(res.ok, res.line())
        self.assertEqual((res.agree, res.values), (200, 200))

    def test_another_offset_fails(self):
        res = pc.check_text("5821366EE8FDE893", LUMINANCE.replace("OffsetX=0.5", "OffsetX=1.5"), trials=50)
        self.assertFalse(res.ok)

    def test_other_weights_fail(self):
        res = pc.check_text("5821366EE8FDE893", LUMINANCE.replace("r0.zxyy", "r0.xyzz"), trials=50)
        self.assertFalse(res.ok)

    def test_no_trials_is_not_agreement(self):
        self.assertFalse(pc.check_text("5821366EE8FDE893", LUMINANCE, trials=0).ok)

    def test_an_unknown_shader_is_refused(self):
        with self.assertRaises(KeyError):
            pc.check_text("0000000000000000", LUMINANCE, trials=1)


class CommandLine(unittest.TestCase):
    def test_a_missing_dump_folder_is_a_failure_not_a_pass(self):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertNotEqual(pc.main(["--dumps", str(ROOT / "out" / "no_such_dump_folder")]), 0)


# --- the pins ---------------------------------------------------------------------------------------------

class Pins(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = post_pins.lines()

    def test_the_lines_are_the_same_in_every_run(self):
        self.assertEqual(self.lines, post_pins.lines())

    def test_every_section_is_there(self):
        heads = [line for line in self.lines if line.startswith("// ")]
        for word in ("table texel pins", "unlit scale pins", "ramp pins", "composite pins", "downsample pins",
                     "bright pins", "blur pins", "radial pins", "reduce pins"):
            self.assertTrue(any(word in h for h in heads), word)

    def test_every_number_is_a_number(self):
        for line in self.lines:
            if line.startswith("//"):
                continue
            self.assertNotIn("nan", line.replace("NAN", "").replace("kNaN", ""), line)
            self.assertNotIn("inf", line.replace("INF", "").replace("kInf", ""), line)

    def test_the_table_is_the_excerpts_real_table(self):
        table = post_pins.load_table()
        self.assertEqual(len(table), 512)
        self.assertEqual(table[0], 0.0)                 # texel 0 is 0 in every captured table (research section 3)
        self.assertTrue(all(math.isfinite(t) and 0.0 <= t < 4.0 for t in table))
        self.assertAlmostEqual(table[128], 0.2427, delta=5e-4)     # T at scene luminance 1, capture 092959

    def test_the_composite_pins_are_the_reference(self):
        table = post_pins.load_table()
        for name, regs in post_pins.SETS:
            for scene, bloom in post_pins.COMPOSITE_POINTS:
                want = lf.post_composite([post_pins.f32(x) for x in scene], [post_pins.f32(x) for x in bloom], table,
                                         regs["c31x"], regs["c46x"], regs["c47x"], regs["c77"], regs["c78x"])
                self.assertEqual(post_pins.composite_pin(regs, scene, bloom, table), want)
                numbers(self, want)

    def test_no_composite_point_lies_near_a_texel_boundary(self):
        # A single-precision mirror must land in the texel the line prints: u * 512 + 0.5 is at least 1e-3
        # from an integer at every point.
        for name, regs in post_pins.SETS:
            for scene, _ in post_pins.COMPOSITE_POINTS:
                x = post_pins.composite_u(regs, scene) * 512 + 0.5
                if math.isfinite(x) and 0.0 < x < 512.0:
                    self.assertGreaterEqual(abs(x - round(x)), 1e-3, (name, scene))

    def test_the_lines_are_the_ones_last_looked_at(self):
        # The digest of post_pins.lines(): a change of the reference, of the excerpt or of the points changes
        # it, and the C++ lines copied from it are then stale. After such a change: look at the lines
        # (python tools/xdk_sigmatch/post_pins.py), update the C++ test, then this digest.
        import hashlib
        self.assertEqual(hashlib.sha256(chr(10).join(self.lines).encode("ascii")).hexdigest(), PINS_DIGEST)

    def test_the_points_reach_the_guards(self):
        flat = [x for scene, bloom in post_pins.COMPOSITE_POINTS for x in tuple(scene) + tuple(bloom)]
        self.assertTrue(any(x != x for x in flat))
        self.assertTrue(any(x == INF for x in flat))
        self.assertTrue(any(x < 0 for x in flat))
        self.assertTrue(any(lf.post_lum(d) == 0.0 for d, _, _ in post_pins.BRIGHT_POINTS))


# --- the real dumps ---------------------------------------------------------------------------------------

@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class RealDumps(unittest.TestCase):
    TRIALS = 500        # spec criterion 1's count: 2,000 values a shader (the eight take about a second)

    def test_every_post_shader_agrees(self):
        for s in lf.POST_SHADERS:
            with self.subTest(shader=s.hash, role=s.role):
                res = pc.check(s.hash, trials=self.TRIALS, dumps=DUMPS)
                self.assertTrue(res.ok, f"{s.hash}: {res.line()} {res.first_bad}")
                self.assertEqual(res.values, 4 * self.TRIALS)
                self.assertEqual(res.agree, res.values)
                self.assertLess(res.worst, 1e-12)

    def test_the_composites_table_texel_is_the_microcodes(self):
        res = pc.check("37B186F4BD3288F7", trials=self.TRIALS, dumps=DUMPS)
        self.assertEqual(res.coords_ok, self.TRIALS)
        # A table read without the fetch's half texel (floor(u * 512), the research replay's reading) fails.
        floor = lambda u, n=512: min(max(int(math.floor(u * n)), 0), n - 1)     # noqa: E731
        self.assertFalse(pc.check("37B186F4BD3288F7", trials=self.TRIALS, dumps=DUMPS, texel=floor).ok)

    def test_wrong_formulas_fail(self):
        def no_fade(scene, bloom, table, c31x, c46x, c47x, c77, c78x, clamp=True):
            return lf.post_composite(scene, bloom, table, c31x, c46x, 1e9, c77, c78x, clamp=clamp)

        def bloom_added(scene, bloom, table, c31x, c46x, c47x, c77, c78x, clamp=True):
            a = lf.post_composite(scene, (0.0, 0.0, 0.0), table, c31x, c46x, c47x, (0.0, c77[1]), 1.0, clamp=False)
            return [abs(a[i] + lf.sat(bloom[i] * c31x)) ** c78x for i in range(3)]

        def no_absolute_value(scene, bloom, table, c31x, c46x, c47x, c77, c78x, clamp=True):
            one = lf.post_composite(scene, bloom, table, c31x, c46x, c47x, c77, 1.0, clamp=False)      # |t|
            none = lf.post_composite(scene, bloom, table, c31x, c46x, c47x, (0.0, c77[1]), 1.0, clamp=False)   # s
            L2 = lf.post_lum(none)
            t = [s + c77[0] * (s - L2) for s in none]
            return [max(x, 0.0) ** c78x if x > 0.0 else 0.0 for x in t]

        def bright_no_threshold(d, tf1, tf2, c31x, c46x, c47x, c77, clamp=True):
            return lf.post_bright(d, tf1, tf2, c31x, c46x, c47x, (0.0, c77[1]), clamp=clamp)

        def bright_inputs_exchanged(d, tf1, tf2, c31x, c46x, c47x, c77, clamp=True):
            return lf.post_bright(d, tf2, tf1, c31x, c46x, c47x, c77, clamp=clamp)

        def blur_other_axis(tex, x, y, weights7, vertical, clamp=True):
            return lf.post_blur13(tex, x, y, weights7, not vertical, clamp=clamp)

        def blur_weights_reversed(tex, x, y, weights7, vertical, clamp=True):
            return lf.post_blur13(tex, x, y, list(weights7)[::-1], vertical, clamp=clamp)

        def downsample_box(tex, x, y, bias):
            taps = [tex(4 * abs(x) + i + 0.5, 4 * abs(y) + j + 0.5) for i in range(4) for j in range(4)]
            return [sum(t[c] for t in taps) / 16 for c in range(len(taps[0]))]

        def reduce_no_offset(tex, x, y):
            return list(tex(2.0 * x, 2.0 * y))

        def radial_factors_exchanged(tex, u, v, c31, c46x, c47x, clamp=True):
            return lf.post_radial(tex, u, v, c31, c47x, c46x, clamp=clamp)

        for shader, formula in (("37B186F4BD3288F7", no_fade), ("37B186F4BD3288F7", bloom_added),
                                ("37B186F4BD3288F7", no_absolute_value),
                                ("EE922CFB4426EA83", bright_no_threshold), ("EE922CFB4426EA83", bright_inputs_exchanged),
                                ("EA83C7E7E24EC4C8", blur_other_axis), ("28DBE18F313350BF", blur_other_axis),
                                ("EA83C7E7E24EC4C8", blur_weights_reversed), ("87E85199E1E9B260", downsample_box),
                                ("0A96B3FD5121E331", reduce_no_offset), ("4BE4344C27016E04", radial_factors_exchanged),
                                ("5821366EE8FDE893", lambda c: c[0])):
            with self.subTest(shader=shader, formula=formula.__name__):
                self.assertFalse(pc.check(shader, trials=40, dumps=DUMPS, formula=formula).ok)

    def test_the_downsample_bias_is_not_the_microcodes_business(self):
        # The microcode multiplies the position it is given; whether that position is integral is the
        # rasteriser's (question 6). With the position handed over as x + bias both sides agree for either bias.
        for bias in (0.0, 0.5):
            res = pc.check("87E85199E1E9B260", trials=40, dumps=DUMPS, bias=bias)
            self.assertTrue(res.ok, res.line())

    def test_the_trials_are_the_same_in_every_process(self):
        a = pc.check("EE922CFB4426EA83", trials=20, dumps=DUMPS)
        b = pc.check("EE922CFB4426EA83", trials=20, dumps=DUMPS)
        self.assertEqual((a.agree, a.worst), (b.agree, b.worst))

    def test_the_command_line(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = pc.main(["--trials", "5", "--dumps", str(DUMPS)])
        text = out.getvalue()
        self.assertEqual(code, 0, text)
        self.assertIn("8 of 8 post shaders agree", text)
        self.assertIn("37B186F4BD3288F7", text)


if __name__ == "__main__":
    unittest.main()
