"""Replays vs-transforms.json entries offline on a discovery capture's stream dumps.

  position_check.py <capture.jsonl> --json docs\\native-renderer\\vs-transforms.json
                    [--entry <file.json>] [--vs <HASH>] [--max-draws 200] [--cpp-fixture <HASH>]
                    [--bones <N>]

For every vertex shader with a transform entry, the positions of each sampled in-scene
draw's first indices (row "idx", at most 512) are built the way the native renderer builds
them and projected with the draw's own constants:
  plain     the position fetch and its swizzle       (src/native/capture/position_decode.h)
  instance  mesh vertex x per-copy rows + offset     (src/native/capture/instance_expand.h)
  skin      weighted bone rows applied per vertex    (src/native/capture/bone_skin.h)
A vertex is inside the clip volume when w > 0, |x| <= w, |y| <= w and 0 <= z <= w; a draw
passes when at least half of its sampled vertices are inside (NaN counts as outside). The
in-clip share is the share of sampled draws that pass.

Acceptance is relative to the capture, because the game draws many objects that are mostly
off screen: the threshold is the in-clip share of the trusted shader 0xECD66A10092E6562 in
the same capture (the table's own entry, same sampling) minus 0.10, never below 0.60, and
0.75 when the capture has no draws of it. A skin entry must also pass two structural
checks, since a wrong skin of a small on-screen object stays inside the clip volume:
  bone ortho share  the share of bone uses whose bone is orthonormal: the deviation of its
                    3x3 part under the entry's row swizzles (the largest of | |row| - 1 |
                    and |row_i . row_j|) is at most 0.05; pass at >= 0.90. A bone use is
                    one palette bone that one sampled draw's vertices reference with a
                    nonzero weight: a bone counts once per draw, however many of the
                    draw's vertices use it, and again in every other draw that uses it.
                    The largest deviation is printed too ("max") but is not a gate: the
                    game scales single bones on purpose, and such a bone can belong to a
                    mesh drawn many times a frame (a correct skin measured 0.972 to
                    0.976), while a wrong row order or swizzle breaks every bone (0).
  edge ok share     over the distinct triangle edges of the sampled draws (triangle lists,
                    fans and strips, cut at restart indices), the share whose skinned
                    length is 0.5 to 2.0 times the bind-pose length; pass at >= 0.98
Every line ends its threshold with the verdict, ACCEPT or REJECT. --bones N lists, under a
skin entry's line, its N bone uses that deviate most (frame, vertex stream, index range,
bone, row lengths).

The shares are taken over the draws judged, so two rules judge what leaves them; either one
rejects the entry whatever its shares, and the reason is printed after REJECT:
  - a sampled row the runtime would skip (unsupported or bad-index, below) rejects the entry;
  - the draws judged must be at least 20 and at least a tenth of the sampled rows (an entry
    whose draws are mostly wholly cut, below, is not judged on the few that remain).
Unreadable rows do not enter either rule.

An instance entry with a distance cut ("cut": the game's shader gives a NaN position to a vertex
whose flat position is farther from the eye constant than the square root of the distance
constant; src/native/fable2_native_shaders.h kClayVs does the same) is judged on the vertices
kept: a draw passes when at least half of its kept vertices are inside. A draw with no vertex
kept is not drawn by the game; it is counted `cut` and is not part of the share. The line
prints the cut draws and the kept share of the sampled vertices.

Rows that cannot be judged are counted apart from the draws:
  unreadable   a stream dump or a constant the replay needs is not in the capture
  unsupported  the runtime would not draw it: the entry does not match the decoded fetches
               (instance-unsupported / skin-unsupported) or an endian is not handled
  bad-index    garbage instance constants or a copy past the instance stream

--entry overlays candidate entries (same shape as vs-transforms.json) without touching the
table. --cpp-fixture prints, for one sampled draw of that shader, C++ arrays (the stream
bytes used, the constants, the first 16 expected positions) for a native regression test.
The exit code is always 0: the report is the result.
"""
import argparse
import json
import math
import statistics
import struct
import sys
from functools import lru_cache
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from gen_transform_table import cut_refs, reg_comp, swizzle_bits  # noqa: E402

BASELINE_VS = "0xECD66A10092E6562"   # trusted plain shader: its in-clip share is the capture's baseline
BASELINE_MARGIN = 0.10               # accept at the baseline share minus this ...
THRESHOLD_FLOOR = 0.60               # ... and never below this
THRESHOLD_FALLBACK = 0.75            # the capture has no baseline draws
MIN_JUDGED_DRAWS = 20                # an entry judged on fewer draws than this is rejected ...
MIN_JUDGED_SHARE = 0.10              # ... and so is one judged on less than this share of its sampled rows
ORTHO_MAX = 0.05                     # skin: a bone within this of orthonormal is orthonormal
ORTHO_SHARE_MIN = 0.90               # skin: share of bone uses that must be orthonormal
EDGE_RATIO = (0.5, 2.0)              # skin: skinned / bind-pose edge length
EDGE_OK_MIN = 0.98                   # skin: share of edges within EDGE_RATIO
EDGE_MIN_LENGTH = 1e-6               # bind-pose edges shorter than this are not measured
RESTART = -1                         # a restart (reset) index in a row's "idx"
MAX_INSTANCE_COPIES = 65536          # instance_expand.h kMaxInstanceCopies
FIXTURE_POSITIONS = 16
_BYTES = {57: 12, 38: 16, 32: 8, 26: 8}   # PositionBytes: float3, float4, half4, short4
_COMP = "xyzw"
NAN = math.nan
NAN_POSITION = (NAN, NAN, NAN, 1.0)


class Unreadable(Exception):
    """The capture does not hold what the replay needs (a dump, a constant, the indices)."""


class Unsupported(Exception):
    """The runtime skips the draw: the entry does not match the fetches, or an endian is not handled."""


class BadIndex(Exception):
    """The runtime skips the draw as bad-index (InstanceBoundsOk)."""


def f32(x):
    """x rounded to a float32, as every C++ `float` operation result is."""
    try:
        return struct.unpack("<f", struct.pack("<f", x))[0]
    except OverflowError:
        return math.copysign(math.inf, x)


def _trunc(x):
    return float(math.trunc(x)) if math.isfinite(x) else x


