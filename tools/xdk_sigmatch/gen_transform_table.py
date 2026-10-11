"""vs-transforms.json -> src/native/capture/vs_transform_table.inc.

Per shader with a transform ("base"):
  FABLE2_VS_TRANSFORM(hash, base, layout, pos_fetch, deformed)
and, when the entry has them (frame-map section 9):
  FABLE2_VS_POS_SWIZZLE(hash, swizzle)   "pos_swizzle": "yxw1" (vfetch notation)
  FABLE2_VS_SKIN(hash, index_fetch, weight_fetch, r0, r1, r2, s0, s1, s2, bones, ic0, wc0, ic1, wc1, ic2, wc2, ic3, wc3)
      rigid: "skin": {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}
      weighted: "skin": {"index_fetch": 1, "weight_fetch": 2, "row_fetches": [3, 4, 5],
                         "row_swizzles": [...] (optional), "pairs": [["x", "z"], ...]}  (1 to 4 index/weight pairs)
      Row swizzles are vfetch swizzle bits in hex (0x0 = keep); unused pairs are 0; weight_fetch -1 = rigid.
  FABLE2_VS_INSTANCE(hash, r0, r1, r2, s0, s1, s2, inv_count_ref, count_ref, first_ref, bias, offset_ref,
                     cut_eye_ref, cut_dist2_ref)
      "instance": {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "row_swizzles": [...] (optional),
                   "inv_count": "c12.x", "count": "c12.y", "first": "c12.z", "bias": 0.5, "offset": "c7.xyz",
                   "cut": {"eye": "c9.xyz", "dist2": "c13.z"} (optional)}
      An entry has "skin" or "instance", never both, and either needs "base"; a skin with "pairs"
      needs "weight_fetch". Each of these raises ValueError naming the shader.
  "wind" (the foliage sub-project: spec Design 5) gets no line here: `--wind-out` writes
      src/native/capture/wind_table.inc from it (generate_wind; the lines' form is that file's head).
        "wind": {"family": "trunk" | "leaf" | "plant", "role": "scene" | "caster",
                 "reg": the family's vertex registers ([15, 16, 17] or [10, 21, 22, 25, 26]),
                 "lit": {name: value}, "lit_at": {name: "c253.x"}, "lit_reg": the registers of lit_at, ascending,
                 "elements": [{"name", "fetch", "format", "offset", "source"}, ...], "evidence"}
      "lit" names every literal the family's formula reads (WIND_FAMILIES; light_formulas.WIND_LITERAL_ORDER),
      each a single-precision value, and "lit_at" the register and component of the shader's own literal
      table that holds it. "elements" are the family's, in its order (a trunk: weights; a leaf: pivot,
      weights, flutter; a plant: none), each a vertex fetch by its ordinal with the format and offset the dump
      shows and the SOURCE components the formula reads, as a light's. Anything else is refused: an unknown
      family, a literal too many or too few, a value that is not a single-precision number, two values at one
      place, an entry that is not "deformed", a plant without "instance" or a tree with it.
An entry with "base2" gets no line: matrix_finder.py --products writes it for a shader whose
transform is the product of two constant windows, which FABLE2_VS_TRANSFORM (one base) cannot hold.
      mesh_fetch becomes the transform line's pos_fetch; refs are register * 4 + component (the offset's and
      the eye's x). "cut": the shader drops a vertex whose squared distance from `eye` is above `dist2`
      (frame-map section 12); -1, -1 without it.
  FABLE2_VS_TERRAIN(hash, grid, cell, height_scale, origin, tex_offset, tex_scale,
                    patch_offset, height_fetch)
      "terrain": {"grid": "c11.xy", ..., "patch_offset": null | "c113.x", "height_fetch": 16}
      registers as register * 4 + component; -1 = no patch offset.
  FABLE2_VS_UV(hash, interp, comp, fetch_index, src_comp, xenos_format, offset_dwords, s0, o0, s1, o1)
      "uv": {"o0.x": {"fetch": 2, "src": "y", "format": 31, "offset": 3, "stages": [...]}, ...}
      Stage refs use uv_refs notation with bank 0 (vertex constants).
  FABLE2_VS_LIGHT(hash, kind, normal_fetch, normal_format, normal_offset, normal_source, second_fetch,
                  second_format, second_offset, second_source, a0, scale_ref, power)
      "light": what the light of a draw reads on the vertex side (docs/native-renderer/lighting-formulas.md),
      by kind: 1 "terrain" (nothing: the normal is the heightmap's), 2 "stream" (the vertex colour is a
      stream of its own), 3 "probe" (the colour is built from four weights per vertex and per-draw
      constants), 4 "instance" (the colour is the instance's).
        stream:   {"kind", "normal", "colour", "power", "scale" (optional), "evidence"}
        probe:    {"kind", "normal", "weights", "a0", "scale" (optional), "evidence"}
        instance: {"kind", "normal", "colour", "evidence"}   (the entry has "instance")
        terrain:  {"kind", "evidence"}                       (the entry has "terrain")
      "normal", "colour", "weights": {"fetch", "format", "offset", "source"}, a vertex fetch by its
      ordinal with the Xenos format and the offset in dwords the dump shows (the runtime refuses the
      entry on a shader whose fetch is another one). The normal is 16 (10_11_11) or 32
      (16_16_16_16_FLOAT), a colour 6 (8_8_8_8), the weights 38 (32_32_32_32_FLOAT).
      "source": the element's SOURCE components (x to w of the GPU's fetch, before the fetch's
      destination swizzle, which only says where the shader's register gets them and differs between
      shaders that use one element the same way) in the order the light's formula reads them, as
      "pos_swizzle" does for a position: a normal's x, y, z ("xyz" for the 10_11_11 element, "yxw" for
      the plants' halves, the first three in memory); a colour's red, green, blue and, where the reader
      keeps it, alpha ("zyxw" for the colour stream, "zyx" for an instance's: four components for
      kind "stream", three for kind "instance", anything else is refused); the four weights in the
      order of the probe constants' components ("xyzw"). Each component at most once, and only
      components the format has (x, y, z for 10_11_11). In the line: two bits a component from bit 0,
      the number of components above them (0x324 = xyz, 0x331 = yxw, 0x4C6 = zyxw, 0x4E4 = xyzw; 0x0
      without an element). "a0": the source component of the position fetch
      that holds the index of the draw's probe set; "scale": the constant the shader multiplies its world
      normal by ("c7.w"; register * 4 + component in the line); "power": the literal exponent of the
      colour stream. Absent: fetch -1 with 0, 0; a0 -1; scale -1; power 0.0f. The pixel shaders a vertex
      shader's kind was checked with are listed in ps-albedo.json ("light"."vertex").

An entry is refused (ValueError naming the shader) rather than read loosely: it and its "skin",
"instance", "terrain" and "uv" objects have only the keys listed below (the committed table's, plus
what matrix_finder.py writes), "layout" is "dot" or "combine", "base" is a JSON integer 0 to 252
(four registers), a key is "0x" and 16 hex digits (written to the table in upper case) and names
its shader once, whatever the letters' case (load() also refuses a key written twice in the file).
A "light" has a kind, its kind's keys and no other, a non-empty "evidence", and needs "base".
Nothing is converted on the way: a fetch ordinal ("pos_fetch", -1 = the default; "fetch",
"index_fetch", "weight_fetch", "mesh_fetch", "row_fetches") is a JSON integer 0 to 127, a "uv"
"format" 0 to 63 and "offset" 0 to 255 dwords, "height_fetch" a texture fetch slot 0 to 31,
"deformed" a JSON boolean, "bias" a finite JSON number; 2.5, true, "2" and "false" are refused.
"""
import argparse
import json
import math
import re
import struct
import sys
from pathlib import Path

