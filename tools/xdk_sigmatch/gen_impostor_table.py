"""light_formulas.IMPOSTOR_SHADER and its sanity bounds -> src/native/capture/impostor_table.inc (distant trees,
plan Task 6): what the capture's record of a far-woods draw (vertex shader 129F, pixel shader 4B1D) is held to.

  FABLE2_IMPOSTOR_SHADERS(vs, ps)                                  the pair's hashes
  FABLE2_IMPOSTOR_VS_REGISTER(reg)                                 the vertex registers, ascending: the record's order
  FABLE2_IMPOSTOR_PS_REGISTER(reg)                                 the pixel registers, ascending
  FABLE2_IMPOSTOR_LITERAL(bank, reg, bits_x, bits_y, bits_z, bits_w)   a shader's own literal register (bank 0 the
                                                                   vertex shader, 1 the pixel shader), as float bits
  FABLE2_IMPOSTOR_WORD(component, source_half)                     component k of (x, y, z, s) is this one of the
                                                                   element's four halves, in the order the GPU reads
                                                                   them after the swap by the endian below
  FABLE2_IMPOSTOR_ENDIAN(e)                                        the storage's vertex fetch endian (2: 8-in-32)
  FABLE2_IMPOSTOR_BOUND(bank, reg, component, lo_bits, hi_bits)    lo <= value <= hi: one line for every component
                                                                   a formula READS, none for the others
  FABLE2_IMPOSTOR_SIZE_MAX(bits)                                   a quad's size is in (0, this]
  FABLE2_IMPOSTOR_B129(mask)                                       the pixel bool word's bit that is bool b129

The word. With endian 2 and the order (1, 0, 3, 2) a quad's eight bytes are four big-endian half floats x, y, z,
s in that order (measured: every 129F row of the five discovery captures of 2026-10-11 decodes so, the draw's
own fetch constant has endian 2, the shader's fetch has the swizzle yxwz, and the replay against the game's depth
falls from 99% to 0.1% with the first two exchanged: frame-map section 19).

The bounds are SANITY bounds (ledger ruling 54, after the review of the whole branch, I2): they refuse what is no
value of its kind at all (no number, an infinity, a magnitude no world coordinate, fog range or scale could
have) and nothing else. None is fitted to the captures any more: the five discovery captures were all made within
ten units of one place at one time of day, and "four times the largest magnitude seen there", which the bounds
of before were, refused a region whose fog range starts beyond 81.6 units for a fog that is valid and merely
different (every far tree missing, `register`). By the kind of each component a formula reads (KIND below):

  a place or a distance, in world units    within 1e6 (PLACE)     the eye (vertex c47.xyz, pixel c19.xyz), the fog
                                                                  range's start (vertex c111.x)
  a matrix term                            within 1e7 (MATRIX)    vertex c72..c75 (the projection), c115..c117 (the view)
  a scale: a factor, a direction's part,   within 65,536 (SCALE)  the raise (c8.z), the lying axis (c46.xyz), the
  an exponent, a reciprocal range                                 fog's axis, blend and reciprocal range (c110.xyz,
                                                                  c111.z, .w), the picture's coordinate scale and
                                                                  offset (c113), the extent's factor and exponent
                                                                  (c114.xy)
  a quad's size                            in (0, 4096] (SIZE)    the word's fourth half

and where the spec gives a bound with a meaning of its own, that one (SPEC below): the pixel shader's distances
c12.y and c29.x in [0, 4096], the dissolve's rate c29.y in (0, 4], and the light's three scales c12.w in [0, 64],
c31.x and c20.w in (0, 64] (c12.w was [0, 4]: eight times the one value seen; it is a light scale as the other
two are). The truth of the fog's registers is not this table's: FrameBuilder::Finish holds c110.xyz and c111.x,
.z, .w bit-equal to the frame's own validated fog block (capture::ImpostorFogMatches, the `fog-table` rule), and
this table only keeps garbage out of a frame that has no such block. "(0, x]" is written with the low bound
ABOVE_ZERO, the smallest positive single (bits 0x00000001): a value passes when lo <= value <= hi. CAPTURED below
(from `impostor_check.py bounds 155547 155731 155929 160104 160251`: out/native-evidence/distant-trees/
t6_bounds.txt) is kept as what the tests and that command hold the table to: every captured value lies inside.

Only what a formula reads is bounded (ledger ruling 34, after the review of phase 12a, I2). READ below names
the components impostor_position, impostor_fog and the pixel formulas read (the Tasks 4-5 report's list;
tests/test_gen_impostor_table.py holds it to the three functions by changing one component at a time). A
component no formula reads has no line: the record carries it and nothing judges it, not even for being a
number. The five captures were all made within ten units of one place at one time of day, and say nothing about
what such a component (the sky's path height in c111.y, c12.x, c29.z, ...) holds elsewhere; a bound on it could
only remove every far tree for a value the picture does not depend on.

A table is refused (ValueError) rather than written loosely: a hash is 16 hex digits and not zero; a register is
below 248 and named once a bank; every register has four bounds (None for a component that is not read) of which
at least one is a bound, and every bound belongs to a register; a bound is two numbers with the low at most the high; the word's sources are 0 to 3, each once; the endian is 0 to 3; a
literal register has four values; the size's bound is a positive number; b129's mask is one bit.

usage: gen_impostor_table.py [--out src/native/capture/impostor_table.inc]     write the file
       gen_impostor_table.py --check [--out FILE]                              exit 1 when the file differs
"""
import argparse
import math
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import light_formulas as lf  # noqa: E402

