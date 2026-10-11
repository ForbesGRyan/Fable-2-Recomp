"""The wind vertex program's HLSL text is held to the Python reference by value (review of plan Task 10,
Important 1; ledger ruling 44).

The pins hold the C++ mirrors to the reference (tests/native/test_clay_logic.cpp), and
tests/test_wind_literals.py holds the HLSL to the mirrors by the ORDER OF ITS LITERAL NAMES. Neither sees a wrong
register component, weight component, sign or function in the text that is compiled: seven such changes of the
HLSL, each alone, passed every test of the tree at b746593. Here the text under CLAY_WIND in
src/native/fable2_native_shaders.h is translated line by line to Python (tools/xdk_sigmatch/wind_hlsl.py) and
evaluated in double against light_formulas.wind_plant, wind_trunk and wind_leaf: at the inputs of every pin line
and at made inputs in which every register component, weight and literal table differs, within 1e-9 of a unit
(the text as committed is within 2e-15). The seven changes, and the plan's own for the mirrors made in the HLSL
instead, must each fail that comparison.

Not reached from here: the compiler, `precise` and the GPU's arithmetic (the game's trunks at 100% of the
replay's depth speak for those), and the loads of the words in the vertex main (test_wind_literals.py)."""
import math
import random
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import wind_hlsl  # noqa: E402
import wind_pins  # noqa: E402

TOLERANCE = 1e-9        # units; double against double, the same order of operations
MADE = 400              # made inputs a family
MUTANT_FLOOR = 1e-3     # a changed text must leave the reference by at least this somewhere (the review's seven: 0.09 to 2.7)

# (name, the text as committed, the changed text, the families it must break): each site exactly once in the header.
MUTANTS = (
    ("the wobble's scale reads w.w for w.y", "* w.y * (W_half * c17.z)", "* w.w * (W_half * c17.z)", ("trunk", "leaf")),
    ("the across term's sign in the second component", "along * c17.y + across * c17.x + v", "along * c17.y - across * c17.x + v", ("trunk", "leaf")),
    ("the lift reads c15.z for c15.w", "precise float lift = c15.w *", "precise float lift = c15.z *", ("leaf",)),
    ("a plant's c26.x and c26.y exchanged", "o_wind_reg[1].xyz * o_wind_reg[0].z + o_wind_reg[2].xyz * o_wind_reg[0].w",
     "o_wind_reg[1].xyz * o_wind_reg[0].w + o_wind_reg[2].xyz * o_wind_reg[0].z", ("plant",)),
    ("s2 reads w.x for w.z", "precise float s2 = sin(WindPhase(w.z * c16.x", "precise float s2 = sin(WindPhase(w.x * c16.x", ("trunk", "leaf")),
    ("s7's v.z and v.x exchanged (the hundredfold byte)", "(clock * W_flutter_x + v.z) + W_hundred * v.x",
     "(clock * W_flutter_x + v.x) + W_hundred * v.z", ("leaf",)),
    ("a plant's cosine as a sine", "saturate(P_height * z) * cos(theta)", "saturate(P_height * z) * sin(theta)", ("plant",)),
    # the plan's mutations of the mirrors (Task 10), made in the HLSL:
    ("c16 for c15 as the clock", "precise float t = c15.x;\n  precise float g = c17.w * W_gust;\n  precise float s0",
     "precise float t = c16.x;\n  precise float g = c17.w * W_gust;\n  precise float s0", ("trunk", "leaf")),
    ("the gain of a plant's rate left out", "(f * P_gain + 1.0) + f", "(f * 1.0 + 1.0) + f", ("plant",)),
    ("the flutter's x left out", "+ W_hundred * v.x));", "));", ("leaf",)),
    ("a leaf's distance from its pivot not kept", "moved = pivot + d / sqrt(turned2) * len;", "moved = pivot + d;", ("leaf",)),
)


def _header():
    return (ROOT / "src" / "native" / "fable2_native_shaders.h").read_bytes().decode("utf-8").replace("\r\n", "\n")


def _slots(hash_):
    row = lf.WIND_SHADERS[hash_]
    return [row["lit"][name] for name in lf.WIND_LITERAL_ORDER[row["family"]]]


