"""The post chain's numbers are written in several places: the Python reference (tools/xdk_sigmatch/light_formulas.py:
post_composite, post_table_texel, post_unlit_scale, post_ramp_index, post_scene), the generator of the numbers the
C++ mirrors are pinned to (tools/xdk_sigmatch/post_pins.py), the pinned lines themselves
(tests/native/test_clay_logic.cpp, cases 600 on) with the table they were made with
(tests/native/post_pins_table.inc), the renderer's pure mirrors (src/native/render/clay_logic.h, "Native post":
PostTableTexel, PostUnlitScale, PostCompositeMirror, PostRampIndex and the steps' constants), the capture's block
(src/native/capture/post_block.h) and the two post shaders (src/native/fable2_native_shaders.h:
kClayPostCompositePs, kClayPostRampPs). This test reads the C++ texts and holds them to one set of numbers, as
tests/test_fog_literals.py does for the fog."""
import math
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import post_pins  # noqa: E402

NUMBER = r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?"

# The pins of plan Task 5 (the composite and the ramp); the generator's other sections are later tasks'.
TASK5_SECTIONS = ("// table texel pins", "// unlit scale pins", "// ramp pins", "// real registers", "// real composite pins",
                  "// made registers", "// made composite pins")


def f32(x):
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def _text(relative):
    return (ROOT / relative).read_text(encoding="ascii")


def _one(pattern, text, what):
    found = re.findall(pattern, text, re.S)
    if len(found) != 1:
        raise AssertionError(f"{what}: {len(found)} matches of {pattern!r}")
    return found[0]


def task5_lines():
    """The generator's lines of the sections above: each section's comment line and the pins under it."""
    out, taking = [], False
    for line in post_pins.lines():
        if line.startswith("//"):
            taking = line.startswith(TASK5_SECTIONS)
        if taking:
            out.append(line)
    return out


