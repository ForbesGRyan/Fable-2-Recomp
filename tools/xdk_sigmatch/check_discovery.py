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

Water discovery (frame-map section 20): the rows of the lake's draws must name the content of
the reflection pictures they sampled, with a texture row of that content before them, the
normal map's texture row its stored levels, and the reflection views' rows "vp"
(check_water_rows); a problem fails the check. A capture made before those members were
written therefore fails here: it is told apart by its problems, which name them.
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


VERTS_DATA_CAP = 64 << 10   # discovery_format.h, kMaxVertsDataBytes


def packet_names_storage(row):
    """Whether an in-scene "verts" row's "packet" (the six dwords the builder wrote for fetch slot 31) names the
    row's storage in its last pair, field by field as the GPU reads a vertex fetch constant (the SDK's xenos.h,
    xe_gpu_vertex_fetch_t): type 3 (dword 0 bits 0 to 1), the address (bits 2 to 31) of the storage in the GPU's
    space (the 0xE0000000 view lies 0x1000 above its low 29 bits), the size in words (dword 1 bits 2 to 25) the
    storage's dwords. Dword 1's endian (bits 0 to 1) and padding (bits 26 to 31, where the game sets bit 28) are
    not this check's (impostor_check.py holds them). False for a row without a packet of six dwords."""
    six = (row.get("vfetch") or {}).get("packet")
    try:
        storage, dwords = int(row["storage"], 16), int(row["storage_dwords"])
        a, b = int(six[4], 16), int(six[5], 16)
    except (KeyError, TypeError, ValueError, IndexError):
        return False
    if len(six) != 6:
        return False
    address = (storage & 0x1FFFFFFF) + (((storage >> 20) + 0x200) & 0x1000)
    return (a & 3) == 3 and (a & 0xFFFFFFFC) == address and (b >> 2) & 0xFFFFFF == dwords


def check_verts_data(rows):
    """(checked, problems) over the in-scene "verts" rows (BeginVertices draws: the far woods' impostors,
    frame-map section 19). Each has a number "n", the whole vertex bank (1,024 floats), the builder's packet of
    fetch slot 31 naming the row's storage (packet_names_storage; the device's shadow of the slot is in the row
    too and is NOT required: it names the storage in no captured row) and exactly one "vertsdata" row of its
    frame and number, whose "want" is the storage whole (storage_dwords * 4), whose "bytes" are that many, read,
    and whose "at" (the capture frame the bytes were read in; rows from before the member have none) is the row's
    own frame. A storage of no bytes has no data
    row; one past the cap has none and is named. A capture from before the storage was kept (no in-scene row
    with a number and no data row at all) is not checked. A row outside the scene has neither member."""
    verts = [r for r in rows if r.get("kind") == "verts" and r.get("in_scene")]
    data = {}
    for r in rows:
        if r.get("kind") == "vertsdata":
            data.setdefault((r.get("frame"), r.get("n")), []).append(r)
    if not data and not any("n" in r for r in verts):
        return 0, []
    checked, problems, owned = 0, [], set()
    for r in verts:
        where = "frame %s verts %s" % (r.get("frame"), r.get("n"))
        if "n" not in r:
            problems.append("frame %s: an in-scene verts row without a number" % r.get("frame"))
            continue
        checked += 1
        key = (r.get("frame"), r.get("n"))
        owned.add(key)
        if len(r.get("bank") or []) != 1024:
            problems.append("%s: the vertex bank is not whole (%d floats)" % (where, len(r.get("bank") or [])))
        if r.get("storage_dwords") and not packet_names_storage(r):
            problems.append("%s: the builder's packet does not name the storage" % where)
        got = data.get(key, [])
        dwords = r.get("storage_dwords")
        if dwords is None:
            problems.append("%s: the device's storage fields were not read" % where)
            continue
        if dwords * 4 > VERTS_DATA_CAP:
            problems.append("%s: the storage is %d bytes, over the cap: not kept" % (where, dwords * 4))
            continue
        if dwords == 0 and not got:
            continue
        if len(got) != 1:
            problems.append("%s: %d data rows" % (where, len(got)))
            continue
        d = got[0]
        if d.get("want") != dwords * 4:
            problems.append("%s: the data row wants %s bytes of a storage of %d" % (where, d.get("want"), dwords * 4))
        elif not d.get("read"):
            problems.append("%s: the storage was not read" % where)
        else:
            text = d.get("bytes") or ""
            try:
                size = len(bytes.fromhex(text))
            except ValueError:
                size = -1
            if size != d["want"]:
                problems.append("%s: the data row has %s bytes of %d" % (where, "no hex for" if size < 0 else size, d["want"]))
            elif "at" in d and d["at"] != r.get("frame"):
                problems.append("%s: the storage was read in frame %s: after its frame's end" % (where, d["at"]))
    for key in sorted(k for k in data if k not in owned):
        problems.append("frame %s vertsdata %s: no verts row owns it" % key)
    return checked, problems


WATER_VS = "BEAD84BD72072E0E"   # texture_decode.h, kWaterVsHash: the lake's vertex shader
VIEW_PITCH = 280                # discovery_format.h, ReflectionViewPitch: a reflection view's surface pitch


def _fetch_words(fc):
    """The six dwords of a row's fetch constant as numbers, or None."""
    try:
        words = [int(x, 16) for x in fc]
    except (TypeError, ValueError):
        return None
    return words if len(words) == 6 else None


def _water_picture(fc):
    """texture_decode.h, WaterPictureFetch: a 2D k_8_8_8_8 texture of 256 x 256."""
    return ((fc[0] & 3) == 2 and ((fc[5] >> 9) & 3) == 1 and (fc[1] & 0x3F) == 6 and (fc[2] & 0x1FFF) == 255
            and ((fc[2] >> 13) & 0x1FFF) == 255)


