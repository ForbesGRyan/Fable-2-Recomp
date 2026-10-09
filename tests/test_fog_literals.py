"""The fog's numbers are written in several places: the Python reference (tools/xdk_sigmatch/light_formulas.py:
fog, vertex_fog, fog_apply, table_1d), the generator of the numbers the C++ mirrors are pinned to
(tools/xdk_sigmatch/fog_pins.py), the pinned lines themselves (tests/native/test_clay_logic.cpp, cases 580 on),
the renderer's pure mirror (src/native/render/clay_logic.h: FogTableRead, FogCoords, FogMirror, TreeFogMirror
and their constants), the capture's block (src/native/capture/fog_block.h) with its test's fixture frame
(tests/native/test_fog_block.cpp, fog_table_fixture.inc) and the trees' scale (src/native/capture/material.h).
This test reads the C++ texts and holds them to one set of numbers, as tests/test_mask_literals.py does for
the mask."""
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import fog_pins  # noqa: E402
import fog_tables  # noqa: E402
import light_formulas as lf  # noqa: E402

NUMBER = r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?"


def f32(x):
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def _text(relative):
    return (ROOT / relative).read_text(encoding="ascii")


def _one(pattern, text, what):
    found = re.findall(pattern, text, re.S)
    if len(found) != 1:
        raise AssertionError(f"{what}: {len(found)} matches of {pattern!r}")
    return found[0]


def fixture_register(text, name):
    """The floats of the row `{...},  // name` (or `{...}};  // name`) of the fixture frame's registers in
    test_fog_block.cpp."""
    body = _one(r"\{([^{}]*)\}\}?[,;]\s*// " + name + r"\n", text, name)
    return [float(v) for v in re.findall(r"(" + NUMBER + r")f", body)]


