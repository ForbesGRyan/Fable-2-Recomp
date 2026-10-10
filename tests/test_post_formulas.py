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


PINS_DIGEST = "fc11790fcaf17cd0a76841a51968d7ad61b08414230a6153ca81544eefa95069"


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
        self.assertLessEqual({s.hash for s in lf.POST_SHADERS}, set(pc.CHECKS))      # the furniture's are there too


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


# --- the sun layer and the sky furniture (plan Task 11) ---------------------------------------------------

TEX = (0.5, 0.25, 0.125, 0.8)           # a texel: red, green, blue, alpha
C31 = (0.4, 0.2, 0.1, 0.5)
MOON, GLARE, BEAM, FLARE = "18D39DCEBEE4C2FC", "04985A0F7296E130", "8E4DBB9E9A63EF67", "D72FD0903CCE7DD6"
GLOW, DISC, LAYER_DEPTH, PREVIOUS = "4B58837D42F08C89", "3E77B1AD4BC5B900", "5D6768847B0602B8", "686DE15FC84D8B73"
CLOUD, CLOUD_OCCLUDER, TREE_OCCLUDER = "61976295517D7015", "3FF79524FCDB71C5", "F1EB2497108F159D"
QUAD_VS, CLOUD_VS, CUBE_VS = "81BADE63A169E3DC", "C304C6A1AF18B46F", "D1CC6DC28244C55A"


class FurnitureQuads(unittest.TestCase):
    def test_the_moon(self):
        # tex.rgb * c31.rgb * tex.a * c20.w; alpha c31.w * tex.a
        near(self, lf.post_moon(TEX, C31, 8.0), [1.28, 0.32, 0.08, 0.4])

    def test_the_glare(self):
        near(self, lf.post_glare(TEX, C31), [0.2, 0.05, 0.0125, 1.0])

    def test_the_beam_source_is_a_sixth_of_the_glare(self):
        near(self, lf.post_beam_source(TEX, C31), [0.2 / 6, 0.05 / 6, 0.0125 / 6, 1.0])
        near(self, lf.post_beam_source(TEX, C31, c255x=0.5), [0.1, 0.025, 0.00625, 1.0])

    def test_the_flare_is_the_textures_alpha_in_the_registers_colour(self):
        near(self, lf.post_flare(TEX, C31), [0.32, 0.16, 0.08, 1.0])

    def test_the_moon_glow(self):
        # tex.rgb * 0.2 * tex.a; alpha tex.a
        near(self, lf.post_moon_glow(TEX), [0.08, 0.04, 0.02, 0.8])

    def test_the_sun_disc(self):
        # the moon's colour, but its alpha is the texture's alone
        near(self, lf.post_sun_disc(TEX, C31, 1.0), [0.16, 0.04, 0.01, 0.8])
        near(self, lf.post_sun_disc(TEX, C31, 2.0), [0.32, 0.08, 0.02, 0.8])

    def test_the_layers_depth_is_the_scenes_clamped_and_its_colour_nothing(self):
        self.assertEqual(lf.post_layer_depth((0.25, 0.9, 0.9, 0.9)), ([0.0, 0.0, 0.0, 0.0], 0.25))
        self.assertEqual(lf.post_layer_depth((1.5, 0.0, 0.0, 0.0))[1], 1.0)
        self.assertEqual(lf.post_layer_depth((-0.5, 0.0, 0.0, 0.0))[1], 0.0)
        self.assertEqual(lf.post_layer_depth((NAN, 0.0, 0.0, 0.0))[1], 0.0)

    def test_the_previous_frame_is_its_texel(self):
        self.assertEqual(lf.post_previous_frame(TEX), list(TEX))
        self.assertEqual(lf.POST_PREVIOUS_BLEND, 0x3DD0D0D2)
        self.assertAlmostEqual(lf.POST_PREVIOUS_WEIGHT, 0.102, delta=5e-4)

    def test_the_targets_clamps(self):
        # In the scene (moon, glare): at most the scene target's largest value; in the layer: at most 1.
        self.assertEqual(lf.post_moon((1.0, 1.0, 1.0, 1.0), (9.0, 9.0, 9.0, 3.0), 16.0), [31.875, 31.875, 31.875, 1.0])
        self.assertEqual(lf.post_glare((1.0, 1.0, 1.0, 1.0), (99.0, 0.5, -1.0, 0.0)), [31.875, 0.5, 0.0, 1.0])
        self.assertEqual(lf.post_beam_source((1.0, 1.0, 1.0, 1.0), (12.0, 3.0, -1.0, 0.0)), [1.0, 0.5, 0.0, 1.0])
        self.assertEqual(lf.post_flare((0.0, 0.0, 0.0, 1.0), (2.0, 0.5, -1.0, 0.0)), [1.0, 0.5, 0.0, 1.0])
        self.assertEqual(lf.post_moon_glow((9.0, 1.0, -1.0, 1.0)), [1.0, 0.2, 0.0, 1.0])
        self.assertEqual(lf.post_sun_disc((1.0, 1.0, 1.0, 1.0), (2.0, 0.5, -1.0, 0.0), 1.0), [1.0, 0.5, 0.0, 1.0])
        # Without the clamp (the microcode's own values).
        near(self, lf.post_glare((1.0, 1.0, 1.0, 1.0), (99.0, 0.5, -1.0, 0.0), clamp=False), [99.0, 0.5, -1.0, 1.0])

    def test_values_that_are_no_numbers(self):
        bad = (NAN, INF, -INF, -1.0)
        for t in ((NAN, 0.5, 0.5, 0.5), (0.5, 0.5, 0.5, NAN), (INF, 0.5, 0.5, 1.0), (0.5, -INF, 0.5, INF), (-1.0, -1.0, -1.0, -1.0)):
            for c31 in (C31, (NAN, 1.0, 1.0, 1.0), (1.0, INF, 1.0, NAN), (-INF, 1.0, 1.0, 1.0)):
                for scale in (8.0,) + bad:
                    numbers(self, lf.post_moon(t, c31, scale)[:3], 0.0, 31.875)
                    numbers(self, lf.post_moon(t, c31, scale)[3:])
                    numbers(self, lf.post_sun_disc(t, c31, scale))
                numbers(self, lf.post_glare(t, c31)[:3], 0.0, 31.875)
                numbers(self, lf.post_beam_source(t, c31))
                numbers(self, lf.post_flare(t, c31))
            numbers(self, lf.post_moon_glow(t))
            numbers(self, lf.post_previous_frame(t))
            numbers(self, lf.post_tree_occluder(t, NAN))
            numbers(self, lf.post_tree_occluder(t, 0.5))
        for x in bad:
            numbers(self, lf.post_beam_source(TEX, C31, c255x=x))
            numbers(self, lf.post_moon_glow(TEX, c255x=x))


