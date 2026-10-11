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


if __name__ == "__main__":
    unittest.main()
