"""ps-albedo.json -> src/native/capture/ps_albedo_table.inc (held against vs-transforms.json).

  FABLE2_PS_ALBEDO(hash, slot, u_interp, u_comp, u_s0, u_o0, u_s1, u_o1,
                   v_interp, v_comp, v_s0, v_o0, v_s1, v_o1)
  FABLE2_PS_NO_ALBEDO(hash)
  FABLE2_PS_ALPHA(hash, source, slot, component, scale_ref0, scale_ref1, divide_colour)  (optional "alpha")
  FABLE2_PS_TERRAIN(hash, kind, base_slot, detail_slot, side_slot, steep_threshold, steep_epsilon)
                                                              ("kind" other than "plain")
  FABLE2_PS_LIGHT(hash, family, multi, facing_scale)          (optional "light")
  FABLE2_PS_LIGHT_VS(hash, vertex shader hash)                (one per vertex shader of "light"."vertex")
Stage refs use uv_refs notation with bank 1 (pixel constants). Alpha: source 0 one, 1 albedo,
2 fetch; slot is the albedo's slot for "albedo" and the entry's own for "fetch"; component 0-3
(x-w); scale_ref0/1 are the up to two pixel constants (register * 4 + component, no bank or negate
bit) the alpha is multiplied by, in the order applied, -1 none; divide_colour 1 when the shader
divides its colour by the alpha ("divide_by_alpha").
Kind: 0 plain (the default; no line), 1 terrain-layer, 2 terrain-far, 3 terrain-far-fade (the far
shader that multiplies its colour by the fade). A terrain entry names the texture fetch slots of the
base map ("base_slot") and of the detail texture, for the far kinds the far colour texture
("detail_slot"); it is the whole entry: no albedo line is written for it, and it takes none of
"slot", "u", "v", "alpha" or "no_albedo". A terrain-layer entry may have "steep", the steep-ground
block of the layer shaders that blend a side texture in where the ground is steep:
{"threshold": the slope value below which the block runs, "epsilon": the small term of its weights
(both literals of the shader), "side_slot": the texture fetch slot of the side texture}; without it
the line ends in -1, 0.0f, 0.0f. All ALPHA lines, then all TERRAIN lines, follow the albedo lines.
Light: an albedo or terrain entry may have "light": {"family", "vertex", "evidence"}, the light family
of the shader as tools/xdk_sigmatch/light_check.py names it (docs/native-renderer/lighting-formulas.md):
1 terrain-layer, 2 terrain-far, 3 object, 4 tree-trunk, 5 plant, 6 plant-lit, 7 tree-leaf, each but
terrain-far and tree-leaf also with "-multi" (the family's two- and four-light form: multi 1; the
leaves' lights are their vertex shader's, their pixel shader reads no light register and is one
program for one, two and four lights, so the entry is "tree-leaf" whatever the vertex shaders it
lists, and light_check.py's tree-leaf-multi pairs stand under it). A family belongs to the pair
of a pixel and a vertex shader, so "vertex" lists the vertex shaders the pair was checked with, one
FABLE2_PS_LIGHT_VS line each; with any other vertex shader the draw has no family. What the vertex
side reads is that shader's own "light" in vs-transforms.json. "facing_scale": the literal of the
terrain layers' facing factor sat(scale * (N . c109)), which the shader loads itself (no constant
reader gives it); required for the terrain-layer families, refused for the others (0.0f in the line).
The LIGHT lines, each followed by its LIGHT_VS lines, come last.
The two tables are held against each other: this generator reads the vertex table too (--vs-json;
by default the vs-transforms.json beside --json) and refuses a pair whose vertex shader has no
"light" there (an entry without one, no entry) and a pair whose vertex shader is of a kind the family
cannot stand on: the terrain families need a "terrain" vertex shader, object "stream" or "probe",
tree-trunk and tree-leaf "probe", the plants "instance" (_LIGHT_VERTEX_KINDS; capture::LightKindFits in
material.h is the same rule at the draw, which would give every draw of such a pair no light,
"vertex-shader"). A table with any "light" is not generated without the vertex table.
What generation cannot check is the family itself: that a pair is of the family named, and of its
one-light form or the "-multi" one, is light_check.py's verdict on the two shaders' microcode
(FAMILIES), which no field of either table repeats. 0x648B965C200A5113 written "object" for
"object-multi" generates, and its draws under a lantern would be lit as the sun's. What holds the
committed table to the checked label is tests/test_gen_albedo_table.py,
test_the_committed_light_pairs_are_the_checked_pairs (the table's triples are FAMILIES', exactly):
run the Python suite after editing a "light".

An entry is refused (ValueError naming the shader) rather than read loosely: a key is "0x" and 16
hex digits and names its shader once, whatever the letters' case (load() also refuses a key written
twice in the file); every entry has "manual": true and a non-empty "evidence", an "alpha" its
"alpha_evidence", a "light" its own "evidence"; a slot is a JSON integer 0 to 31; and an entry has
only the keys of its form (ENTRY_KEYS, ALPHA_KEYS, LIGHT_KEYS: the keys the committed table uses),
so a mistyped key cannot be dropped without a word. A light family the entry's kind cannot have (a
terrain family on a plain entry and the reverse, a layer family on a far entry) is refused.

usage: gen_albedo_table.py --json docs/native-renderer/ps-albedo.json
                           --out src/native/capture/ps_albedo_table.inc [--vs-json vs-transforms.json]
"""
import argparse
import json
import re
from pathlib import Path