def cloud_texture(alpha=lambda dx, dy: 0.6, calls=None):
    def tex(u, v, dx, dy):
        if calls is not None:
            calls.append((u, v, dx, dy))
        return (0.5, 0.25, 0.125, alpha(dx, dy))
    return tex


CLOUD_REGS = dict(c20w=4.0, c31=(5.0, 6.0), c46=(0.25, 0.5), c47=(1.0, 0.5, 0.25), c51x=2.0, c69x=0.5, c70x=0.5,
                  c71x=0.1, c77=(0.0, 0.0, 12.0), c78=(0.0, 0.0, -1.0), c79=(100.0, 200.0, 50.0))


def cloud(tex=None, uv=(0.1, 0.2), P=(100.0, 200.0, 10.0), **over):
    return lf.post_cloud(tex or cloud_texture(), uv, P, **dict(CLOUD_REGS, **over))


class Cloud(unittest.TestCase):
    def test_lit_from_behind_the_density_is_what_lets_light_through(self):
        # Flat density: N = (0, 0, 1). The far point c77 straight above the pixel: D = P - c77 = (0, 0, -2),
        # Dn = (0, 0, -1): b1 = sat(-Dn . N) = 1, b2 = 0; l = b1^2 * (1 - a) = 0.4.
        # rgb = (l * c47 + c71.x) * c51.x * tex * c20.w; alpha = a * c69.x * fade = 0.6 * 0.5 * 1.
        near(self, cloud(c77=(100.0, 200.0, 12.0)), [2.0, 0.6, 0.2, 0.3])

    def test_lit_from_the_front(self):
        # Dn = (0, 0, 1): b2 = 1, b1 = 0. c78 = (0, 0, 1): a1 = sat(-N . c78) = 0, (1 - a1)^32 = 1; l = 2.
        near(self, cloud(c77=(100.0, 200.0, 7.0), c78=(0.0, 0.0, 1.0)), [8.4, 2.2, 0.6, 0.3])
        # c78 = (0, 0, -0.5): a1 = 0.5, l = 1 + 0.5^32.
        got = cloud(c77=(100.0, 200.0, 7.0), c78=(0.0, 0.0, -0.5))
        near(self, got, [(1 + 0.5 ** 32 + 0.1) * 4, (0.5 * (1 + 0.5 ** 32) + 0.1) * 2, (0.25 * (1 + 0.5 ** 32) + 0.1), 0.3])
        # c78 = (0, 0, -1): a1 = 1, the power of zero is 0; l = 1.
        near(self, cloud(c77=(100.0, 200.0, 7.0)), [4.4, 1.2, 0.35, 0.3])

    def test_the_normal_is_the_densitys_gradient_over_two_texels(self):
        # a(+1, 0) - a(-1, 0) = 0.6, c70.x = 0.8: N = (0.6, 0, 0.8). c78 = (-1, 0, 0): a1 = 0.6.
        # D = (5, 0, 0): Dn = (1, 0, 0), b2 = 0.6; l = 0.6 * (1 + 0.4^32).
        along_x = cloud_texture(lambda dx, dy: 0.6 + 0.3 * dx)
        l = 0.6 * (1 + 0.4 ** 32)
        near(self, cloud(along_x, c70x=0.8, c78=(-1.0, 0.0, 0.0), c77=(95.0, 200.0, 10.0)),
             [(l + 0.1) * 4, (l * 0.5 + 0.1) * 2, l * 0.25 + 0.1, 0.3])
        # The same slope along y: N = (0, 0.6, 0.8), nothing of it towards the light or the far point: l = 0.
        along_y = cloud_texture(lambda dx, dy: 0.6 + 0.3 * dy)
        near(self, cloud(along_y, c70x=0.8, c78=(-1.0, 0.0, 0.0), c77=(95.0, 200.0, 10.0)), [0.4, 0.2, 0.1, 0.3])
        # ... and all of it with the light and the far point along y.
        near(self, cloud(along_y, c70x=0.8, c78=(0.0, -1.0, 0.0), c77=(100.0, 195.0, 10.0)),
             [(l + 0.1) * 4, (l * 0.5 + 0.1) * 2, l * 0.25 + 0.1, 0.3])

    def test_the_coordinate_and_the_five_fetches(self):
        calls = []
        cloud(cloud_texture(calls=calls), uv=(0.1, 0.2), c77=(100.0, 200.0, 12.0))
        self.assertEqual(sorted(c[2:] for c in calls), [(-1, 0), (0, -1), (0, 0), (0, 1), (1, 0)])
        for u, v, _, _ in calls:
            self.assertAlmostEqual(u, 0.1 * 5.0 + 0.25, delta=1e-15)
            self.assertAlmostEqual(v, 0.2 * 6.0 + 0.5, delta=1e-15)
        self.assertEqual(lf.post_cloud_uv((0.1, 0.25), (5.0, 6.0), (0.25, 0.5)), (0.75, 2.0))

    def test_the_fade_with_the_distance_from_the_eye_on_the_ground_plane(self):
        # 1 up to 1000 from the eye, 0 from 2000 on; the height does not count.
        eye = CLOUD_REGS["c79"]
        self.assertEqual(lf.post_cloud_fade((100.0, 200.0, 999.0), eye), 1.0)
        self.assertEqual(lf.post_cloud_fade((100.0 + 1000.0, 200.0, 0.0), eye), 1.0)
        self.assertAlmostEqual(lf.post_cloud_fade((100.0, 200.0 - 1500.0, 0.0), eye), 0.5, delta=1e-12)
        self.assertAlmostEqual(lf.post_cloud_fade((100.0 + 900.0, 200.0 + 1200.0, 0.0), eye), 0.5, delta=1e-12)
        self.assertEqual(lf.post_cloud_fade((100.0 + 2500.0, 200.0, 0.0), eye), 0.0)
        self.assertAlmostEqual(cloud(P=(100.0, 200.0 - 1500.0, 10.0), c77=(100.0, -1300.0, 12.0))[3], 0.15, delta=1e-12)

    def test_a_thin_cloud_has_no_colour(self):
        # alpha at or below c254.y = 0.001: black with its alpha (the alpha test's business).
        self.assertEqual(cloud(P=(2700.0, 200.0, 10.0)), [0.0, 0.0, 0.0, 0.0])
        thin = cloud_texture(lambda dx, dy: 0.001)
        self.assertEqual(cloud(thin, c69x=1.0), [0.0, 0.0, 0.0, 0.001])              # at the threshold: not above it
        self.assertGreater(cloud(cloud_texture(lambda dx, dy: 0.002), c69x=1.0, c77=(100.0, 200.0, 12.0))[0], 0.0)
        self.assertEqual(lf.POST_CLOUD_C254, (0.0, 0.001, 1000.0, 1.0))
        self.assertEqual(lf.POST_CLOUD_C255X, 32.0)

    def test_values_that_are_no_numbers(self):
        for bad in (NAN, INF, -INF):
            numbers(self, cloud(cloud_texture(lambda dx, dy: bad))[:3], 0.0, 31.875)
            numbers(self, cloud(cloud_texture(lambda dx, dy: 0.6 if dx == 0 else bad))[:3], 0.0, 31.875)
            for name in ("c20w", "c51x", "c69x", "c70x", "c71x"):
                self.assertEqual(cloud(**{name: bad}), [0.0, 0.0, 0.0, 0.0])
            for name in ("c31", "c46", "c47", "c77", "c78", "c79"):
                self.assertEqual(cloud(**{name: (bad,) * 3}), [0.0, 0.0, 0.0, 0.0])
            self.assertEqual(cloud(P=(bad, 0.0, 0.0)), [0.0, 0.0, 0.0, 0.0])
            self.assertEqual(cloud(uv=(bad, 0.0)), [0.0, 0.0, 0.0, 0.0])
        # No slope and no c70.x: the normal has no length; the far point at the pixel: no direction.
        numbers(self, cloud(c70x=0.0)[:3], 0.0, 31.875)
        numbers(self, cloud(c77=(100.0, 200.0, 10.0))[:3], 0.0, 31.875)
        self.assertEqual(cloud(c20w=1e6, c77=(100.0, 200.0, 12.0))[:3], [31.875, 31.875, 31.875])
        self.assertEqual(cloud(c20w=-4.0, c77=(100.0, 200.0, 12.0))[:3], [0.0, 0.0, 0.0])


