import contextlib
import io
import math
import random
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import fog_check as fc  # noqa: E402
import light_check as lc  # noqa: E402
import light_formulas as lf  # noqa: E402
import ucode_eval as ue  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"
NAN, INF = float("nan"), float("inf")


def near(test, got, want, tol=1e-12):
    test.assertEqual(len(got), len(want))
    for g, w in zip(got, want):
        test.assertAlmostEqual(g, w, delta=tol * max(1.0, abs(w)))


def const(*value):
    """A table that reads the same everywhere, and the coordinates it was read at."""
    seen = []

    def fetch(*coords):
        seen.append(coords if len(coords) > 1 else coords[0])
        return list(value)

    fetch.seen = seen
    return fetch


# --- the addressing of a 1D table -------------------------------------------------------------------

class Table1D(unittest.TestCase):
    """tfetch1D with a linear, clamped sampler as the SDK's translator makes it: the normalised coordinate
    goes to the host's sampler as it is, so texel i has its centre at (i + 0.5) / N."""

    def setUp(self):
        self.texels = [[float(i), 10.0 * i, 100.0 - i] for i in range(8)]
        self.t = lf.table_1d(self.texels)

    def test_a_texel_centre_reads_that_texel_alone(self):
        for i in range(8):
            near(self, self.t((i + 0.5) / 8), self.texels[i])

    def test_a_texel_boundary_reads_the_mean_of_its_two_texels(self):
        for i in range(7):
            near(self, self.t((i + 1) / 8), [(a + b) / 2 for a, b in zip(self.texels[i], self.texels[i + 1])])

    def test_between_two_centres_it_is_linear(self):
        # 0.25 of the way from the centre of texel 2 to the centre of texel 3.
        near(self, self.t((2.5 + 0.25) / 8), [2.25, 22.5, 97.75])
        near(self, self.t((2.5 + 0.75) / 8), [2.75, 27.5, 97.25])

    def test_the_first_and_last_half_texel_are_clamped(self):
        for u in (0.0, 0.03, 0.5 / 8, -0.2, -5.0):
            near(self, self.t(u), self.texels[0])
        for u in (1.0, 0.97, 7.5 / 8, 1.2, 40.0):
            near(self, self.t(u), self.texels[7])

    def test_a_coordinate_that_is_not_a_number_reads_an_end(self):
        near(self, self.t(NAN), self.texels[0])
        near(self, self.t(-INF), self.texels[0])
        near(self, self.t(INF), self.texels[7])

    def test_one_texel_reads_everywhere(self):
        t = lf.table_1d([[3.0, 4.0, 5.0]])
        for u in (-1.0, 0.0, 0.5, 1.0, 9.0):
            self.assertEqual(t(u), [3.0, 4.0, 5.0])

    def test_no_texels_is_an_error_of_the_caller(self):
        with self.assertRaises(ValueError):
            lf.table_1d([])

    def test_the_result_is_a_new_list(self):
        self.t(0.0)[0] = 99.0
        self.assertEqual(self.texels[0][0], 0.0)

    def test_the_transmittance_table_in_distance(self):
        # 256 texels and c67.w = 0.0005: texel i is centred (i + 0.5) * 7.8125 units past the fog start,
        # the table ends 2,000 units past it, and the first 3.9 units read texel 0 alone.
        texels = [[float(i)] * 3 for i in range(256)]
        t = lf.table_1d(texels)
        for i in (0, 1, 100, 255):
            near(self, t(0.0005 * (i + 0.5) * 7.8125), [float(i)] * 3)
        near(self, t(0.0005 * 3.9), [0.0] * 3)
        near(self, t(0.0005 * 5000.0), [255.0] * 3)


# --- the fog block ------------------------------------------------------------------------------------

EYE = [0.0, 0.0, 0.0]
P50 = [0.0, 30.0, 40.0]                     # 50 units from the eye, in the direction (0, 0.6, 0.8)
AXIS = [0.0, 0.6, 0.8, 0.0]
S = [0.2, 0.4, 0.8]
T = [0.5, 0.25, 0.0]


def fog(C=(1.0, 1.0, 1.0), P=P50, eye=EYE, c28=(1.0, 2.0, 0.0, 0.0), c66=AXIS, c67=(20.0, 1900.0, 0.5, 0.01),
        c68=(1.0, 0.0, 0.4, 0.0), tf4=None, tf5=None, **kw):
    return lf.fog(list(C), P, eye, c28, c66, c67, c68, tf4 or const(*S), tf5 or const(*T), **kw)


