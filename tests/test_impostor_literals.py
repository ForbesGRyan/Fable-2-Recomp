"""The impostors' names and numbers are written in several places: the Python reference
(tools/xdk_sigmatch/light_formulas.py: IMPOSTOR_SHADER, impostor_position, impostor_pixel, impostor_out, impostor_fog),
the generator of the pins (tools/xdk_sigmatch/impostor_pins.py) and its tracked output (tests/native/impostor_pins.inc),
the capture's table (src/native/capture/impostor_table.inc) and the renderer's pure mirrors
(src/native/render/clay_logic.h, "Native impostors"). This test reads the C++ text and holds it to the reference, as
tests/test_wind_literals.py does for the wind (distant trees, plan Task 9). The HLSL side is plan Task 10's.

What no pin can hold is held here by text: a literal of a shader appears in the mirrors only where the reference has
it, by its name, and the constant block's fields lie where the shader will declare them."""
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import impostor_pins  # noqa: E402
import light_formulas as lf  # noqa: E402

NUMBER = r"(?<![\w.\[])[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?f?(?![\w.\]])"


def _text(relative):
    return (ROOT / relative).read_bytes().decode("utf-8").replace("\r\n", "\n")


def _code(text):
    """Without // comments."""
    return "\n".join(line.split("//")[0] for line in text.split("\n"))


def mirrors(logic):
    """The mirrors' text of clay_logic.h (between its two markers), without comments."""
    begin, end = "// IMPOSTOR MIRRORS BEGIN", "// IMPOSTOR MIRRORS END"
    if logic.count(begin) != 1 or logic.count(end) != 1:
        raise AssertionError("clay_logic.h has not exactly one pair of the mirrors' markers")
    return _code(logic[logic.index(begin):logic.index(end)])


def function(text, name):
    """The body of the mirrors' inline function `name`."""
    found = re.findall(r"\ninline [^\n]*\b" + name + r"\([^{;]*\) \{\n(.*?)\n\}\n", text, re.S)
    if len(found) != 1:
        raise AssertionError("%s: %d bodies" % (name, len(found)))
    return found[0]


def numbers(body):
    """The decimal literals of a body, as floats, without array indices and loop bounds (integers in brackets and
    after `<` or `=` in a for)."""
    body = re.sub(r"\[[^\]]*\]", "", body)
    body = re.sub(r"for \([^)]*\)", "", body)
    body = re.sub(r"\bk (?:==|>) \d", "", body)
    return sorted(float(n.rstrip("f")) for n in re.findall(NUMBER, body))