import uv_refs


_ALPHA_SOURCES = {"one": 0, "albedo": 1, "fetch": 2}
_KINDS = {"plain": 0, "terrain-layer": 1, "terrain-far": 2, "terrain-far-fade": 3}
_TERRAIN_SLOTS = ("base_slot", "detail_slot")
_STEEP_KEYS = ("threshold", "epsilon", "side_slot")
_ALBEDO_KEYS = ("slot", "u", "v", "alpha", "no_albedo")
_COMP = "xyzw"
# The keys an entry may have, by form: an albedo entry, a "no_albedo" entry, an entry of a terrain
# kind. "kind" selects the form ("plain", the default, for the first two).
_COMMON_KEYS = ("kind", "manual", "evidence")
ENTRY_KEYS = {
    "albedo": _COMMON_KEYS + ("slot", "u", "v", "alpha", "alpha_manual", "alpha_evidence", "light"),
    "no_albedo": _COMMON_KEYS + ("no_albedo",),
    "terrain": _COMMON_KEYS + _TERRAIN_SLOTS + ("steep", "light"),
}
LIGHT_KEYS = ("family", "vertex", "facing_scale", "evidence")
# Light families (light_check.py FAMILIES without "-multi") -> the code of capture::LightFamily
# (material.h pins the two against each other), and the entry kinds each may stand on.
LIGHT_FAMILIES = {"terrain-layer": 1, "terrain-far": 2, "object": 3, "tree-trunk": 4, "plant": 5, "plant-lit": 6,
                  "tree-leaf": 7}
_LIGHT_KINDS = {"terrain-layer": ("terrain-layer",), "terrain-far": ("terrain-far", "terrain-far-fade")}
# No "-multi" form of these exists in the table: the far shaders have one light; the leaves' lights
# are their vertex shader's (one pixel shader for one, two and four).
_ONE_LIGHT_ONLY = ("terrain-far", "tree-leaf")
# The kinds of vertex shader (vs-transforms.json "light"."kind", gen_transform_table.LIGHT_KINDS) a
# family can stand on: capture::LightKindFits of material.h, which refuses the others at the draw.
_VERTEX_KINDS = ("terrain", "stream", "probe", "instance")
_LIGHT_VERTEX_KINDS = {"terrain-layer": ("terrain",), "terrain-far": ("terrain",), "object": ("stream", "probe"),
                       "tree-trunk": ("probe",), "plant": ("instance",), "plant-lit": ("instance",),
                       "tree-leaf": ("probe",)}
ALPHA_KEYS = ("source", "component", "slot", "scale", "divide_by_alpha")  # "slot" only with source "fetch"
# The small term of the steep weights is a literal like 0.0001: this much or more is not one. One
# bound for the table, for terrain_replay.py's reading of a capture (it imports this) and for
# MakeTerrainMaterialInputs (kTerrainSteepEpsMax in terrain_patch.h).
STEEP_EPSILON_MAX = 0.01


