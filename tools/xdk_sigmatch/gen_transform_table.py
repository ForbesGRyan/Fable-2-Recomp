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

An entry is refused (ValueError naming the shader) rather than read loosely: it and its "skin",
"instance", "terrain" and "uv" objects have only the keys listed below (the committed table's, plus
what matrix_finder.py writes), "layout" is "dot" or "combine", "base" is a JSON integer 0 to 252
(four registers), a key is "0x" and 16 hex digits (written to the table in upper case) and names
its shader once, whatever the letters' case (load() also refuses a key written twice in the file).
Nothing is converted on the way: a fetch ordinal ("pos_fetch", -1 = the default; "fetch",
"index_fetch", "weight_fetch", "mesh_fetch", "row_fetches") is a JSON integer 0 to 127, a "uv"
"format" 0 to 63 and "offset" 0 to 255 dwords, "height_fetch" a texture fetch slot 0 to 31,
"deformed" a JSON boolean, "bias" a finite JSON number; 2.5, true, "2" and "false" are refused.
"""
import argparse
import json
import re
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
              "manual", "evidence", "uv_manual", "uv_evidence", "bind_pose_evidence", "finder", "rejected",
              "task11_rejected", "base2", "score", "samples")
SKIN_KEYS = ("index_fetch", "weight_fetch", "index_component", "row_fetches", "row_swizzles", "pairs")
INSTANCE_KEYS = ("mesh_fetch", "row_fetches", "row_swizzles", "inv_count", "count", "first", "bias", "offset", "cut")
TERRAIN_KEYS = ("grid", "cell", "height_scale", "origin", "tex_offset", "tex_scale", "patch_offset", "height_fetch")
UV_KEYS = ("fetch", "src", "format", "offset", "stages")


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
    for layout_key in ("skin", "instance"):
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
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    Path(a.out).write_text(generate(load(a.json)))


if __name__ == "__main__":
    main()
