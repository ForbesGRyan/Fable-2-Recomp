"""light_formulas.IMPOSTOR_SHADER and the captured bounds -> src/native/capture/impostor_table.inc (distant trees,
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
  FABLE2_IMPOSTOR_BOUND(bank, reg, component, lo_bits, hi_bits)    lo <= value <= hi, four lines a register
  FABLE2_IMPOSTOR_SIZE_MAX(bits)                                   a quad's size is in (0, this]
  FABLE2_IMPOSTOR_B129(mask)                                       the pixel bool word's bit that is bool b129

The word. With endian 2 and the order (1, 0, 3, 2) a quad's eight bytes are four big-endian half floats x, y, z,
s in that order (measured: every 129F row of the five discovery captures of 2026-10-11 decodes so, the draw's
own fetch constant has endian 2, the shader's fetch has the swizzle yxwz, and the replay against the game's depth
falls from 99% to 0.1% with the first two exchanged: frame-map section 19).

The bounds (ledger: the plan's provisional table). The spec's own stand where it gives them: pixel c29.y in
(0, 4], c29.x and c12.y in [0, 4096], c12.w in [0, 4], c31.x and c20.w in (0, 64], the matrix rows (vertex
c72..c75, c115..c117) within 1e7, a place (the eye: vertex c47, pixel c19) within 1e6. Every other register is
held, in each of its four components, to plus and minus FOUR TIMES THE LARGEST MAGNITUDE any of its components
has in the in-scene 129F rows of the five discovery captures D1, D2, D3T, D4a, D4b (CAPTURED below, from
`impostor_check.py bounds 155547 155731 155929 160104 160251`: out/native-evidence/distant-trees/t6_bounds.txt);
the size's bound is four times the largest captured size. "(0, x]" is written with the low bound ABOVE_ZERO, the
smallest positive single (bits 0x00000001): a value passes when lo <= value <= hi. A register whose value another
place of the world takes outside these bounds makes a record that is refused and counted (`register`, `quad`),
never one that is drawn wrong.

A table is refused (ValueError) rather than written loosely: a hash is 16 hex digits and not zero; a register is
below 248 and named once a bank; every register has four bounds and every bound belongs to a register; a bound is
two numbers with the low at most the high; the word's sources are 0 to 3, each once; the endian is 0 to 3; a
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
MATRIX, PLACE, FACTOR = 1e7, 1e6, 4.0

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
    ("ps", 12): [None, (0.0, 4096.0), None, (0.0, 4.0)],
    ("ps", 20): [None, None, None, (ABOVE_ZERO, 64.0)],
    ("ps", 29): [(0.0, 4096.0), (ABOVE_ZERO, 4.0), None, None],
    ("ps", 31): [(ABOVE_ZERO, 64.0), None, None, None],
}
WHOLE = {("vs", 47): PLACE, ("ps", 19): PLACE,
         ("vs", 72): MATRIX, ("vs", 73): MATRIX, ("vs", 74): MATRIX, ("vs", 75): MATRIX,
         ("vs", 115): MATRIX, ("vs", 116): MATRIX, ("vs", 117): MATRIX}


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def entry(shader=None):
    """The table's entry: light_formulas.IMPOSTOR_SHADER with the bounds (module text) and b129's mask."""
    s = dict(shader or lf.IMPOSTOR_SHADER)
    bounds = {}
    for key, span in CAPTURED.items():
        if key in WHOLE:
            rule = [(-WHOLE[key], WHOLE[key])] * 4
        else:
            reach = f32(FACTOR * max(max(abs(lo), abs(hi)) for lo, hi in span))
            rule = [(-reach, reach)] * 4
        own = SPEC.get(key, [None] * 4)
        bounds[key] = [own[i] if own[i] is not None else rule[i] for i in range(4)]
    return {"vs": s["vs"], "ps": s["ps"], "reg": tuple(s["reg"]), "ps_reg": tuple(s["ps_reg"]),
            "vs_lit": dict(s["vs_lit"]), "ps_lit": dict(s["ps_lit"]),
            "word_source": tuple(s["word_source"]), "word_endian": s["word_endian"],
            "bounds": bounds, "size_max": f32(FACTOR * CAPTURED_SIZE[1]), "b129": lf.IMPOSTOR_B129}


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
            for i, (lo, hi) in enumerate(span):
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
