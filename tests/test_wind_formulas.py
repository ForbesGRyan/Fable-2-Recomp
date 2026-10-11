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

    def test_the_values_are_the_tracked_literal_tables(self):
        """Without a capture: the literal tables light_check.py and vs_check.py hold (each read from capture rows
        in an earlier sub-project) have every wind literal at the row's place. FBD3 has no such table: its values
        are held by its capture rows alone (wind_check.py rows)."""
        import light_check
        import vs_check
        tables = dict(light_check._PLANT_VS)
        tables.update({h: v["literals"] for h, v in light_check._LEAF_VS.items()})
        tables["475EC9F795E5EDBB"] = light_check._OBJECT_VS["475EC9F795E5EDBB"]["literals"]
        tables.update({h: vs_check.CASTER_LITERALS[h] for h in ("A8A5B2A89F70336A", "432563420047C96C")})
        self.assertEqual(set(lf.WIND_SHADERS) - set(tables), {"FBD39C64463E180B"})
        for hash_, row in lf.WIND_SHADERS.items():
            if hash_ not in tables:
                continue
            for name, (reg, comp) in row["lit_at"].items():
                self.assertEqual(f32(tables[hash_][reg][comp]), row["lit"][name], (hash_, name))

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

    def test_a_draw_without_sway_does_not_count_as_moved(self):
        """The floor on the moved trials means something only if a still vertex is not counted."""
        for hash_ in lf.WIND_SHADERS:
            case = wind_check.made_case(hash_, seed=11, trial=0, dumps=DUMPS)
            self.assertEqual(case.kind, "still")
            t = wind_check.evaluate(case)
            self.assertTrue(t.ok and not t.moved, (hash_, t))
            moving = wind_check.evaluate(wind_check.made_case(hash_, seed=11, trial=3, dumps=DUMPS))
            self.assertTrue(moving.ok, (hash_, moving))
        self.assertGreater(sum(wind_check.evaluate(wind_check.made_case(GRASS, 11, k, DUMPS)).moved for k in range(2, 12)), 4)

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



LEAF, LEAF_CASTER = "A5846836C90E1192", "432563420047C96C"
LEAVES = (LEAF, "D7DD31B9BF194DE8", "2D40B53C926109BE", LEAF_CASTER)
C16_LEAF = C16[:3] + (0.035,)                   # c16.w is 0.035 in every leaf row and -1 in every trunk row (frame map 18)


def made_leaf(i):
    """The i-th made leaf input: (p, q, w4, v3, c15, c16, c17)."""
    p, w4, c15, c16, c17 = made_tree(i)
    reach = 0.05 + 0.03 * (i % 97)
    q = (p[0] + reach * math.cos(0.7 * i), p[1] + reach * math.sin(0.7 * i) * 0.8, p[2] - reach * 0.6 * math.cos(1.3 * i))
    v3 = tuple(((i * k) % 256) / 255.0 for k in (11, 17, 23))
    return p, q, w4, v3, c15, (c16[0], c16[1], c16[2], 0.035 * (i % 6)), c17