OUT = Path(__file__).resolve().parents[2] / "src" / "native" / "capture" / "impostor_table.inc"
_HASH = re.compile(r"[0-9A-Fa-f]{16}")
BANKS = {"vs": 0, "ps": 1}
ABOVE_ZERO = struct.unpack("<f", struct.pack("<I", 1))[0]       # the smallest positive single: the low bound of "(0, x]"
MATRIX, PLACE, SCALE, SIZE = 1e7, 1e6, 65536.0, 4096.0

# The smallest and largest of every component over the 116 in-scene 129F rows (4,744 quads) of the five discovery
# captures (t6_bounds.txt, nine digits as printed).
CAPTURED = {
    ("vs", 8): [(-0.0720065162, 0.068342641), (-0.0976980999, 0.0810229629), (0.69252795, 0.955497861), (0.0, 0.0)],
    ("vs", 46): [(-0.928349078, 0.999403656), (-0.0345312022, 0.527137399), (0.0, 0.0), (0.0, 0.0)],
    ("vs", 47): [(81.4017792, 91.0360718), (157.005249, 171.927902), (47.2069893, 50.1824341), (0.0, 0.0)],
    ("vs", 72): [(0.977419019, 0.977419019), (0.0, 0.0), (0.0, 0.0), (0.0, 0.0)],
    ("vs", 73): [(0.0, 0.0), (1.73763406, 1.73763406), (0.0, 0.0), (0.0, 0.0)],
    ("vs", 74): [(0.0, 0.0), (0.0, 0.0), (1.00002003, 1.00002003), (-0.100002006, -0.100002006)],
    ("vs", 75): [(0.0, 0.0), (0.0, 0.0), (1.0, 1.0), (0.0, 0.0)],
    ("vs", 110): [(-0.83130461, -0.827394605), (0.0158907901, 0.0904366747), (0.548410416, 0.561396182), (0.0, 0.0)],
    ("vs", 111): [(19.0682278, 20.3912964), (1900.0, 1900.0), (1.0, 1.0), (0.000500000024, 0.000500000024)],
    ("vs", 113): [(0.99999994, 1.0), (-0.0341713727, 0.0360032916), (-0.000745296478, 0.078927815), (0.887217522, 0.984183431)],
    ("vs", 114): [(1.5, 1.5), (2.0, 2.0), (0.0, 0.0), (0.0, 0.0)],
    ("vs", 115): [(-0.0345311798, 0.527137339), (-0.999403536, 0.928349137), (-2.23517418e-08, 8.9406953e-08), (-190.624008, 159.722504)],
    ("vs", 116): [(-0.184218585, 0.42829451), (-0.171489045, 0.114274815), (0.887217641, 0.984183311), (-75.9577713, -44.8091316)],
    ("vs", 117): [(-0.913665295, 0.966767371), (-0.0334035605, 0.514601946), (-0.253465444, 0.461350918), (-138.781876, 34.2654915)],
    ("ps", 12): [(20.0, 20.0), (35.0, 35.0), (2.0, 2.0), (0.470367104, 0.489437193)],
    ("ps", 19): [(81.4017792, 91.0360718), (157.005249, 171.927902), (47.2069893, 50.1824341), (1.0, 1.0)],
    ("ps", 20): [(0.00571426284, 0.00751940813), (0.0700119883, 0.0716881976), (0.0909790993, 0.0927378237), (7.5316143, 9.15686417)],
    ("ps", 29): [(48.0, 48.0), (0.125, 0.125), (998.400024, 998.400024), (25.5999756, 25.5999756)],
    ("ps", 31): [(0.359505564, 0.365275502), (0.0, 0.0), (0.0, 0.0), (0.0, 0.0)],
}
CAPTURED_SIZE = (0.377441406, 16.390625)
# The spec's own bounds, a component at a time (None: the register's rule).
SPEC = {
    ("ps", 12): [None, (0.0, 4096.0), None, (0.0, 64.0)],
    ("ps", 20): [None, None, None, (ABOVE_ZERO, 64.0)],
    ("ps", 29): [(0.0, 4096.0), (ABOVE_ZERO, 4.0), None, None],
    ("ps", 31): [(ABOVE_ZERO, 64.0), None, None, None],
}
# The kind of every component a formula reads (None: not read; a component SPEC bounds has its kind all the same,
# the spec's bound being the narrower statement): what "garbage" means for it.
_P, _M, _S = "place", "matrix", "scale"
KIND = {
    ("vs", 8): [None, None, _S, None],          # the raise: lift = size * c8.z
    ("vs", 46): [_S, _S, _S, None],             # the lying axis, a direction
    ("vs", 47): [_P, _P, _P, None],             # the eye
    ("vs", 72): [_M] * 4, ("vs", 73): [_M] * 4, ("vs", 74): [_M] * 4, ("vs", 75): [_M] * 4,
    ("vs", 110): [_S, _S, _S, None],            # the fog's axis, a direction
    ("vs", 111): [_P, None, _S, _S],            # the fog range's start (a distance), its blend, its reciprocal range
    ("vs", 113): [_S] * 4,                      # the picture's coordinate scale and offset
    ("vs", 114): [_S, _S, None, None],          # the extent's factor and exponent
    ("vs", 115): [_M] * 4, ("vs", 116): [_M] * 4, ("vs", 117): [_M] * 4,
    ("ps", 12): [None, _P, None, _S],           # the light's fall: its start (a distance), its far scale
    ("ps", 19): [_P, _P, _P, None],             # the eye
    ("ps", 20): [None, None, None, _S],         # the output scale
    ("ps", 29): [_P, _S, None, None],           # the dissolve: its start (a distance), its rate
    ("ps", 31): [_S, None, None, None],         # the colour's scale
}
REACH = {_P: PLACE, _M: MATRIX, _S: SCALE}