OCCLUDER_REGS = ((5.0, 6.0), (0.25, 0.5), (100.0, 200.0, 50.0))      # c31, c46, c47 (the eye here)


class Occluders(unittest.TestCase):
    def test_the_cloud_occluders_alpha(self):
        # tex.a * c77.x * fade, at the clouds' own coordinate and with their fade about c47.
        calls = []
        tex = cloud_texture(calls=calls)
        got = lf.post_cloud_occluder(tex, (0.1, 0.25), (100.0, 200.0, 500.0), *OCCLUDER_REGS, 0.5)
        self.assertAlmostEqual(got, 0.3, delta=1e-15)
        self.assertEqual(calls, [(0.75, 2.0, 0, 0)])
        half = lf.post_cloud_occluder(tex, (0.1, 0.25), (1600.0, 200.0, 500.0), *OCCLUDER_REGS, 0.5)
        self.assertAlmostEqual(half, 0.15, delta=1e-12)
        self.assertEqual(lf.post_cloud_occluder(tex, (0.1, 0.25), (2700.0, 200.0, 500.0), *OCCLUDER_REGS, 0.5), 0.0)
        self.assertEqual(lf.POST_CLOUD_OCCLUDER_C255, (1000.0, 0.0, 0.001, 1.0))
        self.assertEqual(lf.post_cloud_occluder(tex, (0.1, 0.25), (100.0, 200.0, 500.0), *OCCLUDER_REGS, 9.0), 1.0)

    def test_the_cloud_occluder_with_values_that_are_no_numbers(self):
        tex = cloud_texture()
        for bad in (NAN, INF, -INF):
            self.assertEqual(lf.post_cloud_occluder(tex, (0.1, 0.25), (bad, 200.0, 500.0), *OCCLUDER_REGS, 0.5), 0.0)
            self.assertEqual(lf.post_cloud_occluder(tex, (0.1, 0.25), (100.0, 200.0, 500.0), *OCCLUDER_REGS, bad), 0.0)
            numbers(self, [lf.post_cloud_occluder(cloud_texture(lambda dx, dy: bad), (0.1, 0.25), (100.0, 200.0, 500.0),
                                                 *OCCLUDER_REGS, 0.5)])

    def test_the_tree_occluder(self):
        self.assertEqual(lf.post_tree_occluder_uv((0.25, 0.5), (2.0, 4.0, 0.125, 0.0625)), (0.625, 2.0625))
        near(self, lf.post_tree_occluder(TEX, 0.5), [0.5, 0.25, 0.125, 0.4])


VP = ((1.0, 2.0, 3.0, 4.0), (0.5, -1.0, 0.25, 2.0), (0.0, 1.0, 0.0, -1.0), (2.0, 0.0, 1.0, 0.5))


class Positions(unittest.TestCase):
    def test_a_quads_corner(self):
        # (c0 . p, c1 . p, c2 . p, c3 . p) of p = (x, y, z, 1)
        near(self, lf.post_quad_position((1.0, 10.0, 100.0), *VP), [325.0, 17.5, 9.0, 102.5])

    def test_the_cubes_two_faces(self):
        # The face z = 1 lies about the eye (c9) at the eye's height, 2 * 2850 wide; the others lie about the
        # origin at the height c119.x.
        c9, c119x = (10.0, 20.0, 30.0, 1.0), 5.0
        q = lambda v: lf.post_cube_position(v, *VP, c9, c119x)     # noqa: E731
        near(self, q((1.0, 0.0, 1.0)), lf.post_quad_position((2860.0, -2830.0, 30.0), *VP))
        near(self, q((0.0, 1.0, 1.0)), lf.post_quad_position((-2840.0, 2870.0, 30.0), *VP))
        near(self, q((1.0, 0.0, 0.0)), lf.post_quad_position((2850.0, -2850.0, 5.0), *VP))
        near(self, q((0.0, 1.0, 0.0)), lf.post_quad_position((-2850.0, 2850.0, 5.0), *VP))
        self.assertEqual(lf.POST_CUBE_C254, (2.0, 0.0, 2850.0, -1.0))
        self.assertEqual(lf.post_cube_point((1.0, 0.0, 1.0), c9, c119x), (2860.0, -2830.0, 30.0))
        self.assertEqual(lf.post_cube_point((1.0, 0.0, 0.5), c9, c119x), (2850.0, -2850.0, 5.0))

    def test_the_six_indices(self):
        self.assertEqual(lf.POST_QUAD_INDICES, (0, 1, 2, 1, 3, 2))