def _dot4(a, p):
    acc = f32(a[0] * p[0])
    for k in (1, 2, 3):
        acc = f32(acc + f32(a[k] * p[k]))
    return acc


class Stream:
    """One dumped vertex stream: `data` from the stream's offset, `total` bytes the stream holds."""

    def __init__(self, data, total, endian):
        self.data = data
        self.total = max(total, len(data))
        self.endian = endian

    def at(self, offset, size):
        """Byte offset if [offset, offset + size) can be read, None if it lies outside the stream."""
        if offset < 0 or offset + size > self.total:
            return None
        if offset + size > len(self.data):
            raise Unreadable("read past a truncated stream dump")
        return offset


class Layout:
    """position_decode.h PosLayout."""

    def __init__(self, f, swizzle_override):
        self.fmt = f["fmt"]
        self.signed = bool(f["signed"])
        self.norm = bool(f["norm"])
        self.exp = int(f["exp"])
        self.offset = int(f["off"]) * 4
        self.stride = int(f["stride"]) * 4
        self.slot = f["slot"]
        self.swz = swizzle_override or f["swz"]
        self.swap16 = False


def select_position(fetches, index, swizzle_override=0):
    """vfetch_decode.h SelectPosition: None where it returns false."""
    if index >= 0:
        if index >= len(fetches):
            return None
        f = fetches[index]
    else:
        f = next((c for c in fetches if not c["mini"]), None)
    if f is None or f["stride"] == 0 or f["fmt"] not in _BYTES or f["off"] < 0:
        return None
    return Layout(f, swizzle_override)


def apply_fetch_endian(layout, endian):
    """position_decode.h ApplyFetchEndian."""
    sixteen = layout.fmt in (32, 26)
    if sixteen and endian in (1, 2):
        layout.swap16 = endian == 2
        return True
    if not sixteen and endian == 2:
        layout.swap16 = False
        return True
    return False


def _short(raw, layout):
    if layout.signed:
        v = max(f32(raw / 32767.0), -1.0) if layout.norm else float(raw)
    else:
        v = f32(raw / 65535.0) if layout.norm else float(raw)
    return f32(math.ldexp(v, layout.exp)) if layout.exp else v


def decode_position(stream, layout, vertex):
    """position_decode.h DecodePositions for one vertex: (x, y, z, w), or None outside the stream."""
    if vertex < 0:
        return None
    at = stream.at(vertex * layout.stride + layout.offset, _BYTES[layout.fmt])
    if at is None:
        return None
    if layout.fmt == 57:
        m = struct.unpack_from(">3f", stream.data, at) + (1.0,)
    elif layout.fmt == 38:
        m = struct.unpack_from(">4f", stream.data, at)
    elif layout.fmt == 32:
        m = struct.unpack_from(">4e", stream.data, at)
    else:
        m = [_short(r, layout) for r in struct.unpack_from(">4h" if layout.signed else ">4H", stream.data, at)]
    pairs = layout.swap16 and layout.fmt in (32, 26)
    v = []
    for c in range(4):
        sel = (layout.swz >> (3 * c)) & 7
        if sel < 4:
            v.append(m[sel ^ 1 if pairs else sel])
        elif sel == 4:
            v.append(0.0)
        elif sel == 5:
            v.append(1.0)
        else:
            v.append(1.0 if c == 3 else 0.0)
    return tuple(v)


def skin_word(stream, offset, endian):
    """bone_skin.h detail::SkinWord: the 32-bit word as the GPU reads it, None outside the stream."""
    at = stream.at(offset, 4)
    if at is None or endian not in (0, 2):
        return None
    return struct.unpack_from(">I" if endian == 2 else "<I", stream.data, at)[0]


# --- Entries ------------------------------------------------------------------


def _row_swizzles(items):
    items = list(items or ["", "", ""])
    if len(items) != 3:
        raise ValueError("three row swizzles are required")
    return [swizzle_bits(s) if s else 0 for s in items]


def parse_entry(entry):
    """A vs-transforms.json entry as the generated table holds it (gen_transform_table.py);
    None if the entry has no transform or is a terrain entry (not a draw row). Raises ValueError for
    the entries the generator refuses: "instance" without "base", "skin" together with "instance", a
    skin with "pairs" but no "weight_fetch"."""
    if "instance" in entry and "base" not in entry:
        raise ValueError("instance entry without base")
    if "skin" in entry and "instance" in entry:
        raise ValueError("entry has both skin and instance")
    if "base" not in entry or "base2" in entry or "terrain" in entry:
        return None
    spec = {"kind": "plain", "base": int(entry["base"]), "combine": entry["layout"] != "dot",
            "pos_fetch": int(entry.get("pos_fetch", -1)),
            "pos_swizzle": swizzle_bits(entry["pos_swizzle"]) if "pos_swizzle" in entry else 0}
    if "instance" in entry:
        inst = entry["instance"]
        mesh = int(inst["mesh_fetch"])
        if "pos_fetch" in entry and entry["pos_fetch"] != mesh:
            raise ValueError("pos_fetch disagrees with instance mesh_fetch")
        rows = [int(r) for r in inst["row_fetches"]]
        if len(rows) != 3:
            raise ValueError("instance needs three row fetches")
        if not inst["offset"].endswith(".xyz"):
            raise ValueError("instance offset must be c<N>.xyz")
        spec.update(kind="instance", pos_fetch=mesh, rows=rows, row_swizzles=_row_swizzles(inst.get("row_swizzles")),
                    inv_count=reg_comp(inst["inv_count"]), count=reg_comp(inst["count"]),
                    first=reg_comp(inst["first"]), bias=float(inst["bias"]),
                    offset=reg_comp(inst["offset"][:-2]))
        spec["cut_eye"], spec["cut_dist2"] = cut_refs(inst)
    elif "skin" in entry:
        skin = entry["skin"]
        rows = [int(r) for r in skin["row_fetches"]]
        if len(rows) != 3:
            raise ValueError("skin needs three row fetches")
        if "pairs" in skin and "weight_fetch" not in skin:
            raise ValueError("skin has pairs but no weight_fetch")
        if "weight_fetch" in skin:
            pairs = [(_COMP.index(i), _COMP.index(w)) for i, w in skin["pairs"]]
            if not 1 <= len(pairs) <= 4:
                raise ValueError("skin needs 1 to 4 (index, weight) pairs")
            weight_fetch = int(skin["weight_fetch"])
        else:
            pairs = [(_COMP.index(skin["index_component"]), 0)]
            weight_fetch = -1
        spec.update(kind="skin", index_fetch=int(skin["index_fetch"]), weight_fetch=weight_fetch, rows=rows,
                    row_swizzles=_row_swizzles(skin.get("row_swizzles")), pairs=pairs)
    return spec


