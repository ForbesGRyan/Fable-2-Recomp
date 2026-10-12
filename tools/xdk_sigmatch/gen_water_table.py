"""light_formulas.WATER_SHADER -> src/native/capture/water_table.inc (the water sub-project, plan Task 4): what the
capture's record of a draw of the lake's surface (vertex shader BEAD with pixel shader A17D, the surface, or F6D9,
the dead opaque twin) is held to. No C++ includes the file before plan Task 9.

  FABLE2_WATER_SHADERS(vs, ps, ps_opaque)             the three hashes
  FABLE2_WATER_VS_REGISTER(reg, read_mask)            the vertex registers a formula reads beyond the draw record's
                                                      rows c0..c3, ascending: the record's order
  FABLE2_WATER_PS_REGISTER(reg, read_mask, kind)      the pixel registers, ascending: the record's order
  FABLE2_WATER_LITERAL(reg, bits_x, bits_y, bits_z, bits_w)   a literal register of both pixel shaders, as float bits
  FABLE2_WATER_BOOL(name, mask)                       a bool the pixel formula knows and its bit of the pixel bool
                                                      word (b128..b159: bit N & 31)
  FABLE2_WATER_FETCH_SLOT(index, slot)                the fetch constants a record carries, in its order: the shore
                                                      mask, the normal map, the reflection picture, the near picture

read_mask: bit k is set when component k (x = 0 ... w = 3) is read by a formula (light_formulas.WATER_SHADER
"vs_read" / "ps_read": the interpreter's own account, water_check.py `used`). ONLY those components are judged or
touched: the others hold garbage in the captures (c129.z is no number, c139.w is 1.9e29) and a bound on them could
only remove the lake for a value the picture does not depend on (sub-project 12's ledger ruling 34).

kind: what "garbage" means for the register's read components; a SANITY bound, fitted to no capture (sub-project
12's ledger ruling 54: the bounds of before, multiples of the values of one place, refused a valid region):
  0  a place or a distance in world units: every read component within 1e6 (PLACE): the eye c19.xyz; c67, whose
     first component is the fog's start, a distance (its .z and .w are factors and take the register's kind: a
     register has one kind, the widest of its read components')
  1  a matrix term: within 1e7 (MATRIX). No pixel register of the water is one (the view-projection is the draw
     record's c0..c3); the kind is kept for the reader of the table.
  2  a factor, a direction's part, a colour, a reciprocal size: within 65,536 (SCALE): every other register but
     the three of kind 3. A vertex register of the table (the two scrolls c115, c116 and the two scales c117, c118)
     has no kind in its line: it is a factor (2).
  3  a factor that must be ABOVE 0: in [1 / 65,536, 65,536] (ledger ruling 34, after the review of Tasks 1 to 5,
     I2): c128.x (the normal's third component), c136.x (the second normal's) and c138.x (the glint's power). At 0
     each stands at a point where the console's zero product decides the pixel (0 * infinity = 0) and an IEEE
     program computes another colour or no number; below 0 the power is infinite on a cosine of 0. The floor
     1 / 65,536 is far above underflow and far below any value a water has (0.279 is the smallest captured).
A component that is no number or infinite is outside every bound. The rules with a meaning of their own (c31.xy in
(0, 4]; c141.w in [0, 1]; c20.w in (0, 64]; c0.zw the reciprocal of the scene's size) are capture::MakeWaterRecord's
(plan Task 9) and are not this table's. THE TABLE'S KINDS ARE THE ONLY BOUNDS of c131.x, c128.x and c136.x (ledger
ruling 42, after the review of Tasks 4 to 15, I3): the spec's "c131.x in [0, 64]" and "c128.x, c136.x at most 64"
refused values the formula takes (it saturates c131.x: a negative one means no reflection) and were multiples of
one region's values; a bound that refuses more than garbage costs a region its whole lake. other_region() is such
a region's record, which the tests hold inside every bound here and through capture::MakeWaterRecord.

Two thresholds, kept apart (the review's note for later tasks): c141.w (a register of this table) decides "shaded"
against "colour 0 with its alpha" inside the pixel shader; the state's alpha reference (register 0x210E, not in
this table: a state word of the draw record) decides drawn against not. They are equal in every captured row
(15 / 255) and are two things: water_pins.py's alpha lines carry both.

CAPTURED below is what `water_check.py bounds` printed for the five discovery captures of 2026-10-11 that hold
water rows (out/native-evidence/water/t4_bounds.txt): the tests hold every captured value of a read component at
least eighty times inside its kind's bound, and a made far region (far_region) inside every bound.

A table is refused (ValueError) rather than written loosely: a hash is 16 hex digits, not zero, and the three
differ; a register is below 248 and named once a bank; every register has read components (a read mask of 0 on a
register a bound is asked for is refused) and each is 0 to 3; a pixel register has a kind of 0 to 3; a literal
register is 248 to 255 and has four values; a bool has a name that is an identifier and a mask of one bit, no two
the same bit; a fetch slot is 0 to 31 and named once.

usage: gen_water_table.py [--out src/native/capture/water_table.inc]     write the file
       gen_water_table.py --check [--out FILE]                           exit 1 when the file differs
"""
import argparse
import math
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import light_formulas as lf  # noqa: E402

