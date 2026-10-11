"""Cross-check native discovery rows against the SDK's [vbind] log.

Usage: python tools\\xdk_sigmatch\\check_discovery.py <native_discovery_*.jsonl> <fable_2_*.log>

The game log must come from a run with --native_render_log_vertex_bindings=true.
A "draw" row matches when its decoded position element (fetch slot, offset,
stride, format) is an attribute of one of the same shader's logged bindings and
its vertex-buffer address (base + stream offset) equals that binding's fetch
constant address (rows without fc_match and bindings without an fc line never
match). Exit code 0 when at least 20 draws were checked and all matched.

Foliage discovery (frame-map section 18): every index file a "draw" row's "ib" names
must exist beside the capture with the row's "file_bytes" and hold the draw's range,
and the "frame" rows' "scene_frame" must rise; a problem with either fails the check.
"""
import argparse
import json
import os
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
    # constant dwords plus the offset equal the shadow) does. A row without
    # the field is not evidence of a match.
    if not vb.get("fc_match", False):
        return False
    # The XDK folds the SetStreamSource offset into the fetch constant address
    # (frame-map section 8), so compare base + offset with the logged address.
    address = vb["phys_addr"] + vb.get("offset", 0)
    for b in bindings:
        if b["fetch"] != p["fetch_slot"] or 4 * b["stride_dw"] != p["stride_bytes"]:
            continue
        if not any(4 * off == p["offset_bytes"] and fmt == p["format"] for off, fmt, *_ in b["attrs"]):
            continue
        # A binding logged without its fc line has no address to compare.
        if b["fc"] is not None and (b["fc"][0] & ~3) == address:
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


def check_index_files(rows, base_dir):
    """(named, problems) over "draw" rows whose "ib" names a file (frame-map section 18).

    The file is the index buffer's bytes as the guest holds them, from byte "file_offset" of the
    buffer, "file_bytes" long, at `base_dir` / "file". A problem: the file is missing or has
    another size, it ends past the buffer, or the draw's own range is not inside it.
    """
    named, problems = 0, []
    sizes = {}
    for n, r in enumerate(rows):
        ib = r.get("ib") if r.get("kind") == "draw" else None
        if not ib or "file" not in ib:
            continue
        named += 1
        name, first, length = ib["file"], ib.get("file_offset", 0), ib.get("file_bytes", 0)
        if name not in sizes:
            path = os.path.join(str(base_dir), *name.split("/"))
            sizes[name] = os.path.getsize(path) if os.path.isfile(path) else None
        if sizes[name] is None:
            problems.append(f"row {n}: {name} is missing")
        elif sizes[name] != length:
            problems.append(f"row {n}: {name} has {sizes[name]} bytes, the row says {length} bytes")
        elif first + length > ib["size"]:
            problems.append(f"row {n}: {name} ends past the buffer ({first} + {length} > {ib['size']})")
        else:
            width = 4 if ib["index32"] else 2
            if ib["start"] * width < first or (ib["start"] + ib["count"]) * width > first + length:
                problems.append(f"row {n}: the draw's range is not inside {name}")
    return named, problems


def check_mip_files(rows, base_dir):
    """(texture rows that name a mips file, problems): the file is there with the row's byte count; the levels
    are numbered upward from 1, each of the size the base level's halves to and with its place inside the file
    (ledger ruling 54; discovery_format.h, FormatMipsMember)."""
    named, problems = 0, []
    for r in rows:
        mips = r.get("mips") if r.get("kind") == "texture" else None
        if not mips:
            continue
        named += 1
        where = "texture %s" % r.get("file")
        path = os.path.join(base_dir, str(mips.get("file", "")).replace("/", os.sep))
        if not os.path.isfile(path):
            problems.append("%s: mips file %s is missing" % (where, mips.get("file")))
            continue
        size = os.path.getsize(path)
        if size != mips.get("bytes") or size == 0:
            problems.append("%s: mips file has %d bytes, the row says %s" % (where, size, mips.get("bytes")))
        fc = [int(x, 16) for x in r.get("fc") or []]
        if len(fc) != 6:
            problems.append("%s: no fetch constant" % where)
            continue
        w, h = (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1
        last = 0
        levels = mips.get("levels") or []
        if not levels:
            problems.append("%s: a mips member without a level" % where)
        for lv in levels:
            n = lv.get("level", 0)
            if n <= last or n < 1 or n > 15:
                problems.append("%s: level %s after %s" % (where, n, last))
                break
            last = n
            if (lv.get("w"), lv.get("h")) != (max(1, w >> n), max(1, h >> n)):
                problems.append("%s: level %d is %s x %s of a %d x %d texture" % (where, n, lv.get("w"), lv.get("h"), w, h))
            if not (0 <= lv.get("offset", -1) < size) or lv.get("pitch", 0) <= 0:
                problems.append("%s: level %d lies outside its file" % (where, n))
    return named, problems


def check_scene_frames(rows):
    """(seen, problems) over "frame" rows: "scene_frame" (the scene published at the row's swap)
    is above 0 and rises from row to row. A row without it published no scene (an older capture,
    the renderer off, or a frame without records: no native view was drawn) and is no problem.
    """
    frames = [r for r in rows if r.get("kind") == "frame"]
    seen = sum(1 for r in frames if "scene_frame" in r)
    problems, last = [], 0
    for r in frames:
        value = r.get("scene_frame")
        if value is None:
            continue
        if value <= last:
            problems.append(f"frame {r.get('frame')}: scene_frame {value} does not rise (after {last})")
        else:
            last = value
    return seen, problems


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
    named, file_problems = check_index_files(rows, os.path.dirname(os.path.abspath(a.discovery)))
    seen, frame_problems = check_scene_frames(rows)
    if named or seen or file_problems or frame_problems:
        print(f"{named} draw rows name an index file, {len(file_problems)} problems; "
              f"{seen} frame rows have a scene_frame, {len(frame_problems)} problems")
    with_mips, mip_problems = check_mip_files(rows, os.path.dirname(os.path.abspath(a.discovery)))
    if with_mips or mip_problems:
        print(f"{with_mips} texture rows name a mips file, {len(mip_problems)} problems")
    for line in (file_problems + frame_problems + mip_problems)[:20]:
        print("  " + line)
    return 0 if checked >= 20 and matched == checked and not file_problems and not frame_problems and not mip_problems else 1


if __name__ == "__main__":
    raise SystemExit(main())
