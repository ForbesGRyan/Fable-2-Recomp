"""The wind's names and numbers are written in several places: the Python reference
(tools/xdk_sigmatch/light_formulas.py: wind_plant, wind_tree_terms, wind_tree_offset, wind_leaf_lift, wind_leaf and
WIND_LITERAL_ORDER), the generator of the pins (tools/xdk_sigmatch/wind_pins.py) and its tracked output
(tests/native/wind_pins.inc), the renderer's pure mirrors (src/native/render/clay_logic.h, "Native wind"), the wind
vertex program (src/native/fable2_native_shaders.h, the text under CLAY_WIND) and the pass
(src/native/render/clay_pass.cpp). This test reads the C++ and HLSL texts and holds them to the reference, as
tests/test_post_literals.py does for the post chain (the foliage sub-project, plan Task 10).

What no pin can hold is held here by NAME: a literal is read by its slot, the slot is its place in the family's order,
and two literals of one value (flutter_y and one are both 1.0; half is 0.5 in both families at different slots) would
let a wrong index pass every numeric check."""
import inspect
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import wind_pins  # noqa: E402

NUMBER = r"(?<![\w.\[])[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?(?![\w.\]])"
TREE = lf.WIND_LITERAL_ORDER["leaf"]
TRUNK = lf.WIND_LITERAL_ORDER["trunk"]
PLANT = lf.WIND_LITERAL_ORDER["plant"]


def _text(relative):
    return (ROOT / relative).read_bytes().decode("utf-8").replace("\r\n", "\n")


def _one(pattern, text, what):
    found = re.findall(pattern, text, re.S)
    if len(found) != 1:
        raise AssertionError(f"{what}: {len(found)} matches of {pattern!r}")
    return found[0]


def _code(text):
    """Without // comments."""
    return "\n".join(line.split("//")[0] for line in text.split("\n"))


def hlsl_function(shaders, name):
    """The body of an HLSL function of the wind's text, without comments."""
    head = _one(r"\n((?:float|float3) " + name + r"\([^)]*\) \{[^\n]*)\n", shaders, name)
    if head.endswith("}"):      # a function of one line
        return _code(head[head.index("{") + 1:-1])
    at = shaders.index("\n" + head + "\n") + len(head) + 2
    return _code(shaders[at:shaders.index("\n}\n", at)])


def cpp_function(logic, signature):
    """The body of an inline function of clay_logic.h, without comments."""
    return _code(_one(r"\ninline [^\n]*\b" + signature + r"\([^{;]*\) \{\n(.*?)\n\}\n", logic, signature))


def pass_function(source, name):
    return _one(r"\n[^\n]*ClayPass::" + name + r"\([^{;]*\) \{\n(.*?)\n\}\n", source, name)


def reference_names(*functions):
    names = set()
    for f in functions:
        names |= set(re.findall(r'lit\["(\w+)"\]', inspect.getsource(f)))
    return names