# --- Replay -------------------------------------------------------------------


def _constants(row, highest):
    """The row's vertex constants as a flat float list covering component index `highest`."""
    c = row.get("vconst") or []
    if highest >= len(c):
        c = row.get("bank") or c
    if highest >= len(c):
        raise Unreadable("vertex constants not in the row")
    return c


def _rows(fetches, spec, own_slot):
    """The three row layouts of an instance or skin entry (SelectInstanceRows / SelectSkin)."""
    rows = []
    for k in range(3):
        if spec["rows"][k] < 0:
            raise Unsupported("row fetch")
        layout = select_position(fetches, spec["rows"][k], spec["row_swizzles"][k])
        if layout is None or (rows and (layout.slot != rows[0].slot or layout.stride != rows[0].stride)):
            raise Unsupported("row fetch")
        rows.append(layout)
    if own_slot is not None and rows[0].slot == own_slot:
        raise Unsupported("rows in the mesh stream")
    return rows


def _skin_words(fetches, spec, pos):
    """bone_skin.h SelectSkin: byte offsets and shifts of the index and weight words."""
    def valid(i):
        return 0 <= i < len(fetches)

    def word(f, normalized, comp):
        if f["slot"] != pos.slot or f["stride"] * 4 != pos.stride:
            raise Unsupported("skin word not in the position stream")
        if f["fmt"] != 6 or bool(f["norm"]) != normalized or f["off"] < 0:
            raise Unsupported("skin word format")
        src = (f["swz"] >> (3 * comp)) & 7
        if src > 3:
            raise Unsupported("skin component not fetched")
        return f["off"] * 4, 8 * src

    weighted = spec["weight_fetch"] >= 0
    if not valid(spec["index_fetch"]) or (weighted and not valid(spec["weight_fetch"])):
        raise Unsupported("skin fetch")
    index_offset = weight_offset = 0
    index_shift, weight_shift = [], []
    for ic, wc in spec["pairs"]:
        index_offset, shift = word(fetches[spec["index_fetch"]], False, ic)
        index_shift.append(shift)
        if weighted:
            weight_offset, shift = word(fetches[spec["weight_fetch"]], True, wc)
            weight_shift.append(shift)
    return index_offset, index_shift, weight_offset, weight_shift, weighted


def instance_index(index, bias, inv_count, count, first):
    """instance_expand.h InstanceIndex: (copy, vertex) or None."""
    t = _trunc(f32(f32(float(index) + bias) * inv_count))
    c = f32(t + _trunc(first))
    v = f32(float(index) + _trunc(-f32(count * t)))
    if not math.isfinite(c) or not math.isfinite(v) or c < 0.0 or v < 0.0 or c >= MAX_INSTANCE_COPIES or \
            v >= 16777216.0:
        return None
    return int(c), int(v)


def sanitize_cut(eye, dist2):
    """draw_record.h SanitizeCut: (eye, squared distance) as float32, or None when the constants are not a
    finite eye and a finite squared distance of at least zero (garbage never cuts)."""
    eye = [f32(x) for x in eye]
    dist2 = f32(dist2)
    if not all(math.isfinite(x) for x in eye) or not math.isfinite(dist2) or dist2 < 0.0:
        return None
    return eye, dist2


def is_cut(p, cut):
    """kClayVs: dot(eye - p, eye - p) > dist2 in float32; false for a NaN position and without a cut."""
    if cut is None:
        return False
    eye, dist2 = cut
    d = [f32(eye[k] - p[k]) for k in range(3)]
    s = f32(d[0] * d[0])
    s = f32(s + f32(d[1] * d[1]))
    s = f32(s + f32(d[2] * d[2]))
    return s > dist2


def _instance(row, spec, pos, vb, get, flat, info):
    fetches = row["fetches"]
    rows = _rows(fetches, spec, pos.slot)
    stream = get(rows[0].slot)
    for layout in rows:
        if not apply_fetch_endian(layout, stream.endian):
            raise Unsupported("instance row endian")
    c = _constants(row, max(spec["inv_count"], spec["count"], spec["first"], spec["offset"] + 2))
    inv_count, count, first = (f32(c[spec[k]]) for k in ("inv_count", "count", "first"))
    bias = f32(spec["bias"])
    offset = [f32(c[spec["offset"] + k]) for k in range(3)]
    # instance_expand.h InstanceCopies and InstanceBoundsOk.
    end = max(r.offset + _BYTES[r.fmt] for r in rows)
    copies = 0 if stream.total < end else (stream.total - end) // rows[0].stride + 1
    vertices = vb.total // pos.stride
    if not all(math.isfinite(x) for x in (count, inv_count, first)):
        raise BadIndex("instance constants not finite")
    if count < 1.0 or count != _trunc(count) or first < 0.0 or abs(f32(count * inv_count) - 1.0) > 0.01:
        raise BadIndex("instance constants")
    if int(count) > vertices:
        raise BadIndex("more vertices per copy than the mesh stream holds")
    ib = row.get("ib") or {}
    largest = max(flat)
    if isinstance(ib.get("max_index"), int) and ib["max_index"] >= 0:
        largest = max(largest, ib["max_index"] + int(row.get("base_vertex", 0)))
    last = instance_index(largest, bias, inv_count, count, first) if largest >= 0 else None
    if last is None or last[0] >= copies:
        raise BadIndex("copy past the instance stream")
    # Every index must map inside its own copy: the first and the last index of each copy reached
    # (t never decreases with the index, so the ones between follow).
    n = int(count)
    reached = largest // n
    first_copy = last[0] - reached
    for k in range(reached + 1):
        lo = k * n
        hi = min(lo + n - 1, largest)
        if instance_index(lo, bias, inv_count, count, first) != (first_copy + k, 0) or \
                instance_index(hi, bias, inv_count, count, first) != (first_copy + k, hi - lo):
            raise BadIndex("an index maps outside its copy")
    info.update(copies_available=copies, vertices_per_copy=int(count), first=first, rows_slot=rows[0].slot,
                rows_stride=rows[0].stride, first_ref=spec["first"], pairs=[])
    out = []
    for i in flat:
        cv = instance_index(i, bias, inv_count, count, first) if i >= 0 else None
        info["pairs"].append(cv)
        p = decode_position(vb, pos, cv[1]) if cv else None
        r = [decode_position(stream, layout, cv[0]) for layout in rows] if p else None
        if not p or any(x is None for x in r):
            out.append(NAN_POSITION)
            continue
        out.append(tuple(f32(_dot4(r[k], p) + offset[k]) for k in range(3)) + (1.0,))
    # The distance cut is the vertex shader's: the flat positions stay as built, and info["cut"] marks
    # the vertices the shader drops (absent for an entry without a cut).
    if spec["cut_eye"] >= 0:
        k = _constants(row, max(spec["cut_eye"] + 2, spec["cut_dist2"]))
        cut = sanitize_cut(k[spec["cut_eye"]:spec["cut_eye"] + 3], k[spec["cut_dist2"]])
        info["cut"] = [is_cut(p, cut) for p in out]
    return out


