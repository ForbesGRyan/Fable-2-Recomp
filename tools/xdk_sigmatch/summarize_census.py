"""Summarize a d3d_census_*.jsonl capture into frame-map.md sections."""
import json
import statistics
import sys
from collections import Counter, defaultdict

DRAW_FUNCS = ("D3DDevice_DrawIndexedVertices", "D3DDevice_DrawVertices",
              "D3DDevice_DrawIndexedVertices?", "D3DDevice_DrawVertices?")


def _median(values):
    return statistics.median(values) if values else None


def summarize(rows):
    gpu_rows = [r for r in rows if "gpu" in r]
    pitch_series = defaultdict(list)
    for r in gpu_rows:
        for p in set().union(*(x["gpu"]["pitches"].keys() for x in gpu_rows)):
            pitch_series[p].append(r["gpu"]["pitches"].get(p, 0))
    callers = defaultdict(Counter)
    for r in rows:
        for name, f in r.get("funcs", {}).items():
            callers[name].update({lr: n for lr, n in f["callers"].items()})
    ratios = []
    for r in gpu_rows:
        hooked = sum(r["funcs"].get(n, {}).get("calls", 0) for n in DRAW_FUNCS)
        if r["gpu"]["draws"]:
            ratios.append(hooked / r["gpu"]["draws"])
    return {
        "frames": len(rows),
        "guest_ms_median": _median([r["guest_ms"] for r in rows]),
        "swap_interval_ms_median": _median([r["gpu"]["swap_interval_ms"] for r in gpu_rows]),
        "draw_cpu_ms_median": _median([r["gpu"]["draw_cpu_ms"] for r in gpu_rows]),
        "draws_median": _median([r["gpu"]["draws"] for r in gpu_rows]),
        "copies_median": _median([r["gpu"].get("copies", 0) for r in gpu_rows]),
        "pitches": {p: _median(v) for p, v in sorted(pitch_series.items(), key=lambda kv: -_median(kv[1]))},
        "top_callers": {n: c.most_common(10) for n, c in callers.items()},
        "draw_call_ratio": _median(ratios),
    }


def to_markdown(s):
    out = ["## Timing", "",
           f"- Frames captured: {s['frames']}",
           f"- Guest frame time (median): {s['guest_ms_median']} ms",
           f"- Swap interval (median): {s['swap_interval_ms_median']} ms",
           f"- Emulated IssueDraw CP time (median): {s['draw_cpu_ms_median']} ms/frame",
           f"- Emulated draws (median): {s['draws_median']} per frame",
           f"- Resolves (copies, median): {s['copies_median']} per frame",
           f"- Hooked draw calls / emulated draws (median): {s['draw_call_ratio']}", "",
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
    rows = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
    # Row 1 accumulates every call since startup; drop it.
    rows = [r for r in rows if r.get("frame") != 1]
    print(to_markdown(summarize(rows)))


if __name__ == "__main__":
    main()