def _slot_ok(slot):
    return isinstance(slot, int) and not isinstance(slot, bool) and 0 <= slot <= 31


def _check_entry(ps, e, form):
    """What every entry needs: only its form's keys, "manual": true, a non-empty "evidence"."""
    unknown = [k for k in e if k not in ENTRY_KEYS[form]]
    if unknown:
        raise ValueError(f"{ps}: unknown key {', '.join(sorted(map(str, unknown)))} in "
                         f"{'an' if form == 'albedo' else 'a'} {form} entry")
    if e.get("manual") is not True:
        raise ValueError(f"{ps}: manual is not true")
    evidence = e.get("evidence")
    if not isinstance(evidence, str) or not evidence.strip():
        raise ValueError(f"{ps}: no evidence")


def _scale_ref(ps, text):
    m = re.fullmatch(r"c(\d+)\.([xyzw])", text) if isinstance(text, str) else None
    if not m or int(m.group(1)) > 255:
        raise ValueError(f"{ps}: alpha scale {text!r} is not a pixel constant reference like c15.w")
    return int(m.group(1)) * 4 + _COMP.index(m.group(2))


def _scale_refs(ps, scale):
    """"scale": one reference or a list of one or two -> [ref0, ref1] (-1 absent)."""
    if scale is None:
        return [-1, -1]
    items = scale if isinstance(scale, list) else [scale]
    if not 1 <= len(items) <= 2:
        raise ValueError(f"{ps}: alpha scale list must hold one or two references, not {len(items)}")
    refs = [_scale_ref(ps, t) for t in items]
    if len(refs) == 2 and refs[0] == refs[1]:
        raise ValueError(f"{ps}: alpha scale names {items[0]!r} twice")
    return refs + [-1] * (2 - len(refs))


def _alpha_fields(ps, e):
    """(source, slot, component, scale_ref0, scale_ref1, divide_colour) of an entry's "alpha"."""
    a = e["alpha"]
    src = a.get("source") if isinstance(a, dict) else None
    if not isinstance(src, str) or src not in _ALPHA_SOURCES:
        raise ValueError(f"{ps}: unknown alpha source {src!r}")
    unknown = [k for k in a if k not in ALPHA_KEYS]
    if unknown:
        raise ValueError(f"{ps}: unknown alpha key {', '.join(sorted(map(str, unknown)))}")
    evidence = e.get("alpha_evidence")
    if not isinstance(evidence, str) or not evidence.strip():
        raise ValueError(f"{ps}: alpha without alpha_evidence")
    if src == "one":
        if any(k in a for k in ("scale", "component", "slot", "divide_by_alpha")):
            raise ValueError(f"{ps}: alpha source one takes no slot, component, scale or divide_by_alpha")
        return 0, 0, 3, -1, -1, 0
    comp = a.get("component")
    if comp not in tuple(_COMP):
        raise ValueError(f"{ps}: bad alpha component {comp!r}")
    if src == "fetch":
        if "slot" not in a:
            raise ValueError(f"{ps}: alpha source fetch needs a slot")
        slot = a["slot"]
        if not _slot_ok(slot):
            raise ValueError(f"{ps}: bad alpha fetch slot {slot!r}")
    else:
        if "slot" in a:
            raise ValueError(f"{ps}: an alpha recipe takes a slot only with source fetch (source albedo reads "
                             "the entry's own)")
        slot = e["slot"]
    divide = a.get("divide_by_alpha", False)
    if not isinstance(divide, bool):
        raise ValueError(f"{ps}: divide_by_alpha must be a JSON boolean, not {divide!r}")
    r0, r1 = _scale_refs(ps, a.get("scale"))
    return _ALPHA_SOURCES[src], slot, _COMP.index(comp), r0, r1, int(divide)


def _terrain_fields(ps, e):
    """(base_slot, detail_slot) of a terrain entry."""
    extra = [k for k in _ALBEDO_KEYS if k in e]
    if extra:
        raise ValueError(f"{ps}: a terrain entry takes no {', '.join(extra)}")
    slots = []
    for key in _TERRAIN_SLOTS:
        slot = e.get(key)
        if not _slot_ok(slot):
            raise ValueError(f"{ps}: bad terrain {key} {slot!r}")
        slots.append(slot)
    if slots[0] == slots[1]:
        raise ValueError(f"{ps}: base_slot and detail_slot are both {slots[0]}")
    return slots