class ImpostorLiterals(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.mirrors = mirrors(cls.logic)
        cls.record = _text("src/native/capture/impostor_record.h")

    def test_the_tracked_pins_are_the_generators(self):
        tracked = _text("tests/native/impostor_pins.inc")
        self.assertEqual(tracked, "\n".join(impostor_pins.inc_lines()) + "\n")
        self.assertNotIn("\r", (ROOT / "tests/native/impostor_pins.inc").read_bytes().decode("utf-8"))

    def test_the_constant_blocks_fields_and_offsets(self):
        body = re.search(r"struct ClayImpostorConstants \{\n(.*?)\n\};", self.logic, re.S).group(1)
        fields = []
        for line in _code(body).split("\n"):
            line = line.strip().rstrip(";")
            if not line:
                continue
            kind, names = line.split(" ", 1)
            for name in names.split(","):
                fields.append((kind, name.strip()))
        self.assertEqual(fields, [("float", "vs[capture::kImpostorVsRegisters][4]"), ("float", "ps[capture::kImpostorPsRegisters][4]"),
                                  ("uint32_t", "first_quad"), ("uint32_t", "quads"), ("uint32_t", "kept"), ("uint32_t", "sampler"),
                                  ("float", "alpha_ref"), ("float", "out_scale"), ("float", "fog_on"), ("float", "pad")])
        regs, ps_regs = len(lf.IMPOSTOR_SHADER["reg"]), len(lf.IMPOSTOR_SHADER["ps_reg"])
        self.assertEqual((regs, ps_regs), (14, 5))
        size = 16 * regs + 16 * ps_regs + 4 * 8
        self.assertEqual(size, 336)
        self.assertIn("static_assert(sizeof(ClayImpostorConstants) == %d && offsetof(ClayImpostorConstants, ps) == %d &&" % (size, 16 * regs), self.logic)
        self.assertIn("offsetof(ClayImpostorConstants, first_quad) == %d && offsetof(ClayImpostorConstants, alpha_ref) == %d);" % (
            16 * (regs + ps_regs), 16 * (regs + ps_regs) + 16), self.logic)
        self.assertIn("inline constexpr uint32_t kClayImpostorSlots = (sizeof(ClayImpostorConstants) + 255) / 256;", self.logic)
        self.assertEqual((size + 255) // 256, 2)
        self.assertIn("static_assert(kClayImpostorSlots == 2 && kClayImpostorFrameSlots == 64);", self.logic)
        self.assertIn("inline constexpr uint32_t kMaxImpostorRecords = 32, kMaxImpostorQuads = 4096;", self.record)
        self.assertIn("inline constexpr uint32_t kImpostorVsRegisters = 14, kImpostorPsRegisters = 5;", self.record)

    def test_the_three_keep_words(self):
        self.assertIn("enum : uint32_t { kImpostorKeepFirst = 1, kImpostorKeepFull = 2, kImpostorKeepHalf = 3 };", self.logic)
        self.assertIn("inline constexpr uint8_t kImpostorMaskOffsets = 0xAA;", self.logic)

    def test_the_registers_names_are_the_references_order(self):
        names = re.search(r"enum : uint32_t \{ (kImpC8.*?) \};", self.record, re.S).group(1)
        self.assertEqual([int(n.strip()[5:]) for n in names.replace("\n", " ").split(",")], list(lf.IMPOSTOR_SHADER["reg"]))
        names = re.search(r"enum : uint32_t \{ (kImpP12.*?) \};", self.record, re.S).group(1)
        self.assertEqual([int(n.strip()[5:]) for n in names.split(",")], list(lf.IMPOSTOR_SHADER["ps_reg"]))

    def test_the_literals_names_are_the_tables(self):
        vs, ps = lf.impostor_literals("vs"), lf.impostor_literals("ps")
        self.assertEqual((vs["two"], vs["minus_one"], vs["one"], vs["zero"], ps["one"]), (2.0, -1.0, 1.0, 0.0, 1.0))
        self.assertIn("inline constexpr float kImpostorTwo = 2.0f, kImpostorMinusOne = -1.0f, kImpostorOne = 1.0f;", self.logic)
        self.assertIn("inline constexpr float kImpostorTenth = std::bit_cast<float>(impostor_detail::LiteralBits(1, 255, 2));", self.logic)
        reg, comp = lf.IMPOSTOR_SHADER["ps_lit_at"]["tenth"]
        self.assertEqual((reg, comp), (255, 2))
        tenth = struct.unpack("<I", struct.pack("<f", ps["tenth"]))[0]
        self.assertIn("impostor_detail::LiteralBits(1, 255, 2) == 0x%08Xu" % tenth, self.logic)
        for name, (r, c_) in lf.IMPOSTOR_SHADER["vs_lit_at"].items():
            if name in ("two", "minus_one", "one", "zero"):
                bits = struct.unpack("<I", struct.pack("<f", vs[name]))[0]
                self.assertIn("impostor_detail::LiteralBits(0, %d, %d) == 0x%08Xu" % (r, c_, bits) if bits else
                              "impostor_detail::LiteralBits(0, %d, %d) == 0u" % (r, c_), self.logic, name)

    def test_a_literal_is_in_a_mirror_only_where_the_reference_has_it(self):
        position = function(self.mirrors, "ImpostorPositionMirror")
        core = function(self.mirrors, "ImpostorPixelCore")
        pixel = function(self.mirrors, "ImpostorPixelMirror")
        fog = function(self.mirrors, "ImpostorFogMirror")
        coords = function(self.mirrors, "ImpostorFogCoords")
        # The position: 2 and -1 once, in the last horizontal term (instr 67, 69); 1 by name; no 0.5, 0.25 or 4 (those
        # make the corner's number from the vertex index, which the mirror is given).
        self.assertEqual(position.count("kImpostorTwo"), 1)
        self.assertEqual(position.count("kImpostorMinusOne"), 1)
        self.assertIn("q[0] = ((u * kImpostorTwo + kImpostorMinusOne) * S) + q[0];", position)
        self.assertNotIn("kImpostorTenth", position)
        self.assertEqual(numbers(position), [0.0] * numbers(position).count(0.0) + [1.0])       # zeros, and the reciprocal's 1
        self.assertIn("const float inverse = 1.0f / total;", position)
        # The pixel: 0.1 once, in the light's fall (instr 11, 12); 1 by name; no 0.5 (read only under b129).
        self.assertEqual(core.count("kImpostorTenth"), 1)
        self.assertIn("const float fall = sat(kImpostorTenth * (out.distance + (-c12[1])));", core)
        self.assertEqual(self.mirrors.count("kImpostorTenth"), 1)
        self.assertEqual([n for n in numbers(core) if n not in (0.0, 1.0)], [])
        self.assertEqual([n for n in numbers(pixel) if n != 0.0], [])
        # The fog: no c8.x factor and no literal of its own but the pair of "before the start", (1, 0).
        self.assertNotIn("kImpC8", fog + coords)
        self.assertEqual([n for n in numbers(fog) if n not in (0.0, 1.0)], [])
        self.assertIn("add[i] = (1.0 - T[i]) * S2[i];", fog)
        for name in ("0.5f", "0.25f", "4.0f", "0.1f"):
            self.assertNotIn(name, position + core + pixel + fog + coords, name)

    def test_the_registers_a_mirror_reads_are_the_references(self):
        """Each mirror names exactly the registers the generator's READ list gives its formula (ledger ruling 34)."""
        import gen_impostor_table as gen
        position = function(self.mirrors, "ImpostorPositionMirror")
        fog = function(self.mirrors, "ImpostorFogCoords") + function(self.mirrors, "ImpostorFogMirror")
        pixel = function(self.mirrors, "ImpostorPixelCore") + function(self.mirrors, "ImpostorPixelMirror")
        named = lambda body, prefix: sorted({int(n) for n in re.findall(r"kImp%s(\d+)" % prefix, body)})        # noqa: E731
        self.assertEqual(named(position, "C"), [8, 46, 47, 72, 73, 74, 75, 113, 114, 115, 116, 117])
        self.assertEqual(named(fog, "C"), [47, 110, 111])
        self.assertEqual(sorted(set(named(position, "C") + named(fog, "C"))), sorted(r for k, r in gen.READ if k == "vs"))
        self.assertEqual(named(pixel, "P"), [12, 19, 29, 31])               # (c20.w reaches the pixel as the constants' out_scale)
        self.assertIn("c.out_scale", pixel)
        self.assertEqual(sorted(named(pixel, "P") + [20]), sorted(r for k, r in gen.READ if k == "ps"))


class ImpostorShaderText(unittest.TestCase):
    """The HLSL side (plan Task 10): shaders::kClayImpostorVs and kClayImpostorPs in src/native/fable2_native_shaders.h."""

    @classmethod
    def setUpClass(cls):
        import wind_hlsl
        cls.header = _text("src/native/fable2_native_shaders.h")
        cls.logic = _text("src/native/render/clay_logic.h")
        cls.vertex = _code(wind_hlsl.region(cls.header, *wind_hlsl.IMPOSTOR_VERTEX[:2]))
        cls.pixel = _code(wind_hlsl.region(cls.header, *wind_hlsl.IMPOSTOR_PIXEL[:2]))

    def test_the_block_is_the_structs_fields_in_order(self):
        body = re.search(r"cbuffer Impostor : register\(b0\) \{\n(.*?)\n\};", self.header, re.S).group(1)
        fields = []
        for line in _code(body).split("\n"):
            for part in line.split(";"):
                if part.strip():
                    kind, name = part.strip().split(" ")
                    fields.append((kind, name))
        self.assertEqual(fields, [("float4", "i_vs[14]"), ("float4", "i_ps[5]"), ("uint", "i_first_quad"), ("uint", "i_quads"), ("uint", "i_kept"),
                                  ("uint", "i_sampler"), ("float", "i_alpha_ref"), ("float", "i_out_scale"), ("float", "i_fog_on"), ("float", "i_pad")])
        # the same names, in the struct's order, at the struct's offsets (the shader test holds the table to the compiled programs)
        struct = re.search(r"struct ClayImpostorConstants \{\n(.*?)\n\};", self.logic, re.S).group(1)
        names = [n.strip().split("[")[0] for line in _code(struct).split("\n") if line.strip() for n in line.strip().rstrip(";").split(" ", 1)[1].split(",")]
        self.assertEqual(["i_" + n for n in names], [name.split("[")[0] for _, name in fields])
        table = re.search(r"kClayImpostorFields\[\] = \{\n(.*?)\n\};", self.header, re.S).group(1)
        got = [(n, int(o), int(b)) for n, o, b in re.findall(r'\{"(\w+)", (\d+), (\d+)\}', table)]
        sizes = {"float4": 16, "uint": 4, "float": 4}
        want, at = [], 0
        for kind, name in fields:
            count = int(name.split("[")[1].rstrip("]")) if "[" in name else 1
            want.append((name.split("[")[0], at, sizes[kind] * count))
            at += sizes[kind] * count
        self.assertEqual(got, want)
        self.assertEqual(at, 336)
        self.assertIn("inline constexpr unsigned kClayImpostorSize = 336;", self.header)
        self.assertEqual((len(lf.IMPOSTOR_SHADER["reg"]), len(lf.IMPOSTOR_SHADER["ps_reg"])), (14, 5))

    def test_the_corners_and_the_one_load(self):
        # six vertices a quad: the quad's corners 0 1 2, 0 2 3 (the game's quad list has four, in this order)
        self.assertEqual(self.header.count("static const uint kCorner[6] = {0, 1, 2, 0, 2, 3};"), 1)
        self.assertEqual(self.header.count("impostor_words["), 1)
        self.assertIn("  uint quad = id / 6u;\n  [branch] if (quad < i_quads) {\n    float4 word = impostor_words[i_first_quad + quad];\n", self.header)
        lit = lf.impostor_literals("vs")
        self.assertEqual((lit["two"], lit["minus_one"], lit["one"]), (2.0, -1.0, 1.0))

    def test_a_register_is_named_only_by_a_component_the_reference_reads(self):
        """Ledger rulings 34 and 38: every use of a register in the two texts is `I_Cnn.<components>`, each component on
        the generator's READ list; the blocks' rows are named nowhere else in the programs."""
        import gen_impostor_table as gen
        for text, prefix, kind, block in ((self.vertex, "I_C", "vs", "i_vs"), (self.pixel, "I_P", "ps", "i_ps")):
            body = "\n".join(line for line in text.split("\n") if not line.startswith("#define"))
            self.assertNotIn(block + "[", body)
            uses = re.findall(r"\b%s(\d+)\b(\.[xyzw]+)?" % prefix, body)
            self.assertGreater(len(uses), 5)
            seen = {}
            for reg, comps in uses:
                self.assertTrue(comps, "%s%s is used whole" % (prefix, reg))
                for c in comps[1:]:
                    self.assertIn("xyzw".index(c), gen.READ[(kind, int(reg))], "%s%s.%s is not read by the reference" % (prefix, reg, c))
                    seen.setdefault(int(reg), set()).add("xyzw".index(c))
            # and everything on the list is read, but c111.z (the near blend's slope: the trees' tables have no blend,
            # so nothing of the fog pair depends on it) and c20.w (it reaches the pixel as the block's output scale)
            want = {reg: set(comps) for (k, reg), comps in gen.READ.items() if k == kind}
            if kind == "vs":
                want[111] = want[111] - {2}
            else:
                del want[20]
            self.assertEqual(seen, want)
        # the programs' mains name no register at all
        for name in ("kClayImpostorVs", "kClayImpostorPs"):
            start = self.header.index("inline constexpr const char* %s" % name)
            main = self.header[self.header.index("TEXT END", start):self.header.index(')hlsl";', start)]
            self.assertIsNone(re.search(r"\bI_[CP]\d+|\bi_vs\b|\bi_ps\b", main), name)

    def test_a_literal_is_in_the_text_only_where_the_reference_has_it(self):
        def body(text, name):
            found = re.findall(r"\n(?:float|float2|float3|float4) %s\([^)]*\) \{\n(.*?)\n\}\n" % name, "\n" + text, re.S)
            self.assertEqual(len(found), 1, name)
            return re.sub(r"\bI_[CP]\d+", "", found[0])

        position = body(self.vertex, "ImpostorPosition")
        # 2 and -1 once, in the last horizontal term (instr 67, 69); the refused position's 2; the guards' 3e38; 0 and 1
        self.assertIn("precise float moved = ((u * 2.0 + -1.0) * S) + q_x;", position)
        self.assertIn("precise float4 pos = float4(0.0, 0.0, 2.0, 1.0);", position)
        rest = position.replace("((u * 2.0 + -1.0) * S)", "").replace("float4(0.0, 0.0, 2.0, 1.0)", "")
        self.assertEqual(sorted(set(numbers(rest))), [0.0, 1.0, 2.0, 3.0e38])      # (2: the corner numbers k >= 2.0 and k == 2.0)
        self.assertEqual(rest.count("2.0"), 2)
        self.assertEqual(rest.count("3.0e38"), 5)
        pixel = body(self.pixel, "ImpostorPixel")
        self.assertIn("precise float fall = saturate(0.1 * (d + -I_P12.y));", self.pixel)
        self.assertEqual(sorted(set(numbers(pixel))), [-1.0, 0.1, 1.0])
        self.assertEqual(pixel.count("0.1"), 1)
        keep = body(self.pixel, "ImpostorKeep")
        self.assertEqual(sorted(set(numbers(keep))), [0.0, 0.25, 0.75, 1.0, 2.0, 3.0])     # the samples' thresholds under offsets 0xAA; the three kept sets
        self.assertEqual([0.5 - 0.125 * 2, 1.0 - 0.125 * 2], [0.25, 0.75])
        for name in ("ImpostorFogCoords", "ImpostorFogFactor", "ImpostorFogAdd"):
            self.assertEqual([n for n in numbers(body(self.vertex, name)) if n not in (0.0, 1.0, 1.0e30, 3.0e38)], [], name)
        self.assertNotIn("I_C8", self.vertex[self.vertex.index("float3 ImpostorFogCoords"):])       # no c8.x factor in the fog


class ImpostorPassText(unittest.TestCase):
    """The pass's side of the step (plan Task 10), by its text: with the switch off nothing of the step is reached, and
    the step is drawn in the colour pass alone."""

    @classmethod
    def setUpClass(cls):
        cls.pass_ = _text("src/native/render/clay_pass.cpp")
        cls.front = _text("src/native/fable2_native_render.cpp")

    def test_nothing_of_the_step_without_the_mode(self):
        s = self.pass_
        head = ("  const capture::ImpostorMode impostor_mode = ClayImpostorMode(impostors, scene.impostor_mode);\n"
                "  st.impostor_mode = impostor_mode;\n"
                "  st.impostor_feed = impostor_mode == capture::ImpostorMode::kOn && impostor_feed_ && impostor_feed_->sets;"
                "   // (a feed run is marked wherever its pictures are named)\n"
                "  impostor_step_slots_ = 0;\n  bool impostor_step = false;\n"
                "  if (impostor_mode != capture::ImpostorMode::kOff) {\n")
        self.assertEqual(s.count(head), 1)
        # the one place that makes the step's objects and takes its slots lies inside that test, behind the picture's verdict
        block = s[s.index(head) + len(head):s.index("  // The frame's coverage: the switch as it is now.")]
        self.assertEqual(s.count("EnsureImpostorObjects(dev)"), 1)             # (the one call)
        self.assertEqual(block.count("EnsureImpostorObjects(dev)"), 1)
        self.assertEqual(s.count("impostor_step_slots_ = kClayImpostorStepSlots;"), 1)
        self.assertEqual(block.count("impostor_step_slots_ = kClayImpostorStepSlots;"), 1)
        self.assertEqual(s.count("impostor_step = true;"), 1)
        self.assertEqual(block.count("impostor_step = true;"), 1)
        self.assertIn("        const ClayImpostorSkip picture = DecideImpostorPicture(impostor_mode, ImpostorPicture::kNone);\n"
                      "        if (picture != ClayImpostorSkip::kNone) {\n", block)
        self.assertLess(block.index("DecideImpostorPicture("), block.index("EnsureImpostorObjects(dev)"))     # the picture's verdict first
        # `on` (plan Task 14): each record's picture is read and judged inside the same test, before any object is
        # asked; a record whose picture is not usable is counted there and is not among the drawable ones; the
        # objects are asked only for a frame with something to draw
        self.assertEqual(s.count("ReadImpostorPicture(dev, scene.impostors[i], &which, &sampler, st)"), 1)
        self.assertEqual(block.count("ReadImpostorPicture(dev, scene.impostors[i], &which, &sampler, st)"), 1)
        self.assertLess(block.index("ReadImpostorPicture("), block.index("EnsureImpostorObjects(dev)"))
        self.assertIn("          const ClayImpostorSkip picture = DecideImpostorPicture(impostor_mode, state);\n"
                      "          if (picture != ClayImpostorSkip::kNone) {\n            ++st.impostor_skip[size_t(picture)];\n"
                      "            continue;\n          }\n          impostor_usable[i] = true;\n", block)
        self.assertIn("      if (drawable && !EnsureImpostorObjects(dev)) {\n", block)
        self.assertIn("      if (impostor_mode == capture::ImpostorMode::kOn) {\n", block)
        # no picture object without the mode `on`: the reader is called from that one place, and it alone makes them
        reader = s[s.index("ImpostorPicture ClayPass::ReadImpostorPicture("):s.index("void ClayPass::ForgetImpostorObjects()")]
        for made in ("p.texture = dev->CreateTexture(td);", "p.view = dev->CreateTextureView(p.texture, vd);", "impostor_staging_pool_.Take("):
            self.assertEqual(s.count(made), 1, made)
            self.assertEqual(reader.count(made), 1, made)
        # the guest's bytes are copied before anything is judged, and only the copy is read after that
        copy = reader.index("  std::memcpy(impostor_guest_.data(), guest, kImpostorBaseBytes);\n")
        import re
        self.assertEqual(re.findall(r"(?<![A-Za-z_])guest[,;)\[]", reader[copy + 40:]), [])      # the guest's pointer is not read again
        for later in ("ImpostorPictureHash(", "DecodeImpostorBase(", "Decode1555Level(", "ImpostorPictureUsable(", "MakeImpostorLevels("):
            self.assertGreater(reader.index(later), copy, later)
        # the picture's hash is of every byte of the copy (review of Tasks 10 to 14, Minor 5): never the sampled one
        self.assertNotIn("SampleHash(", reader)
        self.assertIn("  hash = ImpostorPictureHash(impostor_guest_.data(), kImpostorBaseBytes);\n", reader)
        # (ledger ruling 54) the texture's bookkeeping is the pure rule's (impostor_picture.h, with its table): the
        # reader asks the plan after the hash and hands what the device gave to the outcome; nothing in the pass
        # writes a held field but through the pure functions and the two pointers' truth
        self.assertIn("  const ImpostorPicturePlan plan = PlanImpostorPicture(p.held, hash, swizzle);\n  if (plan.reuse) {\n"
                      "    ++st.impostor_pictures;\n    return verdict(ImpostorPicture::kUsable);\n  }\n", reader)
        self.assertGreater(reader.index("PlanImpostorPicture(p.held, hash, swizzle)"), reader.index("ImpostorPictureHash("))
        self.assertLess(reader.index("if (plan.reuse) {"), reader.index("DecodeImpostorBase("))            # an unchanged picture is not decoded
        self.assertIn("  const ImpostorPictureOutcome outcome = CommitImpostorPicture(p.held, plan, hash, swizzle, texture_made, view_made, staged);\n"
                      "  p.held = outcome.held;\n", reader)
        self.assertLess(reader.index("ImpostorPictureUsable(stats)"), reader.index("CommitImpostorPicture("))   # a refused picture changes nothing held
        self.assertLess(reader.index("CommitImpostorPicture("), reader.index("impostor_uploads_[impostor_upload_count_++]"))
        self.assertEqual(s.count("PlanImpostorPicture("), 1)
        self.assertEqual(s.count("CommitImpostorPicture("), 1)
        for field in ("filled", "shader_state", "hash", "swizzle"):
            self.assertEqual(re.findall(r"held\.%s\s*=[^=]" % field, s), [], field)
        self.assertEqual(re.findall(r"\bp\.(filled|shader_state|hash|swizzle)\b", s), [])                    # (the fields of before are gone)
        self.assertEqual(sorted(re.findall(r"p\.held = [^;]+;", s)),
                         ["p.held = ImpostorPictureHeld{};", "p.held = ImpostorPictureUploaded(p.held);", "p.held = outcome.held;"])
        self.assertIn("        if (ImpostorPictureUploadFromShaderState(p.held)) cmd->Barrier(p.texture, nrhi::ResourceState::kPixelShaderResource, "
                      "nrhi::ResourceState::kCopyDest);\n", s)
        self.assertIn("        cmd->Barrier(p.texture, nrhi::ResourceState::kCopyDest, nrhi::ResourceState::kPixelShaderResource);\n"
                      "        p.held = ImpostorPictureUploaded(p.held);\n", s)
        # the first frame drawn from textures as they are says so, once (the closing runs read it: the reuse ran)
        self.assertEqual(s.count("first frame with every picture reused, scene frame {}"), 1)
        self.assertIn("        if (!impostor_first_reuse_logged_ && st.impostor_pictures > 0 && st.impostor_refreshed == 0) {\n"
                      "          impostor_first_reuse_logged_ = true;\n", block)
        # a read-back mode that brings old pictures is refused by its own name before a byte of guest memory is read
        self.assertIn("    if (fable2::ImpostorReadbackByMode(impostor_readback_)) {\n", reader)
        self.assertLess(reader.index("return ImpostorPicture::kMode;"), reader.index("capture::ReadPhysical("))
        self.assertLess(reader.index("return ImpostorPicture::kMode;"), reader.index("ImpostorPicturePlan plan"))
        self.assertEqual(s.count("ImpostorPicture::kMode"), 2)
        # the first refusal's line names the content rule's bounds as they are (ledger ruling 45): no upper cover bound
        self.assertNotIn("kImpostorCoverMax", s)
        self.assertIn("(ring clear {} of {}, bound at least {}; covered {}, bound at least {} and no upper bound; hash 0x{:016X}; pictures {}, feed {})", reader)
        self.assertLess(reader.index("ImpostorPictureUsable(stats)"), reader.index("MakeImpostorLevels("))      # the content rule before any level
        self.assertLess(reader.index("ImpostorPictureUsable(stats)"), reader.index("impostor_uploads_[impostor_upload_count_++]"))
        # everything else of the step asks `impostor_step` or the draws it prepared
        self.assertEqual(s.count("  if (impostor_step && impostor_flat_staging_) {\n"), 1)
        self.assertEqual(s.count("  if (impostor_step) {\n"), 1)
        self.assertEqual(s.count("    if (!impostor_draw_count) return;\n"), 1)
        # the frame's buffer and plan take the step's slots from the one member, 0 in every other frame
        # (since the water, sub-project 13: the water step's slots follow in both calls, 0 in every frame without it)
        self.assertIn("ClayFrameSmallSlots(terrain_items_.size(), plant_constants_.size(), impostor_step_slots_, water_step_slots_)", s)
        self.assertIn("PlanClayFrameSlots(slot - kClayFrameTerrainSlot0, uint32_t(plant_constants_.size()), impostor_step_slots_, water_step_slots_);", s)
        self.assertEqual(s.count("PlanClayFrameSlots("), 1)
        self.assertEqual(s.count("ClayFrameSmallSlots(terrain_items_.size(), plant_constants_.size(), impostor_step_slots_, water_step_slots_)"), 2)   # (the call and its comment, whole)
        self.assertEqual(s.count("ClayFrameSmallSlots("), 2)

    def test_the_step_is_drawn_in_the_colour_pass_alone(self):
        s = self.pass_
        # its layout and pipeline are bound in one lambda, called at its place among the records and after the last
        self.assertEqual(s.count("cmd->SetBindingLayout(impostor_layout_);"), 1)
        self.assertEqual(s.count("cmd->SetPipeline(impostor_pipeline_);"), 1)
        self.assertEqual(s.count("draw_impostors"), 3)
        self.assertIn("    if (due.impostors) draw_impostors();\n", s)
        self.assertIn("  if (!left_out && after.impostors) draw_impostors();\n", s)
        colour = s.index("  // 3. The colour pass, as always:")
        self.assertGreater(s.index("auto draw_impostors = [&] {"), colour)       # after the pre-pass and the mask: no pre-pass draw
        for name in ("DrawShadows", "PrepareShadows", "DrawPrepared"):
            body = s[s.index("void ClayPass::%s(" % name):]
            body = body[:body.index("\n}\n")]
            self.assertNotIn("impostor", body.lower(), name)
        lam = s[s.index("auto draw_impostors = [&] {"):]
        lam = lam[:lam.index("\n  };\n")]
        self.assertIn("    cmd->SetBufferSrv(kImpostorParamWords, terrain_cb_, ClaySmallSlotOffset(impostor_words_slot_));\n", lam)
        self.assertIn("      cmd->Draw(6 * d.quads, 0);\n", lam)
        self.assertIn("    bind_scene();\n    bound = nullptr;", lam)
        # `flat` keeps the first sample's set, the picture is the flat one, and a record is whole or not drawn
        self.assertEqual(s.count("MakeClayImpostorConstants("), 1)
        self.assertIn("MakeClayImpostorConstants(r, out_scale, post_frame, fog_on, kImpostorKeepFirst, with_picture ? impostor_sampler[i] : 0u, &constants)", s)
        # (plan Task 14) the picture bound is the draw's own: in `on` the record's, and a record without a usable
        # picture is passed over before anything of it is made: no flat quad stands in for a picture
        self.assertEqual(s.count("cmd->SetTexture(kImpostorParamPicture, "), 1)
        self.assertIn("        cmd->SetTexture(kImpostorParamPicture, d.view);\n", lam)
        self.assertIn("        const bool with_picture = impostor_pictures_on && i < records && impostor_usable[i];\n"
                      "        if (impostor_pictures_on && !with_picture) continue;\n", s)
        self.assertEqual(s.count("impostor_flat_view_;"), 1)
        self.assertIn("        nrhi::TextureView* view = with_picture ? impostor_pictures_[impostor_picture[i]].view : impostor_flat_view_;\n", s)
        self.assertLess(s.index("if (impostor_pictures_on && !with_picture) continue;"), s.index("MakeClayImpostorConstants("))
        self.assertEqual(s.count("kImpostorKeepFull") + s.count("kImpostorKeepHalf"), 0)       # the first sample's set alone (rulings 13 and 48)
        self.assertIn("    } else if (!ImpostorPlacesOneRun(impostor_seqs_.data(), impostor_seqs_.size(), first_place, last_place)) {\n"
                      "      all = ClayImpostorSkip::kPlace;\n", s)
        self.assertIn("        if (i >= records || !ImpostorWordsHeld(r, word_floats)) {\n", s)
        # one pipeline: depth test less-or-equal with the depth write, no blend, no cull
        made = s[s.index("bool ClayPass::EnsureImpostorObjects("):s.index("// --- The water (clay_logic.h, \"Native water\"; frame-map section 20)")]
        self.assertEqual(made.count("CreateGraphicsPipeline("), 1)
        for line in ("  pipe.cull = nrhi::CullMode::kNone;\n", "  pipe.blend.enable = false;\n", "  pipe.depth.test_enable = true;\n",
                     "  pipe.depth.write_enable = true;\n", "  pipe.depth.func = nrhi::CompareFunc::kLessEqual;\n"):
            self.assertEqual(made.count(line), 1, line)

    def test_a_feed_run_is_marked(self):
        """Ledger ruling 47: the first-upload line, the Impostors line (ClayStats::impostor_feed) and the pictures file
        say that the pictures were the diagnostic feed's, not the running game's."""
        s = self.pass_
        self.assertEqual(s.count("impostor_feed_ && impostor_feed_->sets"), 2)        # the reader's source and the frame's mark
        self.assertEqual(s.count("  st.impostor_feed = impostor_mode == capture::ImpostorMode::kOn && impostor_feed_ && impostor_feed_->sets;"), 1)
        self.assertEqual(s.count("st.impostor_feed ="), 1)
        self.assertNotIn("pictures decoded from guest memory and uploaded", s)
        self.assertIn('st.impostor_feed ? "from the feed\'s file (fable2_native_impostor_feed: NOT the running game\'s)" : "decoded from guest memory"', s)
        self.assertIn("PackImpostorPictures(scene.frame, impostor_dump_entries, impostor_dump_count, &impostor_dump_file_, st.impostor_feed);", s)
        self.assertEqual(s.count("PackImpostorPictures("), 1)

    def test_the_front_end_hands_the_switch_on_and_writes_the_line(self):
        # its definition, the Render's argument, the install line, the capture's; and (plan Tasks 12 and 14) the
        # read-back list's question at start-up and the pictures' availability before a Render in `on`
        # (and the diagnostic feed's loader, which reads its file only in `on`; the frame reads the switch once:
        # review of the whole branch, Minor 2)
        self.assertEqual(self.front.count("CurrentImpostors()"), 6)
        self.assertIn("  const capture::ImpostorMode impostors = CurrentImpostors();\n"
                      "  if (impostors == capture::ImpostorMode::kOn) g_clay->SetImpostorReadback(FrameImpostorReadback());\n", self.front)
        self.assertIn("CurrentCoverage(),\n                 impostors, water, post, st, tst);", self.front)
        # (ledger ruling 47) the list is asked for the pictures only with the switch at `on` AND the native renderer on
        self.assertIn("bool ImpostorSwitchOn() { return CurrentImpostors() == capture::ImpostorMode::kOn; }", self.front)
        self.assertIn("bool ImpostorReadbackWanted() { return fable2::ImpostorPicturesInList(REXCVAR_GET(fable2_native_render), ImpostorSwitchOn()); }", self.front)
        app = (ROOT / "src" / "core" / "fable_2_app.h").read_text(encoding="utf-8")
        self.assertIn("      const bool impostors_on = fable2::native::ImpostorSwitchOn();\n"
                      "      const bool impostors = fable2::native::ImpostorReadbackWanted();\n", app)
        self.assertEqual(app.count("fable2::ForceReadbackSeedSuffix(impostors_on, impostors)"), 1)
        self.assertEqual(app.count("fable2::ForceReadbackHandSuffix(impostors_on)"), 1)
        self.assertEqual(app.count("fable2::ForceReadbackList(cfg.hero_dog_texture_readback, exposure, impostors)"), 1)
        # a list given by hand that holds the fourteen bases has the pictures (review Minor 3)
        self.assertIn('fable2::ForceListHasImpostorPictures(rex::cvar::GetFlagByName("readback_resolve_force_addresses"))', self.front)
        # (ledger ruling 54) the availability is the pure rule's, from the read-back MODE too, asked before every
        # Render in `on`; the install line names it
        self.assertNotIn("SetImpostorPicturesAvailable", self.front)
        self.assertNotIn('== "full"', self.front)
        self.assertEqual(self.front.count("fable2::DecideImpostorReadback("), 2)
        self.assertEqual(self.front.count('fable2::EffectiveReadbackMode('), 2)
        self.assertIn('fable2::EffectiveReadbackMode(rex::cvar::GetFlagByName("readback_resolve"), rex::cvar::GetFlagByName("d3d12_readback_resolve") == "true")', self.front)
        self.assertIn('"the pictures: {}",\n                  fable2::ImpostorReadbackWhy(CurrentImpostorReadback()));', self.front)
        self.assertIn('REXLOG_INFO("[native] impostor pictures: {} (the read-back list {}; --readback_resolve \'{}\', --d3d12_readback_resolve {}){}",', self.front)
        # the lines of `off` and `flat` are the ones of before
        self.assertIn('      REXLOG_INFO("[native] impostors: {} (fable2_native_impostors)", capture::ImpostorModeName(impostors));\n', self.front)
        # (the water's line follows the impostors' since sub-project 13)
        self.assertIn('render::FormatCoverageText(st) + "\\n" + render::FormatImpostorText(st) + "\\n" + render::FormatWaterText(st));', self.front)
        # the log's line: with the pictures their CPU time beside it, otherwise the line of before; none with the mode off
        self.assertIn('    } else if (st.impostor_mode != capture::ImpostorMode::kOff) {\n      REXLOG_INFO("[native] clay: {}", render::FormatImpostorText(st));\n', self.front)
        self.assertIn('    if (st.impostor_mode == capture::ImpostorMode::kOn) {\n      REXLOG_INFO("[native] clay: {} | the pictures\' CPU time this frame {:.3f} ms", '
                      'render::FormatImpostorText(st), st.impostor_picture_ms);\n', self.front)
        # the list is asked for once, at start-up, and never written by the renderer
        self.assertNotIn("SetFlagByName(\"readback_resolve", self.front)


SDK = ROOT / "thirdparty" / "rexglue-sdk" / "src" / "graphics"


@unittest.skipUnless((SDK / "d3d12" / "command_processor.cpp").is_file(), "the SDK's source is not in this checkout (thirdparty/rexglue-sdk)")
class ReadbackRuleSource(unittest.TestCase):
    """src/core/readback_force_list.h states the SDK's read-back rules from a reading of its source, and its native
    test holds the header to the test's own copy (review of the whole branch, question 6: a change of the SDK leaves
    both copies green). These pins are the lines read: when an SDK update changes one, this test says so, and the
    header's rule (ForceListForces, EffectiveReadbackMode, DecideImpostorReadback) is read against the new text."""

    @classmethod
    def setUpClass(cls):
        cls.shared = (SDK / "command_processor.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
        cls.d3d12 = (SDK / "d3d12" / "command_processor.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
        cls.header = _text("src/core/readback_force_list.h")

    def test_the_lists_window(self):
        self.assertEqual(self.shared.count("  constexpr uint32_t kForceReadbackWindow = 1u << 16;"), 1)
        self.assertEqual(self.shared.count("      if (base >= a && base - a < kForceReadbackWindow) {"), 1)
        self.assertIn("inline constexpr uint32_t kForceReadbackWindow = 1u << 16;", self.header)
        self.assertIn("      if (base >= a && base - a < kForceReadbackWindow) return true;", self.header)

    def test_the_four_modes_and_the_legacy_switch(self):
        s = self.shared
        self.assertIn('    .allowed({"none", "fast", "some", "full"})', s)
        self.assertIn('ReadbackResolveMode ParseReadbackResolveMode(std::string_view value) {\n  if (value == "fast") {\n'
                      '    return ReadbackResolveMode::kFast;\n  }\n  if (value == "some") {\n    return ReadbackResolveMode::kSome;\n  }\n'
                      '  if (value == "full") {\n    return ReadbackResolveMode::kFull;\n  }\n  return ReadbackResolveMode::kDisabled;\n}', s)
        self.assertIn("  ReadbackResolveMode shared_mode = ParseReadbackResolveMode(REXCVAR_GET(readback_resolve));\n"
                      "  bool shared_mode_overrides_legacy = shared_mode != ReadbackResolveMode::kDisabled ||\n"
                      '                                      rex::cvar::HasNonDefaultValue("readback_resolve");\n'
                      "  if (shared_mode_overrides_legacy) {\n    return shared_mode;\n  }\n"
                      "  return legacy_readback_resolve_enabled ? ReadbackResolveMode::kFast\n"
                      "                                         : ReadbackResolveMode::kDisabled;", s)
        self.assertIn('REXCVAR_DEFINE_STRING(readback_resolve, "none", "GPU",', s)          # the default the legacy switch waits for

    def test_what_a_resolve_does_in_each_mode(self):
        d = self.d3d12
        # only in "none" does the list choose the path; every other mode takes the read-back path for every resolve
        self.assertIn("  ReadbackResolveMode readback_mode = GetReadbackResolveMode(REXCVAR_GET(d3d12_readback_resolve));\n"
                      "  if (readback_mode == ReadbackResolveMode::kDisabled &&\n"
                      "      !ShouldForceReadbackResolve(register_file_->values[XE_GPU_REG_RB_COPY_DEST_BASE])) {\n", d)
        # fast and some read the OTHER buffer (the previous resolve of the address) without waiting; none and full wait
        self.assertIn("  bool use_delayed_sync =\n"
                      "      readback_mode == ReadbackResolveMode::kFast || readback_mode == ReadbackResolveMode::kSome;\n"
                      "  uint32_t read_index = write_index;\n  if (use_delayed_sync) {\n    read_index = 1 - write_index;\n"
                      "  } else if (!AwaitAllQueueOperationsCompletion()) {\n    return true;\n  }\n", d)
        # some copies only when there is no previous buffer: the first resolve of an address
        self.assertIn("  bool should_copy = (readback_mode == ReadbackResolveMode::kSome) ? is_cache_miss : true;\n", d)
        self.assertEqual(d.count("ReadbackResolveMode::kSome"), 2)
        self.assertEqual(d.count("ReadbackResolveMode::kFast"), 1)
        self.assertEqual(d.count("ReadbackResolveMode::kFull"), 0)                           # full is "not disabled, not delayed"
        # and the header's rule names the two modes that are refused and the two that are not
        h = self.header
        self.assertIn("    case ReadbackMode::kFull: return ImpostorReadback::kFull;\n    case ReadbackMode::kFast: return ImpostorReadback::kModeFast;\n"
                      "    case ReadbackMode::kSome: return ImpostorReadback::kModeSome;\n"
                      "    default: return list_forces_fourteen ? ImpostorReadback::kListed : ImpostorReadback::kNoList;\n", h)


if __name__ == "__main__":
    unittest.main()