# Ensure we can import uv_refs from the same directory
sys.path.insert(0, str(Path(__file__).parent))

import uv_refs

_SWZ = {"x": 0, "y": 1, "z": 2, "w": 3, "0": 4, "1": 5, "_": 7}
_COMP = "xyzw"
_LAYOUTS = {"dot": 0, "combine": 1}
# The keys an entry and its objects may have: those of the committed table; "base2", "score" and
# "samples" are what matrix_finder.py writes beside "base", "layout" and "pos_fetch".
ENTRY_KEYS = ("base", "layout", "pos_fetch", "pos_swizzle", "deformed", "skin", "instance", "terrain", "uv",
              "light", "wind", "manual", "evidence", "uv_manual", "uv_evidence", "bind_pose_evidence", "finder", "rejected",
              "task11_rejected", "base2", "score", "samples")
SKIN_KEYS = ("index_fetch", "weight_fetch", "index_component", "row_fetches", "row_swizzles", "pairs")
INSTANCE_KEYS = ("mesh_fetch", "row_fetches", "row_swizzles", "inv_count", "count", "first", "bias", "offset", "cut")
TERRAIN_KEYS = ("grid", "cell", "height_scale", "origin", "tex_offset", "tex_scale", "patch_offset", "height_fetch")
UV_KEYS = ("fetch", "src", "format", "offset", "stages")
LIGHT_KEYS = ("kind", "normal", "colour", "weights", "a0", "scale", "power", "evidence")
LIGHT_FETCH_KEYS = ("fetch", "format", "offset", "source")
# kind -> (code of capture::LightVertexKind, the keys it needs, the keys it may have besides).
LIGHT_KINDS = {
    "terrain": (1, (), ()),
    "stream": (2, ("normal", "colour", "power"), ("scale",)),
    "probe": (3, ("normal", "weights", "a0"), ("scale",)),
    "instance": (4, ("normal", "colour"), ()),
}
# The Xenos vertex formats the light's readers decode, per fetch.
_LIGHT_FORMATS = {"normal": {16: "10_11_11", 32: "16_16_16_16_FLOAT"}, "colour": {6: "8_8_8_8"},
                  "weights": {38: "32_32_32_32_FLOAT"}}
