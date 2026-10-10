"""The game's vertex wind as closed formulas (light_formulas.wind_plant, ...) and their machine check against
the shaders' microcode (wind_check.py): the foliage sub-project, plan Tasks 5 to 7, spec criteria 1 and 2."""
import math
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import wind_check  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"
LOGS = ROOT / "out" / "build" / "win-amd64-release" / "logs"
BRIDGE = LOGS / "native_discovery_20261010_182313.jsonl"
NO_CAPTURE = f"no capture {BRIDGE.name} in {LOGS}"
GRASS = "8123C16DBF583F92"
C21, C22, C26 = (0.9024, 0.4099, 0.1329), (-0.35, 1.53, 0.0), (0.025, 0.0637)   # bridge, R A2


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


class Plant(unittest.TestCase):
    def lit(self):
        return lf.WIND_SHADERS[GRASS]["lit"]

    def test_a_vertex_at_the_root_stands(self):
        p = (10.0, 20.0, 3.0)
        self.assertEqual(lf.wind_plant(p, 0.0, 0.25, 0.5, (0.1, 0.2), 58.0, C21, C22, 1.0, C26, self.lit()), p)
        self.assertEqual(lf.wind_plant(p, -2.0, 0.25, 0.5, (0.1, 0.2), 58.0, C21, C22, 1.0, C26, self.lit()), p)

    def test_the_closed_form_written_out(self):
        p, z, a3, b3, c7, t = (10.0, 20.0, 3.0), 0.1, 0.25, 0.5, (0.1, 0.2), 58.0
        f = (c7[0] + c7[1] + a3 + b3) % 1.0
        theta = 1.0 + f - t * 1.0 * (1.0 + 0.1 * f)
        theta = (theta + math.pi) % (2.0 * math.pi) - math.pi
        s = min(max(5.0 * z, 0.0), 1.0) * math.cos(theta)
        want = tuple(p[k] + s * (C26[0] * C21[k] + C26[1] * C22[k]) for k in range(3))
        got = lf.wind_plant(p, z, a3, b3, c7, t, C21, C22, 1.0, C26, self.lit())
        for g, w in zip(got, want):
            self.assertAlmostEqual(g, w, delta=2e-6)

    def test_the_offset_never_leaves_its_reach(self):
        reach = lf.wind_reach_plant(C21, C22, C26)
        self.assertAlmostEqual(reach, 0.108, delta=0.001)                      # R A2
        for i in range(400):
            got = lf.wind_plant((0.0, 0.0, 0.0), 0.01 * i, 0.37 * i, 0.11 * i, (0.3, 0.9), 57.0 + 0.07 * i,
                                C21, C22, 1.0, C26, self.lit())
            self.assertLessEqual(math.sqrt(sum(v * v for v in got)), reach * (1.0 + 1e-6))

    def test_no_sway_is_rest(self):
        """Ruling 26: c25.x = 0 with c26 = 0 (the bridge's plant caster rows) is a plant at rest."""
        p = (1.5, -2.0, 0.25)
        self.assertEqual(lf.wind_plant(p, 0.3, 0.1, 0.2, (0.3, 0.9), 63.9, C21, C22, 0.0, (0.0, 0.0), self.lit()), p)
        self.assertEqual(lf.wind_reach_plant(C21, C22, (0.0, 0.0)), 0.0)

    def test_the_tables_rows(self):
        for hash_, row in lf.WIND_SHADERS.items():
            self.assertEqual(len(hash_), 16)
            self.assertIn(row["family"], ("trunk", "leaf", "plant"))
            self.assertIn(row["role"], ("scene", "caster"))
        self.assertNotIn("36B543CECAE5C781", lf.WIND_SHADERS)                   # push only (ruling 10)
        self.assertNotIn("6AD4108C3FF07966", lf.WIND_SHADERS)                   # moves nothing

    def test_the_six_plant_shaders(self):
        plants = {h for h, row in lf.WIND_SHADERS.items() if row["family"] == "plant"}
        self.assertEqual(plants, {GRASS, "B636821F95DC9D8E", "48D30ECCF684F488", "FC4F2EF6930768BE", "33C00226C154C1F2",
                                  "A8A5B2A89F70336A"})
        self.assertEqual({h for h in plants if lf.WIND_SHADERS[h]["role"] == "caster"}, {"A8A5B2A89F70336A"})
        for h in plants:
            self.assertEqual(lf.WIND_SHADERS[h]["reg"], (10, 21, 22, 25, 26))
            self.assertEqual(lf.WIND_SHADERS[h]["elements"], ())


