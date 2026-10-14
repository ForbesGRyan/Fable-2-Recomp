"""The post chain's numbers are written in several places: the Python reference (tools/xdk_sigmatch/light_formulas.py:
post_composite, post_table_texel, post_unlit_scale, post_ramp_index, post_scene), the generator of the numbers the
C++ mirrors are pinned to (tools/xdk_sigmatch/post_pins.py), the pinned lines themselves
(tests/native/test_clay_logic.cpp, cases 600 on) with the table they were made with
(tests/native/post_pins_table.inc), the renderer's pure mirrors (src/native/render/clay_logic.h, "Native post":
PostTableTexel, PostUnlitScale, PostCompositeMirror, PostRampIndex and the steps' constants), the capture's block
(src/native/capture/post_block.h) and the post shaders (src/native/fable2_native_shaders.h:
kClayPostCompositePs, kClayPostRampPs, and the bloom's kClayPostDownsamplePs, kClayPostBrightPs, kClayPostBlurPs of
plan Task 8, with the mirrors PostDownsampleMirror, PostBrightMirror, PostBlurMirror, PostLevel and the place of the
composite's bloom sample). This test reads the C++ texts and holds them to one set of numbers, as
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
import post_replay  # noqa: E402

NUMBER = r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?"

# The pins of plan Task 5 (the composite and the ramp); the generator's other sections are later tasks'.
TASK5_SECTIONS = ("// table texel pins", "// unlit scale pins", "// ramp pins", "// real registers", "// real composite pins",
                  "// made registers", "// made composite pins")
# The pins of plan Task 8 (the bloom's three mirrors).
TASK8_SECTIONS = ("// real bright pins", "// made bright pins", "// downsample pins", "// real blur pins", "// made blur pins")
# The pins of plan Task 13 (the scene's furniture: the moon's, the glare's and a corner's mirrors).
TASK13_SECTIONS = ("// quad moon pins", "// quad glare pins", "// quad position pins")
# The pins of plan Task 14 (the sun layer: the radial blur's, the reduction's and the five layer quads' mirrors).
TASK14_SECTIONS = ("// real radial pins", "// made radial pins", "// edge radial pins", "// reduce pins", "// sun beam pins",
                   "// sun flare pins", "// sun glow pins", "// sun disc pins", "// sun occluder pins")


def f32(x):
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def _text(relative):
    return (ROOT / relative).read_text(encoding="ascii")


def _one(pattern, text, what):
    found = re.findall(pattern, text, re.S)
    if len(found) != 1:
        raise AssertionError(f"{what}: {len(found)} matches of {pattern!r}")
    return found[0]


def section_lines(sections):
    """The generator's lines of the sections named: each section's comment line and the pins under it."""
    out, taking = [], False
    for line in post_pins.lines():
        if line.startswith("//"):
            taking = line.startswith(sections)
        if taking:
            out.append(line)
    return out


class RetryLiterals(unittest.TestCase):
    """ClayPass::Ensure has two ways to try its objects anew: another device, and the F6 retry after a failure. No
    test compiles clay_pass.cpp, so the text is held: every `*_failed_` flag the first clears, the second clears
    too (final code review, Minor 4: the previous-frame blend's second target stayed refused after a retry)."""

    def setUp(self):
        body = _one(r"bool ClayPass::Ensure\(nrhi::Device\* dev\) \{\n(.*?)\n\}\n", _text("src/native/render/clay_pass.cpp"),
                    "ClayPass::Ensure")
        self.device, self.retry = _one(r"if \(dev != device_\) \{\n(.*?)\n  \} else \{\n(.*?)\n  \}\n", body, "the two branches")

    def test_the_retry_clears_every_flag_another_device_clears(self):
        cleared = [set(re.findall(r"\b(\w+_failed_) = false;", t)) for t in (self.device, self.retry)]
        self.assertIn("front_second_failed_", cleared[0])
        self.assertGreaterEqual(len(cleared[0]), 9)
        self.assertEqual(cleared[0], cleared[1])

    def test_no_failed_flag_of_the_pass_is_left_out(self):
        flags = set(re.findall(r"\b(\w+_failed_) = false[,;]", _text("src/native/render/clay_pass.h")))
        self.assertEqual(len(flags), 15)   # (the eleventh: the wind vertex program's, foliage plan Task 10; the twelfth: the coverage half program's, Task 13; the thirteenth: the impostor step's, distant trees plan Task 10; the fourteenth and fifteenth: the water's targets' and its step's, water plan Tasks 11 and 12)
        self.assertTrue({"water_targets_failed_", "water_objects_failed_"} <= flags)
        self.assertEqual(flags, set(re.findall(r"\b(\w+_failed_) = false;", self.retry)))


class PreviousBlendLiterals(unittest.TestCase):
    """Plan Task 15: the previous-frame blend's numbers in the reference, the generator, the mirror, the shader."""

    @classmethod
    def setUpClass(cls):
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.logic_test = _text("tests/native/test_clay_logic.cpp")
        cls.shaders = _text("src/native/fable2_native_shaders.h")
        cls.pass_cpp = _text("src/native/render/clay_pass.cpp")
        cls.lines = section_lines(("// previous blend pins",))

    def test_every_generated_line_is_in_the_cpp_test(self):
        have = {line.strip() for line in self.logic_test.splitlines()}
        self.assertEqual(len(self.lines), 12)
        for line in self.lines:
            self.assertIn(line.strip(), have, "not in tests/native/test_clay_logic.cpp: run tools/xdk_sigmatch/post_pins.py")
        pin = next(line for line in self.lines if "0.7450980392156863" in line)
        self.assertNotIn(pin.replace("0.7450980392156863", "0.7450980392156864").strip(), have)

    def test_the_weight_is_the_games_blend_constant(self):
        w = f32(lf.POST_PREVIOUS_WEIGHT)
        self.assertEqual(struct.unpack(">I", struct.pack(">f", w))[0], lf.POST_PREVIOUS_BLEND)
        self.assertIn("inline constexpr float kPostPreviousWeight = %rf;" % w, self.logic)
        self.assertEqual(post_pins.PREVIOUS_W, w)

    def test_the_shader_blends_as_the_reference(self):
        composite = _one(r"inline constexpr const char\* kClayPostCompositePs = FABLE2_CLAY_POST_HLSL R\"hlsl\((.*?)\)hlsl\";", self.shaders,
                         "the composite's text")
        self.assertIn("  if (p_previous > 0.0) return float4(PostBlended(pos), 1.0);\n", composite)
        self.assertIn("  return post_level(saturate(now + (before - now) * min(p_previous, 1.0)));\n", composite)
        self.assertIn("  float3 now = post_level(PostComposite(PostSceneWithEffects(pos), veil));\n", composite)
        # The pass hands the share the pure rule gives, to the constants' field the shader reads, and reads the
        # other front target, never the one it writes.
        self.assertIn("post_constants.previous = front_step.share;", self.pass_cpp)
        self.assertIn("front_step.share > 0.0f && front_step.read != front_step.write ? front_view_[front_step.read] : nullptr", self.pass_cpp)
        self.assertIn("StepClayFront(front_state_, post_frame, post.sun && !post.presentation, front_wanted && EnsureFrontPrevious(dev), oracle_fed)", self.pass_cpp)


class CloudLiterals(unittest.TestCase):
    """Plan Task 16: the scene clouds' numbers in the reference, the generator, the C++ mirror and the shader."""

    @classmethod
    def setUpClass(cls):
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.logic_test = _text("tests/native/test_clay_logic.cpp")
        cls.shaders = _text("src/native/fable2_native_shaders.h")
        cls.pass_cpp = _text("src/native/render/clay_pass.cpp")

    def test_every_generated_line_is_in_the_cpp_test(self):
        have = {line.strip() for line in self.logic_test.splitlines()}
        lines = post_pins.cloud_lines()
        self.assertEqual(len(lines), 15 + 1)
        for line in lines:
            self.assertIn(line.strip(), have, "not in tests/native/test_clay_logic.cpp: run out of tools/xdk_sigmatch/post_pins.py")
        pin = lines[1]
        self.assertNotIn(pin.replace("0.5f}, {0.6000000238418579f", "0.5f}, {0.6000000238418580f").strip(), have)

    def test_the_literals_are_the_references(self):
        zero, thin, start, one = (f32(v) for v in lf.POST_CLOUD_C254)
        power = f32(lf.POST_CLOUD_C255X)
        self.assertEqual((zero, one, start, power), (0.0, 1.0, 1000.0, 32.0))
        self.assertEqual(post_pins.CLOUD_C254, (zero, thin, start, one))
        self.assertIn("inline constexpr float kCloudThin = %rf;" % thin, self.logic)
        self.assertIn("inline constexpr float kCloudFadeRate = %rf;" % thin, self.logic)
        self.assertIn("inline constexpr float kCloudFadeStart = %rf;" % start, self.logic)
        self.assertIn("inline constexpr float kCloudPower = %rf;" % power, self.logic)
        ps = _one(r"inline constexpr const char\* kClayCloudPs = FABLE2_CLAY_CLOUD_HLSL R\"hlsl\((.*?)\)hlsl\";", self.shaders, "the clouds' text")
        self.assertIn("saturate(%r * (length(i.world.xy - k_reg[10].xy) - %r))" % (thin, start), ps)
        self.assertIn("if (alpha > %r) {" % thin, ps)
        self.assertIn("pow(base, %r)" % power, ps)

    def test_the_registers_are_the_tables_in_its_order(self):
        cloud = next(q for q in lf.POST_QUAD_SHADERS if q.kind == "cloud")
        registers = cloud[3]
        self.assertEqual(tuple(registers), (20, 31, 46, 47, 51, 69, 70, 71, 77, 78, 79))
        names = ", ".join("kCloudC%d" % r for r in registers)
        self.assertIn("enum : uint32_t { kCloudC20 = 0, " + names.split(", ", 1)[1] + ", kCloudRegisters };", self.logic)

    def test_the_clouds_write_no_depth(self):
        # (the sun layer's depth must not hold the scene's clouds: plan Task 16 as amended)
        block = _one(r"bool ClayPass::EnsureCloudObjects\(nrhi::Device\* dev\) \{(.*?)\n\}\n", self.pass_cpp, "the clouds' objects")
        self.assertIn("pipe.depth.test_enable = true;", block)
        self.assertIn("pipe.depth.write_enable = false;", block)
        self.assertNotIn("write_enable = true", block)
        self.assertNotIn("SV_Depth", _one(r"inline constexpr const char\* kClayCloudPs = FABLE2_CLAY_CLOUD_HLSL R\"hlsl\((.*?)\)hlsl\";", self.shaders,
                                          "the clouds' text"))


class SunLayerLiterals(unittest.TestCase):
    """Plan Task 14: the sun layer's numbers in the reference, the generator, the C++ mirrors and the shaders."""

    @classmethod
    def setUpClass(cls):
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.logic_test = _text("tests/native/test_clay_logic.cpp")
        cls.shaders = _text("src/native/fable2_native_shaders.h")
        cls.pass_cpp = _text("src/native/render/clay_pass.cpp")
        cls.oracle = _text("src/native/render/post_oracle.h")
        cls.lines = section_lines(TASK14_SECTIONS)

    def test_every_generated_line_is_in_the_cpp_test(self):
        have = {line.strip() for line in self.logic_test.splitlines()}
        # 4 + 4 + 4 radial points, 3 of the reduction, 6 + 6 + 8 + 9 + 11 of the quads, and nine comment lines.
        self.assertEqual(len(self.lines), 12 + 3 + 40 + 9)
        self.assertEqual(sum(1 for line in self.lines if line.startswith("//")), 9)
        for line in self.lines:
            self.assertIn(line.strip(), have, "not in tests/native/test_clay_logic.cpp: run out of tools/xdk_sigmatch/post_pins.py")
        pin = next(line for line in self.lines if "0.13666137466812245" in line)
        self.assertNotIn(pin.replace("0.13666137466812245", "0.13666137466812246").strip(), have)

    def test_the_shaders_literals_are_the_captured_ones(self):
        # Single-precision, as the microcode's literal registers hold them (the reference's defaults are doubles).
        beam, glow = f32(lf.POST_BEAM_C255X), f32(lf.POST_GLOW_C255X)
        start, zero, rate, one = (f32(v) for v in lf.POST_CLOUD_OCCLUDER_C255)
        self.assertEqual((zero, one), (0.0, 1.0))
        self.assertIn("inline constexpr float kSunBeamScale = %rf;" % beam, self.logic)
        self.assertIn("inline constexpr float kSunGlowScale = %rf;" % glow, self.logic)
        self.assertIn("inline constexpr float kSunFadeStart = %rf, kSunFadeRate = %rf;" % (start, rate), self.logic)
        self.assertEqual((post_pins.SUN_BEAM_SCALE, post_pins.SUN_GLOW_SCALE, post_pins.SUN_OCCLUDER_C255), (beam, glow, (start, zero, rate, one)))
        quad = _one(r"#define FABLE2_CLAY_SUN_QUAD_PS_HLSL FABLE2_CLAY_SUN_QUAD_HLSL R\"hlsl\((.*?)\)hlsl\"", self.shaders, "the layer quads' text")
        self.assertIn("rgb = t.rgb * %r * u_reg[0].rgb;" % beam, quad)
        self.assertIn("rgb = t.rgb * %r * t.a;" % glow, quad)
        self.assertIn("(1.0 - saturate(%r * (length(i.world.xy - u_reg[2].xy) - %r)))" % (rate, start), quad)

    def test_the_layer_quads_are_sampled_where_the_reference_samples_them(self):
        # Ruling 38: the game's PA_SU_VTX_CNTL is 4 at the layer's draws (a pixel sampled at its integral
        # position): the reference's PIXEL_CENTRE 0, and natively a viewport origin of half a pixel.
        import post_sun_layer as sl
        self.assertEqual(sl.PIXEL_CENTRE, 0.0)
        origin = float(_one(r"inline constexpr float kSunQuadViewportOrigin = ([0-9.]+)f;", self.logic, "the origin"))
        self.assertEqual(origin, 0.5 - sl.PIXEL_CENTRE)
        self.assertIn("cmd->SetViewport({kSunQuadViewportOrigin, kSunQuadViewportOrigin, float(kSunLayerWidth), float(kSunLayerHeight), 0.0f, 1.0f});",
                      self.pass_cpp)
        # the four points of test case 651, by the reference's own screen rule on a 560 x 360 layer
        for ndc_x, ndc_y, x, y in ((-1.0, 1.0, 0.0, 0.0), (0.25, -0.5, 350.0, 270.0), (-0.2358, 0.2124, 213.976, 141.768), (1.0, -1.0, 560.0, 360.0)):
            sx, sy = sl._screen((ndc_x, ndc_y, 0.5, 1.0), 560, 360)[:2]
            self.assertAlmostEqual(sx, x, places=9)
            self.assertAlmostEqual(sy, y, places=9)
            self.assertIn("{%s, %s, %s, %s}" % (ndc_x, ndc_y, x, y), self.logic_test)

    def test_the_radial_blur_is_the_references(self):
        radial = _one(r"inline constexpr const char\* kClaySunRadialPs = FABLE2_CLAY_POST_HLSL R\"hlsl\((.*?)\)hlsl\";", self.shaders,
                      "the radial blur's text")
        taps = lf.post_radial_taps(0.25, 0.75, (0.5, 0.5), 0.87, 0.935)
        self.assertEqual(len(taps), 14)
        self.assertIn("for (int k = 0; k < %d; ++k) {" % len(taps), radial)
        self.assertEqual(int(_one(r"inline constexpr uint32_t kSunRadialTaps = (\d+);", self.logic, "the taps")), len(taps))
        self.assertEqual(taps[0], ((0.25, 0.75), 0.5))
        self.assertIn("float w = 0.5;", radial)
        self.assertEqual(_one(r"inline constexpr double kSunRadialNorm = ([^;]+);", self.logic, "the norm"), "4.0 / 7.0")
        self.assertEqual(radial.count("(4.0 / 7.0)"), 3)
        # The capture's bounds, which the pass judges again and the exporter applies.
        self.assertIn("std::fabs(radial[0]) <= 4.0f && std::fabs(radial[1]) <= 4.0f && radial[2] > 0.0f && radial[2] <= 1.0f &&", self.logic)

    def test_the_levels_and_the_sizes(self):
        self.assertIn("inline constexpr uint32_t kSunLayerDivisor = 2, kSunSmallDivisor = 4;", self.logic)
        self.assertIn("inline constexpr uint32_t kSunLevels = 1023;", self.logic)
        self.assertEqual(round(post_replay.q10(0.5) * 1023), 512)
        self.assertIn("constexpr nrhi::Format kSunFormat = nrhi::Format::kR10G10B10A2_UNORM;", self.pass_cpp)
        # The layer's targets are fractions of the logical size, not numbers.
        ensure = _one(r"bool ClayPass::EnsureSunObjects\(nrhi::Device\* dev\) \{(.*?)\n\}\n", self.pass_cpp, "EnsureSunObjects")
        for number in ("1120", "720", "560", "360", "280", "180"):
            self.assertNotIn(number, ensure)

    def test_the_feeds_quad_record_is_the_exporters(self):
        import post_sun_layer
        self.assertEqual(int(_one(r"static_assert\(kOracleQuadBytes == (\d+)\);", self.oracle, "the record's size")), post_sun_layer.QUAD_FILE_BYTES)
        self.assertIn('std::memcmp(data, "F2OU", 4)', self.oracle)
        self.assertEqual(sorted(post_sun_layer.QUAD_FILE_ROLES), sorted(s.kind for s in lf.POST_QUAD_SHADERS if s.where != "scene"))


def task5_lines():
    return section_lines(TASK5_SECTIONS)


class PostLiterals(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.block = _text("src/native/capture/post_block.h")
        cls.logic_test = _text("tests/native/test_clay_logic.cpp")
        cls.shaders = _text("src/native/fable2_native_shaders.h")
        cls.table_inc = _text("tests/native/post_pins_table.inc")
        cls.lines = task5_lines()
        cls.bloom_lines = section_lines(TASK8_SECTIONS)
        cls.pass_cpp = _text("src/native/render/clay_pass.cpp")
        cls.render_cpp = _text("src/native/fable2_native_render.cpp")
        cls.post_block = _one(r"#define FABLE2_CLAY_POST_HLSL R\"hlsl\((.*?)\)hlsl\"", cls.shaders, "the post steps' shared text")
        cls.downsample = _one(r"inline constexpr const char\* kClayPostDownsamplePs = FABLE2_CLAY_POST_HLSL R\"hlsl\((.*?)\)hlsl\";",
                              cls.shaders, "the downsample's text")
        cls.bright = _one(r"inline constexpr const char\* kClayPostBrightPs = FABLE2_CLAY_POST_HLSL R\"hlsl\((.*?)\)hlsl\";", cls.shaders,
                          "the bright pass's text")
        cls.blur = _one(r"inline constexpr const char\* kClayPostBlurPs = FABLE2_CLAY_POST_HLSL R\"hlsl\((.*?)\)hlsl\";", cls.shaders,
                        "the blur's text")
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
                          r")\)\); \}", self.post_block, "post_lum")
        self.assertEqual(tuple(float(v) for v in shader_lum), lf.POST_LUM)
        scene_max = float(_one(r"inline constexpr float kPostSceneMax = (" + NUMBER + r")f;", self.logic, "kPostSceneMax"))
        self.assertEqual(scene_max, lf.POST_SCENE_MAX)
        # The rule of a scene channel: not above 0 is 0, above the clamp is the clamp; in the reference, the
        # mirror and the shader (which takes the clamp from the block).
        for v, want in ((float("nan"), 0.0), (-1.0, 0.0), (0.0, 0.0), (2.5, 2.5), (40.0, 31.875), (float("inf"), 31.875)):
            self.assertEqual(lf.post_scene(v), want)
        _one(r"float3 post_scene\(float3 v\) \{ return \(v > 0\.0\) \? min\(v, p_scene_max\) : float3\(0\.0, 0\.0, 0\.0\); \}",
             self.post_block, "post_scene")
        # (one definition for every step: the composite, the downsample and the bright pass read the scene by it)
        for text in (self.composite, self.ramp, self.downsample, self.bright, self.blur):
            self.assertNotIn("float3 post_scene(", text)
            self.assertNotIn("0.2125", text)
        _one(r"if \(!\(v > 0\.0f\)\) return 0\.0;\n  return double\(v < scene_max \? v : scene_max\);", self.logic, "PostScene")
        _one(r"k\.scene_max = kPostSceneMax;", self.logic, "the block's clamp")

    def test_the_composites_terms(self):
        # The shader's statements are the reference's terms, in its order.
        body = _one(r"float3 PostComposite\(float3 scene, float3 bloom\) \{\n(.*?)\n\}", self.composite, "PostComposite")
        self.assertEqual([line.strip() for line in body.split("\n")], [
            "float3 s = scene;",
            "[branch] if (p_pad == 0) s = post_scene(scene);",
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
        _one(r"float3 mapped = float3\(p_ramp\[post_ramp_index\(v\.r\)\]\.r, p_ramp\[post_ramp_index\(v\.g\)\]\.g, "
             r"p_ramp\[post_ramp_index\(v\.b\)\]\.b\);\n  return float4\(post_level\(saturate\(mapped\)\), 1\.0\);", self.ramp,
             "the ramp's read")

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

    # --- plan Task 8: the bloom, and the writes' own rounding (controller ruling 25) ---

    def test_every_generated_bloom_line_is_in_the_cpp_test(self):
        # 8 bright points for each register set, 8 downsample pixels (two conventions), 4 pixels for each of the
        # four blurs, and the seven comment lines that name them.
        have = {line.strip() for line in self.logic_test.splitlines()}
        self.assertEqual(len(self.bloom_lines), 8 + 8 + 8 + 4 * 4 + 7)
        self.assertEqual(sum(1 for line in self.bloom_lines if line.startswith("//")), 7)
        for line in self.bloom_lines:
            self.assertIn(line.strip(), have, "not in tests/native/test_clay_logic.cpp: run tools/xdk_sigmatch/post_pins.py")
        pin = next(line for line in self.bloom_lines if "0.09379039276015376" in line)
        self.assertNotIn(pin.replace("0.09379039276015376", "0.09379039276015377").strip(), have)
        # The blurs' weights of the two sets and the pins' pictures, as the C++ test makes them.
        for name, regs in post_pins.SETS:
            rows = ",\n".join(" " * 37 + post_pins.floats(w) for w in regs["blur"]).strip()
            self.assertIn("const float %s_blur[2][7] = {%s};" % (name, rows), self.logic_test)
        self.assertIn("scene_px[(y * 16 + x) * 3 + c] = float((7 * x + 13 * y + 5 * c) % 32) / 8.0f;", self.logic_test)
        self.assertIn("unorm_px[(y * 16 + x) * 3 + c] = float(double((37 * x + 101 * y + 59 * c) % 1024) / 1023.0);", self.logic_test)
        self.assertEqual((post_pins.SCENE_W, post_pins.SCENE_H, post_pins.UNORM_W, post_pins.UNORM_H), (16, 8, 16, 16))
        self.assertEqual(post_pins.scene_texel(3, 2), tuple(((7 * 3 + 13 * 2 + 5 * c) % 32) / 8.0 for c in range(3)))
        self.assertEqual(post_pins.unorm_texel(5, 9), tuple(f32(((37 * 5 + 101 * 9 + 59 * c) % 1024) / 1023.0) for c in range(3)))
        for (x, y), want in (((0, 0), "kNaN, 1.0f, 1.0f"), ((2, 0), "kInf, 1.0f, 1.0f"), ((0, 2), "-1.0f, 1.0f, 1.0f"),
                             ((10, 6), "40.0f, -kInf, kNaN")):
            self.assertIn("set_scene(%d, %d, %s);" % (x, y, want), self.logic_test)
            self.assertEqual(post_pins.floats(post_pins.scene_texel(x, y)), "{" + want + "}")

    def test_the_level_a_target_stores(self):
        # Ruling 25: every step into a UNORM target writes the rounded level itself, floor(v * levels + 0.5) /
        # levels, the level count from the target's format. The reference rounds the same way: the 8-bit level is
        # post_ramp_index (to_bytes), the 10-bit one post_replay.q10.
        levels8, levels10 = _one(r"inline constexpr uint32_t kPostFrontLevels = (\d+), kPostBloomLevels = (\d+);", self.logic, "the level counts")
        self.assertEqual((int(levels8), int(levels10)), (255, 1023))
        _one(r"float3 post_level\(float3 v\) \{ return floor\(v \* s_levels \+ 0\.5\) / s_levels; \}", self.post_block, "post_level")
        _one(r"const float x = \(v < 1\.0f \? v : 1\.0f\) \* float\(levels\) \+ 0\.5f;", self.logic, "PostLevelIndex")
        _one(r"inline float PostLevel\(float v, uint32_t levels\) \{ return levels \? float\(PostLevelIndex\(v, levels\)\) / float\(levels\) : 0\.0f; \}",
             self.logic, "PostLevel")

        def level(v, n):                                        # the mirror's rule, in Python
            return min(n, int(math.floor(min(max(v, 0.0), 1.0) * n + 0.5))) if v > 0.0 else 0
        for v in (0.0, 0.2, 0.5 / 255.0 - 1e-6, 0.5 / 255.0 + 1e-6, 100.49 / 255.0, 100.51 / 255.0, 0.999, 1.0, 1.7, -0.3):
            self.assertEqual(level(v, 255), lf.post_ramp_index(v))
            self.assertEqual(level(v, 1023) / 1023.0, post_replay.q10(v))
        for v in (0.31, 0.777, 0.0004):                         # (away from a tie: Python's round is to even there)
            self.assertEqual(post_replay.q10(v), math.floor(v * 1023.0 + 0.5) / 1023.0)
        # The level count is the format's, in one place (review of plan Task 6, Minor 2): no step's text holds 255
        # or 1023 for the rounding, and the pass derives its three counts from its formats.
        for text in (self.composite, self.downsample, self.bright, self.blur, self.post_block.split("cbuffer Step")[0]):
            self.assertNotIn("255", text)
        for text in (self.composite, self.ramp, self.downsample, self.bright, self.blur, self.post_block.split("cbuffer Step")[0]):
            self.assertNotIn("1023", text)
        self.assertEqual(self.ramp.count("255"), 2)             # (the ramp's index: its entry count, not a level)
        _one(r"return f == nrhi::Format::kR8G8B8A8_UNORM \? 255u : f == nrhi::Format::kR10G10B10A2_UNORM \? 1023u : 0u;", self.pass_cpp,
             "UnormLevels")
        _one(r"constexpr uint32_t kFrontLevels = UnormLevels\(kFrontFormat\), kMappedLevels = UnormLevels\(kMappedFormat\), "
             r"kBloomLevels = UnormLevels\(kBloomFormat\);", self.pass_cpp, "the pass's level counts")
        _one(r"static_assert\(kFrontLevels == kPostFrontLevels && kMappedLevels == kPostFrontLevels && kBloomLevels == kPostBloomLevels",
             self.pass_cpp, "the counts against the mirrors'")
        for step, levels in (("kComposite, kWidth, kHeight, kFrontLevels", 1), ("kRamp, kWidth, kHeight, kMappedLevels", 1),
                             ("kBright, kBloomWidth, kBloomHeight, kBloomLevels", 1), ("kBlurX, kBloomWidth, kBloomHeight, kBloomLevels", 1),
                             ("kBlurY, kBloomWidth, kBloomHeight, kBloomLevels", 1),
                             ("kDownsample, kWidth, kHeight, UnormLevels(kBloomDownFormat)", 1)):
            self.assertEqual(self.pass_cpp.count("MakeClayPostStep(ClayPostStep::" + step + ")"), levels, step)
        # Every step into a UNORM target ends in the rounding; the downsample (a float target) has none.
        for text in (self.composite, self.ramp, self.bright, self.blur):
            self.assertEqual(text.count("  return float4(post_level("), 1)
        self.assertNotIn("post_level", self.downsample)

    def test_the_blooms_targets(self):
        # Ruling 6: a quarter of the LOGICAL size; spec ruling 14: the game's own 10 bits for the bright pass and
        # the blurs. No size is a literal: of the pass, of a shader.
        self.assertEqual(int(_one(r"inline constexpr uint32_t kPostBloomDivisor = (\d+);", self.logic, "the divisor")), 4)
        _one(r"constexpr nrhi::Format kBloomDownFormat = nrhi::Format::kR11G11B10_FLOAT;", self.pass_cpp, "the downsample's format")
        _one(r"constexpr nrhi::Format kBloomFormat = nrhi::Format::kR10G10B10A2_UNORM;", self.pass_cpp, "the bloom's format")
        _one(r"static_assert\(ClayPass::kWidth % kPostBloomDivisor == 0 && ClayPass::kHeight % kPostBloomDivisor == 0\);", self.pass_cpp,
             "the exact division")
        _one(r"constexpr uint32_t kBloomWidth = ClayPass::kWidth / kPostBloomDivisor, kBloomHeight = ClayPass::kHeight / kPostBloomDivisor;",
             self.pass_cpp, "the bloom's size")
        for size in ("280", "180"):
            self.assertNotIn(size, self.pass_cpp)
        for text in (self.composite, self.ramp, self.downsample, self.bright, self.blur, self.post_block):
            for size in ("1120", "720", "280", "180"):
                self.assertNotIn(size, text)
        # The switch is off by default (spec ruling 16).
        _one(r'REXCVAR_DEFINE_STRING\(fable2_native_bloom, "off", "Fable2",', self.render_cpp, "the bloom's switch")
        _one(r"post\.bloom = CurrentBloom\(\);", self.render_cpp, "the switch in the request")

    def test_the_downsample(self):
        # The reference's four fetches in its order, P = 4 * (pixel + bias); the bias is the pixel's integral position.
        bias = float(_one(r"inline constexpr float kPostDownsampleBias = (" + NUMBER + r")f;", self.logic, "kPostDownsampleBias"))
        self.assertEqual(bias, 0.0)
        factor = float(_one(r"float2 p = (" + NUMBER + r") \* \(floor\(pos\.xy\) \+ s_offset\);", self.downsample, "the downsample's position"))
        self.assertEqual(factor, 4.0)
        taps = re.findall(r"post_tap\(p, float2\((" + NUMBER + r"), (" + NUMBER + r")\)\)", self.downsample)
        self.assertEqual([(float(a), float(b)) for a, b in taps], [(2.5, 2.5), (2.5, 0.5), (0.5, 0.5), (0.5, 2.5)])
        mirror = _one(r"static constexpr double kTaps\[4\]\[2\] = \{(.*?)\};", self.logic, "the mirror's taps")
        self.assertEqual([float(v) for v in re.findall(NUMBER, mirror)], [2.5, 2.5, 2.5, 0.5, 0.5, 0.5, 0.5, 2.5])
        _one(r"return float4\((" + NUMBER + r") \* sum, 1\.0\);", self.downsample, "the quarter")
        _one(r"const double px = 4\.0 \* \(double\(x\) \+ b\), py = 4\.0 \* \(double\(y\) \+ b\);", self.logic, "the mirror's position")
        # The reference on a hand case: with bias 0 the four fetches are the texels (0, 0), (2, 0), (0, 2), (2, 2)
        # of the pixel's block; a value that is no number counts as 0 and one above the clamp as the clamp.
        rows = [[(float(10 * y + x),) for x in range(8)] for y in range(8)]
        tex = lf.post_texture(rows, linear=True)
        self.assertEqual(lf.post_downsample(tex, 1, 1, bias), [31.875])     # (44, 46, 64, 66: each at the clamp)
        rows = [[(float(y + x),) for x in range(8)] for y in range(8)]
        self.assertEqual(lf.post_downsample(lf.post_texture(rows, linear=True), 1, 1, bias), [0.25 * (8.0 + 10.0 + 10.0 + 12.0)])
        # The step's offset word is that bias.
        _one(r"s\.offset = step == ClayPostStep::kDownsample \? kPostDownsampleBias\n", self.logic, "the step's offset")

    def test_the_bright_pass(self):
        body = _one(r"float4 main\(float4 pos : SV_Position\) : SV_Target \{\n(.*?)\n\}", self.bright, "the bright pass")
        self.assertEqual([line.strip() for line in body.split("\n")], [
            "int3 at = int3(pos.xy, 0);",
            "float3 d = post_scene(down_target.Load(at).rgb);",
            "float L = post_lum(d);",
            "float gain = (L > 0.0) ? saturate(L * p_b77y - p_b77x) / L * p_b31x : 0.0;",
            "float3 v = d * gain;",
            "if (p_sun_on != 0) v += sun_tf1.Load(at).rgb * 6.0 * p_b46x + sun_tf2.Load(at).rgb;",
            "return float4(post_level(post_unit(v / p_b47x)), 1.0);",
        ])
        _one(r"float p_b31x; float p_b46x; float p_b47x; float p_b77x; +// the bright pass EE92: c31\.x, c46\.x, c47\.x, c77\.x", self.shaders,
             "cbuffer Post's third row")
        _one(r"float bright\[5\];      // EE92: c31\.x, c46\.x, c47\.x, c77\.x, c77\.y", self.block, "PostBlock::bright")
        _one(r"const double c31x = k\.bright\[0\], c46x = k\.bright\[1\], c47x = k\.bright\[2\], c77x = k\.bright\[3\], c77y = k\.bright\[4\];",
             self.logic, "the mirror's registers")
        _one(r"const double gain = L > 0\.0 \? sat\(L \* c77y - c77x\) / L \* c31x : 0\.0;", self.logic, "the mirror's gain")
        _one(r"\(s\[i\] \* gain \+ fin\(tf1\[i\]\) \* 6\.0 \* c46x \+ fin\(tf2\[i\]\)\) / c47x", self.logic, "the mirror's layers")
        _one(r"float3 post_unit\(float3 v\) \{ return \(v > 0\.0\) \? min\(v, 1\.0\) : float3\(0\.0, 0\.0, 0\.0\); \}", self.post_block,
             "post_unit")
        # The reference on hand cases: the part of the luminance above the threshold, in the pixel's colour; L = 0.
        out = lf.post_bright([2.0, 2.0, 2.0], [0.0] * 3, [0.0] * 3, 1.5, 0.2, 2.0, (0.25, 0.5))
        for v in out:
            self.assertAlmostEqual(v, 2.0 * (2.0 * 0.5 - 0.25) / 2.0 * 1.5 / 2.0, places=12)
        self.assertEqual(lf.post_bright([0.0] * 3, [0.0] * 3, [0.0] * 3, 1.5, 0.2, 2.0, (0.25, 0.5)), [0.0, 0.0, 0.0])
        self.assertEqual(lf.post_bright([0.0] * 3, [0.1] * 3, [0.05] * 3, 1.5, 0.5, 2.0, (0.25, 0.5)), [(0.1 * 6.0 * 0.5 + 0.05) / 2.0] * 3)

    def test_the_blur(self):
        # 13 taps at -6..6 along the step's direction, the weight by the distance: the centre first.
        _one(r"return w \* blur_source\.Load\(int3\(clamp\(at \+ k \* s_dir, int2\(0, 0\), s_last\), 0\)\)\.rgb;", self.blur, "a tap")
        taps = re.findall(r"post_blur_tap\(at, (-?\d+), (w[01])\.([xyzw])\)", self.blur)
        self.assertEqual(sorted(int(k) for k, _, _ in taps), list(range(-6, 7)))
        place = {0: ("w0", "x"), 1: ("w0", "y"), 2: ("w0", "z"), 3: ("w0", "w"), 4: ("w1", "x"), 5: ("w1", "y"), 6: ("w1", "z")}
        for k, row, component in taps:
            self.assertEqual((row, component), place[abs(int(k))])
        _one(r"float4 w0 = vertical \? p_blur\[2\] : p_blur\[0\];\n  float4 w1 = vertical \? p_blur\[3\] : p_blur\[1\];", self.blur,
             "the weights' rows")
        _one(r"k\.blur\[2 \* s \+ \(i >> 2\)\]\[i & 3\] = \(w >= 0\.0f && w <= 1\.0f\) \? w : 0\.0f;", self.logic, "the constants' rows")
        _one(r"const double w = fin\(k\.blur\[row \+ \(a >> 2\)\]\[a & 3\]\);", self.logic, "the mirror's weight")
        _one(r"for \(int tap = -6; tap <= 6; \+\+tap\) \{", self.logic, "the mirror's taps")
        _one(r"float blur\[2\]\[7\];     // EA83 then 28DB: c46\.x \(centre\), c46\.y, \.z, \.w, c47\.x, \.y, \.z", self.block, "PostBlock::blur")
        # The reference on a hand case: one bright texel spreads by the weights, along its own axis only.
        rows = [[(1.0,) if (x, y) == (8, 8) else (0.0,) for x in range(17)] for y in range(17)]
        weights = [0.4, 0.2, 0.05, 0.02, 0.015, 0.01, 0.005]
        tex = lf.post_texture(rows)
        for k in range(-6, 7):
            self.assertEqual(lf.post_blur13(tex, 8 + k, 8, weights, False), [weights[abs(k)]])
            self.assertEqual(lf.post_blur13(tex, 8, 8 + k, weights, True), [weights[abs(k)]])
        self.assertEqual(lf.post_blur13(tex, 8, 9, weights, False), [0.0])

    def test_where_the_composite_samples_the_bloom(self):
        # The composite's own fetch (37B1 instruction 6): the scene's interpolated coordinate plus half a bloom
        # texel: bloom texel coordinate x / 4 + 0.5. The shader makes it of the step's words, the mirror by
        # PostBloomCoordinate, the reference tool by bloom_sampler(place="fetch"), its default.
        fetch = float(_one(r"inline constexpr float kPostBloomFetch = (" + NUMBER + r")f;", self.logic, "kPostBloomFetch"))
        self.assertEqual(fetch, 0.5)
        _one(r"constexpr double PostBloomCoordinate\(uint32_t x\) \{ return double\(x\) / double\(kPostBloomDivisor\) \+ double\(kPostBloomFetch\); \}",
             self.logic, "PostBloomCoordinate")
        _one(r": step == ClayPostStep::kComposite \? kPostBloomFetch \* float\(kPostBloomDivisor\)\n", self.logic, "the composite's offset word")
        _one(r"if \(p_bloom_on != 0\) bloom = bloom_target\.SampleLevel\(linear_clamp, \(floor\(pos\.xy\) \+ s_offset\) \* s_texel, 0\.0\)\.rgb;",
             self.composite, "the composite's sample")
        self.assertEqual(post_replay.BLOOM_PLACE, "fetch")
        rows = [[(float(x), float(y)) for x in range(280 // 4)] for y in range(180 // 4)]
        at = post_replay.bloom_sampler(70, 45, rows, 280, 180, place="fetch")
        for x, y in ((0, 0), (1, 3), (2, 2), (4, 8), (130, 77), (279, 179)):
            want = (min(max(x / 4.0 + fetch - 0.5, 0.0), 69.0), min(max(y / 4.0 + fetch - 0.5, 0.0), 44.0))
            got = at(x, y)
            self.assertAlmostEqual(got[0], want[0], delta=1e-12)
            self.assertAlmostEqual(got[1], want[1], delta=1e-12)
            # (the shader's coordinate: (x + offset) * texel of the scene, in bloom texels)
            self.assertAlmostEqual((x + fetch * 4.0) / 280.0 * 70.0, x / 4.0 + fetch, delta=1e-12)

    def test_the_steps_words(self):
        table = _one(r"inline constexpr ClayBufferField kClayPostStepFields\[\] = \{(.*?)\n\};", self.shaders, "kClayPostStepFields")
        fields = re.findall(r'\{"(s_\w+)", (\d+), (\d+)\}', table)
        self.assertEqual(fields, [("s_texel", "0", "8"), ("s_levels", "8", "4"), ("s_offset", "12", "4"), ("s_dir", "16", "8"),
                                  ("s_last", "24", "8")])
        self.assertEqual(int(_one(r"inline constexpr unsigned kClayPostStepSize = (\d+);", self.shaders, "kClayPostStepSize")), 32)
        _one(r"cbuffer Step : register\(b1\) \{\n  float2 s_texel; .*?\n  float s_levels; .*?\n  float s_offset; .*?\n  int2 s_dir; .*?\n"
             r"  int2 s_last; .*?\n\};", self.post_block, "cbuffer Step")
        _one(r"static_assert\(sizeof\(ClayPostStepConstants\) == 32\);", self.logic, "the words' size")
        _one(r"static_assert\(sizeof\(ClayPostStepConstants\) == kPostStepWords \* 4\);", self.pass_cpp, "the layout's words")

    # --- plan Task 13: the scene's furniture (the moon and the glare from the quad records) ---

    def test_every_generated_furniture_line_is_in_the_cpp_test(self):
        # 11 moon points, 6 glare points, 4 corners through each of two matrices, and the four comment lines.
        lines = section_lines(TASK13_SECTIONS)
        have = {line.strip() for line in self.logic_test.splitlines()}
        self.assertEqual(len(lines), 11 + 6 + 4 + 4 + 4)
        self.assertEqual(sum(1 for line in lines if line.startswith("//")), 4)
        for line in lines:
            self.assertIn(line.strip(), have, "not in tests/native/test_clay_logic.cpp: run tools/xdk_sigmatch/post_pins.py")
        pin = next(line for line in lines if "1.0634917315560166" in line)
        self.assertNotIn(pin.replace("1.0634917315560166", "1.0634917315560167").strip(), have)
        # The captured matrix and the made one, as the C++ test holds them.
        for b in post_pins.QUAD_MATRIX_BITS:
            self.assertIn("0x%08X" % b, self.logic_test)
        made = post_pins.QUAD_MATRIX_MADE
        self.assertIn("const float made[4][4] = {%s};" % ", ".join(
            "{" + ", ".join("%rf" % v for v in made[4 * i:4 * i + 4]) + "}" for i in range(4)), self.logic_test)

    def test_the_furnitures_formulas(self):
        quad_ps = _one(r"inline constexpr const char\* kClayQuadPs = FABLE2_CLAY_QUAD_HLSL R\"hlsl\((.*?)\)hlsl\";", self.shaders,
                       "the quad step's pixel shader")
        quad_vs = _one(r"inline constexpr const char\* kClayQuadVs = FABLE2_CLAY_QUAD_HLSL R\"hlsl\((.*?)\)hlsl\";", self.shaders,
                       "the quad step's vertex shader")
        record = _text("src/native/capture/quad_record.h")
        # The kinds' numbers are the list's places plus one, and their registers come in the list's order: the
        # moon's c20 then c31 (q_reg[0], q_reg[1]), the glare's c31 (q_reg[0]).
        kinds = [q.kind for q in lf.POST_QUAD_SHADERS]
        moon, glare = lf.POST_QUAD_SHADERS[kinds.index("moon")], lf.POST_QUAD_SHADERS[kinds.index("glare")]
        self.assertEqual((kinds.index("moon") + 1, kinds.index("glare") + 1), (1, 3))
        self.assertEqual((moon.registers, glare.registers), ((20, 31), (31,)))
        self.assertIn("  if (q_kind == 1) {\n    rgb = t.rgb * q_reg[1].rgb * t.a * q_reg[0].w;\n    a = q_reg[1].a * t.a;\n"
                      "  } else if (q_kind == 3) {\n    rgb = t.rgb * q_reg[0].rgb;\n    a = 1.0;\n  }\n", quad_ps)
        self.assertEqual((moon.blend, moon.test, moon.where, glare.blend, glare.test, glare.where),
                         ("alpha", True, "scene", "add", False, "scene"))
        # The mirrors: the same products, the same output rule, the scene's clamp.
        _one(r"for \(int i = 0; i < 3; \+\+i\) rgb\[i\] = detail::QuadFinite\(t\[i\]\) \* double\(c31\[i\]\) \* a \* double\(c20w\);",
             self.logic, "the moon's mirror")
        _one(r"detail::QuadOut\(rgb, double\(c31\[3\]\) \* a, double\(kPostSceneMax\), out\);", self.logic, "the moon's alpha")
        _one(r"for \(int i = 0; i < 3; \+\+i\) rgb\[i\] = detail::QuadFinite\(t\[i\]\) \* double\(c31\[i\]\);", self.logic, "the glare's mirror")
        _one(r"for \(int i = 0; i < 3; \+\+i\) out\[i\] = rgb\[i\] > 0\.0 \? std::min\(rgb\[i\], top\) : 0\.0;", self.logic, "the output rule")
        self.assertIn("  rgb = (rgb > 0.0) ? min(rgb, q_scene_max) : float3(0.0, 0.0, 0.0);\n  return float4(rgb, saturate(a));\n", quad_ps)
        _one(r"  c\.scene_max = kPostSceneMax;\n  \*out = c;", self.logic, "the quad's clamp")
        self.assertEqual(float(_one(r"inline constexpr float kPostSceneMax = (" + NUMBER + r")f;", self.logic, "kPostSceneMax")),
                         lf.POST_SCENE_MAX)
        self.assertEqual(lf.post_moon([1.0, 1.0, 1.0, 1.0], [8.0, 1.0, 1.0, 1.0], 8.0)[0], lf.POST_SCENE_MAX)
        # The alpha test is the draw's: on for the moon in every captured row, "greater" (function 4).
        self.assertIn("  if (q_alpha_test != 0 && !(a > q_alpha_ref)) discard;\n", quad_ps)
        _one(r"\(q\.alpha_control & 7\) != uint32_t\(capture::Compare::kGreater\)\) return ClayQuadState::kNone;", self.logic,
             "the alpha function the pass draws")
        # The six vertices are the builder's six indices, in the capture's record and in the shader.
        order = ", ".join(str(i) for i in lf.POST_QUAD_INDICES)
        self.assertIn("static const uint order[6] = {%s};" % order, quad_vs)
        self.assertIn("inline constexpr uint16_t kQuadIndices[6] = {%s};" % order, record)
        # The corner's position: the four rows against (x, y, z, 1).
        self.assertIn("float4 p = float4(q_corner[c].xyz, 1.0);", quad_vs)
        self.assertEqual(lf.post_quad_position((2.0, 3.0, 4.0), (1, 0, 0, 5), (0, 1, 0, 6), (0, 0, 1, 7), (0, 0, 0, 1)),
                         [7.0, 9.0, 11.0, 1.0])
        # The blend words of the two states are the list's.
        _one(r"inline constexpr uint32_t kQuadBlendAlpha = 0x07060706u, kQuadBlendAdd = 0x01010101u;", self.logic, "the blend words")
        self.assertIn("b == PostQuadBlend::kAlpha ? 0x07060706u : b == PostQuadBlend::kAdd ? 0x01010101u : 0x00010001u;", record)
        # The quad's block: two small slots a quad, 48 a frame; no size of a target in the step's text.
        self.assertEqual(int(_one(r"inline constexpr unsigned kClayQuadSize = (\d+);", self.shaders, "kClayQuadSize")), 416)
        _one(r"static_assert\(kClayQuadSlots == 2 && kClayQuadFrameSlots == 48\);", self.logic, "the quads' slots")
        for text in (quad_ps, quad_vs):
            for size in ("1120", "720", "560", "360"):
                self.assertNotIn(size, text)


if __name__ == "__main__":
    unittest.main()