# The components a formula reads (0 to 3 for x to w); every other component of the table's registers is carried
# and never judged (ruling 34). Position: c8.z, c46.xyz, c47.z, c72..c75, c113, c114.xy, c115..c117. Fog: c47.xyz,
# c110.xyz, c111.x, .z, .w. Pixel: c12.y, .w, c19.xyz, c20.w, c29.x, .y, c31.x.
ALL = (0, 1, 2, 3)
READ = {
    ("vs", 8): (2,), ("vs", 46): (0, 1, 2), ("vs", 47): (0, 1, 2),
    ("vs", 72): ALL, ("vs", 73): ALL, ("vs", 74): ALL, ("vs", 75): ALL,
    ("vs", 110): (0, 1, 2), ("vs", 111): (0, 2, 3), ("vs", 113): ALL, ("vs", 114): (0, 1),
    ("vs", 115): ALL, ("vs", 116): ALL, ("vs", 117): ALL,
    ("ps", 12): (1, 3), ("ps", 19): (0, 1, 2), ("ps", 20): (3,), ("ps", 29): (0, 1), ("ps", 31): (0,),
}


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def entry(shader=None):
    """The table's entry: light_formulas.IMPOSTOR_SHADER with the bounds (module text) and b129's mask."""
    s = dict(shader or lf.IMPOSTOR_SHADER)
    bounds = {}
    for key in CAPTURED:
        if [k is not None for k in KIND[key]] != [i in READ[key] for i in range(4)]:
            raise ValueError("%s c%d: KIND names other components than READ" % key)
        own = SPEC.get(key, [None] * 4)
        # (the spec's bound where it gives one; else the component's kind, plus and minus its reach)
        bounds[key] = [(own[i] if own[i] is not None else (-REACH[KIND[key][i]], REACH[KIND[key][i]])) if i in READ[key] else None
                       for i in range(4)]
    return {"vs": s["vs"], "ps": s["ps"], "reg": tuple(s["reg"]), "ps_reg": tuple(s["ps_reg"]),
            "vs_lit": dict(s["vs_lit"]), "ps_lit": dict(s["ps_lit"]),
            "word_source": tuple(s["word_source"]), "word_endian": s["word_endian"],
            "bounds": bounds, "size_max": SIZE, "b129": lf.IMPOSTOR_B129}