OUT = Path(__file__).resolve().parents[2] / "src" / "native" / "capture" / "water_table.inc"
_HASH = re.compile(r"[0-9A-Fa-f]{16}")
_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
PLACE, MATRIX, SCALE = 1e6, 1e7, 65536.0
FLOOR = 1.0 / 65536.0                                   # kind 3's low bound
BOUND = {0: (-PLACE, PLACE), 1: (-MATRIX, MATRIX), 2: (-SCALE, SCALE), 3: (FLOOR, SCALE)}
KIND_TEXT = {0: "a place or distance: within 1e6", 1: "a matrix term: within 1e7", 2: "a factor: within 65,536",
             3: "a factor above 0: in [1 / 65,536, 65,536]"}
VS_KIND = 2
# The kind of each pixel register (module text).
KIND = {0: 2, 19: 0, 20: 2, 28: 2, 31: 2, 66: 2, 67: 0, 68: 2, 116: 2, 117: 2, 126: 2, 127: 2, 128: 3, 129: 2, 130: 2,
        131: 2, 136: 3, 137: 2, 138: 3, 139: 2, 140: 2, 141: 2, 142: 2, 143: 2}

# The smallest and largest of every read component over the 164 water rows of captures 155547, 155731, 221449,
# 155929 and 160104 (160251 holds none) (t4_bounds.txt, nine digits as printed); None: a component no formula reads.
CAPTURED = {
    ("vs", 115): [(1.01788878, 1.43727362), (1.156739, 1.63789594), None, None],
    ("vs", 116): [(-3.34370279, -2.31415677), (0.995257258, 1.46561015), None, None],
    ("vs", 117): [(0.0799999982, 0.0799999982), (0.0799999982, 0.0799999982), None, None],
    ("vs", 118): [(0.0599999949, 0.0600000024), (0.0599999949, 0.0600000024), None, None],
    ("ps", 0): [(0, 0), (0, 0), (0.000892857148, 0.000892857148), (0.00138888892, 0.00138888892)],
    ("ps", 19): [(81.4017792, 91.0360718), (157.005249, 171.927902), (49.7550163, 50.1824341), None],
    ("ps", 20): [None, None, None, (7.84568882, 9.15686417)],
    ("ps", 28): [(1, 1), (1, 1), None, None],
    ("ps", 31): [(0.666666687, 0.666666687), (0.666666687, 0.666666687), None, None],
    ("ps", 66): [(-0.83130461, -0.827394605), (0.0158907901, 0.0904366747), (0.548410416, 0.561396182), None],
    ("ps", 67): [(19.0682278, 20.3912964), None, (1, 1), (0.000500000024, 0.000500000024)],
    ("ps", 68): [(0.266010404, 0.267041922), (0.398031205, 0.401125729), (0.466666698, 0.466666728), None],
    ("ps", 116): [(0.104081206, 0.136960641), None, None, None],
    ("ps", 117): [(-0.427392125, -0.420816243), None, None, None],
    ("ps", 126): [(0.0274509825, 0.0274509825), (0.0745098069, 0.0745098144), (0.0431372561, 0.0431372598), None],
    ("ps", 127): [(0.0588235296, 0.0588235334), (0.129411772, 0.129411772), (0.121568635, 0.121568635), None],
    ("ps", 128): [(0.300000012, 0.300000012), None, None, None],
    ("ps", 129): [(0.420816243, 0.427392125), (0.420816243, 0.427392125), None, None],
    ("ps", 130): [(0.420816243, 0.427392125), (0.420816243, 0.427392125), None, None],
    ("ps", 131): [(0.5, 0.5), None, None, None],
    ("ps", 136): [(0.272607893, 0.279183775), None, None, None],
    ("ps", 137): [(0.5, 0.5), None, None, None],
    ("ps", 138): [(128, 128), None, None, None],
    ("ps", 139): [(0.800000012, 0.800000072), (0.199999928, 0.199999988), None, None],
    ("ps", 140): [(0.0500000007, 0.0500000007), None, None, None],
    ("ps", 141): [(2.00300002, 2.00300002), (2, 2), None, (0.0588235296, 0.0588235296)],
    ("ps", 142): [(0.615668476, 0.658318281), (-0.0716176853, -0.0118244169), (-0.787916601, -0.749324977), None],
    ("ps", 143): [(0.983392239, 0.999589562), (0.82419157, 0.840347648), (0.396631688, 0.405433029), None],
}


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def entry(shader=None):
    """The table's entry from light_formulas.WATER_SHADER (or a table of its shape) and KIND."""
    s = dict(shader or lf.WATER_SHADER)
    slots = s["ps_slots"]
    return {"vs": s["vs"], "ps": s["ps"], "ps_opaque": s["ps_opaque"], "reg": tuple(s["reg"]),
            "vs_read": {reg: tuple(s["vs_read"][reg]) for reg in s["reg"]},
            "ps_reg": tuple(s["ps_reg"]), "ps_read": {reg: tuple(comps) for reg, comps in s["ps_read"].items()},
            "kind": dict(KIND), "ps_lit": dict(s["ps_lit"]),
            "bools": {"b136": s["b136"], "b137": s["b137"]},
            "slots": (slots["mask"], slots["normal"], slots["reflection"], slots["near"])}