class Slots(unittest.TestCase):
    def setUp(self):
        self.shaders = _text("src/native/fable2_native_shaders.h")
        self.logic = _text("src/native/render/clay_logic.h")

    def test_the_orders_are_the_families(self):
        self.assertEqual(len(TREE), 28)
        self.assertEqual(TREE[:18], TRUNK)
        self.assertEqual(len(PLANT), 7)

    def test_the_hlsl_names_are_the_order(self):
        """#define W_<name> o_wind_lit[r].c is slot 4 r + c of a tree's order, P_<name> of a plant's; every name once."""
        for prefix, order in (("W", TREE), ("P", PLANT)):
            found = re.findall(r"#define " + prefix + r"_(\w+) o_wind_lit\[(\d)\]\.([xyzw])\n", self.shaders)
            self.assertEqual([name for name, _r, _c in found], list(order), prefix)
            self.assertEqual([4 * int(r) + "xyzw".index(c) for _n, r, c in found], list(range(len(order))), prefix)
        # o_wind_lit is named nowhere else in code: the block's declaration and these lines
        code = _code(self.shaders)
        # (and the layout table's line for the reflection)
        self.assertEqual(code.count("o_wind_lit"), 28 + 7 + 1 + 1)
        self.assertRegex(code, r"#ifdef CLAY_WIND\n  float4 o_wind_reg\[3\];\s*\n(?:\s*\n)*  float4 o_wind_lit\[7\];\s*\n#endif\n\};")

    def test_the_mirrors_names_are_the_order(self):
        tree = _one(r"enum Tree : uint32_t \{(.*?)\};", self.logic, "wind_slot::Tree")
        plant = _one(r"enum Plant : uint32_t \{(.*?)\};", self.logic, "wind_slot::Plant")
        self.assertEqual([n.strip() for n in tree.split(",")], list(TREE) + ["kTreeCount"])
        self.assertEqual([n.strip() for n in plant.split(",")], ["p_" + n for n in PLANT] + ["kPlantCount"])
        # a slot is read as lit[slot / 4][slot % 4], in the mirrors and in the packing
        self.assertIn("return c.lit[slot / 4][slot % 4];", self.logic)
        self.assertIn("std::memcpy(&c.lit[k / 4][k % 4], &spec.lit_bits[k], sizeof(float));", self.logic)

    def test_each_function_reads_what_the_reference_reads(self):
        """The literals a function names are those its reference names (WIND_STRUCTURE's are read by none)."""
        plant = reference_names(lf.wind_plant)
        offset = reference_names(lf.wind_tree_terms, lf.wind_tree_offset, lf._wind_phase)
        leaf = reference_names(lf.wind_leaf_lift, lf.wind_leaf, lf._wind_phase)
        self.assertEqual(plant, set(PLANT) - {"unit"})
        self.assertEqual(offset, set(TRUNK) - {"zero", "sel_zero", "sel_one"})
        self.assertEqual(leaf - offset, set(TREE[18:]))
        self.assertFalse((plant | offset | leaf) & (set(lf.WIND_STRUCTURE) - {"one"}) - {"half"})

        def hlsl(prefix, *names):
            return set(re.findall(r"\b" + prefix + r"_(\w+)\b", "".join(hlsl_function(self.shaders, n) for n in names)))

        self.assertEqual(hlsl("P", "WindPlant"), plant)
        self.assertEqual(hlsl("W", "WindPlant"), set())
        self.assertEqual(hlsl("W", "WindTreeOffset", "WindPhase"), offset)
        self.assertEqual(hlsl("W", "WindLeaf", "WindPhase"), leaf)
        self.assertEqual(hlsl("P", "WindTreeOffset", "WindPhase", "WindLeaf", "WindTrunk"), set())

        def mirror(*names):
            return set(re.findall(r"\bLit\(c, (\w+)\)", "".join(cpp_function(self.logic, n) for n in names))) | \
                set(re.findall(r"\bLit\(c, wind_slot::(\w+)\)", "".join(cpp_function(self.logic, n) for n in names)))

        self.assertEqual(mirror("WindPlantMirror"), {"p_" + n for n in plant})
        self.assertEqual(mirror("WindTreeOffsetMirror", "TreePhase"), offset)
        self.assertEqual(mirror("WindLeafLiftMirror", "TreePhase"), leaf)
        self.assertEqual(mirror("WindLeafMirror", "WindTrunkMirror"), set())
        # the reach: wind_reach_tree's and wind_reach_leaf's names
        reach = set(re.findall(r"\blit\((\w+)\)", cpp_function(self.logic, "ClayWindReach")))
        self.assertEqual(reach, reference_names(lf.wind_reach_tree, lf.wind_reach_leaf))

    def test_the_program_names_its_literals_in_the_mirrors_order(self):
        """Term by term: the literal names a function of the vertex program reads, in the order it reads them, are
        those of its C++ mirror (which the pins hold to the reference's numbers). A set of names cannot see a
        literal taken for another in one term (W_one for W_half in the flutter's wrap: both are read elsewhere in
        the function), nor a term rewritten with one read fewer; the sequence does."""
        def hlsl(prefix, name):
            return re.findall(r"\b" + prefix + r"_(\w+)\b", hlsl_function(self.shaders, name))

        def mirror(name):
            return [n[2:] if n.startswith("p_") else n
                    for n in re.findall(r"\bLit\(c, (?:wind_slot::)?(\w+)\)", cpp_function(self.logic, name))]

        self.assertEqual(hlsl("W", "WindPhase"), mirror("TreePhase"))
        self.assertEqual(hlsl("W", "WindPhase"), ["half", "two_pi", "lo"])
        self.assertEqual(hlsl("P", "WindPlant"), mirror("WindPlantMirror"))
        self.assertEqual(len(hlsl("P", "WindPlant")), 6)
        offset = hlsl("W", "WindTreeOffset")
        self.assertEqual(offset, mirror("WindTreeOffsetMirror"))
        self.assertEqual(len(offset), 22)
        # A leaf: the program's one function is the mirror's two (the gust and the lift, then nothing more).
        leaf = hlsl("W", "WindLeaf")
        self.assertEqual(leaf, mirror("WindLeafLiftMirror") + mirror("WindLeafMirror"))
        self.assertEqual(len(leaf), 22)
        self.assertEqual(leaf[leaf.index("flutter_y") + 1:leaf.index("flutter_y") + 5], ["half", "half", "two_pi", "lo"])
        # ... and the reference's own order where it is written the same way (its `k` names the back gain first).
        ref = re.findall(r'lit\["(\w+)"\]', inspect.getsource(lf.wind_tree_terms).split('"""')[2] +
                         inspect.getsource(lf.wind_tree_offset).split('"""')[2])
        self.assertEqual(ref, offset)
        ref = re.findall(r'lit\["(\w+)"\]', inspect.getsource(lf.wind_leaf_lift).split('"""')[2])
        at = ref.index("back")
        self.assertEqual(ref[at:at + 3], ["back", "amp", "amp"])
        ref[at:at + 3] = ["amp", "back", "amp"]
        self.assertEqual(ref, leaf)

    def test_no_literal_of_the_table_is_hlsl_text(self):
        """The three functions' numbers are the reference's own (0 and the phase's 1); every other number comes from
        o_wind_lit by name."""
        allowed = {"WindFrac": [], "WindPhase": [], "WindPlant": ["1.0", "1.0"], "WindTreeOffset": [], "WindTrunk": [],
                   "WindLeaf": ["0.0", "0.0", "0.0"]}
        for name, numbers in allowed.items():
            self.assertEqual(sorted(re.findall(NUMBER, hlsl_function(self.shaders, name))), sorted(numbers), name)
        # the reference's own: wind_plant has the 1 twice, the leaf compares with 0
        self.assertEqual(inspect.getsource(lf.wind_plant).split('"""')[2].count("1.0"), 2)
        # (so no literal of the table but the two whose value is 0 or 1 could stand there as a number)
        others = {v for family in ("plant", "leaf") for v in lf.WIND_LITERALS[family].values()} - {0.0, 1.0}
        self.assertEqual(len(others), 24)

    def test_the_plants_phase_is_the_references(self):
        """theta = 1 + f - c10.x * c25.x * (1 + gain * f), in the reference's order, in all three texts."""
        self.assertIn('theta = 1.0 - c10x * c25x * (f * lit["gain"] + 1.0) + f', inspect.getsource(lf.wind_plant))
        self.assertIn("float theta = 1.0f - c10x * c25x * (f * Lit(c, p_gain) + 1.0f) + f;", cpp_function(self.logic, "WindPlantMirror"))
        self.assertIn("precise float theta = 1.0 - o_wind_reg[0].x * o_wind_reg[0].y * (f * P_gain + 1.0) + f;",
                      hlsl_function(self.shaders, "WindPlant"))