class QuadList(unittest.TestCase):
    def test_the_eight_kinds_in_the_records_order(self):
        self.assertEqual([s.kind for s in lf.POST_QUAD_SHADERS],
                         ["moon", "cloud", "glare", "beam-source", "flare", "moon-glow", "sun-disc", "cloud-occluder"])
        self.assertEqual(len({s.hash for s in lf.POST_QUAD_SHADERS}), 8)

    def test_every_entry_fits_the_record(self):
        furniture = {s.hash: s for s in lf.POST_FURNITURE_SHADERS}
        for s in lf.POST_QUAD_SHADERS:
            with self.subTest(kind=s.kind):
                self.assertEqual(furniture[s.hash].stage, "frag")
                self.assertEqual(furniture[s.hash].role, s.kind)
                self.assertEqual(furniture[s.vs].stage, "vert")
                self.assertLessEqual(len(s.registers), 12)
                self.assertEqual(list(s.registers), sorted(set(s.registers)))
                self.assertTrue(all(0 <= r < 252 for r in s.registers))        # no literal: those are the shader's own
                self.assertEqual(s.slots, (13,))
                self.assertIn(s.where, ("scene", "layer-depth", "layer1", "layer2"))
                self.assertIn(s.blend, ("alpha", "add", "none", "depth"))
                self.assertIsInstance(s.test, bool)

    def test_where_and_how_each_is_drawn(self):
        by = {s.kind: s for s in lf.POST_QUAD_SHADERS}
        self.assertEqual([(k, by[k].where, by[k].blend, by[k].test) for k in by], [
            ("moon", "scene", "alpha", True), ("cloud", "scene", "alpha", True), ("glare", "scene", "add", False),
            ("beam-source", "layer1", "none", False), ("flare", "layer2", "add", False),
            ("moon-glow", "layer2", "alpha", True), ("sun-disc", "layer2", "add", True),
            ("cloud-occluder", "layer-depth", "depth", True)])
        self.assertEqual(by["cloud"].registers, (20, 31, 46, 47, 51, 69, 70, 71, 77, 78, 79))
        self.assertEqual(by["moon-glow"].registers, ())
        self.assertEqual(by["cloud"].vs, CLOUD_VS)
        self.assertEqual(by["sun-disc"].vs, QUAD_VS)

    def test_the_fourteen_furniture_shaders(self):
        self.assertEqual(len(lf.POST_FURNITURE_SHADERS), 14)
        self.assertEqual(len({s.hash for s in lf.POST_FURNITURE_SHADERS}), 14)
        self.assertEqual(sum(1 for s in lf.POST_FURNITURE_SHADERS if s.stage == "vert"), 3)
        self.assertFalse({s.hash for s in lf.POST_FURNITURE_SHADERS} & {s.hash for s in lf.POST_SHADERS})
        self.assertEqual(set(pc.CHECKS), {s.hash for s in lf.POST_SHADERS} | {s.hash for s in lf.POST_FURNITURE_SHADERS})


GLARE_TEXT = """\
/*    0.0 */       exec
/*    2   */          tfetch2D r0.xyz_, r0.xy, tf13
/*    0.1 */       alloc colors
/*    1.0 */       exece
/*    3   */          mul oC0.xyz1, r0.xyzz, c31.xyzz
/*    1.1 */       cnop
"""
QUAD_VS_TEXT = """\
/*    0.0 */       exec
/*    3   */          vfetch_full r1.xyz1, r0.x, vf0, DataFormat=FMT_32_32_32_FLOAT, Stride=5, Signed=true, NumFormat=integer, PrefetchCount=5
/*    4   */          vfetch_mini r0.xy__, Offset=3, DataFormat=FMT_32_32_FLOAT, Signed=true, NumFormat=integer
/*    0.1 */       alloc position
/*    1.0 */       exec
/*    5   */          dp4 oPos.x___, c0.zxyw, r1.zxyw
/*    6   */          dp4 oPos._y__, c1.zxyw, r1.zxyw
/*    7   */          dp4 oPos.__z_, c2.zxyw, r1.zxyw
/*    8   */          dp4 oPos.___w, c3.zxyw, r1.zxyw
/*    1.1 */       alloc interpolators
/*    2.0 */       exece
/*    9   */          max o0.xy__, r0.xyyy, r0.xyyy
/*    2.1 */       cnop
"""


class FurnitureCheckOnSyntheticMicrocode(unittest.TestCase):
    def test_a_pixel_shader_that_is_the_formula_agrees(self):
        res = pc.check_text(GLARE, GLARE_TEXT, trials=50)
        self.assertTrue(res.ok, res.line())
        self.assertEqual((res.agree, res.values), (200, 200))

    def test_another_slot_another_channel_or_another_register_fails(self):
        for old, new in (("tf13", "tf12"), ("r0.xyzz, c31", "r0.zyxx, c31"), ("c31.xyzz", "c31.zyxx"), ("c31", "c30"),
                         ("oC0.xyz1", "oC0.xyz0"), ("tfetch2D r0.xyz_, r0.xy", "tfetch2D r0.xyz_, r0.yx")):
            with self.subTest(old=old, new=new):
                self.assertIn(old, GLARE_TEXT)
                self.assertFalse(pc.check_text(GLARE, GLARE_TEXT.replace(old, new), trials=50).ok)

    def test_a_vertex_shader_that_is_the_formula_agrees(self):
        res = pc.check_text(QUAD_VS, QUAD_VS_TEXT, trials=50)
        self.assertTrue(res.ok, res.line())
        self.assertEqual((res.agree, res.values), (300, 300))       # oPos and the two coordinates

    def test_another_row_another_offset_or_no_translation_fails(self):
        for old, new in (("oPos._y__, c1", "oPos._y__, c2"), ("Offset=3", "Offset=2"), ("r1.xyz1", "r1.xyz0"),
                         ("o0.xy__, r0.xyyy", "o0.xy__, r0.yxxx"), ("c0.zxyw, r1.zxyw", "c0.zxyw, r1.xyzw")):
            with self.subTest(old=old, new=new):
                self.assertIn(old, QUAD_VS_TEXT)
                self.assertFalse(pc.check_text(QUAD_VS, QUAD_VS_TEXT.replace(old, new), trials=50).ok)