def _number(ps, what, v):
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        raise ValueError(f"{ps}: steep {what} {v!r} is not a number")
    return float(v)


def _steep_fields(ps, e, kind, slots):
    """("side_slot", "threshold f", "epsilon f") of a terrain entry: its "steep" object, or -1 and zeros."""
    if "steep" not in e:
        return ["-1", "0.0f", "0.0f"]
    s = e["steep"]
    if kind != "terrain-layer":
        raise ValueError(f"{ps}: steep belongs to a terrain-layer entry, not {kind}")
    if not isinstance(s, dict) or sorted(s) != sorted(_STEEP_KEYS):
        raise ValueError(f"{ps}: steep must have exactly {', '.join(_STEEP_KEYS)}")
    side = s["side_slot"]
    if not _slot_ok(side):
        raise ValueError(f"{ps}: bad steep side_slot {side!r}")
    if side in slots:
        raise ValueError(f"{ps}: steep side_slot {side} is the base map's or the detail texture's slot")
    threshold, epsilon = _number(ps, "threshold", s["threshold"]), _number(ps, "epsilon", s["epsilon"])
    # (a value that is not finite fails both comparisons)
    if not 0.0 < threshold <= 1.0:
        raise ValueError(f"{ps}: steep threshold {threshold!r} is not above 0 and at most 1")
    if not 0.0 < epsilon < STEEP_EPSILON_MAX:
        raise ValueError(f"{ps}: steep epsilon {epsilon!r} is not above 0 and below {STEEP_EPSILON_MAX!r}")
    return [str(side), f"{threshold!r}f", f"{epsilon!r}f"]


def _vertex_lights(vertex):
    """vs-transforms.json as the light pairs are held against it: {shader hash: its "light" object, or
    None for an entry without one}. Refuses a table that is not an object of shader keys, each once."""
    if not isinstance(vertex, dict):
        raise ValueError("the vertex table is not an object")
    out, spelled = {}, {}
    for vs, e in vertex.items():
        if not isinstance(vs, str) or not re.fullmatch(r"0x[0-9A-Fa-f]{16}", vs):
            raise ValueError(f"the vertex table's key {vs!r} is not 0x and 16 hex digits")
        if int(vs, 16) in out:
            raise ValueError(f"the vertex table names {spelled[int(vs, 16)]} and {vs}: the same shader twice")
        spelled[int(vs, 16)] = vs
        light = e.get("light") if isinstance(e, dict) else None
        out[int(vs, 16)] = light if isinstance(light, dict) else None
    return out