class Offsets(unittest.TestCase):
    def test_the_blocks_place(self):
        shaders = _text("src/native/fable2_native_shaders.h")
        logic = _text("src/native/render/clay_logic.h")
        self.assertIn("inline constexpr uint32_t kClayWindOffset = 288;", logic)
        self.assertIn('{"o_leaf", 272, 16},', shaders)
        self.assertIn('inline constexpr ClayBufferField kClayWindObjectFields[] = {{"o_wind_reg", 288, 48}, {"o_wind_lit", 336, 112}};', shaders)
        self.assertIn("inline constexpr unsigned kClayWindObjectSize = 448;", shaders)
        self.assertIn("inline constexpr uint32_t kClayObjectSlotBytes = 512;", logic)

    def test_the_family_bits(self):
        shaders = _text("src/native/fable2_native_shaders.h")
        logic = _text("src/native/render/clay_logic.h")
        self.assertIn("inline constexpr uint32_t kLightModeWindShift = 12, kLightModeWindMask = 3u << kLightModeWindShift;", logic)
        self.assertEqual(shaders.count("uint wind_family = (light_mode >> 12) & 3;"), 1)
        self.assertIn("enum class WindFamily : uint8_t { kNone = 0, kTrunk = 1, kLeaf = 2, kPlant = 3, kCount };",
                      _text("src/native/capture/wind_inputs.h"))
        # the program's three branches are the three families, the words a vertex what the cache builds
        for family, words in ((1, 4), (2, 10)):
            self.assertRegex(shaders, r"\[branch\] if \(wind_family == %d\) \{\n\s+uint at = uint\(v\) \* %d;" % (family, words))
        self.assertIn("      } else {\n        uint at = uint(v) * 2;\n        moved = WindPlant(p.xyz, wind_words[at], wind_words[at + 1]);", shaders)
        self.assertIn("return f == capture::WindFamily::kTrunk ? 4u : f == capture::WindFamily::kLeaf ? 10u : f == capture::WindFamily::kPlant ? 2u : 0u;",
                      _text("src/native/render/wind_decode.h"))
        # the words' order: a trunk's four weights; a leaf's pivot, weights, flutter
        self.assertIn("moved = WindTrunk(p.xyz, float4(wind_words[at], wind_words[at + 1], wind_words[at + 2], wind_words[at + 3]));", shaders)
        self.assertIn("moved = WindLeaf(p.xyz, float3(wind_words[at], wind_words[at + 1], wind_words[at + 2]),\n"
                      "                         float4(wind_words[at + 3], wind_words[at + 4], wind_words[at + 5], wind_words[at + 6]),\n"
                      "                         float3(wind_words[at + 7], wind_words[at + 8], wind_words[at + 9]));", shaders)

    def test_the_constants_packing_is_the_pins(self):
        """A plant's registers as ClayWindConstants packs them are what wind_pins.py's lines say."""
        logic = _text("src/native/render/clay_logic.h")
        self.assertIn("c.reg[0][0] = r[0][0]; c.reg[0][1] = r[3][0]; c.reg[0][2] = r[4][0]; c.reg[0][3] = r[4][1];", logic)
        self.assertIn("c.reg[1][0] = r[1][0]; c.reg[1][1] = r[1][1]; c.reg[1][2] = r[1][2]; c.reg[1][3] = c7_sum;", logic)
        self.assertIn("c.reg[2][0] = r[2][0]; c.reg[2][1] = r[2][1]; c.reg[2][2] = r[2][2]; c.reg[2][3] = 0.0f;", logic)
        self.assertEqual(lf.WIND_REGISTERS["plant"], (10, 21, 22, 25, 26))
        self.assertIn("((c10.x, c25.x, c26.x, c26.y), (c21.xyz, c7.x + c7.y), (c22.xyz, 0))", wind_pins.lines()[0])
        self.assertIn("return PackClayWindConstants(wind, spec, r.instances.offset[0] + r.instances.offset[1]);", logic)