def cloud_form(**flag):
    """The clouds' formula written out a second time, with one thing wrong for each flag. Without a flag it
    is 6197's arithmetic and must pass the check as light_formulas.post_cloud does."""
    def form(tex, uv, P, c20w, c31, c46, c47, c51x, c69x, c70x, c71x, c77, c78, c79, c254=lf.POST_CLOUD_C254,
             c255x=lf.POST_CLOUD_C255X, clamp=True):
        u, v = uv[0] * c31[0] + c46[0], uv[1] * c31[1] + c46[1]
        if flag.get("uv_exchanged"):
            u, v = uv[0] * c46[0] + c31[0], uv[1] * c46[1] + c31[1]
        t = tex(u, v, 0, 0)
        a = t[3]
        d2 = (P[0] - c79[0]) ** 2 + (P[1] - c79[1]) ** 2 + c254[0]
        if flag.get("distance_in_space"):
            d2 += (P[2] - c79[2]) ** 2
        if flag.get("no_abs_distance"):
            d = math.sqrt(d2) if d2 > 0 else 0.0
        else:
            d = math.sqrt(abs(d2))
        ramp = c254[1] * (d - c254[2])
        fade = c254[3] - (ramp if flag.get("fade_unclamped") else lf.sat(ramp))
        if flag.get("no_fade"):
            fade = 1.0
        alpha = a * (1.0 if flag.get("no_density_scale") else abs(c69x) if flag.get("abs_density_scale") else c69x) * fade
        above = alpha >= c254[1] if flag.get("at_the_threshold") else alpha > c254[1]
        if flag.get("no_branch"):
            above = True
        if not above:
            k = 1.0 if -abs(P[0]) > c254[0] else 0.0
            if flag.get("dark_is_zero"):
                k = 0.0
            if flag.get("dark_component_y"):
                k = 1.0 if -abs(P[1]) > c254[0] else 0.0
            if flag.get("dark_not_strict"):
                k = 1.0 if -abs(P[0]) >= c254[0] else 0.0
            return [k * c20w] * 3 + [alpha]
        step = 2 if flag.get("two_texels") else 1
        gx = tex(u, v, step, 0)[3] - tex(u, v, -step, 0)[3]
        gy = tex(u, v, 0, step)[3] - tex(u, v, 0, -step)[3]
        if flag.get("gradient_reversed"):
            gx, gy = -gx, -gy
        if flag.get("gradient_exchanged"):
            gx, gy = gy, gx
        nz = 1.0 if flag.get("normal_z_one") else abs(c70x) if flag.get("abs_height") else c70x
        n2 = nz * nz + (gx * gx + gy * gy + c254[0])
        if flag.get("no_abs_normal"):
            inv = 1.0 / math.sqrt(n2) if n2 > 0 else 0.0
        else:
            inv = 1.0 / math.sqrt(abs(n2))
        N = (gx * inv, gy * inv, nz * inv)
        D = [P[i] - c77[i] for i in range(3)]
        if flag.get("far_point_is_the_eye"):
            D = [P[i] - c79[i] for i in range(3)]
        k = 1.0 / math.sqrt(sum(x * x for x in D))
        Dn = [x * k for x in D]
        if flag.get("direction_not_normalised"):
            Dn = D
        a1 = lf.sat(-sum(N[i] * c78[i] for i in range(3)))
        if flag.get("light_unclamped"):
            a1 = -sum(N[i] * c78[i] for i in range(3))
        dn = sum(Dn[i] * N[i] for i in range(3))
        b1, b2 = lf.sat(-dn), lf.sat(dn)
        if flag.get("sides_exchanged"):
            b1, b2 = b2, b1
        base = c254[3] - a1
        if flag.get("no_abs_power"):
            pw = base ** c255x if base > 0 else 0.0
        else:
            pw = abs(base) ** (16.0 if flag.get("power_16") else c255x) if base != 0 else 0.0
        if flag.get("no_power"):
            pw = 0.0
        through = b1 if flag.get("back_not_squared") else b1 * b1
        l = through * (1.0 if flag.get("no_density_in_light") else c254[3] - a) + b2 + b2 * pw
        if flag.get("ambient_first"):
            rgb = [(l + c71x) * c47[i] * c51x * t[i] * c20w for i in range(3)]
        elif flag.get("colour_reversed"):
            rgb = [(l * c47[2 - i] + c71x) * c51x * t[i] * c20w for i in range(3)]
        elif flag.get("ambient_outside"):
            rgb = [(l * c47[i] * c51x + c71x) * t[i] * c20w for i in range(3)]
        else:
            rgb = [(l * c47[i] + c71x) * c51x * t[i] * c20w for i in range(3)]
        if flag.get("no_scale"):
            rgb = [x / c20w for x in rgb]
        if flag.get("no_texture_colour"):
            rgb = [x / t[i] for i, x in enumerate(rgb)]
        return rgb + [a if flag.get("alpha_is_the_textures") else alpha]
    form.__name__ = "cloud_" + ("_".join(sorted(flag)) or "as_it_is")
    return form


CLOUD_FLAGS = ("uv_exchanged", "distance_in_space", "fade_unclamped", "no_fade", "no_density_scale", "at_the_threshold",
               "no_branch", "two_texels", "gradient_reversed", "gradient_exchanged", "normal_z_one", "far_point_is_the_eye",
               "direction_not_normalised", "light_unclamped", "sides_exchanged", "power_16", "no_power", "back_not_squared",
               "no_density_in_light", "ambient_first", "colour_reversed", "ambient_outside", "no_scale", "no_texture_colour",
               "alpha_is_the_textures", "abs_density_scale", "abs_height")
# Wrong only where a literal is not the game's: caught by the trials that draw the literals. (The dark
# branch's colour is 0 with the literal c254.x = 0 whatever is compared with it.)
CLOUD_LITERAL_FLAGS = ("no_abs_distance", "no_abs_normal", "no_abs_power", "dark_is_zero", "dark_component_y",
                       "dark_not_strict")


