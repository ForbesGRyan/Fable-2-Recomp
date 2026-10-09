"""shadow-casters.json -> src/native/capture/shadow_caster_table.inc (held against vs-transforms.json).

  FABLE2_SHADOW_CASTER(vs, ps, kind, slot, scale_reg)

One line per pair of shaders the game draws into its two sun shadow maps (the passes of surface pitch
1040 with the colour mask 0: docs/native-renderer/lighting-formulas.md, "Shadows", "Casters"). `vs`,
`ps`: the shaders' hashes, `ps` 0 for a draw without a pixel shader. `kind`: 0 opaque (depth only),
1 alpha-test (the draw's alpha test decides, with the reference of the draw's own state). For an
alpha-test caster the alpha is the w of the texture in fetch slot `slot`, read at interpolator 0
(xy) times pixel c8.xy plus c8.zw, times the pixel constant `scale_reg` (register * 4 + component;
-1 none); an opaque caster has slot -1 and scale_reg -1.

The table is a JSON list of entries:
  {"vs": "0x<16 hex>", "ps": null | "0x<16 hex>", "kind": "opaque" | "alpha-test",
   "alpha": {"texture_slot": 0, "scale_register": null | "c15.w"},     (alpha-test only)
   "limitation": "what the table's form does not draw",                 (optional)
   "evidence": "where each statement comes from"}

An entry is refused (ValueError naming the vertex shader, and the pixel shader where there is one)
rather than read loosely: the table is a list of objects with only the keys above; a hash is "0x"
and 16 hex digits (written to the table in upper case), never zero, the pixel shader null where
there is none; "evidence" is a non-empty text, and so is a "limitation" that is given; an alpha-test
entry has a pixel shader and its "alpha" with both keys, the slot a JSON integer 0 to 31, the scale
null or one pixel constant component; an opaque entry has no "alpha"; a pair is listed once, whatever
its letters' case (load() also refuses a key written twice in one object).

Held against the vertex table (--vs-json; by default the vs-transforms.json beside --json, and the
table is not generated without one): every caster's vertex shader has an entry with a transform there
(a "base", no "base2"), and an alpha-test caster's has "uv" for o0.x and o0.y, the coordinates its
pixel shader reads. What generation cannot check is the reading itself: that the pair is drawn into
the maps, that its alpha is the texture's w and nothing else. The entries' evidence names the
captures and dumps, and tests/test_gen_caster_table.py holds the committed table to them.

usage: gen_caster_table.py --json docs/native-renderer/shadow-casters.json
                           --out src/native/capture/shadow_caster_table.inc [--vs-json vs-transforms.json]
"""
import argparse
import json
import re
from pathlib import Path

KINDS = {"opaque": 0, "alpha-test": 1}
ENTRY_KEYS = ("vs", "ps", "kind", "alpha", "limitation", "evidence")
ALPHA_KEYS = ("texture_slot", "scale_register")
_HASH = re.compile(r"0x[0-9A-Fa-f]{16}")
_COMP = "xyzw"


def _scale_ref(name, text):
    """None -> -1; "c15.w" -> 63 (register * 4 + component)."""
    if text is None:
        return -1
    m = re.fullmatch(r"c(\d+)\.([xyzw])", text) if isinstance(text, str) else None
    if not m or int(m.group(1)) > 255:
        raise ValueError(f"{name}: scale_register {text!r} is not null or a pixel constant reference like c15.w")
    return int(m.group(1)) * 4 + _COMP.index(m.group(2))