def _skin(row, spec, pos, vb, get, flat, info):
    fetches = row["fetches"]
    index_offset, index_shift, weight_offset, weight_shift, weighted = _skin_words(fetches, spec, pos)
    rows = _rows(fetches, spec, None)
    palette = get(rows[0].slot)
    if vb.endian not in (0, 2):
        raise Unsupported("skin word endian")
    for layout in rows:
        if not apply_fetch_endian(layout, palette.endian):
            raise Unsupported("bone row endian")
    bones = []
    for b in range(256):
        r = [decode_position(palette, layout, b) for layout in rows]
        if any(x is None for x in r):
            break
        bones.append(r)
    if not bones:
        raise Unsupported("the palette holds no whole bone")
    info.update(palette_bones=len(bones), bone_max=-1, weight_sums=[], palette_slot=rows[0].slot,
                bone_stride=rows[0].stride, bind=[], bones_used={})
    out = []
    for v in flat:
        p = decode_position(vb, pos, v)
        info["bind"].append(p or NAN_POSITION)
        iw = skin_word(vb, v * pos.stride + index_offset, vb.endian) if p else None
        ww = skin_word(vb, v * pos.stride + weight_offset, vb.endian) if p and weighted else 0
        if p is None or iw is None or ww is None:
            out.append(NAN_POSITION)
            continue
        m = [[0.0] * 4 for _ in range(3)]
        used, bad, total = False, False, 0.0
        for k in range(len(index_shift)):
            w = f32(((ww >> weight_shift[k]) & 0xFF) / 255.0) if weighted else 1.0
            total += w
            if w == 0.0:
                continue                      # an unused influence may name any bone
            bone = (iw >> index_shift[k]) & 0xFF
            if bone >= len(bones):
                bad = True
                break
            info["bone_max"] = max(info["bone_max"], bone)
            info["bones_used"][bone] = bones[bone]
            for r in range(3):
                for c in range(4):
                    m[r][c] = f32(m[r][c] + f32(w * bones[bone][r][c]))
            used = True
        info["weight_sums"].append(total)
        out.append(NAN_POSITION if bad or not used else tuple(_dot4(m[r], p) for r in range(3)) + (1.0,))
    return out


def replay(row, spec, get):
    """Positions for the row's indices that are not restarts (base vertex added) and what the replay saw.
    `get(slot)` returns the Stream of a fetch slot or raises Unreadable."""
    fetches = row.get("fetches")
    idx = [int(i) for i in row.get("idx") or [] if int(i) >= 0]   # negative: a restart index, not a vertex
    if not fetches or not idx:
        raise Unreadable("no fetches or indices in the row")
    pos = select_position(fetches, spec["pos_fetch"], spec["pos_swizzle"])
    if pos is None:
        raise Unsupported("no position fetch")
    vb = get(pos.slot)
    if not apply_fetch_endian(pos, vb.endian):
        raise Unsupported("position endian")
    base = int(row.get("base_vertex", 0))
    flat = [i + base for i in idx]
    info = {"pos": pos, "flat": flat}
    if spec["kind"] == "instance":
        return _instance(row, spec, pos, vb, get, flat, info), info
    if spec["kind"] == "skin":
        return _skin(row, spec, pos, vb, get, flat, info), info
    return [decode_position(vb, pos, v) or NAN_POSITION for v in flat], info


def project(row, spec, p):
    """Clip position under the draw's transform rows (draw_record.h TransformLayout)."""
    c = _constants(row, spec["base"] * 4 + 15)
    rows = [c[spec["base"] * 4 + 4 * i:spec["base"] * 4 + 4 * i + 4] for i in range(4)]
    if spec["combine"]:
        return tuple(sum(p[k] * rows[k][i] for k in range(4)) for i in range(4))
    return tuple(sum(rows[i][k] * p[k] for k in range(4)) for i in range(4))


def in_clip(c):
    x, y, z, w = c
    return w > 0 and abs(x) <= w and abs(y) <= w and 0 <= z <= w   # NaN: every comparison is false


def stream_reader(row, capture_dir, read=None):
    """get(slot) over the row's "streams" and their dump files next to the capture. `read(path)`
    returns a file's bytes (check() passes a cached reader)."""
    by_slot = {s["slot"]: s for s in row.get("streams") or []}
    read = read or (lambda path: Path(path).read_bytes())

    def get(slot):
        s = by_slot.get(slot)
        if not s or not s.get("file"):
            raise Unreadable(f"no dump of stream slot {slot}")
        try:
            data = read(str(Path(capture_dir) / s["file"]))
        except OSError:
            raise Unreadable(f"missing {s['file']}") from None
        return Stream(data, int(s.get("total", len(data))), int(s["endian"]))
    return get


def positions_for(row, entry, capture_dir):
    """Pre-transform positions (x, y, z, w) for the row's "idx" (restart indices left out), base vertex added."""
    return replay(row, parse_entry(entry), stream_reader(row, capture_dir))[0]


# --- Capture ------------------------------------------------------------------


def _hash(text):
    return int(str(text), 16)


