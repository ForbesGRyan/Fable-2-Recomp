"""A discovery capture's rows and the game's own vertex microcode run on them (the foliage sub-project,
plan Task 3): the research's `cap.py` and `replay.py` (out/native-evidence/foliage/research) as one tracked
module, with the capture's path an argument. Used by foliage_replay.py and wind_check.py.

  rows(path, kinds, frame=None)   the capture's rows of those kinds, in file order, each with its "line"
  Draw(path, row)                 a "draw" row with its streams, constant bank and literals; .run(index) runs
                                  the vertex shader's microcode (ucode_eval) for one vertex index and returns
                                  its outputs ("oPos", "o0", ...)
  element(data, at, attrs)        one vertex element as a fetch delivers it
  pixels(clip, size)              a clip position in pixels from the picture's centre

What is read and what is not (said where it matters, never guessed):
  - a stream is read as the capture dumped it (8-in-32 swapped dwords, the only form in the captures: a row
    whose stream has another endian raises Unreadable);
  - the formats of the main scene's vertex shaders are decoded; the packed normals (10_11_11, 2_10_10_10,
    11_11_10) only approximately: they are not on any position path. Another format raises NotImplementedError;
  - a vertex texture fetch reads what `sampler` gives (zeros by default); Draw.texture_position() says whether
    the position depends on one;
  - a loop runs `loops[N]` times, 0 by default (the count is an integer constant no row holds): the push of
    the near plants away from characters (c[56 + a0]; spec ruling 10) is therefore not replayed.
"""
import json
import math
import struct
from pathlib import Path

import numpy as np

import ucode_eval as ue

REPO = Path(__file__).resolve().parents[2]
LOGS = REPO / "out" / "build" / "win-amd64-release" / "logs"
DUMPS = REPO / "out" / "shader_dump"
DROP = ("ptiles", "pbank_diff", "positions", "device_dwords")      # members no replay reads; large
SCENE_KINDS = ("draw", "tess", "indx", "verts")


class Unreadable(Exception):
    """The capture does not hold what the replay of a row needs; the text is the reason."""


def find_capture(name):
    """A capture's path: `name` itself, or the one capture in the logs folder whose name ends in it."""
    p = Path(name)
    if p.is_file():
        return p
    found = sorted(LOGS.glob("native_discovery_*%s.jsonl" % name)) if LOGS.is_dir() else []
    if len(found) != 1:
        raise Unreadable("%s: %d captures of that name in %s" % (name, len(found), LOGS))
    return found[0]


def rows(path, kinds=("draw", "tess", "frame", "resolve", "cmdgap", "indx", "verts"), frame=None, drop=DROP):
    """The capture's rows of `kinds` (of frame `frame` alone, if given; a "texture" row has no frame and is
    always kept when asked for), in file order, without the members of `drop`, each with "line" (from 1). A
    last line cut short by a killed capture is left out."""
    out = []
    mark = None if frame is None else '"frame":%d,' % frame
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            head = line[:64]
            if not head.startswith('{"kind":"'):
                continue
            kind = head[9:head.find('"', 9)]
            if kind not in kinds:
                continue
            if mark is not None and kind != "texture" and mark not in head:
                continue
            try:
                r = json.loads(line)
            except ValueError:
                continue
            for d in drop:
                r.pop(d, None)
            r["line"] = n
            out.append(r)
    return out


def vs_hash(r):
    h = r.get("vs_hash") or (r.get("vs") or {}).get("hash") or ""
    return (h[2:] if h.startswith("0x") else h).upper()


def ps_hash(r):
    h = r.get("ps_hash") or ""
    return (h[2:] if h.startswith("0x") else h).upper()


_PACKED = {"FMT_10_11_11": (10, 11, 11), "FMT_2_10_10_10": (10, 10, 10, 2), "FMT_11_11_10": (11, 11, 10)}


def element(data, at, attrs):
    """The four components of the vertex element at byte `at` of a stream dump, as the fetch of `attrs`
    (a vfetch's printed attributes) delivers them before its swizzle."""
    fmt = attrs["DataFormat"]
    if fmt == "FMT_16_16_16_16_FLOAT":
        m = struct.unpack_from(">4e", data, at)
        return [m[1], m[0], m[3], m[2]]
    if fmt == "FMT_16_16_FLOAT":
        m = struct.unpack_from(">2e", data, at)
        return [m[1], m[0], 0.0, 1.0]
    if fmt == "FMT_8_8_8_8":
        if attrs.get("Signed") == "true":
            raise NotImplementedError("FMT_8_8_8_8 signed")
        w = struct.unpack_from(">I", data, at)[0]
        p = [(w >> (8 * k)) & 0xFF for k in range(4)]
        return [float(x) for x in p] if attrs.get("NumFormat") == "integer" else [x / 255.0 for x in p]
    if fmt == "FMT_32_32_32_32_FLOAT":
        return list(struct.unpack_from(">4f", data, at))
    if fmt == "FMT_32_32_32_FLOAT":
        return list(struct.unpack_from(">3f", data, at)) + [1.0]
    if fmt == "FMT_32_32_FLOAT":
        return list(struct.unpack_from(">2f", data, at)) + [0.0, 1.0]
    if fmt in _PACKED:      # normals: not on the position path; approximate
        w = struct.unpack_from(">I", data, at)[0]
        out, sh = [], 0
        for b in _PACKED[fmt]:
            v = (w >> sh) & ((1 << b) - 1)
            sh += b
            if attrs.get("Signed") == "true":
                if v >= 1 << (b - 1):
                    v -= 1 << b
                out.append(max(-1.0, v / float((1 << (b - 1)) - 1)))
            else:
                out.append(v / float((1 << b) - 1))
        return (out + [1.0])[:4]
    raise NotImplementedError(fmt)


