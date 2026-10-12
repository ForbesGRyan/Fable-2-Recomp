"""The lake's surface as closed formulas (light_formulas.water_vertex, water_alpha, water_kept, water_opaque_dead)
and their machine check against the microcode of vertex shader BEAD84BD72072E0E and pixel shaders A17D8AEC3A817D45
(the surface) and F6D98C7B4D98438B (the dead opaque twin): water_check.py. The water sub-project, plan Task 2,
spec criteria 1 (its first half: made inputs; the captured rows are plan Task 4's) and 2 part (a)."""
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


if __name__ == "__main__":
    unittest.main()