def _shaders(family):
    return sorted(h for h, row in lf.WIND_SHADERS.items() if row["family"] == family)


def _made(rng):
    """[(family, what to call on a wind_hlsl.Program, the reference's position, the position at rest)]: MADE inputs a family; every
    register component, every weight and every word its own number, the literal tables those of all twelve
    shaders in turn."""
    f32 = wind_pins.f32
    out = []
    for n in range(MADE):
        hash_ = _shaders("plant")[n % len(_shaders("plant"))]
        p = [f32(rng.uniform(-200, 200)) for _ in range(3)]
        seed, z, c7 = f32(rng.uniform(-20, 20)), f32(rng.uniform(-0.2, 0.5)), f32(rng.uniform(0, 400))
        c10x, c25x = f32(rng.uniform(0, 300)), f32(rng.uniform(0, 2))
        c21 = [f32(rng.uniform(-1, 1)) for _ in range(3)]
        c22 = [f32(rng.uniform(-2, 2)) for _ in range(3)]
        c26 = [f32(rng.uniform(-0.2, 0.2)) for _ in range(2)]
        regs = ((c10x, c25x, c26[0], c26[1]), tuple(c21) + (c7,), tuple(c22) + (0.0,))
        want = lf.wind_plant(p, z, seed, 0.0, (c7, 0.0), c10x, c21, c22, c25x, c26, lf.WIND_SHADERS[hash_]["lit"])
        out.append(("plant", lambda prog, a=(regs, _slots(hash_), p, seed, z): prog.plant(*a), want, tuple(p)))
    for family in ("trunk", "leaf"):
        for n in range(MADE):
            hash_ = _shaders(family)[n % len(_shaders(family))]
            lit = lf.WIND_SHADERS[hash_]["lit"]
            p = [f32(rng.uniform(-20, 20)) for _ in range(3)]
            w4 = [f32(rng.randint(0, 255) / 255.0) for _ in range(4)]
            c15 = [f32(rng.uniform(0, 300)), f32(rng.uniform(0, 1)), f32(rng.uniform(0, 1)), f32(rng.uniform(0, 1))]
            c16 = [f32(rng.uniform(0, 0.5)) for _ in range(3)] + [f32(rng.uniform(-1, 1))]
            c17 = [f32(rng.uniform(-3, 3)), f32(rng.uniform(-3, 3)), f32(rng.uniform(0, 2)), f32(rng.uniform(0, 600))]
            if family == "trunk":
                want = lf.wind_trunk(p, w4, c15, c16, c17, lit)
                out.append(("trunk", lambda prog, a=((c15, c16, c17), _slots(hash_), p, w4): prog.trunk(*a), want, tuple(p)))
            else:
                q = [f32(x + rng.uniform(-2, 2)) for x in p]
                v3 = [f32(rng.randint(0, 255) / 255.0) for _ in range(3)]
                want = lf.wind_leaf(p, q, w4, v3, c15, c16, c17, lit)
                out.append(("leaf", lambda prog, a=((c15, c16, c17), _slots(hash_), p, q, w4, v3): prog.leaf(*a), want, tuple(p)))
    return out


def _pins():
    out = []
    for row in wind_pins.PLANT + wind_pins.PLANT_MADE:
        (hash_, p, seed, z, regs), want = wind_pins.plant_pin(row)
        out.append(("plant", lambda prog, a=(regs, _slots(hash_), p, seed, z): prog.plant(*a), want, tuple(p)))
    for row in wind_pins.TRUNK + wind_pins.TRUNK_MADE:
        (hash_, p, w4, regs), want = wind_pins.trunk_pin(row)
        out.append(("trunk", lambda prog, a=(regs, _slots(hash_), p, w4): prog.trunk(*a), want, tuple(p)))
    for row in wind_pins.LEAF + wind_pins.LEAF_MADE:
        (hash_, p, q, w4, v3, regs), want = wind_pins.leaf_pin(row)
        out.append(("leaf", lambda prog, a=(regs, _slots(hash_), p, q, w4, v3): prog.leaf(*a), want, tuple(p)))
    return out


