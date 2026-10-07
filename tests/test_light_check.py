import contextlib
import io
import math
import random
import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_check as lc  # noqa: E402
import light_formulas as lf  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"

# A small pixel shader in the dump's syntax with the terrain layers' form and nothing else:
#   oC0.xyz = tf0^2 * (c11.x * (pow(tf3, c21.w) * c21.xyz * L + c20.xyz) + c87.xyz * sat(N . c80)) * c20.w,
#   L = c22 + (1 - c22) * sat(normalize(r2) . N)^2,  N = normalize(r1),  c255.x the literal 1.
SYNTHETIC = """/*    0.0 */       exec
/*    1   */          tfetch2D r3.xyz_, r0.xy, tf0
/*    2   */          tfetch2D r4.xyz_, r0.zw, tf3
/*    0.1 */       exec
/*    3   */          mul r3.xyz_, r3.xyzz, r3.xyzz
              +       log r5.x___, r_abs[4].x
/*    4   */          dp3 r6.x___, r1.xyzz, r1.xyzz
              +       log r5._y__, r_abs[4].y
/*    5   */          dp3 r6._y__, r2.xyzz, r2.xyzz
              +       log r5.__z_, r_abs[4].z
/*    6   */          mul r5.xyz_, r5.xyzz, c21.wwww
              +       rsq r6.x___, r_abs[6].x
/*    7   */          mul r1.xyz_, r1.xyzz, r6.xxxx
              +       rsq r6._y__, r_abs[6].y
/*    8   */          mul r2.xyz_, r2.xyzz, r6.yyyy
              +       exp r5.x___, r5.x
/*    9   */          dp3_sat r7.x___, r1.xyzz, r2.xyzz
              +       exp r5._y__, r5.y
/*   10   */          dp3_sat r7._y__, r1.xyzz, c80.xyzz
              +       exp r5.__z_, r5.z
/*    1.0 */       exec
/*   11   */          mul r7.x___, r7.xxxx, r7.xxxx
/*   12   */          add r8.xyz_, -c22.xyzz, c255.xxxx
/*   13   */          mad r8.xyz_, r7.xxxx, r8.xyzz, c22.xyzz
/*   14   */          mul r5.xyz_, r5.xyzz, c21.xyzz
/*   15   */          mad r5.xyz_, r5.xyzz, r8.xyzz, c20.xyzz
/*   16   */          mul r5.xyz_, r5.xyzz, c11.xxxx
/*    1.1 */       exece
/*   17   */          mad r5.xyz_, r7.yyyy, c87.xyzz, r5.xyzz
/*   18   */          mul r5.xyz_, r5.xyzz, r3.xyzz
/*   19   */          mul oC0.xyz1, r5.xyzz, c20.wwww
"""


def synthetic_case(rng, normal=None):
    """One random pixel of SYNTHETIC: what the microcode gets and what the formula gets."""
    consts = {11: [rng.uniform(0.3, 1.0)] * 4,
              20: [rng.uniform(0.0, 0.2) for _ in range(3)] + [rng.choice([1.0, 16.0])],
              21: [rng.uniform(0.5, 2.0) for _ in range(3)] + [rng.choice([1.3, 2.0])],
              22: [rng.uniform(0.02, 0.5) for _ in range(3)] + [0.0],
              80: lc.unit(rng) + [0.0],
              87: [rng.uniform(0.2, 2.0) for _ in range(3)] + [0.0],
              255: [1.0, 0.0, 0.0, 0.0]}
    regs = {0: [rng.random() for _ in range(4)],
            1: [rng.uniform(-1, 1) for _ in range(3)] + [0.0],
            2: [rng.uniform(-1, 1) for _ in range(3)] + [0.0]}
    if normal is not None:
        regs[1] = list(normal) + [0.0]
    albedo = lc.tex(0, tuple(regs[0][:2]))
    base = lc.tex(3, tuple(regs[0][2:]))
    n = lf.normalize(regs[1][:3])
    args = dict(A=[x * x for x in albedo[:3]], baked=base[:3], tint=consts[21][:3], tint_pow=consts[21][3],
                add=consts[20][:3], floor=consts[22][:3], t=lf.ambient_t(regs[2][:3], regs[1][:3]),
                n_dot_sun=lf.sat(lf.dot(n, consts[80][:3])), sun_col=consts[87][:3], c11x=consts[11][0])
    return lc.Case(consts=consts, regs=regs, sampler=lambda slot, coords, attrs: lc.tex(slot, coords),
                   args=args, scale=[consts[20][3]] * 3)


