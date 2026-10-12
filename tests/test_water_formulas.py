"""The lake's surface as closed formulas (light_formulas.water_vertex, water_alpha, water_kept, water_opaque_dead,
water_pixel and its parts) and their machine check against the microcode of vertex shader BEAD84BD72072E0E and pixel
shaders A17D8AEC3A817D45 (the surface) and F6D98C7B4D98438B (the dead opaque twin): water_check.py. The water
sub-project, plan Tasks 2 and 3, spec criteria 1 (its first half: made inputs; the captured rows are plan Task 4's)
and 2 part (a)."""
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import water_check  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
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


if __name__ == "__main__":
    unittest.main()