class PinsFile(unittest.TestCase):
    def test_the_tracked_file_is_the_generators(self):
        tracked = (ROOT / "tests" / "native" / "wind_pins.inc").read_bytes().decode("utf-8").replace("\r\n", "\n")
        self.assertEqual(tracked, "\n".join(wind_pins.inc_lines()) + "\n")

    def test_the_sections_are_the_tests_tables(self):
        lines = wind_pins.inc_lines()
        self.assertEqual([line for line in lines if line.startswith("#ifdef")],
                         ["#ifdef FABLE2_WIND_PINS_" + s for s in ("PLANT", "TRUNK", "LEAF", "REACH")])
        self.assertEqual(sum(line == "#endif" for line in lines), 4)
        # every line of the generator is in the file, in its order
        self.assertEqual([line for line in lines if not line.startswith("#") and not line.startswith("// Generated") and
                          not line.startswith("// regenerate") and not line.startswith("// reference") and
                          not line.startswith("// _LEAF")], wind_pins.lines())
        test = _text("tests/native/test_clay_logic.cpp")
        for section in ("PLANT", "TRUNK", "LEAF", "REACH"):
            self.assertEqual(test.count("#define FABLE2_WIND_PINS_%s\n#include \"wind_pins.inc\"\n#undef FABLE2_WIND_PINS_%s\n" % (section, section)), 1)
        self.assertIn("inline constexpr float kTolerance = 2e-5f;", test)


