"""The lake's surface as closed formulas (light_formulas.water_vertex, water_alpha, water_kept, water_opaque_dead,
water_pixel and its parts) and their machine check against the microcode of vertex shader BEAD84BD72072E0E and pixel
shaders A17D8AEC3A817D45 (the surface) and F6D98C7B4D98438B (the dead opaque twin): water_check.py. The water
sub-project, plan Tasks 2 and 3, spec criteria 1 (its first half: made inputs; the captured rows are plan Task 4's)
and 2 part (a)."""
import math
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import water_check  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"


def water_check_bits(x):
    """A float's single-precision bits (the pins' registers are bit patterns)."""
    return struct.unpack("<I", struct.pack("<f", x))[0]
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"
HAVE = (DUMPS / "shader_A17D8AEC3A817D45.ucode.frag").exists()
C141 = (2.003, 2.0, 0.0015, 0.0588)        # every captured row (R B1)


def vconsts(over=None):
    c = {0: [1.0, 0.0, 0.0, 0.0], 1: [0.0, 1.0, 0.0, 0.0], 2: [0.0, 0.0, 1.0, 0.0], 3: [0.0, 0.0, 0.0, 1.0],
         115: [1.0, 2.0, 9.0, 9.0], 116: [-3.0, 4.0, 9.0, 9.0], 117: [0.5, 0.25, 9.0, 9.0], 118: [0.125, 2.0, 9.0, 9.0]}
    c.update(over or {})
    return c


class Vertex(unittest.TestCase):
    P = (100.0, 200.0, 20.0)

    def test_the_position_is_the_rows_and_nothing_is_added(self):                # instr 5 to 8, 11
        out = lf.water_vertex(self.P, (0.25, 0.75), vconsts())
        self.assertEqual(tuple(out["pos"]), (100.0, 200.0, 20.0, 1.0))
        self.assertEqual(tuple(out["place"]), self.P)
        moved = lf.water_vertex(self.P, (0.25, 0.75), vconsts({3: [0.0, 0.0, 1.0, 5.0]}))
        self.assertEqual(moved["pos"][3], 25.0)

    def test_two_coordinates_scroll_apart(self):                                 # instr 12, 13
        coord = lf.water_vertex(self.P, (0.25, 0.75), vconsts())["coord"]
        self.assertEqual(tuple(coord), (100.0 * 0.5 + 1.0, 200.0 * 0.25 + 2.0, 100.0 * 0.125 - 3.0, 200.0 * 2.0 + 4.0))

    def test_the_mask_coordinate_is_the_fetched_pair(self):                      # instr 17
        self.assertEqual(tuple(lf.water_vertex(self.P, (0.25, 0.75), vconsts())["mask"]), (0.25, 0.75))

    def test_the_height_does_not_reach_a_coordinate(self):
        a = lf.water_vertex((100.0, 200.0, 20.0), (0.0, 0.0), vconsts())["coord"]
        b = lf.water_vertex((100.0, 200.0, 99.0), (0.0, 0.0), vconsts())["coord"]
        self.assertEqual(tuple(a), tuple(b))

    def test_no_unread_component_reaches_the_vertex(self):
        a = lf.water_vertex(self.P, (0.25, 0.75), vconsts())
        other = vconsts()
        for reg in (115, 116, 117, 118):
            other[reg][2] = other[reg][3] = math.nan
        b = lf.water_vertex(self.P, (0.25, 0.75), other)
        self.assertEqual(a, b)

    def test_single_precision_rounds_every_step(self):
        c = vconsts({117: [0.1, 0.3, 9.0, 9.0]})
        double = lf.water_vertex((100.1, 200.3, 20.0), (0.25, 0.75), c)["coord"]
        single = lf.water_vertex((100.1, 200.3, 20.0), (0.25, 0.75), c, single=True)["coord"]
        for d, s in zip(double, single):
            self.assertEqual(s, water_check.f32(s))                              # a single-precision number
            self.assertLess(abs(d - s), 1e-4)
        self.assertNotEqual(tuple(double), tuple(single))


class Alpha(unittest.TestCase):
    def test_open_water_is_just_below_one_and_land_is_nothing(self):
        self.assertAlmostEqual(lf.water_alpha(0.0, C141), 0.997, places=6)     # m = 0: 1 - sat(2.003 - 2)
        self.assertEqual(lf.water_alpha(1.0, C141), 0.0)                        # m = 1: 1 - sat(2.003)
        self.assertEqual(lf.water_alpha(0.5, C141), 0.0)

    def test_the_two_shaders_kill_complementary_pixels(self):
        for alpha in (0.0, 0.5, 0.997):
            self.assertEqual(lf.water_kept(alpha, C141, "opaque"), "killed")
            self.assertNotEqual(lf.water_kept(alpha, C141, "surface"), "killed")
        self.assertEqual(lf.water_kept(1.0, C141, "surface"), "killed")
        self.assertNotEqual(lf.water_kept(1.0, C141, "opaque"), "killed")

    def test_at_or_below_the_threshold_nothing_is_shaded(self):
        self.assertEqual(lf.water_kept(0.0588, C141, "surface"), "skipped")
        self.assertEqual(lf.water_kept(0.0589, C141, "surface"), "shaded")

    def test_a_mask_that_is_no_number_is_alpha_one(self):                        # the saturate of no number is 0
        self.assertEqual(lf.water_alpha(math.nan, C141), 1.0)
        self.assertEqual(lf.water_kept(1.0, (2.003, 2.0, 0.0, math.nan), "opaque"), "skipped")

    def test_an_unknown_kind_is_refused(self):
        with self.assertRaises(ValueError):
            lf.water_kept(0.5, C141, "lake")

    def test_the_dead_condition(self):
        self.assertTrue(lf.water_opaque_dead(C141))
        self.assertFalse(lf.water_opaque_dead((2.0, 2.0, 0.0, 0.0588)))         # m = 0 gives alpha 1
        self.assertFalse(lf.water_opaque_dead((0.0, -1.0, 0.0, 0.0588)))        # m = 1 gives alpha 1
        self.assertFalse(lf.water_opaque_dead((5.0, 1.0, 0.0, 0.0588)))         # outside the magnitudes the margin was reasoned for
        self.assertFalse(lf.water_opaque_dead((math.nan, 2.0, 0.0, 0.0588)))
        self.assertFalse(lf.water_opaque_dead((2.0, math.nan, 0.0, 0.0588)))
        self.assertTrue(lf.water_opaque_dead((2.000002, 2.0, 0.0, 0.0588)))      # above the margin of 1e-6
        self.assertFalse(lf.water_opaque_dead((2.0000005, 2.0, 0.0, 0.0588)))    # below it (the single-precision rule is Task 10's)
        self.assertTrue(lf.water_opaque_dead((2.003, 2.0, math.nan, math.nan)))  # c141.z and c141.w are not the condition's