class PostLiterals(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.block = _text("src/native/capture/post_block.h")
        cls.logic_test = _text("tests/native/test_clay_logic.cpp")
        cls.shaders = _text("src/native/fable2_native_shaders.h")
        cls.table_inc = _text("tests/native/post_pins_table.inc")
        cls.lines = task5_lines()
        cls.composite = _one(r"inline constexpr const char\* kClayPostCompositePs = FABLE2_CLAY_POST_HLSL R\"hlsl\((.*?)\)hlsl\";", cls.shaders,
                             "the composite's text")
        cls.ramp = _one(r"inline constexpr const char\* kClayPostRampPs = FABLE2_CLAY_POST_HLSL R\"hlsl\((.*?)\)hlsl\";", cls.shaders,
                        "the ramp's text")

    def test_every_generated_line_is_in_the_cpp_test(self):
        # 41 table pins, 10 unlit scales, 15 ramp entries, 12 composite points for each of the two register sets,
        # and the seven comment lines that name them.
        have = {line.strip() for line in self.logic_test.splitlines()}
        self.assertEqual(len(self.lines), 41 + 10 + 15 + 12 + 12 + 7)
        self.assertEqual(sum(1 for line in self.lines if line.startswith("//")), 7)
        for line in self.lines:
            self.assertIn(line.strip(), have, "not in tests/native/test_clay_logic.cpp: run tools/xdk_sigmatch/post_pins.py")

    def test_a_changed_digit_is_seen(self):
        have = {line.strip() for line in self.logic_test.splitlines()}
        pin = next(line for line in self.lines if "0.4214398567838915" in line)
        self.assertIn(pin.strip(), have)
        self.assertNotIn(pin.replace("0.4214398567838915", "0.4214398567838916").strip(), have)
        texel = next(line for line in self.lines if line.strip() == "{0.0009775625076144934f, 1},")
        self.assertNotIn(texel.replace(", 1}", ", 0}").strip(), have)

    def test_the_table_is_the_generators(self):
        want = post_pins.table_lines()
        got = [line for line in self.table_inc.splitlines() if not line.startswith("//")]
        self.assertEqual(got, want, "run tools/xdk_sigmatch/post_pins.py --table")
        table = post_pins.load_table()
        self.assertEqual(len(table), 512)
        self.assertEqual(table[0], 0.0)
        # (the texel the C++ test names, and the unlit scale's)
        self.assertIn("table[128] == %rf" % table[128], self.logic_test)
        self.assertEqual(int(_one(r"inline constexpr uint32_t kPostUnlitTexel = (\d+);", self.logic, "the unlit scale's texel")), 128)

    def test_the_register_sets_are_the_generators(self):
        for name, regs in post_pins.SETS:
            composite = [regs["c31x"], regs["c46x"], regs["c47x"], regs["c77"][0], regs["c77"][1], regs["c78x"]]
            bright = [regs["bright"]["c31x"], regs["bright"]["c46x"], regs["bright"]["c47x"], regs["bright"]["c77"][0],
                      regs["bright"]["c77"][1]]
            self.assertIn("const float %s_composite[6] = %s;" % (name, post_pins.floats(composite)), self.logic_test)
            self.assertIn("const float %s_bright[5] = %s;" % (name, post_pins.floats(bright)), self.logic_test)
        # The made-up set exercises what the captured one does not: another coordinate scale, a grey fade that
        # reaches far up, a saturation step, another power.
        real, made = post_pins.REAL, post_pins.MADE
        self.assertEqual(real["c46x"], 0.25)
        self.assertNotEqual(made["c46x"], real["c46x"])
        self.assertLess(made["c47x"], 10.0)
        self.assertGreater(abs(made["c77"][0]), 0.1)
        self.assertNotEqual(f32(made["c78x"]), f32(real["c78x"]))

    def test_the_addressing_is_the_nearest_texel(self):
        # Controller ruling 19: floor(u * 512 + 0.5), clamped; not floor(u * 512). The reference, the mirror's
        # text and the shader's text.
        for k in (0, 1, 12, 127, 128, 510):
            below, above = f32((k + 0.5) / 512.0 - 1e-6), f32((k + 0.5) / 512.0 + 1e-6)
            self.assertEqual((lf.post_table_texel(below), lf.post_table_texel(above)), (k, k + 1))
            self.assertEqual(lf.post_table_texel(f32(k / 512.0)), k)
            # (a floor would read k on both sides of the half, and k - 1 just below k / 512)
            self.assertEqual(int(math.floor(above * 512.0)), k)
        self.assertEqual((lf.post_table_texel(float("nan")), lf.post_table_texel(-1.0), lf.post_table_texel(1e30)), (0, 0, 511))
        scale, half = _one(r"const float x = (" + NUMBER + r")f \* \(u < 1\.0f \? u : 1\.0f\) \+ (" + NUMBER + r")f;", self.logic,
                           "PostTableTexel's texel coordinate")
        self.assertEqual((float(scale), float(half)), (float(lf.POST_TABLE_TEXELS), 0.5))
        _one(r"return i < capture::kPostTableTexels - 1 \? i : capture::kPostTableTexels - 1;", self.logic, "PostTableTexel's bound")
        self.assertEqual(int(_one(r"kPostTableTexels = (\d+), kPostBlockMaxAge", self.block, "the table's size")), lf.POST_TABLE_TEXELS)
        s_scale, s_half, s_last = _one(r"uint i = min\(uint\((" + NUMBER + r") \* saturate\(u\) \+ (" + NUMBER + r")\), (\d+)u\);",
                                       self.composite, "the shader's texel")
        self.assertEqual((float(s_scale), float(s_half), int(s_last)), (float(lf.POST_TABLE_TEXELS), 0.5, lf.POST_TABLE_TEXELS - 1))
        _one(r"return p_table\[i >> 2\]\[i & 3\];", self.composite, "the shader's read of the table")
        self.assertEqual(int(_one(r"float4 p_table\[(\d+)\];", self.shaders, "the table's array")), lf.POST_TABLE_TEXELS // 4)
        _one(r"float table\[capture::kPostTableTexels / 4\]\[4\];", self.logic, "ClayPostConstants::table")

    def test_luminance_and_the_scenes_clamp(self):
        lum = _one(r"inline constexpr double kPostLum\[3\] = \{(" + NUMBER + r"), (" + NUMBER + r"), (" + NUMBER + r")\};", self.logic,
                   "kPostLum")
        self.assertEqual(tuple(float(v) for v in lum), lf.POST_LUM)
        shader_lum = _one(r"float post_lum\(float3 c\) \{ return dot\(c, float3\((" + NUMBER + r"), (" + NUMBER + r"), (" + NUMBER +
                          r")\)\); \}", self.composite, "post_lum")
        self.assertEqual(tuple(float(v) for v in shader_lum), lf.POST_LUM)
        scene_max = float(_one(r"inline constexpr float kPostSceneMax = (" + NUMBER + r")f;", self.logic, "kPostSceneMax"))
        self.assertEqual(scene_max, lf.POST_SCENE_MAX)
        # The rule of a scene channel: not above 0 is 0, above the clamp is the clamp; in the reference, the
        # mirror and the shader (which takes the clamp from the block).
        for v, want in ((float("nan"), 0.0), (-1.0, 0.0), (0.0, 0.0), (2.5, 2.5), (40.0, 31.875), (float("inf"), 31.875)):
            self.assertEqual(lf.post_scene(v), want)
        _one(r"float3 post_scene\(float3 v\) \{ return \(v > 0\.0\) \? min\(v, p_scene_max\) : float3\(0\.0, 0\.0, 0\.0\); \}",
             self.composite, "post_scene")
        _one(r"if \(!\(v > 0\.0f\)\) return 0\.0;\n  return double\(v < scene_max \? v : scene_max\);", self.logic, "PostScene")
        _one(r"k\.scene_max = kPostSceneMax;", self.logic, "the block's clamp")

    def test_the_composites_terms(self):
        # The shader's statements are the reference's terms, in its order.
        body = _one(r"float3 PostComposite\(float3 scene, float3 bloom\) \{\n(.*?)\n\}", self.composite, "PostComposite")
        self.assertEqual([line.strip() for line in body.split("\n")], [
            "float3 s = post_scene(scene);",
            "float L = post_lum(s);",
            "float e = p_c77y * post_table(p_c46x * L);",
            "float3 c = saturate(e * (L + saturate(p_c47x * L) * (s - L)));",
            "float3 b = saturate(bloom * p_c31x);",
            "float3 scr = c + b * (1.0 - c);",
            "float3 t = scr + p_c77x * (scr - post_lum(scr));",
            "return saturate(post_pow(t, p_c78x));",
        ])
        # The reference on a hand case: a grey at scene luminance 1 with a flat gain g comes out as g^c78.x.
        table = [0.25] * 512
        out = lf.post_composite([1.0, 1.0, 1.0], [0.0, 0.0, 0.0], table, 3.0, 0.25, 50.0, (0.0, 2.0), 0.5)
        for v in out:
            self.assertAlmostEqual(v, 0.5 ** 0.5, places=12)
        # The registers' places in the block are those of the capture's block: c31.x, c46.x, c47.x, c77.x, c77.y, c78.x.
        _one(r"float composite\[6\];   // 37B1: c31\.x, c46\.x, c47\.x, c77\.x, c77\.y, c78\.x", self.block, "PostBlock::composite")
        _one(r"float p_c31x; float p_c46x; float p_c47x; float p_c77x; +// the composite 37B1", self.shaders, "cbuffer Post's first row")
        _one(r"float p_c77y; float p_c78x; float p_scene_max; float p_previous;", self.shaders, "cbuffer Post's second row")
        _one(r"const double c31x = k\.composite\[0\], c47x = k\.composite\[2\], c77x = k\.composite\[3\], c77y = k\.composite\[4\], "
             r"c78x = k\.composite\[5\];", self.logic, "the mirror's registers")

    def test_the_bounds_are_the_blocks(self):
        # MakeClayPostConstants judges the registers again by MakePostBlock's bounds: the two conditions are the
        # same numbers in the same order.
        def numbers(text, what):
            cond = _one(what, text, what)
            return [float(v) for v in re.findall(r"(?:<=|>=|<|>) (" + NUMBER + r")f", cond)]
        block_c = numbers(self.block, r"if \((!\(composite\[0\].*?)\) \{\n    return fail\(PostBlockReason::kRange\);")
        block_h = numbers(self.block, r"if \((!\(bright\[0\].*?)\) \{\n    return fail\(PostBlockReason::kRange\);")
        logic_c = numbers(self.logic, r"if \((!\(c\[0\] >= .*?)\) \{\n    return false;")
        logic_h = numbers(self.logic, r"if \((!\(h\[0\] >= .*?)\) \{\n    return false;")
        self.assertEqual(logic_c, block_c)
        self.assertEqual(logic_h, block_h)
        self.assertEqual(len(block_c), 11)
        self.assertEqual(len(block_h), 9)
        _one(r"if \(!\(texel >= 0\.0f && texel < 4\.0f\)\) return fail\(PostBlockReason::kTableTexel\);", self.block, "the block's texel rule")
        _one(r"for \(float texel : b\.table\) \{\n    if \(!\(texel >= 0\.0f && texel < 4\.0f\)\) return false;", self.logic,
             "the constants' texel rule")

    def test_the_unlit_scale(self):
        self.assertEqual(float(_one(r"inline constexpr float kPostUnlitMax = (" + NUMBER + r")f;", self.logic, "kPostUnlitMax")), 64.0)
        _one(r"return \(gain > 1\.0 / 64\.0 && std::isfinite\(gain\)\) \? float\(1\.0 / gain\) : kPostUnlitMax;", self.logic,
             "PostUnlitScale")
        table = [0.0] * 512
        table[128] = 0.25
        self.assertEqual(lf.post_unlit_scale(2.0, table), 2.0)
        table[128] = 1.0 / 64.0
        self.assertEqual(lf.post_unlit_scale(1.0, table), 64.0)
        # Every unlit colour of the four scene pixel shaders takes the word, and nothing else does.
        self.assertEqual(self.shaders.count(" * f_post"), 3 + 6 + 2)
        self.assertEqual(self.shaders.count("clay_linear(base * shade) * f_post"), 3)
        self.assertEqual(self.shaders.count("clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post"), 3)
        self.assertEqual(self.shaders.count("clay_linear(base * (0.35 + 0.65 * facet)) * f_post"), 3)
        self.assertEqual(self.shaders.count("saturate(colour * (shade * shade)) * f_post"), 2)
        self.assertEqual(len(re.findall(r"clay_lit\([^;]*f_post", self.shaders)), 0)
        self.assertEqual(_one(r'\{"f_post", (\d+), (\d+)\}', self.shaders, "f_post's field"), ("60", "4"))
        _one(r"float post = 1\.0f;", self.logic, "ClayFrameConstants::post")

    def test_the_ramp(self):
        scale, half, last = _one(r"uint post_ramp_index\(float v\) \{ return min\(uint\(saturate\(v\) \* (" + NUMBER + r") \+ (" + NUMBER +
                                 r")\), (\d+)u\); \}", self.ramp, "the shader's entry")
        self.assertEqual((float(scale), float(half), int(last)), (255.0, 0.5, 255))
        m_scale, m_half = _one(r"const float x = \(v < 1\.0f \? v : 1\.0f\) \* (" + NUMBER + r")f \+ (" + NUMBER + r")f;", self.logic,
                               "PostRampIndex")
        self.assertEqual((float(m_scale), float(m_half)), (255.0, 0.5))
        for n in range(256):
            self.assertEqual(lf.post_ramp_index(f32(n / 255.0)), n)
        self.assertEqual((lf.post_ramp_index(float("nan")), lf.post_ramp_index(-1.0), lf.post_ramp_index(9.0)), (0, 0, 255))
        self.assertEqual(int(_one(r"float4 p_ramp\[(\d+)\];", self.shaders, "the ramp's array")), 256)
        _one(r"float ramp\[256\]\[4\];", self.logic, "ClayPostConstants::ramp")
        _one(r"return float4\(p_ramp\[post_ramp_index\(v\.r\)\]\.r, p_ramp\[post_ramp_index\(v\.g\)\]\.g, "
             r"p_ramp\[post_ramp_index\(v\.b\)\]\.b, 1\.0\);", self.ramp, "the ramp's read")

    def test_the_layout(self):
        self.assertEqual(_one(r'\{"p_table", (\d+), (\d+)\}', self.shaders, "p_table's field"), ("144", str(128 * 16)))
        self.assertEqual(_one(r'\{"p_ramp", (\d+), (\d+)\}', self.shaders, "p_ramp's field"), ("2192", str(256 * 16)))
        self.assertEqual(int(_one(r"inline constexpr unsigned kClayPostSize = (\d+);", self.shaders, "kClayPostSize")), 2192 + 4096)
        _one(r"static_assert\(kClayPostSlots == 25\);", self.logic, "the slots")
        self.assertEqual((2192 + 4096 + 255) // 256, 25)
        # The output mapping, which a frame without post is drawn with, is the text it was.
        mapping = _one(r"inline constexpr const char\* kOutputMapPs = R\"hlsl\(\n(.*?)\)hlsl\";", self.shaders, "the output mapping")
        self.assertEqual(mapping, "cbuffer Map : register(b0) { float exposure; float3 map_pad; };\n"
                                  "Texture2D<float4> linear_target : register(t0);\n"
                                  "float4 main(float4 pos : SV_Position) : SV_Target {\n"
                                  "  float3 v = linear_target.Load(int3(pos.xy, 0)).rgb * exposure;\n"
                                  "  v = (v > 0.0) ? min(v, 1.0) : float3(0.0, 0.0, 0.0);\n"
                                  "  return float4(sqrt(v), 1.0);\n"
                                  "}\n")
        self.assertEqual(float(_one(r"inline constexpr float kLitExposure = (" + NUMBER + r")f;", self.logic, "kLitExposure")), 2.22)
        self.assertEqual(float(_one(r"inline constexpr float kClayMapExposure = (" + NUMBER + r")f;", self.logic, "kClayMapExposure")), 1.0)


if __name__ == "__main__":
    unittest.main()