class Leaf(unittest.TestCase):
    def lit(self):
        return lf.WIND_SHADERS[LEAF]["lit"]

    def test_a_vertex_at_its_pivot_without_weights_is_the_pivot(self):
        """The direction has no length to turn, and a pivot without weights stands."""
        q = (4.0, -2.0, 7.5)
        zero4, zero3 = (0.0, 0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
        self.assertEqual(lf.wind_leaf(q, q, zero4, zero3, C15, C16_LEAF, C17, self.lit()), q)
        # with weights the pivot moves as a trunk vertex does, and the vertex at it goes with it
        w4 = (0.3, 0.6, 0.9, 0.2)
        self.assertEqual(lf.wind_leaf(q, q, w4, zero3, C15, C16_LEAF, C17, self.lit()), lf.wind_trunk(q, w4, C15, C16_LEAF, C17, self.lit()))
        # no lift at all (c15.w = 0) and no length: still the pivot, not a division by zero
        still = lf.wind_leaf(q, q, w4, (0.1, 0.2, 0.3), C15[:3] + (0.0,), C16_LEAF, C17, self.lit())
        self.assertEqual(still, lf.wind_trunk(q, w4, C15, C16_LEAF, C17, self.lit()))

    def test_the_vertex_keeps_its_distance_from_the_pivot(self):
        """Instr 98: the turned direction is brought back to the length |P - Q|."""
        turned = 0
        for i in range(400):
            p, q, w4, v3, c15, c16, c17 = made_leaf(i)
            got = lf.wind_leaf(p, q, w4, v3, c15, c16, c17, self.lit())
            off = lf.wind_tree_offset(w4, c15, c16, c17, self.lit())
            moved_pivot = tuple(q[k] + off[k] for k in range(3))
            length = math.sqrt(sum((p[k] - q[k]) ** 2 for k in range(3)))
            self.assertAlmostEqual(math.sqrt(sum((got[k] - moved_pivot[k]) ** 2 for k in range(3))), length, delta=1e-5, msg=i)
            # only the third coordinate of the direction is pushed: the horizontal direction is kept
            d, e = (p[0] - q[0], p[1] - q[1]), (got[0] - moved_pivot[0], got[1] - moved_pivot[1])
            self.assertAlmostEqual(d[0] * e[1] - d[1] * e[0], 0.0, delta=1e-9 * max(1.0, length * length))
            self.assertGreaterEqual(d[0] * e[0] + d[1] * e[1], 0.0)
            turned += abs((got[2] - moved_pivot[2]) - (p[2] - q[2])) > 1e-4
        self.assertGreater(turned, 200)

    def test_the_offset_never_leaves_its_reach(self):
        for i in range(400):
            p, q, w4, v3, c15, c16, c17 = made_leaf(i)
            got = lf.wind_leaf(p, q, w4, v3, c15, c16, c17, self.lit())
            dist = math.sqrt(sum((g - x) ** 2 for g, x in zip(got, p)))
            length = math.sqrt(sum((p[k] - q[k]) ** 2 for k in range(3)))
            self.assertLessEqual(dist, lf.wind_reach_leaf(max(w4), length, c15, c16, c17, self.lit()) * (1.0 + 1e-6), i)
        # a mesh of longer leaves has the larger bound, and a mesh without weights only the turn
        self.assertLess(lf.wind_reach_leaf(1.0, 1.0, C15, C16_LEAF, C17, self.lit()), lf.wind_reach_leaf(1.0, 8.0, C15, C16_LEAF, C17, self.lit()))
        self.assertGreater(lf.wind_reach_leaf(0.0, 2.0, C15, C16_LEAF, C17, self.lit()), 0.0)
        self.assertEqual(lf.wind_reach_leaf(0.0, 0.0, C15, C16_LEAF, C17, self.lit()), 0.0)

    def test_the_flutter_written_out(self):
        """The reading of A584's instr 72 to 93 with the shader's numbers written as decimals."""
        def s(x):
            return math.sin(2.0 * math.pi * x)

        k = f32
        for i in (3, 41, 207, 388):
            p, q, (b, a, d, c), v3, c15, c16, c17 = made_leaf(i)
            t, g = c15[0], c17[3] * k(0.025)
            length = math.sqrt(sum((p[n] - q[n]) ** 2 for n in range(3)))
            clock = t + k(0.15) * c17[3]
            s6 = s(k(0.15) * b + (c16[1] + k(0.035)) * t + g + c16[0] * d + k(0.05))
            s7 = s(0.25 * clock + v3[2] + 100.0 * v3[0])
            s8 = s(clock + v3[1])
            kk = k(0.3849) * s6 if s6 >= 0.0 else k(0.28867501) * s6
            push = k(0.3849) * s7 * max(length - k(0.3), 0.0) * c16[3]
            lift = c15[3] * abs(kk) * k(0.3849) * s8 * kk
            want = push * (k(0.55) * c17[2] + 1.0) + lift * (k(0.9) * c17[2] + 1.0)
            got = lf.wind_leaf_lift(p, q, (b, a, d, c), v3, c15, c16, c17, self.lit())
            self.assertAlmostEqual(got, want, delta=2e-5 * max(1.0, abs(want)), msg=i)

    def test_what_a_leaf_reads_of_fetch_5(self):
        """Three values, not two: the first (source x) enters at a hundredfold, so that a byte of 0 or 255, which
        most vertices hold, adds a whole number of turns and nothing else (why the research's perturbation of
        real vertices saw two)."""
        p, q, w4, v3, c15, c16, c17 = made_leaf(77)
        base = lf.wind_leaf_lift(p, q, w4, v3, c15, c16, c17, self.lit())
        for n in range(3):
            other = tuple(v3[m] + (0.003 if m == n else 0.0) for m in range(3))
            self.assertNotEqual(lf.wind_leaf_lift(p, q, w4, other, c15, c16, c17, self.lit()), base, n)
        zero = lf.wind_leaf_lift(p, q, w4, (0.0, v3[1], v3[2]), c15, c16, c17, self.lit())
        full = lf.wind_leaf_lift(p, q, w4, (1.0, v3[1], v3[2]), c15, c16, c17, self.lit())
        self.assertAlmostEqual(zero, full, delta=1e-9)
        self.assertNotAlmostEqual(lf.wind_leaf_lift(p, q, w4, (28.0 / 255.0, v3[1], v3[2]), c15, c16, c17, self.lit()), zero, delta=1e-6)

    def test_the_rows(self):
        for hash_ in LEAVES:
            row = lf.WIND_SHADERS[hash_]
            self.assertEqual((row["family"], row["reg"]), ("leaf", (15, 16, 17)), hash_)
            self.assertEqual(row["role"], "caster" if hash_ == LEAF_CASTER else "scene")
            self.assertEqual([e[0] for e in row["elements"]], ["pivot", "weights", "flutter"])
            self.assertEqual([e[2:] for e in row["elements"]], [(32, 5, "yxw", 3), (6, 7, "xyzw", 4), (6, 8, "xyz", 3)])
            self.assertEqual(len(row["lit_reg"]), 7)                 # seven literal registers, not five
        self.assertEqual(lf.WIND_SHADERS[LEAF]["lit_reg"], tuple(range(249, 256)))
        self.assertEqual(lf.WIND_SHADERS["D7DD31B9BF194DE8"]["lit_reg"], tuple(range(248, 255)))
        self.assertEqual(len(lf.WIND_LITERAL_ORDER["leaf"]), 28)
        self.assertEqual(lf.WIND_LITERAL_ORDER["leaf"][:18], lf.WIND_LITERAL_ORDER["trunk"])
        self.assertIn("182130", lf.WIND_SHADERS["2D40B53C926109BE"]["evidence"])
        self.assertEqual(len(lf.WIND_SHADERS), 12)


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class LeafAgainstMicrocode(unittest.TestCase):
    def test_trace_gives_the_transcriptions_steps(self):
        """A584: instr 74 holds the moved pivot in r5 (z, y, x), instr 93 the direction's third coordinate with
        the flutter in r0.x."""
        case = wind_check.made_case(LEAF, seed=3, trial=2, dumps=DUMPS)
        ins = wind_check.formula_inputs(case)
        c, lit = case.consts, case.row["lit"]
        off = lf.wind_tree_offset(ins["weights"], c[15], c[16], c[17], lit)
        r = wind_check.trace(case, 74)
        for got, want in zip((r[5][2], r[5][1], r[5][0]), (ins["pivot"][k] + off[k] for k in range(3))):
            self.assertAlmostEqual(got, want, delta=1e-9 * max(1.0, abs(want)))
        r = wind_check.trace(case, 93)
        want = ins["p"][2] - ins["pivot"][2] + lf.wind_leaf_lift(ins["p"], ins["pivot"], ins["weights"], ins["flutter"], c[15], c[16], c[17], lit)
        self.assertAlmostEqual(r[0][0], want, delta=1e-9 * max(1.0, abs(want)))

    def test_the_caster_is_the_scene_shader_on_made_inputs(self):
        """Criterion 2's identity without a capture: 4325 on the made draws of A584, D7DD and 2D40. Exact but for
        the caster's own order of two sums: the bound is a rounding of the double arithmetic, not the criterion's."""
        for scene in LEAVES[:3]:
            worst, moved = 0.0, 0
            for k in range(60):
                case = wind_check.made_case(scene, seed=7, trial=k, dumps=DUMPS)
                a, m = wind_check.run_microcode(case)
                b = wind_check.run_microcode(wind_check._as_other(case, LEAF_CASTER, case.consts, DUMPS))[0]
                worst = max(worst, max(abs(x - y) / max(1.0, abs(x)) for x, y in zip(a["oPos"], b["oPos"])))
                rest = wind_check.project(case.consts, wind_check.formula_inputs(case, m)["p"])
                moved += max(abs(x - y) for x, y in zip(a["oPos"], rest)) > 1e-3
            self.assertLess(worst, 1e-9, scene)
            self.assertGreater(moved, 40)

    def test_the_made_leaf_trials(self):
        seen = wind_check.made_ranges(LEAF, trials=400, seed=11, dumps=DUMPS)
        for key, (lo, hi) in wind_check.REGISTER_BOUNDS["leaf"].items():
            self.assertEqual(seen[key], (f32(lo), f32(hi)), key)
        self.assertGreater(seen["still"], 0)
        self.assertGreater(seen["pivot"], 0)



import json  # noqa: E402
import re  # noqa: E402

import gen_transform_table as gtt  # noqa: E402
import wind_pins  # noqa: E402

VS_TABLE = ROOT / "docs" / "native-renderer" / "vs-transforms.json"
WIND_TABLE = ROOT / "src" / "native" / "capture" / "wind_table.inc"


class Table(unittest.TestCase):
    """vs-transforms.json's "wind" members are the rows of WIND_SHADERS, and wind_table.inc is made of them."""

    def test_the_tables_members_are_the_rows(self):
        table = gtt.load(VS_TABLE)
        with_wind = {key[2:].upper() for key, e in table.items() if isinstance(e, dict) and "wind" in e}
        self.assertEqual(with_wind, set(lf.WIND_SHADERS))
        for hash_ in lf.WIND_SHADERS:
            self.assertEqual(table["0x" + hash_]["wind"], lf.wind_table_member(hash_), hash_)
            self.assertIs(table["0x" + hash_].get("deformed"), True)

    def test_the_generators_families_are_the_formulas(self):
        self.assertEqual(set(gtt.WIND_FAMILIES), set(lf.WIND_LITERAL_ORDER))
        for family, (code, regs, names, elements) in gtt.WIND_FAMILIES.items():
            self.assertEqual(names, lf.WIND_LITERAL_ORDER[family])
            self.assertEqual(regs, lf.WIND_REGISTERS[family])
            self.assertLessEqual(len(names), gtt.WIND_LITERALS_MAX)
            rows = [r for r in lf.WIND_SHADERS.values() if r["family"] == family]
            for row in rows:
                self.assertEqual([(e[0], e[2], e[5]) for e in row["elements"]], list(elements))
        self.assertEqual(len(lf.WIND_LITERAL_ORDER["leaf"]), gtt.WIND_LITERALS_MAX)

    def test_the_generated_table_is_current(self):
        want = gtt.generate_wind(gtt.load(VS_TABLE))
        self.assertEqual(WIND_TABLE.read_text(encoding="ascii"), want,
                         "run gen_transform_table.py --json docs/native-renderer/vs-transforms.json --wind-out " + str(WIND_TABLE))
        self.assertEqual(len(re.findall(r"^FABLE2_WIND\(", want, re.M)), 12)
        self.assertEqual(len(re.findall(r"^FABLE2_WIND_LITERAL\(", want, re.M)), 6 * 7 + 2 * 18 + 4 * 28)
        # a literal's line is the row's place and the value's bits
        self.assertIn("FABLE2_WIND_LITERAL(0x8123C16DBF583F92ull, 0, 253, 0, 0xC0490FDBu)", want)         # lo = -pi at c253.x
        self.assertIn("FABLE2_WIND_LITERAL(0xA8A5B2A89F70336Aull, 0, 253, 1, 0xC0490FDBu)", want)         # ... at c253.y
        self.assertIn("FABLE2_WIND(0x432563420047C96Cull, 2, 1, 15, 16, 17, 255, 255, 2, 32, 5, 0x331, 3, 6, 7, 0x4E4, 4, 6, 8, 0x324)", want)


class F(float):
    """A number whose every operation is rounded to single precision (a stand-in for a C++ float mirror)."""

    def __new__(cls, x):
        return float.__new__(cls, f32(float(x)))

    def __add__(self, o):
        return F(float(self) + float(o))

    __radd__ = __add__

    def __sub__(self, o):
        return F(float(self) - float(o))

    def __rsub__(self, o):
        return F(float(o) - float(self))

    def __mul__(self, o):
        return F(float(self) * float(o))

    __rmul__ = __mul__

    def __truediv__(self, o):
        return F(float(self) / float(o))

    def __rtruediv__(self, o):
        return F(float(o) / float(self))

    def __neg__(self):
        return F(-float(self))

    def __abs__(self):
        return F(abs(float(self)))


def singles(values):
    return tuple(F(v) for v in values)


def from_line(line):
    """The float bit patterns of a pin's line, in order (the shader's hash left out)."""
    return [wind_pins.from_bits(int(b, 16)) for b in re.findall(r"0x([0-9A-F]{8})u", line)]


class Pins(unittest.TestCase):
    def sections(self):
        out, name = {}, None
        for line in wind_pins.lines():
            if line.startswith("// "):
                name = line[3:line.index(":")]
                out[name] = []
            else:
                out[name].append(line)
        return out

    def test_the_sections(self):
        s = self.sections()
        self.assertEqual(list(s), ["wind plant pins", "wind trunk pins", "wind leaf pins", "wind reach pins"])
        self.assertEqual([len(s[k]) for k in s], [len(wind_pins.PLANT) + 4, len(wind_pins.TRUNK) + 2, len(wind_pins.LEAF) + 5, 15])
        for name, rows, where in (("plant", wind_pins.PLANT, wind_pins.PLANT_ROWS), ("trunk", wind_pins.TRUNK, wind_pins.TRUNK_ROWS),
                                  ("leaf", wind_pins.LEAF, wind_pins.LEAF_ROWS)):
            self.assertEqual(len(rows), len(where), name)
            self.assertTrue(all(lf.WIND_SHADERS[r[0]]["family"] == name for r in rows))
        # every one of the twelve shaders is pinned by a real row
        pinned = {r[0] for r in wind_pins.PLANT + wind_pins.TRUNK + wind_pins.LEAF}
        self.assertEqual(pinned, set(lf.WIND_SHADERS))

    def test_a_line_is_the_formula_at_its_inputs(self):
        s = self.sections()
        moved = 0
        for line in s["wind trunk pins"]:
            v = from_line(line)
            hash_ = re.search(r"0x([0-9A-F]{16})ull", line).group(1)
            p, w4, c15, c16, c17, pos = v[0:3], v[3:7], v[7:11], v[11:15], v[15:19], v[19:22]
            want = lf.wind_trunk(p, w4, c15, c16, c17, lf.WIND_SHADERS[hash_]["lit"])
            self.assertEqual(pos, [f32(x) for x in want], line)
            moved += max(abs(a - b) for a, b in zip(pos, p)) > 1e-3
        self.assertGreaterEqual(moved, len(wind_pins.TRUNK))
        for line in s["wind leaf pins"]:
            v = from_line(line)
            hash_ = re.search(r"0x([0-9A-F]{16})ull", line).group(1)
            want = lf.wind_leaf(v[0:3], v[3:6], v[6:10], v[10:13], v[13:17], v[17:21], v[21:25], lf.WIND_SHADERS[hash_]["lit"])
            self.assertEqual(v[25:28], [f32(x) for x in want], line)
        for line in s["wind plant pins"]:
            v = from_line(line)
            hash_ = re.search(r"0x([0-9A-F]{16})ull", line).group(1)
            p, seed, z, r0, r1, r2, pos = v[0:3], v[3], v[4], v[5:9], v[9:13], v[13:17], v[17:20]
            want = lf.wind_plant(p, z, seed, 0.0, (r1[3], 0.0), r0[0], r1[:3], r2[:3], r0[1], r0[2:], lf.WIND_SHADERS[hash_]["lit"])
            self.assertEqual(pos, [f32(x) for x in want], line)
            self.assertEqual(r2[3], 0.0)
        # the made plant vertices at and below the root stand
        for line in s["wind plant pins"][len(wind_pins.PLANT):len(wind_pins.PLANT) + 2]:
            v = from_line(line)
            self.assertEqual(v[17:20], v[0:3])
        for line in s["wind reach pins"]:
            v = from_line(line)
            self.assertTrue(math.isfinite(v[-1]) and v[-1] >= 0.0)

    def test_the_packed_plant_is_the_plant(self):
        """One word for A3 + B3 and one constant for c7.x + c7.y move the vertex as the two pairs do (the seed's
        sum is exact; the constant's rounding is 1.5e-5 of a turn of the seed)."""
        for row in wind_pins.PLANT:
            hash_, p, a3, b3, z, c7, c10x, c21, c22, c25x, c26 = row
            fl = wind_pins._floats
            whole = lf.wind_plant(fl(p), wind_pins.from_bits(z), wind_pins.from_bits(a3), wind_pins.from_bits(b3), fl(c7),
                                  wind_pins.from_bits(c10x), fl(c21), fl(c22), wind_pins.from_bits(c25x), fl(c26),
                                  lf.WIND_SHADERS[hash_]["lit"])
            _ins, packed = wind_pins.plant_pin(row)
            for a, b in zip(whole, packed):
                self.assertAlmostEqual(a, b, delta=2e-5)

    def test_a_single_precision_evaluation_is_near(self):
        """What a C++ float mirror can reach: the same expressions with every operation rounded to single
        precision are within 2e-5 of a unit of the pinned positions (measured: plants 7e-6, trees 2e-6)."""
        worst = 0.0
        for row in wind_pins.PLANT + wind_pins.PLANT_MADE:
            (hash_, p, seed, z, regs), ref = wind_pins.plant_pin(row)
            lit = {k: F(v) for k, v in lf.WIND_SHADERS[hash_]["lit"].items()}
            got = lf.wind_plant(singles(p), F(z), F(seed), F(0.0), (F(regs[1][3]), F(0.0)), F(regs[0][0]), singles(regs[1][:3]),
                                singles(regs[2][:3]), F(regs[0][1]), singles(regs[0][2:]), lit)
            worst = max(worst, max(abs(float(a) - b) for a, b in zip(got, ref)))
        for row in wind_pins.TRUNK + wind_pins.TRUNK_MADE:
            (hash_, p, w4, regs), ref = wind_pins.trunk_pin(row)
            lit = {k: F(v) for k, v in lf.WIND_SHADERS[hash_]["lit"].items()}
            got = lf.wind_trunk(singles(p), singles(w4), singles(regs[0]), singles(regs[1]), singles(regs[2]), lit)
            worst = max(worst, max(abs(float(a) - b) for a, b in zip(got, ref)))
        for row in wind_pins.LEAF + wind_pins.LEAF_MADE:
            (hash_, p, q, w4, v3, regs), ref = wind_pins.leaf_pin(row)
            lit = {k: F(v) for k, v in lf.WIND_SHADERS[hash_]["lit"].items()}
            got = lf.wind_leaf(singles(p), singles(q), singles(w4), singles(v3), singles(regs[0]), singles(regs[1]), singles(regs[2]), lit)
            worst = max(worst, max(abs(float(a) - b) for a, b in zip(got, ref)))
        self.assertLess(worst, 2e-5)
        self.assertGreater(worst, 0.0)

    @unittest.skipUnless(DUMPS.is_dir() and BRIDGE.is_file(), NO_CAPTURE)
    def test_the_bridges_tuples_are_the_captures(self):
        import capture_replay as cr
        by_line = None
        seen = 0
        for rows, where in ((wind_pins.PLANT, wind_pins.PLANT_ROWS), (wind_pins.TRUNK, wind_pins.TRUNK_ROWS),
                            (wind_pins.LEAF, wind_pins.LEAF_ROWS)):
            for row, (capture, line, vertex) in zip(rows, where):
                if capture != "182313":
                    continue
                if by_line is None:
                    by_line = {r["line"]: r for r in cr.rows(BRIDGE, kinds=("draw",))}
                case = wind_check.row_case(BRIDGE, by_line[line], DUMPS)
                self.assertEqual(case.hash_, row[0])
                case.index = vertex
                ins = wind_check.formula_inputs(case)
                self.assertEqual(wind_pins._floats(row[1]), tuple(f32(x) for x in ins["p"]), line)
                if case.family == "plant":
                    self.assertEqual(wind_pins.from_bits(row[4]), ins["z"])
                    self.assertEqual(wind_pins._floats(row[7]), tuple(case.consts[21][:3]))
                else:
                    self.assertEqual(wind_pins._floats(row[2 if case.family == "trunk" else 3]), tuple(f32(x) for x in ins["weights"]))
                    self.assertEqual(wind_pins._floats(row[-3]), tuple(case.consts[15]))
                seen += 1
        self.assertGreaterEqual(seen, 9)


SUNWARD = LOGS / "native_discovery_20261010_182130.jsonl"


class LeafAtItsPivot(unittest.TestCase):
    """Review of Tasks 5 to 7, I1, and ledger ruling 36: where a leaf's moved offset D' has no length the
    position is the moved pivot Q'. The made pin holds it; the captures show such vertices are drawn."""

    def rows(self):
        """The made leaf rows at their pivot whose lift is exactly 0 (c15.w = 0 and c16.w = 0)."""
        return [r for r in wind_pins.LEAF_MADE if r[1] == r[2] and r[5][3] == 0 and r[6][3] == 0]

    def test_there_is_a_made_pin_at_the_pivot_with_no_lift(self):
        rows = self.rows()
        self.assertEqual(len(rows), 1)
        (hash_, p, q, w4, v3, regs), pos = wind_pins.leaf_pin(rows[0])
        lit = lf.WIND_SHADERS[hash_]["lit"]
        self.assertEqual(p, q)
        # the lift is exactly zero, so D' = (0, 0, 0 + 0): an unguarded L * D' / |D'| is 0 * (0 / 0)
        lift = lf.wind_leaf_lift(p, q, w4, v3, *regs, lit)
        self.assertEqual(lift, 0.0)
        self.assertEqual([p[k] - q[k] for k in range(3)], [0.0, 0.0, 0.0])
        # the pin expects the MOVED pivot, which is neither the vertex at rest nor the pivot at rest
        off = lf.wind_tree_offset(w4, *regs, lit)
        self.assertEqual(pos, tuple(q[k] + off[k] for k in range(3)))
        self.assertGreater(max(abs(o) for o in off), 0.05)
        self.assertTrue(all(math.isfinite(x) for x in pos))
        # every other register and element is the first real row's, so the row differs from it in nothing
        # but the place of the vertex and the two strengths
        real = wind_pins.LEAF[0]
        self.assertEqual((rows[0][0], rows[0][2], rows[0][3], rows[0][4], rows[0][7]), (real[0], real[2], real[3], real[4], real[7]))
        self.assertEqual((rows[0][5][:3], rows[0][6][:3]), (real[5][:3], real[6][:3]))

    def test_the_pins_line_holds_it(self):
        (hash_, _p, q, w4, _v3, regs), _pos = wind_pins.leaf_pin(self.rows()[0])
        off = lf.wind_tree_offset(w4, *regs, lf.WIND_SHADERS[hash_]["lit"])
        want = [f32(q[k] + off[k]) for k in range(3)]
        found = []
        for line in wind_pins.lines():
            v = from_line(line)
            if "// made" in line and len(v) == 28 and v[0:3] == v[3:6] and v[16] == 0.0 and v[20] == 0.0:
                found.append(v[25:28])
        self.assertEqual(found, [want])

    def test_the_other_made_pins_at_the_pivot_have_a_lift(self):
        """Why they did not hold the case (the review's point): with c15.w = 0.32 the lift gives D' a length."""
        others = [r for r in wind_pins.LEAF_MADE if r[1] == r[2] and r not in self.rows()]
        self.assertEqual(len(others), 2)
        for row in others:
            (hash_, p, q, w4, v3, regs), _pos = wind_pins.leaf_pin(row)
            self.assertNotEqual(lf.wind_leaf_lift(p, q, w4, v3, *regs, lf.WIND_SHADERS[hash_]["lit"]), 0.0)

    def test_in_single_precision_too(self):
        """Ruling 35's arithmetic (every operation rounded to single) reaches the same pin: the lift is exactly
        0 there too, and the moved pivot is within the pins' tolerance of 2e-5."""
        (hash_, p, q, w4, v3, regs), ref = wind_pins.leaf_pin(self.rows()[0])
        lit = {k: F(v) for k, v in lf.WIND_SHADERS[hash_]["lit"].items()}
        args = (singles(p), singles(q), singles(w4), singles(v3), singles(regs[0]), singles(regs[1]), singles(regs[2]), lit)
        self.assertEqual(float(lf.wind_leaf_lift(*args)), 0.0)
        got = lf.wind_leaf(*args)
        self.assertLess(max(abs(float(a) - b) for a, b in zip(got, ref)), 2e-5)


def leaf_streams(capture):
    """{stream file: (positions [n, 3], pivots [n, 3], the vertices a leaf draw indexes, the rows naming it)}
    of a capture's leaf draw rows, every frame, whole streams and whole index files."""
    import numpy as np
    import capture_replay as cr
    import foliage_replay as fr
    out = {}
    for r in cr.rows(capture, kinds=("draw",)):
        h = cr.vs_hash(r)
        if h not in lf.WIND_SHADERS or lf.WIND_SHADERS[h]["family"] != "leaf" or not r.get("streams") or not r.get("bank"):
            continue
        mesh = [s for s in r["streams"] if s["slot"] == 95 and s.get("file")]
        if not mesh:
            continue
        name = mesh[0]["file"]
        if name not in out:
            stride = r["fetches"][0]["stride"]
            halves = np.frombuffer(cr.stream_bytes(Path(capture).parent / name), dtype=">f2")
            halves = halves[:len(halves) // (2 * stride) * 2 * stride].reshape(-1, 2 * stride).astype(np.float64)
            out[name] = [halves[:, 0:3], halves[:, 10:13], set(), 0]          # the position: dword 0; the pivot: dword 5
        idx, state, _note = fr.draw_indices(r, Path(capture).parent)
        if state != "whole":
            raise AssertionError("line %d: the draw's indices are not whole" % r["line"])
        out[name][2] |= {int(i) + int(r.get("base_vertex", 0)) for i in idx if int(i) >= 0}
        out[name][3] += 1
    return out


@unittest.skipUnless(BRIDGE.is_file() and SUNWARD.is_file(), f"no captures {SUNWARD.name}, {BRIDGE.name} in {LOGS}")
class LeafMeshesOfTheCaptures(unittest.TestCase):
    """What the two captures with whole streams and whole index files say about |P - Q| of the leaf meshes: the
    lengths wind_pins.REACH_LENGTHS are made at (review M9), and the vertices exactly at their pivot (I1)."""

    @classmethod
    def setUpClass(cls):
        cls.streams = {c.stem[-6:]: leaf_streams(c) for c in (SUNWARD, BRIDGE)}

    def lengths(self, streams):
        import numpy as np
        return np.sort(np.concatenate([np.sqrt(((p - q) ** 2).sum(axis=1)) for p, q, _ids, _n in streams.values()]))

    def test_the_reach_lengths_are_the_whole_streams(self):
        zero, median, largest = wind_pins.REACH_LENGTHS
        self.assertEqual(zero, 0.0)
        seen = []
        for name, streams in self.streams.items():
            d = self.lengths(streams)
            self.assertGreater(len(d), 50000, name)
            self.assertAlmostEqual(float(d[len(d) // 2]), median, delta=0.01, msg=name)
            self.assertGreaterEqual(largest, float(d[-1]), name)          # no captured leaf vertex is farther
            seen.append(float(d[-1]))
            # every vertex of every stream is indexed by a leaf draw: the whole stream is what is drawn
            for file, (p, _q, ids, _n) in streams.items():
                self.assertEqual(len(ids), len(p), file)
        self.assertLess(largest, max(seen) * 1.001)                       # ... and it is the largest, not a guess above it
        self.assertGreater(min(seen), 19.0)                               # frame-map section 12's sampled 12.4 is not

    def test_vertices_at_their_pivot_are_drawn(self):
        import numpy as np
        for name, streams in self.streams.items():
            at = []
            for file, (p, q, ids, rows) in streams.items():
                for v in np.nonzero((p == q).all(axis=1))[0]:
                    at.append((len(p), int(v), int(v) in ids, rows))
            # three in each capture, in two meshes (14,334 and 600 vertices), every one indexed by every
            # draw row that names its stream
            self.assertEqual(sorted((n, v) for n, v, _i, _r in at), [(600, 230), (14334, 91), (14334, 7863)], name)
            self.assertTrue(all(indexed and rows >= 16 for _n, _v, indexed, rows in at), (name, at))


if __name__ == "__main__":
    unittest.main()
