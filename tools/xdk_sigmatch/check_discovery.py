"""Cross-check native discovery rows against the SDK's [vbind] log.

Usage: python tools\\xdk_sigmatch\\check_discovery.py <native_discovery_*.jsonl> <fable_2_*.log>

The game log must come from a run with --native_render_log_vertex_bindings=true.
A "draw" row matches when its decoded position element (fetch slot, offset,
stride, format) is an attribute of one of the same shader's logged bindings and
its vertex-buffer address (base + stream offset) equals that binding's fetch
constant address. Exit code 0 when at least 20 draws were checked and all matched.
"""
import argparse
import json
import re

VS = re.compile(r"\[vbind\] vs=(0x[0-9A-Fa-f]+) dwords=(\d+)")
BIND = re.compile(r"\[vbind\]\s+fetch=(\d+) stride_dw=(\d+) attrs=(\S*)")
FC = re.compile(r"\[vbind\]\s+fc=(0x[0-9A-Fa-f]+) (0x[0-9A-Fa-f]+)")


def parse_vbind(lines):
    """{vs_hash: [{"fetch", "stride_dw", "attrs": [(offset, format, signed, normalized)], "fc"}]}.

    A shader can appear in several blocks (one per distinct set of fetch
    constants); their bindings accumulate under the same hash.
    """
    out, cur = {}, None
    for line in lines:
        if m := VS.search(line):
            cur = out.setdefault(m.group(1), [])
        elif (m := BIND.search(line)) and cur is not None:
            attrs = [tuple(int(x) for x in a.split(":")) for a in m.group(3).split(",") if a]
            cur.append({"fetch": int(m.group(1)), "stride_dw": int(m.group(2)), "attrs": attrs, "fc": None})
        elif (m := FC.search(line)) and cur:
            cur[-1]["fc"] = (int(m.group(1), 16), int(m.group(2), 16))
    return out


def _match(row, bindings):
    p, vb = row["pos"], row["vb"]
    # vb.offset is derived from the device shadow, so the address comparison
    # below cannot catch a wrong object layout; fc_match (the object's fetch
    # constant dwords plus the offset equal the shadow) does.
    if not vb.get("fc_match", True):
        return False
    # The XDK folds the SetStreamSource offset into the fetch constant address
    # (frame-map section 8), so compare base + offset with the logged address.
    address = vb["phys_addr"] + vb.get("offset", 0)
    for b in bindings:
        if b["fetch"] != p["fetch_slot"] or 4 * b["stride_dw"] != p["stride_bytes"]:
            continue
        if not any(4 * off == p["offset_bytes"] and fmt == p["format"] for off, fmt, *_ in b["attrs"]):
            continue
        if b["fc"] is None or (b["fc"][0] & ~3) == address:
            return True
    return False


def check_rows(rows, vbind):
    """(checked, matched) over "draw" rows whose shader appears in the log."""
    checked = matched = 0
    for r in rows:
        if r.get("kind") != "draw" or "pos" not in r or r["vs_hash"] not in vbind:
            continue
        checked += 1
        matched += _match(r, vbind[r["vs_hash"]])
    return checked, matched


def read_rows(lines):
    """JSON rows; a partial last line (capture cut off by closing the game) is dropped."""
    lines = [l for l in lines if l.strip()]
    rows = []
    for i, line in enumerate(lines):
        try:
            rows.append(json.loads(line))
        except ValueError:
            if i != len(lines) - 1:
                raise
            print(f"ignored a truncated last row ({len(line)} chars)")
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("discovery")
    ap.add_argument("game_log")
    a = ap.parse_args(argv)
    with open(a.game_log, errors="ignore") as fh:
        vbind = parse_vbind(fh)
    with open(a.discovery) as fh:
        rows = read_rows(fh)
    draws = [r for r in rows if r.get("kind") == "draw"]
    checked, matched = check_rows(rows, vbind)
    no_pos = sum(1 for r in draws if "pos" not in r)
    unknown = sum(1 for r in draws if "pos" in r and r.get("vs_hash") not in vbind)
    print(f"{len(draws)} draw rows, {len(vbind)} logged shaders; not checked: "
          f"{no_pos} without a position element, {unknown} with a shader hash not in the log")
    print(f"checked {checked} sampled draws, {matched} matched")
    return 0 if checked >= 20 and matched == checked else 1


if __name__ == "__main__":
    raise SystemExit(main())