class OnePass(unittest.TestCase):
    def setUp(self):
        self.source = _text("src/native/render/clay_pass.cpp")
        self.logic = _text("src/native/render/clay_logic.h")

    def test_one_vertex_program_for_every_pass(self):
        """A wind record's pipelines (colour, pre-pass, shadow) all take wind_vs_, and no pipeline takes wind_vs_ under a
        key without `wind` (plan Task 10; Review Focus 1)."""
        s = self.source
        # Every vertex shader a pipeline of the pass is given.
        given = re.findall(r"\.vs = ([^;]+);", s)
        self.assertEqual([g for g in given if "wind_vs_" in g], ["key.wind ? wind_vs_ : vs_"])
        create = pass_function(s, "CreatePipeline")
        self.assertIn("  if (key.wind && !wind_vs_) return nullptr;\n  pipe.vs = key.wind ? wind_vs_ : vs_;\n", create)
        # The colour pass's and the pre-pass's pipelines are both made by CreatePipeline, from keys.
        self.assertEqual(len(re.findall(r"\bCreatePipeline\(dev, ", s)), 3)   # the two caches, and Ensure's defaults (default keys)
        self.assertIn("pipelines_.push_back(CreatePipeline(dev, key));", pass_function(s, "TryPipeline"))
        self.assertIn("prepass_pipelines_.push_back(CreatePipeline(dev, prepass_keys_[slot], true));", pass_function(s, "PrepassPipeline"))
        self.assertIn("const PipelineKey prepass = PrepassKey(*key, alpha);", pass_function(s, "PrepassPipeline"))
        self.assertIn("  k.wind = colour.wind;", self.logic)
        # A key gets the wind's field in one place: a moved record's, in PrepareWind, after the program exists.
        self.assertEqual(len(re.findall(r"\.wind = 1;", s)), 1)
        prepare = pass_function(s, "PrepareWind")
        self.assertIn("  if (in.has_words && EnsureWindShader(dev)) {\n    if (const PipelineKey* key = KeyOf(p.pipeline)) {\n"
                      "      PipelineKey moved = *key;  // (a copy: the cache's keys may move when one is added)\n"
                      "      moved.wind = 1;\n      colour = TryPipeline(dev, moved);\n", prepare)
        self.assertIn("if (colour && in_prepass) depth = PrepassPipeline(dev, colour, p.alpha, &flat);", prepare)
        # The maps: the shadow pipelines with the wind program are made by the one function, with wind_vs_, and
        # the others with vs_.
        self.assertEqual(re.findall(r"CreateShadowPipeline\(dev, (\w+), cull\)", s), ["wind_vs_", "vs_"])
        self.assertIn("shadow_wind_pipelines_[slot] = CreateShadowPipeline(dev, wind_vs_, cull);", pass_function(s, "ShadowWindPipeline"))
        self.assertIn("  pipe.vs = vs;\n  pipe.ps = shadow_ps_;\n", pass_function(s, "CreateShadowPipeline"))

    def test_a_verdict_once_and_all_or_none(self):
        """A record is given the wind's pipeline, words, slot and mode bits together, only on the verdict kDrawn."""
        s = self.source
        prepare = pass_function(s, "PrepareWind")
        self.assertEqual(prepare.count("DecideClayWind("), 1)
        at_return = prepare.index("  if (verdict != ClayWind::kDrawn) {\n    if (verdict != ClayWind::kNone) ++st.wind_rest[size_t(verdict)];\n    return;\n  }\n")
        for field in ("  p.wind = wind;\n", "  p.wind_words = words;\n", "  p.wind_slot = slot;\n", "  p.pipeline = colour;\n", "  p.bounds = GrowBounds("):
            self.assertEqual(prepare.count(field), 1, field)
            self.assertGreater(prepare.index(field), at_return, field)
        shadows = pass_function(s, "PrepareShadows")
        self.assertEqual(shadows.count("DecideClayWind("), 1)
        self.assertIn("        if (verdict == ClayWind::kDrawn) {\n          p.pipeline = moved;\n          p.wind_words = words;\n"
                      "          p.wind_slot = slot;\n          p.constants.light_mode = LightModeWind(wind->family);\n"
                      "        } else if (verdict != ClayWind::kNone) {\n          ++st.wind_rest[size_t(verdict)];\n        }\n", shadows)
        self.assertEqual(s.count("DecideClayWind("), 2)
        # The mode's wind bits: set for a moved record alone, in the three draws (the flat pre-pass form, the
        # colour and textured pre-pass form, the caster), each beside the record's words and slot.
        self.assertEqual(s.count("LightModeWind("), 3)
        draw = pass_function(s, "DrawPrepared")
        self.assertIn("const uint32_t flat_mode = p.wind ? LightModeWind(p.wind->family) : 0u;", draw)
        self.assertIn("  if (p.wind) light_mode |= LightModeWind(p.wind->family);\n", draw)
        self.assertEqual(draw.count("BindWind(cmd, p.wind_words, p.wind_slot);"), 2)
        self.assertIn("      if (p.wind_words) {\n        BindWind(cmd, p.wind_words, p.wind_slot);\n", pass_function(s, "DrawShadows"))
        self.assertEqual(s.count("BindWind(cmd, "), 3)
        bind = pass_function(s, "BindWind")
        self.assertIn("cmd->SetBufferSrv(kParamTerrainNormals, words, 0);", bind)
        self.assertIn("cmd->SetConstantBuffer(kParamTerrainConstants, terrain_cb_, ClayObjectSlotOffset(object_slot0_, slot));", bind)

    def test_nothing_of_the_wind_without_the_switch(self):
        """Everything the pass does for the wind is behind wind_on_ (the switch and a scene captured with it)."""
        s = self.source
        render = pass_function(s, "Render")
        self.assertIn("  wind_on_ = wind && scene.wind_records;\n", render)
        self.assertEqual(render.count("PrepareWind("), 1)
        self.assertIn("  if (wind_on_) {\n    const bool prepass = DecideClayMask(mask_in).mask == ClayMask::kDrawn;\n"
                      "    for (Prepared& p : prepared_) {\n"
                      "      // (a late coverage record drawn at half is not in the pre-pass: it needs no wind pipeline there)\n"
                      "      if (p.pipeline && !p.terrain) PrepareWind(dev, *p.r, WindOf(scene, *p.r), prepass && !p.coverage_half, p, st);\n",
                      render)
        self.assertIn("      if (wind_on_) {\n        const capture::WindInputs* wind = ShadowWindOf(scene, c, r);\n", pass_function(s, "PrepareShadows"))
        # the cache's wind buffers, the program and a wind slot are asked for in these functions alone
        self.assertEqual(s.count("geometry_.WindWords("), 1)
        self.assertEqual(s.count("WindWordsFor(dev, "), 2)
        self.assertEqual(s.count("EnsureWindShader(dev)"), 2)
        self.assertEqual(s.count("TakeWindSlot("), 3)   # (its definition and the two callers)
        self.assertIn("  if (wind) {\n    uint32_t form = ", render)
        # the Wind line in the log only with the switch
        self.assertIn("if (st.wind_switch) REXLOG_INFO(\"[native] clay: {}\", render::FormatWindText(st));",
                      _text("src/native/fable2_native_render.cpp"))

    def test_the_switch_is_off_by_default(self):
        self.assertRegex(_text("src/native/fable2_native_render.cpp"), r'REXCVAR_DEFINE_STRING\(fable2_native_wind, "off", "Fable2",')


if __name__ == "__main__":
    unittest.main()