class Fog(unittest.TestCase):
    def test_the_whole_block_by_hand(self):
        # d = 50, dD = 30, blend sat(0.5 * 30) = 1 so F = S; G = C T + (1 - T) * 2 * S.
        near(self, fog(), [0.5 + 0.5 * 2 * 0.2, 0.25 + 0.75 * 2 * 0.4, 0.0 + 1.0 * 2 * 0.8])
        near(self, fog(), [0.7, 0.85, 1.6])

    def test_the_amount_blends_towards_the_fogged_colour(self):
        near(self, fog(c28=(0.5, 2.0)), [0.85, 0.925, 1.3])
        near(self, fog(c28=(0.0, 2.0)), [1.0, 1.0, 1.0])

    def test_the_table_coordinates(self):
        tf4, tf5 = const(*S), const(*T)
        fog(tf4=tf4, tf5=tf5)
        self.assertEqual(len(tf4.seen), 1)
        self.assertAlmostEqual(tf4.seen[0], 1.0, delta=1e-12)          # looking along the axis
        self.assertAlmostEqual(tf5.seen[0], 0.01 * 30.0, delta=1e-12)  # c67.w * (d - c67.x)
        tf4 = const(*S)
        fog(P=[0.0, -30.0, -40.0], tf4=tf4)                            # looking away from it: clamped to 0
        self.assertEqual(tf4.seen[0], 0.0)
        tf4 = const(*S)
        fog(P=[50.0, 0.0, 0.0], tf4=tf4)                               # at a right angle
        self.assertAlmostEqual(tf4.seen[0], 0.0, delta=1e-12)
        tf4 = const(*S)
        fog(P=[0.0, 50.0, 0.0], tf4=tf4)
        self.assertAlmostEqual(tf4.seen[0], 0.6, delta=1e-12)

    def test_fog_coords_names_the_three_coordinates(self):
        a, s, t = lf.fog_coords(P50, EYE, AXIS, (20.0, 1900.0, 0.01, 0.01))
        self.assertAlmostEqual(a, 1.0, delta=1e-12)
        self.assertAlmostEqual(s, 0.3, delta=1e-12)
        self.assertAlmostEqual(t, 0.3, delta=1e-12)
        self.assertIsNone(lf.fog_coords(P50, EYE, AXIS, (50.0, 1900.0, 1.0, 0.01)))
        self.assertIsNone(lf.fog_coords(P50, EYE, AXIS, (60.0, 1900.0, 1.0, 0.01)))

    def test_inside_the_start_the_colour_is_untouched(self):
        C = [0.123456789, 7.25, 1e-9]
        tf4, tf5 = const(*S), const(*T)
        self.assertEqual(fog(C=C, c67=(50.0001, 1900.0, 1.0, 0.01), tf4=tf4, tf5=tf5), C)
        self.assertEqual(fog(C=C, c67=(50.0, 1900.0, 1.0, 0.01), tf4=tf4, tf5=tf5), C)      # the predicate is `>`
        self.assertEqual(fog(C=C, P=EYE, tf4=tf4, tf5=tf5), C)                              # at the eye
        self.assertEqual(tf4.seen + tf5.seen, [])                                           # no table is read

    def test_a_transmittance_of_one_is_the_identity(self):
        near(self, fog(C=[0.3, 1.7, 0.01], tf5=const(1.0, 1.0, 1.0)), [0.3, 1.7, 0.01], tol=1e-15)

    def test_a_transmittance_of_zero_is_the_scaled_in_scatter(self):
        # c28.y * F, the pixel's own colour gone.
        near(self, fog(C=[9.0, 9.0, 9.0], tf5=const(0.0, 0.0, 0.0)), [2 * 0.2, 2 * 0.4, 2 * 0.8])
        near(self, fog(C=[9.0, 9.0, 9.0], c28=(1.0, 0.25), tf5=const(0.0, 0.0, 0.0)), [0.05, 0.1, 0.2])

    def test_the_in_scatter_starts_at_c68(self):
        # c67.z = 0.01: 30 units past the start the blend is 0.3; F = c68 + 0.3 * (S - c68).
        F = [1.0 + 0.3 * (0.2 - 1.0), 0.0 + 0.3 * 0.4, 0.4 + 0.3 * (0.8 - 0.4)]
        near(self, F, [0.76, 0.12, 0.52])
        near(self, fog(c67=(20.0, 1900.0, 0.01, 0.01), c28=(1.0, 1.0), tf5=const(0.0, 0.0, 0.0)), F)
        # Just past the start it is c68 itself.
        near(self, fog(c67=(49.999999, 1900.0, 0.01, 0.01), c28=(1.0, 1.0), tf5=const(0.0, 0.0, 0.0)),
             [1.0, 0.0, 0.4], tol=1e-7)

    def test_the_blended_form_scales_the_in_scatter_by_the_alpha(self):
        near(self, fog(scatter_scale=0.25), [0.5 + 0.25 * 0.2, 0.25 + 0.25 * 0.6, 0.25 * 1.6])
        near(self, fog(scatter_scale=1.0), fog())
        near(self, fog(scatter_scale=0.0), [0.5, 0.25, 0.0])

    def test_real_tables_are_read_with_the_1d_addressing(self):
        tf5 = lf.table_1d([[i / 255.0] * 3 for i in range(256)])
        tf4 = lf.table_1d([[i / 63.0, 0.0, 1.0] for i in range(64)])
        # s = 0.3: x = 0.3 * 256 - 0.5 = 76.3, so T = 76.3 / 255; a = 1: the last texel.
        k = 76.3 / 255.0
        near(self, fog(C=[0.0, 0.0, 0.0], c28=(1.0, 1.0), tf4=tf4, tf5=tf5), [(1 - k) * 1.0, 0.0, (1 - k) * 1.0])

    def test_a_coordinate_far_beyond_the_table_reads_its_last_texel(self):
        tf5 = lf.table_1d([[1.0 - i / 255.0] * 3 for i in range(256)])
        near(self, fog(P=[0.0, 3e6, 4e6], c28=(1.0, 1.0), tf5=tf5), S)        # T = 0 at the end: all in-scatter

    # Review focus 2. The rule (light_formulas.py, "Guards"): the function never raises on numbers; a fog
    # input that is not finite, a fetched texel included, leaves the pixel unfogged, as a frame without a
    # valid fog block is drawn; a colour channel that is not finite counts as 0.
    def test_an_input_that_is_not_finite_leaves_the_pixel_unfogged(self):
        C = [0.25, 0.5, 0.75]
        for bad in (NAN, INF, -INF):
            for name, value in (("P", [0.0, bad, 40.0]), ("eye", [bad, 0.0, 0.0]), ("c28", (bad, 2.0)),
                                ("c28", (1.0, bad)), ("c66", [0.0, bad, 0.8]), ("c67", (bad, 1900.0, 0.5, 0.01)),
                                ("c67", (20.0, 1900.0, bad, 0.01)), ("c67", (20.0, 1900.0, 0.5, bad)),
                                ("c68", (1.0, bad, 0.4)), ("tf4", const(0.2, bad, 0.8)), ("tf5", const(bad, 0.25, 0.0)),
                                ("scatter_scale", bad)):
                with self.subTest(name=name, bad=bad):
                    self.assertEqual(fog(C=C, **{name: value}), C)

    def test_c67_y_is_not_an_input(self):
        near(self, fog(c67=(20.0, NAN, 0.5, 0.01)), fog())

    def test_a_colour_that_is_not_finite_counts_as_zero(self):
        near(self, fog(C=[NAN, 1.0, INF]), [0.5 * 0.4, 0.85, 1.6])
        self.assertEqual(fog(C=[NAN, 1.0, -INF], P=EYE), [0.0, 1.0, 0.0])

    def test_a_zero_axis_reads_the_table_at_zero(self):
        tf4 = const(*S)
        near(self, fog(c66=[0.0, 0.0, 0.0, 0.0], tf4=tf4), [0.7, 0.85, 1.6])
        self.assertEqual(tf4.seen, [0.0])

    def test_a_negative_start_at_the_eye_is_finite(self):
        # d = 0 with the predicate true: the view direction has no direction, the angle reads 0.
        tf4 = const(*S)
        out = fog(P=EYE, c67=(-5.0, 1900.0, 0.5, 0.01), tf4=tf4)
        self.assertTrue(all(math.isfinite(x) for x in out))
        self.assertEqual(tf4.seen, [0.0])

    def test_a_result_that_overflows_leaves_the_pixel_unfogged(self):
        self.assertEqual(fog(C=[1.0, 1.0, 1.0], c28=(1e308, 1e308), tf4=const(1e308, 1e308, 1e308)), [1.0, 1.0, 1.0])

    def test_it_is_finite_and_silent_for_any_numbers(self):
        rng = random.Random(7)
        pool = [0.0, -0.0, 1.0, -1.0, 1e-300, 1e300, -1e300, NAN, INF, -INF, 20.0, 0.0005]

        def v(n):
            return [rng.choice(pool) if rng.random() < 0.5 else rng.uniform(-100, 100) for _ in range(n)]

        for _ in range(3000):
            out = lf.fog(v(3), v(3), v(3), v(4), v(4), v(4), v(4), const(*v(4)), const(*v(4)), scatter_scale=v(1)[0])
            self.assertEqual(len(out), 3)
            self.assertTrue(all(math.isfinite(x) for x in out), out)


