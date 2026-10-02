"""Propose albedo texture slots and UV transforms from a discovery capture (D3).

Usage: albedo_finder.py <capture.jsonl> --dumps <shader_dump_dir> --out <proposals.json> [--thumbs <dir>]

Reads the discovery draw rows (ps_hash, vs_hash, "tf" fetch constants) and "texture" rows (raw guest dumps),
traces each in-scene pixel shader's texture fetches through the shader disassembly (shader_trace.py), scores
the candidates and writes proposals in the ps-albedo.json and vs-transforms.json "uv" shapes with
"proposed": true. Thumbnails: <thumbs>/<PS>_tf<slot>.png. Prints cumulative coverage in draw-count order.
"""
import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import shader_trace as st  # noqa: E402
import texture_thumb as tt  # noqa: E402

_M64 = (1 << 64) - 1


def texture_identity(fc):
    """Port of render::TextureIdentity (texture_decode.h): sampler/LOD fields masked out."""
    masked = [fc[0] & ~(0x1FF << 10) & 0xFFFFFFFF, fc[1], fc[2], fc[3] & 0x7FFFF, fc[4] & (0xFF << 2),
              fc[5] & ~0x1FF & 0xFFFFFFFF]
    h = 1469598103934665603
    for v in masked:
        h = ((h ^ v) * 1099511628211) & _M64
    return h


def hash16(text):
    return f"0x{int(text, 16):016X}"


def _fc(row_fc):
    return [int(v, 16) for v in row_fc]


def load_capture(path):
    """-> (ps stats, texture rows by identity). ps stats: hash -> {draws, vs: Counter, tf: {slot: fc}}."""
    stats = {}
    textures = {}
    total = 0
    for line in Path(path).read_text().splitlines():
        if not line.strip():
            continue
        r = json.loads(line)
        kind = r.get("kind")
        if kind == "texture":
            fc = _fc(r["fc"])
            textures[(fc[1] & 0xFFFFF000, texture_identity(fc))] = r["file"]
        elif kind == "draw" and r.get("in_scene"):
            total += 1
            ps = int(r.get("ps_hash", "0x0"), 16)
            if not ps:
                continue
            e = stats.setdefault(hash16(r["ps_hash"]), {"draws": 0, "vs": Counter(), "tf": {}})
            e["draws"] += 1
            if r.get("vs_hash") and int(r["vs_hash"], 16):
                e["vs"][hash16(r["vs_hash"])] += 1
            for slot, fc in (r.get("tf") or {}).items():
                if fc and int(slot) not in e["tf"]:
                    e["tf"][int(slot)] = _fc(fc)
    return stats, textures, total


def _load_shader(dumps, kind, h):
    p = Path(dumps) / f"shader_{h[2:]}.ucode.{kind}"
    return st.parse(p.read_text(errors="replace")) if p.is_file() else None


def _interp(inp):
    return int(inp[1:inp.index(".")])


def score_fetches(instrs):
    """-> list of (score, TFetch, trace dict | None), best first."""
    out = []
    for tf in st.texture_fetches(instrs):
        score = 1.0 if tf.dim == "2D" else 0.0
        trace = st.trace_ps_albedo(instrs, tf)
        if isinstance(trace, st.Unsupported):
            trace = None
        else:
            score += 2
        if len(tf.mask) >= 3:
            score += 1
        out.append([score, tf, trace])
    traced = [o for o in out if o[2]]
    if traced:
        low = min(min(_interp(o[2]["u"]["input"]), _interp(o[2]["v"]["input"])) for o in traced)
        for o in traced:
            if min(_interp(o[2]["u"]["input"]), _interp(o[2]["v"]["input"])) == low:
                o[0] += 0.5
    out.sort(key=lambda o: -o[0])  # stable: shader order breaks ties
    return [tuple(o) for o in out]


