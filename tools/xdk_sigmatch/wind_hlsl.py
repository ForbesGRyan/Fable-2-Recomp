"""The wind vertex program's HLSL text, translated mechanically to Python and evaluated in double.

The native wind is written three times: the Python reference (light_formulas.py: wind_plant, wind_trunk,
wind_leaf), the renderer's C++ mirrors (src/native/render/clay_logic.h), which the pins hold to the reference,
and the text that is compiled: the functions under CLAY_WIND in src/native/fable2_native_shaders.h. Nothing
but the game runs that text. This module reads it as it stands in the header (from the declaration of the wind
words at t9 to the `#else` of the terrain normals), turns each line into Python by fixed rules (no line is
edited by hand, and a line no rule knows stops the translation), and evaluates WindPlant, WindTrunk and
WindLeaf on given registers, literals and vertex words, so that tests/test_wind_hlsl.py can hold the text
itself to the reference: a wrong register component, weight, sign or function in the HLSL is then a failing
test and not a picture nobody measured (review of plan Task 10, Important 1; ledger ruling 44).

What it does not reach: the compiler and the GPU (single precision, `precise`, the device's sin and cos), and
the loads of the words from t9 in the vertex main (held by text in tests/test_wind_literals.py).

The translation is not the wind's alone: `translate` and `region` take the two strings a text stands between and
the functions it must hold, and `Region` compiles any such text and runs it in a caller's environment. The far
woods' impostor programs are held the same way (distant trees, plan Task 10: tests/test_impostor_hlsl.py; the
strings are IMPOSTOR_VERTEX and IMPOSTOR_PIXEL below).

  wind_hlsl.py [<fable2_native_shaders.h>]     prints the translated program
"""
import math
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SHADERS = REPO / "src" / "native" / "fable2_native_shaders.h"
BEGIN = "StructuredBuffer<float> wind_words : register(t9);"
END = "#else\nStructuredBuffer<float2> terrain_normals"
FUNCTIONS = ("WindFrac", "WindPhase", "WindPlant", "WindTreeOffset", "WindTrunk", "WindLeaf")
# The impostor step's two texts (shaders::kClayImpostorVs, kClayImpostorPs): (begin, end, the functions).
IMPOSTOR_VERTEX = ("// IMPOSTOR VERTEX TEXT BEGIN\n", "// IMPOSTOR VERTEX TEXT END\n",
                   ("ImpostorPlace", "ImpostorCoord", "ImpostorPosition", "ImpostorFogCoords", "ImpostorFogFactor", "ImpostorFogAdd"))
IMPOSTOR_PIXEL = ("// IMPOSTOR PIXEL TEXT BEGIN\n", "// IMPOSTOR PIXEL TEXT END\n",
                  ("ImpostorDistance", "ImpostorPixel", "ImpostorKeep", "ImpostorOut"))


class Untranslated(ValueError):
    """A line of the wind text that no rule of the translation knows."""


class Vec:
    """An HLSL float2, float3 or float4: component-wise arithmetic with a vector of the same size or a number,
    swizzles read as a number or a Vec, and single components written."""

    def __init__(self, *parts):
        values = []
        for part in parts:
            values.extend(part.values if isinstance(part, Vec) else [float(part)])
        self.__dict__["values"] = values

    def __getattr__(self, name):
        if name and all(c in "xyzw" for c in name):
            got = [self.values["xyzw".index(c)] for c in name]
            return got[0] if len(got) == 1 else Vec(*got)
        raise AttributeError(name)

    def __setattr__(self, name, value):
        if name not in ("x", "y", "z", "w"):
            raise AttributeError(name)
        self.values["xyzw".index(name)] = float(value)

    def _each(self, other, op):
        if isinstance(other, Vec):
            if len(other.values) != len(self.values):
                raise ValueError("vectors of %d and %d components" % (len(self.values), len(other.values)))
            return Vec(*[op(a, b) for a, b in zip(self.values, other.values)])
        return Vec(*[op(a, other) for a in self.values])

    def __add__(self, other):
        return self._each(other, lambda a, b: a + b)

    def __radd__(self, other):
        return self._each(other, lambda a, b: b + a)

    def __sub__(self, other):
        return self._each(other, lambda a, b: a - b)

    def __rsub__(self, other):
        return self._each(other, lambda a, b: b - a)

    def __mul__(self, other):
        return self._each(other, lambda a, b: a * b)

    def __rmul__(self, other):
        return self._each(other, lambda a, b: b * a)

    def __truediv__(self, other):
        return self._each(other, lambda a, b: a / b)

    def __neg__(self):
        return Vec(*[-a for a in self.values])


def saturate(x):
    return min(max(x, 0.0), 1.0)


def region(header_text, begin=BEGIN, end=END):
    """The lines of a text in the shader header: after `begin`, before the next `end`. Exactly one `begin` (the
    wind's: the declaration of the words, and the `#else` of the terrain normals), or ValueError."""
    text = header_text.replace("\r\n", "\n")
    if text.count(begin) != 1:
        raise ValueError("%d times %r in the header" % (text.count(begin), begin))
    start = text.index(begin) + len(begin)
    stop = text.find(end, start)
    if stop < 0:
        raise ValueError("no %r after %r" % (end, begin))
    return text[start:stop]


def wind_text(header_text):
    """The lines of the wind's text in the shader header: after the declaration of the words, before the
    `#else`. Exactly one such block, or ValueError."""
    return region(header_text)


