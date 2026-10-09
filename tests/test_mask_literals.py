"""The mask's literals are written in four places: the Python reference
(tools/xdk_sigmatch/light_formulas.py: MASK_NEAR_TAPS and mask_0f99's defaults), the generator of the
numbers the C++ mirrors are pinned to (tools/xdk_sigmatch/mask_pins.py: it made the pins of
tests/native/test_clay_logic.cpp, cases 560 on, and of tests/test_light_check.py,
MaskPinnedForTheRenderer), the renderer's pure mirror (src/native/render/clay_logic.h: kMaskNearTaps,
kMaskTapWeight, kMaskFarReach, kMaskFadeSlope, kMaskNearRadiusTexels) and the mask's pixel shader
(src/native/fable2_native_shaders.h: kClayMaskPs). This test reads the two C++ texts and holds all four
to one set of numbers, each as the single-precision value the renderer and the GPU hold."""
import inspect
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import mask_pins  # noqa: E402

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


def logic_constant(text, name):
    """The value of `inline constexpr <type> name = <number>f;` in clay_logic.h."""
    return float(_one(r"inline constexpr \w+ " + name + r" = (" + NUMBER + r")f?;", text, name))


def logic_taps(text):
    body = _one(r"inline constexpr float kMaskNearTaps\[12\]\[2\] = \{(.*?)\n\};", text, "kMaskNearTaps")
    return [(float(a), float(b)) for a, b in re.findall(r"\{(" + NUMBER + r")f, (" + NUMBER + r")f\}", body)]


def shader_source(text):
    return _one(r'inline constexpr const char\* kClayMaskPs = R"hlsl\((.*?)\)hlsl";', text, "kClayMaskPs")


def shader_taps(source):
    body = _one(r"static const float2 kMaskTaps\[12\] = \{(.*?)\};", source, "kMaskTaps")
    return [(float(a), float(b)) for a, b in re.findall(r"float2\((" + NUMBER + r"), (" + NUMBER + r")\)", body)]


class MaskLiterals(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.shader = shader_source(_text("src/native/fable2_native_shaders.h"))
        cls.defaults = {k: v.default for k, v in inspect.signature(lf.mask_0f99).parameters.items()}

    def assertSameFloats(self, got, want, what):
        self.assertEqual(len(got), len(want), what)
        for i, (g, w) in enumerate(zip(got, want)):
            self.assertEqual(f32(g), f32(w), f"{what}[{i}]: {g!r} against {w!r}")

    def test_the_twelve_taps(self):
        want = [v for tap in lf.MASK_NEAR_TAPS for v in tap]
        self.assertEqual(len(lf.MASK_NEAR_TAPS), 12)
        self.assertEqual(logic_constant(self.logic, "kMaskNearTapCount"), 12)
        self.assertSameFloats([v for tap in logic_taps(self.logic) for v in tap], want, "kMaskNearTaps")
        self.assertSameFloats([v for tap in shader_taps(self.shader) for v in tap], want, "kMaskTaps of kClayMaskPs")
        # (every tap is used once in the shader's loop, and the loop is the twelve)
        self.assertEqual(len(re.findall(r"for \(int i = 0; i < 12; \+\+i\)", self.shader)), 1)

    def test_tap_weight(self):
        weight = logic_constant(self.logic, "kMaskTapWeight")
        self.assertEqual(f32(weight), f32(1.0 / 12.0))
        # mask_near's default is 1 / len(offsets); mask_0f99 passes its own through (None: that default).
        self.assertIsNone(self.defaults["tap_weight"])
        self.assertEqual(f32(mask_pins.TAP_WEIGHT), f32(weight))
        in_shader = float(_one(r"return lit \* (" + NUMBER + r");", self.shader, "the tap weight in MaskNear"))
        self.assertEqual(f32(in_shader), f32(weight))

    def test_far_reach(self):
        reach = logic_constant(self.logic, "kMaskFarReach")
        self.assertEqual(reach, self.defaults["far_reach"])
        self.assertEqual(reach, mask_pins.FAR_REACH)
        in_shader = float(_one(r"m_split\.y \+ (" + NUMBER + r") > d\)", self.shader, "the far reach in main"))
        self.assertEqual(f32(in_shader), f32(reach))

    def test_fade_slope(self):
        slope = logic_constant(self.logic, "kMaskFadeSlope")
        self.assertEqual(f32(slope), f32(self.defaults["slope"]))
        self.assertEqual(f32(slope), f32(mask_pins.SLOPE))
        in_shader = float(_one(r"saturate\((" + NUMBER + r") \* \(d - m_split\.y\)\)", self.shader, "the slope in MaskCombine"))
        self.assertEqual(f32(in_shader), f32(slope))

    def test_near_radius(self):
        texels = logic_constant(self.logic, "kMaskNearRadiusTexels")
        size = logic_constant(self.logic, "kShadowMapSize")
        self.assertEqual((texels, size), (2.5, 1024))
        # The radius is the texels over the map's size in the header, and the generator's is that number.
        _one(r"inline constexpr float kMaskNearRadius = kMaskNearRadiusTexels / float\(kShadowMapSize\);", self.logic,
             "kMaskNearRadius")
        self.assertEqual(f32(mask_pins.RADIUS), f32(texels / size))
        self.assertEqual((mask_pins.MAP_SIZE, mask_pins.W, mask_pins.H), (1024, 1120, 720))
        # The shader takes the radius from its constants (ClayMaskConstants::radius = kMaskNearRadius), not a literal.
        self.assertIn("p.xy + m_radius * o", self.shader)
        _one(r"c\.radius = kMaskNearRadius;", self.logic, "MakeClayMaskConstants' radius")

    def test_the_generator_uses_the_reference_taps(self):
        self.assertIs(mask_pins.TAPS, lf.MASK_NEAR_TAPS)

    def test_a_changed_literal_is_seen(self):
        # The parsers read what is written: one digit off in either text fails the comparison.
        tap = repr(lf.MASK_NEAR_TAPS[5][0])
        self.assertIn(tap, self.logic)
        self.assertIn(tap, self.shader)
        self.assertNotEqual(f32(logic_taps(self.logic.replace(tap, "0.5074309"))[5][0]), f32(lf.MASK_NEAR_TAPS[5][0]))
        self.assertNotEqual(f32(shader_taps(self.shader.replace(tap, "0.5074309"))[5][0]), f32(lf.MASK_NEAR_TAPS[5][0]))


if __name__ == "__main__":
    unittest.main()
