"""Summarize a d3d_census_*.jsonl capture into frame-map.md sections."""
import argparse
import json
import math
import statistics
import sys
from collections import Counter, defaultdict

DRAW_FUNCS = ("D3DDevice_DrawIndexedVertices", "D3DDevice_DrawVertices",
              "D3DDevice_DrawIndexedVertices?", "D3DDevice_DrawVertices?")


def _median(values):
    return statistics.median(values) if values else None


def _percentile_nearest_rank(values, pct):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(1, math.ceil(pct / 100 * len(ordered))) - 1]


def filter_rows(rows, from_frame=None, to_frame=None):
    """Keep rows whose `frame` is within [from_frame, to_frame] (inclusive)."""
    return [r for r in rows
            if (from_frame is None or r.get("frame", 0) >= from_frame)
            and (to_frame is None or r.get("frame", 0) <= to_frame)]


def drop_repeated_gpu_frames(rows):
    """Keep only the first of consecutive rows sharing the same gpu.gpu_frame."""
    out = []
    last = None
    for r in rows:
        gf = r.get("gpu", {}).get("gpu_frame")
        if gf is not None and gf == last:
            continue
        if gf is not None:
            last = gf
        out.append(r)
    return out


def summarize(rows):
    rows = drop_repeated_gpu_frames(rows)
    gpu_rows = [r for r in rows if "gpu" in r]
    pitch_keys = set().union(*(x["gpu"]["pitches"].keys() for x in gpu_rows)) if gpu_rows else set()
    pitch_series = defaultdict(list)
    for r in gpu_rows:
        for p in pitch_keys:
            pitch_series[p].append(r["gpu"]["pitches"].get(p, 0))
    callers = defaultdict(Counter)
    for r in rows:
        for name, f in r.get("funcs", {}).items():
            callers[name].update({lr: n for lr, n in f["callers"].items()})
    ratios = []
    hooked_total = 0
    draws_total = 0
    for r in gpu_rows:
        hooked = sum(r["funcs"].get(n, {}).get("calls", 0) for n in DRAW_FUNCS)
        hooked_total += hooked
        draws_total += r["gpu"]["draws"]
        if r["gpu"]["draws"]:
            ratios.append(hooked / r["gpu"]["draws"])
    draws = [r["gpu"]["draws"] for r in gpu_rows]
    return {
        "frames": len(rows),
        "guest_ms_median": _median([r["guest_ms"] for r in rows]),
        "swap_interval_ms_median": _median([r["gpu"]["swap_interval_ms"] for r in gpu_rows]),
        "draw_cpu_ms_median": _median([r["gpu"]["draw_cpu_ms"] for r in gpu_rows]),
        "draws_median": _median(draws),
        "draws_mean": statistics.mean(draws) if draws else None,
        "draws_p90": _percentile_nearest_rank(draws, 90),
        "copies_median": _median([r["gpu"].get("copies", 0) for r in gpu_rows]),
        "pitches": {p: _median(v) for p, v in sorted(pitch_series.items(), key=lambda kv: -_median(kv[1]))},
        "top_callers": {n: c.most_common(10) for n, c in callers.items()},
        "draw_call_ratio": _median(ratios),
        "draw_call_ratio_total": (hooked_total / draws_total) if draws_total else None,
    }


def to_markdown(s):
    out = ["## Timing", "",
           f"- Frames captured: {s['frames']}",
           f"- Guest frame time (median): {s['guest_ms_median']} ms",
           f"- Swap interval (median): {s['swap_interval_ms_median']} ms",
           f"- Emulated IssueDraw CP time (median): {s['draw_cpu_ms_median']} ms/frame",
           f"- Emulated draws (median): {s['draws_median']} per frame",
           f"- Emulated draws (mean): {s['draws_mean']} per frame",
           f"- Emulated draws (p90, nearest-rank): {s['draws_p90']} per frame",
           f"- Resolves (copies, median): {s['copies_median']} per frame",
           f"- Hooked draw calls / emulated draws (median): {s['draw_call_ratio']}",
           f"- Hooked draw calls / emulated draws (total): {s['draw_call_ratio_total']}", "",
           "## Draws per render-target pitch (median per frame)", "",
           "| Pitch | Draws |", "|---|---|"]
    out += [f"| {p} | {d} |" for p, d in s["pitches"].items()]
    out += ["", "## Top call sites", ""]
    for name, top in s["top_callers"].items():
        out += [f"### {name}", "", "| Caller (LR) | Calls |", "|---|---|"]
        out += [f"| {lr} | {n} |" for lr, n in top]
        out.append("")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path")
    ap.add_argument("--from-frame", type=int, default=None)
    ap.add_argument("--to-frame", type=int, default=None)
    args = ap.parse_args()
    with open(args.path) as fh:
        rows = [json.loads(line) for line in fh if line.strip()]
    # Row 1 accumulates every call since startup; drop it.
    rows = [r for r in rows if r.get("frame") != 1]
    rows = filter_rows(rows, args.from_frame, args.to_frame)
    print(to_markdown(summarize(rows)))


if __name__ == "__main__":
    main()
