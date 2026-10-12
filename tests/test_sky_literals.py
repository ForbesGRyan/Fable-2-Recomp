"""The sky's numbers are written in several places: the Python reference (tools/xdk_sigmatch/light_formulas.py:
sky, sky_latlong, sky_paths, sky_extinction and SKY_LITERALS), the generator of the numbers the C++ mirror is
pinned to (tools/xdk_sigmatch/sky_pins.py), the pinned lines themselves (tests/native/test_clay_logic.cpp,
cases 588 on), the renderer's pure mirror (src/native/render/clay_logic.h: SkyLatLong, SkyPaths, SkyExtinction,
SkyMirror and their literals) and the sky step's pixel shader (src/native/fable2_native_shaders.h, kClaySkyPs).
This test reads the C++ texts and holds them to one set of numbers, as tests/test_fog_literals.py does for the
fog."""
import math
import random
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import fog_pins  # noqa: E402
import light_formulas as lf  # noqa: E402
import sky_pins  # noqa: E402

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


class SkyLiterals(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.logic_test = _text("tests/native/test_clay_logic.cpp")
        shaders = _text("src/native/fable2_native_shaders.h")
        # (to the sky program's own end: the impostor step's and the water step's programs follow it before the #undef block)
        cls.sky_ps = shaders[shaders.index("inline constexpr const char* kClaySkyPs"):shaders.index("// The impostor step (render/clay_pass.cpp;")]
        cls.sky_vs = _one(r"inline constexpr const char\* kSkyVs = R\"hlsl\((.*?)\)hlsl\";", shaders, "kSkyVs")
        cls.lines = sky_pins.lines()

    def test_every_generated_line_is_in_the_cpp_test(self):
        # The pins of case 588 are the generator's output, line for line: the two samples, and for each of
        # the two register sets its registers, a heading and 22 directions.
        have = {line.strip() for line in self.logic_test.splitlines()}
        self.assertEqual(len(self.lines), 1 + 2 * (2 + len(sky_pins.DIRECTIONS)))
        self.assertEqual(len(sky_pins.DIRECTIONS), 22)
        self.assertEqual(sum(1 for line in self.lines if line.startswith("//")), 5)
        for line in self.lines:
            self.assertIn(line.strip(), have, "not in tests/native/test_clay_logic.cpp: run tools/xdk_sigmatch/sky_pins.py")

    def test_a_changed_digit_is_seen(self):
        have = {line.strip() for line in self.logic_test.splitlines()}
        pin = next(line for line in self.lines if "1.0000147399573862" in line)
        self.assertIn(pin.strip(), have)
        self.assertNotIn(pin.replace("1.0000147399573862", "1.0000147399573863").strip(), have)

    def test_the_generator_takes_the_fixture_frame(self):
        # The real set is the fog block's fixture frame (fog_pins.REAL, tests/native/test_fog_block.cpp) with
        # the sky block of every captured row; every input is a single.
        real, made = sky_pins.REAL, sky_pins.MADE
        self.assertEqual(real["c66"], fog_pins.REAL["c66"])
        self.assertEqual(real["c67y"], fog_pins.REAL["c67"][1])
        self.assertEqual((real["c31x"], real["c46x"], real["c47x"]), (150.0, 100.0, 0.0))
        for regs in (real, made):
            for name in ("c64", "c65", "c66"):
                for v in regs[name]:
                    self.assertEqual(v, f32(v), name)
            for name in ("c31x", "c46x", "c47x", "c67y"):
                self.assertEqual(regs[name], f32(regs[name]), name)
        # The same registers are written out in the C++ test: the block's extinctions (case 580's fixture)
        # and the made-up set's values.
        for v in real["c64"] + real["c65"]:
            self.assertIn("%.9g" % v, self.logic_test)
        for v in made["c64"] + made["c65"]:
            self.assertGreaterEqual(self.logic_test.count("%rf" % v), 2, v)  # (the code and the generator's comment)
        for literal in ("made_sky.c31x = 120.5f;", "made_sky.c46x = 80.25f;", "made_sky.c47x = 0.25f;", "made_fog.range[1] = 1500.0f;"):
            self.assertEqual(self.logic_test.count(literal), 1, literal)
        self.assertEqual((made["c31x"], made["c46x"], made["c47x"], made["c67y"]), (120.5, 80.25, 0.25, 1500.0))
        samples = _one(r"const float s13\[4\] = \{([^}]*)\}, s14\[4\] = \{([^}]*)\};", self.logic_test, "the samples")
        for got, want in zip(samples, (sky_pins.S13, sky_pins.S14)):
            self.assertEqual([float(v.rstrip("f")) for v in got.split(", ")], list(want))

    def test_the_made_up_set_exercises_what_the_frame_does_not(self):
        self.assertEqual(sky_pins.REAL["c47x"], 0.0)
        self.assertGreater(sky_pins.MADE["c47x"], 0.0)
        self.assertLess(sky_pins.MADE["c47x"], 1.0)
        self.assertNotEqual(sky_pins.S13, sky_pins.S14)
        self.assertGreater(sky_pins.S13[3], 0.0)
        tf4 = lf.table_1d(fog_pins.load_tables()[0])
        for d in sky_pins.DIRECTIONS:
            n, uv, a, paths, E, clear, first, both = sky_pins.one(sky_pins.MADE, d, tf4)
            self.assertNotEqual(first, clear)
            self.assertNotEqual(both, first)
            n, uv, a, paths, E, clear, first, both = sky_pins.one(sky_pins.REAL, d, tf4)
            self.assertEqual(both, first)  # (c47.x = 0: the second texture is not read)
        # The named places are among the directions: the poles, the horizon, the seam on -x on both sides.
        for d in ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (-1.0, 0.0, 0.0), (-1.0, 1e-6, 0.0), (-1.0, -1e-6, 0.0)):
            self.assertIn(d, sky_pins.DIRECTIONS)
        u_on, _ = lf.sky_latlong([-1.0, 0.0, 0.0])
        u_south, _ = lf.sky_latlong([-1.0, f32(-1e-6), 0.0])
        self.assertGreater(u_on, 1.0)
        self.assertLess(u_south, 0.0)

    def test_the_mirrors_literals_are_the_references(self):
        L = lf.SKY_LITERALS
        atan = [float(v) for v in re.findall(NUMBER, _one(r"inline constexpr double kSkyAtan\[5\] = \{(.*?)\};", self.logic, "kSkyAtan"))]
        self.assertEqual(atan, [L[252][3], L[253][3], L[250][1], L[251][1], L[254][1]])
        acos = [float(v) for v in re.findall(NUMBER, _one(r"inline constexpr double kSkyAcos\[4\] = \{(.*?)\};", self.logic, "kSkyAcos"))]
        self.assertEqual(acos, [L[252][1], L[252][2], L[253][2], L[250][0]])
        pi, half = _one(r"inline constexpr double kSkyPi = (" + NUMBER + r"), kSkyHalfPi = (" + NUMBER + r");", self.logic, "pi")
        self.assertEqual((float(pi), float(half)), (L[251][2], L[252][0]))
        self.assertEqual(-float(pi), L[251][0])
        two_pi, two_over = _one(r"inline constexpr double kSkyInvTwoPi = (" + NUMBER + r"), kSkyTwoOverPi = (" + NUMBER + r");", self.logic, "1 / 2 pi")
        self.assertEqual((float(two_pi), float(two_over)), (L[254][3], L[254][2]))
        power, horizon = _one(r"inline constexpr double kSkyPathPower = (" + NUMBER + r"), kSkyPathHorizon = (" + NUMBER + r");", self.logic, "paths")
        self.assertEqual((float(power), float(horizon)), (L[254][0], L[250][3]))
        self.assertEqual(float(_one(r"inline constexpr double kSkyInvLn2 = (" + NUMBER + r");", self.logic, "1 / ln 2")), L[255][0])
        self.assertEqual((L[251][3], L[250][2], L[253][0]), (-2.0, 0.5, 1.0))  # (written as plain numbers in the mirror and the shader)
        # The mirror's lines, term for term as the reference's.
        for line in ("double at = q * (q2 * (q2 * (q2 * (q2 * kSkyAtan[4] + kSkyAtan[3]) + kSkyAtan[2]) + kSkyAtan[1]) + kSkyAtan[0]);",
                     "if (ay > ax) at = at + (-2.0 * at + kSkyHalfPi);", "if (x < 0.0) at = at + -kSkyPi;",
                     "if ((x < y ? x : y) < 0.0 && (x < y ? y : x) >= 0.0) at = at - (at + at);",
                     "const double poly = az * (az * (az * kSkyAcos[3] + kSkyAcos[2]) + kSkyAcos[1]) + kSkyAcos[0];",
                     "if (z < 0.0) ac = ac + (-2.0 * root * poly + kSkyPi);", "uv[0] = 0.5 + at * kSkyInvTwoPi;",
                     "uv[1] = (kSkyHalfPi - ac) * kSkyTwoOverPi;", "const double k = az > 0.0 ? std::pow(az, kSkyPathPower) : 0.0;",
                     "const double h = finite(c.c67y) * (kSkyPathHorizon - k);",
                     "const double x = -(l[0] * finite(c.ext1[i]) + l[1] * finite(c.ext2[i])) * kSkyInvLn2;",
                     "base[i] = (1.0 - E[i]) * (std::isfinite(S[i]) ? S[i] : 0.0);", "const double v = base[i] + alpha * (col[i] - base[i]);"):
            self.assertEqual(self.logic.count(line), 1, line)

    def test_the_shaders_literals_are_the_references(self):
        L = lf.SKY_LITERALS
        s = self.sky_ps
        poly = _one(r"float at = q \* \(q2 \* \(q2 \* \(q2 \* \(q2 \* (" + NUMBER + r") \+ (" + NUMBER + r")\) \+ (" + NUMBER + r")\) \+ (" +
                    NUMBER + r")\) \+ (" + NUMBER + r")\);", s, "the arctangent's polynomial")
        self.assertEqual([float(v) for v in poly], [L[254][1], L[251][1], L[250][1], L[253][3], L[252][3]])
        self.assertEqual(float(_one(r"if \(a\.y > a\.x\) at = at \+ \(-2\.0 \* at \+ (" + NUMBER + r")\);", s, "pi / 2")), L[252][0])
        self.assertEqual(float(_one(r"if \(n\.x < 0\.0\) at = at \+ (" + NUMBER + r");", s, "-pi")), L[251][0])
        acos = _one(r"float poly = a\.z \* \(a\.z \* \(a\.z \* (" + NUMBER + r") \+ (" + NUMBER + r")\) \+ (" + NUMBER + r")\) \+ (" + NUMBER +
                    r");", s, "the arccosine's polynomial")
        self.assertEqual([float(v) for v in acos], [L[250][0], L[253][2], L[252][2], L[252][1]])
        self.assertEqual(float(_one(r"if \(n\.z < 0\.0\) ac = ac \+ \(-2\.0 \* root \* poly \+ (" + NUMBER + r")\);", s, "pi")), L[251][2])
        uv = _one(r"return float2\(0\.5 \+ at \* (" + NUMBER + r"), \((" + NUMBER + r") - ac\) \* (" + NUMBER + r")\);", s, "u and v")
        self.assertEqual([float(v) for v in uv], [L[254][3], L[252][0], L[254][2]])
        self.assertEqual(float(_one(r"float k = \(az > 0\.0\) \? exp2\((" + NUMBER + r") \* log2\(az\)\) : 0\.0;", s, "the power")), L[254][0])
        self.assertEqual(float(_one(r"float h = s_c67y \* \((" + NUMBER + r") - k\);", s, "the horizon's factor")), L[250][3])
        self.assertEqual(float(_one(r"return exp2\((" + NUMBER + r") \* \(l\.x \* s_ext1 \+ l\.y \* s_ext2\)\);", s, "1 / ln 2")), -L[255][0])
        # Every literal of the shader's own text is a single: what the compiler makes of it is the number.
        own = s[s.index("float2 SkyLatLong"):s.index("float4 sky_sample")]
        for v in re.findall(r"(?<![\w.])" + NUMBER, own):
            self.assertEqual(float(v), f32(float(v)), v)
        # The triangle is at depth 1, the clear value; the shader reads no depth and samples at level 0.
        _one(r"return float4\(uv \* float2\(2\.0, -2\.0\) \+ float2\(-1\.0, 1\.0\), 1\.0, 1\.0\);", self.sky_vs, "the far depth")
        self.assertNotIn("pos.z", own + s[s.index("float4 main"):])
        self.assertEqual(s.count("SampleLevel("), 4)
        self.assertEqual(len(re.findall(r"SampleLevel\(s_\w+, uv, 0\)", s)), 4)

    def test_the_mirror_is_the_reference_on_random_directions(self):
        # A Python transcription of the shader's statements (single literals, double arithmetic) against
        # light_formulas on random directions and both register sets: the text held by the tests above
        # computes the reference's sky.
        tf4 = lf.table_1d(fog_pins.load_tables()[0])
        rng = random.Random(5)

        def shader(n, regs, s13, s14):
            x, y, z = lf.normalize(n)
            S = tf4(lf.sat(lf.dot([x, y, z], regs["c66"])))
            az = abs(z)
            k = 2.0 ** (0.20000000298023224 * math.log2(az)) if az > 0.0 else 0.0
            h = regs["c67y"] * (1.0499999523162842 - k)
            l1, l2 = h + (regs["c31x"] - h) * k, h + (regs["c46x"] - h) * k
            sky = [(1.0 - 2.0 ** (-1.4426950216293335 * (l1 * regs["c64"][i] + l2 * regs["c65"][i]))) * S[i] for i in range(3)]
            col = [v * v for v in s13[:3]]
            col = [col[i] + regs["c47x"] * (s14[i] * s14[i] - col[i]) for i in range(3)]
            alpha = s13[3] + regs["c47x"] * (s14[3] - s13[3])
            return [sky[i] + alpha * (col[i] - sky[i]) for i in range(3)]

        for regs in (sky_pins.REAL, sky_pins.MADE):
            for _ in range(200):
                n = [f32(rng.uniform(-1.0, 1.0)) for _ in range(3)]
                want = lf.sky(n, dict(regs), tf4, lambda u, v: sky_pins.S13, lambda u, v: sky_pins.S14)
                got = shader(n, regs, sky_pins.S13, sky_pins.S14)
                for g, w in zip(got, want):
                    self.assertAlmostEqual(g, w, places=12)


if __name__ == "__main__":
    unittest.main()