_files = {}


def stream_bytes(path):
    """A dump file's bytes (kept: a mesh's stream is named by many rows)."""
    key = str(path)
    if key not in _files:
        try:
            _files[key] = Path(path).read_bytes()
        except OSError:
            raise Unreadable("missing %s" % Path(path).name) from None
    return _files[key]


def _zeros(slot, coords, attrs):
    return [0.0, 0.0, 0.0, 0.0]


def _nans(slot, coords, attrs):
    return [math.nan] * 4


class Draw:
    """A "draw" row that has its geometry ("fetches", "streams", "bank"): its vertex shader's microcode, its
    constants (the bank's 256 registers with the shader's literals over them) and its stream dumps."""

    def __init__(self, path, row, dumps=DUMPS):
        self.row, self.path = row, Path(path)
        self.h = vs_hash(row)
        if not row.get("streams") or not row.get("bank"):
            raise Unreadable("no streams or no constant bank in the row")
        try:
            self.text = ue.load(dumps, self.h, "vert")
        except OSError:
            raise Unreadable("no microcode dump of vertex shader %s" % self.h) from None
        bank = row["bank"]
        self.consts = {i: list(bank[4 * i:4 * i + 4]) for i in range(len(bank) // 4)}
        for lit in row.get("vs_lit") or []:
            v = lit["v"]
            for k in range(len(v) // 4):
                self.consts[lit["reg"] + k] = [math.nan if x is None else x for x in v[4 * k:4 * k + 4]]
        self.streams = {}
        for s in row["streams"]:
            if not s.get("file"):
                continue
            if int(s.get("endian", 2)) != 2:
                raise Unreadable("stream slot %d has endian %s, not 2" % (s["slot"], s.get("endian")))
            self.streams[s["slot"]] = stream_bytes(self.path.parent / s["file"])
        self.M = np.array([bank[0:4], bank[4:8], bank[8:12], bank[12:16]], dtype=np.float64)
        self.W = np.array([bank[16:20], bank[20:24], bank[24:28]], dtype=np.float64)

    def run(self, index, consts=None, loops=None, sampler=_zeros):
        """(the shader's outputs for vertex index `index`, the machine). IndexError: a fetch outside a dump;
        Unreadable: a stream the capture did not dump."""
        stride = {}

        def vfetch(stream, offset, attrs, idx):
            if "Stride" in attrs:
                stride[stream] = int(attrs["Stride"])
            data = self.streams.get(95 - stream)
            if data is None:
                raise Unreadable("no dump of stream slot %d" % (95 - stream))
            if not math.isfinite(idx):
                raise IndexError("vf%d index %s" % (stream, idx))
            at = (int(round(idx)) * stride[stream] + offset) * 4
            if at < 0 or at + 4 > len(data):
                raise IndexError("vf%d index %s" % (stream, idx))
            try:
                return element(data, at, attrs)
            except struct.error:
                raise IndexError("vf%d index %s" % (stream, idx)) from None

        m = ue.Machine(self.text, consts or self.consts, sampler, regs={0: [float(index), 0.0, 0.0, 0.0]}, vfetch=vfetch,
                       loops=loops if loops is not None else {i: 0 for i in range(8)})
        return m.run(), m

    def texture_position(self, index):
        """Whether the position of vertex `index` depends on a vertex texture fetch (which no replay here
        reads): True when the microcode's oPos is a number with every fetch reading zero and is none with
        every fetch reading no number; None when the vertex cannot tell (its position is no number either
        way: a vertex the shader cuts); False for a shader without a texture fetch."""
        if "tfetch" not in self.text:
            return False
        if not all(math.isfinite(x) for x in self.run(index)[0]["oPos"]):
            return None
        return not all(math.isfinite(x) for x in self.run(index, sampler=_nans)[0]["oPos"])

    def rest(self, index):
        """The position the native pass draws for a plain "deformed" entry: the position fetch read as
        yxw1 (m0, m1, m2, 1), stream slot 95, offset 0."""
        f = self.row["fetches"][0]
        data = self.streams[f["slot"]]
        m = struct.unpack_from(">4e", data, index * f["stride"] * 4)
        return np.array([m[0], m[1], m[2], 1.0])

    def obj_from_clip(self, clip):
        return np.linalg.solve(self.M, np.array(clip, dtype=np.float64))


def pixels(clip, size=(1120, 720)):
    """A clip position in pixels from the picture's centre (x right, y up)."""
    w = clip[3]
    return np.array([clip[0] / w * size[0] / 2, clip[1] / w * size[1] / 2])