def _light_lines(ps, h, e, kind, vertex_lights):
    """The FABLE2_PS_LIGHT line of an entry's "light" and its FABLE2_PS_LIGHT_VS lines.
    `vertex_lights`: _vertex_lights of the vertex table, None when no table was given."""
    light = e["light"]
    if not isinstance(light, dict):
        raise ValueError(f"{ps}: light is not an object")
    unknown = [k for k in light if k not in LIGHT_KEYS]
    if unknown:
        raise ValueError(f"{ps}: unknown light key {', '.join(sorted(map(str, unknown)))}")
    name = light.get("family")
    base = name[:-len("-multi")] if isinstance(name, str) and name.endswith("-multi") else name
    if not isinstance(name, str) or base not in LIGHT_FAMILIES or (base != name and base in _ONE_LIGHT_ONLY):
        raise ValueError(f"{ps}: unknown light family {name!r}")
    kinds = _LIGHT_KINDS.get(base, ("plain",))
    if kind not in kinds:
        raise ValueError(f"{ps}: light family {name} belongs to a {' or '.join(kinds)} entry, not {kind}")
    evidence = light.get("evidence")
    if not isinstance(evidence, str) or not evidence.strip():
        raise ValueError(f"{ps}: light without evidence")
    vertex = light.get("vertex")
    if not isinstance(vertex, list) or not vertex:
        raise ValueError(f"{ps}: light vertex is not a list of vertex shaders")
    seen = {}
    for vs in vertex:
        if not isinstance(vs, str) or not re.fullmatch(r"0x[0-9A-Fa-f]{16}", vs):
            raise ValueError(f"{ps}: light vertex {vs!r} is not 0x and 16 hex digits")
        if int(vs, 16) in seen:
            raise ValueError(f"{ps}: light vertex {seen[int(vs, 16)]} twice")
        seen[int(vs, 16)] = vs
    # The pair against the vertex table: the vertex shader has its "light", of a kind the family
    # stands on.
    if vertex_lights is None:
        raise ValueError(f"{ps}: light needs the vertex table (vs-transforms.json) to hold its vertex shaders against")
    fits = _LIGHT_VERTEX_KINDS[base]
    for value in sorted(seen):
        vs_light = vertex_lights.get(value)
        if vs_light is None:
            raise ValueError(f"{ps}: light vertex 0x{value:016X} has no light in the vertex table")
        vs_kind = vs_light.get("kind")
        if not isinstance(vs_kind, str) or vs_kind not in fits:
            shown = vs_kind if isinstance(vs_kind, str) and vs_kind in _VERTEX_KINDS else repr(vs_kind)
            raise ValueError(f"{ps}: light family {name} needs a vertex shader of kind {' or '.join(fits)}, "
                             f"and 0x{value:016X} is of kind {shown}")
    facing = "0.0f"
    if base == "terrain-layer":
        if "facing_scale" not in light:
            raise ValueError(f"{ps}: light family {name} needs facing_scale")
        scale = light["facing_scale"]
        # (a value that is not finite fails the comparison or is infinite)
        if isinstance(scale, bool) or not isinstance(scale, (int, float)) or not 0.0 < scale < float("inf"):
            raise ValueError(f"{ps}: light facing_scale {scale!r} is not a number above 0")
        facing = f"{float(scale)!r}f"
    elif "facing_scale" in light:
        raise ValueError(f"{ps}: light facing_scale belongs to a terrain-layer family, not {name}")
    lines = [f"FABLE2_PS_LIGHT({h}, {LIGHT_FAMILIES[base]}, {int(base != name)}, {facing})"]
    return lines + [f"FABLE2_PS_LIGHT_VS({h}, 0x{value:016X}ull)" for value in sorted(seen)]


def _axis_fields(ps, axis, a):
    """(interpolator, component, four stage refs) of an albedo entry's "u" or "v": an object with
    "input" and "stages" and nothing else; a stage has "scale" and "offset" and nothing else."""
    if not isinstance(a, dict):
        raise ValueError(f"{ps}: {axis} is not an object with input and stages")
    unknown = [k for k in a if k not in ("input", "stages")]
    if unknown:
        raise ValueError(f"{ps}: unknown key {', '.join(sorted(map(str, unknown)))} in {axis}")
    if "input" not in a:
        raise ValueError(f"{ps}: {axis} has no input")
    stages = a.get("stages")
    if not isinstance(stages, list):
        raise ValueError(f"{ps}: stages of {axis} is not a list")
    for s in stages:
        if not isinstance(s, dict):
            raise ValueError(f"{ps}: a stage of {axis} is not an object")
        unknown = [k for k in s if k not in ("scale", "offset")]
        if unknown:
            raise ValueError(f"{ps}: unknown key {', '.join(sorted(map(str, unknown)))} in a stage of {axis}")
    try:
        if not isinstance(a["input"], str):
            raise ValueError(f"bad interpolator component {a['input']!r}")
        interp, comp = uv_refs.input_comp(a["input"])
        refs = uv_refs.stages(stages, 1)
    except (ValueError, TypeError) as err:   # uv_refs does not know the shader
        raise ValueError(f"{ps}: {axis}: {err}") from None
    return [str(interp), str(comp)] + [str(r) for r in refs]


def _check_hashes(data):
    """Every key is "0x" and 16 hex digits, and no shader is named twice (in another spelling)."""
    seen = {}
    for ps in data:
        if not isinstance(ps, str) or not re.fullmatch(r"0x[0-9A-Fa-f]{16}", ps):
            raise ValueError(f"{ps!r}: a key is not 0x and 16 hex digits")
        if int(ps, 16) in seen:
            raise ValueError(f"{seen[int(ps, 16)]} and {ps}: the same shader twice")
        seen[int(ps, 16)] = ps