def read_components(e):
    """{("vs" | "ps", register): the components a formula reads} of an entry."""
    out = {("vs", reg): tuple(e["vs_read"][reg]) for reg in e["reg"]}
    out.update({("ps", reg): tuple(e["ps_read"][reg]) for reg in e["ps_reg"]})
    return out


def bound(e, key):
    """(low, high) of a read component of register `key` (("vs" | "ps", register))."""
    return BOUND[VS_KIND if key[0] == "vs" else e["kind"][key[1]]]


def reach(e, key):
    """The magnitude a read component of register `key` may have."""
    return bound(e, key)[1]


def kind_text(e, key):
    return KIND_TEXT[VS_KIND if key[0] == "vs" else e["kind"][key[1]]]


def outside(e, registers):
    """The (key, component) pairs of `registers` ({("vs" | "ps", register): four values}) that the table's rule
    refuses: a READ component that is no number, infinite or beyond its kind's reach, taken in single precision as
    a record holds it. A component no formula reads is never judged."""
    out = []
    for key, values in sorted(registers.items(), key=lambda kv: (kv[0][0] != "vs", kv[0][1])):
        lo, hi = (f32(x) for x in bound(e, key))
        for i in read_components(e)[key]:
            v = values[i]
            v = f32(v) if math.isfinite(v) and abs(v) < 3.4e38 else v
            if not (lo <= v <= hi):
                out.append((key, i))
    return out


