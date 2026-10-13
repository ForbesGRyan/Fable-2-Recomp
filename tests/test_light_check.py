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
    consts = {11: [rng.uniform(0.3, 1.0) for _ in range(4)],
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
                n_dot_sun=lf.sat(lf.dot(n, consts[80][:3])), sun_col=consts[87][:3], c11x=consts[11][0],
                sun_gate=1.0)       # the synthetic shader has no mask and no facing factor
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
                            floor=[0.25] * 3, t=0.5, n_dot_sun=0.5, sun_col=[3.0] * 3, c27w=0.5, c49=[1.0, 2.0, 0.5])
        base = 0.5 * 0.5 * (0.21875 + 0.1) + 0.5 * 3.0 * 0.5
        self.assertAlmostEqual(got[0], base, places=12)
        self.assertAlmostEqual(got[1], base * 2.0, places=12)       # c49 scales the whole colour, per channel
        self.assertAlmostEqual(got[2], base * 0.5, places=12)

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
        # (the spot is on the front light too)
        front, back = lf.leaf_light([0.0, 0.0, 1.0], [math.sqrt(0.75), 0.0, 0.5], [0.0, math.sqrt(0.75), 0.5], 0.5, spot=0.5)
        self.assertAlmostEqual(front, 1 / 3, places=12)
        self.assertEqual(back, 0.0)
        self.assertEqual(lf.leaf_mask(0.5, 0.5, 0.5), (0.75, 0.875))
        # (A * (amb * c27w + m1 * sun * front) + green * A * m2 * sun * back) * c49, green texel:
        got = lf.tree_leaf(A=[0.25, 0.5, 0.25], vs_ambient=[0.5] * 3, front=0.5, back=0.25, sun_col=[2.0] * 3, c27w=0.5,
                           m1=0.5, m2=0.25, c49=[2.0, 1.0, 1.0])
        self.assertEqual(got, [0.4375, 0.4375, 0.21875])
        # A texel with more red than green has no back light.
        got = lf.tree_leaf(A=[0.5, 0.25, 0.25], vs_ambient=[0.5] * 3, front=0.5, back=0.25, sun_col=[2.0] * 3, c27w=0.5,
                           m1=0.5, m2=0.25, c49=[1.0] * 3)
        self.assertEqual(got, [0.375, 0.1875, 0.1875])

    def test_open_inputs_have_no_default(self):
        """Fix round 1: an input whose value is open or in no capture row must be given by the caller; a
        default would be arithmetic no shader computes. The error names the input."""
        layer = dict(A=[0.5] * 3, baked=[0.5] * 3, tint=[2.0] * 3, tint_pow=2.0, add=[0.1] * 3, floor=[0.25] * 3,
                     t=0.5, n_dot_sun=0.5, sun_col=[3.0] * 3)
        with self.assertRaisesRegex(TypeError, "sun_gate"):         # the layers' facing factor and mask
            lf.terrain_layer(**layer)
        self.assertEqual(len(lf.terrain_layer(**layer, sun_gate=1.0)), 3)        # c11x may default (1 at layer rows)
        obj = dict(layer, vcolour=layer["baked"])
        del obj["baked"]
        with self.assertRaisesRegex(TypeError, "c49"):
            lf.lit_object(**obj)
        self.assertEqual(len(lf.lit_object(**obj, c49=[1.0] * 3)), 3)             # c27w and the mask may default
        leaf = dict(A=[0.25, 0.5, 0.25], vs_ambient=[0.5] * 3, front=0.5, back=0.25, sun_col=[2.0] * 3,
                    m1=0.5, m2=0.25, c49=[1.0] * 3)
        for name in ("m1", "m2", "c49"):
            with self.assertRaisesRegex(TypeError, name):
                lf.tree_leaf(**{k: v for k, v in leaf.items() if k != name})
        self.assertEqual(len(lf.tree_leaf(**leaf)), 3)
        # Positional use cannot slip an open input in by its place either.
        with self.assertRaises(TypeError):
            lf.tree_leaf(leaf["A"], leaf["vs_ambient"], 0.5, 0.25, leaf["sun_col"], 1.0, 0.5, 0.25, [1.0] * 3)

    def test_leaf_mask_is_finite_for_a_huge_gain(self):
        # m1 = 1 * 1e200 is finite, m2 = m1 * 1e200 overflows: the guard gives a finite number.
        for got in (lf.leaf_mask(1.0, 1e200, 0.0), lf.leaf_mask(1.0, 1e308, 1e308), lf.leaf_mask(0.5, -1e200, 1e308)):
            self.assertTrue(all(math.isfinite(x) for x in got), got)
        self.assertEqual(lf.leaf_mask(1.0, 1e200, 0.0)[0], 1e200)

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
        for fn, more in ((lf.terrain_layer, dict(A=[0.0, 1.0, inf], sun_gate=nan)), (lf.terrain_far, dict(F=[0.0, 1.0, inf]))):
            got = fn(baked=[0.0, -1.0, nan], tint=[1.0, nan, 1.0], tint_pow=1.3, add=[0.0, 0.0, inf],
                     floor=[0.0, nan, 0.0], t=nan, n_dot_sun=inf, sun_col=[nan, 1.0, 1.0], c11x=nan, **more)
            self.assertTrue(all(math.isfinite(x) for x in got), got)
        got = lf.lit_object(A=[1.0] * 3, vcolour=[0.0] * 3, tint=[1.0] * 3, tint_pow=1.3, add=[0.0] * 3,
                            floor=[0.0] * 3, t=0.0, n_dot_sun=0.0, sun_col=[1.0] * 3, c49=[1.0] * 3)
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