def _worst(program, cases):
    """{family: the largest |program text - reference| over the cases}; a value that is no number counts as
    infinitely far."""
    worst = {"plant": 0.0, "trunk": 0.0, "leaf": 0.0}
    for family, call, want, _rest in cases:
        got = call(program)
        for a, b in zip(got, want):
            worst[family] = max(worst[family], abs(a - b) if math.isfinite(a) else math.inf)
    return worst


class TheText(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = _header()
        cls.program = wind_hlsl.Program(cls.header)
        cls.pins = _pins()
        cls.made = _made(random.Random(11))

    def test_the_translation_reads_the_whole_text(self):
        # every literal name of the three families is a define of the text, and each function is there
        names = {"W_" + n for n in lf.WIND_LITERAL_ORDER["leaf"]} | {"P_" + n for n in lf.WIND_LITERAL_ORDER["plant"]}
        self.assertEqual(set(self.program.defines), names)
        for name in wind_hlsl.FUNCTIONS:
            self.assertIn("def %s(" % name, self.program.source)

    def test_a_line_no_rule_knows_stops_the_translation(self):
        site = "float3 WindTrunk(float3 p, float4 w) {"
        self.assertEqual(self.header.count(site), 1)
        for text in (site + "\n  for (int i = 0; i < 2; ++i) { p += w.xyz; }", site + "\n#if 1"):
            with self.assertRaises(wind_hlsl.Untranslated):
                wind_hlsl.translate(self.header.replace(site, text))

    def test_the_text_is_the_reference_at_the_pins(self):
        self.assertEqual(sum(1 for case in self.pins if case[0] == "plant"), len(wind_pins.PLANT) + len(wind_pins.PLANT_MADE))
        worst = _worst(self.program, self.pins)
        for family, value in worst.items():
            self.assertLessEqual(value, TOLERANCE, "%s at the pins: %r" % (family, worst))

    def test_the_made_inputs_move(self):
        # a comparison on inputs at which nothing moves holds nothing: in each family more than a quarter of the made
        # inputs are moved by the reference by more than a thousandth of a unit
        for family in ("plant", "trunk", "leaf"):
            own = [(want, rest) for f, _, want, rest in self.made if f == family]
            self.assertEqual(len(own), MADE)
            moved = sum(1 for want, rest in own if max(abs(a - b) for a, b in zip(want, rest)) > 1e-3)
            self.assertGreater(moved, MADE // 4, family)

    def test_the_text_is_the_reference_at_made_inputs(self):
        worst = _worst(self.program, self.made)
        for family, value in worst.items():
            self.assertLessEqual(value, TOLERANCE, "%s at made inputs: %r" % (family, worst))

    def test_a_leaf_at_its_pivot_with_no_lift_is_the_moved_pivot(self):
        # ledger ruling 36: no division by a zero length; the made pin line at the pivot is among the pins, and here
        # a vertex exactly at its pivot with every flutter input zero and c15.w = 0 (no lift, no push through c16.w = 0)
        hash_ = _shaders("leaf")[0]
        c15, c16, c17 = (12.5, 0.3, 0.4, 0.0), (0.1, 0.2, 0.3, 0.0), (1.0, -2.0, 1.5, 25.0)
        q = (1.0, 2.0, 3.0)
        w4 = (0.5, 0.25, 0.75, 1.0)
        got = self.program.leaf((c15, c16, c17), _slots(hash_), q, q, w4, (0.0, 0.0, 0.0))
        pivot = self.program.trunk((c15, c16, c17), _slots(hash_)[:18], q, w4)
        self.assertTrue(all(math.isfinite(v) for v in got))
        for a, b in zip(got, pivot):
            self.assertAlmostEqual(a, b, delta=1e-12)

    def test_each_wrong_text_fails(self):
        cases = self.pins + self.made
        for name, old, new, families in MUTANTS:
            with self.subTest(mutant=name):
                self.assertEqual(self.header.count(old), 1, "the site of the mutant is not in the header exactly once")
                worst = _worst(wind_hlsl.Program(self.header.replace(old, new)), cases)
                for family in families:
                    self.assertGreater(worst[family], MUTANT_FLOOR, "%s passes with %s: %r" % (family, name, worst))
                for family in set(worst) - set(families):
                    self.assertLessEqual(worst[family], TOLERANCE, "%s is touched by %s: %r" % (family, name, worst))


if __name__ == "__main__":
    unittest.main()