def far_region():
    """A made region far from the bridge, for the tests: the eye at a world position of (5000, -5000, 300), a fog
    range that starts at 200 units, a wider scroll, another water colour, a sharper glint."""
    r = {("vs", 115): [41.5, -77.25, 0.0, 0.0], ("vs", 116): [-93.0, 12.5, 0.0, 0.0], ("vs", 117): [0.2, 0.2, 0.0, 0.0],
         ("vs", 118): [0.01, 0.03, 0.0, 0.0],
         ("ps", 0): [0.0, 0.0, 1.0 / 1920.0, 1.0 / 1080.0], ("ps", 19): [5000.0, -5000.0, 300.0, 1.0],
         ("ps", 20): [0.0, 0.0, 0.0, 3.5], ("ps", 28): [0.8, 1.2, 0.0, 0.0], ("ps", 31): [0.5, 0.5, 0.0, 0.0],
         ("ps", 66): [0.6, -0.64, 0.48, 0.0], ("ps", 67): [200.0, 6500.0, 0.5, 1.0 / 6000.0], ("ps", 68): [0.4, 0.3, 0.2, 0.0],
         ("ps", 116): [0.02, 0.0, 0.0, 0.0], ("ps", 117): [0.3, 0.0, 0.0, 0.0], ("ps", 126): [0.1, 0.05, 0.02, 0.0],
         ("ps", 127): [0.01, 0.2, 0.3, 0.0], ("ps", 128): [1.5, 0.0, 0.0, 0.0], ("ps", 129): [0.05, 0.9, 0.0, 0.0],
         ("ps", 130): [0.7, 0.1, 0.0, 0.0], ("ps", 131): [0.9, 0.0, 0.0, 0.0], ("ps", 136): [1.0, 0.0, 0.0, 0.0],
         ("ps", 137): [4.0, 0.0, 0.0, 0.0], ("ps", 138): [2048.0, 0.0, 0.0, 0.0], ("ps", 139): [0.5, 0.5, 0.0, 0.0],
         ("ps", 140): [0.002, 0.0, 0.0, 0.0], ("ps", 141): [3.5, 1.25, 0.0, 0.25], ("ps", 142): [0.6, 0.0, -0.8, 0.0],
         ("ps", 143): [8.0, 6.0, 3.0, 0.0]}
    return r


def other_region():
    """far_region with what the spec's named bounds of before refused and the formula takes (ledger ruling 42): no
    reflection (c131.x below 0), a much flatter normal (c128.x = 200), a second normal's factor of 90."""
    r = far_region()
    r[("ps", 131)] = [-0.5, 0.0, 0.0, 0.0]
    r[("ps", 128)] = [200.0, 0.0, 0.0, 0.0]
    r[("ps", 136)] = [90.0, 0.0, 0.0, 0.0]
    return r


def _hash(text, what):
    if not isinstance(text, str) or not _HASH.fullmatch(text):
        raise ValueError("%s %r: a hash is 16 hex digits" % (what, text))
    if int(text, 16) == 0:
        raise ValueError("%s: the hash is zero" % what)
    return int(text, 16)


def _registers(regs, read, bank):
    regs = list(regs)
    for reg in regs:
        if not isinstance(reg, int) or isinstance(reg, bool) or not 0 <= reg < 248:
            raise ValueError("%s register %r is not a register below 248" % (bank, reg))
        if regs.count(reg) != 1:
            raise ValueError("%s register c%d is named twice" % (bank, reg))
        if reg not in read:
            raise ValueError("%s register c%d has no read components named" % (bank, reg))
        comps = list(read[reg])
        if any(not isinstance(k, int) or isinstance(k, bool) or not 0 <= k <= 3 for k in comps) or len(set(comps)) != len(comps):
            raise ValueError("%s register c%d: a read component is 0 to 3, each once (%r)" % (bank, reg, comps))
        if not comps:
            raise ValueError("%s register c%d: a read mask of 0 on a register a bound is asked for" % (bank, reg))
    return sorted(regs)