def write_thumb(thumbs, ps, slot, fc, textures, base_dir):
    """Thumbnail <PS>_tf<slot>.png from the texture dump matching fc; returns the file name or None."""
    f = textures.get((fc[1] & 0xFFFFF000, texture_identity(fc)))
    if not f:
        return None
    path = Path(base_dir) / f
    if not path.is_file():
        return None
    try:
        w, h, rgba = tt.decode_rgba(path.read_bytes(), fc)
    except ValueError as e:
        print(f"  thumb {ps[2:]} tf{slot}: {e}")
        return None
    name = f"{ps[2:]}_tf{slot}.png"
    Path(thumbs).mkdir(parents=True, exist_ok=True)
    tt.write_png(Path(thumbs) / name, w, h, rgba)
    return name


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--dumps", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--thumbs")
    a = ap.parse_args(argv)
    cap = Path(a.capture)
    stats, textures, total = load_capture(cap)
    ps_out, vs_out = {}, {}
    covered = cum = 0
    print(f"{cap.name}: {total} in-scene draws, {len(stats)} pixel shaders (terrain excluded)")
    for ps, e in sorted(stats.items(), key=lambda kv: -kv[1]["draws"]):
        cum += e["draws"]
        instrs = _load_shader(a.dumps, "frag", ps)
        status = "no dump"
        if instrs is not None:
            cands = score_fetches(instrs)
            best = next((c for c in cands if c[2]), None)
            status = "no traceable fetch"
            if best:
                score, tf, trace = best
                thumb = None
                if a.thumbs:
                    for _, other, _ in cands:
                        if other.slot in e["tf"]:
                            t = write_thumb(a.thumbs, ps, other.slot, e["tf"][other.slot], textures, cap.parent)
                            if other.slot == tf.slot:
                                thumb = t
                evidence = (f"{cap.name}: {e['draws']} draws, score {score:g}, "
                            f"thumbnail {thumb or 'none'}")
                ps_out[ps] = {"slot": tf.slot, "u": trace["u"], "v": trace["v"], "proposed": True,
                              "evidence": evidence}
                status = f"slot {tf.slot} score {score:g}"
                for vs, n in e["vs"].most_common():
                    vinstrs = _load_shader(a.dumps, "vert", vs)
                    if vinstrs is None:
                        continue
                    uv = {}
                    for axis in "uv":
                        inp = trace[axis]["input"]
                        r = st.trace_vs_export(vinstrs, _interp(inp), inp[-1])
                        if not isinstance(r, st.Unsupported):
                            uv[f"o{_interp(inp)}.{inp[-1]}"] = r
                    if uv:
                        v = vs_out.setdefault(vs, {"uv": {}, "proposed": True, "evidence": "",
                                                   "_src": {}, "_conf": []})
                        for key, r in uv.items():
                            if key not in v["uv"]:
                                v["uv"][key] = r
                                v["_src"][key] = (ps, n)
                            elif v["uv"][key] != r:
                                kept = v["_src"][key][0]
                                msg = f"{key}: ps {ps[2:]} needs a different trace than kept ps {kept[2:]}"
                                v["_conf"].append(msg)
                                print(f"  conflict vs {vs[2:]} {msg}")
                covered += e["draws"]
        print(f"  ps {ps[2:]}  draws {e['draws']:6d}  cumulative {cum} of {total}  {status}")
    for vs, v in vs_out.items():
        src = v.pop("_src")
        conf = v.pop("_conf")
        parts = sorted({f"ps {p[2:]} ({n} draws)" for p, n in src.values()})
        v["evidence"] = f"{cap.name}: kept traces from " + ", ".join(parts)
        if conf:
            v["evidence"] += "; conflicts: " + "; ".join(conf)
    print(f"covered {covered} of {total} draws ({100.0 * covered / total if total else 0:.1f}%)")
    Path(a.out).write_text(json.dumps({"ps_albedo": ps_out, "vs_transforms": vs_out}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