class VertexFog(unittest.TestCase):
    """The trees' form: a factor and an added colour per vertex, applied by the pixel shader."""

    def test_far_vertex_by_hand(self):
        tf16, tf17 = const(*T), const(*S)
        factor, add = lf.vertex_fog(P50, EYE, 2.0, AXIS, (20.0, 1900.0, 0.01, 0.01), tf16, tf17)
        near(self, factor, T)
        near(self, add, [(1 - 0.5) * 0.2 * 2, (1 - 0.25) * 0.4 * 2, (1 - 0.0) * 0.8 * 2])
        self.assertAlmostEqual(tf16.seen[0], 0.3, delta=1e-12)
        self.assertAlmostEqual(tf17.seen[0][0], 1.0, delta=1e-12)      # the angle
        self.assertAlmostEqual(tf17.seen[0][1], 0.3, delta=1e-12)      # the near blend

    def test_near_vertex_is_one_and_zero(self):
        tf16, tf17 = const(*T), const(*S)
        self.assertEqual(lf.vertex_fog(P50, EYE, 2.0, AXIS, (50.0, 1900.0, 1.0, 0.01), tf16, tf17),
                         ([1.0, 1.0, 1.0], [0.0, 0.0, 0.0]))
        self.assertEqual(tf16.seen + tf17.seen, [])

    def test_an_input_that_is_not_finite_is_one_and_zero(self):
        for name, args in (("P", ([NAN, 0.0, 0.0], EYE, 2.0, AXIS, (20.0, 1900.0, 0.5, 0.01), const(*T), const(*S))),
                           ("c8", (P50, EYE, INF, AXIS, (20.0, 1900.0, 0.5, 0.01), const(*T), const(*S))),
                           ("tf16", (P50, EYE, 2.0, AXIS, (20.0, 1900.0, 0.5, 0.01), const(NAN, 0.0, 0.0), const(*S))),
                           ("tf17", (P50, EYE, 2.0, AXIS, (20.0, 1900.0, 0.5, 0.01), const(*T), const(0.0, INF, 0.0)))):
            with self.subTest(name=name):
                self.assertEqual(lf.vertex_fog(*args), ([1.0, 1.0, 1.0], [0.0, 0.0, 0.0]))

    def test_the_pixel_shader_applies_factor_and_add(self):
        near(self, lf.fog_apply([1.0, 2.0, 4.0], [0.5, 0.25, 0.0], [0.1, 0.2, 0.3]), [0.6, 0.7, 0.3])
        self.assertEqual(lf.fog_apply([1.0, NAN, 4.0], [1.0, 1.0, 1.0], [0.0, 0.0, INF]), [1.0, 0.0, 4.0])

    def test_it_is_the_pixel_form_if_the_tables_are_what_ruling_4_takes_them_for(self):
        # tf16 = tf5, tf17(a, t) = c68 + t * (tf4(a) - c68), vertex c8.x = pixel c28.y, and c28.x = 1 (the
        # vertex form has no amount): then factor and add applied to a colour are the pixel block.
        rng = random.Random(11)
        tf4 = lf.table_1d([[rng.uniform(0, 3) for _ in range(3)] for _ in range(64)])
        tf5 = lf.table_1d([[rng.random() for _ in range(3)] for _ in range(256)])
        for _ in range(300):
            c68 = [rng.random() for _ in range(3)]
            c67 = (rng.uniform(1, 30), 1900.0, rng.uniform(0.02, 1.0), rng.uniform(0.0005, 0.01))
            axis, eye = lc.unit(rng), [rng.uniform(-50, 50) for _ in range(3)]
            P = [rng.uniform(-80, 80) for _ in range(3)]
            C = [rng.uniform(0, 2) for _ in range(3)]
            y = rng.uniform(0.3, 1.5)

            def tf17(a, t):
                return [c68[i] + t * (tf4(a)[i] - c68[i]) for i in range(3)]

            factor, add = lf.vertex_fog(P, eye, y, axis, c67, tf5, tf17)
            near(self, lf.fog_apply(C, factor, add), lf.fog(C, P, eye, (1.0, y), axis, c67, c68, tf4, tf5), tol=1e-12)


# --- the sky ------------------------------------------------------------------------------------------

BLOCK = dict(c31x=150.0, c46x=100.0, c47x=0.0, c64=(0.000305633, 0.000394035, 0.00107172),
             c65=(0.00529975, 0.00601759, 0.00992419), c66=(-0.831325, 0.1149, 0.54378), c67y=1900.0)


class SkyLatLong(unittest.TestCase):
    def test_it_is_atan2_and_asin_to_the_polynomials_error(self):
        rng = random.Random(3)
        worst = 0.0
        for _ in range(20000):
            n = lc.unit(rng)
            u, v = lf.sky_latlong(n)
            worst = max(worst, abs(u - (0.5 + math.atan2(n[1], n[0]) / (2 * math.pi))),
                        abs(v - math.asin(n[2]) * 2 / math.pi))
        self.assertLess(worst, 6e-5)

    def test_the_direction_need_not_be_a_unit_vector(self):
        near(self, lf.sky_latlong([300.0, -400.0, 1200.0]), lf.sky_latlong([3 / 13, -4 / 13, 12 / 13]))

    def test_the_four_compass_directions(self):
        for n, u in (([1.0, 0.0, 0.0], 0.5), ([0.0, 1.0, 0.0], 0.75), ([0.0, -1.0, 0.0], 0.25)):
            got = lf.sky_latlong(n)
            self.assertAlmostEqual(got[0], u, delta=2e-5)
            self.assertAlmostEqual(got[1], 0.0, delta=5e-5)

    def test_the_seam_lies_on_the_negative_x_axis(self):
        # u runs from 0 (just south of -x, y < 0) to 1 (on -x and just north of it). On the axis itself the
        # microcode's comparisons give the upper end.
        self.assertAlmostEqual(lf.sky_latlong([-1.0, 0.0, 0.0])[0], 1.0, delta=2e-5)
        self.assertAlmostEqual(lf.sky_latlong([-1.0, 1e-9, 0.0])[0], 1.0, delta=2e-5)
        self.assertAlmostEqual(lf.sky_latlong([-1.0, -1e-9, 0.0])[0], 0.0, delta=2e-5)
        self.assertAlmostEqual(lf.sky_latlong([-1.0, 0.0, 0.5])[0], 1.0, delta=2e-5)
        # The ends overshoot [0, 1] by the literals' rounding, 1.5e-5: a clamped sampler would lose nothing,
        # a wrapped one reads the neighbour.
        self.assertGreater(lf.sky_latlong([-1.0, 0.0, 0.0])[0], 1.0)
        self.assertLess(lf.sky_latlong([-1.0, -1e-9, 0.0])[0], 0.0)
        # Across the seam the jump is one whole turn.
        north, south = lf.sky_latlong([-1.0, 1e-6, 0.2])[0], lf.sky_latlong([-1.0, -1e-6, 0.2])[0]
        self.assertAlmostEqual(north - south, 1.0, delta=1e-4)

    def test_the_poles(self):
        # No azimuth there: the ratio min / max of |x| and |y| is 0 / 0, which counts as 0 (the console's
        # product rule, see light_formulas.py), so u = 0.5; v is 1 and -1 to the literals' rounding.
        up, down = lf.sky_latlong([0.0, 0.0, 1.0]), lf.sky_latlong([0.0, 0.0, -1.0])
        self.assertAlmostEqual(up[0], 0.5, delta=1e-12)
        self.assertAlmostEqual(down[0], 0.5, delta=1e-12)
        self.assertAlmostEqual(up[1], 1.0, delta=5e-5)
        self.assertAlmostEqual(down[1], -1.0, delta=5e-5)
        self.assertGreater(up[1], 1.0)
        # Beside the pole the azimuth is the direction's own.
        self.assertAlmostEqual(lf.sky_latlong([0.0, 1e-12, 1.0])[0], 0.75, delta=2e-5)
        self.assertAlmostEqual(lf.sky_latlong([-1e-12, 0.0, 1.0])[0], 1.0, delta=2e-5)

    def test_the_octant_boundaries_jump_by_the_polynomials_error_only(self):
        # Where |x| = |y| the shader changes from atan(q) to pi / 2 - atan(q); its polynomial exceeds pi / 4 by
        # 1.1e-5 at q = 1, so u jumps by 3.6e-6 of a turn there. Nothing like the seam's whole turn.
        for sx in (1.0, -1.0):
            for sy in (1.0, -1.0):
                a = lf.sky_latlong([sx * 1.0, sy * (1.0 + 1e-9), 0.3])
                b = lf.sky_latlong([sx * (1.0 + 1e-9), sy * 1.0, 0.3])
                self.assertAlmostEqual(abs(a[0] - b[0]), 3.64e-6, delta=0.05e-6)

    def test_no_direction_is_finite(self):
        for n in ([0.0, 0.0, 0.0], [NAN, 1.0, 0.0], [INF, 0.0, 0.0]):
            self.assertTrue(all(math.isfinite(x) for x in lf.sky_latlong(n)))