def _normal_map(fc):
    """A 2D k_DXN texture with a mip address (texture_decode.h, DumpMipsAt: slot 2 of a water row)."""
    return (fc[0] & 3) == 2 and ((fc[5] >> 9) & 3) == 1 and (fc[1] & 0x3F) == 49 and (fc[5] & 0xFFFFF000) != 0


def _hash_of(text):
    try:
        return int(text, 16)
    except (TypeError, ValueError):
        return None


def check_water_rows(rows):
    """(checked, problems) over the water rows and the reflection views' rows (frame-map section 20).

    A water row is a "draw" row whose vertex shader is the lake's (WATER_VS). For each of its slots 13 and 14
    that holds a reflection picture (_water_picture), its "tfc" names the content the draw sampled, and a
    "texture" row with that content and the slot's base address stands before it in the capture (a content is
    written once, at the first row that samples it: a later frame's row names it again). Its slot 2, where it
    holds the normal map (_normal_map), has a "texture" row before it whose "mips" names the stored levels;
    a map without them is a problem once a base. A view's row is a "draw", "indx" or "tess" row outside the
    scene at surface pitch 280: it carries "vp" (the viewport's registers and those of the clip registers the
    device shadows). A capture made before these members were written fails on each of them: that is what the
    check is for."""
    checked, problems = 0, []
    contents = {}     # content hash -> the bases a texture row carried it at, so far
    levels = {}       # the base of a k_DXN texture row so far -> whether one of its rows has "mips"
    said = set()
    for r in rows:
        kind = r.get("kind")
        if kind == "texture":
            fc = _fetch_words(r.get("fc"))
            if fc is None:
                continue
            base = fc[1] & 0xFFFFF000
            content = _hash_of(r.get("content"))
            if content is not None:
                contents.setdefault(content, set()).add(base)
            if (fc[1] & 0x3F) == 49:
                levels[base] = levels.get(base, False) or bool((r.get("mips") or {}).get("levels"))
            continue
        if kind in ("draw", "indx", "tess") and r.get("in_scene") is False:
            surface = _hash_of(r.get("surface_info"))
            if surface is not None and (surface & 0x3FFF) == VIEW_PITCH:
                checked += 1
                if not isinstance(r.get("vp"), dict):
                    problems.append("frame %s: a %s row of a reflection view (pitch 280) has no \"vp\"" % (r.get("frame"), kind))
        shader = r.get("vs_hash") or (r.get("vs") or {}).get("hash") or ""
        if kind != "draw" or not str(shader).upper().endswith(WATER_VS):
            continue
        checked += 1
        where = "frame %s water row (ps %s)" % (r.get("frame"), r.get("ps_hash"))
        tf, tfc = r.get("tf") or {}, r.get("tfc") or {}
        for slot in ("13", "14"):
            fc = _fetch_words(tf.get(slot))
            if fc is None or not _water_picture(fc):
                continue
            base = fc[1] & 0xFFFFF000
            content = _hash_of(tfc.get(slot))
            if content is None:
                problems.append("%s: slot %s (0x%08X) names no content" % (where, slot, base))
            elif content not in contents:
                problems.append("%s: slot %s's content %s was never written" % (where, slot, tfc.get(slot)))
            elif base not in contents[content]:
                problems.append("%s: slot %s's content %s was written for another base, not 0x%08X" % (where, slot, tfc.get(slot), base))
        fc = _fetch_words(tf.get("2"))
        if fc is not None and _normal_map(fc):
            base = fc[1] & 0xFFFFF000
            if not levels.get(base) and base not in said:
                said.add(base)
                problems.append("%s: the normal map 0x%08X %s" % (
                    where, base, "was dumped without its stored levels" if base in levels else "was not dumped"))
    return checked, problems


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
    ap.add_argument("game_log", nargs="?")
    ap.add_argument("--rows-only", action="store_true",
                    help="the capture's own checks alone (index files, scene frames, level files, storages): for a run "
                         "made without --native_render_log_vertex_bindings=true, whose log has no [vbind] lines. Exit 0 "
                         "when they hold; the draws' vertex bindings are then NOT checked, and the output says so")
    a = ap.parse_args(argv)
    if not a.rows_only and not a.game_log:
        ap.error("a game log is needed (or --rows-only)")
    vbind = {}
    if a.game_log:
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
    with_data, verts_problems = check_verts_data(rows)
    if with_data or verts_problems:
        print(f"{with_data} in-scene verts rows checked for their storage, {len(verts_problems)} problems")
    water, water_problems = check_water_rows(rows)
    if water or water_problems:
        print(f"water rows: {water} checked, {len(water_problems)} problems")
    for line in (file_problems + frame_problems + mip_problems + verts_problems + water_problems)[:20]:
        print("  " + line)
    structural = not file_problems and not frame_problems and not mip_problems and not verts_problems and not water_problems
    if a.rows_only:
        # (asked for by name, never by default: the bound of 20 checked draws below is not relaxed)
        print("vertex bindings: NOT checked (--rows-only): %d logged shaders, %d draws matched of %d" % (len(vbind), matched, checked))
        return 0 if structural and matched == checked else 1
    if not vbind:
        print("vertex bindings: not checked, the log has no [vbind] lines (the run lacks "
              "--native_render_log_vertex_bindings=true); --rows-only runs the capture's own checks alone")
    return 0 if (checked >= 20 and matched == checked and structural) else 1


if __name__ == "__main__":
    raise SystemExit(main())