# How many source components an element's selector names (a normal's three, a colour with or without
# its alpha, the four weights), and the components a format has. A colour's number is its kind's
# (_COLOUR_SOURCE_COUNT): the readers hold a colour stream's selector to four components and an
# instance colour's to three (normal_decode.h, ColourElementSupported and
# InstanceColourElementSupported), so the other number would be a table that generates and a draw
# without colours.
_LIGHT_SOURCE_COUNTS = {"normal": (3,), "colour": (3, 4), "weights": (4,)}
_LIGHT_SOURCE_WORDS = {(3,): "three", (3, 4): "three or four", (4,): "four"}
_COLOUR_SOURCE_COUNT = {"stream": 4, "instance": 3}
_FORMAT_COMPONENTS = {16: "xyz", 32: "xyzw", 6: "xyzw", 38: "xyzw"}


def source_code(text):
    """'yxw' -> 0x331: the source components two bits each from bit 0, their number above them."""
    return (len(text) << 8) | sum(_COMP.index(c) << (2 * i) for i, c in enumerate(text))


def _only_keys(vs, what, obj, allowed):
    """`obj` is a JSON object with no key outside `allowed` (`what`: "" for the entry itself)."""
    if not isinstance(obj, dict):
        raise ValueError(f"{vs}: {what or 'the entry'} is not an object")
    unknown = [k for k in obj if k not in allowed]
    if unknown:
        raise ValueError(f"{vs}: unknown {what + ' ' if what else ''}key {', '.join(sorted(map(str, unknown)))}")


def _check_keys(vs, e):
    _only_keys(vs, "", e, ENTRY_KEYS)
    for what, allowed in (("skin", SKIN_KEYS), ("instance", INSTANCE_KEYS), ("terrain", TERRAIN_KEYS)):
        if what in e:
            _only_keys(vs, what, e[what], allowed)
    if "uv" in e:
        if not isinstance(e["uv"], dict):
            raise ValueError(f"{vs}: uv is not an object")
        for u in e["uv"].values():
            _only_keys(vs, "uv", u, UV_KEYS)


def _check_hashes(data):
    """Every key is "0x" and 16 hex digits (gen_albedo_table.py's rule: the key becomes a C++
    literal), and no shader is named twice (in another case)."""
    seen = {}
    for vs in data:
        if not isinstance(vs, str) or not re.fullmatch(r"0x[0-9A-Fa-f]{16}", vs):
            raise ValueError(f"{vs!r}: a key is not 0x and 16 hex digits")
        value = int(vs, 16)
        if value in seen:
            raise ValueError(f"{seen[value]} and {vs}: the same shader twice")
        seen[value] = vs


def _int(what, v, lo, hi):
    """`v` as it stands if it is a JSON integer from lo to hi (no float, string or boolean)."""
    if not isinstance(v, int) or isinstance(v, bool) or not lo <= v <= hi:
        raise ValueError(f"{what} {v!r} is not an integer from {lo} to {hi}")
    return v


def _fetch(what, v):
    """A vertex fetch ordinal: the runtime keeps it in a signed byte."""
    return _int(what, v, 0, 127)


def _rows(what, rows):
    if not isinstance(rows, list) or len(rows) != 3:
        raise ValueError(f"{what} row_fetches {rows!r} is not a list of three fetches")
    return [_fetch(f"{what} row_fetches", r) for r in rows]


def _component(what, c):
    if not isinstance(c, str) or len(c) != 1 or c not in _COMP:
        raise ValueError(f"{what} {c!r} is not one of x, y, z, w")
    return _COMP.index(c)