def outside(e, registers):
    """The (key, component) pairs of `registers` ({("vs" | "ps", register): four values}) that lie outside the
    entry's bounds, as capture::detail::ImpostorBounded judges a component (no number is outside; a component
    without a bound is never judged). The values are taken in single precision, as a record holds them."""
    out = []
    for key, values in sorted(registers.items(), key=lambda kv: (kv[0][0] != "vs", kv[0][1])):
        for i, b in enumerate(e["bounds"][key]):
            if b is None:
                continue
            v = values[i]
            v = f32(v) if math.isfinite(v) and abs(v) < 3.4e38 else v
            if not (f32(b[0]) <= v <= f32(b[1])):
                out.append((key, i))
    return out


def far_region():
    """A made region far from the bridge, for the tests (review of the whole branch, I2): the registers of a
    captured row's middle with the eye at a world position of (5000, -5000, 300), a fog range that starts at 200
    units and ends 6,000 further, a dissolve that starts at 300 units, other scales and a wider view."""
    r = {key: [0.5 * (lo + hi) for lo, hi in span] for key, span in CAPTURED.items()}
    r[("vs", 47)] = [5000.0, -5000.0, 300.0, 0.0]
    r[("ps", 19)] = [5000.0, -5000.0, 300.0, 1.0]
    r[("vs", 111)] = [200.0, 6500.0, 1.0, 1.0 / 6000.0]
    r[("vs", 110)] = [0.6, -0.64, 0.48, 0.0]
    r[("vs", 115)] = [0.6, 0.8, 0.0, 1000.0]
    r[("vs", 116)] = [-0.3, 0.2, 0.93, -579.0]
    r[("vs", 117)] = [0.74, -0.56, 0.37, -6611.0]
    r[("vs", 8)] = [0.0, 0.0, 1.6, 0.0]
    r[("vs", 114)] = [3.0, 4.0, 0.0, 0.0]
    r[("ps", 12)] = [20.0, 250.0, 2.0, 2.5]
    r[("ps", 29)] = [300.0, 0.02, 998.4, 25.6]
    return r


def _hash(text, what):
    if not isinstance(text, str) or not _HASH.fullmatch(text):
        raise ValueError("%s %r: a hash is 16 hex digits" % (what, text))
    if int(text, 16) == 0:
        raise ValueError("%s: the hash is zero" % what)
    return int(text, 16)


