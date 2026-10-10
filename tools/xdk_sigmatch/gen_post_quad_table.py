"""light_formulas.POST_QUAD_SHADERS -> src/native/capture/post_quad_table.inc.

  FABLE2_POST_QUAD(ps, vs, kind, layer, target, blend, alpha_test,
                   slot_count, slot0, slot1, slot2,
                   register_count, r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11)

One line per furniture quad the capture records (docs/native-renderer/lighting-formulas.md, "The sun layer and
the sky furniture"; every pixel and vertex shader in it is checked against its microcode by post_check.py),
in the order of the list, which is the order of the record's kinds (plan Task 12, PostQuadKind).
  ps, vs          the pixel shader's hash and the hash of the vertex shader it is drawn with
  kind            Moon, Cloud, Glare, BeamSource, Flare, MoonGlow, SunDisc, CloudOccluder
  layer           0 the main scene, 1 the sun layer (the record's `layer`)
  target          Scene; LayerDepth (the layer's depth, colour mask 0), Layer1 (the beam source's picture),
                  Layer2 (flares, moon glow, sun disc)
  blend           Alpha (source alpha, inverse source alpha), Add (one, one), Replace (no blend), Depth
                  (nothing but depth is written)
  alpha_test      1 where every captured row of the shader has the alpha test on (function greater; the
                  reference is the draw's register 0x210E, not the table's)
  slot_count, slot0..2             the texture slots, in the record's `fetch` order; unused ones are 0
  register_count, r0..r11          the pixel registers the microcode reads without its literals, ascending:
                                   the record's `ps` rows in this order; unused ones are 0

A list is refused (ValueError naming the shader) rather than read loosely: a hash is 16 hex digits (written
in upper case), never zero, listed once whatever its letters' case; a kind is one of the eight, once; at most
12 registers, each an integer 0 to 251 (c252 on are literals), ascending without repeats; one to three
different slots 0 to 31; a shader of the post chain's eight is no quad; and a shader without a machine check
(not in POST_FURNITURE_SHADERS, pixel or vertex) is not written (spec ruling 18); an empty list is no table.

usage: gen_post_quad_table.py --out src/native/capture/post_quad_table.inc
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import light_formulas as lf  # noqa: E402

KINDS = {"moon": "Moon", "cloud": "Cloud", "glare": "Glare", "beam-source": "BeamSource", "flare": "Flare",
         "moon-glow": "MoonGlow", "sun-disc": "SunDisc", "cloud-occluder": "CloudOccluder"}
TARGETS = {"scene": ("Scene", 0), "layer-depth": ("LayerDepth", 1), "layer1": ("Layer1", 1), "layer2": ("Layer2", 1)}
BLENDS = {"alpha": "Alpha", "add": "Add", "none": "Replace", "depth": "Depth"}
MAX_REGISTERS, MAX_SLOTS = 12, 3            # QuadRecord::ps and QuadRecord::fetch
ARGUMENTS = 7 + 1 + MAX_SLOTS + 1 + MAX_REGISTERS
_HASH = re.compile(r"[0-9A-Fa-f]{16}")


def _hash(text, name):
    if not isinstance(text, str) or not _HASH.fullmatch(text):
        raise ValueError(f"{name}: {text!r}: a hash is 16 hex digits")
    value = int(text, 16)
    if value == 0:
        raise ValueError(f"{name}: the hash is zero")
    return value


def _integers(values, lo, hi):
    return all(isinstance(v, int) and not isinstance(v, bool) and lo <= v <= hi for v in values)


def generate(shaders, not_quads=(), checked=None):
    """The table text for a list of light_formulas.PostQuadShader. not_quads: hashes that must not be in it;
    checked: the hashes that have a machine check (None: no such condition)."""
    shaders = list(shaders)
    if not shaders:
        raise ValueError("no quad shader: the table is not generated from an empty list")
    excluded = {int(h, 16) for h in not_quads}
    known = None if checked is None else {int(h, 16) for h in checked}
    seen, kinds, lines = set(), set(), []
    for s in shaders:
        value = _hash(s.hash, "pixel shader")
        name = f"{value:016X}"
        vs = _hash(s.vs, f"{name}: vertex shader")
        if value in seen:
            raise ValueError(f"{name}: the shader is listed twice")
        seen.add(value)
        if value in excluded:
            raise ValueError(f"{name}: not a quad of the furniture (a shader of the post chain)")
        if known is not None and (value not in known or vs not in known):
            raise ValueError(f"{name}: no machine check of it or of its vertex shader {vs:016X} (POST_FURNITURE_SHADERS)")
        if not isinstance(s.kind, str) or s.kind not in KINDS:
            raise ValueError(f"{name}: kind {s.kind!r} is not one of {', '.join(KINDS)}")
        if s.kind in kinds:
            raise ValueError(f"{name}: kind {s.kind} twice")
        kinds.add(s.kind)
        registers, slots = tuple(s.registers), tuple(s.slots)
        if len(registers) > MAX_REGISTERS:
            raise ValueError(f"{name}: {len(registers)} registers, the record holds {MAX_REGISTERS} registers")
        if not _integers(registers, 0, 251):
            raise ValueError(f"{name}: a register of {registers!r} is not an integer 0 to 251")
        if list(registers) != sorted(set(registers)):
            raise ValueError(f"{name}: the registers {registers!r} are not ascending without repeats")
        if not 1 <= len(slots) <= MAX_SLOTS or not _integers(slots, 0, 31) or len(set(slots)) != len(slots):
            raise ValueError(f"{name}: {slots!r} is not one to {MAX_SLOTS} different slots 0 to 31")
        if not isinstance(s.where, str) or s.where not in TARGETS:
            raise ValueError(f"{name}: where {s.where!r} is not one of {', '.join(TARGETS)}")
        if not isinstance(s.blend, str) or s.blend not in BLENDS:
            raise ValueError(f"{name}: blend {s.blend!r} is not one of {', '.join(BLENDS)}")
        if not isinstance(s.test, bool):
            raise ValueError(f"{name}: test {s.test!r} is not True or False")
        target, layer = TARGETS[s.where]
        args = [f"0x{value:016X}ull", f"0x{vs:016X}ull", KINDS[s.kind], str(layer), target, BLENDS[s.blend], str(int(s.test)),
                str(len(slots))] + [str(v) for v in slots + (0,) * (MAX_SLOTS - len(slots))]
        args += [str(len(registers))] + [str(v) for v in registers + (0,) * (MAX_REGISTERS - len(registers))]
        lines.append("FABLE2_POST_QUAD(" + ", ".join(args) + ")")
    return "\n".join(["// Generated by tools/xdk_sigmatch/gen_post_quad_table.py - do not edit."] + lines) + "\n"


def table():
    """The table of light_formulas.POST_QUAD_SHADERS."""
    return generate(lf.POST_QUAD_SHADERS, not_quads=[s.hash for s in lf.POST_SHADERS],
                    checked=[s.hash for s in lf.POST_FURNITURE_SHADERS])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    # Line feeds, whatever the platform: the tracked file's (git stores it so; a checkout may convert).
    Path(a.out).write_text(table(), newline="\n")


if __name__ == "__main__":
    main()