def swizzle_bits(text):
    """'yxw1' -> the vfetch destination swizzle (3 bits per component, x first)."""
    if len(text) != 4 or any(c not in _SWZ for c in text):
        raise ValueError(f"bad swizzle {text!r}")
    return sum(_SWZ[c] << (3 * i) for i, c in enumerate(text))


def reg_comp(text, pair=False):
    """'c47.zw' -> 47 * 4 + 2; a pair must name two consecutive components."""
    m = re.fullmatch(r"c([0-9]+)\.([xyzw]{1,2})", text) if isinstance(text, str) else None
    if not m or int(m.group(1)) > 255:
        raise ValueError(f"bad register {text!r}")
    comps = [_COMP.index(c) for c in m.group(2)]
    if pair and (len(comps) != 2 or comps[1] != comps[0] + 1):
        raise ValueError(f"{text!r} is not a pair of consecutive components")
    if not pair and len(comps) != 1:
        raise ValueError(f"{text!r} names more than one component")
    return int(m.group(1)) * 4 + comps[0]


def _swizzles(items):
    items = ["", "", ""] if items is None else items
    if not isinstance(items, list) or len(items) != 3 or not all(isinstance(s, str) for s in items):
        raise ValueError("three row swizzles are required")
    return ["0x%X" % (swizzle_bits(s) if s else 0) for s in items]


def _skin_line(vs, h, s):
    """`vs`: the key, for messages; `h`: the key as the table writes it."""
    rows = _rows("skin", s.get("row_fetches"))
    if "pairs" in s and "weight_fetch" not in s:
        raise ValueError(f"{vs}: skin has pairs but no weight_fetch")
    if "weight_fetch" in s:
        pairs = s.get("pairs")
        if not isinstance(pairs, list) or not 1 <= len(pairs) <= 4:
            raise ValueError(f"{vs}: skin pairs must be 1 to 4 [index, weight] pairs")
        if not all(isinstance(p, list) and len(p) == 2 for p in pairs):
            raise ValueError(f"{vs}: skin pairs {pairs!r} are not [index component, weight component] pairs")
        comps = [(_component("skin pairs component", i), _component("skin pairs component", w)) for i, w in pairs]
        weight_fetch = _fetch("skin weight_fetch", s["weight_fetch"])
    else:
        comps = [(_component("skin index_component", s.get("index_component")), 0)]
        weight_fetch = -1
    flat = [c for pair in comps + [(0, 0)] * (4 - len(comps)) for c in pair]
    fields = [_fetch("skin index_fetch", s.get("index_fetch")), weight_fetch, *rows,
              *_swizzles(s.get("row_swizzles")), len(comps), *flat]
    return f"FABLE2_VS_SKIN({h}ull, {', '.join(str(f) for f in fields)})"


def cut_refs(inst):
    """An "instance" entry's optional "cut" as (eye x reference, squared-distance reference); (-1, -1) without."""
    if "cut" not in inst:
        return -1, -1
    cut = inst["cut"]
    if not isinstance(cut, dict) or set(cut) != {"eye", "dist2"}:
        raise ValueError('instance cut must be {"eye": "c<N>.xyz", "dist2": "c<N>.<c>"}')
    m = re.fullmatch(r"c(\d+)\.xyz", cut["eye"]) if isinstance(cut["eye"], str) else None
    if not m or int(m.group(1)) > 255 or not isinstance(cut["dist2"], str):
        raise ValueError('instance cut must be {"eye": "c<N>.xyz", "dist2": "c<N>.<c>"}')
    return int(m.group(1)) * 4, reg_comp(cut["dist2"])


def _instance_line(vs, h, e):
    inst = e["instance"]
    rows = _rows("instance", inst.get("row_fetches"))
    offset = inst.get("offset")
    m = re.fullmatch(r"c([0-9]+)\.xyz", offset) if isinstance(offset, str) else None
    if not m or int(m.group(1)) > 255:
        raise ValueError(f"{vs}: instance offset must be c<N>.xyz")
    bias = inst.get("bias")
    if isinstance(bias, bool) or not isinstance(bias, (int, float)) or bias != bias or bias in (float("inf"), float("-inf")):
        raise ValueError(f"{vs}: instance bias {bias!r} is not a finite number")
    cut = cut_refs(inst)
    fields = [*rows, *_swizzles(inst.get("row_swizzles")), reg_comp(inst.get("inv_count")), reg_comp(inst.get("count")),
              reg_comp(inst.get("first")), repr(float(bias)) + "f", int(m.group(1)) * 4, *cut]
    return f"FABLE2_VS_INSTANCE({h}ull, {', '.join(str(f) for f in fields)})"


