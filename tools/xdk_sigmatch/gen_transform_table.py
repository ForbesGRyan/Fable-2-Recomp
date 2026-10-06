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


def swizzle_bits(text):
    """'yxw1' -> the vfetch destination swizzle (3 bits per component, x first)."""
    if len(text) != 4 or any(c not in _SWZ for c in text):
        raise ValueError(f"bad swizzle {text!r}")
    return sum(_SWZ[c] << (3 * i) for i, c in enumerate(text))


def reg_comp(text, pair=False):
    """'c47.zw' -> 47 * 4 + 2; a pair must name two consecutive components."""
    m = re.fullmatch(r"c(\d+)\.([xyzw]{1,2})", text)
    if not m or int(m.group(1)) > 255:
        raise ValueError(f"bad register {text!r}")
    comps = [_COMP.index(c) for c in m.group(2)]
    if pair and (len(comps) != 2 or comps[1] != comps[0] + 1):
        raise ValueError(f"{text!r} is not a pair of consecutive components")
    if not pair and len(comps) != 1:
        raise ValueError(f"{text!r} names more than one component")
    return int(m.group(1)) * 4 + comps[0]


def _swizzles(items):
    items = list(items or ["", "", ""])
    if len(items) != 3:
        raise ValueError("three row swizzles are required")
    return ["0x%X" % (swizzle_bits(s) if s else 0) for s in items]


def _skin_line(vs, s):
    rows = s["row_fetches"]
    if len(rows) != 3:
        raise ValueError(f"{vs}: skin needs three row fetches")
    if "pairs" in s and "weight_fetch" not in s:
        raise ValueError(f"{vs}: skin has pairs but no weight_fetch")
    if "weight_fetch" in s:
        pairs = s["pairs"]
        if not 1 <= len(pairs) <= 4:
            raise ValueError(f"{vs}: skin needs 1 to 4 (index, weight) pairs")
        comps = [(_COMP.index(i), _COMP.index(w)) for i, w in pairs]
        weight_fetch = int(s["weight_fetch"])
    else:
        comps = [(_COMP.index(s["index_component"]), 0)]
        weight_fetch = -1
    flat = [c for pair in comps + [(0, 0)] * (4 - len(comps)) for c in pair]
    fields = [int(s["index_fetch"]), weight_fetch, *rows, *_swizzles(s.get("row_swizzles")), len(comps), *flat]
    return f"FABLE2_VS_SKIN({vs}ull, {', '.join(str(f) for f in fields)})"


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


def _instance_line(vs, e):
    inst = e["instance"]
    rows = inst["row_fetches"]
    if len(rows) != 3:
        raise ValueError(f"{vs}: instance needs three row fetches")
    m = re.fullmatch(r"c(\d+)\.xyz", inst["offset"])
    if not m or int(m.group(1)) > 255:
        raise ValueError(f"{vs}: instance offset must be c<N>.xyz")
    try:
        cut = cut_refs(inst)
    except ValueError as err:
        raise ValueError(f"{vs}: {err}") from None
    fields = [*rows, *_swizzles(inst.get("row_swizzles")), reg_comp(inst["inv_count"]), reg_comp(inst["count"]),
              reg_comp(inst["first"]), repr(float(inst["bias"])) + "f", int(m.group(1)) * 4, *cut]
    return f"FABLE2_VS_INSTANCE({vs}ull, {', '.join(str(f) for f in fields)})"


def generate(data):
    lines = ["// Generated by tools/xdk_sigmatch/gen_transform_table.py - do not edit."]
    extras = []
    for vs, e in sorted(data.items(), key=lambda kv: int(kv[0], 16)):
        for layout_key in ("skin", "instance"):
            if layout_key in e and "base" not in e:
                raise ValueError(f"{vs}: {layout_key} entry without base")
        if "skin" in e and "instance" in e:
            raise ValueError(f"{vs}: entry has both skin and instance")
        # No transform (a rejected record), or a two-window product from the finder ("base2"): no line.
        if "base" not in e or "base2" in e:
            continue
        layout = 0 if e["layout"] == "dot" else 1
        # deformed: the shader moves the fetched position (skinning, displacement)
        # before the transform, so the record draws it undeformed.
        deformed = 1 if e.get("deformed") else 0
        pos_fetch = e.get("pos_fetch", -1)
        if "instance" in e:
            mesh = int(e["instance"]["mesh_fetch"])
            if "pos_fetch" in e and e["pos_fetch"] != mesh:
                raise ValueError(f"{vs}: pos_fetch disagrees with instance mesh_fetch")
            pos_fetch = mesh
        lines.append(f"FABLE2_VS_TRANSFORM({vs}ull, {e['base']}, {layout}, {pos_fetch}, {deformed})")
        if "pos_swizzle" in e:
            extras.append(f"FABLE2_VS_POS_SWIZZLE({vs}ull, 0x{swizzle_bits(e['pos_swizzle']):X})")
        if "skin" in e:
            extras.append(_skin_line(vs, e["skin"]))
        if "instance" in e:
            extras.append(_instance_line(vs, e))
        if "terrain" in e:
            t = e["terrain"]
            po = t.get("patch_offset")
            fields = [reg_comp(t["grid"], pair=True), reg_comp(t["cell"], pair=True), reg_comp(t["height_scale"]),
                      reg_comp(t["origin"], pair=True), reg_comp(t["tex_offset"], pair=True),
                      reg_comp(t["tex_scale"], pair=True), -1 if po is None else reg_comp(po),
                      int(t["height_fetch"])]
            extras.append(f"FABLE2_VS_TERRAIN({vs}ull, {', '.join(str(f) for f in fields)})")
        for name, u in sorted(e.get("uv", {}).items()):
            interp, comp = uv_refs.input_comp(name)
            src = _COMP.index(u["src"])
            refs = ", ".join(str(r) for r in uv_refs.stages(u.get("stages", []), 0))
            extras.append(f"FABLE2_VS_UV({vs}ull, {interp}, {comp}, {int(u['fetch'])}, {src}, "
                          f"{int(u['format'])}, {int(u['offset'])}, {refs})")
    return "\n".join(lines + extras) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    Path(a.out).write_text(generate(json.loads(Path(a.json).read_text())))


if __name__ == "__main__":
    main()