def generate(data, vertex=None):
    """The table's text. `vertex`: vs-transforms.json as loaded, which every "light" is held against
    (a table with a "light" is refused without it)."""
    _check_hashes(data)
    vertex_lights = None if vertex is None else _vertex_lights(vertex)
    lines = ["// Generated by tools/xdk_sigmatch/gen_albedo_table.py - do not edit."]
    alpha_lines, terrain_lines, light_lines = [], [], []
    for ps, e in sorted(data.items(), key=lambda kv: int(kv[0], 16)):
        h = f"0x{int(ps, 16):016X}ull"
        if not isinstance(e, dict):
            raise ValueError(f"{ps}: the entry is not an object")
        kind = e.get("kind", "plain")
        if not isinstance(kind, str) or kind not in _KINDS:
            raise ValueError(f"{ps}: unknown kind {kind!r}")
        if kind != "plain":
            base, detail = _terrain_fields(ps, e)
            steep = _steep_fields(ps, e, kind, (base, detail))
            _check_entry(ps, e, "terrain")
            terrain_lines.append(f"FABLE2_PS_TERRAIN({h}, {_KINDS[kind]}, {base}, {detail}, {', '.join(steep)})")
            if "light" in e:
                light_lines += _light_lines(ps, h, e, kind, vertex_lights)
            continue
        if any(k in e for k in _TERRAIN_SLOTS + ("steep",)):
            raise ValueError(f"{ps}: base_slot, detail_slot and steep belong to a terrain kind")
        if "no_albedo" in e:
            if "alpha" in e:
                raise ValueError(f"{ps}: alpha on a no_albedo entry")
            extra = [k for k in ("slot", "u", "v") if k in e]
            if extra:
                raise ValueError(f"{ps}: a no_albedo entry takes no {', '.join(extra)}")
            _check_entry(ps, e, "no_albedo")
            reason = e["no_albedo"]
            if not isinstance(reason, str) or not reason.strip():
                raise ValueError(f"{ps}: no_albedo is not a reason (a non-empty string), but {reason!r}")
            lines.append(f"FABLE2_PS_NO_ALBEDO({h})")
            continue
        _check_entry(ps, e, "albedo")
        slot = e.get("slot")
        if not _slot_ok(slot):
            raise ValueError(f"{ps}: bad texture fetch slot {slot!r}")
        fields = [str(slot)]
        for axis in ("u", "v"):
            fields += _axis_fields(ps, axis, e.get(axis))
        if "alpha" in e and e.get("alpha_manual") is not True:
            raise ValueError(f"{ps}: alpha_manual is not true")
        lines.append(f"FABLE2_PS_ALBEDO({h}, {', '.join(fields)})")
        if "alpha" in e:
            alpha_lines.append(f"FABLE2_PS_ALPHA({h}, {', '.join(str(f) for f in _alpha_fields(ps, e))})")
        if "light" in e:
            light_lines += _light_lines(ps, h, e, kind, vertex_lights)
    return "\n".join(lines + alpha_lines + terrain_lines + light_lines) + "\n"


def _no_repeats(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError(f"{key}: the key is written twice in one object of the file")
        out[key] = value
    return out


def load(path):
    """The table file as generate() takes it. A key written twice in one object (two entries of one
    shader, a field given twice) is refused: json would keep the last and say nothing."""
    return json.loads(Path(path).read_text(), object_pairs_hook=_no_repeats)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--vs-json", help="the vertex table the light pairs are held against "
                                      "(default: vs-transforms.json in the folder of --json)")
    a = ap.parse_args(argv)
    data = load(a.json)
    vs_json = Path(a.vs_json) if a.vs_json else Path(a.json).parent / "vs-transforms.json"
    if not vs_json.is_file():
        raise FileNotFoundError(f"the vertex table {vs_json} is missing: the light pairs are held against it "
                                "(name another file with --vs-json)")
    vertex = load(vs_json)
    Path(a.out).write_text(generate(data, vertex))


if __name__ == "__main__":
    main()