def _uv_line(vs, h, name, u):
    interp, comp = uv_refs.input_comp(name) if isinstance(name, str) else (None, None)
    src = _component("uv src", u.get("src"))
    stages = u.get("stages", [])
    if not isinstance(stages, list):
        raise ValueError(f"{vs}: uv stages {stages!r} is not a list")
    for s in stages:
        if not isinstance(s, dict) or any(k not in ("scale", "offset") for k in s):
            raise ValueError(f"{vs}: uv stage {s!r} is not an object with scale and offset")
    refs = ", ".join(str(r) for r in uv_refs.stages(stages, 0))
    return (f"FABLE2_VS_UV({h}ull, {interp}, {comp}, {_fetch('uv fetch', u.get('fetch'))}, {src}, "
            f"{_int('uv format', u.get('format'), 0, 63)}, {_int('uv offset', u.get('offset'), 0, 255)}, {refs})")


def _light_fetch(vs, what, f, kind=None):
    """(fetch ordinal, format, offset in dwords, source code) of a light's "normal", "colour" or
    "weights". `kind`: the light's kind, which says how many components a colour's source names."""
    _only_keys(vs, f"light {what}", f, LIGHT_FETCH_KEYS)
    fetch = _fetch(f"light {what} fetch", f.get("fetch"))
    offset = _int(f"light {what} offset", f.get("offset"), 0, 255)
    fmt, known = f.get("format"), _LIGHT_FORMATS[what]
    if isinstance(fmt, bool) or fmt not in known:
        raise ValueError(f"light {what} format {fmt!r} is not "
                         + " or ".join(f"{code} ({name})" for code, name in known.items()))
    source, counts, components = f.get("source"), _LIGHT_SOURCE_COUNTS[what], _FORMAT_COMPONENTS[fmt]
    if (not isinstance(source, str) or len(source) not in counts or len(set(source)) != len(source)
            or any(c not in components for c in source)):
        raise ValueError(f"light {what} source {source!r} is not {_LIGHT_SOURCE_WORDS[counts]} of "
                         f"{', '.join(components)}, each once")
    if what == "colour" and kind in _COLOUR_SOURCE_COUNT and len(source) != _COLOUR_SOURCE_COUNT[kind]:
        a = "an" if kind[0] in "aeiou" else "a"
        raise ValueError(f"{vs}: {a} {kind} light's colour source {source!r} is not "
                         f"{_LIGHT_SOURCE_WORDS[(_COLOUR_SOURCE_COUNT[kind],)]} of {', '.join(components)}")
    return fetch, fmt, offset, f"0x{source_code(source):X}"


def _light_line(vs, h, e):
    light = e["light"]
    _only_keys(vs, "light", light, LIGHT_KEYS)
    kind = light.get("kind")
    if not isinstance(kind, str) or kind not in LIGHT_KINDS:
        raise ValueError(f"{vs}: unknown light kind {kind!r}")
    code, needs, may = LIGHT_KINDS[kind]
    a = "an" if kind[0] in "aeiou" else "a"
    for key in LIGHT_KEYS[1:-1]:
        if key in light and key not in needs + may:
            raise ValueError(f"{vs}: {a} {kind} light takes no {key}")
        if key in needs and key not in light:
            raise ValueError(f"{vs}: {a} {kind} light needs {key}")
    evidence = light.get("evidence")
    if not isinstance(evidence, str) or not evidence.strip():
        raise ValueError(f"{vs}: light without evidence")
    # The kind and the entry's own form: a terrain shader has no vertex streams, an instancing one
    # takes its colour from the instance stream.
    for form in ("terrain", "instance"):
        if kind == form and form not in e:
            raise ValueError(f"{vs}: {a} {kind} light belongs to an entry with {form}")
        if kind != form and form in e:
            raise ValueError(f"{vs}: {a} {kind} light does not belong to an entry with {form}")
    none = (-1, 0, 0, "0x0")
    normal = _light_fetch(vs, "normal", light["normal"]) if "normal" in light else none
    second = next((_light_fetch(vs, k, light[k], kind) for k in ("colour", "weights") if k in light), none)
    a0 = _component("light a0", light["a0"]) if "a0" in light else -1
    scale = reg_comp(light["scale"]) if "scale" in light else -1
    power = "0.0f"
    if "power" in light:
        value = light["power"]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0.0 < value < float("inf"):
            raise ValueError(f"{vs}: light power {value!r} is not a number above 0")
        power = repr(float(value)) + "f"
    fields = [code, *normal, *second, a0, scale, power]
    return f"FABLE2_VS_LIGHT({h}ull, {', '.join(str(f) for f in fields)})"