class Sky(unittest.TestCase):
    def test_the_paths_at_the_zenith_and_the_horizon(self):
        l1, l2 = lf.sky_paths([0.0, 0.0, 1.0], 1900.0, 150.0, 100.0)
        self.assertAlmostEqual(l1, 150.0, delta=1e-9)
        self.assertAlmostEqual(l2, 100.0, delta=1e-9)
        l1, l2 = lf.sky_paths([1.0, 0.0, 0.0], 1900.0, 150.0, 100.0)
        self.assertAlmostEqual(l1, 1900.0 * 1.05, delta=1e-3)          # the literal 1.05 is a single
        self.assertAlmostEqual(l2, 1900.0 * 1.05, delta=1e-3)

    def test_the_paths_between(self):
        # k = |n.z|^0.2, h = c67.y * (1.05 - k), l = h + (c - h) * k.
        k = 0.5 ** 0.2
        h = 1900.0 * (1.05 - k)
        l1, l2 = lf.sky_paths([math.sqrt(0.75), 0.0, 0.5], 1900.0, 150.0, 100.0)
        self.assertAlmostEqual(l1, h + (150.0 - h) * k, delta=1e-3)
        self.assertAlmostEqual(l2, h + (100.0 - h) * k, delta=1e-3)

    def test_below_the_horizon_the_paths_are_mirrored(self):
        near(self, lf.sky_paths([0.6, 0.0, -0.8], 1900.0, 150.0, 100.0), lf.sky_paths([0.6, 0.0, 0.8], 1900.0, 150.0, 100.0))

    def test_the_extinction_is_an_exponential(self):
        E = lf.sky_extinction(150.0, 100.0, BLOCK["c64"], BLOCK["c65"])
        for i in range(3):
            self.assertAlmostEqual(E[i], math.exp(-(150.0 * BLOCK["c64"][i] + 100.0 * BLOCK["c65"][i])), delta=1e-7)
        self.assertEqual(lf.sky_extinction(0.0, 0.0, BLOCK["c64"], BLOCK["c65"]), [1.0, 1.0, 1.0])

    def test_clear_sky_where_the_texture_is_transparent(self):
        tf4 = const(*S)
        n = [0.0, 0.0, 1.0]
        E = lf.sky_extinction(150.0, 100.0, BLOCK["c64"], BLOCK["c65"])
        out = lf.sky(n, BLOCK, tf4, const(0.9, 0.8, 0.7, 0.0), const(0.1, 0.2, 0.3, 0.0))
        near(self, out, [(1 - E[i]) * S[i] for i in range(3)], tol=1e-9)
        self.assertAlmostEqual(tf4.seen[0], 0.54378, delta=1e-12)      # sat(n . c66)

    def test_the_texture_where_it_is_opaque(self):
        out = lf.sky([1.0, 2.0, 3.0], BLOCK, const(*S), const(0.9, 0.8, 0.7, 1.0), const(0.1, 0.2, 0.3, 1.0))
        near(self, out, [0.81, 0.64, 0.49])                            # the texel squared
        block = dict(BLOCK, c47x=1.0)
        out = lf.sky([1.0, 2.0, 3.0], block, const(*S), const(0.9, 0.8, 0.7, 1.0), const(0.1, 0.2, 0.3, 1.0))
        near(self, out, [0.01, 0.04, 0.09])                            # the second texture
        block = dict(BLOCK, c47x=0.25)
        out = lf.sky([1.0, 2.0, 3.0], block, const(*S), const(0.9, 0.8, 0.7, 1.0), const(0.1, 0.2, 0.3, 0.0))
        E = lf.sky_extinction(*lf.sky_paths([1.0, 2.0, 3.0], 1900.0, 150.0, 100.0), BLOCK["c64"], BLOCK["c65"])
        col = [0.81 + 0.25 * (0.01 - 0.81), 0.64 + 0.25 * (0.04 - 0.64), 0.49 + 0.25 * (0.09 - 0.49)]
        base = [(1 - E[i]) * S[i] for i in range(3)]
        near(self, out, [base[i] + 0.75 * (col[i] - base[i]) for i in range(3)])     # alpha 1 + 0.25 * (0 - 1)

    def test_both_textures_are_read_at_the_lat_long_coordinate(self):
        tf13, tf14 = const(0.0, 0.0, 0.0, 0.0), const(0.0, 0.0, 0.0, 0.0)
        n = [0.3, -0.4, 0.5]
        lf.sky(n, BLOCK, const(*S), tf13, tf14)
        self.assertEqual(tf13.seen, [lf.sky_latlong(n)])
        self.assertEqual(tf14.seen, [lf.sky_latlong(n)])

    def test_without_textures_it_is_the_clear_sky(self):
        n = [0.3, -0.4, 0.5]
        clear = lf.sky(n, BLOCK, const(*S), const(0.9, 0.8, 0.7, 0.0), const(0.1, 0.2, 0.3, 0.0))
        near(self, lf.sky(n, BLOCK, const(*S), None, None), clear)
        # The second texture alone missing: the first as with c47.x = 0.
        both = lf.sky(n, dict(BLOCK, c47x=0.0), const(*S), const(0.9, 0.8, 0.7, 0.6), const(0.1, 0.2, 0.3, 0.0))
        near(self, lf.sky(n, dict(BLOCK, c47x=0.7), const(*S), const(0.9, 0.8, 0.7, 0.6), None), both)

    def test_it_is_finite_and_silent_for_any_numbers(self):
        rng = random.Random(9)
        pool = [0.0, -0.0, 1.0, -1.0, 1e-300, 1e300, -1e300, NAN, INF, -INF, 1900.0]

        def v(n):
            return [rng.choice(pool) if rng.random() < 0.5 else rng.uniform(-100, 100) for _ in range(n)]

        for _ in range(3000):
            block = dict(c31x=v(1)[0], c46x=v(1)[0], c47x=v(1)[0], c64=v(3), c65=v(3), c66=v(3), c67y=v(1)[0])
            out = lf.sky(v(3), block, const(*v(4)), const(*v(4)), const(*v(4)))
            self.assertEqual(len(out), 3)
            self.assertTrue(all(math.isfinite(x) for x in out), out)

    def test_the_literals_are_the_registers_of_the_gpu_log(self):
        # c250..c255 at every execution of F760... in frame 1116 of gpu_shadow_bridge2.log (hex in the doc).
        self.assertEqual(lf.SKY_LITERALS[250][3], 1.0499999523162842)
        self.assertEqual(lf.SKY_LITERALS[251][0], -3.1415927410125732)
        self.assertEqual(lf.SKY_LITERALS[254], (0.20000000298023224, 0.02083509974181652, 0.6366385221481323,
                                                0.15915963053703308))
        self.assertEqual(lf.SKY_LITERALS[255][0], 1.4426950216293335)
        self.assertEqual(sorted(lf.SKY_LITERALS), [250, 251, 252, 253, 254, 255])