class FogLiterals(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.block = _text("src/native/capture/fog_block.h")
        cls.material = _text("src/native/capture/material.h")
        cls.logic_test = _text("tests/native/test_clay_logic.cpp")
        cls.block_test = _text("tests/native/test_fog_block.cpp")
        cls.lines = fog_pins.lines()

    def assertSameFloats(self, got, want, what):
        self.assertEqual(len(got), len(want), what)
        for i, (g, w) in enumerate(zip(got, want)):
            self.assertEqual(f32(g), f32(w), f"{what}[{i}]: {g!r} against {w!r}")

    def test_every_generated_line_is_in_the_cpp_test(self):
        # The pins of cases 580 and 581 are the generator's output, line for line: 12 table pins and 10
        # pixels for each of the two register sets, with the comment lines that name the registers.
        have = {line.strip() for line in self.logic_test.splitlines()}
        self.assertEqual(len(self.lines), 13 + 12 + 12)
        self.assertEqual(sum(1 for line in self.lines if line.startswith("//")), 5)
        for line in self.lines:
            self.assertIn(line.strip(), have, "not in tests/native/test_clay_logic.cpp: run tools/xdk_sigmatch/fog_pins.py")

    def test_a_changed_digit_is_seen(self):
        have = {line.strip() for line in self.logic_test.splitlines()}
        pin = next(line for line in self.lines if "0.06343188398347593" in line)
        self.assertIn(pin.strip(), have)
        self.assertNotIn(pin.replace("0.06343188398347593", "0.06343188398347594").strip(), have)

    def test_the_generator_takes_the_fixture_frame(self):
        # fog_pins.REAL is the frame the block's test is made of (frame 1 of capture 092459), as singles.
        self.assertSameFloats(list(fog_pins.REAL["c28"]), fixture_register(self.block_test, "c28")[:2], "c28")
        self.assertSameFloats(list(fog_pins.REAL["c66"]), fixture_register(self.block_test, "c66")[:3], "c66")
        self.assertSameFloats(list(fog_pins.REAL["c67"]), fixture_register(self.block_test, "c67"), "c67")
        self.assertSameFloats(list(fog_pins.REAL["c68"]), fixture_register(self.block_test, "c68")[:3], "c68")
        for regs in (fog_pins.REAL, fog_pins.MADE):
            for name in ("c28", "c66", "c67", "c68", "eye"):
                self.assertSameFloats(list(regs[name]), [f32(v) for v in regs[name]], name + " is not single precision")
        # The same registers and the same eye are written out in the C++ test of the mirrors.
        for literal in ("-0.8295828104019165f", "0.04456644505262375f", "0.5566023588180542f", "19.881736755371094f",
                        "0.0005000000237487257f", "0.2666446566581726f", "0.39993393421173096f", "0.46666669845581055f",
                        "82.07209777832031f", "157.33926391601562f", "50.17070388793945f"):
            self.assertGreaterEqual(self.logic_test.count(literal), 2, literal)  # (the code and the generator's comment)

    def test_the_tables_are_the_fixture_decoded(self):
        tf4, tf5 = fog_pins.load_tables()
        self.assertEqual((len(tf4), len(tf5)), (64, 256))
        self.assertEqual((fog_pins.SCATTER_TEXELS, fog_pins.TRANSMIT_TEXELS), (fog_tables.TF4_TEXELS, fog_tables.TF5_TEXELS))
        # The block's test pins the same texels of the same bytes (test_fog_block.cpp, cases 8 to 15).
        self.assertEqual(tf4[0], (0.30810546875, 0.384033203125, 0.46533203125))
        self.assertEqual(tf4[63], (0.93310546875, 0.466552734375, 0.306396484375))
        self.assertEqual(tf5[0], (1.0, 1.0, 1.0))
        self.assertSameFloats(list(tf5[128]), [92.0 / 255.0, 79.0 / 255.0, 34.0 / 255.0], "tf5[128]")
        self.assertSameFloats(list(tf5[255]), [38.0 / 255.0, 29.0 / 255.0, 6.0 / 255.0], "tf5[255]")
        # What the block decodes is what the generator read the tables with: a byte over 255 and a half
        # float, each as a single.
        _one(r"b\.transmit\[i\]\[c\] = float\(p\[1 \+ c\]\) / 255\.0f;", self.block, "the block's tf5 decode")

    def test_table_sizes(self):
        sizes = _one(r"inline constexpr uint32_t kFogScatterTexels = (\d+), kFogTransmitTexels = (\d+);", self.logic, "the tables' sizes")
        self.assertEqual(tuple(int(v) for v in sizes), (fog_pins.SCATTER_TEXELS, fog_pins.TRANSMIT_TEXELS))
        _one(r"float scatter\[64\]\[3\];", self.block, "FogBlock::scatter")
        _one(r"float transmit\[256\]\[3\];", self.block, "FogBlock::transmit")
        # The mirrors read each table with its own size.
        self.assertEqual(len(re.findall(r"FogTableRead\(scatter, kFogScatterTexels, at\.a, S\);", self.logic)), 2)
        self.assertEqual(len(re.findall(r"FogTableRead\(transmit, kFogTransmitTexels, at\.s, T\);", self.logic)), 2)

    def test_the_addressing_is_the_references(self):
        # table_1d: centres at (i + 0.5) / N, clamped. The mirror's line and the reference's.
        _one(r"x = c \* double\(n\) - 0\.5;", self.logic, "FogTableRead's texel coordinate")
        table = lf.table_1d([[0.0], [1.0], [2.0], [3.0]])
        for u, want in ((0.125, 0.0), (0.375, 1.0), (0.25, 0.5), (0.6875, 2.25), (0.0, 0.0), (1.0, 3.0), (-2.0, 0.0), (9.0, 3.0)):
            self.assertAlmostEqual(table(u)[0], want, places=12)
        # (the C++ test's hand cases are these: case 580)
        for case in ("{0.125, 0.0, 10.0}", "{0.25, 0.5, 15.0}", "{0.6875, 2.25, 50.0}"):
            self.assertIn(case, self.logic_test)

    def test_bounds(self):
        limit = float(_one(r"inline constexpr float kFogRegisterLimit = (" + NUMBER + r")f;", self.block, "kFogRegisterLimit"))
        self.assertEqual(limit, 1e6)
        d2 = float(_one(r"inline constexpr float kFogDistance2Max = (" + NUMBER + r")f;", self.logic, "kFogDistance2Max"))
        self.assertEqual(f32(d2), f32(fog_pins.DISTANCE2_MAX))
        # No pin lies near the distance bound (the generator refuses one), and with every register inside
        # the limit a table coordinate stays a number in single precision: limit * sqrt(bound) < 3.4e38.
        self.assertLess(limit * d2 ** 0.5, 3.0e38)
        # The trees' scale has the range of its pixel twin c28.y.
        tree_max = float(_one(r"inline constexpr float kTreeFogScaleMax = (" + NUMBER + r")f;", self.material, "kTreeFogScaleMax"))
        amount_max = float(_one(r"c28\[c\] >= 0\.0f && c28\[c\] <= (" + NUMBER + r")f", self.block, "the amounts' ceiling"))
        self.assertEqual(tree_max, amount_max)
        self.assertEqual(len(re.findall(r"b\.amount\[[01]\] <= " + re.escape("%.1f" % amount_max) + "f", self.logic)), 2)
        self.assertEqual(float(_one(r"inline constexpr float kTreeFogNone = (" + NUMBER + r")f;", self.material, "kTreeFogNone")), -1.0)
        self.assertEqual(int(_one(r"inline constexpr uint32_t kLightVsFogScaleReg = (\d+);", self.material, "the trees' register")), 8)
        # The generator pins the scale the captures hold (1) and one that is not 1.
        self.assertEqual(fog_pins.C8X[0], 1.0)
        self.assertNotEqual(fog_pins.C8X[1], 1.0)

    def test_the_made_up_set_exercises_what_the_frame_does_not(self):
        real, made = fog_pins.REAL, fog_pins.MADE
        self.assertEqual(real["c28"], (1.0, 1.0))
        self.assertEqual(real["c67"][2], 1.0)
        self.assertNotEqual(made["c28"][0], 1.0)
        self.assertNotEqual(made["c28"][1], 1.0)
        self.assertNotEqual(made["c28"][0], made["c28"][1])
        self.assertLess(made["c67"][2], 0.1)
        # Pixels inside the near blend with a transmittance below 1, in the made-up set.
        tf4_texels, tf5_texels = fog_pins.load_tables()
        tf4, tf5 = lf.table_1d(tf4_texels), lf.table_1d(tf5_texels)
        inside = 0
        for direction, distance, colour in fog_pins.POINTS:
            P, C, coords, ground, trees = fog_pins.one(made, direction, distance, colour, tf4, tf5)
            if coords and 0.0 < coords[2] < 1.0 and min(tf5(coords[1])) < 1.0:
                inside += 1
                self.assertNotEqual(ground, trees[0])
        self.assertGreaterEqual(inside, 2)


if __name__ == "__main__":
    unittest.main()