def translate(header_text, begin=BEGIN, end=END, functions=FUNCTIONS):
    """(Python source, {a define's name: the place it names}) of the text between `begin` and `end` (the wind's
    by default). Rules, a line each: a comment is dropped; `#define NAME block[r].c` (or `block[r]`) names a
    place in a constant block (a literal's slot `o_wind_lit[r].c`, a register row `i_vs[r]`) and the name is
    replaced where it is used; `float|float2|float3|float4 Name(args) {` opens a function (a body on the same
    line ends in `}`); `}` closes it or a block; `precise` and a declaration's type are dropped;
    `[branch] if (c) x = y;` is an if, `[branch] if (c) {` an if whose statements are the lines indented under
    it; `x = (c) ? a : b;` is a conditional; `&&` and `||` are `and` and `or`; `float2(`, `float3(` and
    `float4(` build a Vec. Anything left that still reads as HLSL raises Untranslated, and so does a text
    without one of `functions`."""
    defines, out = {}, []
    for raw in region(header_text, begin, end).split("\n"):
        line = raw.split("//")[0].rstrip()
        if not line.strip():
            continue
        m = re.fullmatch(r"#define (\w+) (\w+\[\d+\](?:\.[xyzw])?)", line)
        if m:
            if m.group(1) in defines:
                raise Untranslated("%s defined twice" % m.group(1))
            defines[m.group(1)] = m.group(2)
            continue
        if line.startswith("#"):
            raise Untranslated(raw)
        m = re.fullmatch(r"(?:float|float2|float3|float4) (\w+)\(([^)]*)\) \{(.*)", line)
        if m:
            args = ", ".join(a.split()[-1] for a in m.group(2).split(","))
            out.append("def %s(%s):" % (m.group(1), args))
            rest = m.group(3).strip()
            if rest:
                if not rest.endswith("}"):
                    raise Untranslated(raw)
                out.append("  " + rest[:-1].strip().rstrip(";"))
            continue
        if line.strip() == "}":
            continue
        line = line.replace("precise ", "")
        line = line.replace(" && ", " and ").replace(" || ", " or ")
        line = re.sub(r"^(\s*)(?:float4|float3|float2|float) (\w+) = ", r"\1\2 = ", line)
        m = re.fullmatch(r"(\s*)\[branch\] if \((.*)\) \{", line)
        if m:
            out.append("%sif %s:" % (m.group(1), m.group(2)))
            continue
        m = re.fullmatch(r"(\s*)\[branch\] if \((.*)\) (\w+ = .*);", line)
        if m:
            line = "%sif %s: %s" % (m.group(1), m.group(2), m.group(3))
        m = re.fullmatch(r"(\s*\w+ = )\((.*)\) \? (.*) : (.*);", line)
        if m:
            line = "%s(%s) if (%s) else (%s)" % (m.group(1), m.group(3), m.group(2), m.group(4))
        line = re.sub(r"\bfloat[234]\(", "Vec(", line)
        out.append(line.rstrip(";"))
    code = "\n".join(out)
    for name, place in defines.items():
        code = re.sub(r"\b%s\b" % re.escape(name), place, code)
    for mark in ("?", "[branch]", "float", "precise", "#", "{", "}", ";", "&", "|"):
        if mark in code:
            raise Untranslated("`%s` is left in the translated text" % mark)
    for name in functions:
        if "def %s(" % name not in code:
            raise Untranslated("no function %s in the text" % name)
    return code, defines


class Region:
    """A text of the shader header between two strings, translated and compiled once. `functions(env)` gives
    its functions with `env`'s names for everything the text does not define itself: the constant blocks (lists
    of Vec, numbers), the intrinsics it calls and the functions of other texts. `Vec` and `saturate` are given."""

    def __init__(self, header_text, begin, end, functions):
        self.source, self.defines = translate(header_text, begin, end, functions)
        self.code = compile(self.source, "<a text of fable2_native_shaders.h>", "exec")

    def functions(self, env):
        names = {"Vec": Vec, "saturate": saturate}
        names.update(env)
        exec(self.code, names)
        return names


class Program:
    """The translated wind text, compiled once. `functions(registers, literals)` gives its functions bound to
    three register rows (four numbers each: a tree's c15, c16, c17; a plant's packed rows) and to a family's
    literals in slot order (28 for a leaf, 18 for a trunk, 7 for a plant; the rest of the seven rows zero)."""

    def __init__(self, header_text):
        self.source, self.defines = translate(header_text)
        self.code = compile(self.source, "<the wind text of fable2_native_shaders.h>", "exec")

    def functions(self, registers, literals):
        if len(registers) != 3 or any(len(r) != 4 for r in registers):
            raise ValueError("three register rows of four numbers")
        if len(literals) > 28:
            raise ValueError("%d literals: more than the seven rows hold" % len(literals))
        slots = list(literals) + [0.0] * (28 - len(literals))
        env = {"Vec": Vec, "sin": math.sin, "cos": math.cos, "sqrt": math.sqrt, "floor": math.floor, "abs": abs,
               "max": max, "saturate": saturate, "o_wind_reg": [Vec(*r) for r in registers],
               "o_wind_lit": [Vec(*slots[4 * r:4 * r + 4]) for r in range(7)]}
        exec(self.code, env)
        return env

    def plant(self, registers, literals, p, seed, z):
        return self.functions(registers, literals)["WindPlant"](Vec(*p), seed, z).values

    def trunk(self, registers, literals, p, weights):
        return self.functions(registers, literals)["WindTrunk"](Vec(*p), Vec(*weights)).values

    def leaf(self, registers, literals, p, q, weights, flutter):
        return self.functions(registers, literals)["WindLeaf"](Vec(*p), Vec(*q), Vec(*weights), Vec(*flutter)).values


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    path = Path(argv[0]) if argv else SHADERS
    source, defines = translate(path.read_bytes().decode("utf-8"))
    print(source)
    print("# %d literal names, %d lines" % (len(defines), source.count("\n") + 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