# --- the data for the capture ---------------------------------------------------------------------------

class FogShaders(unittest.TestCase):
    def test_forty_pixel_shaders_thirty_plain_and_ten_scaled(self):
        self.assertEqual(len(lf.FOG_SHADERS), 40)
        self.assertEqual(len({s.hash for s in lf.FOG_SHADERS}), 40)
        self.assertEqual(sum(s.form == "plain" for s in lf.FOG_SHADERS), 30)
        self.assertEqual(sum(s.form == "alpha" for s in lf.FOG_SHADERS), 10)
        for s in lf.FOG_SHADERS:
            self.assertRegex(s.hash, r"^[0-9A-F]{16}$")
            self.assertIn(s.world, range(16))

    def test_the_family_is_light_checks(self):
        by_ps = {}
        for family, pairs in lc.FAMILIES.items():
            for ps, _ in pairs:
                by_ps.setdefault(ps, set()).add(family)
        for s in lf.FOG_SHADERS:
            with self.subTest(shader=s.hash):
                self.assertEqual({s.family} if s.family else set(), by_ps.get(s.hash, set()))

    def test_the_scaled_form_belongs_to_no_lit_family(self):
        # One exception since sub-project 14: the Murgo crowd hair F2F103F1 is alpha-tested and lit
        # (object-multi, light_check); every other alpha-form shader is in no lit family.
        self.assertEqual({s.family for s in lf.FOG_SHADERS if s.form == "alpha"}, {None, "object-multi"})
        self.assertEqual([s.hash for s in lf.FOG_SHADERS if s.form == "alpha" and s.family], ["F2F103F16764043B"])

    def test_the_lake_and_the_sky_are_not_in_the_list(self):
        hashes = {s.hash for s in lf.FOG_SHADERS}
        self.assertNotIn("2B091BD0C45A7454", hashes)
        self.assertNotIn("F7601FBE485936FD", hashes)
        self.assertEqual(lf.FOG_NOT_OF_THE_FORM, ("2B091BD0C45A7454",))
        self.assertEqual(lf.SKY_SHADER, "F7601FBE485936FD")

    def test_the_vertex_form(self):
        self.assertEqual(len(lf.FOG_VERTEX_SHADERS), 6)
        by = {s.hash: s for s in lf.FOG_VERTEX_SHADERS}
        self.assertEqual((by["475EC9F795E5EDBB"].world, by["475EC9F795E5EDBB"].factor, by["475EC9F795E5EDBB"].add), (2, 6, 7))
        self.assertEqual((by["A5846836C90E1192"].world, by["A5846836C90E1192"].factor, by["A5846836C90E1192"].add), (0, 4, 5))


# --- what the interpreter gained -------------------------------------------------------------------------

def no_sampler(slot, coords, attrs):
    raise AssertionError(f"unexpected texture fetch tf{slot}")


def run(text, consts=None, regs=None, **kw):
    m = ue.Machine(text, consts or {}, no_sampler, regs=regs, **kw)
    return m.run(), m


PUSH = """/*    0.0 */       exec    // PredicateClean=false
/*    1   */          setp_ne_push r2.___w, c1.xxxx, r0.wwww
/*    2   */     (p0) add r3, r0, r0
/*    3   */          setp_gt_push r4.x___, r2.wwww, r0.xxxx
/*    4   */     (p0) add r5, r0, r0
/*    0.1 */       exece
/*    5   */          max oC0, r3, r5
"""

KILL = """/*    0.0 */       exec    // PredicateClean=false
/*    1   */          max r6._, r6, r6
              +       setp_gt r6._, r0.w
/*    2   */     (p0) kill_gt r1.x___, r0.xyzz, c1.xyzz
              +  (p0) sgts r1._y__, r0.x
/*    0.1 */       exece
/*    3   */          max oC0, r1, r1
"""

ZERO = """/*    0.0 */       exec
/*    1   */          max r6._, r6, r6
              +       rcp r1.x___, r0.x
/*    2   */          mul r2.x___, r0.xxxx, r1.xxxx
              +       muls r2._y__, r0.xy
/*    3   */          mad r2.__z_, r0.xxxx, r1.xxxx, r0.zzzz
              +       mulsc r2.___w, c1.x, r1.x
/*    4   */          dp3 r3.x___, r0.xxxx, r1.xxxx
              +       muls_prev r3._y__, r0.x
/*    0.1 */       exece
/*    5   */          max oC0, r2, r2
"""