# The three wrong formulas of the brief, as wrappers of the right one.
def wrong_exponent(**a):
    """L with t in the place of t^2 (ambient_factor is given sqrt(t))."""
    return lf.terrain_layer(**dict(a, t=math.sqrt(a["t"])))


def missing_c11x(**a):
    return lf.terrain_layer(**dict(a, c11x=1.0))


def swapped_floor(**a):
    f = a["floor"]
    return lf.terrain_layer(**dict(a, floor=[f[1], f[0], f[2]]))


def wrong_tint_pow(**a):
    return lf.terrain_layer(**dict(a, tint_pow=a["tint_pow"] + 0.1))


class Formulas(unittest.TestCase):
    def test_ambient_factor(self):
        # floor + (1 - floor) * t^2 per channel; t clamped to [0, 1].
        self.assertEqual(lf.ambient_factor([0.0, 0.5, 1.0], 0.5), [0.25, 0.625, 1.0])
        self.assertEqual(lf.ambient_factor([0.2, 0.2, 0.2], -3.0), [0.2, 0.2, 0.2])
        self.assertEqual(lf.ambient_factor([0.2, 0.2, 0.2], 7.0), [1.0, 1.0, 1.0])

    def test_terrain_layer_by_hand(self):
        # A * c11x * (baked^p * tint * L + add) + A * sun_col * n_dot_sun * sun_gate, with p = 2, t = 0.5:
        # L = 0.25 + 0.75 * 0.25 = 0.4375; channel 0: 0.5 * 0.5 * (0.25 * 2 * 0.4375 + 0.1) + 0.5 * 3 * 0.5 * 0.5
        got = lf.terrain_layer(A=[0.5] * 3, baked=[0.5] * 3, tint=[2.0] * 3, tint_pow=2.0, add=[0.1] * 3,
                               floor=[0.25] * 3, t=0.5, n_dot_sun=0.5, sun_col=[3.0] * 3, c11x=0.5, sun_gate=0.5)
        self.assertAlmostEqual(got[0], 0.25 * (0.21875 + 0.1) + 0.375, places=12)
        self.assertEqual(got[0], got[1])
        self.assertEqual(got[1], got[2])

    def test_terrain_far_by_hand(self):
        # F * (baked^p * tint * L + add) + F * c11x * sun_col * n_dot_sun: c11x scales the sun, not the ambient.
        got = lf.terrain_far(F=[0.5] * 3, baked=[0.5] * 3, tint=[2.0] * 3, tint_pow=2.0, add=[0.1] * 3,
                             floor=[0.25] * 3, t=0.5, n_dot_sun=0.5, sun_col=[3.0] * 3, c11x=0.5)
        self.assertAlmostEqual(got[0], 0.5 * (0.21875 + 0.1) + 0.5 * 0.5 * 3.0 * 0.5, places=12)

    def test_lit_object_by_hand(self):
        # A * c27w * (vcolour^p * tint * L + add) + A * sun_col * n_dot_sun: c27w is on the ambient part only.
        got = lf.lit_object(A=[0.5] * 3, vcolour=[0.5] * 3, tint=[2.0] * 3, tint_pow=2.0, add=[0.1] * 3,
                            floor=[0.25] * 3, t=0.5, n_dot_sun=0.5, sun_col=[3.0] * 3, c27w=0.5)
        self.assertAlmostEqual(got[0], 0.5 * 0.5 * (0.21875 + 0.1) + 0.5 * 3.0 * 0.5, places=12)

    def test_ambient_axis_and_t(self):
        # lerp(N, C, (1 + N . C) / 2): N = (0, 0, 1), C = (1, 0, 0): k = 0.5, axis (0.5, 0, 0.5); t = cos 45.
        axis = lf.ambient_axis([0.0, 0.0, 1.0], [1.0, 0.0, 0.0])
        self.assertEqual(axis, [0.5, 0.0, 0.5])
        self.assertAlmostEqual(lf.ambient_t(axis, [0.0, 0.0, 2.0]), math.sqrt(0.5), places=12)
        # N = C: the axis is N and t is 1; N = -C: k = 0 and the axis is N again.
        self.assertEqual(lf.ambient_axis([0.0, 1.0, 0.0], [0.0, 1.0, 0.0]), [0.0, 1.0, 0.0])
        self.assertEqual(lf.ambient_axis([0.0, 1.0, 0.0], [0.0, -1.0, 0.0]), [0.0, 1.0, 0.0])

    def test_sun_gates(self):
        self.assertEqual(lf.facing_gate(0.0625), 0.5)                     # sat(8 * 0.0625)
        self.assertEqual(lf.facing_gate(0.5), 1.0)
        self.assertEqual(lf.facing_gate(-0.5), 0.0)
        self.assertEqual(lf.sun_gate_single(), 1.0)
        self.assertEqual(lf.sun_gate_single(mask=0.5, facing=0.5), 0.25)
        # shadow = (1 - 0.5 * 1) * 0.5 = 0.25; a light without cone or range (blend 0): 1 - shadow.
        self.assertEqual(lf.sun_gate_multi(0.5, facing=0.5), 0.75)
        # strength 1 and blend 0 is the one-light gate.
        self.assertEqual(lf.sun_gate_multi(1.0, mask=0.5, facing=0.5), lf.sun_gate_single(mask=0.5, facing=0.5))
        # blend 1: times cone * range = sat((0.9 - 0.5) * 2) * sat(1 - 10 * 0.05) = 0.8 * 0.5.
        self.assertAlmostEqual(lf.sun_gate_multi(1.0, spot_blend=1.0, distance=10.0, range_scale=0.05,
                                                 cone_dot=0.9, cone_cos=0.5, cone_scale=2.0), 0.4, places=12)

    def test_vertex_side_helpers_by_hand(self):
        # pow(|c|, p) per channel.
        self.assertEqual(lf.vertex_colour([0.5, 1.0, 0.0, 0.25], 2.0), [0.25, 1.0, 0.0, 0.0625])
        # base + (row . weights) per channel, four-component dot products.
        self.assertEqual(lf.probe_colour([0.1, 0.2, 0.3], [[1, 0, 0, 0], [0, 2, 0, 0], [0, 0, 0, 4]], [0.5, 0.25, 9.0, 0.125]),
                         [0.6, 0.7, 0.8])
        # pow(c, p) * tint + add.
        self.assertEqual(lf.instance_ambient([0.5, 0.5, 0.5], [2.0, 4.0, 8.0], 2.0, [0.25, 0.0, 1.0]), [0.75, 1.0, 3.0])
        # No bend at height 0 or strength 0; at height 1 and strength 1 the normal is the up axis.
        n, up = [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]
        self.assertEqual(lf.plant_normal(n, up, 0.0, 1.0), n)
        self.assertEqual(lf.plant_normal(n, up, 0.7, 0.0), n)
        self.assertEqual(lf.plant_normal(n, up, 1.0, 1.0), up)
        # height 0.5, strength 1: bent = normalize(0.5, 0, 0.5); N = normalize(lerp(n, bent, 0.5)).
        s = math.sqrt(0.5)
        want = lf.normalize([0.5 + 0.5 * s, 0.0, 0.5 * s])
        for got, w in zip(lf.plant_normal(n, up, 0.5, 1.0), want):
            self.assertAlmostEqual(got, w, places=12)

    def test_plant_and_leaf_by_hand(self):
        # A * amb + A * sun_col * n_dot_sun * gate.
        self.assertEqual(lf.plant([0.5] * 3, [0.25, 0.5, 1.0], 0.5, [2.0] * 3, sun_gate=0.5), [0.375, 0.5, 0.75])
        # The light faces the leaf (L . N = 0.5), the viewer too (V . N = 0.5), wrap 0.5:
        # front = (0.5 + 0.5) / 1.5; back = sat(-0.5) * ... = 0.
        front, back = lf.leaf_light([0.0, 0.0, 1.0], [math.sqrt(0.75), 0.0, 0.5], [0.0, math.sqrt(0.75), 0.5], 0.5)
        self.assertAlmostEqual(front, 2.0 / 3.0, places=12)
        self.assertEqual(back, 0.0)
        # The light behind the leaf (L . N = -0.5): front = sat(0 / 1.5) = 0; back = 0.5 * (1 - (1 - 0.5)^2).
        front, back = lf.leaf_light([0.0, 0.0, 1.0], [math.sqrt(0.75), 0.0, 0.5], [0.0, math.sqrt(0.75), -0.5], 0.5, spot=0.5)
        self.assertEqual(front, 0.0)
        self.assertAlmostEqual(back, 0.5 * 0.75 * 0.5, places=12)
        # A normal that faces away from the viewer is negated first: the same numbers.
        again = lf.leaf_light([0.0, 0.0, -1.0], [math.sqrt(0.75), 0.0, 0.5], [0.0, math.sqrt(0.75), -0.5], 0.5, spot=0.5)
        self.assertEqual(again, (front, back))
        self.assertEqual(lf.leaf_mask(0.5, 0.5, 0.5), (0.75, 0.875))
        # (A * (amb * c27w + m1 * sun * front) + green * A * m2 * sun * back) * c49, green texel:
        got = lf.tree_leaf(A=[0.25, 0.5, 0.25], vs_ambient=[0.5] * 3, front=0.5, back=0.25, sun_col=[2.0] * 3, c27w=0.5,
                           m1=0.5, m2=0.25, c49=[2.0, 1.0, 1.0])
        self.assertEqual(got, [0.4375, 0.4375, 0.21875])
        # A texel with more red than green has no back light.
        got = lf.tree_leaf(A=[0.5, 0.25, 0.25], vs_ambient=[0.5] * 3, front=0.5, back=0.25, sun_col=[2.0] * 3, c27w=0.5,
                           m1=0.5, m2=0.25)
        self.assertEqual(got, [0.375, 0.1875, 0.1875])

    def test_zeros_and_non_finite_inputs_give_finite_numbers(self):
        """Review focus 2: a zero-length normal, a zero colour, a non-finite constant and the power of a
        negative never give NaN or infinity (in the microcode they would: rsq(0) is infinity)."""
        nan, inf = float("nan"), float("inf")
        self.assertEqual(lf.normalize([0.0, 0.0, 0.0]), [0.0, 0.0, 0.0])
        self.assertEqual(lf.normalize([nan, 1.0, 0.0]), [0.0, 0.0, 0.0])
        self.assertEqual(lf.ambient_t([0.0, 0.0, 0.0], [0.0, 0.0, 1.0]), 0.0)
        self.assertEqual(lf.ambient_t([0.0, 0.0, 1.0], [inf, 0.0, 0.0]), 0.0)
        self.assertEqual(lf.powc([0.0, -0.25, nan], 2.0), [0.0, 0.0625, 0.0])   # |x|^p as the microcode's log of r_abs
        self.assertEqual(lf.powc([0.5, 0.5, 0.5], nan), [0.0, 0.0, 0.0])
        self.assertEqual(lf.ambient_factor([0.1, 0.1, 0.1], nan), [0.1, 0.1, 0.1])
        for fn in (lf.terrain_layer, lf.terrain_far):
            name = "F" if fn is lf.terrain_far else "A"
            got = fn(**{name: [0.0, 1.0, inf]}, baked=[0.0, -1.0, nan], tint=[1.0, nan, 1.0], tint_pow=1.3,
                     add=[0.0, 0.0, inf], floor=[0.0, nan, 0.0], t=nan, n_dot_sun=inf, sun_col=[nan, 1.0, 1.0],
                     c11x=nan)
            self.assertTrue(all(math.isfinite(x) for x in got), got)
        got = lf.lit_object(A=[1.0] * 3, vcolour=[0.0] * 3, tint=[1.0] * 3, tint_pow=1.3, add=[0.0] * 3,
                            floor=[0.0] * 3, t=0.0, n_dot_sun=0.0, sun_col=[1.0] * 3)
        self.assertEqual(got, [0.0, 0.0, 0.0])
        bad = [nan, inf, -inf]
        zero = [0.0, 0.0, 0.0]
        results = [
            lf.lit_object(A=bad, vcolour=bad, tint=bad, tint_pow=inf, add=bad, floor=bad, t=inf, n_dot_sun=nan,
                          sun_col=bad, c27w=nan, sun_gate=inf, c49=bad),
            lf.plant(bad, bad, nan, bad, sun_gate=nan),
            lf.tree_leaf(bad, bad, nan, inf, bad, c27w=inf, m1=nan, m2=inf, c49=bad),
            lf.instance_ambient(bad, bad, 1e308, bad), lf.instance_ambient([1e300] * 3, [1.0] * 3, 1e300, zero),
            lf.probe_colour(bad, [bad + [nan]] * 3, bad + [nan]), lf.vertex_colour(bad + [0.0], nan),
            lf.plant_normal(zero, zero, nan, inf), lf.plant_normal(bad, bad, 0.5, 1.0),
            lf.ambient_axis(bad, bad), list(lf.leaf_light(bad, bad, bad, nan, spot=inf)),
            list(lf.leaf_light(zero, zero, zero, -1.0)), list(lf.leaf_mask(nan, inf, nan)),
            [lf.facing_gate(nan), lf.facing_gate(inf, k=nan), lf.sun_gate_single(nan, inf),
             lf.sun_gate_multi(nan, mask=nan, facing=inf, spot_blend=inf, distance=nan, range_scale=inf, cone_dot=nan,
                               cone_cos=inf, cone_scale=nan)],
        ]
        for got in results:
            self.assertTrue(all(math.isfinite(x) for x in got), got)