def _wrong_furniture():
    """(shader, name, formula): for every furniture check, forms that differ from the shader in one thing."""
    P, G, B, F = lf.post_moon, lf.post_glare, lf.post_beam_source, lf.post_flare
    W, S = lf.post_moon_glow, lf.post_sun_disc
    O, OC = lf.post_cloud_occluder, lf.POST_CLOUD_OCCLUDER_C255
    out = [
        (MOON, "alpha without the texture's", lambda t, c31, c20w, clamp=True: P(t, c31, c20w, clamp=clamp)[:3] + [c31[3]]),
        (MOON, "alpha without c31.w", lambda t, c31, c20w, clamp=True: P(t, c31, c20w, clamp=clamp)[:3] + [t[3]]),
        (MOON, "colour without the texture's alpha",
         lambda t, c31, c20w, clamp=True: [t[i] * c31[i] * c20w for i in range(3)] + [c31[3] * t[3]]),
        (MOON, "no output scale", lambda t, c31, c20w, clamp=True: P(t, c31, 1.0, clamp=clamp)),
        (MOON, "the register's colour reversed",
         lambda t, c31, c20w, clamp=True: P(t, (c31[2], c31[1], c31[0], c31[3]), c20w, clamp=clamp)),
        (MOON, "the texture's colour reversed",
         lambda t, c31, c20w, clamp=True: P((t[2], t[1], t[0], t[3]), c31, c20w, clamp=clamp)),
        (MOON, "the texture's alpha not below 0 in the colour",
         lambda t, c31, c20w, clamp=True: [t[i] * c31[i] * max(t[3], 0.0) * c20w for i in range(3)] + [c31[3] * t[3]]),
        (MOON, "the texture's colour clamped", lambda t, c31, c20w, clamp=True: P([lf.sat(v) for v in t[:3]] + [t[3]], c31, c20w, clamp=clamp)),
        (GLARE, "the texture's colour clamped", lambda t, c31, clamp=True: G([lf.sat(v) for v in t], c31, clamp=clamp)),
        (GLARE, "alpha is the texture's", lambda t, c31, clamp=True: G(t, c31, clamp=clamp)[:3] + [t[3]]),
        (GLARE, "times the texture's alpha", lambda t, c31, clamp=True: [x * t[3] for x in G(t, c31, clamp=clamp)[:3]] + [1.0]),
        (GLARE, "the register's colour reversed", lambda t, c31, clamp=True: G(t, c31[2::-1], clamp=clamp)),
        (GLARE, "the beam source's sixth", lambda t, c31, clamp=True: B(t, c31, clamp=clamp)),
        (BEAM, "no sixth", lambda t, c31, c255x=1.0, clamp=True: G(t, c31, clamp=clamp)),
        (BEAM, "alpha is the texture's", lambda t, c31, c255x=1.0 / 6.0, clamp=True: B(t, c31, c255x, clamp=clamp)[:3] + [t[3]]),
        (BEAM, "times the texture's alpha",
         lambda t, c31, c255x=1.0 / 6.0, clamp=True: [x * t[3] for x in B(t, c31, c255x, clamp=clamp)[:3]] + [1.0]),
        (BEAM, "the texture's colour reversed",
         lambda t, c31, c255x=1.0 / 6.0, clamp=True: B((t[2], t[1], t[0], t[3]), c31, c255x, clamp=clamp)),
        (BEAM, "the texture's colour not below 0",
         lambda t, c31, c255x=1.0 / 6.0, clamp=True: B([max(v, 0.0) for v in t], c31, c255x, clamp=clamp)),
        (FLARE, "the texture's alpha clamped", lambda t, c31, clamp=True: F((0, 0, 0, lf.sat(t[3])), c31, clamp=clamp)),
        (FLARE, "the texture's red for its alpha", lambda t, c31, clamp=True: F((0, 0, 0, t[0]), c31, clamp=clamp)),
        (FLARE, "the texture's colour", lambda t, c31, clamp=True: G(t, c31, clamp=clamp)),
        (FLARE, "alpha is the texture's", lambda t, c31, clamp=True: F(t, c31, clamp=clamp)[:3] + [t[3]]),
        (FLARE, "the register's colour reversed", lambda t, c31, clamp=True: F(t, c31[2::-1], clamp=clamp)),
        (GLOW, "colour without the texture's alpha", lambda t, c255x=0.2, clamp=True: [t[i] * c255x for i in range(3)] + [t[3]]),
        (GLOW, "alpha 1", lambda t, c255x=0.2, clamp=True: W(t, c255x, clamp=clamp)[:3] + [1.0]),
        (GLOW, "no fifth", lambda t, c255x=0.2, clamp=True: W(t, 1.0, clamp=clamp)),
        (GLOW, "the texture's colour reversed", lambda t, c255x=0.2, clamp=True: W((t[2], t[1], t[0], t[3]), c255x, clamp=clamp)),
        (GLOW, "alpha clamped", lambda t, c255x=0.2, clamp=True: W(t, c255x, clamp=clamp)[:3] + [lf.sat(t[3])]),
        (GLOW, "the texture's alpha not below 0 in the colour",
         lambda t, c255x=0.2, clamp=True: [t[i] * c255x * max(t[3], 0.0) for i in range(3)] + [t[3]]),
        (DISC, "alpha clamped", lambda t, c31, c20w, clamp=True: S(t, c31, c20w, clamp=clamp)[:3] + [lf.sat(t[3])]),
        (DISC, "alpha's absolute value", lambda t, c31, c20w, clamp=True: S(t, c31, c20w, clamp=clamp)[:3] + [abs(t[3])]),
        (DISC, "the moon's alpha", lambda t, c31, c20w, clamp=True: P(t, c31, c20w, clamp=clamp)),
        (DISC, "no output scale", lambda t, c31, c20w, clamp=True: S(t, c31, 1.0, clamp=clamp)),
        (DISC, "colour without the texture's alpha",
         lambda t, c31, c20w, clamp=True: [t[i] * c31[i] * c20w for i in range(3)] + [t[3]]),
        (DISC, "alpha 1", lambda t, c31, c20w, clamp=True: S(t, c31, c20w, clamp=clamp)[:3] + [1.0]),
        (LAYER_DEPTH, "depth not clamped", lambda t: ([0.0] * 4, t[0])),
        (LAYER_DEPTH, "depth from green", lambda t: lf.post_layer_depth((t[1], t[0], t[2], t[3]))),
        (LAYER_DEPTH, "the depth as colour", lambda t: ([lf.sat(t[0]), 0.0, 0.0, 0.0], lf.sat(t[0]))),
        (LAYER_DEPTH, "one minus the depth", lambda t: ([0.0] * 4, 1.0 - lf.sat(t[0]))),
        (PREVIOUS, "blue and red exchanged", lambda t: [t[2], t[1], t[0], t[3]]),
        (PREVIOUS, "alpha 1", lambda t: list(t[:3]) + [1.0]),
        (PREVIOUS, "clamped", lambda t: [lf.sat(v) for v in t]),
        (TREE_OCCLUDER, "the texture's colour clamped", lambda t, c15w, clamp=True: [lf.sat(v) for v in t[:3]] + [t[3] * c15w]),
        (TREE_OCCLUDER, "no c15.w", lambda t, c15w, clamp=True: list(t)),
        (TREE_OCCLUDER, "colour times c15.w", lambda t, c15w, clamp=True: [x * c15w for x in t]),
        (TREE_OCCLUDER, "no colour", lambda t, c15w, clamp=True: [0.0, 0.0, 0.0, t[3] * c15w]),
        (CLOUD_OCCLUDER, "no fade",
         lambda tex, uv, P, c31, c46, c47, c77x, c255=OC, clamp=True: tex(*lf.post_cloud_uv(uv, c31, c46), 0, 0)[3] * c77x),
        (CLOUD_OCCLUDER, "no c77.x",
         lambda tex, uv, P, c31, c46, c47, c77x, c255=OC, clamp=True: O(tex, uv, P, c31, c46, c47, 1.0, c255, clamp=clamp)),
        (CLOUD_OCCLUDER, "the distance in space",
         lambda tex, uv, P, c31, c46, c47, c77x, c255=OC, clamp=True:
            O(tex, uv, (P[0], math.hypot(P[1] - c47[1], P[2] - c47[2]) + c47[1], P[2]), c31, c46, c47, c77x, c255, clamp=clamp)),
        (CLOUD_OCCLUDER, "tiling and scroll exchanged",
         lambda tex, uv, P, c31, c46, c47, c77x, c255=OC, clamp=True: O(tex, uv, P, c46, c31, c47, c77x, c255, clamp=clamp)),
        (CLOUD_OCCLUDER, "the fade not clamped",
         lambda tex, uv, P, c31, c46, c47, c77x, c255=OC, clamp=True:
            tex(*lf.post_cloud_uv(uv, c31, c46), 0, 0)[3] * c77x
            * (c255[3] - c255[2] * (math.sqrt(abs((P[0] - c47[0]) ** 2 + (P[1] - c47[1]) ** 2 + c255[1])) - c255[0]))),
        (CLOUD_OCCLUDER, "the texture's red",
         lambda tex, uv, P, c31, c46, c47, c77x, c255=OC, clamp=True:
            O(lambda u, v, dx, dy: [tex(u, v, dx, dy)[0]] * 4, uv, P, c31, c46, c47, c77x, c255, clamp=clamp)),
        (CLOUD_OCCLUDER, "one texel off",
         lambda tex, uv, P, c31, c46, c47, c77x, c255=OC, clamp=True:
            O(lambda u, v, dx, dy: tex(u, v, 1, 0), uv, P, c31, c46, c47, c77x, c255, clamp=clamp)),
        (QUAD_VS, "the matrix transposed", lambda p, c0, c1, c2, c3: lf.post_quad_position(p, *zip(c0, c1, c2, c3))),
        (QUAD_VS, "no translation", lambda p, c0, c1, c2, c3: [sum(c[i] * p[i] for i in range(3)) for c in (c0, c1, c2, c3)]),
        (QUAD_VS, "rows exchanged", lambda p, c0, c1, c2, c3: lf.post_quad_position(p, c0, c1, c3, c2)),
        (CLOUD_VS, "the matrix transposed", lambda p, c0, c1, c2, c3: lf.post_quad_position(p, *zip(c0, c1, c2, c3))),
        (CLOUD_VS, "rows exchanged", lambda p, c0, c1, c2, c3: lf.post_quad_position(p, c1, c0, c2, c3)),
    ]
    cube, k0 = lf.post_cube_position, lf.POST_CUBE_C254

    def cube_with(point):
        """The cube's position with another rule for the point it transforms."""
        def form(v, c0, c1, c2, c3, c9, c119x, c254=k0, c255x=1.0):
            q = lf.post_quad_position(point(v, c9, c119x, c254, c255x), c0, c1, c2, c3)
            return [a + b for a, b in zip(q, lf.post_cube_bias(v, c254, c255x))]
        return form

    def xy(v, k):
        return [(v[i] * k[0] + k[3]) * k[2] for i in range(2)]

    def real(v, c9, h, k, z):
        return lf.post_cube_point(v, c9, h, k, z)

    out += [
        (CUBE_VS, "the faces exchanged", cube_with(lambda v, c9, h, k, z: real((v[0], v[1], v[2] + 1.0 if v[2] == z else z), c9, h, k, z))),
        (CUBE_VS, "the eye under every face",
         cube_with(lambda v, c9, h, k, z: (xy(v, k)[0] + c9[0], xy(v, k)[1] + c9[1], c9[2] if v[2] == z else h))),
        (CUBE_VS, "the eye under no face", cube_with(lambda v, c9, h, k, z: (*xy(v, k), c9[2] if v[2] == z else h))),
        (CUBE_VS, "the heights exchanged", cube_with(lambda v, c9, h, k, z: (*real(v, c9, h, k, z)[:2], h if v[2] == z else c9[2]))),
        (CUBE_VS, "one height for all", cube_with(lambda v, c9, h, k, z: (*real(v, c9, h, k, z)[:2], h))),
        (CUBE_VS, "not centred",
         cube_with(lambda v, c9, h, k, z: (real(v, c9, h, k, z)[0] - k[3] * k[2], real(v, c9, h, k, z)[1] - k[3] * k[2], real(v, c9, h, k, z)[2]))),
        (CUBE_VS, "the matrix transposed",
         lambda v, c0, c1, c2, c3, c9, c119x, c254=k0, c255x=1.0: cube(v, *zip(c0, c1, c2, c3), c9, c119x, c254, c255x)),
        (CUBE_VS, "the vertex's y and z exchanged",
         lambda v, c0, c1, c2, c3, c9, c119x, c254=k0, c255x=1.0: cube((v[0], v[2], v[1]), c0, c1, c2, c3, c9, c119x, c254, c255x)),
    ]
    out += [(CLOUD, flag, cloud_form(**{flag: True})) for flag in CLOUD_FLAGS]
    return out


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class FurnitureRealDumps(unittest.TestCase):
    TRIALS = 500        # spec criterion 16's count: at least 2,000 values a shader

    def test_every_furniture_shader_agrees(self):
        for s in lf.POST_FURNITURE_SHADERS:
            with self.subTest(shader=s.hash, role=s.role):
                res = pc.check(s.hash, trials=self.TRIALS, dumps=DUMPS)
                self.assertTrue(res.ok, f"{s.hash}: {res.line()} {res.first_bad}")
                self.assertGreaterEqual(res.values, 2000)
                self.assertEqual(res.agree, res.values)
                self.assertLess(res.worst, 1e-11)

    def test_with_other_literals_too(self):
        # The shaders' own literals drawn at random: what a literal of 0 or 1 hides (an absolute value, a
        # term that is added) is then held as well.
        self.assertEqual(set(pc.FURNITURE_LITERALS), {BEAM, GLOW, CLOUD, CLOUD_OCCLUDER, CUBE_VS})
        for shader in pc.FURNITURE_LITERALS:
            with self.subTest(shader=shader):
                res = pc.check(shader, trials=self.TRIALS, dumps=DUMPS, literals="random")
                self.assertTrue(res.ok, f"{shader}: {res.line()} {res.first_bad}")
                self.assertGreaterEqual(res.values, 2000)

    def test_the_trials_reach_both_sides_of_every_branch(self):
        res = pc.check(CLOUD, trials=self.TRIALS, dumps=DUMPS)
        self.assertGreaterEqual(res.reached.get("thin", 0), 40)             # alpha at or below 0.001: no colour
        self.assertGreaterEqual(res.reached.get("at the threshold", 0), 20)
        self.assertGreaterEqual(res.reached.get("lit", 0), 200)
        self.assertGreaterEqual(res.reached.get("light clamped at 1", 0), 20)
        self.assertGreaterEqual(res.reached.get("fade between", 0), 50)
        for word in ("c69.x below 0", "c70.x below 0"):
            self.assertGreaterEqual(res.reached.get(word, 0), 40, word)
        # With drawn literals: the thin branch with c254.x at -|P.x| and between two components' values.
        res = pc.check(CLOUD, trials=self.TRIALS, dumps=DUMPS, literals="random")
        for word in ("thin, -|P.x| at c254.x", "thin, c254.x between -|P.x| and -|P.y|"):
            self.assertGreaterEqual(res.reached.get(word, 0), 40, word)
        res = pc.check(CLOUD_OCCLUDER, trials=self.TRIALS, dumps=DUMPS)
        for word in ("fade 1", "fade between", "fade 0"):
            self.assertGreaterEqual(res.reached.get(word, 0), 50, word)
        res = pc.check(CUBE_VS, trials=self.TRIALS, dumps=DUMPS)
        self.assertGreaterEqual(res.reached.get("the eye's face", 0), 100)
        self.assertGreaterEqual(res.reached.get("the other faces", 0), 100)
        res = pc.check(LAYER_DEPTH, trials=self.TRIALS, dumps=DUMPS)
        for word in ("below 0", "inside", "above 1"):
            self.assertGreaterEqual(res.reached.get(word, 0), 50, word)

    def test_wrong_formulas_fail(self):
        for shader, name, formula in _wrong_furniture():
            with self.subTest(shader=shader, wrong=name):
                self.assertFalse(pc.check(shader, trials=120, dumps=DUMPS, formula=formula).ok)

    def test_every_furniture_check_has_a_wrong_form_it_catches(self):
        self.assertEqual({shader for shader, _, _ in _wrong_furniture()}, {s.hash for s in lf.POST_FURNITURE_SHADERS})

    def test_formulas_wrong_only_beside_the_literals_fail_with_other_literals(self):
        for flag in CLOUD_LITERAL_FLAGS:
            with self.subTest(wrong=flag):
                self.assertTrue(pc.check(CLOUD, trials=120, dumps=DUMPS, formula=cloud_form(**{flag: True})).ok)
                self.assertFalse(pc.check(CLOUD, trials=120, dumps=DUMPS, formula=cloud_form(**{flag: True}), literals="random").ok)

    def test_the_second_writing_of_the_clouds_agrees(self):
        for literals in (None, "random"):
            res = pc.check(CLOUD, trials=200, dumps=DUMPS, formula=cloud_form(), literals=literals)
            self.assertTrue(res.ok, f"{res.line()} {res.first_bad}")

    def test_wrong_microcode_in_the_thin_branch_fails_with_other_literals(self):
        import ucode_eval as ue
        text = ue.load(DUMPS, CLOUD, "frag")
        for old, new in (("sgt r0.xyz_, -r_abs[0].xxxx, c254.xxxx", "sgt r0.xyz_, -r_abs[0].yyyy, c254.xxxx"),
                         ("sgt r0.xyz_, -r_abs[0].xxxx, c254.xxxx", "sge r0.xyz_, -r_abs[0].xxxx, c254.xxxx")):
            with self.subTest(new=new):
                self.assertIn(old, text)
                self.assertFalse(pc.check_text(CLOUD, text.replace(old, new), trials=200, literals="random").ok)

    def test_wrong_microcode_fails(self):
        import ucode_eval as ue
        cases = ((PREVIOUS, "frag", "OffsetX=0.5", "OffsetX=1.5"), (PREVIOUS, "frag", "OffsetY=0.5", "OffsetY=0"),
                 (CLOUD, "frag", "OffsetX=-1", "OffsetX=-2"), (CLOUD, "frag", "OffsetY=1", "OffsetY=2"),
                 (CLOUD, "frag", "c79.yxxx", "c77.yxxx"),
                 (DISC, "frag", "maxs oC0.___w, r0.ww", "maxs_sat oC0.___w, r0.ww"),
                 (MOON, "frag", "maxs r0.x___, r0.xx", "maxs_sat r0.x___, r0.xx"),
                 (CLOUD, "frag", "dp3_sat r0.x___, r1.yzww, -c78.zxyy", "dp3 r0.x___, r1.yzww, -c78.zxyy"),
                 (CLOUD_OCCLUDER, "frag", "mulsc_sat", "mulsc"), (TREE_OCCLUDER, "frag", "c8.zwww", "c8.xyyy"),
                 (LAYER_DEPTH, "frag", "maxs_sat", "maxs"), (CUBE_VS, "vert", "c9.xyyy", "c9.yxxx"),
                 (CLOUD_VS, "vert", "max o0.xyz_, r1.xyzz", "max o0.xyz_, r1.zyxx"))
        for shader, stage, old, new in cases:
            with self.subTest(shader=shader, old=old):
                text = ue.load(DUMPS, shader, stage)
                self.assertIn(old, text)
                self.assertFalse(pc.check_text(shader, text.replace(old, new), trials=120).ok)

    def test_the_quad_lists_registers_are_the_ones_the_microcode_reads(self):
        import ucode_eval as ue
        for s in lf.POST_QUAD_SHADERS:
            with self.subTest(kind=s.kind):
                self.assertEqual(pc.registers_read(ue.load(DUMPS, s.hash, "frag")), (set(s.registers), set(s.slots)))
                self.assertEqual(pc.registers_read(ue.load(DUMPS, s.vs, "vert"))[0], {0, 1, 2, 3})

    def test_the_command_line_checks_the_furniture(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = pc.main(["--trials", "5", "--dumps", str(DUMPS)])
        text = out.getvalue()
        self.assertEqual(code, 0, text)
        self.assertIn("14 of 14 furniture shaders agree", text)
        self.assertIn("vs D1CC6DC28244C55A", text)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = pc.main(["--shader", CLOUD, "--trials", "5", "--dumps", str(DUMPS), "--literals", "random"])
        self.assertEqual(code, 0, out.getvalue())
        self.assertIn("1 of 1 furniture shaders agree", out.getvalue())


if __name__ == "__main__":
    unittest.main()