class InterpreterForTheWaterAndTheSky(unittest.TestCase):
    """ucode.h (AluVectorOpcode): setp_ne_push sets p0 = (src0.w == 0 and src1.w != 0) and writes 0 where
    (src0.x == 0 and src1.x != 0), else src0.x + 1; setp_gt_push the same with `> 0`; kill_gt discards the
    pixel where any component of src0 is greater than src1's and writes 1 there, else 0."""

    def test_setp_ne_push_taken(self):
        # c1.x = 0 (the outer counter), r0.w = 3: p0, r2.w = 0. Then r2.w = 0 and r0.x = 2 > 0: p0, r4.x = 0.
        out, m = run(PUSH, {1: [0, 0, 0, 0]}, {0: [2, 5, 7, 3], 2: [9, 9, 9, 9], 4: [9, 9, 9, 9]})
        self.assertEqual(m.r[2], [9, 9, 9, 0.0])
        self.assertEqual(m.r[3], [4, 10, 14, 6])
        self.assertEqual(m.r[4][0], 0.0)
        self.assertEqual(out["oC0"], [4, 10, 14, 6])

    def test_setp_ne_push_not_taken_counts_up_and_holds_the_inner_one_off(self):
        # r0.w = 0: no p0, r2.w = c1.x + 1 = 1. The inner push sees a counter that is not 0: no p0 although
        # r0.x > 0, and r4.x = r2.w + 1 = 2.
        out, m = run(PUSH, {1: [0, 0, 0, 0]}, {0: [2, 5, 7, 0]})
        self.assertEqual(m.r[2][3], 1.0)
        self.assertEqual(m.r[3], [0, 0, 0, 0])
        self.assertEqual(m.r[4][0], 2.0)
        self.assertEqual(m.r[5], [0, 0, 0, 0])
        self.assertFalse(m.p0)

    def test_setp_gt_push_needs_a_positive_value(self):
        out, m = run(PUSH, {1: [0, 0, 0, 0]}, {0: [0, 5, 7, 3]})        # outer taken, r0.x = 0: not > 0
        self.assertEqual(m.r[3], [0, 10, 14, 6])
        self.assertEqual(m.r[5], [0, 0, 0, 0])
        self.assertEqual(m.r[4][0], 1.0)                                # r2.w (0) + 1
        out, m = run(PUSH, {1: [4, 0, 0, 0]}, {0: [2, 5, 7, 3]})        # an outer counter of 4: never taken
        self.assertEqual(m.r[2][3], 5.0)
        self.assertEqual(m.r[3], [0, 0, 0, 0])

    def test_kill_gt_marks_the_pixel_and_goes_on(self):
        out, m = run(KILL, {1: [1, 1, 1, 1]}, {0: [0.5, 2.0, 0.5, 1.0]})     # y: 2 > 1
        self.assertTrue(m.killed)
        self.assertEqual(out["oC0"][:2], [1.0, 1.0])                         # the vector result 1; sgts(0.5) = 1
        out, m = run(KILL, {1: [1, 1, 1, 1]}, {0: [0.5, 1.0, 0.5, 1.0]})     # equal is not greater
        self.assertFalse(m.killed)
        self.assertEqual(out["oC0"][:2], [0.0, 1.0])

    def test_a_kill_under_a_false_predicate_does_nothing(self):
        out, m = run(KILL, {1: [1, 1, 1, 1]}, {0: [5.0, 5.0, 5.0, 0.0]})
        self.assertFalse(m.killed)
        self.assertEqual(out["oC0"], [0, 0, 0, 0])

    def test_a_product_with_a_zero_factor(self):
        # By default 0 * infinity is not a number (the docstring's "Not modelled").
        out, m = run(ZERO, {1: [0, 0, 0, 0]}, {0: [0.0, INF, 7.0, 0.0]})
        self.assertTrue(all(x != x for x in out["oC0"]))
        self.assertTrue(m.r[3][0] != m.r[3][0] and m.r[3][1] != m.r[3][1])
        # zero_product: the console's rule as the SDK's interpreter and translators have it, +0.
        out, m = run(ZERO, {1: [0, 0, 0, 0]}, {0: [0.0, INF, 7.0, 0.0]}, zero_product=True)
        self.assertEqual(m.r[1][0], INF)
        self.assertEqual(out["oC0"], [0.0, 0.0, 7.0, 0.0])               # mul, muls, mad (0 + r0.z), mulsc
        self.assertEqual(m.r[3][:2], [0.0, 0.0])                         # dp3, muls_prev
        # It changes nothing where no factor is zero.
        a, _ = run(ZERO, {1: [3, 0, 0, 0]}, {0: [2.0, 5.0, 7.0, 0.0]})
        b, _ = run(ZERO, {1: [3, 0, 0, 0]}, {0: [2.0, 5.0, 7.0, 0.0]}, zero_product=True)
        self.assertEqual(a, b)
        self.assertEqual(a["oC0"], [1.0, 10.0, 8.0, 1.5])


# --- the checker on a dump written for the test --------------------------------------------------------

# A pixel shader in the dump's syntax: a colour from tf0, the fog block with every step on a line of its
# own (the world position is interpolator 1), then the output scale c20.w and c26.
SYNTHETIC = """/*    0.0 */       exec
/*    1   */          tfetch2D r3, r0.xy, tf0
/*    0.1 */       exec    // PredicateClean=false
/*    2   */          add r4.xyz_, -r1.xyzz, c19.xyzz
/*    3   */          dp3 r5.x___, r4.xyzz, r4.xyzz
/*    4   */          max r6._, r6, r6
              +       rsq r5._y__, r_abs[5].x
/*    5   */          mul r4.xyz_, r4.xyzz, r5.yyyy
              +       sqrt r5.x___, r_abs[5].x
/*    6   */          max r6._, r6, r6
              +       subsc r5.x___, -c67.x, -r5.x
/*    7   */          max r6._, r6, r6
              +       setp_gt r6._, r5.x
/*    1.0 */ (!p0) jmp L1
/*    1.1 */  (p0) exec
/*    8   */     (p0) dp3_sat r6.x___, -r4.xyzz, c66.xyzz
              +  (p0) mulsc r6._y__, c67.w, r5.x
/*    9   */     (p0) tfetch1D r7.xyz_, r6.x, tf4
/*   10   */     (p0) tfetch1D r8.xyz_, r6.y, tf5
/*   11   */     (p0) add r9.xyz_, -r8.xyzz, c255.xxxx
              +  (p0) mulsc_sat r6.__z_, c67.z, r5.x
/*   12   */     (p0) add r7.xyz_, r7.xyzz, -c68.xyzz
/*   13   */     (p0) mad r7.xyz_, r6.zzzz, r7.xyzz, c68.xyzz
/*   14   */     (p0) mul r9.xyz_, r9.xyzz, c28.yyyy
/*   15   */     (p0) mad r7.xyz_, r9.xyzz, r7.xyzz, -r3.xyzz
/*   16   */     (p0) mad r7.xyz_, r3.xyzz, r8.xyzz, r7.xyzz
/*   17   */     (p0) mad r3.xyz_, r7.xyzz, c28.xxxx, r3.xyzz
                label L1
/*    2.0 */       exece
/*   18   */          mul r3.xyz_, r3.xyzz, c26.xyzz
/*   19   */          mul oC0.xyz_, r3.xyzz, c20.wwww
              +       mulsc oC0.___w, c26.w, r3.w
"""
# The blended form: the in-scatter term times the pixel's alpha.
SYNTHETIC_ALPHA = SYNTHETIC.replace("/*   14   */     (p0) mul r9.xyz_, r9.xyzz, c28.yyyy\n",
                                    "/*   14   */     (p0) mul r9.xyz_, r9.xyzz, c28.yyyy\n"
                                    "/*   14   */     (p0) mul r9.xyz_, r9.xyzz, r3.wwww\n")


def wrong_fog(change):
    """lf.fog with one deliberate error."""
    def f(C, P, eye, c28, c66, c67, c68, tf4, tf5, scatter_scale=1.0):
        if change == "amounts exchanged":
            c28 = (c28[1], c28[0])
        elif change == "axis towards the eye":
            c66 = [-x for x in c66[:3]]
        elif change == "start not subtracted":
            return lf.fog(C, P, eye, c28, c66, (0.0, c67[1], c67[2], c67[3]), c68, tf4, tf5, scatter_scale)
        elif change == "no near blend":
            c67 = (c67[0], c67[1], 1e9, c67[3])
        elif change == "table exchanged":
            tf4, tf5 = tf5, tf4
        elif change == "texel centres at the edges":
            inner = tf5
            tf5 = lambda u: inner(u + 0.5 / 256)      # noqa: E731
        elif change == "alpha ignored":
            scatter_scale = 1.0
        return lf.fog(C, P, eye, c28, c66, c67, c68, tf4, tf5, scatter_scale)
    return f


