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



TRUNK, TRUNK_CASTER = "475EC9F795E5EDBB", "FBD39C64463E180B"
# A trunk's registers at the bridge (capture native_discovery_20261010_182130, a 475E row; R A2).
C15, C16, C17 = (59.7342339, 0.63, 0.72, 0.32), (0.012, 0.06, 0.4, -1.0), (-0.660901546, -0.63156563, 2.0, 111.802376)


def made_tree(i):
    """The i-th made tree input: (p, w4, c15, c16, c17), the registers inside the spec's Design 5 bounds."""
    w4 = tuple(((i * k) % 256) / 255.0 for k in (7, 13, 29, 31))
    p = (3.0 + 0.1 * i, -7.0 + 0.03 * i, 1.0 + 0.01 * i)
    c15 = (50.0 + 0.37 * i, 0.63, 0.5 + (i % 9) * 0.4, (i % 5) * 0.3)
    c16 = (0.012 + 0.001 * (i % 7), 0.06, 0.4, -1.0 + 0.3 * (i % 8))
    c17 = (math.cos(0.1 * i) * (0.3 + 0.02 * (i % 100)), math.sin(0.1 * i) * (0.3 + 0.02 * (i % 100)), (i % 17) * 1.0,
           100.0 + 0.74 * i)
    return p, w4, c15, c16, c17