def scan(capture_path):
    """vertex shader hash -> file offsets of its in-scene draw rows."""
    index = {}
    with open(capture_path, "rb") as f:
        offset = 0
        for line in f:
            if b'"draw"' in line:
                try:
                    row = json.loads(line)
                    if row.get("kind") == "draw" and row.get("in_scene") and "vs_hash" in row:
                        index.setdefault(_hash(row["vs_hash"]), []).append(offset)
                except ValueError:
                    pass                      # a truncated last line
            offset += len(line)
    return index


def _specs(entries):
    """hash -> (entry key, spec or the entry's error text) for the entries with a transform."""
    out = {}
    for key, entry in entries.items():
        try:
            spec = parse_entry(entry)
        except (ValueError, KeyError, TypeError, AttributeError) as e:
            spec = f"{type(e).__name__}: {e}"
        if spec is not None:
            out[_hash(key)] = (key, spec)
    return out


def unlisted(capture_path, entries, index=None):
    """In-scene vertex shaders with no transform entry: "0x<HASH16>" -> draw rows, most rows first."""
    index = scan(capture_path) if index is None else index
    known = set(_specs(entries)) | {_hash(k) for k, e in entries.items() if "terrain" in e}
    rows = {h: len(o) for h, o in index.items() if h not in known}
    return {f"0x{h:016X}": n for h, n in sorted(rows.items(), key=lambda kv: (-kv[1], kv[0]))}


def bone_ortho(rows):
    """How far a bone's 3x3 part (its three rows' xyz) is from orthonormal: the largest of
    | |row_k| - 1 | and |row_i . row_j|; infinity if a component is not finite."""
    r = [tuple(row[:3]) for row in rows]
    if not all(math.isfinite(x) for row in r for x in row):
        return math.inf
    worst = 0.0
    for k in range(3):
        other = r[(k + 1) % 3]
        worst = max(worst, abs(math.sqrt(sum(x * x for x in r[k])) - 1.0), abs(sum(a * b for a, b in zip(r[k], other))))
    return worst


def triangles(prim, idx):
    """Triangles of a draw's indices as ordinals into the indices that are not restarts (the order
    of the replayed positions). `prim` is the Xenos primitive type: 4 triangle list, 5 triangle fan,
    6 triangle strip; None for any other type. A restart index (negative in "idx") cuts the run."""
    if prim not in (4, 5, 6):
        return None
    out, run, n = [], [], 0
    for i in list(idx) + [RESTART]:
        if int(i) >= 0:
            run.append(n)
            n += 1
            continue
        if prim == 4:
            out += [tuple(run[k:k + 3]) for k in range(0, len(run) - 2, 3)]
        elif prim == 6:
            out += [tuple(run[k:k + 3]) for k in range(len(run) - 2)]
        else:
            out += [(run[0], run[k], run[k + 1]) for k in range(1, len(run) - 1)]
        run = []
    return out


def edge_stats(tris, flat, bind, skinned):
    """(edges whose skinned / bind-pose length is within EDGE_RATIO, edges measured, edges touching a
    NaN vertex) over the distinct edges of `tris`. `flat` names the vertex behind each position;
    edges shorter than EDGE_MIN_LENGTH in the bind pose are not measured."""
    def length(p, q):
        return math.sqrt(sum((a - b) ** 2 for a, b in zip(p[:3], q[:3])))

    seen = set()
    ok = measured = nan = 0
    for t in tris:
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[0], t[2])):
            edge = (min(flat[a], flat[b]), max(flat[a], flat[b]))
            if edge[0] == edge[1] or edge in seen:
                continue
            seen.add(edge)
            if not all(math.isfinite(x) for p in (bind[a], bind[b], skinned[a], skinned[b]) for x in p[:3]):
                nan += 1
                continue
            rest = length(bind[a], bind[b])
            if rest < EDGE_MIN_LENGTH:
                continue
            measured += 1
            ok += EDGE_RATIO[0] - 1e-9 <= length(skinned[a], skinned[b]) / rest <= EDGE_RATIO[1] + 1e-9
    return ok, measured, nan


def _row_triangles(row):
    """The row's triangles, or None when they cannot be built: a primitive type other than list,
    fan or strip, or a capture from before restart indices were kept in "idx" (no "idx_resets")
    whose draw has restarts, so the cuts between its strips are lost."""
    args = row.get("args") or []
    try:
        prim = int(str(args[0]), 16)
    except (IndexError, ValueError):
        return None
    if "idx_resets" not in row and ((row.get("ib") or {}).get("restarts") or 0) > 0:
        return None
    return triangles(prim, row["idx"])


def reject_reason(r):
    """Why an entry is rejected whatever its shares, or None. The in-clip share is taken over the draws
    judged, so what leaves it is judged here:
      - a sampled row the runtime would skip (unsupported or bad-index) rejects the entry: the game
        draws that row and the native renderer would not;
      - the draws judged must be at least MIN_JUDGED_DRAWS and at least MIN_JUDGED_SHARE of the sampled
        rows (wholly cut draws are sampled rows that are not judged).
    Unreadable rows (a dump missing from the capture) say nothing about the entry: they are left out
    of the sampled rows here and only printed."""
    if r["kind"] == "error":
        return None
    skipped = r["unsupported"] + r["bad_index"]
    if skipped:
        return f"the runtime would skip {skipped} of {r['sampled']} sampled rows"
    floor = math.ceil(max(MIN_JUDGED_DRAWS, MIN_JUDGED_SHARE * (r["sampled"] - r["unreadable"])) - 1e-9)
    if r["draws"] < floor:
        return f"judged on {r['draws']} draws, fewer than {floor}"
    return None


def accepted(r, threshold):
    """The verdict: no reject_reason, the in-clip share reaches the capture's threshold and, for a skin
    entry, at least ORTHO_SHARE_MIN of the bone uses are orthonormal within ORTHO_MAX and at least
    EDGE_OK_MIN of the edges keep their length. The largest bone deviation is reported, not judged."""
    if r["kind"] == "error" or not r["draws"] or reject_reason(r) or r["share"] < threshold - 1e-9:
        return False
    if r["kind"] != "skin":
        return True
    return (r["bone_ortho_share"] is not None and r["bone_ortho_share"] >= ORTHO_SHARE_MIN - 1e-9 and
            r["edge_ok_share"] is not None and r["edge_ok_share"] >= EDGE_OK_MIN - 1e-9)