class SyntheticDump(unittest.TestCase):
    def test_the_formula_agrees_with_the_microcode(self):
        res = fc.check_pixel_text(SYNTHETIC, world=1, form="plain", c26=True, trials=200)
        self.assertTrue(res.ok, res.line())
        self.assertEqual(res.fog + res.near, 200)
        self.assertGreater(res.fog, 100)
        self.assertGreater(res.near, 20)
        self.assertEqual((res.fog_ok, res.near_ok, res.alpha_ok, res.coords_ok), (res.fog, res.near, res.fog, res.fog))
        self.assertLess(res.worst, 1e-9)
        self.assertGreater(res.blend_between, 30)       # the blend from c68 to the table is exercised

    def test_the_blended_form(self):
        res = fc.check_pixel_text(SYNTHETIC_ALPHA, world=1, form="alpha", c26=True, trials=200)
        self.assertTrue(res.ok, res.line())
        self.assertFalse(fc.check_pixel_text(SYNTHETIC_ALPHA, world=1, form="plain", c26=True, trials=60).ok)
        self.assertFalse(fc.check_pixel_text(SYNTHETIC, world=1, form="alpha", c26=True, trials=60).ok)

    def test_a_wrong_formula_fails(self):
        for change in ("amounts exchanged", "axis towards the eye", "start not subtracted", "no near blend",
                       "table exchanged", "texel centres at the edges"):
            with self.subTest(change=change):
                res = fc.check_pixel_text(SYNTHETIC, world=1, form="plain", c26=True, trials=60, formula=wrong_fog(change))
                self.assertFalse(res.ok, change)
                self.assertLess(res.fog_ok, res.fog)
                self.assertIsNotNone(res.first_bad)
        res = fc.check_pixel_text(SYNTHETIC_ALPHA, world=1, form="alpha", c26=True, trials=60, formula=wrong_fog("alpha ignored"))
        self.assertFalse(res.ok)

    def test_the_wrong_interpolator_or_scale_fails(self):
        self.assertFalse(fc.check_pixel_text(SYNTHETIC, world=2, form="plain", c26=True, trials=60).ok)
        self.assertFalse(fc.check_pixel_text(SYNTHETIC, world=1, form="plain", c26=False, trials=60).ok)

    def test_a_shader_that_fogs_near_pixels_fails(self):
        text = SYNTHETIC.replace("subsc r5.x___, -c67.x, -r5.x", "subsc r5.x___, c255.y, -r5.x")
        res = fc.check_pixel_text(text, world=1, form="plain", c26=True, trials=60)
        self.assertFalse(res.ok)

    def test_no_trial_is_no_pass(self):
        self.assertFalse(fc.check_pixel_text(SYNTHETIC, world=1, form="plain", c26=True, trials=0).ok)

    def test_the_trials_are_the_same_in_every_process(self):
        a = fc.check_pixel_text(SYNTHETIC, world=1, form="plain", c26=True, trials=40)
        b = fc.check_pixel_text(SYNTHETIC, world=1, form="plain", c26=True, trials=40)
        self.assertEqual((a.fog, a.near, a.worst), (b.fog, b.near, b.worst))
        self.assertEqual((a.fog, a.near), (27, 13))

    def test_the_static_scan(self):
        self.assertEqual(fc.scan_text(SYNTHETIC), dict(pixel_form=True, c26=True, bools=[]))
        self.assertFalse(fc.scan_text(SYNTHETIC.replace("tf5", "tf6"))["pixel_form"])
        self.assertEqual(fc.scan_text("/*  1 */ cjmp !b129, L1\n/*  2 */ cexec b132\n")["bools"], [129, 132])


class CommandLine(unittest.TestCase):
    def test_a_missing_dump_folder_is_a_failure_not_a_pass(self):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertNotEqual(fc.main(["--dumps", str(ROOT / "out" / "no_such_dump_folder")]), 0)


# --- the real dumps ---------------------------------------------------------------------------------------

