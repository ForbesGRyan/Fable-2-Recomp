"""Find the world-view-projection constant window per vertex shader.

Reads native discovery rows (FABLE2_NATIVE_DISCOVERY) and, for each vertex
shader hash, tries every 4-register window of the vertex constant bank in two
layouts: "dot" (clip[i] = dot(row[i], p)) and "combine" (clip = sum p[j] *
row[j]). With --products it also tries row-window products (second window
applied first, e.g. view-projection x world). Writes vs-transforms.json.
"""
import argparse
import json
from collections import defaultdict
from pathlib import Path


def _rows(bank, base):
    return [bank[4 * (base + r): 4 * (base + r) + 4] for r in range(4)]


def _apply(rows, layout, p):
    if layout == "dot":
        return [sum(rows[i][k] * p[k] for k in range(4)) for i in range(4)]
    return [sum(p[k] * rows[k][i] for k in range(4)) for i in range(4)]


def _matmul_dot(a, b):
    # Combined "dot" rows for applying b first, then a: clip = A (B p).
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def _to_dot(rows, layout):
    return rows if layout == "dot" else [[rows[k][i] for k in range(4)] for i in range(4)]


def _sample_score(rows, layout, positions):
    inside = 0
    xs = []
    for p in positions:
        c = _apply(rows, layout, p)
        w = c[3]
        if w <= 1e-6:
            continue
        x, y, z = c[0] / w, c[1] / w, c[2] / w
        if abs(x) <= 1.05 and abs(y) <= 1.05 and -0.05 <= z <= 1.05:
            inside += 1
            xs.append(x)
    if not positions:
        return 0.0, False
    score = inside / len(positions)
    spread = (max(xs) - min(xs)) if xs else 0.0
    return score, score >= 0.5 and spread >= 0.002


def _candidate_score(get_rows, layout, samples):
    total = 0.0
    for bank, positions in samples:
        rows = get_rows(bank)
        if rows is None:
            return 0.0
        s, ok = _sample_score(rows, layout, positions)
        if not ok:
            return 0.0
        total += s
    return total / len(samples)


def _nonzero(bank, base):
    return any(v != 0.0 for v in bank[4 * base: 4 * base + 16])


def find_transform(samples, products=False, min_score=0.9):
    """samples: list of (bank, positions). Returns the best window or None."""
    if not samples:
        return None
    best = None
    for base in range(0, 253):
        if not all(_nonzero(b, base) for b, _ in samples):
            continue
        for layout in ("dot", "combine"):
            s = _candidate_score(lambda bank, b=base: _rows(bank, b), layout, samples)
            if s > (best["score"] if best else 0.0):
                best = {"base": base, "layout": layout, "score": s}
    if best and best["score"] >= min_score:
        return best
    if not products:
        return None
    best = None
    bases = [b for b in range(0, 253) if all(_nonzero(bank, b) for bank, _ in samples)]
    for a in bases:
        for b in bases:
            if a == b:
                continue
            for layout in ("dot", "combine"):
                def rows_ab(bank, a=a, b=b, layout=layout):
                    return _matmul_dot(_to_dot(_rows(bank, a), layout), _to_dot(_rows(bank, b), layout))
                s = _candidate_score(rows_ab, "dot", samples)
                if s > (best["score"] if best else 0.0):
                    best = {"base": a, "base2": b, "layout": layout, "score": s}
    return best if best and best["score"] >= min_score else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log")
    ap.add_argument("--out", required=True)
    ap.add_argument("--products", action="store_true")
    ap.add_argument("--min-score", type=float, default=0.9)
    a = ap.parse_args(argv)
    by_shader = defaultdict(list)
    with open(a.log) as fh:
        for line in fh:
            if not line.strip():
                continue
            row = json.loads(line)
            if row.get("kind") != "draw" or "bank" not in row or not row.get("positions"):
                continue
            if row.get("pos_suspect"):  # indexed past its stream: not positions
                continue
            by_shader[row["vs_hash"]].append((row["bank"], row["positions"]))
    out = Path(a.out)
    data = json.loads(out.read_text()) if out.exists() else {}
    for vs, samples in sorted(by_shader.items()):
        if data.get(vs, {}).get("manual"):
            continue
        r = find_transform(samples, a.products, a.min_score)
        if r is None:
            continue
        r["score"] = round(r["score"], 4)
        r["samples"] = len(samples)
        r.setdefault("pos_fetch", data.get(vs, {}).get("pos_fetch", -1))
        data[vs] = r
    out.write_text(json.dumps(dict(sorted(data.items())), indent=2) + "\n")
    print(f"{sum(1 for v in data.values() if 'base' in v)} shaders with transforms "
          f"({len(by_shader)} shaders sampled)")


if __name__ == "__main__":
    main()