@unittest.skipUnless(HAVE, NO_DUMPS)
class Microcode(unittest.TestCase):
    TRIALS = 200                                                                 # the reduced count; `formulas` runs 2,000

    def test_the_vertex_formula_is_the_microcode(self):
        r = water_check.check_vertex(trials=self.TRIALS)
        self.assertEqual((r.failed, r.trials), (0, self.TRIALS), r.first_failure)

    def test_the_alpha_is_the_microcode_for_both_shaders(self):
        for kind in ("surface", "opaque"):
            r = water_check.check_alpha(trials=self.TRIALS, kind=kind)
            self.assertEqual(r.failed, 0)
            self.assertGreaterEqual(min(r.killed, r.skipped, r.shaded), self.TRIALS // 10)   # every fate is reached

    def test_mutations_fail(self):
        for m in water_check.VERTEX_MUTATIONS:
            self.assertGreater(water_check.check_vertex(trials=self.TRIALS, mutation=m).failed, 0, m)
        for m in water_check.ALPHA_MUTATIONS:
            self.assertGreater(water_check.check_alpha(trials=self.TRIALS, mutation=m).failed, 0, m)

    def test_an_unknown_mutation_is_refused(self):
        with self.assertRaises(ValueError):
            water_check.check_vertex(trials=1, mutation="none such")
        with self.assertRaises(ValueError):
            water_check.check_alpha(trials=1, mutation="rows")

    def test_the_dead_draw_is_dead_on_the_microcode(self):
        r = water_check.check_dead(trials=self.TRIALS)
        self.assertEqual(r.killed, r.trials)                                     # F6D9 killed in every trial
        self.assertGreater(r.control_kept, 0)                                    # and not killed where the condition fails
        self.assertEqual(r.control_kept, r.controls)
        self.assertEqual(r.single_killed, r.trials)                              # also with every step in single precision
        self.assertGreaterEqual(r.at_margin, self.TRIALS // 10)                  # trials at the margin itself
        self.assertEqual(r.levels, 256)                                          # every level a k_8 mask can hold

    def test_the_vertex_shader_reads_what_the_table_says(self):
        used = water_check.used_registers("vs")
        self.assertTrue(set(lf.WATER_SHADER["reg"]) | {0, 1, 2, 3} <= used)
        self.assertEqual(used - set(lf.WATER_SHADER["reg"]) - {0, 1, 2, 3}, {9, 113, 119, 120, 121, 122, 255})   # o0.w and o2: read by neither pixel shader

    def test_the_components_the_vertex_outputs_take_are_the_tables(self):
        self.assertEqual(water_check.read_components("vs"), lf.WATER_SHADER["vs_read"])

    def test_the_pixel_shaders_take_only_these_interpolator_components(self):
        for kind in ("surface", "opaque"):
            self.assertEqual(water_check.read_interpolators(kind), lf.WATER_SHADER["ps_in"], kind)


def pconsts(over=None):
    c = {r: [1e22, 1e22, 1e22, 1e22] for r in lf.WATER_SHADER["ps_reg"]}        # unread components are garbage
    c[0][:] = [0.0, 0.0, 1.0 / 1120.0, 1.0 / 720.0]
    c[19][:3] = [0.0, 0.0, 30.0]
    c[20][3] = 2.0
    c[28][:2] = [0.0, 1.0]                                                      # the fog's amount 0: the block changes nothing
    c[31][:2] = [2.0 / 3.0, 2.0 / 3.0]
    c[66][:3] = [0.0, 0.0, 1.0]
    c[67][:] = [1e6, 1e22, 0.0, 0.0]                                            # the fog starts beyond everything
    c[68][:3] = [0.0, 0.0, 0.0]
    c[116][0], c[117][0] = 0.0, 0.0
    c[126][:3] = [0.2, 0.4, 0.6]
    c[127][:3] = [0.1, 0.1, 0.1]
    c[128][0] = 1.0
    c[129][:2] = [0.0, 0.0]
    c[130][:2] = [0.0, 0.0]
    c[131][0] = 0.5
    c[136][0], c[137][0], c[138][0] = 1.0, 0.0, 128.0
    c[139][:2] = [1.0, 0.0]
    c[140][0] = 0.05
    c[141][:] = [2.003, 2.0, 1e22, 0.0588]
    c[142][:3] = [0.0, 0.0, -1.0]
    c[143][:3] = [1.0, 1.0, 1.0]
    c.update(over or {})
    return c


FLAT = (0.5, 0.5)                              # a normal sample without tilt: s = (0, 0), N = (0, 0, 1)
TEX = {"normal": lambda u, v: FLAT, 13: lambda u, v: (0.8, 0.6, 0.4), 14: lambda u, v: (0.0, 0.0, 0.0),
       8: lambda u, v: 1.0, "tf4": lambda a: (0.0, 0.0, 0.0), "tf5": lambda s: (1.0, 1.0, 1.0)}


class Pixel(unittest.TestCase):
    def test_a_flat_normal_is_straight_up_and_the_lookup_is_the_screen_place_scaled_about_the_centre(self):
        s, N = lf.water_normal(FLAT, FLAT, 0.3)
        self.assertEqual(tuple(s), (0.0, 0.0))
        self.assertEqual(tuple(N), (0.0, 0.0, 1.0))
        c = pconsts()
        (u, v), _ = lf.water_lookup((560.0, 360.0), s, c[0], c[31], c[129], c[130])
        self.assertAlmostEqual(u, 0.5)
        self.assertAlmostEqual(v, 0.5)
        (u, v), _ = lf.water_lookup((0.0, 720.0), s, c[0], c[31], c[129], c[130])
        self.assertAlmostEqual(u, 0.5 - 0.5 * 2.0 / 3.0)
        self.assertAlmostEqual(v, 0.5 + 0.5 * 2.0 / 3.0)

    def test_the_signs_of_the_position_register_do_not_reach_the_lookup(self):  # instr 22: r_abs[4]
        c = pconsts()
        a = lf.water_lookup((100.0, 500.0), (0.25, -0.5), c[0], c[31], c[129], c[130])
        b = lf.water_lookup((-100.0, -500.0), (0.25, -0.5), c[0], c[31], c[129], c[130])
        self.assertEqual(a, b)

    def test_the_distortion_moves_each_slot_by_its_own_register(self):
        c = pconsts({129: [0.8, 0.4, 1e22, 1e22], 130: [0.2, 0.1, 1e22, 1e22]})
        at13, at14 = lf.water_lookup((560.0, 360.0), (0.5, -0.25), c[0], c[31], c[129], c[130])
        self.assertAlmostEqual(at13[0], 0.5 + 0.5 * 0.8 / 2)
        self.assertAlmostEqual(at13[1], 0.5 - 0.25 * 0.4 / 2)
        self.assertAlmostEqual(at14[0], 0.5 + 0.5 * 0.2 / 2)
        self.assertAlmostEqual(at14[1], 0.5 - 0.25 * 0.1 / 2)

    def test_the_factor_c128x_shrinks_the_third_component_and_so_tilts_the_normal(self):
        tilted = ((0.75, 0.5), (0.75, 0.5))                                     # both samples lean along x
        _, whole = lf.water_normal(*tilted, 1.0)
        _, shrunk = lf.water_normal(*tilted, 0.3)
        self.assertLess(shrunk[2], whole[2])
        self.assertGreater(shrunk[0], whole[0])
        self.assertAlmostEqual(sum(x * x for x in shrunk), 1.0)

    def test_the_shadow_mask_is_read_at_the_screen_place_itself(self):          # instr 48: no scale, no normal
        c = pconsts({0: [0.25, -0.125, 1.0 / 1120.0, 1.0 / 720.0]})
        self.assertEqual(lf.water_shadow_at((-560.0, 360.0), c[0]), (0.5 + 0.25, 0.5 - 0.125))

    def test_looking_straight_down_is_the_deep_colour_and_grazing_the_other(self):
        base, F, V, d = lf.water_base((0.0, 0.0, 0.0), (0.0, 0.0, 30.0), (0.0, 0.0, 1.0), 0.0, (0.2, 0.4, 0.6), (0.1, 0.1, 0.1))
        self.assertEqual((F, d), (0.0, 30.0))
        self.assertEqual(tuple(base), (0.1, 0.1, 0.1))                           # c127
        base, F, _, _ = lf.water_base((1e6, 0.0, 0.0), (0.0, 0.0, 30.0), (0.0, 0.0, 1.0), 0.0, (0.2, 0.4, 0.6), (0.1, 0.1, 0.1))
        self.assertAlmostEqual(F, 1.0, places=4)
        self.assertAlmostEqual(base[2], 0.6, places=4)                           # c126

    def test_beyond_the_range_half_the_base_and_half_the_reflection(self):
        col = lf.water_mix((0.1, 0.1, 0.1), (0.8, 0.6, 0.4), (0.0, 0.0, 0.0), 0.0, 30.0, 0.0, 0.5, 0.05)
        for got, want in zip(col, (0.45, 0.35, 0.25)):
            self.assertAlmostEqual(got, want)
        near = lf.water_mix((0.1, 0.1, 0.1), (0.8, 0.6, 0.4), (0.0, 0.0, 0.0), 0.0, 0.0, 0.0, 0.5, 0.05)
        for got in near:
            self.assertAlmostEqual(got, 0.05)                                    # at the eye: slot 14 (zero) for the reflection
        over = lf.water_mix((0.1, 0.1, 0.1), (0.8, 0.6, 0.4), (0.0, 0.0, 0.0), 0.0, 30.0, 0.0, 7.0, 0.05)
        self.assertAlmostEqual(over[0], 0.8)                                     # c131.x is saturated: the reflection alone

    def test_the_whole_pixel_far_from_the_eye(self):
        out = lf.water_pixel((560.0, 360.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 0.0, pconsts(), TEX)
        self.assertEqual(out["fate"], "shaded")
        self.assertAlmostEqual(out["alpha"], 0.997, places=6)
        for got, want in zip(out["colour"], (0.45 * 2.0, 0.35 * 2.0, 0.25 * 2.0)):   # the mix times c20.w; the sun's scale is 0
            self.assertAlmostEqual(got, want, places=6)
        self.assertEqual(out["glint"], 0.0)
        self.assertEqual(tuple(out["lookup"]), (0.5, 0.5))

    def test_land_is_skipped_with_no_colour(self):
        out = lf.water_pixel((560.0, 360.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 1.0, pconsts(), TEX)
        self.assertEqual((out["fate"], out["alpha"], tuple(out["colour"])), ("skipped", 0.0, (0.0, 0.0, 0.0)))

    def test_a_killed_or_skipped_pixel_reads_no_texture(self):
        def never(*_):
            raise AssertionError("a texture was read")
        none = {key: never for key in TEX}
        self.assertEqual(lf.water_pixel((0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 1.0, pconsts(), none)["fate"], "skipped")
        dry = pconsts({141: [2.0, 2.0, 1e22, 0.0588]})
        self.assertEqual(lf.water_pixel((0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 0.0, dry, none)["fate"], "killed")

    def test_the_shadow_factor_is_on_the_reflection_alone_and_only_under_b137(self):
        c = pconsts({139: [0.5, 0.25, 1e22, 1e22]})
        tex = dict(TEX)
        tex[8] = lambda u, v: 0.5                                               # the factor: 0.5 * 0.5 + 0.25 = 0.5
        plain = lf.water_pixel((560.0, 360.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 0.0, c, tex, bools=())
        shadowed = lf.water_pixel((560.0, 360.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 0.0, c, tex, bools=(137,))
        self.assertAlmostEqual(plain["colour"][0], (0.1 + (0.8 - 0.1) * 0.5) * 2.0)
        self.assertAlmostEqual(shadowed["colour"][0], (0.1 + (0.8 * 0.5 - 0.1) * 0.5) * 2.0)
        self.assertEqual(shadowed["shadow_at"], (0.5, 0.5))
        self.assertIsNone(plain["shadow_at"])

    def test_the_sun_mirrored_into_the_eye_is_its_whole_colour(self):
        # the eye straight above, the surface flat, the light coming straight down (c142 = (0, 0, -1): L = (0, 0, 1))
        term = lf.water_sun((0.0, 0.0, 1.0), (0.0, 0.0, 1.0), 1.0, 0.5, 128.0, (0.0, 0.0, -1.0), (1.0, 0.5, 0.25))
        for got, want in zip(term, (0.5, 0.25, 0.125)):
            self.assertAlmostEqual(got, want)
        away = lf.water_sun((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), 1.0, 0.5, 128.0, (0.0, 0.0, -1.0), (1.0, 0.5, 0.25))
        self.assertEqual(tuple(away), (0.0, 0.0, 0.0))

    def test_the_cosine_of_the_glint_is_plus_v_dot_r(self):                     # as fixed by the machine: instr 66 to 69
        # The light comes down at 45 degrees along +x (c142 points FROM the sun); a level surface mirrors it towards
        # (+x, +z) / sqrt 2. An eye there sees the sun; an eye on the opposite ray does not.
        k = math.sqrt(0.5)
        c142 = (k, 0.0, -k)
        seen = lf.water_sun((0.0, 0.0, 1.0), (k, 0.0, k), 1.0, 1.0, 4.0, c142, (1.0, 1.0, 1.0))
        unseen = lf.water_sun((0.0, 0.0, 1.0), (-k, 0.0, -k), 1.0, 1.0, 4.0, c142, (1.0, 1.0, 1.0))
        self.assertAlmostEqual(seen[0], 1.0)
        self.assertEqual(unseen[0], 0.0)

    def test_the_second_normal_is_flattened_by_c136x(self):
        tilted = lf.normalize([0.6, 0.0, 0.8])
        k = math.sqrt(0.5)
        whole = lf.water_sun(tilted, (k, 0.0, k), 1.0, 1.0, 4.0, (k, 0.0, -k), (1.0, 1.0, 1.0))
        other = lf.water_sun(tilted, (k, 0.0, k), 0.279, 1.0, 4.0, (k, 0.0, -k), (1.0, 1.0, 1.0))
        self.assertNotAlmostEqual(whole[0], other[0], places=3)

    def test_the_fog_block_is_the_plain_form(self):
        tf4 = lf.table_1d([[0.2 + 0.01 * i, 0.5, 0.9 - 0.01 * i] for i in range(64)])
        tf5 = lf.table_1d([[1.0 - i / 300.0, 1.0 - i / 400.0, 1.0 - i / 500.0] for i in range(256)])
        P, eye, C = (40.0, -25.0, 3.0), (1.0, 2.0, 30.0), [0.3, 0.2, 0.1]
        c28, c66, c67, c68 = (0.7, 1.2), lf.normalize([0.3, -0.5, 0.2]), (12.0, 1e22, 0.01, 0.004), (0.3, 0.4, 0.5)
        _, _, V, d = lf.water_base(P, eye, (0.0, 0.0, 1.0), 0.0, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
        got, at = lf.water_fog(C, V, d, c28, c66, c67, c68, tf4, tf5)
        want = lf.fog(C, P, eye, c28, c66, c67, c68, tf4, tf5)
        a, s, _ = lf.fog_coords(P, eye, c66, c67)
        self.assertNotEqual(want, C)
        for g, w in zip(list(got) + list(at), list(want) + [a, s]):
            self.assertAlmostEqual(g, w, places=12)
        near, at = lf.water_fog(C, V, d, c28, c66, (d + 1.0, 0.0, 0.01, 0.004), c68, tf4, tf5)
        self.assertEqual((near, at), (C, None))

    def test_no_unread_component_reaches_the_pixel(self):
        a = lf.water_pixel((100.0, 500.0), (40.0, 60.0, 0.0), (0.3, 0.1), (0.7, 0.9), 0.1, pconsts(), TEX, bools=(136, 137))
        other = pconsts()
        for reg, comps in water_check.UNREAD.items():                           # {register: the components no formula reads}
            for k in comps:
                other[reg][k] = math.nan
        b = lf.water_pixel((100.0, 500.0), (40.0, 60.0, 0.0), (0.3, 0.1), (0.7, 0.9), 0.1, other, TEX, bools=(136, 137))
        self.assertEqual(a["fate"], "shaded")
        self.assertEqual(tuple(a["colour"]), tuple(b["colour"]))
        self.assertTrue(all(math.isfinite(x) for x in a["colour"]))
        self.assertEqual(sum(len(k) for k in water_check.UNREAD.values()), 4 * 24 - 50)

    def test_single_precision_is_near_the_double_reference_and_made_of_singles(self):
        args = ((100.0, 500.0), (40.0, 60.0, 0.0), (0.3, 0.1), (0.7, 0.9), 0.1, pconsts({137: [0.5, 1e22, 1e22, 1e22]}), TEX)
        double = lf.water_pixel(*args)
        single = lf.water_pixel(*args, single=True)
        for d, s in zip(double["colour"] + [double["alpha"]], single["colour"] + [single["alpha"]]):
            self.assertEqual(s, water_check.f32(s))
            self.assertLess(abs(d - s), 1e-4 * max(1.0, abs(d)))
        self.assertNotEqual(tuple(double["colour"]), tuple(single["colour"]))

    def test_the_bools_of_a_word(self):
        self.assertEqual(lf.water_bools(0x00000110), (136,))                    # the captured last writer's word
        self.assertEqual(lf.water_bools(0x00000300), (136, 137))
        self.assertEqual(lf.water_bools(0x00000010), ())

    def test_the_opaque_twin_shades_where_alpha_is_one(self):
        dry = pconsts({141: [1.0, 2.0, 1e22, 0.0588]})                           # m = 0: the ramp at -1, alpha 1
        out = lf.water_pixel((560.0, 360.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 0.0, dry, TEX, kind="opaque")
        self.assertEqual((out["fate"], out["alpha"]), ("shaded", 1.0))
        self.assertEqual(lf.water_pixel((560.0, 360.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 0.0, pconsts(), TEX,
                                        kind="opaque")["fate"], "killed")


@unittest.skipUnless(HAVE, NO_DUMPS)
class PixelMicrocode(unittest.TestCase):
    TRIALS = 200

    def test_the_pixel_formula_is_the_microcode(self):
        r = water_check.check_pixel(trials=self.TRIALS)
        self.assertEqual((r.failed, r.trials), (0, self.TRIALS), r.first_failure)
        tenth, fifth = self.TRIALS // 10, self.TRIALS // 5
        self.assertGreaterEqual(min(r.killed, r.skipped, r.shaded), tenth)       # (Review Focus 1: every branch is reached)
        self.assertGreaterEqual(min(r.sun, r.shaded - r.sun), tenth)             # b136 in both states among the shaded
        self.assertGreaterEqual(r.shadow, 1)                                     # b137 set somewhere
        self.assertGreaterEqual(min(r.near, r.far), fifth // 2)                  # both sides of 1 / c140.x
        self.assertGreaterEqual(min(r.fogged, r.shaded - r.fogged), fifth // 2)  # both sides of the fog's start
        self.assertEqual(water_check.pixel_floors(r), [])
        self.assertEqual(set(r.kinds), set(water_check.PIXEL_KINDS.values()))
        # (ledger ruling 34; the review of Tasks 1 to 5, I2) the zero-product points are four
        self.assertEqual(sorted(water_check.PIXEL_KINDS.values()), ["at_eye", "flat_zero", "power_zero", "second_flat"])

    def test_each_pixel_mutation_fails(self):
        self.assertEqual(len(water_check.PIXEL_MUTATIONS), 16)
        for m in water_check.PIXEL_MUTATIONS:
            self.assertGreater(water_check.check_pixel(trials=self.TRIALS, mutation=m).failed, 0, m)

    def test_each_literal_is_held_at_its_place(self):
        for name in water_check.HELD_LITERALS:
            self.assertGreater(water_check.check_pixel(trials=self.TRIALS, mutation=("literal", name)).failed, 0, name)
            self.assertEqual(water_check.check_pixel(trials=self.TRIALS, shift=(name, 1.01)).failed, 0, name)

    def test_an_ieee_machine_disagrees_at_the_degenerate_points_and_nowhere_else(self):
        r = water_check.check_pixel(trials=self.TRIALS, zero_product=False)
        self.assertEqual(r.failed_kinds.get("", 0), 0)                          # no trial that is not degenerate
        for kind in water_check.PIXEL_KINDS.values():
            self.assertGreater(r.failed_kinds.get(kind, 0), 0, kind)            # (a saturate hides some: not all of them)

    def test_the_mix_factor_is_saturated_in_the_microcode(self):                # as fixed by the machine: instr 14
        case = water_check.shaded_case(1, 0)
        case.consts[131][0] = 7.0
        seven = water_check.evaluate_pixel(case)
        self.assertTrue(seven.ok)
        case.consts[131][0] = 1.0
        self.assertEqual(water_check.evaluate_pixel(case).got, seven.got)

    def test_the_sign_of_the_glint_in_the_microcode(self):                      # as fixed by the machine: instr 66 to 69
        # A level surface (a flat normal sample), the eye in the mirror direction of the light: the machine's own
        # colour carries the sun term there (the plan's reading; with the spec's it would carry none).
        k = math.sqrt(0.5)
        case = water_check.shaded_case(1, 0)
        case.bools = {136}
        case.normal = lambda u, v: FLAT
        eye = case.consts[19][:3]
        case.regs[0][:3] = [eye[0] - 40.0, eye[1], eye[2] - 40.0]              # V = (k, 0, k)
        case.consts[142][:3] = [k, 0.0, -k]
        case.consts[138][0] = 4.0
        lit = water_check.evaluate_pixel(case)
        self.assertTrue(lit.ok)
        self.assertGreater(lit.want["glint"], 0.5)
        case.bools = set()
        dark = water_check.evaluate_pixel(case)
        self.assertTrue(dark.ok)
        self.assertGreater(lit.got[0], 2.0 * dark.got[0])

    def test_the_pixel_shader_reads_what_the_table_says(self):
        used = water_check.used_registers("ps")
        self.assertEqual(used - {254, 255}, set(lf.WATER_SHADER["ps_reg"]))
        self.assertEqual(water_check.used_registers("ps_opaque"), used)

    def test_the_components_the_pixel_takes_are_the_tables(self):
        read = water_check.read_components("ps")
        self.assertEqual(read, lf.WATER_SHADER["ps_read"])
        self.assertEqual(set(read), set(lf.WATER_SHADER["ps_reg"]))
        self.assertEqual(sum(len(k) for k in read.values()), 50)

    def test_the_registers_under_a_bool_are_read_under_it_alone(self):
        for bool_, registers in lf.WATER_SHADER["ps_read_under"].items():
            read = water_check.read_components("ps", cases=16, bools={136, 137} - {bool_})
            self.assertEqual(set(lf.WATER_SHADER["ps_read"]) - set(read), set(registers), bool_)

    def test_the_slots_the_pixel_fetches(self):
        case = water_check.shaded_case(1, 0)
        slots = [f[1] for f in water_check.run_pixel(case)[1].fetches]
        s = lf.WATER_SHADER["ps_slots"]
        self.assertEqual(slots, [s["mask"], s["normal"], s["normal"], s["reflection"], s["near"], s["shadow"],
                                 s["scatter"], s["transmittance"]])


# --- the formulas against captured rows (plan Task 4): on a made capture (tests/water_made.py) ---------------------

import tempfile  # noqa: E402

import numpy as np  # noqa: E402

import water_made  # noqa: E402


def mirrored(matrix, factor, height):
    mirror = np.eye(4)
    mirror[2, 2], mirror[2, 3] = -1.0, 2.0 * height
    return (np.diag([factor[0], factor[1], 1.0, 1.0]) @ np.array(matrix, dtype=np.float64) @ mirror).tolist()


@unittest.skipUnless(HAVE, NO_DUMPS)
class CapturedRows(unittest.TestCase):
    def made(self, folder, **kw):
        return water_made.capture(folder, **kw)

    def test_a_made_row_has_200_positions_and_no_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            r = water_check.check_rows(self.made(folder))
        self.assertEqual((r.rows, r.surface_rows, r.opaque_rows), (1, 1, 0))
        self.assertEqual(r.positions, 200)
        self.assertEqual((r.failed, r.vertex_failed, r.not_finite, r.unread), (0, 0, 0, 0), r.first_failure)
        self.assertEqual(r.vertices, 4)
        self.assertEqual(water_check.rows_problems(r), [])
        # the made mask's two texels make both fates of a surface pixel that is not killed
        self.assertGreater(r.fates["surface"]["shaded"], 20)
        self.assertGreater(r.fates["surface"]["skipped"], 20)
        self.assertEqual(r.fates["surface"]["killed"], 0)
        self.assertLessEqual(r.worst, water_check.PIXEL_TOLERANCE)

    def test_the_opaque_twin_is_killed_at_every_position(self):
        with tempfile.TemporaryDirectory() as folder:
            r = water_check.check_rows(self.made(folder, opaque=True))
        self.assertEqual((r.rows, r.surface_rows, r.opaque_rows, r.positions, r.failed), (2, 1, 1, 400, 0), r.first_failure)
        self.assertEqual(r.fates["opaque"], {"killed": 200, "skipped": 0, "shaded": 0})

    def test_a_read_component_that_is_no_number_is_counted_and_fails_the_run(self):
        with tempfile.TemporaryDirectory() as folder:
            bad = list(water_made.PIXEL[128])
            bad[0] = math.nan
            path = self.made(folder, pixel={128: bad})
            r = water_check.check_rows(path)
            self.assertEqual(r.not_finite, 1)
            self.assertTrue(any("not finite" in text and "c128.x" in text for text in water_check.rows_problems(r)))
            self.assertEqual(water_check.main(["rows", str(path)], say=lambda *a: None), 1)

    def test_an_unread_component_that_is_no_number_passes(self):
        with tempfile.TemporaryDirectory() as folder:
            pixel = {reg: [math.nan if k in water_check.UNREAD[reg] else v for k, v in enumerate(water_made.PIXEL[reg])]
                     for reg in water_made.PIXEL}
            vertex = {reg: [v[0], v[1], math.nan, math.nan] for reg, v in water_made.VERTEX.items()}
            path = self.made(folder, pixel=pixel, vertex=vertex)
            r = water_check.check_rows(path)
            self.assertEqual((r.positions, r.failed, r.vertex_failed, r.not_finite), (200, 0, 0, 0), r.first_failure)

    def test_the_row_mutations_fail_on_the_made_row(self):
        with tempfile.TemporaryDirectory() as folder:
            r = water_check.check_rows(self.made(folder))
        for name in water_check.VERTEX_MUTATIONS + water_check.ALPHA_MUTATIONS:
            self.assertGreater(r.caught[name], 0, name)
        # what the made row cannot show is named with what stands in its way, never passed over
        blind = water_check.rows_blind(r)
        for name in water_check.PIXEL_MUTATIONS:
            self.assertTrue(r.caught[name] > 0 or name in blind, name)
        self.assertIn("distort", blind)                         # the made c129.xy and c130.xy are equal, as the captured
        self.assertGreater(r.caught["base"], 0)
        self.assertGreater(r.caught["exposure"], 0)

    def test_the_views_are_the_mirrored_camera_and_each_mutation_fails(self):
        self.assertEqual(water_check.VIEW_MUTATIONS, ("factor", "plane", "height"))
        matrix = ([0.49, -0.84, 0.0, 92.6], [0.27, 0.15, 1.71, -132.0], [0.85, 0.49, -0.18, -138.9], [0.85, 0.49, -0.18, -138.8])
        with tempfile.TemporaryDirectory() as folder:
            path = self.made(folder, matrix=matrix, views=[mirrored(matrix, (2.0 / 3.0, 2.0 / 3.0), water_made.HEIGHT)])
            views = water_check.check_views(path)
            self.assertEqual(len(views), 1)
            v = views[0]
            self.assertEqual((v.frame, v.view, v.base, v.rows), (1, 1, 0x13401000, 3))
            self.assertAlmostEqual(v.height, 20.0)
            self.assertLessEqual(v.worst, 1e-6)
            self.assertTrue(v.ok)
            self.assertEqual(v.caught, {"factor": True, "plane": True, "height": True})
            self.assertEqual(water_check.main(["views", str(path)], say=lambda *a: None), 0)
            # a view that is not the mirror of the main camera is refused
            wrong = self.made(Path(folder) / "wrong", matrix=matrix, views=[mirrored(matrix, (2.0 / 3.0, 2.0 / 3.0), 20.5)])
            self.assertFalse(water_check.check_views(wrong)[0].ok)
            self.assertEqual(water_check.main(["views", str(wrong)], say=lambda *a: None), 1)

    def test_the_dead_draw_on_rows_and_masks(self):
        with tempfile.TemporaryDirectory() as folder:
            good = water_check.check_dead_rows(self.made(Path(folder) / "a", opaque=True))
            self.assertEqual((good.rows, good.dead, good.masks, good.ones), (1, 1, 1, 0))
            self.assertAlmostEqual(good.largest, 1.0 - 0.003, places=6)
            self.assertEqual(good.problems, [])
            pixel = {141: (2.0, 2.0, water_made.GARBAGE, 0.0)}
            path = self.made(Path(folder) / "b", opaque=True, pixel=pixel)
            bad = water_check.check_dead_rows(path)
            self.assertEqual((bad.rows, bad.dead, bad.ones), (1, 0, 1))        # the mask's one texel of 0 has alpha 1
            self.assertEqual(bad.largest, 1.0)
            self.assertEqual(water_check.main(["dead", str(path)], say=lambda *a: None), 1)

    def test_the_bools_states_and_bounds_of_the_made_rows(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.made(folder, opaque=True)
            words = water_check.bool_words(path)
            self.assertEqual(words, {("surface", 0x100, 0x110): 1, ("opaque", 0x100, 0x110): 1})
            states = water_check.state_words(path)
            self.assertEqual(states[("surface", "2201")], {0x07060706: 1})
            self.assertEqual(states[("opaque", "2200")], {0x00708766: 1})
            spans, rows = water_check.row_bounds([path])
            self.assertEqual(rows, 2)
            self.assertEqual(spans[("ps", 141)][0], (water_check.f32(2.003), water_check.f32(2.003)))
            self.assertIsNone(spans[("ps", 141)][2])                # an unread component has no span
            self.assertEqual(water_check.bounds_outside(spans), [])
            for name in ("bools", "states", "bounds"):
                self.assertEqual(water_check.main([name, str(path)], say=lambda *a: None), 0, name)



# --- the pins (plan Task 4): tools/xdk_sigmatch/water_pins.py --------------------------------------------------------

class Pins(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import water_pins
        cls.pins = water_pins

    def sections(self):
        out, name = {}, None
        for line in self.pins.lines():
            if line.startswith("// "):
                name = line[3:line.index(":")]
                out[name] = []
            else:
                out[name].append(line)
        return out

    def test_the_five_sections_and_their_lines(self):
        s = self.sections()
        self.assertEqual(list(s), ["water vertex pins", "water alpha pins", "water pixel pins", "water view pins", "water dead pins"])
        self.assertGreaterEqual(sum(len(v) for v in s.values()), 24)
        self.assertEqual(len(s["water view pins"]), 3)
        self.assertGreaterEqual(len(s["water pixel pins"]), 16)
        for lines in s.values():
            for line in lines:
                self.assertRegex(line, r"^    \{.*\},(  // .*)?$")
        inc = self.pins.inc_lines()
        self.assertEqual(sum(line.startswith("#ifdef FABLE2_WATER_PINS_") for line in inc), 5)
        self.assertEqual(sum(line == "#endif" for line in inc), 5)
        self.assertEqual([line for line in inc if not line.startswith("#") and not line.startswith("// Generated") and not line.startswith("// The pins")
                          and not line.startswith("// FABLE2")], self.pins.lines())

    def test_a_line_is_the_single_precision_reference(self):
        p = self.pins
        differs = 0
        for item in p.CAPTURED_PIXELS:
            (q, P, coord, m, c, word, a, b, r13, r14, shadow, scatter, transmit), result = p.pixel_pin(item)
            for single in (True, False):
                samples = iter((a, b))
                tex = {"normal": lambda u, v: next(samples), 13: lambda u, v: r13, 14: lambda u, v: r14, 8: lambda u, v: shadow,
                       "tf4": lambda x: scatter, "tf5": lambda x: transmit}
                out = lf.water_pixel(q, P, coord[:2], coord[2:], m, c, tex, bools=lf.water_bools(word), single=single)
                if single:
                    self.assertEqual(tuple(out["colour"]), result[2])
                    self.assertTrue(all(x == water_check.f32(x) for x in out["colour"]))        # made of singles
                else:
                    differs += tuple(out["colour"]) != result[2]
                    for x, y in zip(out["colour"], result[2]):
                        self.assertAlmostEqual(x, y, delta=1e-4 * max(1.0, abs(x)))
        self.assertGreater(differs, 0)                                # the double reference is another number

    def test_the_made_pixels_reach_what_the_captured_do_not(self):
        p = self.pins
        self.assertEqual(len(p.MADE_PIXELS), len(p.MADE_KINDS))
        self.assertEqual(p.MADE_KINDS[-4:], ("flat_zero", "at_eye", "power_zero", "second_flat"))     # ledger ruling 34: four points
        made = {kind: p.pixel_pin(item) for kind, item in zip(p.MADE_KINDS, p.MADE_PIXELS)}
        # (review of Tasks 4 to 15, Minor 7) "normal_order": the one line at which the order of the three squares
        # under the normal's length decides the normal's bits; every other order of the sum gives another normal
        # there, and on no other shaded line does any of them.
        import numpy as np
        def normals(inputs):
            q, P, coord, m, c, word, a, b = inputs[:8]
            f = np.float32
            sx, sy = f(a[0]) * f(2) + (f(b[0]) * f(2) + f(-2)), f(a[1]) * f(2) + (f(b[1]) * f(2) + f(-2))
            za = np.sqrt(np.abs(-((f(a[0]) * f(2) + f(-1)) ** 2 + (f(a[1]) * f(2) + f(-1)) ** 2) + f(1)), dtype=np.float32)
            zb = np.sqrt(np.abs(-((f(b[0]) * f(2) + f(-1)) ** 2 + (f(b[1]) * f(2) + f(-1)) ** 2) + f(1)), dtype=np.float32)
            nx, ny, nz = sx * f(0.5), sy * f(0.5), (f(0.5) * f(c[128][0])) * (za + zb)
            x, y, z = nx * nx, ny * ny, nz * nz
            def unit(total):
                inv = f(1.0 / np.sqrt(np.float64(np.abs(total)))) if total != 0 else f(0)
                return (float(nx * inv), float(ny * inv), float(nz * inv))
            return unit(((f(0) + z) + x) + y), [unit(x + (y + z)), unit((x + y) + z), unit((z + y) + x), unit((y + z) + x)]
        inputs, result = made["normal_order"]
        own, others = normals(inputs)
        self.assertEqual(result[0], 2)
        self.assertEqual(tuple(result[5]), own)                       # the reference's order is z, x, y
        for other in others:
            self.assertNotEqual(other, own)
        for kind, (inputs, result) in made.items():
            if kind == "normal_order" or result[0] != 2 or kind in ("flat_zero",):
                continue
            own, others = normals(inputs)
            self.assertEqual(tuple(result[5]), own, kind)
            self.assertIn(own, others, kind)                          # (some other order gives the same bits: the line cannot tell)
        for kind, (inputs, result) in made.items():
            fate, alpha, colour, at13, at14, normal, fresnel, distance, sun, fog_at = result
            self.assertTrue(all(math.isfinite(v) for v in (alpha, fresnel, distance) + colour + sun), kind)
        reach = 1.0 / made["near"][0][4][140][0]
        self.assertLess(made["near"][1][7], reach)                    # within the range: slot 14 counts
        self.assertGreater(made["shadow"][1][7], reach)
        self.assertTrue(made["shadow"][0][5] & lf.WATER_SHADER["b137"])
        self.assertEqual(made["skipped"][1][0], 1)
        self.assertEqual(made["skipped"][1][2], (0.0, 0.0, 0.0))
        self.assertEqual(made["killed"][1][0], 0)
        self.assertEqual(made["before the fog"][1][9], (0.0, 0.0))
        # the fog partly applied, and c129 unlike c130: the one line on which those three registers can be told
        inputs, result = made["part_fog"]
        c = inputs[4]
        self.assertEqual((c[28][0], c[28][1]), (0.5, 0.75))
        self.assertNotEqual(tuple(c[129][:2]), tuple(c[130][:2]))
        self.assertEqual(result[0], 2)
        self.assertNotEqual(result[9], (0.0, 0.0))                   # past the fog's start
        self.assertNotEqual(result[3], result[4])                    # the two lookups differ
        # just past the fog's start: the near blend sat(c67.z * (d - c67.x)) lies strictly between 0 and 1 on this line alone
        blends = {}
        for kind, (inputs_, result_) in made.items():
            if result_[0] == 2 and result_[9] != (0.0, 0.0):
                blends[kind] = inputs_[4][67][2] * (result_[7] - inputs_[4][67][0])
        self.assertTrue(0.2 < blends["fog_edge"] < 0.8, blends["fog_edge"])
        self.assertTrue(all(v >= 1.0 for kind, v in blends.items() if kind != "fog_edge"), blends)
        for item_ in p.CAPTURED_PIXELS:
            inputs_, result_ = p.pixel_pin(item_)
            self.assertGreaterEqual(inputs_[4][67][2] * (result_[7] - inputs_[4][67][0]), 1.0)
        for other in p.CAPTURED_PIXELS + tuple(item for kind, item in zip(p.MADE_KINDS, p.MADE_PIXELS) if kind != "part_fog"):
            registers = p.pixel_inputs(other)[4]
            self.assertEqual((registers[28][0], registers[28][1]), (1.0, 1.0))
            self.assertEqual(tuple(registers[129][:2]), tuple(registers[130][:2]))
        # ... and the pin moves with each of them (the same pixel with the captured values is another colour)
        item = [it for kind, it in zip(p.MADE_KINDS, p.MADE_PIXELS) if kind == "part_fog"][0]
        for reg, values in ((28, (1.0, 0.75, 0.0, 0.0)), (28, (0.5, 1.0, 0.0, 0.0))):
            over = dict(item[12])
            over[reg] = tuple(water_check_bits(v) for v in values)
            changed = item[:12] + (over, item[13])
            self.assertNotEqual(p.pixel_pin(changed)[1][2], result[2], (reg, values))
        over = dict(item[12])
        over[129] = item[12][130]
        changed = item[:12] + (over, item[13])
        self.assertNotEqual(p.pixel_pin(changed)[1][3], result[3])
        self.assertNotEqual(made["shadow"][1][9], (0.0, 0.0))        # 150 units: past the start
        # the glint whole: the sun term is most of the pixel
        colour, sun, scale = made["glint"][1][2], made["glint"][1][8], made["glint"][0][4][20][3]
        self.assertGreater(sum(sun) * scale, 0.5 * sum(colour))
        # every captured pixel is shaded and far; one of each row glints or not as the row has it
        for item in p.CAPTURED_PIXELS:
            inputs, result = p.pixel_pin(item)
            self.assertEqual(result[0], 2)
            self.assertGreater(result[7], 1.0 / inputs[4][140][0])
        # at the four points the reference is the console's value, and an IEEE machine's would be another: the shadow
        # of the sun's whole colour at a power of 0
        c = made["power_zero"][0][4]
        whole = [water_check.f32(c[137][0] * c[143][k]) for k in range(3)]
        for got, want in zip(made["power_zero"][1][8], whole):
            self.assertAlmostEqual(got, want, places=6)
        self.assertEqual(made["second_flat"][1][8], (0.0, 0.0, 0.0))  # N2 = 0: the cosine is sat(V . c142), 0 with the eye above and the sun too
        self.assertEqual(made["flat_zero"][1][5], (0.0, 0.0, 0.0))    # N = 0 * infinity = 0
        self.assertEqual(made["at_eye"][1][7], 0.0)

    def test_the_two_thresholds_are_two_things(self):
        p = self.pins
        results = [p.alpha_pin(item) for item in p.ALPHA]
        self.assertIn((1, 1), [(fate, drawn) for _, (_, fate, drawn) in results])       # colour 0 with its alpha, and drawn
        self.assertIn((2, 0), [(fate, drawn) for _, (_, fate, drawn) in results])       # shaded, and not drawn
        self.assertIn((2, 1), [(fate, drawn) for _, (_, fate, drawn) in results])
        self.assertIn((0, 0), [(fate, drawn) for _, (_, fate, drawn) in results])
        self.assertEqual({kind for (_, _, _, kind), _ in results}, {0, 1})
        # the captured pair: c141.w is 15 / 255 as a single, the state's reference one unit above it
        self.assertEqual(p.CAPTURED_C141[3], struct.unpack("<I", struct.pack("<f", 15.0 / 255.0))[0])
        self.assertEqual(p.REFERENCE, p.CAPTURED_C141[3] + 1)
        open_water = results[0][1]
        self.assertAlmostEqual(open_water[0], 0.997, places=5)
        self.assertEqual(open_water[1:], (2, 1))

    def test_the_dead_lines_stand_at_the_margin(self):
        p = self.pins
        results = [p.dead_pin(item) for item in p.DEAD]
        self.assertEqual(results[0][1], 1)                                              # the captured c141
        for y in (0.0, 2.0, -1.5):
            x = water_check.dead_margin_x(water_check.f32(y))
            below = struct.unpack("<f", struct.pack("<I", struct.unpack("<I", struct.pack("<f", x))[0] - 1))[0]
            self.assertIn(((x, water_check.f32(y), 0.0, 0.0), 1), results, y)
            self.assertIn(((below, water_check.f32(y), 0.0, 0.0), 0), results, y)
        self.assertGreaterEqual(sum(dead for _, dead in results), 5)
        self.assertGreaterEqual(sum(1 - dead for _, dead in results), 7)
        self.assertTrue(any(c141[0] != c141[0] for c141, dead in results if not dead))     # no number is not dead
        for c141, dead in results:
            self.assertEqual(bool(dead), lf.water_opaque_dead(c141))

    def test_the_view_lines_are_the_derived_rows(self):
        p = self.pins
        for item in p.CAPTURED_VIEWS:
            (M, c31, h), rows = p.view_pin(item)
            want = water_check.water_view_rows(np.array(M).reshape(4, 4), c31, h).reshape(-1)
            self.assertEqual(rows, tuple(water_check.f32(float(x)) for x in want))
            self.assertAlmostEqual(c31[0], 2.0 / 3.0, places=6)

    def test_the_captured_rows_are_the_captures(self):
        p = self.pins
        import capture_replay as cr
        import impostor_check as ic
        seen = 0
        for index, (name, frame, place, word, vertex, pixel) in enumerate(p.CAPTURED_ROWS):
            try:
                path = cr.find_capture(name)
            except cr.Unreadable:
                continue
            rows = [r for r in water_check.water_rows(path) if r.get("frame") == frame and water_check.row_kind(r) == "surface"]
            row = rows[place]
            seen += 1
            self.assertEqual(int(row["pbool_load"], 16), word)
            bank = ic.row_bank(row)
            for i, reg in enumerate(p.VS_REG):
                self.assertEqual(tuple(p.bits(v) if v == v else None for v in bank[reg]), tuple(b if p.from_bits(b) == p.from_bits(b) else None for b in vertex[i]), reg)
            for i, reg in enumerate(p.PS_REG):
                text = row["pfull"][32 * reg:32 * reg + 32]
                self.assertEqual(tuple(int(text[8 * k:8 * k + 8], 16) for k in range(4)), tuple(pixel[i]), reg)
        if not seen:
            self.skipTest("none of the four captures is here")


class Records(unittest.TestCase):
    """water_check.py records (plan Task 9; spec criterion 6): a frame row's water records against the rows of their
    own draws, bit for bit. The made capture's records are made from water_made's registers, not from its rows."""

    def check(self, change=None, **kw):
        with tempfile.TemporaryDirectory() as folder:
            water_check._captures.clear()
            return water_check.check_records(water_made.records_capture(folder, change=change, **kw))

    def one_problem(self, change, text):
        frames, records, problems = self.check(change)
        self.assertTrue(problems, "nothing was found wrong")
        self.assertTrue(any(text in line for line in problems), problems)
        return problems

    def test_a_record_and_its_row_agree(self):
        self.assertEqual(self.check(), (1, 2, []))

    def test_one_register_word_changed_fails(self):
        def pixel(rows, water):                                     # c141.w of the surface's record, one unit in the last place
            words = water["records"][1]["ps"]
            at = 4 * water_made.RECORD_PIXEL_REGISTERS.index(141) + 3
            words[at] = "0x%08X" % (int(words[at], 16) + 1)
        problems = self.one_problem(pixel, "pixel c141.w")
        self.assertEqual(len(problems), 1, problems)

        def unread(rows, water):                                    # a component no formula reads is the record's too
            words = water["records"][0]["ps"]
            at = 4 * water_made.RECORD_PIXEL_REGISTERS.index(129) + 2
            words[at] = "0x7FC00000"
        self.one_problem(unread, "pixel c129.z")

        def vertex(rows, water):
            water["records"][1]["vs"][5] = "0x3F800001"
        self.assertEqual(len(self.one_problem(vertex, "vertex c116.y")), 1)

    def test_a_record_whose_place_names_another_row_fails(self):
        def other(rows, water):                                     # the surface's record under the opaque row's number
            water["records"][1]["place"] = 5
        problems = self.one_problem(other, "place 5")
        self.assertTrue(any("has no record" in line for line in problems), problems)

        def exchanged(rows, water):                                 # both records under the other's number: every member differs
            water["records"][0]["place"], water["records"][1]["place"] = 900, 5
        problems = self.one_problem(exchanged, "ps_hash")
        self.assertTrue(any("is not after the record before it" in line for line in problems), problems)

        def nowhere(rows, water):
            water["records"][1]["place"] = 901
        self.one_problem(nowhere, "place 901 is no in-scene water row's seq")

        def row_moved(rows, water):                                 # the row's own number changed: the record names no row
            [r for r in rows if r.get("seq") == 900][0]["seq"] = 899
        self.one_problem(row_moved, "place 900 is no in-scene water row's seq")

    def test_the_bool_word_is_the_last_writers(self):
        def shadow(rows, water):
            water["records"][1]["pbool"] = "0x00000100"             # the device's shadow, which the rows carry as "pbool"
        self.one_problem(shadow, "pbool 0x00000100, the row's 0x00000110")

    def test_the_fetches_the_bases_the_stride_and_the_hashes(self):
        def fetch(rows, water):
            water["records"][1]["fetch"][2][1] = "0x13501086"       # slot 13 at another body's picture
        self.one_problem(fetch, "fetch")

        def base(rows, water):
            water["records"][0]["fog_base"][1] = "0x1FAB1000"
        self.one_problem(base, "fog_base")

        def stride(rows, water):
            water["records"][0]["stride"] = 6
        self.one_problem(stride, "stride 6")

        def kind(rows, water):
            water["records"][0]["kind"] = "surface"
        self.one_problem(kind, "kind surface, the row's opaque")

    def test_the_counts_are_held_to_the_rows(self):
        def seen(rows, water):
            water["seen"]["opaque"] = 2
        self.one_problem(seen, "seen opaque 2, the frame has 1 in-scene rows of F6D9")

        def dropped(rows, water):
            del water["records"][0]
        problems = self.one_problem(dropped, "has no record")
        self.assertTrue(any("taken 2 and 1 records" in line for line in problems), problems)

        def twice(rows, water):
            water["records"].append(dict(water["records"][1]))
        self.one_problem(twice, "is not after the record before it")

        def member(rows, water):
            del water["records"][1]["fog_base"]
        self.one_problem(member, "no fog_base")

    def test_a_frame_refused_whole_or_without_the_switch_fails(self):
        def refused(rows, water):
            water["whole"] = False
            water["refused"] = {"register": 1}
        self.one_problem(refused, "refused whole: register: 1")

        def off(rows, water):
            [r for r in rows if r.get("kind") == "frame"][0]["water"] = "off"
        frames, records, problems = self.check(off)
        self.assertEqual((frames, records), (0, 0))
        self.assertIn("not made with --fable2_native_water=on", problems[0])

    def test_a_vertex_component_the_row_cannot_print_is_not_compared_and_a_printed_one_is(self):
        nan = float("nan")
        vertex = {115: (1.0365, 1.1775, nan, water_made.GARBAGE)}   # c115.z: no formula reads it; the row prints null

        def keep(rows, water):
            water["records"][1]["vs"][2] = "0x7FC00000"
            water["records"][0]["vs"][2] = "0x7FC00000"
        self.assertEqual(self.check(keep, vertex=vertex), (1, 2, []))

        def garbage(rows, water):                                   # c115.w is printed (1.5e22) and so compared
            water["records"][1]["vs"][3] = "0x00000000"
        self.one_problem(garbage, "vertex c115.w")

    def test_the_command_exits_1_on_a_difference_and_0_without(self):
        lines = []
        with tempfile.TemporaryDirectory() as folder:
            water_check._captures.clear()
            good = water_made.records_capture(folder)
            self.assertEqual(water_check.main(["records", str(good)], lines.append), 0)
        self.assertIn("native_discovery_made.jsonl: 1 frames with water records, 2 records, 0 problems", lines)
        self.assertEqual(lines[-1], "every record equals its own row bit for bit")

        def wrong(rows, water):
            water["records"][1]["ps"][0] = "0x00000001"
        lines = []
        with tempfile.TemporaryDirectory() as folder:
            water_check._captures.clear()
            bad = water_made.records_capture(folder, change=wrong)
            self.assertEqual(water_check.main(["records", str(bad)], lines.append), 1)
        self.assertEqual(lines[-1], "1 captures FAILED")
        with tempfile.TemporaryDirectory() as folder:               # a capture of a build without the switch: no water at all
            water_check._captures.clear()
            self.assertEqual(water_check.main(["records", str(water_made.capture(folder, opaque=True))], [].append), 1)


class ViewRule(unittest.TestCase):
    """Plan Task 15, entry condition 4: the k-th view of a frame is the picture at the k-th smallest slot-13 base."""
    VIEW, BLUR, SCENE = ("row", 280, "D72FD0903CCE7DD6", False), ("row", 320, "F186D27249B462F8", False), ("row", 1120, "F6D98C7B4D98438B", True)

    def events(self, *destinations):
        """A frame of views: a destination that is a pair is resolved to the scratch first and blurred."""
        out = [("row", 320, "6D6B000000000000", False), ("resolve", 0x13401000, 128, 128)]       # the pass before the views
        for d in destinations:
            out += [self.VIEW, self.VIEW]
            if isinstance(d, tuple):
                out += [("resolve", d[0], 256, 256), self.BLUR, ("resolve", d[1], 256, 256)]
            else:
                out += [("resolve", d, 256, 256)]
        return out + [self.SCENE, ("resolve", 0x199E0000, 1120, 720)]

    def test_a_view_ends_at_its_last_resolve(self):
        got = water_check.view_destinations(self.events((0x13601000, 0x13401000), 0x13501000))
        self.assertEqual(got, [(0x13401000, True), (0x13501000, False)])
        self.assertEqual(water_check.view_destinations([self.SCENE, ("resolve", 0x199E0000, 1120, 720)]), [])

    def test_two_views_in_order_hold_and_exchanged_they_do_not(self):
        bases = [0x13501000, 0x13401000]
        ok, note = water_check.view_rule(water_check.view_destinations(self.events(0x13401000, 0x13501000)), bases)
        self.assertTrue(ok, note)
        ok, note = water_check.view_rule(water_check.view_destinations(self.events(0x13501000, 0x13401000)), bases)
        self.assertFalse(ok)
        self.assertIn("view 1", note)

    def test_a_view_too_many_or_a_base_nobody_drew_fails(self):
        d = water_check.view_destinations(self.events(0x13401000, 0x13501000))
        self.assertFalse(water_check.view_rule(d, [0x13401000])[0])
        self.assertFalse(water_check.view_rule(d, [0x13401000, 0x13501000, 0x13581000])[0])
        self.assertFalse(water_check.view_rule(d, [0x13401000, 0x13581000])[0])
        self.assertFalse(water_check.view_rule([], [])[0])              # nothing to hold

    def test_a_view_without_a_resolve_has_no_destination(self):
        got = water_check.view_destinations([self.VIEW, self.SCENE])
        self.assertEqual(got, [(None, False)])
        self.assertFalse(water_check.view_rule(got, [0x13401000])[0])


class ViewTerrain(unittest.TestCase):
    """Plan Task 15, entry condition 5: what the native pass would hold for a view's far-terrain row is enough."""

    def test_the_registers_that_differ_are_named_by_component(self):
        a = {19: [1.0, 2.0, 3.0, 1.0], 20: [0.1, 0.2, 0.3, 9.0], 21: [1.0, 1.0, 1.0, 1.3]}
        b = {19: [1.0, 2.0, -3.0, 1.0], 20: [0.1, 0.2, 0.3, 1.0], 21: [1.0, 1.0, 1.0, 1.3]}
        self.assertEqual(water_check.registers_differing(a, b, (19, 20, 21)), ["c19.z", "c20.w"])
        self.assertEqual(water_check.registers_differing(a, a, (19, 20, 21)), [])
        self.assertEqual(water_check.registers_differing(a, {19: a[19]}, (19, 20)), ["c20 (not in the row)"])

    def test_the_mirrored_eye(self):
        self.assertEqual(water_check.mirrored_eye([82.0, 157.0, 50.0], 20.0), [82.0, 157.0, -10.0])

    @unittest.skipUnless((water_check.DUMPS / "shader_7DDAD5C29B3F56EE.ucode.frag").is_file(), "no shader dumps")
    def test_the_native_inputs_give_the_microcodes_colour_and_wrong_ones_do_not(self):
        import random
        rng = random.Random(7)
        light = {"sun_dir": [-0.66, 0.07, 0.75], "sun_col": [1.0, 0.84, 0.41], "tint": [1.14, 1.4, 1.94], "tint_pow": 1.3,
                 "add": [0.0057, 0.07, 0.091], "floor": [0.0588, 0.0902, 0.0], "axis": [-0.66, 0.07, 0.75]}
        fog = {"c28": [1.0, 1.0], "c66": [-0.83, 0.09, 0.55], "c67": [19.0, 1900.0, 1.0, 0.0005], "c68": [0.27, 0.4, 0.47]}
        tables = (lambda a: [0.3 + 0.2 * a, 0.4, 0.5 - 0.1 * a], lambda s: [math.exp(-3.0 * s), math.exp(-2.0 * s), math.exp(-s)])
        eye = [82.0, 157.0, -10.0]
        consts = water_check.far_terrain_registers(light, fog, eye, c11x=0.47, out_scale=1.0)
        text = water_check._text(water_check.DUMPS, "7DDAD5C29B3F56EE")
        r = water_check.check_far_terrain(text, consts, {255: [1.0, 0.0, 0.0, 0.0]}, light, fog, tables, eye, 0.47, 1.0, 40, rng)
        self.assertEqual((r["agree"], r["fogged"] > 10, r["plain"] > 5), (40, True, True), r)
        for name, change in (("another eye", dict(eye=[82.0, 157.0, 50.0])), ("the main scene's output scale", dict(out_scale=9.0)),
                             ("another light factor", dict(c11x=1.0))):
            kw = dict(eye=eye, c11x=0.47, out_scale=1.0)
            kw.update(change)
            r = water_check.check_far_terrain(text, consts, {255: [1.0, 0.0, 0.0, 0.0]}, light, fog, tables, kw["eye"], kw["c11x"],
                                              kw["out_scale"], 40, random.Random(7))
            self.assertLess(r["agree"], 40, name)


if __name__ == "__main__":
    unittest.main()