@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class RealDumps(unittest.TestCase):
    TRIALS = 24

    def test_the_list_is_the_dumps_fogged_pixel_shaders(self):
        found = {h for h, facts in fc.scan(DUMPS).items() if facts["pixel_form"]}
        self.assertEqual(found, {s.hash for s in lf.FOG_SHADERS} | set(lf.FOG_NOT_OF_THE_FORM))

    def test_every_pixel_shader_of_the_list(self):
        fog = near_ = 0
        for s in lf.FOG_SHADERS:
            with self.subTest(shader=s.hash):
                res = fc.check_pixel(s.hash, trials=self.TRIALS, dumps=DUMPS)
                self.assertTrue(res.ok, f"{s.hash}: {res.line()}")
                self.assertEqual(res.errors, {})
                self.assertGreater(res.fog, 0)
                self.assertGreater(res.blend_between, 0)
                fog += res.fog
                near_ += res.near
        self.assertGreater(fog, 40 * self.TRIALS // 2)
        self.assertGreater(near_, 40 * self.TRIALS // 10)

    def test_the_water_shaders_run_with_their_kill_and_reach_the_block(self):
        for h in ("A17D8AEC3A817D45", "F6D98C7B4D98438B"):
            res = fc.check_pixel(h, trials=60, dumps=DUMPS)
            self.assertTrue(res.ok, res.line())
            self.assertGreater(res.fog, 15)
            self.assertGreater(res.killed, 0)
            self.assertGreater(res.unreached, 0)
            self.assertEqual(res.fog + res.near + res.killed + res.unreached, 60)

    def test_only_the_water_shaders_may_miss_the_block(self):
        for s in lf.FOG_SHADERS:
            if s.hash not in fc.WATER:
                res = fc.check_pixel(s.hash, trials=8, dumps=DUMPS)
                self.assertEqual((res.killed, res.unreached), (0, 0), s.hash)

    def test_the_cross_fade_of_2f8a_is_exercised(self):
        res = fc.check_pixel("2F8A0341E8C09698", trials=60, dumps=DUMPS)
        self.assertTrue(res.ok, res.line())
        self.assertGreater(res.after_between, 10)       # trials whose fade is strictly between 0 and 1

    def test_the_lake_is_not_of_the_form(self):
        res = fc.check_pixel_text(ue.load(DUMPS, "2B091BD0C45A7454", "frag"), world=0, form="plain", c26=False, trials=60)
        self.assertFalse(res.ok)

    def test_a_wrong_formula_fails_on_the_real_shaders(self):
        for h in ("7DDAD5C29B3F56EE", "8D900846800943C8", "401A01FD4E5F8757", "A17D8AEC3A817D45"):
            for change in ("amounts exchanged", "no near blend", "texel centres at the edges"):
                with self.subTest(shader=h, change=change):
                    self.assertFalse(fc.check_pixel(h, trials=40, dumps=DUMPS, formula=wrong_fog(change)).ok)

    def test_the_vertex_form(self):
        for vs in fc.VERTEX_CHECKED:
            with self.subTest(vs=vs):
                res = fc.check_vertex(vs, trials=40, dumps=DUMPS)
                self.assertTrue(res.ok, f"{vs}: {res.line()}")
                self.assertGreater(res.fog, 10)
                self.assertGreater(res.near, 2)
        self.assertEqual(set(fc.VERTEX_CHECKED), {"475EC9F795E5EDBB", "A5846836C90E1192", "D7DD31B9BF194DE8",
                                                  "2D40B53C926109BE", "129F7F2793DA9084"})

    def test_a_wrong_vertex_formula_fails(self):
        def no_c8(P, eye, c8x, c110, c111, tf16, tf17):
            return lf.vertex_fog(P, eye, 1.0, c110, c111, tf16, tf17)

        def coordinates_exchanged(P, eye, c8x, c110, c111, tf16, tf17):
            return lf.vertex_fog(P, eye, c8x, c110, c111, tf16, lambda a, t: tf17(t, a))

        for formula in (no_c8, coordinates_exchanged):
            self.assertFalse(fc.check_vertex("475EC9F795E5EDBB", trials=40, dumps=DUMPS, formula=formula).ok)
            self.assertFalse(fc.check_vertex("A5846836C90E1192", trials=40, dumps=DUMPS, formula=formula).ok)

    def test_the_impostors_form_has_no_c8x_and_its_eye_is_c47(self):
        # 129F... (the distant trees' impostors): lf.impostor_fog. The trees' form with the draw's c8.x fails
        # there, and so does an eye that is not c47.
        impostor = "129F7F2793DA9084"

        def with_c8x(P, eye, c8x, c110, c111, tf16, tf17):
            return lf.vertex_fog(P, eye, c8x, c110, c111, tf16, tf17)

        def coordinates_exchanged(P, eye, c8x, c110, c111, tf16, tf17):
            return lf.impostor_fog(P, eye, c110, c111, tf16, lambda a, t: tf17(t, a))

        def eye_moved(P, eye, c8x, c110, c111, tf16, tf17):
            return lf.impostor_fog(P, [x + 1.0 for x in eye], c110, c111, tf16, tf17)

        for formula in (with_c8x, coordinates_exchanged, eye_moved):
            self.assertFalse(fc.check_vertex(impostor, trials=40, dumps=DUMPS, formula=formula).ok, formula.__name__)
        self.assertEqual(fc._VERTEX_EXTRA[impostor]["eye"], 47)
        self.assertEqual(fc._VERTEX_EXTRA[impostor]["literals"], lf.IMPOSTOR_SHADER["vs_lit"])

    def test_the_tree_pixel_shaders_apply_factor_and_add(self):
        self.assertEqual({(f, ps) for f, ps, _ in fc.APPLY_PAIRS},
                         {("tree-trunk", "00E09D1BC5295D52"), ("tree-trunk-multi", "58D3FAAAA84A48AA"),
                          ("tree-trunk-multi", "2FBE9B4EE822609A"), ("tree-trunk-multi", "7928BAA7A6691A5D"),
                          ("tree-leaf", "7CD57B81550F19E3"),
                          ("tree-leaf-multi", "7CD57B81550F19E3")})
        for family, ps, vs in fc.APPLY_PAIRS:
            with self.subTest(ps=ps, vs=vs):
                res = fc.check_apply(family, ps, vs, trials=20, dumps=DUMPS)
                self.assertTrue(res.ok, f"{ps} {vs}: {res.line()}")
                self.assertEqual(res.trials, 20)

    def test_a_wrong_application_fails(self):
        def add_scaled(C, factor, add):
            return lf.fog_apply(C, factor, [x * 0.5 for x in add])

        family, ps, vs = fc.APPLY_PAIRS[0]
        self.assertFalse(fc.check_apply(family, ps, vs, trials=20, dumps=DUMPS, formula=add_scaled).ok)

    def test_the_sky(self):
        res = fc.check_sky(trials=150, dumps=DUMPS)
        self.assertTrue(res.ok, res.line())
        self.assertEqual(res.trials, 150 + len(fc.SKY_SPECIAL))
        self.assertEqual(res.agree, res.trials)
        self.assertLess(res.worst, 1e-9)

    def test_the_skys_special_directions_name_the_seam_and_the_poles(self):
        names = [name for name, _ in fc.SKY_SPECIAL]
        for want in ("seam, on -x", "seam, north of -x", "seam, south of -x", "zenith", "nadir", "horizon"):
            self.assertIn(want, names)
        by = dict(fc.SKY_SPECIAL)
        self.assertEqual(by["zenith"][:2], [0.0, 0.0])
        self.assertEqual(by["seam, on -x"][1], 0.0)
        self.assertLess(by["seam, on -x"][0], 0.0)

    def test_at_the_poles_the_microcode_needs_the_zero_product(self):
        # 0 * (1 / 0) at instructions 17 and 19: not a number in plain arithmetic.
        res = fc.check_sky(trials=0, dumps=DUMPS, zero_product=False)
        self.assertFalse(res.ok)
        self.assertEqual(sorted(res.bad_names), ["nadir", "zenith"])

    def test_a_wrong_sky_fails(self):
        def true_angles(n, block, tf4, tf13, tf14):
            # atan2 and asin in the place of the shader's polynomials: off by up to 4e-5 of a turn.
            original = lf.sky_latlong
            lf.sky_latlong = lambda d: (0.5 + math.atan2(d[1], d[0]) / (2 * math.pi), math.asin(lf.normalize(d)[2]) * 2 / math.pi)
            try:
                return lf.sky(n, block, tf4, tf13, tf14)
            finally:
                lf.sky_latlong = original

        def paths_exchanged(n, block, tf4, tf13, tf14):
            return lf.sky(n, dict(block, c31x=block["c46x"], c46x=block["c31x"]), tf4, tf13, tf14)

        def no_square(n, block, tf4, tf13, tf14):
            return lf.sky(n, block, tf4, lambda u, v: [math.sqrt(abs(x)) for x in tf13(u, v)[:3]] + [tf13(u, v)[3]], tf14)

        def second_texture_ignored(n, block, tf4, tf13, tf14):
            return lf.sky(n, block, tf4, tf13, None)

        def clear_sky_only(n, block, tf4, tf13, tf14):
            return lf.sky(n, block, tf4, None, None)

        def axis_reversed(n, block, tf4, tf13, tf14):
            return lf.sky(n, dict(block, c66=[-x for x in block["c66"]]), tf4, tf13, tf14)

        for formula in (true_angles, paths_exchanged, no_square, second_texture_ignored, clear_sky_only, axis_reversed):
            self.assertFalse(fc.check_sky(trials=60, dumps=DUMPS, formula=formula).ok)

    def test_the_command_line(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = fc.main(["--trials", "4", "--dumps", str(DUMPS)])
        text = out.getvalue()
        self.assertEqual(code, 0, text)
        self.assertIn("40 of 40 pixel shaders agree", text)
        self.assertIn("sky F7601FBE485936FD", text)


if __name__ == "__main__":
    unittest.main()