def generate(e):
    """The table's text for an entry (the keys of `entry`'s result)."""
    lines = ["// Generated by tools/xdk_sigmatch/gen_impostor_table.py - do not edit.",
             "// The far woods' impostors: vertex shader 129F, pixel shader 4B1D (frame-map section 19).",
             "FABLE2_IMPOSTOR_SHADERS(0x%016Xull, 0x%016Xull)" % (_hash(e["vs"], "the vertex shader"), _hash(e["ps"], "the pixel shader"))]
    registers = {}
    for bank, key in (("vs", "reg"), ("ps", "ps_reg")):
        regs = list(e[key])
        for reg in regs:
            if not isinstance(reg, int) or isinstance(reg, bool) or not 0 <= reg < 248:
                raise ValueError("%s register %r is not a register below 248" % (bank, reg))
            if regs.count(reg) != 1:
                raise ValueError("%s register c%d is named twice" % (bank, reg))
        registers[bank] = sorted(regs)
    lines += ["FABLE2_IMPOSTOR_VS_REGISTER(%d)" % r for r in registers["vs"]]
    lines += ["FABLE2_IMPOSTOR_PS_REGISTER(%d)" % r for r in registers["ps"]]
    for bank in ("vs", "ps"):
        for reg, values in sorted(e[bank + "_lit"].items()):
            if len(values) != 4 or not all(isinstance(v, (int, float)) and math.isfinite(v) for v in values):
                raise ValueError("%s literal c%s: a literal register has four values" % (bank, reg))
            if not 248 <= reg <= 255:
                raise ValueError("%s literal c%s is not a literal register (248 to 255)" % (bank, reg))
            lines.append("FABLE2_IMPOSTOR_LITERAL(%d, %d, %s)" % (BANKS[bank], reg, ", ".join("0x%08Xu" % bits(v) for v in values)))
    source = list(e["word_source"])
    if len(source) != 4 or any(not isinstance(s, int) or isinstance(s, bool) or not 0 <= s <= 3 for s in source):
        raise ValueError("the word's sources %r are not four of 0 to 3" % (source,))
    if sorted(source) != [0, 1, 2, 3]:
        raise ValueError("the word's sources %r do not name every half once" % (source,))
    lines += ["FABLE2_IMPOSTOR_WORD(%d, %d)" % (k, s) for k, s in enumerate(source)]
    if e["word_endian"] not in (0, 1, 2, 3):
        raise ValueError("the endian %r is none of 0 to 3" % (e["word_endian"],))
    lines.append("FABLE2_IMPOSTOR_ENDIAN(%d)" % e["word_endian"])
    for key in e["bounds"]:
        if key[0] not in registers or key[1] not in registers[key[0]]:
            raise ValueError("the bound of %s c%s is not a register of the table" % key)
    for bank in ("vs", "ps"):
        for reg in registers[bank]:
            span = e["bounds"].get((bank, reg))
            if span is None:
                raise ValueError("%s register c%d has no bound" % (bank, reg))
            if len(span) != 4:
                raise ValueError("%s register c%d: a register has four bounds" % (bank, reg))
            if all(b is None for b in span):
                raise ValueError("%s register c%d has no bound: a register none of whose components is read" % (bank, reg))
            for i, b in enumerate(span):
                if b is None:           # not read: carried, never judged
                    continue
                lo, hi = b
                if not (isinstance(lo, (int, float)) and isinstance(hi, (int, float)) and math.isfinite(lo) and math.isfinite(hi)):
                    raise ValueError("%s register c%d component %d: a bound that is no number" % (bank, reg, i))
                if lo > hi:
                    raise ValueError("%s register c%d component %d: the low bound is above its high" % (bank, reg, i))
                lines.append("FABLE2_IMPOSTOR_BOUND(%d, %d, %d, 0x%08Xu, 0x%08Xu)" % (BANKS[bank], reg, i, bits(lo), bits(hi)))
    size = e["size_max"]
    if not (isinstance(size, (int, float)) and math.isfinite(size) and size > 0.0):
        raise ValueError("the size's bound %r is not a positive number" % (size,))
    lines.append("FABLE2_IMPOSTOR_SIZE_MAX(0x%08Xu)" % bits(size))
    mask = e["b129"]
    if not isinstance(mask, int) or isinstance(mask, bool) or mask <= 0 or mask >= 1 << 32 or mask & (mask - 1):
        raise ValueError("b129's mask %r is not one bit" % (mask,))
    lines.append("FABLE2_IMPOSTOR_B129(0x%08Xu)" % mask)
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    text = generate(entry())
    out = Path(a.out)
    if a.check:
        if not out.is_file() or out.read_text(encoding="utf-8").replace("\r\n", "\n") != text:
            print("%s is not the generator's output" % out)
            return 1
        return 0
    # Line feeds, whatever the platform: the tracked file's.
    out.write_text(text, newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