def _entry(index, e, seen, vs_table):
    """One entry -> (vertex hash, pixel hash, table line). Raises ValueError naming the pair."""
    if not isinstance(e, dict):
        raise ValueError(f"entry {index} is not an object")
    vs = e.get("vs")
    if not isinstance(vs, str) or not _HASH.fullmatch(vs):
        raise ValueError(f"entry {index}: vs {vs!r} is not 0x and 16 hex digits")
    if int(vs, 16) == 0:
        raise ValueError(f"entry {index}: vs {vs!r} is zero")
    unknown = [k for k in e if k not in ENTRY_KEYS]
    if unknown:
        raise ValueError(f"{vs}: unknown key {', '.join(sorted(map(str, unknown)))}")
    for key in ("ps", "kind", "evidence"):
        if key not in e:
            raise ValueError(f"{vs}: the entry lacks {key}")
    ps = e["ps"]
    if ps is not None and (not isinstance(ps, str) or not _HASH.fullmatch(ps)):
        raise ValueError(f"{vs}: ps {ps!r} is not null or 0x and 16 hex digits")
    if ps is not None and int(ps, 16) == 0:
        raise ValueError(f"{vs}: ps {ps!r}: a draw without a pixel shader is written null")
    name = vs if ps is None else f"{vs} with {ps}"
    kind = e["kind"]
    if not isinstance(kind, str) or kind not in KINDS:
        raise ValueError(f"{vs}: kind {kind!r} is not opaque or alpha-test")
    evidence = e["evidence"]
    if not isinstance(evidence, str) or not evidence.strip():
        raise ValueError(f"{name}: no evidence")
    if "limitation" in e and (not isinstance(e["limitation"], str) or not e["limitation"].strip()):
        raise ValueError(f"{name}: limitation is not a text")
    slot, scale = -1, -1
    if kind == "alpha-test":
        if ps is None:
            raise ValueError(f"{vs}: an alpha-test caster needs a pixel shader")
        if "alpha" not in e:
            raise ValueError(f"{name}: an alpha-test caster needs alpha")
        alpha = e["alpha"]
        if not isinstance(alpha, dict):
            raise ValueError(f"{name}: alpha is not an object")
        unknown = [k for k in alpha if k not in ALPHA_KEYS]
        if unknown:
            raise ValueError(f"{name}: unknown alpha key {', '.join(sorted(map(str, unknown)))}")
        for key in ALPHA_KEYS:
            if key not in alpha:
                raise ValueError(f"{name}: alpha lacks {key}")
        slot = alpha["texture_slot"]
        if not isinstance(slot, int) or isinstance(slot, bool) or not 0 <= slot <= 31:
            raise ValueError(f"{name}: texture_slot {slot!r} is not an integer from 0 to 31")
        scale = _scale_ref(name, alpha["scale_register"])
    elif "alpha" in e:
        raise ValueError(f"{name}: an opaque caster takes no alpha")
    pair = (int(vs, 16), 0 if ps is None else int(ps, 16))
    if pair in seen:
        raise ValueError(f"{name}: the pair is listed twice")
    seen.add(pair)
    if vs_table is not None:
        entry = vs_table.get(pair[0])
        if entry is None:
            raise ValueError(f"{name}: the vertex shader has no entry in the vertex table")
        if not isinstance(entry, dict) or "base" not in entry or "base2" in entry:
            raise ValueError(f"{name}: the vertex shader's entry has no transform in the vertex table")
        uv = entry.get("uv") if isinstance(entry.get("uv"), dict) else {}
        if kind == "alpha-test" and not ("o0.x" in uv and "o0.y" in uv):
            raise ValueError(f"{name}: the vertex shader's entry has no uv for o0.x and o0.y in the vertex table")
    return pair, f"FABLE2_SHADOW_CASTER(0x{pair[0]:016X}ull, 0x{pair[1]:016X}ull, {KINDS[kind]}, {slot}, {scale})"


def generate(data, vs_table=None):
    """The table text. `vs_table`: vs-transforms.json as loaded ({"0x<hash>": entry}); with it every
    caster is held against its vertex shader's entry."""
    if not isinstance(data, list):
        raise ValueError("the caster table is not a list of entries")
    by_hash = None
    if vs_table is not None:
        by_hash = {int(k, 16): v for k, v in vs_table.items() if isinstance(k, str) and _HASH.fullmatch(k)}
    seen, lines = set(), []
    for index, e in enumerate(data):
        lines.append(_entry(index, e, seen, by_hash))
    lines.sort()
    return "\n".join(["// Generated by tools/xdk_sigmatch/gen_caster_table.py - do not edit."] + [line for _, line in lines]) + "\n"


def _no_repeats(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError(f"{key}: the key is written twice in one object of the file")
        out[key] = value
    return out


def load(path):
    """The table file as generate() takes it; a key written twice in one object is refused."""
    return json.loads(Path(path).read_text(), object_pairs_hook=_no_repeats)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--vs-json", help="the vertex table (default: vs-transforms.json beside --json)")
    a = ap.parse_args(argv)
    vs_path = Path(a.vs_json) if a.vs_json else Path(a.json).parent / "vs-transforms.json"
    if not vs_path.is_file():
        raise ValueError(f"no vertex table at {vs_path}: the caster table is not generated without one")
    text = generate(load(a.json), vs_table=json.loads(vs_path.read_text(), object_pairs_hook=_no_repeats))
    Path(a.out).write_text(text)


if __name__ == "__main__":
    main()