# --- the wind table ---------------------------------------------------------------------------------
#
# family -> (code of capture::WindFamily, its vertex registers in WindInputs' order, its literals in the
# order the table and the native constants keep them, its elements as (name, Xenos format, number of
# source components)). The names and their order are light_formulas.WIND_LITERAL_ORDER's
# (tests/test_gen_transform_table.py holds the two together).
_WIND_TRUNK_LITERALS = ("half", "base", "rate1", "rate2", "two_pi", "quarter", "lean", "gust", "phase_x", "phase_y", "lo",
                        "amp", "along", "across", "rate0", "zero", "sel_zero", "sel_one")
WIND_FAMILIES = {
    "trunk": (1, (15, 16, 17), _WIND_TRUNK_LITERALS, (("weights", 6, 4),)),
    "leaf": (2, (15, 16, 17),
             _WIND_TRUNK_LITERALS + ("hundred", "slack", "phase_f", "flutter_x", "flutter_y", "one", "back", "push_gust",
                                     "lift_gust", "rate_f"),
             (("pivot", 32, 3), ("weights", 6, 4), ("flutter", 6, 3))),
    "plant": (3, (10, 21, 22, 25, 26), ("lo", "two_pi", "inv_two_pi", "gain", "height", "half", "unit"), ()),
}
WIND_KEYS = ("family", "role", "reg", "lit_reg", "lit", "lit_at", "elements", "evidence")
WIND_ELEMENT_KEYS = ("name", "fetch", "format", "offset", "source")
WIND_ELEMENT_SLOTS = ("pivot", "weights", "flutter")    # capture::kWindPivot, kWindWeights, kWindFlutter
WIND_REGISTERS = 5                                      # capture::kWindRegisters
WIND_NO_REGISTER = 255                                  # capture::kWindNoRegister
WIND_LITERALS_MAX = 28                                  # the leaves': seven rows of four in the native constants
WIND_HEADER = """\
// Generated by tools/xdk_sigmatch/gen_transform_table.py --wind-out - do not edit.
// FABLE2_WIND(hash, family, caster, reg0, reg1, reg2, reg3, reg4,
//             pivot_fetch, pivot_format, pivot_offset, pivot_source,
//             weights_fetch, weights_format, weights_offset, weights_source,
//             flutter_fetch, flutter_format, flutter_offset, flutter_source)
//   family: 1 trunk, 2 leaf, 3 plant (capture::WindFamily); caster: 1 for a shadow caster's shader. regN: the
//   vertex registers the family's formula reads (255: none). An element: the fetch's ordinal, its Xenos format,
//   its offset in dwords and its source components (two bits a component from bit 0, their number above them:
//   0x331 = yxw, 0x4E4 = xyzw, 0x324 = xyz); -1, 0, 0, 0x0: the family has none there.
// FABLE2_WIND_LITERAL(hash, slot, register, component, bits)
//   One line a literal the formula reads: `slot` is its place in the family's order
//   (light_formulas.WIND_LITERAL_ORDER: the order of the native constants), `bits` its single-precision value,
//   which the shader object's own literal table must hold at register.component, bit for bit (spec ruling 9).
"""


def _wind_bits(name, value):
    """The bits of a literal's value, which must be a single-precision number."""
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"wind literal {name} {value!r} is not a finite JSON number")
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    if struct.unpack("<f", struct.pack("<I", bits))[0] != value:
        raise ValueError(f"wind literal {name} {value!r} is not a single-precision value")
    return bits