class Trunk(unittest.TestCase):
    def lit(self):
        return lf.WIND_SHADERS[TRUNK]["lit"]

    def test_a_vertex_without_weights_stands(self):
        """R A1: the four values of fetch 4 scale the whole offset."""
        p = (10.0, 20.0, 3.0)
        self.assertEqual(lf.wind_trunk(p, (0.0, 0.0, 0.0, 0.0), C15, C16, C17, self.lit()), p)
        self.assertNotEqual(lf.wind_trunk(p, (0.5, 0.5, 0.5, 0.5), C15, C16, C17, self.lit()), p)
        # the two halves of the offset each have their own byte: d (source z) the tree's sway, a (source y) the
        # wobble, whose phase is b and c times a clock (with neither it is 0 and its sine is 0)
        self.assertNotEqual(lf.wind_trunk(p, (0.0, 0.0, 0.5, 0.0), C15, C16, C17, self.lit()), p)
        self.assertNotEqual(lf.wind_trunk(p, (0.25, 0.5, 0.0, 0.0), C15, C16, C17, self.lit()), p)
        self.assertNotEqual(lf.wind_trunk(p, (0.0, 0.5, 0.0, 0.5), C15, C16, C17, self.lit()), p)
        self.assertEqual(lf.wind_trunk(p, (0.0, 0.5, 0.0, 0.0), C15, C16, C17, self.lit()), p)
        self.assertEqual(lf.wind_trunk(p, (0.5, 0.0, 0.0, 0.5), C15, C16, C17, self.lit()), p)

    def test_the_offset_never_leaves_its_reach(self):
        moved = 0
        for i in range(400):
            p, w4, c15, c16, c17 = made_tree(i)
            got = lf.wind_trunk(p, w4, c15, c16, c17, self.lit())
            dist = math.sqrt(sum((g - x) ** 2 for g, x in zip(got, p)))
            reach = lf.wind_reach_tree(max(w4), c15, c16, c17, self.lit())
            self.assertLessEqual(dist, reach * (1.0 + 1e-6), i)
            moved += dist > 1e-3
        self.assertGreater(moved, 300)
        self.assertEqual(lf.wind_reach_tree(0.0, C15, C16, C17, self.lit()), 0.0)
        # the bound is not a slack one: within four times the largest offset found over a sweep of the clock
        far = 0.0
        for i in range(4000):
            c15 = (0.013 * i,) + C15[1:]
            off = lf.wind_tree_offset((1.0, 1.0, 1.0, 1.0), c15, C16, (C17[0], C17[1], C17[2], 2 * c15[0]), self.lit())
            far = max(far, math.sqrt(sum(x * x for x in off)))
        self.assertLess(lf.wind_reach_tree(1.0, C15, C16, C17, self.lit()), 4.0 * far)

    def test_the_closed_form_written_out(self):
        """The reading of 475E's instr 17 to 58 with the shader's numbers written as decimals: the sway along
        and across the wind, the branch's wobble, and the drop of z that instr 56 and 57 (and 40, 49) give."""
        def s(q):
            return math.sin(2.0 * math.pi * q)

        k = f32                 # the shader holds its numbers in single precision
        for i in (1, 17, 123, 311):
            p, (b, a, d, c), c15, c16, c17 = made_tree(i)
            t, g = c15[0], c17[3] * k(0.025)
            s0 = s(k(0.1) * t)
            s1 = s(k(0.1) * b + d * c16[0] + (c16[1] + k(0.035)) * t + g)
            s2 = s(d * c16[0] + (c16[1] + k(0.472)) * t + g)
            along = d * d * c17[2] * k(0.3) * (k(0.3849) * s1 * (0.25 * abs(k(0.3849) * s0) + k(0.7)) + k(0.04) * c17[2])
            across = d * d * c17[2] * k(0.019245) * s2
            scale = (k(0.3849) * s(k(0.11) * t + b) + 0.5) * a * 0.5 * c17[2] * c15[2]
            u = k(0.3849) * s((c16[2] * t + g + k(0.23)) * c + b) * scale
            v = k(0.3849) * s((c16[2] * t + g + k(0.11)) * c + b) * scale
            want = (p[0] + along * c17[0] - across * c17[1] + u, p[1] + along * c17[1] + across * c17[0] + v,
                    p[2] - 0.25 * along * along - 0.5 * (u * u + v * v))
            got = lf.wind_trunk(p, (b, a, d, c), c15, c16, c17, self.lit())
            for x, w in zip(got, want):
                self.assertAlmostEqual(x, w, delta=2e-5 * max(1.0, abs(w)))
            # the relation itself, on the function's own terms: exact
            al, ac, uu, vv = lf.wind_tree_terms((b, a, d, c), c15, c16, c17, self.lit())
            off = lf.wind_tree_offset((b, a, d, c), c15, c16, c17, self.lit())
            flat = (off[0] - uu, off[1] - vv)                       # the sway's part of the horizontal offset
            self.assertAlmostEqual(flat[0] ** 2 + flat[1] ** 2, (al * al + ac * ac) * (c17[0] ** 2 + c17[1] ** 2), delta=1e-9)
            self.assertAlmostEqual(off[2], -0.25 * al * al - 0.5 * (uu * uu + vv * vv), delta=1e-12)
            self.assertLessEqual(off[2], 0.0)

    def test_what_the_trunk_does_not_read(self):
        p, w4 = (1.0, 2.0, 3.0), (0.2, 0.4, 0.6, 0.8)
        base = lf.wind_trunk(p, w4, C15, C16, C17, self.lit())
        self.assertEqual(lf.wind_trunk(p, w4, C15, C16[:3] + (0.035,), C17, self.lit()), base)       # c16.w: the leaves'
        self.assertEqual(lf.wind_trunk(p, w4, (C15[0], 9.0, C15[2], 9.0), C16, C17, self.lit()), base)   # c15.y, c15.w
        self.assertNotEqual(lf.wind_trunk(p, w4, (C15[0], C15[1], 0.5, C15[3]), C16, C17, self.lit()), base)

    def test_the_rows(self):
        scene, caster = lf.WIND_SHADERS[TRUNK], lf.WIND_SHADERS[TRUNK_CASTER]
        self.assertEqual((scene["family"], scene["role"], caster["family"], caster["role"]), ("trunk", "scene", "trunk", "caster"))
        self.assertEqual(scene["reg"], (15, 16, 17))
        self.assertEqual(scene["lit_reg"], (251, 252, 253, 254, 255))
        self.assertEqual(scene["elements"], (("weights", 4, 6, 7, "xyzw", 4),))
        self.assertEqual(caster["elements"], (("weights", 1, 6, 7, "xyzw", 4),))
        self.assertEqual(scene["lit"], caster["lit"])
        self.assertNotEqual(scene["lit_at"], caster["lit_at"])          # the caster keeps 0.7 in another register


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class TrunkAgainstMicrocode(unittest.TestCase):
    def test_the_elements_are_the_dumps_fetches(self):
        for hash_, row in lf.WIND_SHADERS.items():
            fetches = wind_check.dump_fetches(wind_check._text(hash_, DUMPS))
            for name, fetch, fmt, offset, source, count in row["elements"]:
                self.assertEqual(fetches[fetch], (95, fmt, offset), (hash_, name))
                self.assertEqual(len(source), count)

    def test_trace_gives_the_transcriptions_steps(self):
        """475E: instr 31 holds the sway's two amplitudes in r8.xy, instr 58 the offset in r5 (z, y, x)."""
        case = wind_check.made_case(TRUNK, seed=3, trial=2, dumps=DUMPS)
        ins = wind_check.formula_inputs(case)
        c = case.consts
        along, across, u, v = lf.wind_tree_terms(ins["weights"], c[15], c[16], c[17], case.row["lit"])
        r = wind_check.trace(case, 31)
        self.assertAlmostEqual(r[8][0], along, delta=1e-9 * max(1.0, abs(along)))
        self.assertAlmostEqual(r[8][1], across, delta=1e-9 * max(1.0, abs(across)))
        off = lf.wind_tree_offset(ins["weights"], c[15], c[16], c[17], case.row["lit"])
        r = wind_check.trace(case, 58)
        for got, want in zip((r[5][2], r[5][1], r[5][0]), off):
            self.assertAlmostEqual(got, want, delta=1e-9 * max(1.0, abs(want)))

    def test_the_caster_is_the_scene_shader_on_made_inputs(self):
        """Criterion 2's identity without a capture: FBD3 on 475E's made draws, exact."""
        text = wind_check._text(TRUNK_CASTER, DUMPS)
        moved = 0
        for k in range(60):
            case = wind_check.made_case(TRUNK, seed=7, trial=k, dumps=DUMPS)
            a, m = wind_check.run_microcode(case)
            other = wind_check._as_other(case, TRUNK_CASTER, case.consts, DUMPS)
            self.assertEqual(other.text, text)
            b = wind_check.run_microcode(other)[0]
            self.assertEqual(max(abs(x - y) for x, y in zip(a["oPos"], b["oPos"])), 0.0, k)
            rest = wind_check.project(case.consts, wind_check.formula_inputs(case, m)["p"])
            moved += max(abs(x - y) for x, y in zip(a["oPos"], rest)) > 1e-3
        self.assertGreater(moved, 40)


if __name__ == "__main__":
    unittest.main()