class SyntheticDump(unittest.TestCase):
    """Always runs: the checker on a hand-written shader of the terrain layers' form."""

    def check(self, formula):
        return lc.check(SYNTHETIC, synthetic_case, formula, trials=60, seed=7)

    def test_the_right_formula_agrees_in_every_trial(self):
        res = self.check(lf.terrain_layer)
        self.assertEqual((res.agree, res.trials), (60, 60), res.first_bad)
        self.assertLess(res.worst, 1e-9)
        self.assertTrue(res.ok)

    def test_a_wrong_exponent_fails(self):
        res = self.check(wrong_exponent)       # agrees only where t is 0 or 1 (the normal faces away)
        self.assertFalse(res.ok)
        self.assertLess(res.agree, 45)
        res = self.check(wrong_tint_pow)
        self.assertFalse(res.ok)

    def test_a_missing_c11x_fails(self):
        res = self.check(missing_c11x)
        self.assertFalse(res.ok)
        self.assertEqual(res.agree, 0)

    def test_a_swapped_floor_channel_fails(self):
        res = self.check(swapped_floor)
        self.assertFalse(res.ok)
        self.assertIsNotNone(res.first_bad)

    def test_no_trials_is_not_agreement(self):
        self.assertFalse(lc.check(SYNTHETIC, synthetic_case, lf.terrain_layer, trials=0, seed=7).ok)

    def test_a_non_finite_microcode_result_is_a_failure(self):
        def infinite_sun(rng):
            case = synthetic_case(rng)
            case.consts[87] = [math.inf, math.inf, math.inf, 0.0]     # the formula's guard reads it as 0
            return case
        res = lc.check(SYNTHETIC, infinite_sun, lf.terrain_layer, trials=5, seed=7)
        self.assertFalse(res.ok)
        self.assertEqual(res.agree, 0)

    def test_a_zero_length_normal_agrees_with_the_guards(self):
        """rsq(0) is infinity and 0 * infinity NaN in the microcode; its saturating dot products turn NaN
        into 0 (the interpreter's rule for `_sat`), which is what the reference's guards give: t = 0 and
        no sun."""
        res = lc.check(SYNTHETIC, lambda rng: synthetic_case(rng, normal=[0.0, 0.0, 0.0]), lf.terrain_layer,
                       trials=20, seed=7)
        self.assertTrue(res.ok, res.first_bad)


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class RealDumps(unittest.TestCase):
    TRIALS = 24

    def test_every_listed_pair_agrees(self):
        for family, pairs in lc.FAMILIES.items():
            for ps, vs in pairs:
                with self.subTest(family=family, ps=ps, vs=vs):
                    res = lc.check_pair(family, ps, vs, trials=self.TRIALS)
                    self.assertTrue(res.ok, f"{res.agree}/{res.trials} worst {res.worst:.3g} {res.first_bad}")
                    self.assertEqual(res.trials, self.TRIALS)

    def test_the_eight_terrain_shaders_are_listed(self):
        terrain = {ps for fam in ("terrain-layer", "terrain-layer-multi", "terrain-far") for ps, _ in lc.FAMILIES[fam]}
        self.assertEqual(terrain, {"9C2144E42E95A76F", "4755A2E4A05BA678", "45243B758F9272AB", "AF1E16EE57E4BB06",
                                   "04F23292B2F44077", "578342593A37DD35", "7DDAD5C29B3F56EE", "2F8A0341E8C09698"})

    def test_wrong_formulas_fail_on_the_real_terrain_dumps(self):
        for ps, vs in lc.FAMILIES["terrain-layer"] + lc.FAMILIES["terrain-layer-multi"]:
            family = lc.family_of(ps, vs)
            for wrong in (wrong_exponent, missing_c11x, swapped_floor):
                with self.subTest(ps=ps, wrong=wrong.__name__):
                    res = lc.check_pair(family, ps, vs, trials=self.TRIALS, formula=wrong)
                    self.assertFalse(res.ok)
                    self.assertIsNotNone(res.first_bad)

    def test_wrong_formulas_fail_on_the_far_terrain_dumps(self):
        def far_wrong_exponent(**a):
            return lf.terrain_far(**dict(a, t=math.sqrt(a["t"])))

        def far_c11x_on_the_ambient(**a):      # the layers' place for c11.x
            return lf.terrain_layer(A=a["F"], **{k: v for k, v in a.items() if k != "F"})

        def far_swapped_floor(**a):
            f = a["floor"]
            return lf.terrain_far(**dict(a, floor=[f[1], f[0], f[2]]))

        for ps, vs in lc.FAMILIES["terrain-far"]:
            for wrong in (far_wrong_exponent, far_c11x_on_the_ambient, far_swapped_floor):
                with self.subTest(ps=ps, wrong=wrong.__name__):
                    self.assertFalse(lc.check_pair("terrain-far", ps, vs, trials=self.TRIALS, formula=wrong).ok)

    def test_wrong_object_formulas_fail(self):
        def c27w_on_the_sun_too(**a):
            a = dict(a)
            w = a.pop("c27w")
            return lf.lit_object(**dict(a, A=[x * w for x in a["A"]]))

        def vcolour_without_the_power(**a):
            return lf.lit_object(**dict(a, tint_pow=1.0))

        for ps, vs in lc.FAMILIES["object"][:2]:
            for wrong in (c27w_on_the_sun_too, vcolour_without_the_power):
                with self.subTest(ps=ps, wrong=wrong.__name__):
                    self.assertFalse(lc.check_pair("object", ps, vs, trials=self.TRIALS, formula=wrong).ok)

    def test_a_wrong_vertex_side_formula_fails(self):
        """The closed formulas of what the vertex shaders compute (the vector of t, the vertex and probe
        colours, the plants' bent normal and ambient, the leaves' two light scalars) and of the sun's gate
        are checked through the pair: with one of them replaced by a plausible wrong form, every pair that
        uses it disagrees."""
        ECD6, PROBE = "ECD66A10092E6562", "79EAC49585797037"
        right = {name: getattr(lf, name) for name in ("instance_ambient", "leaf_light", "sun_gate_single")}
        wrong = [
            # (helper, wrong form, family, vertex shader or None for every pair of the family)
            ("ambient_axis", lambda normal, axis: list(normal), "terrain-layer", None),
            ("ambient_axis", lambda normal, axis: list(axis), "object", None),
            ("ambient_axis", lambda normal, axis: list(normal), "plant-lit", None),
            ("ambient_axis", lambda normal, axis: list(normal), "tree-trunk", None),
            ("facing_gate", lambda n_dot_c109, k=8.0: 1.0, "terrain-layer", None),
            ("facing_gate", lambda n_dot_c109, k=8.0: lf.sat(n_dot_c109), "terrain-layer-multi", None),
            ("sun_gate_multi", lambda strength, mask=1.0, facing=1.0, **cone: right["sun_gate_single"](mask, facing),
             "terrain-layer-multi", None),
            ("sun_gate_multi", lambda strength, mask=1.0, facing=1.0, **cone: right["sun_gate_single"](mask, facing),
             "object-multi", None),
            ("sun_gate_multi", lambda strength, mask=1.0, facing=1.0, **cone: right["sun_gate_single"](mask, facing),
             "plant-multi", None),
            ("sun_gate_single", lambda mask=1.0, facing=1.0: facing, "object", None),
            ("sun_gate_single", lambda mask=1.0, facing=1.0: facing, "plant", None),
            ("vertex_colour", lambda colour, power: list(colour), "object", ECD6),
            ("vertex_colour", lambda colour, power: lf.powc(colour[::-1], power), "object", ECD6),
            ("probe_colour", lambda base, rows, weights: list(base), "object", PROBE),
            ("probe_colour", lambda base, rows, weights: [base[i] + lf.dot(rows[i][:3], weights[:3]) for i in range(3)],
             "tree-trunk", None),
            ("plant_normal", lambda normal, up, height, strength: list(normal), "plant", None),
            ("plant_normal", lambda normal, up, height, strength: lf.normalize(
                [n + (u - n) * height for n, u in zip(normal, up)]), "plant-lit", None),
            ("instance_ambient", lambda colour, tint, power, add: right["instance_ambient"](colour, tint, power, [0.0] * 3),
             "plant", None),
            ("instance_ambient", lambda colour, tint, power, add: right["instance_ambient"](colour, tint, power * 2.2, add),
             "tree-leaf", None),
            ("leaf_light", lambda normal, view, light_dir, wrap, spot=1.0: right["leaf_light"](normal, view, light_dir, 0.0, spot),
             "tree-leaf", None),
            ("leaf_light", lambda normal, view, light_dir, wrap, spot=1.0: right["leaf_light"](
                normal, [-x for x in view], light_dir, wrap, spot), "tree-leaf", None),
            ("leaf_mask", lambda mask, gain, bias: (mask * gain + bias,) * 2, "tree-leaf", None),
        ]
        for helper, form, family, only_vs in wrong:
            pairs = [(ps, vs) for ps, vs in lc.FAMILIES[family] if only_vs in (None, vs)]
            self.assertTrue(pairs, (helper, family))
            for ps, vs in pairs:
                with self.subTest(helper=helper, family=family, ps=ps, vs=vs):
                    with mock.patch.object(lf, helper, form):
                        res = lc.check_pair(family, ps, vs, trials=self.TRIALS)
                    self.assertFalse(res.ok, f"{res.agree}/{res.trials} agree with a wrong {helper}")

    def test_wrong_pixel_side_formulas_fail_for_plants_and_leaves(self):
        def plant_with_the_ambient_factor(**a):       # the objects' L applied where the shader has none
            return [x * 0.5 for x in lf.plant(**a)]

        def leaf_without_the_green_test(**a):
            return lf.tree_leaf(**dict(a, A=[a["A"][0], max(a["A"][0], a["A"][1]), a["A"][2]]))

        def leaf_c27w_on_the_sun(**a):
            return lf.tree_leaf(**dict(a, front=a["front"] * a["c27w"]))

        for family, wrong in (("plant", plant_with_the_ambient_factor), ("plant-multi", plant_with_the_ambient_factor),
                              ("tree-leaf", leaf_without_the_green_test), ("tree-leaf", leaf_c27w_on_the_sun)):
            for ps, vs in lc.FAMILIES[family]:
                with self.subTest(family=family, ps=ps, wrong=wrong.__name__):
                    self.assertFalse(lc.check_pair(family, ps, vs, trials=self.TRIALS, formula=wrong).ok)

    def test_command_line(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = lc.main(["--family", "terrain-far", "--trials", "8"])
        self.assertEqual(code, 0)
        self.assertIn("7DDAD5C29B3F56EE", out.getvalue())
        self.assertIn("8/8", out.getvalue())
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(lc.main(["--family", "terrain-layer", "--shader", "9C2144E42E95A76F", "--trials", "8"]), 0)
            self.assertNotEqual(lc.main(["--family", "terrain-far", "--shader", "9C2144E42E95A76F"]), 0)


class Coverage(unittest.TestCase):
    def test_rows_are_counted_by_what_was_checked(self):
        def draw(ps, vs, in_scene=True, frame=1):
            return {"kind": "draw", "frame": frame, "in_scene": in_scene, "ps_hash": "0x" + ps, "vs_hash": "0x" + vs}

        rows = [
            draw("8D900846800943C8", "ECD66A10092E6562"),
            draw("8D900846800943C8", "ECD66A10092E6562", frame=2),
            draw("8D900846800943C8", "ECD66A10092E6562", in_scene=False),          # a shadow or reflection pass
            draw("7CD57B81550F19E3", "D7DD31B9BF194DE8"),                          # its literals are in no capture
            draw("014F8A02DB7B19CA", "7C5710DEF3EE33C4"),                          # fur shells
            {"kind": "tess", "frame": 2, "in_scene": True, "ps_hash": "0x9C2144E42E95A76F",
             "vs": {"hash": "0xC30A97D946FA2BE4"}},
            {"kind": "raw", "frame": 2, "in_scene": True},
            {"kind": "texture"},
        ]
        frames, by_label, by_pair = lc.coverage(rows)
        self.assertEqual(frames, 2)
        self.assertEqual(by_label, {"object": 2, "tree-leaf, vertex shader not run": 1, "no family": 1,
                                    "terrain-layer": 1})
        self.assertEqual(by_pair[("object", "8D900846800943C8", "ECD66A10092E6562")], 2)
        self.assertEqual(lc.classify("8D900846800943C8", "ECD66A10092E6562"), ("object", True))
        self.assertEqual(lc.classify("8D900846800943C8", "0000000000000000"), ("object, vertex shader not run", False))

    def test_every_pair_is_in_one_family_and_has_a_driver(self):
        seen = {}
        for family, pairs in lc.FAMILIES.items():
            self.assertIn(family, lc.DRIVERS)
            self.assertIn(family, lc.FORMULAS)
            for pair in pairs:
                self.assertNotIn(pair, seen, f"{pair} is in {seen.get(pair)} and {family}")
                seen[pair] = family
                self.assertEqual(lc.family_of(*pair), family)
        with self.assertRaises(KeyError):
            lc.family_of("8D900846800943C8", "0000000000000000")


class CommandLineWithoutDumps(unittest.TestCase):
    def test_a_missing_dump_folder_is_a_failure_not_a_pass(self):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertNotEqual(lc.main(["--all", "--dumps", str(ROOT / "out" / "no_such_dump_folder")]), 0)

    def test_tex_is_the_same_in_every_process(self):
        # Not Python's salted hash: a failing trial can be reproduced from its seed.
        self.assertEqual(lc.tex(3, (0.25, 0.5)), lc.tex(3, (0.25, 0.5)))
        rng = random.Random(lc.tex_seed(3, (0.25, 0.5)))
        self.assertEqual(lc.tex(3, (0.25, 0.5)), [rng.uniform(0.05, 1.0) for _ in range(4)])
        self.assertEqual(lc.tex_seed(3, (0.25, 0.5)), 3875375324)


if __name__ == "__main__":
    unittest.main()