def _bone_use(row, info, bone, rows, deviation):
    """One bone use for the --bones listing: where it is and what its rows look like."""
    vb = row.get("vb") or {}
    ib = row.get("ib") or {}
    return {"deviation": deviation, "frame": row.get("frame"), "vb": vb.get("phys_addr"), "start": ib.get("start"),
            "count": ib.get("count"), "bone": bone, "palette_bones": info["palette_bones"],
            "row_lengths": [math.sqrt(sum(x * x for x in r[:3])) for r in rows]}


def _judge(f, capture_path, key, spec, offsets, max_draws, read, want_fixture, worst_bones=0):
    """Replays one shader's sampled draws (`max_draws` rows spread evenly over `offsets`). With
    `worst_bones` the result of a skin entry also holds "bone_worst": its bone uses that deviate most
    from orthonormal, worst first."""
    n = min(max(int(max_draws), 0), len(offsets))
    r = {"kind": spec["kind"], "rows": len(offsets), "sampled": n, "draws": 0, "passed": 0, "share": 0.0,
         "unreadable": 0, "unsupported": 0, "bad_index": 0, "reasons": {}}
    if spec.get("cut_eye", -1) >= 0:
        r.update(cut=0, kept_vertices=0, sampled_vertices=0)
    if spec["kind"] == "skin":
        r.update(bone_ortho_max=None, bone_ortho_share=None, bone_uses=0, bone_ortho_ok=0, edge_ok_share=None,
                 edges=0, edge_ok=0, edge_nan=0, edge_draws=0)
    weight_sums, per_copy, fixture, worst = [], [], None, []
    for k in range(n):
        f.seek(offsets[k * len(offsets) // n])
        row = json.loads(f.readline())
        get = stream_reader(row, capture_path.parent, read)
        try:
            positions, info = replay(row, spec, get)
            dropped = info.get("cut") or [False] * len(positions)
            kept = len(positions) - sum(dropped)
            inside = sum(1 for p, gone in zip(positions, dropped) if not gone and in_clip(project(row, spec, p)))
        except (Unreadable, Unsupported, BadIndex) as e:
            which = {Unreadable: "unreadable", Unsupported: "unsupported", BadIndex: "bad_index"}[type(e)]
            r[which] += 1
            r["reasons"][str(e)] = r["reasons"].get(str(e), 0) + 1
            continue
        if spec["kind"] == "instance":
            per_copy.append(info["vertices_per_copy"])
            copy_max = max((cv[0] for cv in info["pairs"] if cv), default=-1)
            if copy_max > r.get("copies_max", -1) or "copies_available" not in r:
                r["copies_max"], r["copies_available"] = copy_max, info["copies_available"]
        if "cut" in r:
            r["kept_vertices"] += kept
            r["sampled_vertices"] += len(positions)
            if kept == 0:             # the game draws nothing of it: not part of the share
                r["cut"] += 1
                continue
        r["draws"] += 1
        passed = inside * 2 >= kept
        r["passed"] += passed
        if spec["kind"] == "skin":
            weight_sums += info["weight_sums"]
            if info["bone_max"] > r.get("bone_max", -1) or "palette_bones" not in r:
                r["bone_max"], r["palette_bones"] = info["bone_max"], info["palette_bones"]
            # One bone use per bone the draw's sampled vertices reference with a nonzero weight.
            for bone, bone_rows in info["bones_used"].items():
                deviation = bone_ortho(bone_rows)
                r["bone_uses"] += 1
                r["bone_ortho_ok"] += deviation <= ORTHO_MAX
                r["bone_ortho_max"] = max(r["bone_ortho_max"] or 0.0, deviation)
                if worst_bones > 0:
                    worst.append(_bone_use(row, info, bone, bone_rows, deviation))
            if len(worst) > 4 * max(worst_bones, 64):               # keep the list short while scanning
                worst = sorted(worst, key=lambda u: -u["deviation"])[:worst_bones]
            tris = _row_triangles(row)
            if tris is not None:
                ok, measured, nan = edge_stats(tris, info["flat"], info["bind"], positions)
                r["edge_draws"] += 1
                r["edge_ok"] += ok
                r["edges"] += measured
                r["edge_nan"] += nan
        if want_fixture:
            finite = all(math.isfinite(x) for p in positions[:FIXTURE_POSITIONS] for x in p)
            rank = (passed, finite)
            if fixture is None or rank > fixture[0]:
                fixture = (rank, row, positions, info, get)
    r["share"] = r["passed"] / r["draws"] if r["draws"] else 0.0
    if r.get("edges"):
        r["edge_ok_share"] = r["edge_ok"] / r["edges"]
    if r.get("bone_uses"):
        r["bone_ortho_share"] = r["bone_ortho_ok"] / r["bone_uses"]
    if spec["kind"] == "skin" and worst_bones > 0:
        r["bone_worst"] = sorted(worst, key=lambda u: -u["deviation"])[:worst_bones]
    if weight_sums:
        r["weight_sum_min"], r["weight_sum_max"] = min(weight_sums), max(weight_sums)
        r["weight_sum_median"] = statistics.median(weight_sums)
    if per_copy:
        r["vertices_per_copy"] = [min(per_copy), max(per_copy)]
    if fixture:
        r["fixture"] = cpp_fixture(key, capture_path.name, spec, *fixture[1:])
    return r


def check(capture_path, entries, max_draws=200, only_vs=None, index=None, fixture_vs=None, baseline_entries=None,
          worst_bones=0):
    """Replays every entry on its sampled in-scene draws: entry key -> {kind, rows, sampled, draws, passed,
    share, accepted, unreadable, unsupported, bad_index, ...}; a skin entry also has bone_uses,
    bone_ortho_ok, bone_ortho_share, bone_ortho_max, edge_ok_share, edges, edge_nan and edge_draws, and
    with `worst_bones` "bone_worst" (its bone uses that deviate most). "_baseline" holds the capture's acceptance
    threshold: {"share", "threshold", "draws", "fallback"}, from the BASELINE_VS entry of
    `baseline_entries` (the table; default `entries`) under the same sampling. With `fixture_vs` the
    result of that shader also holds "fixture": the C++ text of one of its draws."""
    capture_path = Path(capture_path)
    index = scan(capture_path) if index is None else index
    read = lru_cache(maxsize=256)(lambda path: Path(path).read_bytes())
    trusted = _specs(entries if baseline_entries is None else baseline_entries).get(_hash(BASELINE_VS))
    baseline = {"share": None, "threshold": THRESHOLD_FALLBACK, "draws": 0, "fallback": True}
    report = {"_baseline": baseline}
    with open(capture_path, "rb") as f:
        if trusted and not isinstance(trusted[1], str):
            b = _judge(f, capture_path, trusted[0], trusted[1], index.get(_hash(BASELINE_VS), []), max_draws, read,
                       False)
            if b["draws"]:
                baseline.update(share=b["share"], threshold=max(b["share"] - BASELINE_MARGIN, THRESHOLD_FLOOR),
                                draws=b["draws"], fallback=False)
        for h, (key, spec) in _specs(entries).items():
            if only_vs is not None and h != _hash(only_vs):
                continue
            offsets = index.get(h, [])
            if isinstance(spec, str):
                r = {"kind": "error", "error": spec, "rows": len(offsets), "sampled": 0, "draws": 0, "passed": 0,
                     "share": 0.0, "unreadable": 0, "unsupported": 0, "bad_index": 0}
            else:
                r = _judge(f, capture_path, key, spec, offsets, max_draws, read,
                           fixture_vs is not None and h == _hash(fixture_vs), worst_bones)
            r["accepted"] = accepted(r, baseline["threshold"])
            report[key] = r
    return report


# --- C++ fixture --------------------------------------------------------------


def _cpp_float(x):
    if math.isnan(x):
        return "NAN"
    if math.isinf(x):
        return "INFINITY" if x > 0 else "-INFINITY"
    text = "%.9g" % x
    return text + ("f" if any(c in text for c in ".e") else ".0f")


def _cpp_bytes(name, data, note):
    lines = [f"// {note}", f"static const uint8_t {name}[{len(data)}] = {{"]
    for at in range(0, len(data), 16):
        lines.append("    " + " ".join(f"0x{b:02X}," for b in data[at:at + 16]))
    return lines + ["};"]


def _same(a, b):
    return all((math.isnan(x) and math.isnan(y)) or x == y for p, q in zip(a, b) for x, y in zip(p, q))


def cpp_fixture(key, capture_name, spec, row, positions, info, get):
    """C++ arrays reproducing the first positions of one draw from the bytes they use: the vertices of a
    plain or skinned draw are re-indexed into a compact stream, a palette is cut after its last bone
    used, and an instanced draw keeps its indices with the copies' rows rebased to the first one used."""
    n = min(FIXTURE_POSITIONS, len(positions))
    pos, flat = info["pos"], info["flat"][:n]
    vb = get(pos.slot)
    row = dict(row, idx=list(flat), base_vertex=0, fetches=row["fetches"], vconst=list(row.get("vconst") or []))
    row.pop("ib", None)
    streams = {}
    notes = {}

    def vertex_bytes(stream, stride, v):
        return stream.data[v * stride:(v + 1) * stride].ljust(stride, b"\0")

    if spec["kind"] == "instance":
        pairs = [cv for cv in info["pairs"][:n] if cv] or [(0, 0)]
        lo, hi = min(cv[0] for cv in pairs), max(cv[0] for cv in pairs)
        rows = get(info["rows_slot"])
        stride = info["rows_stride"]
        streams[info["rows_slot"]] = Stream(rows.data[lo * stride:(hi + 1) * stride], 0, rows.endian)
        notes[info["rows_slot"]] = f"copies {lo}..{hi}, {stride} bytes per copy (first copy rebased from " \
                                   f"{int(info['first'])} to {int(info['first']) - lo})"
        if len(row["vconst"]) > info["first_ref"]:
            row["vconst"][info["first_ref"]] = float(_trunc(info["first"]) - lo)
        vertices = max(info["vertices_per_copy"], max(cv[1] for cv in pairs) + 1)
        streams[pos.slot] = Stream(vb.data[:vertices * pos.stride], 0, vb.endian)
        notes[pos.slot] = f"mesh vertices 0..{vertices - 1}, {pos.stride} bytes per vertex"
    else:
        used = sorted({v for v in flat if v >= 0 and (v + 1) * pos.stride <= len(vb.data)})
        remap = {v: k for k, v in enumerate(used)}
        row["idx"] = [remap.get(v, len(used)) for v in flat]     # an index past the stream stays past it
        streams[pos.slot] = Stream(b"".join(vertex_bytes(vb, pos.stride, v) for v in used), 0, vb.endian)
        notes[pos.slot] = f"guest vertices {', '.join(str(v) for v in used)} re-indexed from 0, " \
                          f"{pos.stride} bytes per vertex"
        if spec["kind"] == "skin":
            palette = get(info["palette_slot"])
            bones = max(info["bone_max"], 0) + 1
            streams[info["palette_slot"]] = Stream(palette.data[:bones * info["bone_stride"]], 0, palette.endian)
            notes[info["palette_slot"]] = f"bones 0..{bones - 1}, {info['bone_stride']} bytes per bone"

    def compact(slot):
        if slot not in streams:
            raise Unreadable(f"stream slot {slot} is not in the fixture")
        return streams[slot]

    expected = positions[:n]
    try:
        reproduced = _same(replay(row, spec, compact)[0], expected)
    except (Unreadable, Unsupported, BadIndex) as e:
        reproduced = False
        notes["error"] = str(e)
    out = [f"// position_check.py --cpp-fixture {key}: {capture_name}, frame {row.get('frame', '?')}, "
           f"{spec['kind']} entry, transform rows c{spec['base']}..c{spec['base'] + 3} "
           f"({'combine' if spec['combine'] else 'dot'})."]
    if not reproduced:
        out.append(f"// WARNING: the compact streams do not reproduce the expected positions ({notes.get('error')}).")
    out.append("// Fetches in DecodeVertexFetches order: {instr_index, fetch_slot, dst_reg, dst_swizzle, format, "
               "is_signed, normalized, mini, exp_adjust, stride_dwords, offset_dwords}.")
    out.append(f"static const VertexFetch kFixtureFetches[{len(row['fetches'])}] = {{")
    for f in row["fetches"]:
        fields = [f.get("instr", f["i"]), f["slot"], f.get("reg", 0), "0x%03X" % f["swz"], f["fmt"],
                  *(str(bool(f[k])).lower() for k in ("signed", "norm", "mini")), f["exp"], f["stride"], f["off"]]
        out.append("    {" + ", ".join(str(x) for x in fields) + "},")
    out.append("};")
    for slot, s in sorted(streams.items(), reverse=True):
        out += _cpp_bytes(f"kFixtureStream{slot}", s.data, f"Fetch slot {slot}, fetch constant endian {s.endian}: "
                                                         f"{notes[slot]}.")
    out.append(f"static const uint32_t kFixtureIndices[{n}] = {{{', '.join(str(i) for i in row['idx'])}}};")
    out.append(f"static const float kFixtureVconst[{len(row['vconst'])}] = {{  // c0.. as the draw had them")
    for at in range(0, len(row["vconst"]), 4):
        out.append("    " + " ".join(_cpp_float(f32(x)) + "," for x in row["vconst"][at:at + 4]))
    out.append("};")
    out.append(f"static const float kFixtureExpected[{n}][4] = {{  // positions before the transform rows")
    out += ["    {" + ", ".join(_cpp_float(x) for x in p) + "}," for p in expected]
    out.append("};")
    return "\n".join(out)


# --- CLI ----------------------------------------------------------------------


def format_baseline(baseline):
    if baseline["fallback"]:
        return f"baseline {BASELINE_VS}: no draws in this capture, accept >= {baseline['threshold']:.3f} (fallback)"
    return f"baseline {BASELINE_VS}: share {baseline['share']:.3f}, accept >= {baseline['threshold']:.3f}"


def format_line(key, r, threshold):
    if r["kind"] == "error":
        return f"VS {key} entry error: {r['error']} (rows {r['rows']})"

    def number(x):
        return "n/a" if x is None else f"{x:.3f}"

    reason = reject_reason(r)
    line = (f"VS {key} {r['kind']}: draws {r['draws']}, in-clip share {r['share']:.3f} (accept >= {threshold:.3f}) "
            f"{'ACCEPT' if accepted(r, threshold) else 'REJECT'}{f' ({reason})' if reason else ''}, "
            f"passed {r['passed']}, unreadable {r['unreadable']}, unsupported {r['unsupported']}, "
            f"bad-index {r['bad_index']}, rows {r['rows']}")
    if r["kind"] == "skin":
        share = "n/a" if r["bone_ortho_share"] is None else f"{r['bone_ortho_share']:.4f}"
        line += (f", bone ortho share {share} ({r['bone_ortho_ok']} of {r['bone_uses']}, >= {ORTHO_SHARE_MIN:.2f}), "
                 f"max {number(r['bone_ortho_max'])}, edge ok share "
                 f"{number(r['edge_ok_share'])} (>= {EDGE_OK_MIN:.2f}) of {r['edges']} edges, edge nan {r['edge_nan']}, "
                 f"edge draws {r['edge_draws']}")
    if "weight_sum_median" in r:
        line += (f", weight sum min/median/max {r['weight_sum_min']:.3f}/{r['weight_sum_median']:.3f}/"
                 f"{r['weight_sum_max']:.3f}")
    if "palette_bones" in r:
        line += f", bone index max {r['bone_max']} / palette bones {r['palette_bones']}"
    if "copies_available" in r:
        lo, hi = r["vertices_per_copy"]
        line += (f", copies max {r['copies_max']} / available {r['copies_available']}, vertices per copy "
                 f"{lo if lo == hi else f'{lo}-{hi}'}")
    if "cut" in r:
        share = r["kept_vertices"] / r["sampled_vertices"] if r["sampled_vertices"] else 0.0
        line += f", cut {r['cut']}, kept vertices {r['kept_vertices']} of {r['sampled_vertices']} ({share:.3f})"
    if r.get("reasons"):
        line += " [" + "; ".join(f"{k}: {v}" for k, v in sorted(r["reasons"].items())) + "]"
    return line


def format_bones(worst):
    """The --bones lines: one bone use each, worst first."""
    def hexa(x):
        return "?" if x is None else f"0x{x:08X}"

    return [f"  bone use: deviation {u['deviation']:.3f}, frame {u['frame'] if u['frame'] is not None else '?'}, "
            f"vb {hexa(u['vb'])}, indices {u['start'] if u['start'] is not None else '?'}+"
            f"{u['count'] if u['count'] is not None else '?'}, bone {u['bone']} of {u['palette_bones']}, "
            f"row lengths {' '.join(f'{x:.3f}' for x in u['row_lengths'])}" for u in worst]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--json", required=True, help="vs-transforms.json")
    ap.add_argument("--entry", help="candidate entries overlaid on the table")
    ap.add_argument("--vs", help="check only this vertex shader hash")
    ap.add_argument("--max-draws", type=int, default=200, help="draws sampled per shader, spread over the capture")
    ap.add_argument("--cpp-fixture", metavar="HASH", help="print C++ arrays for one sampled draw of this shader")
    ap.add_argument("--bones", type=int, default=0, metavar="N",
                    help="list each skin entry's N bone uses that deviate most from orthonormal")
    a = ap.parse_args(argv)
    table = json.loads(Path(a.json).read_text())
    entries = dict(table)
    if a.entry:
        entries.update(json.loads(Path(a.entry).read_text()))
    index = scan(a.capture)
    report = check(a.capture, entries, a.max_draws, a.vs, index, a.cpp_fixture, table, max(a.bones, 0))
    baseline = report.pop("_baseline")
    print(format_baseline(baseline))
    seen = {k: r for k, r in report.items() if r["rows"]}
    for key, r in sorted(seen.items(), key=lambda kv: -kv[1]["rows"]):
        print(format_line(key, r, baseline["threshold"]))
        for line in format_bones(r.get("bone_worst") or []):
            print(line)
    missing = {} if a.vs else unlisted(a.capture, entries, index)
    for key, rows in missing.items():
        print(f"VS {key} no entry: rows {rows}")
    judged = [r for r in seen.values() if r["draws"]]
    print(f"totals: in-scene draw rows {sum(len(o) for o in index.values())}, shaders {len(index)}; with an entry "
          f"{len(seen)} ({sum(r['rows'] for r in seen.values())} rows), accepted "
          f"{sum(1 for r in judged if r['accepted'])} of {len(judged)} judged; no entry {len(missing)} "
          f"({sum(missing.values())} rows)")
    for key, r in report.items():
        if "fixture" in r:
            print()
            print(r["fixture"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