def _mask(comps):
    return sum(1 << k for k in comps)


def generate(e):
    """The table's text for an entry (the keys of `entry`'s result)."""
    hashes = [_hash(e["vs"], "the vertex shader"), _hash(e["ps"], "the pixel shader"), _hash(e["ps_opaque"], "the opaque pixel shader")]
    if len(set(hashes)) != 3:
        raise ValueError("two of the three shaders have the same hash")
    lines = ["// Generated by tools/xdk_sigmatch/gen_water_table.py - do not edit.",
             "// The lake's surface: vertex shader BEAD with pixel shaders A17D (the surface) and F6D9 (the opaque twin)",
             "// (frame-map section 20). read_mask: bit k set when component k is read by a formula; kind: 0 a place",
             "// within 1e6, 1 a matrix term within 1e7, 2 a factor or direction within 65,536, 3 a factor that must be",
             "// above 0: in [1 / 65,536, 65,536] (c128.x, c136.x, c138.x: the console's zero-product points).",
             "FABLE2_WATER_SHADERS(0x%016Xull, 0x%016Xull, 0x%016Xull)" % tuple(hashes)]
    for reg in _registers(e["reg"], e["vs_read"], "vertex"):
        lines.append("FABLE2_WATER_VS_REGISTER(%d, 0x%X)" % (reg, _mask(e["vs_read"][reg])))
    for reg in _registers(e["ps_reg"], e["ps_read"], "pixel"):
        kind = e["kind"].get(reg)
        if kind not in (0, 1, 2, 3) or isinstance(kind, bool):
            raise ValueError("pixel register c%d: its kind %r is none of 0 to 3" % (reg, kind))
        lines.append("FABLE2_WATER_PS_REGISTER(%d, 0x%X, %d)" % (reg, _mask(e["ps_read"][reg]), kind))
    for reg, values in sorted(e["ps_lit"].items()):
        if not isinstance(reg, int) or not 248 <= reg <= 255:
            raise ValueError("literal c%s is not a literal register (248 to 255)" % (reg,))
        if len(values) != 4 or not all(isinstance(v, (int, float)) and math.isfinite(v) for v in values):
            raise ValueError("literal c%s: a literal register has four values" % (reg,))
        lines.append("FABLE2_WATER_LITERAL(%d, %s)" % (reg, ", ".join("0x%08Xu" % bits(v) for v in values)))
    seen = {}
    for name, mask in e["bools"].items():
        if not isinstance(name, str) or not _NAME.fullmatch(name):
            raise ValueError("bool %r: its name is not an identifier" % (name,))
        if not isinstance(mask, int) or isinstance(mask, bool) or mask <= 0 or mask >= 1 << 32 or mask & (mask - 1):
            raise ValueError("bool %s: its mask %r is not one bit" % (name, mask))
        if mask in seen:
            raise ValueError("bools %s and %s are the same bit" % (seen[mask], name))
        seen[mask] = name
        lines.append("FABLE2_WATER_BOOL(%s, 0x%08Xu)" % (name, mask))
    slots = list(e["slots"])
    for index, slot in enumerate(slots):
        if not isinstance(slot, int) or isinstance(slot, bool) or not 0 <= slot <= 31:
            raise ValueError("fetch slot %r is not a slot (0 to 31)" % (slot,))
        if slots.count(slot) != 1:
            raise ValueError("fetch slot %d is named twice" % slot)
        lines.append("FABLE2_WATER_FETCH_SLOT(%d, %d)" % (index, slot))
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
    out.write_text(text, newline="\n")              # line feeds, whatever the platform: the tracked file's
    return 0


if __name__ == "__main__":
    sys.exit(main())