class PinnedForTheRenderer(unittest.TestCase):
    """Plan Task 7: numbers of the reference that the native renderer's pure mirrors are held against
    (tests/native/test_clay_logic.cpp, cases 420 to 449, has the same inputs and the same literals). Each
    case composes the functions as the clay shaders do: the vertex side makes the normal and the vector
    of t, the pixel side normalises, takes the two dot products and calls the family's function with the
    mask at 1. The frame values are rounded ones of a captured light block (the bridge, 2026-10-08)."""
    TINT, TINT_POW, ADD = [1.125, 1.375, 1.875], 1.3, [0.0068, 0.071, 0.092]
    FLOOR, SUN_COL = [0.0588, 0.0902, 0.0], [0.99, 0.83, 0.40]
    SUN_DIR, AXIS = [-0.6334, 0.0346, 0.7731], [-0.6332, 0.0344, 0.7732]
    A, BAKED = [0.36, 0.25, 0.16], [0.5, 0.6, 0.7]
    M_ROT = [[0.0, -1.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]]
    M_SKEW = [[1.0, 0.0, 0.0], [0.0, 2.0, 0.0], [0.0, 0.0, 0.5]]
    M_MIRROR = [[-1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]

    def close(self, got, want):
        self.assertEqual(len(got), len(want))
        for g, w in zip(got, want):
            self.assertAlmostEqual(g, w, places=12)

    def layer(self, xy, c11x, scale=8.0):
        n = [xy[0], xy[1], math.sqrt(abs(1.0 - xy[0] * xy[0] - xy[1] * xy[1]))]     # the vertex shader's normal
        big_n = lf.normalize(n)
        t = lf.ambient_t(lf.ambient_axis(n, self.AXIS), n)
        nds = lf.dot(big_n, self.SUN_DIR)
        gate = lf.sun_gate_single(1.0, lf.facing_gate(nds, scale))
        return [t, nds, gate] + lf.terrain_layer(self.A, self.BAKED, self.TINT, self.TINT_POW, self.ADD, self.FLOOR, t,
                                                 nds, self.SUN_COL, c11x, sun_gate=gate)

    def far(self, xy, c11x):
        big_n = lf.normalize([xy[0], xy[1], math.sqrt(abs(1.0 - xy[0] * xy[0] - xy[1] * xy[1]))])
        t, nds = lf.dot(big_n, self.AXIS), lf.dot(big_n, self.SUN_DIR)
        return [t, nds] + lf.terrain_far([0.3, 0.3, 0.2], self.BAKED, self.TINT, self.TINT_POW, self.ADD, self.FLOOR, t,
                                         nds, self.SUN_COL, c11x)

    def obj(self, n, m, rgb, c27w=1.0, c49=(1.0, 1.0, 1.0), c26=(1.0, 1.0, 1.0)):
        o1 = [lf.dot(row, n) for row in m]                                          # the game's (R n) * c7.w
        t = lf.ambient_t(lf.ambient_axis(o1, self.AXIS), o1)
        nds = lf.dot(lf.normalize(o1), self.SUN_DIR)
        vcolour = lf.vertex_colour([c / 255.0 for c in rgb], 2.2)
        c = lf.lit_object([0.5, 0.4, 0.3], vcolour, self.TINT, self.TINT_POW, self.ADD, self.FLOOR, t, nds, self.SUN_COL,
                          c27w, lf.sun_gate_single(1.0, 1.0), c49=list(c49))
        return [t, nds] + vcolour + [c[i] * c26[i] for i in range(3)]

    def test_shared_pieces(self):
        self.close(lf.ambient_factor(self.FLOOR, 0.5), [0.29410000000000003, 0.31765, 0.25])
        self.close(lf.ambient_axis([-0.25, 0.5, 0.75], self.AXIS), [-0.5863346399999999, 0.09134288000000004, 0.77036264])
        self.close(lf.powc([0.5, -0.25, 0.0], 1.3), [0.40612619817811774, 0.16493848884661177, 0.0])

    def test_terrain_layer(self):
        # Facing the sun: the facing factor is 1.
        self.close(self.layer((-0.25, 0.5), 1.0), [0.8464635611181413, 0.8166706563559398, 1.0,
                                                   0.41410209137988163, 0.31851544194361503, 0.20218335739892443])
        # Facing away: no sun, the ambient alone.
        self.close(self.layer((0.75, -0.5), 1.0), [0.7762633015448206, -0.15758788016712524, 0.0,
                                                   0.10540540036087104, 0.13071754433579474, 0.12842166302336305])
        # Grazing: sat(8 * (N . c109)) is between 0 and 1; c11.x 0.75 scales the ambient part only.
        self.close(self.layer((0.6, 0.55), 0.75), [0.6809123554006105, 0.08812051374294312, 0.704964109943545,
                                                   0.08506196574192819, 0.09415262382898765, 0.08062917350272386])
        # A map normal outside the unit disk (x^2 + y^2 > 1): z is the root of the absolute value.
        self.close(self.layer((0.9, 0.8), 1.0), [0.8191864712988252, -0.017243666506090518, 0.0,
                                                 0.11600703589238615, 0.14174207696615995, 0.14134348129986526])

    def test_terrain_far(self):
        self.close(self.far((-0.25, 0.5), 0.48), [0.8166035719756988, 0.8166706563559398,
                                                  0.21255202935748654, 0.26688275988662435, 0.20704296707043351])
        self.close(self.far((0.75, -0.5), 0.48), [-0.15729457889693604, -0.15758788016712524,
                                                  0.010099574402844745, 0.040452572541314194, 0.0184])

    def test_object(self):
        n = [-0.6, 0.0, 0.8]
        colour = [0.5859729839496614, 0.2195197180748679, 0.04777575355617064]      # pow((200, 128, 64) / 255, 2.2)
        self.close(self.obj(n, self.M_ROT, (200, 128, 64)),
                   [0.727362161936653, 0.59772] + colour + [0.4555938921109685, 0.2706276044156476, 0.10503584306025268])
        # c27.w on the ambient part, c49 on the whole colour, c26 after the function.
        self.close(self.obj(n, self.M_ROT, (200, 128, 64), c27w=0.5, c49=(1.0, 0.5, 2.0), c26=(1.0, 1.0, 0.5)),
                   [0.727362161936653, 0.59772] + colour + [0.3757326460554843, 0.1172676611039119, 0.08838112153012634])
        # A scale that is not uniform: the game's own normalize(M n), not the inverse transpose.
        self.close(self.obj(n, self.M_SKEW, (200, 128, 64)),
                   [0.9653736259006114, 0.9558593781353147] + colour + [0.7393441381594212, 0.41761079399085327, 0.1523604668465132])
        # A mirror: M n as it is, sign included.
        self.close(self.obj(n, self.M_MIRROR, (200, 128, 64)),
                   [0.660085140197664, 0.23844000000000004] + colour + [0.2530825970423436, 0.14484074306862768, 0.06091490419395856])

    def test_no_direction_and_no_colour(self):
        # A zero normal is no direction: no sun and t = 0, the ambient at its floor (blue has none).
        colour = [0.5859729839496614, 0.2195197180748679, 0.04777575355617064]
        self.close(self.obj([0.0, 0.0, 0.0], self.M_ROT, (200, 128, 64)),
                   [0.0, 0.0] + colour + [0.01990975864609052, 0.03531010782579682, 0.0276])
        # A black stream colour: the added light and the sun remain.
        self.close(self.obj([-0.6, 0.0, 0.8], self.M_ROT, (0, 0, 0)),
                   [0.727362161936653, 0.59772, 0.0, 0.0, 0.0, 0.2992714, 0.22684304, 0.09932640000000001])

    # Plan Task 8a: the probe colour (objects without a colour stream, trunks). The twelve registers
    # c28..c39 as three sets of four (base, then the rows of red, green, blue), the vertex's four weights,
    # and the set its position names: tests/native/test_clay_logic.cpp, cases 490 to 495, has the same.
    PROBE = [[0.25, 0.125, 0.0625, 9.0], [0.5, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 0.125, 1.0],
             [0.125, 0.25, 0.0, 9.0], [-0.25, 0.0, 0.0, 0.0], [0.0, 1.25, 0.0, 0.0], [0.0, 0.0, 0.0, 0.0],
             [0.0625, 0.5, 0.25, 9.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0], [1.0, 1.0, 1.0, 1.0]]
    WEIGHTS = [0.5, -0.25, 1.0, 0.125]

    def probe_obj(self, a0, c27w=1.0, c49=(1.0, 1.0, 1.0), c26=(1.0, 1.0, 1.0), vcolour=None):
        """A probe object as the clay shaders compose it: the vertex side makes the colour with no power,
        the pixel side hands it to the object function, which raises its absolute value to c21.w."""
        n, m = [-0.6, 0.0, 0.8], self.M_ROT
        o1 = [lf.dot(row, n) for row in m]
        t = lf.ambient_t(lf.ambient_axis(o1, self.AXIS), o1)
        nds = lf.dot(lf.normalize(o1), self.SUN_DIR)
        if vcolour is None:
            vcolour = lf.probe_colour(self.PROBE[a0][:3], self.PROBE[a0 + 1:a0 + 4], self.WEIGHTS)
        c = lf.lit_object([0.5, 0.4, 0.3], vcolour, self.TINT, self.TINT_POW, self.ADD, self.FLOOR, t, nds, self.SUN_COL,
                          c27w, lf.sun_gate_single(1.0, 1.0), c49=list(c49))
        return list(vcolour) + [c[i] * c26[i] for i in range(3)]

    def test_probe_object(self):
        # Set 0: base + (rows . weights), four-component dot products; the base's w is not read.
        self.close(self.probe_obj(0), [0.5, -0.125, 0.3125, 0.426458072138784, 0.24789964999454064, 0.16493019528946776])
        # Set 8, a colour above 1 (no clamp anywhere before the output), with c27.w, c49 and c26.
        self.close(self.probe_obj(8), [1.0625, 0.625, 1.625, 0.6381219686383413, 0.3974707094581953, 0.6587403749904683])
        self.close(self.probe_obj(8, c27w=0.5, c49=(1.0, 0.5, 2.0), c26=(1.0, 1.0, 0.5)),
                   [1.0625, 0.625, 1.625, 0.46699668431917063, 0.14897843736454883, 0.3652333874952342])

    def test_probe_colour_zero_and_negative(self):
        # Set 4: the weights cancel the base exactly in red (0: no baked term, the added light and the sun
        # remain, as for a black stream colour) and overshoot in green (-0.0625: the largest negative of
        # the captured meshes is -0.062). The game takes the logarithm of the ABSOLUTE value (`log r6.x,
        # r_abs[6].x` in 8D90..., instr 37 to 39; `r_abs[8]` in the trunks' 00E0...), so a negative colour
        # lights like its mirror image: no clamp at zero, no NaN.
        self.close(self.probe_obj(4), [0.0, -0.0625, 0.0, 0.2992714, 0.23539468096360217, 0.09932640000000001])
        # The three values of the review of plan Task 5b: exactly 0, a rounding residue of either sign,
        # the genuinely negative one.
        want = [0.2992714, 0.2268430400125143, 0.10700391302815564]
        self.close(self.probe_obj(0, vcolour=[0.0, -1e-8, -0.06]), [0.0, -1e-8, -0.06] + want)
        self.close(self.probe_obj(0, vcolour=[0.0, 1e-8, 0.06]), [0.0, 1e-8, 0.06] + want)
        self.close(lf.powc([0.0, -1e-8, -0.06], 1.3), [0.0, 3.9810717055349695e-11, 0.025798628902309793])

    # Plan Task 8b: the instanced plants. The clay vertex shader makes, per vertex, the height blend
    # h = sat(z * c45.y + c45.z), the bend (c45.x * normalize(lerp(N, U, h)), c45.x * h) and the colour
    # pow(instance colour, 2.2 * c21.w) * c21.xyz (family plant: + c20.xyz; the vertex copies c18 and c54
    # of the frame's block); the pixel shader makes Np = normalize(lerp(normalize(N), bend.xyz,
    # sat(bend.w))) and calls plant, or lit_object with c27.w = 1 and c49 = 1 (family plant-lit, with L
    # from the vertex side's axis and Np). tests/native/test_clay_logic.cpp, cases 500 to 509, has the same.
    C45 = [1.0, -2.0, 1.5, 0.0]               # h = sat(1.5 - 2 z): 1 up to z = 0.25, 0 from z = 0.75
    UP = [0.28, 0.0, 0.96]                    # a copy's up axis (unit), tilted
    PLANT_POW = 2.2 * 1.3                     # vertex c18.w = 2.2 * pixel c21.w

    def plant(self, n, z, rgb, c45=None, up=None):
        c45, up = c45 or self.C45, up or self.UP
        h = lf.sat(z * c45[1] + c45[2])
        normal = lf.plant_normal(n, up, h, c45[0])
        amb = lf.instance_ambient([c / 255.0 for c in rgb], self.TINT, self.PLANT_POW, self.ADD)
        nds = lf.dot(normal, self.SUN_DIR)
        return [h] + normal + amb + lf.plant([0.5, 0.4, 0.3], amb, nds, self.SUN_COL, lf.sun_gate_single(1.0, 1.0))

    def plant_lit(self, n, z, rgb, c45=None, up=None):
        c45, up = c45 or self.C45, up or self.UP
        h = lf.sat(z * c45[1] + c45[2])
        normal = lf.plant_normal(n, up, h, c45[0])
        t = lf.ambient_t(lf.ambient_axis(n, self.AXIS), normal)
        nds = lf.dot(normal, self.SUN_DIR)
        return [t, nds] + lf.lit_object([0.5, 0.4, 0.3], [c / 255.0 for c in rgb], self.TINT, self.PLANT_POW, self.ADD,
                                        self.FLOOR, t, nds, self.SUN_COL, 1.0, lf.sun_gate_single(1.0, 1.0), c49=[1.0, 1.0, 1.0])

    def test_plant(self):
        n = [0.6, 0.0, 0.8]                   # turned from the sun in x
        # Half way up: the normal half bent towards the copy's up axis.
        self.close(self.plant(n, 0.5, (200, 128, 64)), PLANT_PINS["half"])
        # At the foot (h = 1): the up axis itself. At the top (h = 0): the vertex normal.
        self.close(self.plant(n, 0.25, (200, 128, 64)), PLANT_PINS["foot"])
        self.close(self.plant(n, 0.75, (200, 128, 64)), PLANT_PINS["top"])
        # A strength of 0.5: the pixel side blends by sat(0.5 * h) towards HALF the bent normal.
        self.close(self.plant(n, 0.5, (200, 128, 64), c45=[0.5, -2.0, 1.5, 0.0]), PLANT_PINS["weak"])

    def test_plant_lit(self):
        n = [0.6, 0.0, 0.8]
        self.close(self.plant_lit(n, 0.5, (200, 128, 64)), PLANT_PINS["lit half"])
        self.close(self.plant_lit(n, 0.25, (200, 128, 64)), PLANT_PINS["lit foot"])
        self.close(self.plant_lit(n, 0.75, (200, 128, 64)), PLANT_PINS["lit top"])

    def test_plant_zero_inputs(self):
        n = [0.6, 0.0, 0.8]
        # c45.x == 0: the set does not bend, whatever the height says.
        self.close(self.plant(n, 0.25, (200, 128, 64), c45=[0.0, -2.0, 1.5, 0.0]), PLANT_PINS["unbent"])
        self.assertEqual(self.plant(n, 0.25, (200, 128, 64), c45=[0.0, -2.0, 1.5, 0.0])[1:4], n)
        self.close(self.plant_lit(n, 0.25, (200, 128, 64), c45=[0.0, 0.0, 0.0, 0.0]), PLANT_PINS["lit unbent"])
        # A negative strength blends by sat(negative) = 0: the vertex normal too.
        self.assertEqual(self.plant(n, 0.25, (200, 128, 64), c45=[-1.0, -2.0, 1.5, 0.0])[1:4], n)
        # An instance colour of 0: the power of zero is 0, the added light and the sun remain.
        self.close(self.plant(n, 0.5, (0, 0, 0)), PLANT_PINS["black"])
        self.close(self.plant_lit(n, 0.5, (0, 0, 0)), PLANT_PINS["lit black"])
        # A zero normal (a vertex whose normal has no direction): bent, it is the up axis where h > 0 ...
        self.close(self.plant([0.0, 0.0, 0.0], 0.5, (200, 128, 64)), PLANT_PINS["no normal"])
        # ... and no direction where nothing bends it: no sun; plant-lit: t = 0, the ambient at its floor.
        self.close(self.plant([0.0, 0.0, 0.0], 0.75, (200, 128, 64)), PLANT_PINS["no normal, top"])
        self.close(self.plant_lit([0.0, 0.0, 0.0], 0.75, (200, 128, 64)), PLANT_PINS["lit no normal, top"])
        # A copy without an up axis (a third column of zeros): the bent normal is the vertex normal's
        # direction (shortened by 1 - h before it is normalised).
        self.close(self.plant(n, 0.5, (200, 128, 64), up=[0.0, 0.0, 0.0]), PLANT_PINS["no up"])

    # Plan Task 8c: the tree leaves. The clay vertex shader makes, per vertex: N = M n as it is (the game
    # does not normalise it), the world position (M p) / c7.w + the rows' fourth components, the unit vector
    # to the eye, the two scalars of the sun (leaf_light with the frame's sun direction, normalised, the
    # wrap c50.w and no cone or range: the sun's slot has c90 = 0), the probe colour, and the ambient
    # pow(|probe colour|, 2.2 * c21.w * 0.4545) * c21.xyz + c20.xyz (the vertex copies c18 and c54 of the
    # frame's block; 0.4545 the vertex shaders' literal). The pixel shader multiplies the two scalars by
    # the sun's colour (the game's vertex shader does: the product commutes with the interpolation) and
    # calls tree_leaf with the mask at 1 (leaf_mask(1, 0.6, 0.4) = (1, 1)), then c26.
    # tests/native/test_clay_logic.cpp, cases 520 to 529, has the same.
    LEAF_GAMMA = 0.45454543828964233          # literal c249.w of A584..., c248.w of D7DD..., c255.x of 2D40...
    LEAF_T = [1.0, 2.0, 3.0]                  # the fourth components of c4..c6
    LEAF_P = [0.5, -1.0, 2.0]                 # a vertex position in the mesh's space
    EYE_FRONT, EYE_BEHIND = [1.5, -10.0, 13.0], [0.0, 12.0, -5.0]
    GREEN, RED = [0.3, 0.5, 0.2], [0.5, 0.4, 0.3]     # a leaf texel (green >= red) and a twig texel

    def leaf(self, n, eye, A, a0=0, scale=1.0, wrap=0.4, c27w=1.0, c49=(1.0, 1.0, 1.0), c26=(1.0, 1.0, 1.0), vcolour=None):
        m = [[x * scale for x in row] for row in self.M_ROT]                        # M = R * c7.w
        normal = [lf.dot(row, n) for row in m]
        world = [lf.dot(row, self.LEAF_P) / scale + t for row, t in zip(m, self.LEAF_T)]
        view = lf.normalize([e - w for e, w in zip(eye, world)])
        front, back = lf.leaf_light(normal, view, lf.normalize(self.SUN_DIR), wrap)
        if vcolour is None:
            vcolour = lf.probe_colour(self.PROBE[a0][:3], self.PROBE[a0 + 1:a0 + 4], self.WEIGHTS)
        amb = lf.instance_ambient(vcolour, self.TINT, self.PLANT_POW * self.LEAF_GAMMA, self.ADD)
        m1, m2 = lf.leaf_mask(1.0, 0.6, 0.4)
        c = lf.tree_leaf(A, amb, front, back, self.SUN_COL, c27w, m1=m1, m2=m2, c49=list(c49))
        return [front, back] + amb + [c[i] * c26[i] for i in range(3)]

    def leaf_cases(self):
        n = [-0.6, 0.0, 0.8]
        return {
            # The viewer on the normal's side, the sun in front: the wrapped front light, no back light.
            "front": self.leaf(n, self.EYE_FRONT, self.GREEN),
            # The viewer behind the card: the normal is turned to him, the sun is then behind it, and the
            # back light passes through a leaf texel (green >= red) ...
            "behind, leaf": self.leaf(n, self.EYE_BEHIND, self.GREEN),
            # ... and not through a twig texel.
            "behind, twig": self.leaf(n, self.EYE_BEHIND, self.RED),
            "green equals red": self.leaf(n, self.EYE_BEHIND, [0.4, 0.4, 0.2]),
            # c7.w = 0.5 (in the rows, and taken out of the world position), set 8, another wrap, c27.w on
            # the ambient part only, c49 on the whole colour, c26 after the function.
            "scaled": self.leaf(n, self.EYE_FRONT, self.GREEN, a0=8, scale=0.5, wrap=0.25, c27w=0.5,
                                c49=(1.0, 0.5, 2.0), c26=(1.0, 1.0, 0.5)),
            "behind, scaled": self.leaf(n, self.EYE_BEHIND, self.GREEN, a0=8, scale=0.5, wrap=0.25, c27w=0.5,
                                        c49=(1.0, 0.5, 2.0), c26=(1.0, 1.0, 0.5)),
            # A zero normal: front = wrap / (1 + wrap), no back light.
            "no normal": self.leaf([0.0, 0.0, 0.0], self.EYE_FRONT, self.GREEN),
            # A probe colour of exactly zero in red and negative in green (set 4): the power of the
            # absolute value, as the game's `log r, r_abs` (A584... instr 124, 126, 127).
            "probe zero and negative": self.leaf(n, self.EYE_FRONT, self.GREEN, a0=4),
            # The eye in the vertex: no view direction, so no turn and no back light.
            "eye in the vertex": self.leaf(n, [1.0 + 1.0, 2.0 + 0.5, 3.0 + 2.0], self.GREEN),
            # No wrap: the plain clamped cosine.
            "no wrap": self.leaf(n, self.EYE_FRONT, self.GREEN, wrap=0.0),
        }

    def test_leaf(self):
        cases = self.leaf_cases()
        self.assertEqual(sorted(cases), sorted(LEAF_PINS))
        for name, got in cases.items():
            with self.subTest(case=name):
                self.close(got, LEAF_PINS[name])
        # What the cases are meant to reach, so that a pin cannot be of the wrong branch:
        self.assertAlmostEqual(cases["front"][0], (cases["no wrap"][0] + 0.4) / 1.4, places=15)
        self.assertEqual(cases["front"][1], 0.0)
        self.assertEqual(cases["behind, leaf"][0], 0.0)
        self.assertGreater(cases["behind, leaf"][1], 0.5)
        self.assertEqual(cases["behind, leaf"][:5], cases["behind, twig"][:5])
        self.assertAlmostEqual(cases["no normal"][0], 0.4 / 1.4, places=15)
        self.assertEqual(cases["no normal"][1], 0.0)
        self.assertEqual(cases["eye in the vertex"][1], 0.0)
        self.assertEqual(cases["eye in the vertex"][0], cases["front"][0])
        self.assertLess(cases["no wrap"][0], cases["front"][0])
        self.assertEqual(lf.leaf_mask(1.0, 0.6, 0.4), (1.0, 1.0))
        # A wrap of -1 (the wrapped cosine's denominator zero) gives no front light, also where the
        # numerator is above zero; and the viewer's cosine is clamped before the back light's factor
        # (tests/native/test_clay_logic.cpp, case 526, has the same two).
        self.assertEqual(lf.leaf_light([0.0, 0.0, 2.0], [0.0, 0.0, 1.0], [0.0, 0.0, 1.0], -1.0), (0.0, 0.0))
        self.assertEqual(lf.leaf_light([0.0, 0.0, 4.0], [math.sqrt(0.75), 0.0, 0.5], [0.0, math.sqrt(0.984375), -0.125], 0.0),
                         (0.0, 0.5))
        # The scale is in the normal (the game does not normalise it) and not in the world position.
        self.assertAlmostEqual(cases["scaled"][0], (0.5 * 0.5976971893220461 + 0.25) / 1.25, places=12)


LEAF_PINS = {
    "front": [0.7126408495157472, 0.0, 0.46369198767397196, 0.16310554084116385, 0.5053389422232634, 0.3507619286083685, 0.377298722969617, 0.15807905640591247],
    "behind, leaf": [0.0, 0.5975238349664942, 0.46369198767397196, 0.16310554084116385, 0.5053389422232634, 0.31657217528724035, 0.329525161931677, 0.14886969524197222],
    "behind, twig": [0.0, 0.5975238349664942, 0.46369198767397196, 0.16310554084116385, 0.5053389422232634, 0.23184599383698598, 0.06524221633646554, 0.15160168266697902],
    "green equals red": [0.0, 0.5975238349664942, 0.46369198767397196, 0.16310554084116385, 0.5053389422232634, 0.42209623371632055, 0.26362012954534164, 0.14886969524197222],
    "scaled": [0.4390788757288185, 0.0, 1.2240510043759552, 0.8173572059344705, 3.6166064948512817, 0.31401407674785237, 0.19327851745553865, 0.3967869595434337],
    "behind, scaled": [0.0, 0.22157000987666525, 1.2240510043759552, 0.8173572059344705, 3.6166064948512817, 0.24941394358976282, 0.14814542779121684, 0.37938625027526146],
    "no normal": [0.28571428571428575, 0.0, 0.46369198767397196, 0.16310554084116385, 0.5053389422232634, 0.22396473915933449, 0.2001241989920105, 0.12392493130179555],
    "probe zero and negative": [0.7126408495157472, 0.0, 0.0068, 0.10840647433840495, 0.092, 0.21369433230617693, 0.3499491897182375, 0.0754112679612598],
    "eye in the vertex": [0.7126408495157472, 0.0, 0.46369198767397196, 0.16310554084116385, 0.5053389422232634, 0.3507619286083685, 0.377298722969617, 0.15807905640591247],
    "no wrap": [0.5976971893220461, 0.0, 0.46369198767397196, 0.16310554084116385, 0.5053389422232634, 0.31662366153083926, 0.32959710398923103, 0.14888356359041638],
}


PLANT_PINS = {
    "half": [0.5, 0.5257311121191336, 0.0, 0.8506508083520399, 0.5683564165336911, 0.2625218355265196, 0.12797253813103704, 0.44487503475959345, 0.2127892319794812, 0.07734856786179545],
    "foot": [1.0, 0.28, 0.0, 0.96, 0.5683564165336911, 0.2625218355265196, 0.12797253813103704, 0.5637660882668456, 0.29253030221060783, 0.10617064143931111],
    "top": [0.0, 0.6, 0.0, 0.8, 0.5683564165336911, 0.2625218355265196, 0.12797253813103704, 0.40220600826684555, 0.18417081421060788, 0.06700456143931112],
    "weak": [0.5, 0.5793187229742429, 0.0, 0.8151011085819305, 0.5683564165336911, 0.2625218355265196, 0.12797253813103704, 0.414469231283684, 0.19239584459765913, 0.06997746398884772],
    "lit half": [0.7248367390246099, 0.32464005352070285, 0.3194501361635652, 0.17970944830063978, 0.07222667160009423],
    "lit foot": [0.8820928926290964, 0.564824, 0.5051217140439017, 0.27706333165698416, 0.10377581868510069],
    "lit top": [0.660085140197664, 0.23844000000000004, 0.25308259704234354, 0.14484074306862768, 0.06091490419395856],
    "unbent": [1.0, 0.6, 0.0, 0.8, 0.5683564165336911, 0.2625218355265196, 0.12797253813103704, 0.40220600826684555, 0.18417081421060788, 0.06700456143931112],
    "lit unbent": [0.660085140197664, 0.23844000000000004, 0.25308259704234354, 0.14484074306862768, 0.06091490419395856],
    "black": [0.5, 0.5257311121191336, 0.0, 0.8506508083520399, 0.0068, 0.071, 0.092, 0.1640968264927479, 0.13618049776887337, 0.06655680642248434],
    "lit black": [0.7248367390246099, 0.32464005352070285, 0.1640968264927479, 0.13618049776887337, 0.06655680642248434],
    "no normal": [0.5, 0.28, 0.0, 0.96, 0.5683564165336911, 0.2625218355265196, 0.12797253813103704, 0.5637660882668456, 0.29253030221060783, 0.10617064143931111],
    "no normal, top": [0.0, 0.0, 0.0, 0.0, 0.5683564165336911, 0.2625218355265196, 0.12797253813103704, 0.28417820826684553, 0.10500873421060784, 0.03839176143931111],
    "lit no normal, top": [0.0, 0.0, 0.019909758646090518, 0.03531010782579682, 0.0276],
    "no up": [0.5, 0.6, 0.0, 0.8, 0.5683564165336911, 0.2625218355265196, 0.12797253813103704, 0.40220600826684555, 0.18417081421060788, 0.06700456143931112],
}


class LocalLights(unittest.TestCase):
    """The per-light form of the two-, four- and seven-light shaders (lighting-formulas.md, 'The sun',
    the multi form), as one function of one light, and the families' sum."""
    L = dict(pos=[3.0, 4.0, 0.0], w=1.0, col=[1.5, 0.75, 0.1], blend=1.0, range_scale=0.1,
             axis=[0.0, 0.0, 1.0], cone_cos=-1.0, cone_scale=1.0, shadow=0.0)

    def test_a_lantern_without_cone_is_colour_times_n_dot_times_range(self):
        P, N = [0.0, 0.0, 0.0], [0.6, 0.8, 0.0]
        got = lf.local_light(P, N, **self.L)
        # V = (3, 4, 0), |V| = 5, l = (0.6, 0.8, 0): n_dot 1, range 0.5, cone sat(0 + 1) = 1, gate 0.5
        self.assertEqual(got, [0.75, 0.375, 0.05])

    def test_a_direction_light_with_w_zero_ignores_the_position(self):
        L = dict(self.L, pos=[0.0, 0.0, 1.0], w=0.0, blend=0.0)
        got = lf.local_light([100.0, 100.0, 100.0], [0.0, 0.0, 1.0], **L)
        self.assertEqual(got, [1.5, 0.75, 0.1])

    def test_the_shadow_strength_takes_the_mask(self):
        L = dict(self.L, shadow=1.0, blend=0.0)
        got = lf.local_light([0.0, 0.0, 0.0], [0.6, 0.8, 0.0], mask=0.25, facing=1.0, **L)
        self.assertEqual(got, [1.5 * 0.25, 0.75 * 0.25, 0.1 * 0.25])

    def test_the_sum_and_the_families_add_it_to_the_sun_term(self):
        P, N = [0.0, 0.0, 0.0], [0.6, 0.8, 0.0]
        one = lf.local_light(P, N, **self.L)
        two = lf.local_lights(P, N, [self.L, self.L])
        self.close(two, [2 * x for x in one])
        A, baked = [0.5, 0.5, 0.5], [0.2, 0.3, 0.4]
        base = lf.terrain_layer(A, baked, [1.0] * 3, 1.0, [0.0] * 3, [0.0] * 3, 1.0, 0.0, [1.0] * 3, sun_gate=1.0)
        lit = lf.terrain_layer(A, baked, [1.0] * 3, 1.0, [0.0] * 3, [0.0] * 3, 1.0, 0.0, [1.0] * 3, sun_gate=1.0, lights=two)
        self.close(lit, [b + A[i] * two[i] for i, b in enumerate(base)])
        base = lf.lit_object(A, baked, [1.0] * 3, 1.0, [0.0] * 3, [0.0] * 3, 1.0, 0.0, [1.0] * 3, c49=[2.0] * 3)
        lit = lf.lit_object(A, baked, [1.0] * 3, 1.0, [0.0] * 3, [0.0] * 3, 1.0, 0.0, [1.0] * 3, c49=[2.0] * 3, lights=two)
        self.close(lit, [b + A[i] * two[i] * 2.0 for i, b in enumerate(base)])
        base = lf.plant(A, baked, 0.0, [1.0] * 3)
        lit = lf.plant(A, baked, 0.0, [1.0] * 3, lights=two)
        self.close(lit, [b + A[i] * two[i] for i, b in enumerate(base)])

    def test_tree_leaf_is_its_colour_sum_form(self):
        A, amb, sun = [0.25, 0.5, 0.25], [0.5, 0.5, 0.5], [2.0, 2.0, 2.0]
        want = lf.tree_leaf(A, amb, 0.5, 0.25, sun, 0.5, m1=0.75, m2=0.875, c49=[1.0, 0.5, 2.0])
        got = lf.tree_leaf_sums(A, amb, [s * 0.5 for s in sun], [s * 0.25 for s in sun], 0.5, m1=0.75, m2=0.875, c49=[1.0, 0.5, 2.0])
        self.close(got, want)

    def test_finite_for_any_input(self):
        nan, inf = float("nan"), float("inf")
        L = dict(self.L, pos=[nan, inf, 0.0], col=[inf, nan, 1.0], range_scale=nan, cone_scale=inf, shadow=nan)
        for x in lf.local_light([inf, 0.0, 0.0], [nan, 0.0, 0.0], **L):
            self.assertTrue(math.isfinite(x))

    def close(self, got, want):
        self.assertEqual(len(got), len(want))
        for g, w in zip(got, want):
            self.assertAlmostEqual(g, w, places=12)


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
class MultiSun(unittest.TestCase):
    """Controller amendment C2 (plan Task 7): a two- or four-light pair is drawn by the native pass with
    the frame's sun through its family's ONE-light function. That is right only if the shader's own
    arithmetic, with the light set as the game loads it (the sun behind dark lanterns, c112 one-hot at
    its slot, c94 zero there, w = 0), is that function."""
    TRIALS = 24

    def test_every_pair_is_its_one_light_function_at_every_slot_behind_light_0(self):
        checked = 0
        for family in lc.MULTI_SUN_FAMILIES:
            for ps, vs in lc.FAMILIES[family]:
                for slot in range(1, lc.light_count(ps, vs)):
                    with self.subTest(family=family, ps=ps, vs=vs, slot=slot):
                        res = lc.check_multi_sun(family, ps, vs, slot, trials=self.TRIALS)
                        self.assertTrue(res.ok, f"{res.agree}/{res.trials} worst {res.worst:.3g} {res.first_bad}")
                        self.assertEqual(res.trials, self.TRIALS)
                        checked += 1
        # Twenty-four pairs: fourteen of two lights, seven of four, three of seven.
        self.assertEqual(checked, 53)

    def test_the_families_and_light_counts_cover_the_table(self):
        # Every -multi pair of every family is checked (the plants' since plan Task 8b), and has a light count.
        for family, pairs in lc.FAMILIES.items():
            if family.endswith("-multi"):
                self.assertIn(family, lc.MULTI_SUN_FAMILIES)
            if family.endswith("-multi"):
                for ps, vs in pairs:
                    self.assertIn(lc.light_count(ps, vs), (2, 4, 7))
        # The light count is the registers the dump reads: c82 / c89 only in the four-light shaders.
        for ps, lights in lc.MULTI_LIGHTS.items():
            text = lc.ue.load(DUMPS, ps, "frag")
            self.assertEqual("c89" in text and "c82" in text, lights >= 4, ps)
            self.assertIn("c88", text)
            self.assertEqual("c86" in text and "c93" in text, lights == 7, ps)
        # The leaves' lights are the VERTEX shader's (plan Task 8c: their pixel shader 7CD5... reads no light
        # register at all): c84 / c77 from two lights on, c85 / c78 only with four.
        self.assertEqual(lc.MULTI_LIGHTS_VS, {"D7DD31B9BF194DE8": 2, "2D40B53C926109BE": 4})
        for vs, lights in list(lc.MULTI_LIGHTS_VS.items()) + [("A5846836C90E1192", 1)]:
            text = lc.ue.load(DUMPS, vs, "vert")
            self.assertEqual("c84" in text and "c77" in text, lights >= 2, vs)
            self.assertEqual("c85" in text and "c78" in text, lights == 4, vs)
        leaf_ps = lc.ue.load(DUMPS, "7CD57B81550F19E3", "frag")
        for register in ("c80", "c87", "c83", "c112", "c109"):
            self.assertNotIn(register, leaf_ps)
        self.assertNotIn("7CD57B81550F19E3", lc.MULTI_LIGHTS)
        self.assertEqual(lc.light_count("7CD57B81550F19E3", "D7DD31B9BF194DE8"), 2)
        self.assertEqual(lc.light_count("7CD57B81550F19E3", "2D40B53C926109BE"), 4)
        self.assertEqual(lc.light_count("648B965C200A5113", "ECD66A10092E6562"), 2)
        self.assertEqual(lc.GAME_SUN_SLOT, {2: 1, 4: 2, 7: 4})

    def test_a_slot_the_shader_does_not_have_is_refused(self):
        with self.assertRaises(ValueError):
            lc.check_multi_sun("object-multi", "648B965C200A5113", "ECD66A10092E6562", 2, trials=1)
        with self.assertRaises(ValueError):
            lc.check_multi_sun("object-multi", "789266C3E42D7E59", "ECD66A10092E6562", 0, trials=1)
        with self.assertRaises(ValueError):
            lc.check_multi_sun("plant-multi", "E99F4ACC7A3C78B4", "8123C16DBF583F92", 2, trials=1)
        with self.assertRaises(KeyError):     # a one-light family has no slot to check
            lc.check_multi_sun("plant", "A7E45D07F5CF6627", "8123C16DBF583F92", 1, trials=1)
        with self.assertRaises(ValueError):   # the leaves: the light count is the vertex shader's
            lc.check_multi_sun("tree-leaf-multi", "7CD57B81550F19E3", "D7DD31B9BF194DE8", 2, trials=1)
        with self.assertRaises(KeyError):
            lc.check_multi_sun("tree-leaf", "7CD57B81550F19E3", "A5846836C90E1192", 1, trials=1)
        self.assertTrue(lc.check_multi_sun("tree-leaf-multi", "7CD57B81550F19E3", "2D40B53C926109BE", 3, trials=2).ok)

    def check_with(self, family, ps, vs, slot, spoil):
        """check_multi_sun with the light set spoiled after _sun_in_slot made it."""
        real = lc._sun_in_slot

        def spoiled(rng, c, s, **kwargs):
            out = real(rng, c, s, **kwargs)
            spoil(c, s)
            return out

        with mock.patch.object(lc, "_sun_in_slot", spoiled):
            return lc.check_multi_sun(family, ps, vs, slot, trials=self.TRIALS)

    def test_the_check_fails_when_the_light_set_is_not_the_games(self):
        """What makes the equality hold is the game's light set; with any one of its properties gone the
        microcode is no longer the one-light function, and the check says so."""
        def lantern_lit(c, s):
            c[87] = [0.5, 0.25, 0.125, 0.0]         # a lantern with its light

        def mask_on_the_lantern(c, s):
            c[112] = [1.0, 0.0, 0.0, 0.0]           # the screen mask shadows light 0, not the sun
            if 113 in c:
                c[113] = [0.0] * 4

        def sun_with_a_cone(c, s):
            c[94 if s < 4 else 95][s if s < 4 else s - 4] = 1.0  # range/cone on sun

        def sun_as_a_position(c, s):
            c[80 + s][3] = 1.0                      # w = 1: the direction becomes position minus point

        pairs = [("terrain-layer-multi", "45243B758F9272AB", "C30A97D946FA2BE4", 1),
                 ("terrain-layer-multi", "578342593A37DD35", "FB68A7F2301210E1", 2),
                 ("terrain-layer-multi", "E0996BF4D9539D38", "C30A97D946FA2BE4", 4),
                 ("terrain-layer-multi", "0981301D645E2517", "FB68A7F2301210E1", 4),
                 ("tree-trunk-multi", "7928BAA7A6691A5D", "475EC9F795E5EDBB", 4),
                 ("object-multi", "648B965C200A5113", "ECD66A10092E6562", 1),
                 ("object-multi", "789266C3E42D7E59", "ECD66A10092E6562", 2),
                 ("plant-multi", "E99F4ACC7A3C78B4", "8123C16DBF583F92", 1),
                 ("plant-lit-multi", "F525E023E08AC9BB", "B636821F95DC9D8E", 1),
                 ("plant-lit-multi", "F525E023E08AC9BB", "36B543CECAE5C781", 1)]
        for family, ps, vs, slot in pairs:
            for spoil in (lantern_lit, mask_on_the_lantern, sun_with_a_cone, sun_as_a_position):
                with self.subTest(ps=ps, spoil=spoil.__name__):
                    self.assertFalse(self.check_with(family, ps, vs, slot, spoil).ok)
            # Control: the same wrapper without a change agrees.
            self.assertTrue(self.check_with(family, ps, vs, slot, lambda c, s: None).ok)
        # The leaves (plan Task 8c): their light set is the vertex shader's (c76.. the positions, c83.. the
        # colours, c90 the blend of cone and range), made from the same set. The screen mask shadows the
        # whole leaf light whatever c112 says (the pixel shader reads no c112), so spoiling that says nothing.
        for vs, slot in (("D7DD31B9BF194DE8", 1), ("2D40B53C926109BE", 2), ("2D40B53C926109BE", 1)):
            for spoil in (lantern_lit, sun_with_a_cone, sun_as_a_position):
                with self.subTest(vs=vs, slot=slot, spoil=spoil.__name__):
                    self.assertFalse(self.check_with("tree-leaf-multi", "7CD57B81550F19E3", vs, slot, spoil).ok)
            self.assertTrue(self.check_with("tree-leaf-multi", "7CD57B81550F19E3", vs, slot, mask_on_the_lantern).ok)
            self.assertTrue(self.check_with("tree-leaf-multi", "7CD57B81550F19E3", vs, slot, lambda c, s: None).ok)

    def test_the_default_check_is_untouched_by_the_sun_slot(self):
        # Without a slot the drivers are the ones `--all` runs: light 0 with every constant random.
        res = lc.check_pair("object-multi", "789266C3E42D7E59", "ECD66A10092E6562", trials=8)
        self.assertTrue(res.ok)
        # ... and that general form is not the one-light function (the reason the check exists).
        _, make_case = lc._object_driver("789266C3E42D7E59", "ECD66A10092E6562", DUMPS, multi=True)
        case = make_case(random.Random(3))
        self.assertNotEqual(case.consts[112], [1.0, 0.0, 0.0, 0.0])

    def test_command_line(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = lc.main(["--multi-sun", "--trials", "4"])
        text = out.getvalue()
        self.assertEqual(code, 0)
        self.assertEqual(text.count("(the game)"), 24)
        self.assertIn("tree-leaf-multi: ps 7CD57B81550F19E3 vs D7DD31B9BF194DE8 (2 lights), sun in slot 1 (the game): 4/4 agree", text)
        self.assertIn("tree-leaf-multi: ps 7CD57B81550F19E3 vs 2D40B53C926109BE (4 lights), sun in slot 2 (the game): 4/4 agree", text)
        self.assertIn("plant-multi: ps E99F4ACC7A3C78B4 vs 8123C16DBF583F92 (2 lights), sun in slot 1 (the game): 4/4 agree", text)
        self.assertIn("plant-lit-multi: ps F525E023E08AC9BB vs 36B543CECAE5C781 (2 lights), sun in slot 1 (the game): 4/4 agree", text)
        self.assertIn("ps 789266C3E42D7E59 vs ECD66A10092E6562 (4 lights), sun in slot 2 (the game): 4/4 agree", text)
        self.assertIn("0 pairs disagree at the slot the game puts the sun in", text)
        # A light set that is not the game's (the sun as a position, w = 1): pairs disagree at the game's
        # slot, the line counts them and the exit code says so.
        real = lc._sun_in_slot

        def sun_as_a_position(rng, c, slot, **kwargs):
            out_ = real(rng, c, slot, **kwargs)
            c[80 + slot][3] = 1.0
            return out_

        out = io.StringIO()
        with mock.patch.object(lc, "_sun_in_slot", sun_as_a_position), contextlib.redirect_stdout(out):
            code = lc.main(["--multi-sun", "--trials", "8"])
        self.assertEqual(code, 1)
        self.assertIn("24 pairs disagree at the slot the game puts the sun in", out.getvalue())
        self.assertIn("first disagreement, trial ", out.getvalue())


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

    def test_the_ten_terrain_shaders_are_listed(self):
        terrain = {ps for fam in ("terrain-layer", "terrain-layer-multi", "terrain-far") for ps, _ in lc.FAMILIES[fam]}
        self.assertEqual(terrain, {"9C2144E42E95A76F", "4755A2E4A05BA678", "45243B758F9272AB", "AF1E16EE57E4BB06",
                                   "04F23292B2F44077", "578342593A37DD35", "7DDAD5C29B3F56EE", "2F8A0341E8C09698",
                                   "E0996BF4D9539D38", "0981301D645E2517"})

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
            return lf.terrain_layer(A=a["F"], sun_gate=1.0, **{k: v for k, v in a.items() if k != "F"})

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
            ("instance_ambient", lambda colour, tint, power, add: right["instance_ambient"](colour, tint, power * 2.2, add),
             "tree-leaf-multi", None),
            ("leaf_light", lambda normal, view, light_dir, wrap, spot=1.0: right["leaf_light"](normal, view, light_dir, 0.0, spot),
             "tree-leaf-multi", None),
            ("leaf_light", lambda normal, view, light_dir, wrap, spot=1.0: right["leaf_light"](normal, view, light_dir, wrap, 1.0),
             "tree-leaf-multi", None),
            ("leaf_mask", lambda mask, gain, bias: (mask * gain + bias,) * 2, "tree-leaf-multi", None),
            ("probe_colour", lambda base, rows, weights: list(base), "tree-leaf-multi", None),
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
                              ("tree-leaf", leaf_without_the_green_test), ("tree-leaf", leaf_c27w_on_the_sun),
                              ("tree-leaf-multi", leaf_without_the_green_test), ("tree-leaf-multi", leaf_c27w_on_the_sun)):
            for ps, vs in lc.FAMILIES[family]:
                with self.subTest(family=family, ps=ps, wrong=wrong.__name__):
                    self.assertFalse(lc.check_pair(family, ps, vs, trials=self.TRIALS, formula=wrong).ok)

    def test_the_leaf_pairs_are_checked_with_the_shaders_own_literals(self):
        # Plan Task 8c: the leaf pixel shader's mask literals are the captured ones (c255 = (1, 0, 0.4, 0.6) in
        # every "ps_lit" of 7CD5... in two captures: out/native-evidence/lighting/task8c/leaf_literals.txt), no
        # longer drawn at random, and each of the three vertex shaders runs with its own table (the 1 / 2.2
        # of the ambient exponent is c249.w, c248.w and c255.x in them).
        self.assertEqual(lc.PS_LITERALS["7CD57B81550F19E3"][255], [1.0, 0.0, 0.4, 0.6])
        self.assertEqual({vs: (info["lights"], info["gamma"]) for vs, info in lc._LEAF_VS.items()},
                         {"A5846836C90E1192": (1, (249, 3)), "D7DD31B9BF194DE8": (2, (248, 3)),
                          "2D40B53C926109BE": (4, (255, 0))})
        for vs, info in lc._LEAF_VS.items():
            reg, comp = info["gamma"]
            self.assertAlmostEqual(info["literals"][reg][comp], 1 / 2.2, places=7)
            self.assertEqual(sorted(info["literals"]), list(range(248, 256)))
        self.assertEqual(lc.FAMILIES["tree-leaf"], [("7CD57B81550F19E3", "A5846836C90E1192")])
        self.assertEqual(lc.FAMILIES["tree-leaf-multi"], [("7CD57B81550F19E3", "D7DD31B9BF194DE8"),
                                                          ("7CD57B81550F19E3", "2D40B53C926109BE")])
        _, make_case = lc._leaf_driver("7CD57B81550F19E3", "D7DD31B9BF194DE8", DUMPS)
        case = make_case(random.Random(5))
        self.assertEqual(case.consts[255], [1.0, 0.0, 0.4, 0.6])

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


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class TrialInputs(unittest.TestCase):
    """Fix round 1: a trial must not draw a constant or a texel with equal components, or a formula that
    names the wrong component of it would pass."""

    def test_no_pixel_constant_has_four_equal_components(self):
        for family, pairs in lc.FAMILIES.items():
            for ps, vs in pairs:
                _, make_case = lc.DRIVERS[family](ps, vs, DUMPS)
                rng = random.Random(3)
                for _ in range(4):
                    case = make_case(rng)
                    for reg, value in case.consts.items():
                        if reg >= 240 or not any(value):
                            continue        # literals; a register that is zero on purpose (lights 1 to 3)
                        self.assertGreater(len(set(value)), 1, f"{family} {ps}: c{reg} = {value}")
                        if reg == 11:       # the review's case: c11.x read as .y or .w must not pass
                            self.assertEqual(len(set(value)), 4, f"{family} {ps}: c11 = {value}")

    def test_c11_x_is_the_component_the_terrain_shaders_read(self):
        for family in ("terrain-layer", "terrain-layer-multi", "terrain-far"):
            ps, vs = lc.FAMILIES[family][0]
            ps_text, make_case = lc.DRIVERS[family](ps, vs, DUMPS)

            def other_components(rng):
                case = make_case(rng)
                case.consts[11][1:] = [9.0, -9.0, 99.0]             # y, z, w: not read
                return case

            def x_exchanged(rng):
                case = make_case(rng)
                c11 = case.consts[11]
                c11[0], c11[1] = c11[1], c11[0]                     # the formula keeps the old x
                return case

            with self.subTest(family=family):
                self.assertTrue(lc.check(ps_text, other_components, lc.FORMULAS[family], 24).ok)
                self.assertFalse(lc.check(ps_text, x_exchanged, lc.FORMULAS[family], 24).ok)

    def test_the_mask_texel_has_distinct_components(self):
        for family, pairs in lc.FAMILIES.items():
            if family == "terrain-far":
                continue                    # no mask in the far shaders
            ps, vs = pairs[0]
            _, make_case = lc.DRIVERS[family](ps, vs, DUMPS)
            texel = make_case(random.Random(3)).sampler(8, (0.0, 0.0), {})
            self.assertEqual(len(set(texel)), 4, f"{family}: tf8 texel {texel}")

    def test_bone_matrices_differ_by_index(self):
        vertex = lc.Vertex(random.Random(3))
        attrs = {"DataFormat": "FMT_16_16_16_16_FLOAT"}
        a = vertex(3, 0, attrs, 5.0)
        self.assertEqual(vertex(3, 0, attrs, 5.0), a)               # the same bone again: the same row
        self.assertNotEqual(vertex(3, 0, attrs, 6.0), a)            # another bone
        self.assertNotEqual(vertex(3, 2, attrs, 5.0), a)            # another row of the same bone
        self.assertEqual(vertex.elements[(3, 0)], a)                # the first element fetched at an offset


class Coverage(unittest.TestCase):
    def test_rows_are_counted_by_what_was_checked(self):
        def draw(ps, vs, in_scene=True, frame=1):
            return {"kind": "draw", "frame": frame, "in_scene": in_scene, "ps_hash": "0x" + ps, "vs_hash": "0x" + vs}

        rows = [
            draw("8D900846800943C8", "ECD66A10092E6562"),
            draw("8D900846800943C8", "ECD66A10092E6562", frame=2),
            draw("8D900846800943C8", "ECD66A10092E6562", in_scene=False),          # a shadow or reflection pass
            draw("7CD57B81550F19E3", "D7DD31B9BF194DE8"),                          # a two-light leaf (plan Task 8c)
            draw("7CD57B81550F19E3", "0000000000000000"),                          # a leaf vertex shader that was not run
            draw("014F8A02DB7B19CA", "7C5710DEF3EE33C4"),                          # fur shells
            {"kind": "tess", "frame": 2, "in_scene": True, "ps_hash": "0x9C2144E42E95A76F",
             "vs": {"hash": "0xC30A97D946FA2BE4"}},
            {"kind": "raw", "frame": 2, "in_scene": True},
            {"kind": "texture"},
        ]
        frames, by_label, by_pair = lc.coverage(rows)
        self.assertEqual(frames, 2)
        self.assertEqual(by_label, {"object": 2, "tree-leaf-multi": 1,
                                    "tree-leaf / tree-leaf-multi, vertex shader not run": 1, "no family": 1,
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


# --- the screen mask (shadows sub-project, plan Task 2) -------------------------------------------

# A pixel shader in the dump's syntax with the form of the mask shader 0F99810F072BF0FC and two near taps
# in the place of twelve (the offsets (c254.x, c253.x) and (c254.y, c253.y), the tap weight c248.y): the
# depth fetch and the world position, the distance, the near block under its predicate, the far block of
# four offset fetches with getWeights2D, the blend and the fade. The far block and the output are the real
# shader's lines.
MASK_SYNTHETIC = """/*    0.0 */       exec
/*   11   */          add r1, c46.zwzw, c249.zzyy
              +       maxs r3.___w, c249.yy
/*   12   */          add r1.__zw, r1.zzzw, c46.zzzw
/*   13   */          add r1.xy__, r1.xyyy, r_abs[0].xyyy
/*   14   */          mad r1.__zw, r_abs[0].xxxy, c248.wwww, r1.zzzw
/*   15   */          mul r1, r1.xywz, c46.xyyx
/*   16   */          tfetch2D r0.__x_, r1.xy, tf13, FetchValidOnly=false
/*    0.1 */       alloc colors
/*    1.0 */       exec
/*   17   */          add r2.x_z_, r1.wzzz, c248.zzzz
              +       subsc r2._y__, c249.y, r0.z
/*   18   */          cndeq r3.xyz_, c249.xyyy, -r2.zzzz, r2.yyxx
/*   19   */          dp4 r1.x___, c116.zxyw, r3.yzxw
/*   20   */          dp4 r1._y__, c117.zxyw, r3.yzxw
/*   21   */          dp4 r0.___w, c119.zxyw, r3.yzxw
/*   22   */          dp4 r1.__z_, c118.zxyw, r3.yzxw
              +       rcp r0.___w, r0.w
/*    1.1 */       exec    // PredicateClean=false
/*   23   */          mad r1.xyz_, r1.zyxx, r0.wwww, -c31.zyxx
/*   24   */          dp3 r0.___w, r1.xzyy, r1.xzyy
/*   25   */          add r1.x___, c12.xxxx, c12.zzzz
              +       sqrt r0.___w, r_abs[0].w
/*   26   */          sgt r1.x___, r1.xxxx, r0.wwww
/*   27   */          sgt r3.xy__, -r_abs[0].xxxx, c249.xxxx
              +       setp_ne r0._, r1.x
/*    2.0 */ (!p0) jmp L15
/*    2.1 */  (p0) exec
/*   28   */     (p0) add r0.xy__, r_abs[0].xyyy, c47.xyyy
/*   29   */     (p0) mul r0.xy__, r0.yxxx, c47.wzzz
/*   30   */     (p0) tfetch2D r0.xy__, r0.yx, tf14, FetchValidOnly=false
/*   31   */          serialize
                 (p0) cndeq r9.xyz_, c249.xxyy, r2.yxxx, -r2.zzzz
/*   32   */     (p0) mad r0.xy__, r0.xyyy, c248.wwww, c248.zzzz
/*   33   */     (p0) mul r1.x_z_, r0.xxxx, c254.xyyy
              +  (p0) maxs r9.___w, c249.yy
/*   34   */     (p0) mul r1._y_w, -r0.yyyy, c254.xxyy
/*   35   */     (p0) dp4 r3._y__, c36.zxyw, r9
/*   36   */     (p0) dp4 r3.__z_, c37.zxyw, r9
/*   37   */     (p0) dp4 r3.___w, c38.zxyw, r9
/*   38   */     (p0) mad r1, r0.yxyx, c253.xxyy, r1
/*   39   */     (p0) dp4 r0.x___, c39.zxyw, r9
/*    3.0 */  (p0) exec
/*   40   */     (p0) mul r1, r1, c77.xxxx
              +  (p0) rcp r0.x___, r0.x
/*   41   */     (p0) mad r1, r3.yzyz, r0.xxxx, r1
/*   42   */     (p0) tfetch2D r4.x___, r1.xy, tf6
/*   43   */     (p0) tfetch2D r4._x__, r1.zw, tf6
/*   44   */          serialize
                 (p0) mad r0.x___, -r3.wwww, r0.xxxx, c249.yyyy
/*   45   */     (p0) sgt r4.xy__, r0.xxxx, r4.xyyy
/*   46   */     (p0) add r0.x___, r4.xxxx, r4.yyyy
/*   47   */     (p0) mulsc r3._y__, c248.y, r0.x
                label L15
/*    7.1 */       exec    // PredicateClean=false
/*   87   */          add r0.x___, c12.yyyy, c248.xxxx
/*   88   */          sgt r0._y__, r0.wwww, c12.xxxx
/*   89   */          sgt r0.x___, r0.xxxx, r0.wwww
/*   90   */          mul r0.x___, r0.yyyy, r0.xxxx
/*   91   */          setp_ne r0._, r0.x
/*    8.0 */ (!p0) jmp L20
/*    8.1 */  (p0) exec
/*   92   */     (p0) maxs r4.___w, c249.yy
/*   93   */     (p0) cndeq r4.xyz_, c249.xxyy, r2.yxxx, -r2.zzzz
/*   94   */     (p0) dp4 r1.x___, c40.zxyw, r4
/*   95   */     (p0) dp4 r1._y__, c41.zxyw, r4
/*   96   */     (p0) dp4 r0.x___, c43.zxyw, r4
/*   97   */     (p0) dp4 r1.__z_, c42.zxyw, r4
              +  (p0) rcp r0.x___, r0.x
/*    9.0 */  (p0) exec
/*   98   */     (p0) mul r0.xyz_, r1.zxyy, r0.xxxx
/*   99   */     (p0) tfetch2D r1.x___, r0.yz, tf7, OffsetX=-1, OffsetY=-1
/*  100   */     (p0) tfetch2D r1._x__, r0.yz, tf7, OffsetY=-1
/*  101   */     (p0) tfetch2D r1.__x_, r0.yz, tf7, OffsetX=-1
/*  102   */     (p0) tfetch2D r1.___x, r0.yz, tf7
/*  103   */     (p0) getWeights2D r2.__xy, r0.yz, tf7, MagFilter=linear, MinFilter=linear, OffsetX=-0.5, OffsetY=-0.5
/*    9.1 */  (p0) exec
/*  104   */          serialize
                 (p0) add r2.xy__, -r2.zwww, c249.yyyy
/*  105   */     (p0) mul r2, r2.xyxz, r2.yzww
              +  (p0) subsc r0.x___, c249.y, r0.x
/*  106   */     (p0) sge r1, r0.xxxx, r1
/*  107   */     (p0) dp4 r3.x___, r2.wzxy, r1.wzxy
                label L20
/*   10.0 */       exece
/*  108   */          add r0.__z_, r3.xxxx, -r3.yyyy
/*  109   */          add r0.xy__, r0.wwww, -c12.yxxx
              +       rcp r0.___w, c12.z
/*  110   */          mul_sat r0.___w, r0.yyyy, r0.wwww
/*  111   */          mad r0.__z_, r0.wwww, r0.zzzz, r3.yyyy
/*  112   */          add r0.___w, -r0.zzzz, c12.wwww
              +       mulsc_sat r0.x___, c249.w, r0.x
/*  113   */          mad oC0.xyz1, r0.xxxx, r0.wwww, r0.zzzz
/*   10.1 */       cnop
"""
MASK_SYNTHETIC_LITERALS = {248: [10.0, 0.5, -1.0, 2.0], 249: [0.0, 1.0, 0.5, 0.1],
                           253: [-0.40581, -0.932615, 0.0, 0.0], 254: [-0.326212, -0.32194, 0.0, 0.0]}


def synthetic_taps(lit):
    return [(lit[254][0], lit[253][0]), (lit[254][1], lit[253][1])]


IDENTITY_ROWS = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]]


def near_with(compare):
    """mask_near with another comparison (the reference's is `>`: lit where 1 - depth is beyond the map)."""
    def near(P, near_rows, sample_near, rot, offsets, radius, tap_weight=None):
        w = lf.dot(near_rows[3], P)
        u, v, z = (lf.dot(near_rows[i], P) / w for i in range(3))
        lit = 0
        for offset in offsets:
            ox, oy = lf.mask_rotate(offset, rot)
            lit += compare(1.0 - z, sample_near(u + radius * ox, v + radius * oy))
        return lit * (1.0 / len(offsets) if tap_weight is None else tap_weight)
    return near


def far_with(compare, taps=((0, 0, "11"), (-1, 0, "01"), (-1, -1, "00"), (0, -1, "10")), shift=0.0):
    """mask_far with another comparison (the reference's is `>=`), other texels, or the weights of another
    fraction (the reference's is frac(u * size); `shift` is added before the fraction is taken)."""
    def far(P, far_rows, sample_far_texel, size):
        w = lf.dot(far_rows[3], P)
        u, v, z = (lf.dot(far_rows[i], P) / w for i in range(3))
        x, y = u * size[0], v * size[1]
        ix, iy = math.floor(x), math.floor(y)
        fx, fy = (x + shift) - math.floor(x + shift), (y + shift) - math.floor(y + shift)
        weight = {"11": fx * fy, "01": (1 - fx) * fy, "00": (1 - fx) * (1 - fy), "10": fx * (1 - fy)}
        return sum(weight[k] * compare(1.0 - z, sample_far_texel(ix + dx, iy + dy)) for dx, dy, k in taps)
    return far


def fade_to_one(d, near, far, split, slope):
    return REAL_MASK_COMBINE(d, near, far, list(split[:3]) + [1.0], slope)


REAL_MASK_COMBINE = lf.mask_combine
GT, GE, LT = (lambda a, b: float(a > b)), (lambda a, b: float(a >= b)), (lambda a, b: float(a < b))
# Wrong forms of the mask arithmetic, each a (function of light_formulas, replacement): the two the plan
# names first (a compare direction flipped; the fade to 1 instead of split[3]), then the others a port could
# plausibly make. "tap offset wrong" is WRONG_TAPS below, since the offsets are the checker's input.
WRONG_MASK = {
    "near compare reversed": ("mask_near", near_with(LT)),
    "near compare inclusive": ("mask_near", near_with(GE)),
    "far compare reversed": ("mask_far", far_with(LT)),
    "far compare strict": ("mask_far", far_with(GT)),
    "far taps at +1 in the place of -1": ("mask_far", far_with(GE, taps=((1, 1, "11"), (0, 1, "01"), (0, 0, "00"), (1, 0, "10")))),
    "far weights of frac(x - 0.5)": ("mask_far", far_with(GE, shift=-0.5)),
    "far weights exchanged": ("mask_far", far_with(GE, taps=((0, 0, "00"), (-1, 0, "10"), (-1, -1, "11"), (0, -1, "01")))),
    "fade to 1": ("mask_combine", fade_to_one),
    "blend over split[1]": ("mask_combine", lambda d, near, far, split, slope: REAL_MASK_COMBINE(
        d, near, far, [split[0], split[1], split[1], split[3]], slope)),
    "rotation reversed": ("mask_rotate", lambda offset, rot: (rot[0] * offset[0] - rot[1] * offset[1],
                                                               rot[0] * offset[1] + rot[1] * offset[0])),
    "rotation dropped": ("mask_rotate", lambda offset, rot: tuple(offset)),
}
NEAR_ONLY = ("near compare reversed", "near compare inclusive", "rotation reversed", "rotation dropped")


class MaskFormulas(unittest.TestCase):
    """The mask functions on hand-computed numbers and on synthetic depth maps; no microcode."""

    def test_combine_by_hand(self):
        split = [20.0, 35.0, 2.0, 0.5]
        # d = 10: before the blend, the near term. d = 21: half way through the 2 units: (0.25 + 0.75) / 2.
        # d = 30: the far term. d = 40: half of the fade (0.1 * 5): 0.75 + 0.5 * (0.5 - 0.75). d = 50: split[3].
        for d, want in ((10.0, 0.25), (21.0, 0.5), (30.0, 0.75), (40.0, 0.625), (50.0, 0.5)):
            self.assertAlmostEqual(lf.mask_combine(d, 0.25, 0.75, split, 0.1), want, places=12)
        self.assertAlmostEqual(fade_to_one(50.0, 0.25, 0.75, split, 0.1), 1.0, places=12)

    def test_rotate_by_hand(self):
        # (x, y) -> (a x + b y, a y - b x) with (a, b) = (0.6, 0.8).
        for offset, want in (((1.0, 0.0), (0.6, -0.8)), ((0.0, 1.0), (0.8, 0.6)), ((0.5, -2.0), (-1.3, -1.6))):
            got = lf.mask_rotate(offset, (0.6, 0.8))
            self.assertAlmostEqual(got[0], want[0], places=12)
            self.assertAlmostEqual(got[1], want[1], places=12)

    def test_near_by_hand(self):
        rows = [IDENTITY_ROWS[0], IDENTITY_ROWS[1], IDENTITY_ROWS[2], [0.0, 0.0, 0.0, 2.0]]
        P = [0.5, 0.25, 0.6, 1.0]              # u = 0.25, v = 0.125, depth 0.3: compared value 0.7
        taps = []

        def sample(u, v):
            taps.append((round(u, 12), round(v, 12)))
            return 0.9 if u > 0.3 else 0.5

        offsets = [(1.0, 0.0), (0.0, 1.0), (-1.0, 0.0), (0.0, -1.0)]
        # Unrotated, radius 0.1: taps at u 0.35 (0.7 > 0.9 is false: shadowed), 0.25, 0.15, 0.25 (lit): 3 of 4.
        self.assertAlmostEqual(lf.mask_near(P, rows, sample, (1.0, 0.0), offsets, 0.1), 0.75, places=12)
        self.assertEqual(taps, [(0.35, 0.125), (0.25, 0.225), (0.15, 0.125), (0.25, 0.025)])
        # With the shader's weight of a tap (the literal 1 / 12) the sum is 3 / 12.
        self.assertAlmostEqual(lf.mask_near(P, rows, sample, (1.0, 0.0), offsets, 0.1, tap_weight=1 / 12), 0.25, places=12)
        # Rotated by (a, b) = (0, 1): (1, 0) goes to (0, -1), so the one tap is at v - radius.
        del taps[:]
        self.assertEqual(lf.mask_near(P, rows, sample, (0.0, 1.0), [(1.0, 0.0)], 0.1), 1.0)
        self.assertEqual(taps, [(0.25, 0.025)])

    def test_near_constant_maps_and_the_equal_depth(self):
        P = [0.3, 0.6, 0.25, 1.0]              # compared value 1 - 0.25 = 0.75
        args = ((1.0, 0.0), lf.MASK_NEAR_TAPS, 2.5 / 1024)
        self.assertEqual(lf.mask_near(P, IDENTITY_ROWS, lambda u, v: 0.0, *args), 1.0)      # nothing nearer the light
        self.assertEqual(lf.mask_near(P, IDENTITY_ROWS, lambda u, v: 1.0, *args), 0.0)      # all shadowed
        # A map equal to the compared value is shadow for the near cascade (`>`) ...
        self.assertEqual(lf.mask_near(P, IDENTITY_ROWS, lambda u, v: 0.75, *args), 0.0)
        # ... and light for the far one (`>=`).
        self.assertEqual(lf.mask_far(P, IDENTITY_ROWS, lambda ix, iy: 0.75, (1024, 1024)), 1.0)
        self.assertEqual(lf.mask_far(P, IDENTITY_ROWS, lambda ix, iy: 0.0, (1024, 1024)), 1.0)
        self.assertEqual(lf.mask_far(P, IDENTITY_ROWS, lambda ix, iy: 1.0, (1024, 1024)), 0.0)

    def test_near_half_plane_the_penumbra_is_the_kernel(self):
        # The map is lit left of u = 0.5 and shadowed from there on. A tap at u + radius * x is lit while
        # it is left of the edge, so the mask leaves 1 where the kernel's rightmost tap crosses the edge
        # (u = 0.5 - radius * max x) and reaches 0 where its leftmost does (u = 0.5 - radius * min x).
        radius = 0.01
        xs = [x for x, _ in lf.MASK_NEAR_TAPS]
        self.assertAlmostEqual(max(xs), 0.96234, places=5)
        self.assertAlmostEqual(min(xs), -0.840144, places=5)

        def sweep(rot):
            step = radius / 400
            values = []
            for k in range(-1600, 1601):
                u = 0.5 + k * step
                values.append((u, lf.mask_near([u, 0.2, 0.5, 1.0], IDENTITY_ROWS,
                                               lambda tu, tv: 0.0 if tu < 0.5 else 1.0, rot, lf.MASK_NEAR_TAPS, radius)))
            return step, values

        step, values = sweep((1.0, 0.0))
        self.assertEqual((values[0][1], values[-1][1]), (1.0, 0.0))
        self.assertTrue(all(a[1] >= b[1] for a, b in zip(values, values[1:])))             # never brighter to the right
        self.assertEqual({round(v * 12) for _, v in values}, set(range(13)))               # one tap at a time
        first = next(u for u, v in values if v < 1.0)
        last = next(u for u, v in values if v == 0.0)
        self.assertAlmostEqual(first, 0.5 - radius * max(xs), delta=1.5 * step)
        self.assertAlmostEqual(last, 0.5 - radius * min(xs), delta=1.5 * step)
        self.assertAlmostEqual(last - first, radius * (max(xs) - min(xs)), delta=3 * step)   # 1.80 radii
        # Under any rotation the penumbra stays inside one radius on either side (the taps lie in the unit disk).
        for angle in (0.3, 1.1, 2.0, 3.7, 5.5):
            _, values = sweep((math.cos(angle), math.sin(angle)))
            first = next(u for u, v in values if v < 1.0)
            last = next(u for u, v in values if v == 0.0)
            self.assertGreater(first, 0.5 - radius)
            self.assertLess(last, 0.5 + radius)
            self.assertGreater(last - first, 1.5 * radius)
        self.assertTrue(all(x * x + y * y < 1.0 for x, y in lf.MASK_NEAR_TAPS))
        self.assertEqual(len(set(lf.MASK_NEAR_TAPS)), 12)

    def test_far_by_hand(self):
        P = [0.3125, 0.40625, 0.25, 1.0]       # on an 8 x 4 map: x = 2.5, y = 1.625; compared value 0.75
        # The four texels are (1, 0), (2, 0), (1, 1), (2, 1) with the weights of fx = 0.5, fy = 0.625:
        # (1 - fx)(1 - fy) = 0.1875, fx (1 - fy) = 0.1875, (1 - fx) fy = 0.3125, fx fy = 0.3125.
        weights = {(1, 0): 0.1875, (2, 0): 0.1875, (1, 1): 0.3125, (2, 1): 0.3125}
        for lit, want in weights.items():
            seen = []

            def texel(ix, iy):
                seen.append((ix, iy))
                return 0.1 if (ix, iy) == lit else 0.9
            self.assertAlmostEqual(lf.mask_far(P, IDENTITY_ROWS, texel, (8, 4)), want, places=12)
            self.assertEqual(sorted(seen), sorted(weights))
        self.assertAlmostEqual(lf.mask_far(P, IDENTITY_ROWS, lambda ix, iy: 0.1 if ix == 1 else 0.9, (8, 4)), 0.5, places=12)
        # A division by rows[3] . P: the same point given with w = 2.
        self.assertAlmostEqual(lf.mask_far([2 * p for p in P], IDENTITY_ROWS, lambda ix, iy: 0.1 if iy == 1 else 0.9, (8, 4)),
                               0.625, places=12)

    def test_far_half_plane_is_a_ramp_of_one_texel(self):
        # Texels left of column 4 are lit. The mask is the linear filter of the lit / shadowed texels
        # floor(x) - 1 and floor(x): 1 up to x = 4, then down to 0 at x = 5.
        def at(x):
            return lf.mask_far([x / 8, 0.3, 0.5, 1.0], IDENTITY_ROWS, lambda ix, iy: 0.0 if ix < 4 else 1.0, (8, 8))
        for x, want in ((2.5, 1.0), (3.999, 1.0), (4.0, 1.0), (4.25, 0.75), (4.5, 0.5), (4.75, 0.25), (5.0, 0.0), (6.3, 0.0)):
            self.assertAlmostEqual(at(x), want, places=9, msg=f"x = {x}")

    def test_world_rows_and_screen_rows_give_the_same_mask(self):
        """The game hands the mask shaders screen-to-cascade rows (the world-to-cascade rows times the
        screen-to-world rows c116..c119); a native pass has the world position and the world rows. Both
        are the same function: the rows are applied to a homogeneous point and divided by their own w."""
        rng = random.Random(11)
        for k in range(50):
            world_rows = [[rng.uniform(-0.05, 0.05) for _ in range(3)] + [rng.uniform(0.3, 0.7)] for _ in range(3)]
            world_rows.append([0.0, 0.0, 0.0, 1.0])                                        # orthographic
            to_world = [[rng.uniform(-1, 1) for _ in range(4)] for _ in range(3)]
            to_world.append([rng.uniform(-0.1, 0.1) for _ in range(3)] + [rng.uniform(0.8, 1.3)])
            screen_rows = [[sum(world_rows[i][k] * to_world[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
            S = [rng.uniform(-1, 1), rng.uniform(-1, 1), rng.random(), 1.0]
            Ph = [lf.dot(row, S) for row in to_world]
            Pw = [x / Ph[3] for x in Ph]

            def smooth(u, v):
                return 0.5 + 0.4 * math.sin(40 * u) * math.cos(31 * v)

            def texel(ix, iy):
                return 0.5 + 0.4 * math.sin(0.7 * ix + 1.3 * iy)

            rot = (math.cos(k * 0.4), math.sin(k * 0.4))
            a = lf.mask_near(S, screen_rows, smooth, rot, lf.MASK_NEAR_TAPS, 2.5 / 1024)
            self.assertEqual(lf.mask_near(Ph, world_rows, smooth, rot, lf.MASK_NEAR_TAPS, 2.5 / 1024), a)
            self.assertEqual(lf.mask_near(Pw, world_rows, smooth, rot, lf.MASK_NEAR_TAPS, 2.5 / 1024), a)
            b = lf.mask_far(S, screen_rows, texel, (1024, 1024))
            self.assertAlmostEqual(lf.mask_far(Pw, world_rows, texel, (1024, 1024)), b, places=6)

    def test_screen_point(self):
        # Pixel (10, 20) of 1120 x 720 with depth 0.25: X = (2 * 10 + 1) / 1120 - 1, -Y = 1 - (2 * 20 + 1) / 720.
        S = lf.mask_screen_point((10.0, 20.0), 0.25, (1 / 1120, 1 / 720), (0.0, 0.0))
        self.assertAlmostEqual(S[0], 21 / 1120 - 1, places=12)
        self.assertAlmostEqual(S[1], 1 - 41 / 720, places=12)
        self.assertEqual(S[2:], [0.75, 1.0])
        u, v = lf.mask_depth_uv((10.0, 20.0), (1 / 1120, 1 / 720), (0.25, -0.5))      # the pixel's centre plus the offset
        self.assertAlmostEqual(u, 10.75 / 1120, places=12)
        self.assertAlmostEqual(v, 20.0 / 720, places=12)
        # The position register is read through its absolute value.
        self.assertEqual(lf.mask_screen_point((-10.0, -20.0), 0.25, (1 / 1120, 1 / 720), (0.0, 0.0)), S)

    def test_the_twelve_taps_are_the_literal_table(self):
        # c250..c255 of 0F99... as the emulated GPU held them at every execution of the shader in four logs
        # (out/native-evidence/shadows/task2_gpu_log_mask_registers.txt); the x of a tap and its y stand in
        # different registers (lighting-formulas.md, "Shadows").
        lit = lc.MASK_LITERALS["0F99810F072BF0FC"]
        self.assertEqual(lc.mask_taps(lit), [tuple(p) for p in lf.MASK_NEAR_TAPS])
        self.assertEqual(sorted(lit), list(range(248, 256)))
        self.assertEqual(lit[248][0], 10.0)
        self.assertAlmostEqual(lit[248][1], 1 / 12, places=8)
        self.assertEqual(lit[248][2:], [-1.0, 2.0])
        self.assertEqual(lit[249][:3], [0.0, 1.0, 0.5])
        self.assertAlmostEqual(lit[249][3], 0.1, places=8)
        self.assertEqual(lc.MASK_LITERALS["1B4B5AE12EF270CA"], {254: [0.5, 1.0, 0.0, -1.0], 255: [2.0, 0.0, 0.0, 0.0]})
        # The first of the published 12-tap Poisson disc, to five places.
        self.assertIn((-0.32621, -0.40581), [(round(x, 5), round(y, 5)) for x, y in lf.MASK_NEAR_TAPS])

    def test_zeros_and_non_finite_inputs_give_finite_numbers(self):
        bad = (0.0, math.inf, -math.inf, math.nan)
        zero_rows = [[0.0] * 4 for _ in range(4)]
        P = [0.3, 0.6, 0.25, 1.0]
        # A projection without a finite w, u, v or depth: the cascade's term is 1 (no shadow from a map
        # that cannot be addressed), and the sampler is not called.
        def never(*a):
            raise AssertionError("sampled")
        self.assertEqual(lf.mask_near(P, zero_rows, never, (1.0, 0.0), lf.MASK_NEAR_TAPS, 0.01), 1.0)
        self.assertEqual(lf.mask_far(P, zero_rows, never, (1024, 1024)), 1.0)
        for b in bad[1:]:
            rows = [list(r) for r in IDENTITY_ROWS]
            rows[1][1] = b
            self.assertEqual(lf.mask_near(P, rows, never, (1.0, 0.0), lf.MASK_NEAR_TAPS, 0.01), 1.0)
            self.assertEqual(lf.mask_far(P, rows, never, (1024, 1024)), 1.0)
            self.assertEqual(lf.mask_far([b, 0.6, 0.25, 1.0], IDENTITY_ROWS, never, (1024, 1024)), 1.0)
            self.assertEqual(lf.mask_far(P, IDENTITY_ROWS, never, (b, 1024)), 1.0)
        self.assertEqual(lf.mask_far(P, IDENTITY_ROWS, never, (0, 1024)), 1.0)
        # Non-finite rotation, radius or map values: finite, in [0, 1].
        for b in bad:
            for value in (lf.mask_near(P, IDENTITY_ROWS, lambda u, v: 0.5, (b, b), lf.MASK_NEAR_TAPS, 0.01),
                          lf.mask_near(P, IDENTITY_ROWS, lambda u, v: 0.5, (1.0, 0.0), lf.MASK_NEAR_TAPS, b),
                          lf.mask_near(P, IDENTITY_ROWS, lambda u, v: b, (1.0, 0.0), lf.MASK_NEAR_TAPS, 0.01),
                          lf.mask_far(P, IDENTITY_ROWS, lambda ix, iy: b, (1024, 1024))):
                self.assertTrue(math.isfinite(value) and 0.0 <= value <= 1.0, (b, value))
        # The combination: a zero blend width is a step at split[0]; any d, slope and split give a finite value.
        self.assertEqual(lf.mask_combine(19.0, 0.25, 0.75, [20.0, 35.0, 0.0, 0.5], 0.1), 0.25)
        self.assertEqual(lf.mask_combine(21.0, 0.25, 0.75, [20.0, 35.0, 0.0, 0.5], 0.1), 0.75)
        self.assertEqual(lf.mask_combine(math.inf, 0.25, 0.75, [20.0, 35.0, 2.0, 0.5], 0.1), 0.5)
        for d in bad:
            for s in bad:
                for slope in bad:
                    for split in ([s, 35.0, 2.0, 0.5], [20.0, s, 2.0, 0.5], [20.0, 35.0, s, 0.5], [20.0, 35.0, 2.0, s]):
                        value = lf.mask_combine(d, 0.25, 0.75, split, slope)
                        self.assertTrue(math.isfinite(value), (d, split, slope, value))
        # The whole shaders with every input zero, and with a world position that has no finite w.
        zero = dict(pixel=(0.0, 0.0), sample_depth=lambda u, v: 0.0, depth_scale=(0.0, 0.0), depth_offset=(0.0, 0.0),
                    screen_to_world=zero_rows, eye=[0.0] * 3, split=[0.0] * 4, near_rows=zero_rows, far_rows=zero_rows,
                    sample_rot=lambda u, v: (0.0, 0.0), rot_offset=(0.0, 0.0), rot_scale=(0.0, 0.0),
                    offsets=lf.MASK_NEAR_TAPS, radius=0.0, sample_near=lambda u, v: 0.0,
                    sample_far_texel=lambda ix, iy: 0.0, far_size=(1024, 1024))
        self.assertTrue(math.isfinite(lf.mask_0f99(**zero)))
        for b in bad:
            args = dict(zero, sample_depth=lambda u, v, b=b: b, split=[20.0, 35.0, 2.0, 0.5],
                        screen_to_world=[[b] * 4 for _ in range(4)], eye=[b] * 3)
            self.assertEqual(lf.mask_0f99(**args), 0.5, b)          # no position: beyond the shadow distance
            value, alpha = lf.mask_1b4b(pixel=(0.0, 0.0), sample_depth=lambda u, v, b=b: b, depth_scale=(b, b),
                                        depth_offset=(b, b), far_rows=zero_rows, sample_far_texel=lambda ix, iy: b,
                                        far_size=(1024, 1024), start=b, fade_start=b, fade_width=b, fade_to=0.5)
            self.assertTrue(math.isfinite(value) and alpha in (0.0, 1.0), (b, value, alpha))


class MaskSynthetic(unittest.TestCase):
    """Always runs: the mask checker on the hand-written shader of the mask's form."""

    def check(self, family="mask", taps=synthetic_taps, **kw):
        return lc.check_mask(family, "0F99810F072BF0FC", trials=80, seed=7, text=MASK_SYNTHETIC,
                             literals=MASK_SYNTHETIC_LITERALS, taps=taps, **kw)

    def test_the_right_formulas_agree_in_every_trial(self):
        for family in ("mask", "mask-near", "mask-far"):
            res = self.check(family)
            self.assertEqual((res.agree, res.trials), (80, 80), (family, res.first_bad))
            self.assertLess(res.worst, 1e-9)

    def test_the_trials_reach_every_part_of_the_shader(self):
        seen = lc.mask_regimes("0F99810F072BF0FC", trials=80, seed=7, literals=MASK_SYNTHETIC_LITERALS, taps=synthetic_taps)
        for name in ("near only", "blend", "far only", "fade", "beyond"):
            self.assertGreaterEqual(seen.get(name, 0), 5, seen)
        self.assertGreaterEqual(seen["equal depth"], 10, seen)       # trials whose maps hold the compared value itself

    def test_wrong_formulas_fail(self):
        for name, (helper, form) in WRONG_MASK.items():
            with self.subTest(wrong=name):
                with mock.patch.object(lf, helper, form):
                    res = self.check()
                self.assertFalse(res.ok, f"{res.agree}/{res.trials} agree with: {name}")
                self.assertIsNotNone(res.first_bad)

    def test_a_wrong_tap_offset_fails(self):
        for name, wrong in (("x and y of the second tap exchanged", lambda lit: [synthetic_taps(lit)[0], synthetic_taps(lit)[1][::-1]]),
                            ("the taps paired within a register", lambda lit: [(lit[254][0], lit[254][1]), (lit[253][0], lit[253][1])]),
                            ("one tap moved by a tenth", lambda lit: [synthetic_taps(lit)[0], (lit[254][1] + 0.1, lit[253][1])])):
            with self.subTest(wrong=name):
                self.assertFalse(self.check(taps=wrong).ok)

    def test_the_parts_alone_catch_their_own_errors(self):
        with mock.patch.object(lf, "mask_near", near_with(LT)):
            self.assertFalse(self.check("mask-near").ok)
            self.assertTrue(self.check("mask-far").ok)
        with mock.patch.object(lf, "mask_far", far_with(GT)):
            self.assertFalse(self.check("mask-far").ok)
            self.assertTrue(self.check("mask-near").ok)

    def test_no_trials_is_not_agreement(self):
        self.assertFalse(lc.check_mask("mask", "0F99810F072BF0FC", trials=0, text=MASK_SYNTHETIC,
                                       literals=MASK_SYNTHETIC_LITERALS, taps=synthetic_taps).ok)


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class MaskRealDumps(unittest.TestCase):
    TRIALS = 80

    def test_both_shaders_agree_in_every_family(self):
        self.assertEqual(lc.MASK_FAMILIES, {"mask-near": ["0F99810F072BF0FC"],
                                            "mask-far": ["0F99810F072BF0FC", "1B4B5AE12EF270CA"],
                                            "mask": ["0F99810F072BF0FC", "1B4B5AE12EF270CA"]})
        for family, shaders in lc.MASK_FAMILIES.items():
            for shader in shaders:
                with self.subTest(family=family, shader=shader):
                    res = lc.check_mask(family, shader, trials=self.TRIALS)
                    self.assertTrue(res.ok, f"{res.agree}/{res.trials} worst {res.worst:.3g} {res.first_bad}")
                    self.assertEqual(res.trials, self.TRIALS)
                    self.assertLess(res.worst, 1e-9)

    def test_the_trials_reach_every_part_of_both_shaders(self):
        seen = lc.mask_regimes("0F99810F072BF0FC", trials=self.TRIALS)
        for name in ("near only", "blend", "far only", "fade", "beyond", "equal depth", "random literals"):
            self.assertGreaterEqual(seen.get(name, 0), 5, seen)
        seen = lc.mask_regimes("1B4B5AE12EF270CA", trials=self.TRIALS)
        for name in ("not far", "far", "fade", "beyond", "equal depth"):
            self.assertGreaterEqual(seen.get(name, 0), 5, seen)

    def test_wrong_formulas_fail_on_the_real_dumps(self):
        for name, (helper, form) in WRONG_MASK.items():
            for shader in lc.MASK_FAMILIES["mask"]:
                if shader.startswith("1B4B") and (name in NEAR_ONLY or helper == "mask_combine"):
                    continue                    # the far shader has no near taps and fades by depth (below)
                with self.subTest(wrong=name, shader=shader):
                    with mock.patch.object(lf, helper, form):
                        res = lc.check_mask("mask", shader, trials=self.TRIALS)
                    self.assertFalse(res.ok, f"{res.agree}/{res.trials} agree with: {name}")

    def test_wrong_forms_of_the_far_shader_fail(self):
        right = lf.mask_1b4b
        wrong = {
            "fade to 1": lambda **a: right(**dict(a, fade_to=1.0)),
            "fade from the start depth": lambda **a: right(**dict(a, fade_start=a["start"])),
            "alpha 1 everywhere": lambda **a: (right(**a)[0], 1.0),
            "the near side lit": lambda **a: right(**a) if right(**a)[1] else (1.0, 0.0),
            "another start depth": lambda **a: right(**dict(a, start=a["start"] + 0.05)),
        }
        for name, form in wrong.items():
            with self.subTest(wrong=name):
                self.assertFalse(lc.check_mask("mask", "1B4B5AE12EF270CA", trials=self.TRIALS, formula=form).ok)

    def test_a_wrong_tap_offset_fails(self):
        right = lc.mask_taps
        wrong = {
            "x and y of one tap exchanged": lambda lit: right(lit)[:11] + [right(lit)[11][::-1]],
            "pairs read within a register": lambda lit: [(lit[r][i], lit[r][i + 1]) for r in range(250, 256) for i in (0, 2)],
            "one tap moved by a tenth of the radius": lambda lit: [(right(lit)[0][0] + 0.1, right(lit)[0][1])] + right(lit)[1:],
            "eleven taps": lambda lit: right(lit)[:11],
        }
        for name, taps in wrong.items():
            with self.subTest(wrong=name):
                self.assertFalse(lc.check_mask("mask", "0F99810F072BF0FC", trials=self.TRIALS, taps=taps).ok)

    def test_command_line(self):
        for family, lines in (("mask-near", 1), ("mask-far", 2), ("mask", 2)):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = lc.main(["--family", family, "--trials", "16"])
            self.assertEqual(code, 0, out.getvalue())
            self.assertEqual(out.getvalue().count("16/16 agree"), lines, out.getvalue())
            self.assertIn("0F99810F072BF0FC", out.getvalue())
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(lc.main(["--family", "mask", "--shader", "1B4B5AE12EF270CA", "--trials", "8"]), 0)
        self.assertNotIn("0F99810F072BF0FC", out.getvalue())
        with mock.patch.object(lf, "mask_combine", fade_to_one), contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(lc.main(["--family", "mask", "--trials", "40"]), 1)
        self.assertIn("first disagreement, trial ", out.getvalue())
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            lc.main(["--list"])
        self.assertIn("mask-near: ps 0F99810F072BF0FC", out.getvalue())


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class LitFamiliesWithTheMask(unittest.TestCase):
    """Plan Task 2, step 3: every lit family is checked with the screen mask as an input. A formula that
    takes the mask as 1 (the native pass before shadows) must disagree with the microcode in every pair
    that has the mask, and at a mask of 1 every function is what it was."""
    TRIALS = 24
    MASKED = ("terrain-layer", "terrain-layer-multi", "object", "object-multi", "tree-trunk", "tree-trunk-multi",
              "tree-leaf", "tree-leaf-multi", "plant", "plant-multi", "plant-lit", "plant-lit-multi")

    def test_every_family_but_the_far_terrain_has_the_mask(self):
        self.assertEqual(sorted(self.MASKED + ("terrain-far",)), sorted(lc.FAMILIES))

    def test_the_trials_draw_the_mask_at_random(self):
        for family in self.MASKED:
            ps, vs = lc.FAMILIES[family][0]
            _, make_case = lc.DRIVERS[family](ps, vs, DUMPS)
            rng = random.Random(3)
            masks = [make_case(rng).sampler(8, (0.0, 0.0), {})[0] for _ in range(12)]
            self.assertEqual(len(set(masks)), 12, family)
            self.assertTrue(all(0.0 <= m < 1.0 for m in masks), family)
            self.assertLess(min(masks), 0.4, family)
            self.assertGreater(max(masks), 0.6, family)

    def test_a_formula_without_the_mask_fails_in_every_pair(self):
        single, multi, leaf = lf.sun_gate_single, lf.sun_gate_multi, lf.leaf_mask
        for family in self.MASKED:
            for ps, vs in lc.FAMILIES[family]:
                with self.subTest(family=family, ps=ps, vs=vs):
                    with mock.patch.object(lf, "sun_gate_single", lambda mask=1.0, facing=1.0: single(1.0, facing)), \
                            mock.patch.object(lf, "sun_gate_multi",
                                              lambda strength, mask=1.0, facing=1.0, **cone: multi(strength, 1.0, facing, **cone)), \
                            mock.patch.object(lf, "leaf_mask", lambda mask, gain, bias: leaf(1.0, gain, bias)):
                        res = lc.check_pair(family, ps, vs, trials=self.TRIALS)
                    self.assertFalse(res.ok, f"{res.agree}/{res.trials} agree without the mask")

    def test_a_multi_light_pair_with_the_sun_in_its_slot_has_the_mask_too(self):
        single, leaf = lf.sun_gate_single, lf.leaf_mask
        for family in lc.MULTI_SUN_FAMILIES:
            ps, vs = lc.FAMILIES[family][0]
            slot = lc.GAME_SUN_SLOT[lc.light_count(ps, vs)]
            with self.subTest(family=family):
                self.assertTrue(lc.check_multi_sun(family, ps, vs, slot, trials=self.TRIALS).ok)
                with mock.patch.object(lf, "sun_gate_single", lambda mask=1.0, facing=1.0: single(1.0, facing)), \
                        mock.patch.object(lf, "leaf_mask", lambda mask, gain, bias: leaf(1.0, gain, bias)):
                    self.assertFalse(lc.check_multi_sun(family, ps, vs, slot, trials=self.TRIALS).ok)

    def test_at_a_mask_of_1_nothing_changes(self):
        self.assertEqual(lf.sun_gate_single(), 1.0)
        self.assertEqual(lf.sun_gate_single(mask=1.0, facing=0.25), 0.25)
        self.assertEqual(lf.sun_gate_multi(1.0), 1.0)
        self.assertEqual(lf.sun_gate_multi(1.0, mask=0.25), 0.25)
        m1, m2 = lf.leaf_mask(1.0, 0.6, 0.4)
        self.assertAlmostEqual(m1, 1.0, places=12)
        self.assertAlmostEqual(m2, 1.0, places=12)
        m1, m2 = lf.leaf_mask(0.0, 0.6, 0.4)                    # full shadow: 0.4 on the front light, 0.64 on the back
        self.assertAlmostEqual(m1, 0.4, places=12)
        self.assertAlmostEqual(m2, 0.64, places=12)
        m1, m2 = lf.leaf_mask(0.5, 0.6, 0.4)
        self.assertAlmostEqual(m1, 0.7, places=12)
        self.assertAlmostEqual(m2, 0.82, places=12)


class MaskPinnedForTheRenderer(unittest.TestCase):
    """Shadows plan Task 5: numbers of the mask functions that the native renderer's pure mirrors are
    held against (tests/native/test_clay_logic.cpp, cases 564 to 566, has the same inputs and the same
    literals). The inputs are frame 1 of the bridge capture of 2026-10-09 (the inverse of the
    view-projection, the scene's pixel c12, c19 and c36..c43), every number single precision as the
    renderer holds it. The maps and the rotation tile are a value per texel that a float holds
    exactly; they hold the NATIVE maps' value n (the caster's depth, 1 = none), which the game's
    functions get as 1 - n: the native pass compares z < n and z <= n where the game compares
    1 - z > 1 - n and 1 - z >= 1 - n."""
    W, H = 1120, 720
    INV_VIEW_PROJ = [[0.5143311619758606, 0.08813292533159256, -820.677490234375, 821.5447387695312],
                     [-0.8844040036201477, 0.05125229433178902, -1573.3095703125, 1573.8358154296875],
                     [8.367820555577055e-06, 0.5663930773735046, -501.68060302734375, 501.51348876953125],
                     [1.6652556666940654e-07, 7.833264703549503e-09, -9.999471664428711, 9.999671936035156]]
    NEAR = [[0.02583850733935833, 0.0, 0.021059028804302216, -2.8588876724243164],
            [-0.0006741421530023217, -0.03331625089049339, 0.0008271429105661809, 5.92822265625],
            [0.005491321906447411, -0.0002783896343316883, -0.006737612187862396, 0.743424117565155],
            [0.0, 0.0, 0.0, 1.0]]
    FAR = [[0.011073646135628223, 0.0, 0.009025298990309238, -1.05615234375],
           [-0.0002889182942453772, -0.014278394170105457, 0.0003544900973793119, 2.937011480331421],
           [0.004074114840477705, -0.00020654266700148582, -0.004998760763555765, 0.6168575882911682],
           [0.0, 0.0, 0.0, 1.0]]
    SPLIT = [20.0, 35.0, 2.0, 0.48273685574531555]
    EYE = [82.07209777832031, 157.33926391601562, 50.17070388793945]
    RADIUS, TAP_WEIGHT, SLOPE = 0.00244140625, 0.0833333358168602, 0.10000000149011612     # floats: 2.5 / 1024, 1 / 12, 0.1
    INV_W, INV_H = 0.0008928571478463709, 0.0013888889225199819                             # floats: 1 / 1120, 1 / 720

    @staticmethod
    def near_texel(ix, iy):
        return ((ix * 37 + iy * 101) % 129) / 128.0

    @staticmethod
    def far_texel(ix, iy):
        return ((ix * 53 + iy * 17 + 5) % 129) / 128.0

    @staticmethod
    def clamp(v):
        return min(max(v, 0), 1023)

    def sample_near(self, u, v):
        return 1.0 - self.near_texel(self.clamp(math.floor(u * 1024)), self.clamp(math.floor(v * 1024)))

    def sample_far(self, ix, iy):
        return 1.0 - self.far_texel(self.clamp(ix), self.clamp(iy))

    @staticmethod
    def mul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

    def mask(self, px, py, depth, rotate):
        def tile(u, v):
            tx, ty = math.floor(u * 32) % 32, math.floor(v * 32) % 32
            return (((tx * 73 + ty * 29) % 256) / 255.0, ((tx * 31 + ty * 57) % 256) / 255.0)
        return lf.mask_0f99(
            pixel=(float(px), float(py)), sample_depth=lambda u, v: 1.0 - depth, depth_scale=(self.INV_W, self.INV_H),
            depth_offset=(0.0, 0.0), screen_to_world=self.INV_VIEW_PROJ, eye=self.EYE, split=self.SPLIT,
            near_rows=self.mul(self.NEAR, self.INV_VIEW_PROJ), far_rows=self.mul(self.FAR, self.INV_VIEW_PROJ),
            sample_rot=tile if rotate else (lambda u, v: (1.0, 0.5)), rot_offset=(0.5, 0.5), rot_scale=(1 / 32, 1 / 32),
            offsets=lf.MASK_NEAR_TAPS, radius=self.RADIUS, sample_near=self.sample_near, sample_far_texel=self.sample_far,
            far_size=(1024, 1024), tap_weight=self.TAP_WEIGHT, far_reach=10.0, slope=self.SLOPE)

    def test_combine(self):
        for d, near, far, want in ((10.0, 0.25, 0.75, 0.25), (21.0, 0.25, 0.75, 0.5), (30.0, 0.25, 0.75, 0.75),
                                   (40.0, 0.25, 0.75, 0.6163684258813922), (50.0, 0.25, 0.75, 0.48273685574531555),
                                   (20.5, 1.0, 0.0, 0.75), (36.5, 0.0, 1.0, 0.9224105272056241)):
            self.assertAlmostEqual(lf.mask_combine(d, near, far, self.SPLIT, self.SLOPE), want, places=12)

    def test_the_two_terms_of_a_world_point(self):
        rot = (0.6000000238418579, 0.800000011920929)                                        # floats 0.6, 0.8
        for P, near, far in (([84.5, 150.25, 44.0, 1.0], 0.0833333358168602, 0.3243241310119629),
                             ([90.0, 140.0, 43.5, 1.0], 0.0833333358168602, 0.6628598777695416),
                             ([70.125, 165.5, 46.0, 1.0], 0.0833333358168602, 0.0),
                             ([100.0, 120.0, 40.0, 1.0], 0.0, 0.8823890686035156)):
            self.assertAlmostEqual(lf.mask_near(P, self.NEAR, self.sample_near, rot, lf.MASK_NEAR_TAPS, self.RADIUS, self.TAP_WEIGHT),
                                   near, places=12)
            self.assertAlmostEqual(lf.mask_far(P, self.FAR, self.sample_far, (1024, 1024)), far, places=10)

    def test_the_mask_of_a_pixel_in_every_regime(self):
        # px, py, the native depth (a float), the distance from the eye, the mask with the tile, without.
        pins = ((560, 600, 0.9821628332138062, 5.999995277693097, 0.1666666716337204, 0.1666666716337204),
                (300, 500, 0.9909855127334595, 12.500029722610465, 0.0833333358168602, 0.1666666716337204),
                (700, 420, 0.994562029838562, 18.9999053127623, 0.0, 0.0),
                (640, 400, 0.9950790405273438, 20.499951274353368, 0.168626222122592, 0.10612419002461002),
                (200, 380, 0.9944530129432678, 21.500103049226347, 0.30675134735710674, 0.30675134735710674),
                (900, 350, 0.9956575036048889, 27.000158411112753, 0.0, 0.0),
                (500, 340, 0.9970599412918091, 34.00031121842236, 0.9111211026206659, 0.9111211026206659),
                (333, 333, 0.9971696734428406, 37.99976545013204, 0.183352533656075, 0.183352533656075),
                (800, 300, 0.9975280165672302, 44.00000617981933, 0.4344634749674494, 0.4344634749674494),
                (100, 250, 0.9978243708610535, 60.000769539675034, 0.48273685574531555, 0.48273685574531555),
                (17, 5, 1.0, 7584.099111323902, 0.48273685574531555, 0.48273685574531555),
                (1119, 719, 1.0, 7689.370675434363, 0.48273685574531555, 0.48273685574531555))
        regimes = set()
        for px, py, depth, d, rotated, unrotated in pins:
            self.assertAlmostEqual(self.mask(px, py, depth, True), rotated, places=10)
            self.assertAlmostEqual(self.mask(px, py, depth, False), unrotated, places=10)
            S = lf.mask_screen_point((float(px), float(py)), 1.0 - depth, (self.INV_W, self.INV_H), (0.0, 0.0))
            Ph = [lf.dot(row, S) for row in self.INV_VIEW_PROJ]
            got = math.sqrt(sum((Ph[i] / Ph[3] - self.EYE[i]) ** 2 for i in range(3)))
            self.assertAlmostEqual(got / d, 1.0, places=9)
            regimes.add("near" if d < 20 else "blend" if d < 22 else "far" if d < 35 else "fade" if d < 45 else "beyond")
        self.assertEqual(regimes, {"near", "blend", "far", "fade", "beyond"})
        # A pixel at the clear depth (the native 1, the game's 0) is one beyond the shadow distance: split[3], not 1.
        self.assertEqual(self.mask(17, 5, 1.0, True), self.SPLIT[3])

    def test_the_native_compare_is_the_games(self):
        # The game's verdicts on 1 - z and 1 - n are z < n (near) and z <= n (far) on the native values.
        P = [0.3, 0.6, 0.25, 1.0]
        identity = [[1.0, 0, 0, 0], [0, 1.0, 0, 0], [0, 0, 1.0, 0], [0, 0, 0, 1.0]]
        for n, near_lit, far_lit in ((1.0, True, True), (0.5, True, True), (0.25, False, True), (0.125, False, False)):
            near = lf.mask_near(P, identity, lambda u, v, n=n: 1.0 - n, (1.0, 0.0), lf.MASK_NEAR_TAPS, self.RADIUS, self.TAP_WEIGHT)
            far = lf.mask_far(P, identity, lambda ix, iy, n=n: 1.0 - n, (1024, 1024))
            self.assertEqual((near > 0.5, far > 0.5), (near_lit, far_lit), n)
            self.assertEqual((0.25 < n, 0.25 <= n), (near_lit, far_lit), n)



@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class CampfireTrunkSevenLights(unittest.TestCase):
    def test_trunk_seven_light_pair_and_alpha(self):
        ps, vs = "7928BAA7A6691A5D", "475EC9F795E5EDBB"
        self.assertIn((ps, vs), lc.FAMILIES["tree-trunk-multi"])
        self.assertEqual(lc.light_count(ps, vs), 7)
        self.assertIn(ps, lc._UNPREMULTIPLY)
        text, make_case = lc._object_driver(ps, vs, DUMPS, multi=True, sun_slot=4)
        rng = random.Random(7928)
        for _ in range(24):
            case = make_case(rng)
            machine = lc.ue.Machine(text, case.consts, case.sampler, regs=case.regs, bools=case.bools)
            got = machine.run()["oC0"]
            alpha = case.sampler(0, (0, 0), {})[3] * case.consts[26][3] * case.consts[15][3]
            self.assertAlmostEqual(got[3], alpha)
        self.assertTrue(lc.check_pair("tree-trunk-multi", ps, vs, trials=24).ok)
        self.assertTrue(lc.check_multi_sun("tree-trunk-multi", ps, vs, 4, trials=24).ok)


class CampfireSevenLights(unittest.TestCase):
    PAIRS = (("E0996BF4D9539D38", "C30A97D946FA2BE4"),
             ("0981301D645E2517", "FB68A7F2301210E1"))

    def test_seven_light_pairs_have_the_captured_sun_slot(self):
        for ps, vs in self.PAIRS:
            self.assertEqual(lc.light_count(ps, vs), 7)
            self.assertEqual(lc.family_of(ps, vs), "terrain-layer-multi")
        self.assertEqual(lc.GAME_SUN_SLOT[7], 4)

    def test_second_bank_sun_and_first_bank_sun_are_isolated(self):
        for slot in (1, 4, 5, 6):
            c = lc._light_block(random.Random(763))
            lc._extra_lights(random.Random(764), c, lights=7)
            direction, colour = lc._sun_in_slot(random.Random(765), c, slot, lights=7)
            self.assertEqual(c[80 + slot], list(direction) + [0.0])
            self.assertEqual(c[87 + slot][:3], colour)
            for other in range(7):
                bank, component = (112, other) if other < 4 else (113, other - 4)
                self.assertEqual(c[bank][component], float(other == slot))
                if other != slot:
                    self.assertEqual(c[87 + other], [0.0] * 4)
                    self.assertEqual(c[80 + other][3], 1.0)

    @unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
    def test_game_sun_matches_microcode_and_needs_shadow_mask(self):
        for ps, vs in self.PAIRS:
            good = lc.check_multi_sun("terrain-layer-multi", ps, vs, 4, trials=24)
            self.assertTrue(good.ok, good.first_bad)
            single = lf.sun_gate_single
            with mock.patch.object(lf, "sun_gate_single", lambda mask=1.0, facing=1.0: single(1.0, facing)):
                wrong = lc.check_multi_sun("terrain-layer-multi", ps, vs, 4, trials=24)
            self.assertFalse(wrong.ok)

if __name__ == "__main__":
    unittest.main()