def _wind_lines(vs, h, e):
    """The lines of an entry's "wind" member (spec Design 5): one FABLE2_WIND and one FABLE2_WIND_LITERAL a
    literal. Raises ValueError for a member that is not exactly what its family's formula reads."""
    w = e["wind"]
    _only_keys(vs, "wind", w, WIND_KEYS)
    missing = [k for k in WIND_KEYS if k not in w]
    if missing:
        raise ValueError(f"{vs}: wind lacks {', '.join(missing)}")
    family = w["family"]
    if not isinstance(family, str) or family not in WIND_FAMILIES:
        raise ValueError(f"{vs}: unknown wind family {family!r}")
    code, regs, names, elements = WIND_FAMILIES[family]
    role = w["role"]
    if role not in ("scene", "caster"):
        raise ValueError(f"{vs}: wind role {role!r} is not scene or caster")
    if not isinstance(w["evidence"], str) or not w["evidence"].strip():
        raise ValueError(f"{vs}: wind without evidence")
    if e.get("deformed") is not True:
        raise ValueError(f"{vs}: a wind member belongs to a deformed entry")
    if (family == "plant") != ("instance" in e):
        raise ValueError(f"{vs}: a {family}'s wind does not belong to an entry "
                         + ("without instance" if family == "plant" else "with instance"))
    if w["reg"] != list(regs):
        raise ValueError(f"{vs}: wind reg {w['reg']!r} is not the {family}'s {list(regs)}")
    lit, lit_at = w["lit"], w["lit_at"]
    for what, obj in (("lit", lit), ("lit_at", lit_at)):
        if not isinstance(obj, dict) or set(obj) != set(names):
            odd = sorted(set(obj) ^ set(names)) if isinstance(obj, dict) else []
            raise ValueError(f"{vs}: wind {what} does not name the {family}'s literals ({', '.join(odd)})")
    places, at_place, lines = {}, {}, []
    for slot, name in enumerate(names):
        bits = _wind_bits(name, lit[name])
        place = reg_comp(lit_at[name])
        if at_place.setdefault(place, bits) != bits:
            raise ValueError(f"{vs}: wind literal {name} shares {lit_at[name]} with another value")
        places[name] = place
        lines.append(f"FABLE2_WIND_LITERAL({h}ull, {slot}, {place // 4}, {place % 4}, 0x{bits:08X}u)")
    if w["lit_reg"] != sorted({p // 4 for p in places.values()}):
        raise ValueError(f"{vs}: wind lit_reg {w['lit_reg']!r} is not the registers of lit_at")
    given = w["elements"]
    if not isinstance(given, list) or [g.get("name") if isinstance(g, dict) else None for g in given] != [n for n, _, _ in elements]:
        raise ValueError(f"{vs}: wind elements are not the {family}'s ({', '.join(n for n, _, _ in elements) or 'none'})")
    fields = {}
    for g, (name, fmt, count) in zip(given, elements):
        _only_keys(vs, f"wind element {name}", g, WIND_ELEMENT_KEYS)
        fetch = _fetch(f"wind {name} fetch", g.get("fetch"))
        offset = _int(f"wind {name} offset", g.get("offset"), 0, 255)
        if isinstance(g.get("format"), bool) or g.get("format") != fmt:
            raise ValueError(f"{vs}: wind {name} format {g.get('format')!r} is not {fmt}")
        source = g.get("source")
        if (not isinstance(source, str) or len(source) != count or len(set(source)) != count
                or any(c not in _COMP for c in source)):
            raise ValueError(f"{vs}: wind {name} source {source!r} is not {count} of x, y, z, w, each once")
        fields[name] = f"{fetch}, {fmt}, {offset}, 0x{source_code(source):X}"
    padded = list(regs) + [WIND_NO_REGISTER] * (WIND_REGISTERS - len(regs))
    head = (f"FABLE2_WIND({h}ull, {code}, {int(role == 'caster')}, {', '.join(str(r) for r in padded)}, "
            + ", ".join(fields.get(name, "-1, 0, 0, 0x0") for name in WIND_ELEMENT_SLOTS) + ")")
    return [head] + lines


def generate_wind(data):
    """The text of src/native/capture/wind_table.inc: the lines of every entry with a "wind" member, by hash."""
    _check_hashes(data)
    lines = []
    for vs, e in sorted(data.items(), key=lambda kv: int(kv[0], 16)):
        if not isinstance(e, dict) or "wind" not in e:
            continue
        try:
            _check_keys(vs, e)
            if "base" not in e or "base2" in e:
                raise ValueError(f"{vs}: wind entry without base")
            lines.extend(_wind_lines(vs, f"0x{int(vs, 16):016X}", e))
        except ValueError as err:
            if str(err).startswith(f"{vs}: "):
                raise
            raise ValueError(f"{vs}: {err}") from None
        except (KeyError, TypeError, IndexError, AttributeError) as err:
            raise ValueError(f"{vs}: malformed entry ({type(err).__name__}: {err})") from None
    return WIND_HEADER + "\n".join(lines) + ("\n" if lines else "")


def generate(data):
    _check_hashes(data)
    lines = ["// Generated by tools/xdk_sigmatch/gen_transform_table.py - do not edit."]
    extras = []
    for vs, e in sorted(data.items(), key=lambda kv: int(kv[0], 16)):
        try:
            _entry_lines(vs, e, lines, extras)
        except ValueError as err:   # every message names its shader
            if str(err).startswith(f"{vs}: "):
                raise
            raise ValueError(f"{vs}: {err}") from None
        except (KeyError, TypeError, IndexError, AttributeError) as err:
            raise ValueError(f"{vs}: malformed entry ({type(err).__name__}: {err})") from None
    return "\n".join(lines + extras) + "\n"


def _entry_lines(vs, e, lines, extras):
    """Appends one entry's lines. Raises ValueError (generate() puts the shader in front if it is missing)."""
    h = f"0x{int(vs, 16):016X}"   # the key as the table writes it
    _check_keys(vs, e)
    for layout_key in ("skin", "instance", "light", "wind"):
        if layout_key in e and "base" not in e:
            raise ValueError(f"{vs}: {layout_key} entry without base")
    if "skin" in e and "instance" in e:
        raise ValueError(f"{vs}: entry has both skin and instance")
    # No transform (a rejected record), or a two-window product from the finder ("base2"): no line.
    if "base" not in e or "base2" in e:
        return
    base = e["base"]
    if not isinstance(base, int) or isinstance(base, bool) or not 0 <= base <= 252:
        raise ValueError(f"{vs}: base {base!r} is not an integer from 0 to 252")
    layout = e.get("layout")
    if not isinstance(layout, str) or layout not in _LAYOUTS:
        raise ValueError(f"{vs}: layout {layout!r} is not dot or combine")
    layout = _LAYOUTS[layout]
    # deformed: the shader moves the fetched position (skinning, displacement)
    # before the transform, so the record draws it undeformed.
    deformed = e.get("deformed", False)
    if not isinstance(deformed, bool):
        raise ValueError(f"{vs}: deformed {deformed!r} is not a JSON boolean")
    pos_fetch = _int("pos_fetch", e["pos_fetch"], -1, 127) if "pos_fetch" in e else -1
    if "instance" in e:
        mesh = _fetch("instance mesh_fetch", e["instance"].get("mesh_fetch"))
        if "pos_fetch" in e and pos_fetch != mesh:
            raise ValueError(f"{vs}: pos_fetch disagrees with instance mesh_fetch")
        pos_fetch = mesh
    own = []   # the entry's extra lines: added only when the whole entry is sound
    if "pos_swizzle" in e:
        swizzle = e["pos_swizzle"]
        if not isinstance(swizzle, str):
            raise ValueError(f"{vs}: bad swizzle {swizzle!r}")
        own.append(f"FABLE2_VS_POS_SWIZZLE({h}ull, 0x{swizzle_bits(swizzle):X})")
    if "skin" in e:
        own.append(_skin_line(vs, h, e["skin"]))
    if "instance" in e:
        own.append(_instance_line(vs, h, e))
    if "terrain" in e:
        t = e["terrain"]
        missing = [k for k in TERRAIN_KEYS if k not in t]
        if missing:
            raise ValueError(f"{vs}: terrain lacks {', '.join(missing)}")
        po = t["patch_offset"]
        fields = [reg_comp(t["grid"], pair=True), reg_comp(t["cell"], pair=True), reg_comp(t["height_scale"]),
                  reg_comp(t["origin"], pair=True), reg_comp(t["tex_offset"], pair=True),
                  reg_comp(t["tex_scale"], pair=True), -1 if po is None else reg_comp(po),
                  _int("terrain height_fetch", t["height_fetch"], 0, 31)]
        own.append(f"FABLE2_VS_TERRAIN({h}ull, {', '.join(str(f) for f in fields)})")
    for name, u in sorted(e.get("uv", {}).items()):
        own.append(_uv_line(vs, h, name, u))
    if "light" in e:
        own.append(_light_line(vs, h, e))
    if "wind" in e:
        _wind_lines(vs, h, e)       # held here too: a wind member that is refused stops this table as well
    lines.append(f"FABLE2_VS_TRANSFORM({h}ull, {base}, {layout}, {pos_fetch}, {int(deformed)})")
    extras.extend(own)


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
    ap.add_argument("--out", help="vs_transform_table.inc")
    ap.add_argument("--wind-out", help="wind_table.inc")
    a = ap.parse_args(argv)
    if not a.out and not a.wind_out:
        ap.error("nothing to write: --out, --wind-out or both")
    data = load(a.json)
    if a.out:
        Path(a.out).write_text(generate(data))
    if a.wind_out:
        Path(a.wind_out).write_text(generate_wind(data), newline="\n")


if __name__ == "__main__":
    main()