class Literals(unittest.TestCase):
    """Ruling 9: a shader's literals are data. Each row names every literal component its family's formula
    reads, where the shader holds it and its single-precision value."""

    def test_every_row_places_every_literal_of_its_family(self):
        for hash_, row in lf.WIND_SHADERS.items():
            with self.subTest(hash_):
                names = lf.WIND_LITERAL_ORDER[row["family"]]
                self.assertEqual(tuple(row["lit"]), names)
                self.assertEqual(set(row["lit_at"]), set(names))
                self.assertEqual(row["lit_reg"], tuple(sorted({reg for reg, _ in row["lit_at"].values()})))
                for name in names:
                    reg, comp = row["lit_at"][name]
                    self.assertTrue(248 <= reg <= 255 and 0 <= comp <= 3, name)
                    self.assertEqual(row["lit"][name], f32(row["lit"][name]), name)      # a single-precision value
                # two names at one place hold one value
                by_place = {}
                for name in names:
                    by_place.setdefault(row["lit_at"][name], set()).add(row["lit"][name])
                self.assertTrue(all(len(v) == 1 for v in by_place.values()))

    def test_the_values_are_the_familys(self):
        for hash_, row in lf.WIND_SHADERS.items():
            self.assertEqual(row["lit"], lf.WIND_LITERALS[row["family"]], hash_)
        plant = lf.WIND_LITERALS["plant"]
        self.assertEqual(plant["lo"], f32(-math.pi))
        self.assertEqual(plant["two_pi"], f32(2 * math.pi))
        self.assertEqual(plant["inv_two_pi"], f32(1 / (2 * math.pi)))
        self.assertEqual((plant["gain"], plant["height"], plant["half"], plant["unit"]), (f32(0.1), 5.0, 0.5, 1.0))

    def test_the_structure_is_zero_and_one(self):
        """What a formula takes as structure (a select's condition, an added zero, the 1 that is also the
        position's w) is in the row all the same, so that the capture holds the shader object's table against it."""
        for hash_, row in lf.WIND_SHADERS.items():
            self.assertTrue(any(name in lf.WIND_STRUCTURE for name in row["lit"]), hash_)
            for name, value in lf.WIND_STRUCTURE.items():
                if name in row["lit"]:
                    self.assertEqual(row["lit"][name], value, (hash_, name))


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class AgainstMicrocode(unittest.TestCase):
    def test_every_entered_shader_on_made_inputs(self):
        for hash_ in lf.WIND_SHADERS:
            with self.subTest(hash_):
                r = wind_check.check_formula(hash_, trials=200, seed=11, dumps=DUMPS)
                self.assertEqual(r.failed, 0, r.first_failure)
                self.assertGreaterEqual(r.moved, r.trials // 2)    # a check on vertices that stand proves nothing

    def test_mutations_fail(self):
        families = {row["family"]: hash_ for hash_, row in lf.WIND_SHADERS.items() if row["role"] == "scene"}
        for hash_ in families.values():
            for name in wind_check.MUTATIONS:
                with self.subTest(hash_=hash_, mutation=name):
                    r = wind_check.check_formula(hash_, trials=200, seed=11, mutation=name, dumps=DUMPS)
                    self.assertGreater(r.failed, 0, f"{name} was not caught")

    def test_an_unknown_mutation_is_refused(self):
        with self.assertRaises(ValueError):
            wind_check.check_formula(GRASS, trials=1, mutation="nothing", dumps=DUMPS)
        with self.assertRaises(KeyError):
            wind_check.check_formula("36B543CECAE5C781", trials=1, dumps=DUMPS)

    def test_every_literal_is_where_the_row_says(self):
        """A literal scaled by 1% in the formula alone is caught; scaled in the formula AND at the row's place in
        the microcode's constants the two agree again: the name, the place and the use are one thing."""
        for hash_, row in lf.WIND_SHADERS.items():
            for name in row["lit"]:
                if name in lf.WIND_STRUCTURE:
                    continue
                with self.subTest(hash_=hash_, literal=name):
                    alone = wind_check.check_formula(hash_, trials=48, seed=5, mutation=("literal", name), dumps=DUMPS)
                    self.assertGreater(alone.failed, 0, f"{name} changed by 1% was not caught")
                    both = wind_check.check_formula(hash_, trials=48, seed=5, shift=(name, 1.01), dumps=DUMPS)
                    self.assertEqual(both.failed, 0, both.first_failure)

    def test_the_made_inputs_reach_the_bounds(self):
        """Criterion 1's inputs: the registers inside Design 5's bounds, their edges and a draw without sway among
        them."""
        seen = wind_check.made_ranges(GRASS, trials=400, seed=11)
        for (reg, comp), (lo, hi) in wind_check.REGISTER_BOUNDS["plant"].items():
            self.assertEqual((seen[(reg, comp)][0], seen[(reg, comp)][1]), (f32(lo), f32(hi)), (reg, comp))
        self.assertGreater(seen["still"], 0)
        self.assertGreater(seen["cut"], 0)
        self.assertEqual(wind_check.REGISTER_BOUNDS["plant"][(25, 0)], (0.0, 64.0))      # ruling 26: zero is admitted

    def test_a_vertex_beyond_the_cut(self):
        """The microcode's position is no number there. Under a row without a factor on x, y, z (the w of a
        shadow map's projection: 0 0 0 1) that component is the row's fourth, and the check must say the same
        (2,305 caster vertices of the bridge capture are such)."""
        caster = "A8A5B2A89F70336A"
        case = wind_check.made_case(caster, seed=11, trial=1, dumps=DUMPS)
        self.assertEqual(case.kind, "cut")
        t = wind_check.evaluate(case)
        self.assertTrue(t.ok and not t.moved and all(x != x for x in t.got), t)
        case.consts[3] = [0.0, 0.0, 0.0, 1.0]
        t = wind_check.evaluate(case)
        self.assertTrue(t.ok, t)
        self.assertEqual(t.got[3], 1.0)
        self.assertTrue(all(x != x for x in t.got[:3]))
        case.consts[13][2] = 1e12                       # not cut after all: the check may not call it cut
        t = wind_check.evaluate(case)
        self.assertTrue(t.ok and t.error is not None, t)

    def test_the_near_plants_are_checked_without_their_push(self):
        """Ruling 10: the push loop of the four near shaders runs 0 times in the check, and the check says so."""
        for hash_ in ("48D30ECCF684F488", "FC4F2EF6930768BE", "33C00226C154C1F2"):
            self.assertIn("push loop", wind_check.describe(hash_, dumps=DUMPS))
        self.assertNotIn("push loop", wind_check.describe(GRASS, dumps=DUMPS))

    def test_trace_gives_the_register_after_an_instruction(self):
        """8123, instr 42 writes f = frac(c7.x + c7.y + A3 + B3) into r4.w."""
        case = wind_check.made_case(GRASS, seed=3, trial=0, dumps=DUMPS)
        regs = wind_check.trace(case, 42)
        ins = wind_check.formula_inputs(case)
        f = (case.consts[7][0] + case.consts[7][1] + ins["b3"] + ins["a3"]) % 1.0
        self.assertAlmostEqual(regs[4][3], f, delta=1e-12)


@unittest.skipUnless(DUMPS.is_dir() and BRIDGE.is_file(), NO_CAPTURE)
class OnACapture(unittest.TestCase):
    def test_rows_of_the_bridge(self):
        r = wind_check.check_rows(BRIDGE, per_draw=4, frames=(1,), per_shader=3, dumps=DUMPS)
        self.assertEqual(r.failed, 0, r.first_failure)
        self.assertGreater(r.trials, 0)
        self.assertGreater(r.moved, 0)
        self.assertTrue(set(r.by_shader) >= {GRASS, "B636821F95DC9D8E", "A8A5B2A89F70336A"})

    def test_a_mutation_fails_on_real_rows(self):
        r = wind_check.check_rows(BRIDGE, hash_=GRASS, per_draw=8, frames=(1,), per_shader=6, mutation="sign", dumps=DUMPS)
        self.assertGreater(r.failed, 0)

    def test_the_casters_identity(self):
        lines, bad = wind_check.check_identities(BRIDGE, frames=(1,), per_pair=2, per_draw=6, dumps=DUMPS)
        self.assertEqual(bad, 0, "\n".join(lines))
        self.assertTrue(any("A8A5" in line and "8123" in line for line in lines), lines)


if __name__ == "__main__":
    unittest.main()
