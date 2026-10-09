"""The fog tables, the sky draw and the scene picture of a fog discovery capture (frame-map section 16).

Decoders (pure, tested on hand-made bytes in tests/test_fog_tables.py):
  decode_tf4(bytes)            -> 64 x (r, g, b, a): the in-scatter table, 1D k_16_16_16_16_EXPAND read as
                                  half floats (the SDK presents the format as R16G16B16A16_FLOAT), endian
                                  8-in-16: each half float is stored high byte first, in the order r, g, b, a.
  decode_tf5(bytes)            -> 256 x (r, g, b, a): the transmittance table, 1D k_8_8_8_8, endian 8-in-32,
                                  after the fetch constant's swizzle 0x60A: a texel is stored a, r, g, b.
  decode_1d(bytes, fc)         -> the same reading taken from a 1D fetch constant (format, endian, swizzle),
                                  for tables whose constants are not known beforehand (the trees' tf16).
  decode_2d(bytes, fc)         -> (width, height, texels) of the base level of an uncompressed 2D texture.
  untile_scene(bytes, w, h)    -> the scene colour the post chain samples (2D k_16_16_16_16_EXPAND, tiled, the
                                  pitch the width rounded up to 32, endian 8-in-16) as w * h * 4 floats, row-major.
  sample_1d(table, u)          -> a linear, clamped lookup with texel centres at (i + 0.5) / n. This tool's own
                                  reading for its reports; the convention of the game's tfetch1D is settled in
                                  light_formulas (plan Task 2), not here.
  fit_exponential(table, c67w) -> per channel the k of exp(-k d) nearest the table, d the distance of each texel
                                  centre ((i + 0.5) / n / c67.w), and the largest error of that curve.

Reports (every number is of the capture named on the command line):
  fog_tables.py tables <capture.jsonl> [--dump <file.json>]  the tables per frame: addresses, ranges, change from
                                             frame to frame, the exponential fit against c64 / c65
  fog_tables.py trees  <capture.jsonl>       the vertex shaders' tf16 / tf17 against tf5 and {c68, tf4}
  fog_tables.py sky    <capture.jsonl>       the "indx" rows (builder 0x82217EE8): the sky draw's textures,
                                             constants, vertex constants and the cube
  fog_tables.py scene  <capture.jsonl> --png <out.png> [--frame N] [--scale 16]
                                             the scene colour of a capture made with --readback_resolve=full:
                                             which rows sampled which content, statistics, a picture
The texture dumps are read relative to the capture's folder (or --tex-root).

A capture made by a build with fog discovery has, in every decoded row, "tfc": the content hash of each sampled
texture that the dump keeps by content (1D tables, colour and depth targets), and "texture" rows with "frame"
and "content". An older capture has neither: the reports then say that nothing was dumped.
"""
import argparse
import json
import math
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from texture_thumb import tiled_offset_2d, write_png  # noqa: E402

TF4_TEXELS, TF5_TEXELS = 64, 256
SKY_PS = "F7601FBE485936FD"
SCENE_FORMAT = 29


# --- texel decoding -----------------------------------------------------------------------------------------

def decode_tf4(data):
    if len(data) < TF4_TEXELS * 8:
        raise ValueError("tf4 needs %d bytes, got %d" % (TF4_TEXELS * 8, len(data)))
    v = struct.unpack(">%de" % (TF4_TEXELS * 4), data[:TF4_TEXELS * 8])
    return [tuple(v[4 * i:4 * i + 4]) for i in range(TF4_TEXELS)]


def decode_tf5(data):
    if len(data) < TF5_TEXELS * 4:
        raise ValueError("tf5 needs %d bytes, got %d" % (TF5_TEXELS * 4, len(data)))
    return [(data[4 * i + 1] / 255.0, data[4 * i + 2] / 255.0, data[4 * i + 3] / 255.0, data[4 * i] / 255.0)
            for i in range(TF5_TEXELS)]


def _swap(data, endian):
    """The GPU's endian swap (xenos::Endian) of whole 2- or 4-byte groups."""
    b = bytearray(data)
    if endian == 1:
        n = len(b) & ~1
        b[0:n:2], b[1:n:2] = b[1:n:2], b[0:n:2]
    elif endian == 2:
        n = len(b) & ~3
        b[0:n:4], b[1:n:4], b[2:n:4], b[3:n:4] = b[3:n:4], b[2:n:4], b[1:n:4], b[0:n:4]
    elif endian == 3:
        n = len(b) & ~3
        b[0:n:4], b[1:n:4], b[2:n:4], b[3:n:4] = b[2:n:4], b[3:n:4], b[0:n:4], b[1:n:4]
    return bytes(b)


# format -> (bytes a texel, struct code of one component after the endian swap, components, divisor or None).
# The channels a shader sees before the fetch constant's swizzle are the host format's as the SDK loads them
# (thirdparty/rexglue-sdk/src/graphics/d3d12/texture_cache.cpp, the host format table): one component is
# repeated into all four, two are R, G, G, G.
_FORMATS = {
    2: (1, "B", 1, 255.0),      # k_8
    6: (4, "B", 4, 255.0),      # k_8_8_8_8
    10: (2, "B", 2, 255.0),     # k_8_8
    24: (2, "H", 1, 65535.0),   # k_16
    26: (8, "H", 4, 65535.0),   # k_16_16_16_16
    29: (8, "e", 4, None),      # k_16_16_16_16_EXPAND (half floats in the SDK)
    30: (2, "e", 1, None),      # k_16_FLOAT
    31: (4, "e", 2, None),      # k_16_16_FLOAT
    32: (8, "e", 4, None),      # k_16_16_16_16_FLOAT
    36: (4, "f", 1, None),      # k_32_FLOAT
    37: (8, "f", 2, None),      # k_32_32_FLOAT
    38: (16, "f", 4, None),     # k_32_32_32_32_FLOAT
}


def texel_bytes(fmt):
    return _FORMATS[fmt][0] if fmt in _FORMATS else 0


def _swizzle(fc):
    return [(fc[3] >> (1 + 3 * i)) & 7 for i in range(4)]


def _texels(data, fmt, endian, swizzle, count):
    """`count` texels from the start of `data` as (x, y, z, w) after the swizzle."""
    if fmt not in _FORMATS:
        raise ValueError("format %d" % fmt)
    size, code, comps, div = _FORMATS[fmt]
    if len(data) < count * size:
        raise ValueError("%d texels of format %d need %d bytes, got %d" % (count, fmt, count * size, len(data)))
    v = struct.unpack("<%d%s" % (count * comps, code), _swap(data[:count * size], endian))
    out = []
    for i in range(count):
        c = v[i * comps:(i + 1) * comps]
        if div is not None:
            c = tuple(x / div for x in c)
        host = (c[0], c[0], c[0], c[0]) if comps == 1 else (c[0], c[1], c[1], c[1]) if comps == 2 else c
        out.append(tuple(0.0 if s == 4 else 1.0 if s >= 5 else float(host[s]) for s in swizzle))
    return out


def is_1d(fc):
    return (fc[0] & 3) == 2 and ((fc[5] >> 9) & 3) == 0


def describe_fc(fc):
    """One line for a texture fetch constant."""
    fmt, endian = fc[1] & 0x3F, (fc[1] >> 6) & 3
    if is_1d(fc):
        size = "1D %d" % ((fc[2] & 0xFFFFFF) + 1)
    else:
        size = "%dD %d x %d" % (((fc[5] >> 9) & 3) + 1, (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1)
    return ("base 0x%08X %s format %d endian %d %s pitch %d swizzle 0x%03X clamp %d/%d filter %d mip 0x%08X"
            % (fc[1] & 0xFFFFF000, size, fmt, endian, "tiled" if fc[0] >> 31 else "linear",
               ((fc[0] >> 22) & 0x1FF) << 5, (fc[3] >> 1) & 0xFFF, (fc[0] >> 10) & 7, (fc[0] >> 13) & 7,
               (fc[3] >> 19) & 3, fc[5] & 0xFFFFF000))


def decode_1d(data, fc):
    if not is_1d(fc):
        raise ValueError("not a 1D texture fetch constant")
    return _texels(data, fc[1] & 0x3F, (fc[1] >> 6) & 3, _swizzle(fc), (fc[2] & 0xFFFFFF) + 1)


def decode_2d(data, fc):
    """(width, height, texels row-major) of an uncompressed 2D texture's base level."""
    if (fc[0] & 3) != 2 or ((fc[5] >> 9) & 3) != 1:
        raise ValueError("not a 2D texture fetch constant")
    fmt, endian = fc[1] & 0x3F, (fc[1] >> 6) & 3
    if fmt not in _FORMATS:
        raise ValueError("format %d" % fmt)
    size = _FORMATS[fmt][0]
    w, h = (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1
    pitch = max(((fc[0] >> 22) & 0x1FF) << 5, w)
    tiled = (fc[0] >> 31) != 0
    log2 = size.bit_length() - 1
    row_bytes = ((pitch + 31) & ~31) * size
    raw = bytearray()
    for y in range(h):
        for x in range(w):
            o = tiled_offset_2d(x, y, pitch, log2) if tiled else y * row_bytes + x * size
            if o + size > len(data):
                raise ValueError("texel (%d, %d) at %d lies outside the %d bytes" % (x, y, o, len(data)))
            raw += data[o:o + size]
    return w, h, _texels(bytes(raw), fmt, endian, _swizzle(fc), w * h)


def sample_1d(table, u):
    """Linear between texel centres ((i + 0.5) / n), clamped to the first and the last texel."""
    if not table or not math.isfinite(u):
        raise ValueError("no table or a non-finite coordinate")
    n = len(table)
    x = min(max(u * n - 0.5, 0.0), n - 1.0)
    i = min(int(x), n - 2) if n > 1 else 0
    f = x - i if n > 1 else 0.0
    if f == 0.0:
        return tuple(table[i])
    return tuple(a + (b - a) * f for a, b in zip(table[i], table[i + 1]))


# --- the scene picture --------------------------------------------------------------------------------------

def scene_bytes_needed(w, h):
    """Bytes from the base that a tiled w x h level of 8-byte texels needs (its last texel's end, exactly)."""
    if w <= 0 or h <= 0:
        raise ValueError("empty picture")
    pitch = (w + 31) & ~31
    # The largest offset is in the last row of tiles and the last column of tiles; scan that tile's rows.
    worst = 0
    for y in range((h - 1) & ~31, h):
        for x in range((w - 1) & ~31, w):
            worst = max(worst, tiled_offset_2d(x, y, pitch, 3))
    return worst + 8


def untile_scene(data, w, h):
    if w <= 0 or h <= 0:
        raise ValueError("empty picture")
    pitch = (w + 31) & ~31
    raw = bytearray(w * h * 8)
    n = len(data)
    at = 0
    for y in range(h):
        # Inside a tile row the offset of x + 8 is not the offset of x plus a constant; every texel is asked.
        for x in range(w):
            o = tiled_offset_2d(x, y, pitch, 3)
            if o + 8 > n:
                raise ValueError("texel (%d, %d) at %d lies outside the %d bytes" % (x, y, o, n))
            raw[at:at + 8] = data[o:o + 8]
            at += 8
    return list(struct.unpack(">%de" % (w * h * 4), bytes(raw)))


def scene_pixel(px, w, x, y):
    i = (y * w + x) * 4
    return tuple(px[i:i + 4])


def scene_rgba8(px, scale=16.0, gamma=2.2):
    """RGBA8 of a float picture for viewing: value / scale, clamped to 0..1, to the power 1 / gamma, opaque. A
    texel with a non-finite colour is magenta."""
    out = bytearray(len(px))
    inv = 1.0 / gamma
    for i in range(0, len(px), 4):
        rgb = px[i:i + 3]
        if not all(math.isfinite(c) for c in rgb):
            out[i:i + 4] = b"\xff\x00\xff\xff"
            continue
        for k in range(3):
            out[i + k] = round(255.0 * min(max(rgb[k] / scale, 0.0), 1.0) ** inv)
        out[i + 3] = 255
    return bytes(out)


def channel_stats(values):
    """(finite count, min, median, mean, max) of a list of floats; the numbers are None when none is finite."""
    fin = sorted(v for v in values if math.isfinite(v))
    if not fin:
        return 0, None, None, None, None
    return len(fin), fin[0], fin[len(fin) // 2], sum(fin) / len(fin), fin[-1]


# --- the exponential fit ------------------------------------------------------------------------------------

def fit_exponential(table, c67w):
    """Per colour channel {"k", "max_error", "texels"}: the k that minimises the squared error of exp(-k d)
    against the table over all its texels (d = (i + 0.5) / n / c67w), found by bisection on the derivative
    (the error has one minimum in k for a table that falls with distance), and the largest absolute error of
    that curve. k = 0 for a table that is 1 everywhere."""
    n = len(table)
    d = [(i + 0.5) / n / c67w for i in range(n)]
    out = []
    for ch in range(3):
        t = [row[ch] for row in table]

        def slope(k):
            # d/dk of sum (exp(-k d) - t)^2
            return sum(-2.0 * di * math.exp(-k * di) * (math.exp(-k * di) - ti) for di, ti in zip(d, t))

        lo, hi = 0.0, 1.0
        if slope(lo) >= 0.0:
            k = 0.0
        else:
            for _ in range(200):
                mid = 0.5 * (lo + hi)
                if slope(mid) < 0.0:
                    lo = mid
                else:
                    hi = mid
            k = 0.5 * (lo + hi)
        err = max(abs(math.exp(-k * di) - ti) for di, ti in zip(d, t))
        out.append({"k": k, "max_error": err, "texels": n})
    return out


# --- reading a capture --------------------------------------------------------------------------------------

class Texture:
    """A "texture" row."""

    def __init__(self, row, root):
        self.fc = [int(x, 16) for x in row["fc"]]
        self.file = row["file"]
        self.path = Path(root) / row["file"]
        self.bytes = row["bytes"]
        self.frame = row.get("frame")
        self.content = int(row["content"], 16) if row.get("content") else None
        self.dim = row.get("dim", 2)
        self.base = self.fc[1] & 0xFFFFF000

    def read(self):
        return self.path.read_bytes()


class Frame:
    def __init__(self, number):
        self.number = number
        self.regs = {}            # pixel register -> (x, y, z, w), of the first fogged main-scene row
        self.tf4 = self.tf5 = None
        self.tf4_fc = self.tf5_fc = None
        self.tf4_base = self.tf5_base = None
        self.tf4_content = self.tf5_content = None
        self.fog_row_ps = None
        self.uses = Counter()     # (kind, ps hash, in scene, slot, fc tuple, content or None) -> rows
        self.tree_row = None      # the first main-scene row whose vertex shader samples slot 16 (parsed)
        self.indx = []            # "indx" rows (parsed)
        self.indxdata = {}        # n -> "indxdata" row


class Capture:
    def __init__(self, path):
        self.path = Path(path)
        self.frames = {}
        self.textures = []
        self.by_content = {}      # (base, content) -> Texture
        self.has_tfc = False

    def frame(self, n):
        if n not in self.frames:
            self.frames[n] = Frame(n)
        return self.frames[n]


_RE_FRAME = re.compile(r'"frame":(\d+)')
_RE_TF = re.compile(r'"tf":\{([^}]*)\}')
_RE_TFC = re.compile(r'"tfc":\{([^}]*)\}')
_RE_PS = re.compile(r'"ps_hash":"0x([0-9A-Fa-f]{16})"')
_RE_SLOT = re.compile(r'"(\d+)":\[((?:"0x[0-9A-Fa-f]{8}",?){6})\]')
_RE_CONTENT = re.compile(r'"(\d+)":"0x([0-9A-Fa-f]{16})"')
_RE_VS_SLOTS = re.compile(r'"vs_tex_slots":\[([0-9,]*)\]')


def pfull_reg(pfull, reg):
    """A register of a row's "pfull" as four floats, or None where the reader gave none (dashes)."""
    text = pfull[reg * 32:reg * 32 + 32]
    if len(text) != 32 or "-" in text:
        return None
    return struct.unpack(">4f", bytes.fromhex(text))


def load_capture(path, tex_root=None):
    """Reads a capture without parsing every row in full (a capture of eight frames is some 300 MB): the
    "texture", "indx" and "indxdata" rows, and of the decoded draw rows the texture slots and their content
    hashes; the first fogged main-scene row and the first tree row of each frame are parsed whole."""
    path = Path(path)
    root = Path(tex_root) if tex_root else path.parent
    cap = Capture(path)
    with open(path, encoding="utf-8") as f:
        for line in f:
            head = line[:40]
            if head.startswith('{"kind":"texture"'):
                t = Texture(json.loads(line), root)
                cap.textures.append(t)
                if t.content is not None:
                    cap.by_content[(t.base, t.content)] = t
                continue
            if head.startswith('{"kind":"indxdata"'):
                r = json.loads(line)
                cap.frame(r["frame"]).indxdata[r["n"]] = r
                continue
            kind = head[9:head.find('"', 9)] if head.startswith('{"kind":"') else ""
            if kind not in ("draw", "tess", "verts", "indx"):
                continue
            m = _RE_FRAME.search(head) or _RE_FRAME.search(line)
            if not m:
                continue
            fr = cap.frame(int(m.group(1)))
            if kind == "indx":
                fr.indx.append(json.loads(line))
            mtf = _RE_TF.search(line)
            if not mtf:
                continue
            slots = {int(s): tuple(int(x.strip('"'), 16) for x in body.split(","))
                     for s, body in _RE_SLOT.findall(mtf.group(1))}
            mc = _RE_TFC.search(line)
            contents = {int(s): int(h, 16) for s, h in _RE_CONTENT.findall(mc.group(1))} if mc else {}
            if mc:
                cap.has_tfc = True
            mps = _RE_PS.search(line)
            ps = mps.group(1).upper() if mps else ""
            in_scene = '"in_scene":true' in line[:400]
            for slot, fc in slots.items():
                if slot in contents or is_1d(fc):
                    fr.uses[(kind, ps, in_scene, slot, fc, contents.get(slot))] += 1
            if not in_scene:
                continue
            if fr.tf4_fc is None and 4 in slots and 5 in slots and is_1d(slots[4]) and is_1d(slots[5]) \
                    and ps != SKY_PS and '"pfull"' in line:
                r = json.loads(line)
                fr.fog_row_ps = ps
                fr.tf4_fc, fr.tf5_fc = list(slots[4]), list(slots[5])
                fr.tf4_base, fr.tf5_base = slots[4][1] & 0xFFFFF000, slots[5][1] & 0xFFFFF000
                fr.tf4_content, fr.tf5_content = contents.get(4), contents.get(5)
                for reg in range(256):
                    v = pfull_reg(r["pfull"], reg)
                    if v is not None:
                        fr.regs[reg] = v
            if fr.tree_row is None and kind == "draw":
                mv = _RE_VS_SLOTS.search(line)
                if mv and "16" in mv.group(1).split(","):
                    fr.tree_row = json.loads(line)
    for fr in cap.frames.values():
        for name, decode in (("tf4", decode_tf4), ("tf5", decode_tf5)):
            base, content = getattr(fr, name + "_base"), getattr(fr, name + "_content")
            t = cap.by_content.get((base, content)) if content is not None else None
            if t is not None and t.path.is_file():
                setattr(fr, name, decode(t.read()))
    return cap


# --- reports ------------------------------------------------------------------------------------------------

def _fmt(v, digits=6):
    return "(" + ", ".join("%.*g" % (digits, x) for x in v) + ")"


def _table_range(table):
    return " ".join("%s %.5g..%.5g" % (n, min(t[i] for t in table), max(t[i] for t in table))
                    for i, n in enumerate("rgba"))


def _table_diff(a, b):
    d = [abs(x - y) for ta, tb in zip(a, b) for x, y in zip(ta[:3], tb[:3])]
    return max(d), sum(d) / len(d)


def report_tables(cap):
    lines = ["capture %s: %d frames, %d texture rows (%d of 1D textures), content hashes in rows: %s"
             % (cap.path.name, len(cap.frames), len(cap.textures), sum(1 for t in cap.textures if t.dim == 1),
                "yes" if cap.has_tfc else "no (a capture from before fog discovery)")]
    prev = None
    for n in sorted(cap.frames):
        fr = cap.frames[n]
        if fr.tf4_fc is None:
            lines.append("frame %d: no fogged main-scene row" % n)
            continue
        lines.append("frame %d (first fogged row: pixel shader %s)" % (n, fr.fog_row_ps))
        for reg in (28, 64, 65, 66, 67, 68, 20):
            if reg in fr.regs:
                lines.append("  c%d = %s" % (reg, _fmt(fr.regs[reg], 9)))
        for slot, name in ((4, "tf4"), (5, "tf5")):
            seen = Counter()
            for (kind, ps, in_scene, s, fc, content), count in fr.uses.items():
                if s == slot and in_scene and is_1d(fc) and ps != SKY_PS:
                    seen[(fc[1] & 0xFFFFF000, content)] += count
            lines.append("  %s: %s" % (name, describe_fc(getattr(fr, name + "_fc"))))
            lines.append("  %s: (address, content) pairs over the frame's main-scene rows: %s" % (
                name, ", ".join("0x%08X / %s in %d rows" % (b, "%016X" % c if c is not None else "none", k)
                                for (b, c), k in sorted(seen.items(), key=lambda kv: -kv[1]))))
            table = getattr(fr, name)
            if table is None:
                lines.append("  %s: no dump of this frame's table" % name)
                continue
            fin = all(math.isfinite(x) for t in table for x in t)
            lines.append("  %s: %d texels, all finite: %s, range %s" % (name, len(table), fin, _table_range(table)))
            lines.append("  %s: first %s  middle %s  last %s" % (name, _fmt(table[0], 5), _fmt(table[len(table) // 2], 5),
                                                                 _fmt(table[-1], 5)))
            if prev is not None and getattr(prev, name) is not None:
                mx, mean = _table_diff(getattr(prev, name), table)
                lines.append("  %s: against frame %d: largest change %.6g, mean %.6g (rgb)" % (name, prev.number, mx, mean))
        if fr.tf5 is not None and 67 in fr.regs and fr.regs[67][3] > 0:
            fit = fit_exponential(fr.tf5, fr.regs[67][3])
            for ch, cname in enumerate("rgb"):
                extra = ""
                if 64 in fr.regs and 65 in fr.regs:
                    extra = "  (c64.%s = %.6g, c65.%s = %.6g, sum %.6g)" % (
                        cname, fr.regs[64][ch], cname, fr.regs[65][ch], fr.regs[64][ch] + fr.regs[65][ch])
                lines.append("  tf5.%s as exp(-k d): k = %.6g, largest error %.4f (one step of the table is %.4f)%s"
                             % (cname, fit[ch]["k"], fit[ch]["max_error"], 1 / 255.0, extra))
        prev = fr
    return lines


def tables_json(cap):
    """The decoded tables and registers per frame, for fixtures and other tools."""
    out = {}
    for n in sorted(cap.frames):
        fr = cap.frames[n]
        out[str(n)] = {"regs": {str(r): list(fr.regs[r]) for r in (20, 28, 64, 65, 66, 67, 68) if r in fr.regs},
                       "tf4_fc": fr.tf4_fc, "tf5_fc": fr.tf5_fc,
                       "tf4": [list(t) for t in fr.tf4] if fr.tf4 else None,
                       "tf5": [list(t) for t in fr.tf5] if fr.tf5 else None}
    return out


def report_trees(cap):
    lines = []
    for n in sorted(cap.frames):
        fr = cap.frames[n]
        vs = defaultdict(Counter)
        for (kind, ps, in_scene, slot, fc, content), count in fr.uses.items():
            if in_scene and slot in (16, 17) and kind == "draw":
                vs[slot][(fc, content)] += count
        if not vs:
            lines.append("frame %d: no draw row whose shaders sample slot 16 or 17 with a kept content" % n)
            continue
        lines.append("frame %d" % n)
        for slot in (16, 17):
            for (fc, content), count in sorted(vs[slot].items(), key=lambda kv: -kv[1]):
                lines.append("  tf%d in %d draw rows: %s, content %s" % (
                    slot, count, describe_fc(list(fc)), "%016X" % content if content is not None else "none"))
                t = cap.by_content.get((fc[1] & 0xFFFFF000, content)) if content is not None else None
                if t is None or not t.path.is_file():
                    lines.append("    no dump")
                    continue
                data = t.read()
                try:
                    if is_1d(fc):
                        table = decode_1d(data, list(fc))
                        lines.append("    %d texels, range %s" % (len(table), _table_range(table)))
                        if slot == 16 and fr.tf5 is not None:
                            same = content == fr.tf5_content
                            lines.append("    same content hash as the frame's tf5: %s" % same)
                            if len(table) == len(fr.tf5):
                                lines.append("    against tf5 texel by texel: largest difference %.6g, mean %.6g"
                                             % _table_diff(table, fr.tf5))
                            else:
                                d = [abs(a - b) for i, row in enumerate(table)
                                     for a, b in zip(row[:3], sample_1d(fr.tf5, (i + 0.5) / len(table))[:3])]
                                lines.append("    against tf5 resampled: largest difference %.6g" % max(d))
                    else:
                        w, h, texels = decode_2d(data, list(fc))
                        lines.append("    %d x %d texels, range %s" % (w, h, _table_range(texels)))
                        if slot == 17:
                            # Inference to test: the rows are {c68, tf4}: v = 0 the near colour, v = 1 the table.
                            for y in sorted({0, h - 1}):
                                row = texels[y * w:(y + 1) * w]
                                lines.append("    row %d: first %s  middle %s  last %s" % (
                                    y, _fmt(row[0], 5), _fmt(row[w // 2], 5), _fmt(row[-1], 5)))
                                if fr.tf4 is not None:
                                    d = [abs(a - b) for i, t17 in enumerate(row)
                                         for a, b in zip(t17[:3], sample_1d(fr.tf4, (i + 0.5) / w)[:3])]
                                    lines.append("    row %d against tf4 at the same coordinate: largest difference "
                                                 "%.6g, mean %.6g" % (y, max(d), sum(d) / len(d)))
                                if 68 in fr.regs:
                                    d = [abs(a - b) for t17 in row for a, b in zip(t17[:3], fr.regs[68][:3])]
                                    lines.append("    row %d against c68 %s: largest difference %.6g, mean %.6g"
                                                 % (y, _fmt(fr.regs[68][:3], 5), max(d), sum(d) / len(d)))
                except ValueError as e:
                    lines.append("    not decoded: %s" % e)
    return lines


def report_sky(cap):
    lines = []
    for n in sorted(cap.frames):
        fr = cap.frames[n]
        by_ps = Counter((r.get("ps_hash", ""), bool(r.get("in_scene"))) for r in fr.indx)
        lines.append("frame %d: %d indx rows: %s" % (n, len(fr.indx), ", ".join(
            "%s%s x %d" % (ps[2:6], "" if scene else " (outside the scene)", k) for (ps, scene), k in sorted(by_ps.items()))))
        for r in fr.indx:
            if r.get("ps_hash", "")[2:].upper() != SKY_PS:
                continue
            lines.append("  sky row n=%s in_scene=%s prim %s vertices %s indices %s stride %s vs %s storage_ok %s" % (
                r.get("n"), r.get("in_scene"), r.get("prim"), r.get("vertices"), r.get("indices"), r.get("stride"),
                (r.get("vs") or {}).get("hash"), r.get("storage_ok")))
            rs = r.get("rs") or {}
            lines.append("    rs: " + " ".join("%s=%s" % (k, rs[k]) for k in ("2200", "2201", "2205", "2104", "2001") if k in rs))
            for slot in sorted(r.get("tf") or {}, key=int):
                fc = r["tf"][slot]
                content = (r.get("tfc") or {}).get(slot)
                lines.append("    tf%s: %s%s" % (slot, describe_fc([int(x, 16) for x in fc]) if fc else "null",
                                                  "  content " + content if content else ""))
            if r.get("pfull"):
                for reg in (20, 28, 31, 46, 47, 64, 65, 66, 67, 68):
                    v = pfull_reg(r["pfull"], reg)
                    lines.append("    c%d = %s" % (reg, _fmt(v, 9) if v else "not read"))
            bank = r.get("bank")
            if bank:
                for reg in (0, 1, 2, 3, 9, 255):
                    lines.append("    vertex c%d = %s" % (reg, _fmt(bank[4 * reg:4 * reg + 4], 9)))
            lines.append("    fetches: %s" % json.dumps(r.get("fetches")))
            data = fr.indxdata.get(r.get("n"))
            if not data:
                lines.append("    no indxdata row")
                continue
            raw = bytes.fromhex(data.get("verts", ""))
            stride = data.get("stride") or 0
            if stride >= 4 and stride % 4 == 0:
                for i in range(0, len(raw) - stride + 1, stride):
                    lines.append("    vertex %d as big-endian floats: %s" % (
                        i // stride, _fmt(struct.unpack(">%df" % (stride // 4), raw[i:i + stride]), 6)))
            else:
                lines.append("    vertex bytes: %s" % data.get("verts"))
            lines.append("    indices: %s" % data.get("idx"))
    return lines


def scene_uses(cap, frame=None):
    """[(frame, kind, ps, slot, fc, content, rows)] of the rows that sampled a 2D texture of the scene colour's
    format, in no particular order within a frame."""
    out = []
    for n in sorted(cap.frames):
        if frame is not None and n != frame:
            continue
        for (kind, ps, in_scene, slot, fc, content), count in cap.frames[n].uses.items():
            if not is_1d(fc) and (fc[1] & 0x3F) == SCENE_FORMAT:
                out.append((n, kind, ps, slot, fc, content, count))
    return out


def report_scene(cap, png=None, frame=None, scale=16.0, base=0x199E0000, max_edge=1120):
    lines = []
    uses = scene_uses(cap)
    for n, kind, ps, slot, fc, content, count in uses:
        lines.append("frame %d: %s row(s) x %d, pixel shader %s, slot %d: base 0x%08X %d x %d, content %s" % (
            n, kind, count, ps[:4], slot, fc[1] & 0xFFFFF000, (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1,
            "%016X" % content if content is not None else "none"))
    picks = [u for u in uses if (u[4][1] & 0xFFFFF000) == base and u[5] is not None
             and (frame is None or u[0] == frame)]
    if not picks:
        lines.append("no row with a kept content of the texture at 0x%08X%s" % (base, "" if frame is None else " in frame %d" % frame))
        return lines
    n, kind, ps, slot, fc, content, count = picks[0]
    contents = sorted({u[5] for u in picks if u[0] == n})
    lines.append("frame %d: %d distinct content(s) of 0x%08X sampled; the picture below is the one pixel shader %s sampled"
                 % (n, len(contents), base, ps[:4]))
    t = cap.by_content.get((base, content))
    if t is None or not t.path.is_file():
        lines.append("no dump of that content")
        return lines
    data = t.read()
    w, h = (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1
    lines.append("dump %s: %d bytes, needs %d" % (t.file, len(data), scene_bytes_needed(w, h)))
    px = untile_scene(data, w, h)
    for ch, name in enumerate("rgba"):
        count_fin, lo, med, mean, hi = channel_stats(px[ch::4])
        lines.append("  %s: finite %d of %d, min %.5g, median %.5g, mean %.5g, max %.5g" % (
            name, count_fin, w * h, lo, med, mean, hi))
    lines.append("  share of texels that are all zero: %.4f" % (
        sum(1 for i in range(0, len(px), 4) if px[i] == 0 and px[i + 1] == 0 and px[i + 2] == 0) / (w * h)))
    if png:
        write_png(png, w, h, scene_rgba8(px, scale), max_edge=max_edge)
        lines.append("wrote %s (value / %g, gamma 2.2)" % (png, scale))
    return lines


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("report", choices=("tables", "trees", "sky", "scene"))
    ap.add_argument("capture")
    ap.add_argument("--tex-root")
    ap.add_argument("--dump", help="tables: also write the decoded tables and registers per frame as JSON")
    ap.add_argument("--png")
    ap.add_argument("--frame", type=int)
    ap.add_argument("--scale", type=float, default=16.0)
    ap.add_argument("--base", type=lambda s: int(s, 16), default=0x199E0000)
    a = ap.parse_args(argv)
    cap = load_capture(a.capture, a.tex_root)
    if a.report == "tables":
        lines = report_tables(cap)
        if a.dump:
            Path(a.dump).write_text(json.dumps(tables_json(cap)))
            lines.append("wrote %s" % a.dump)
    elif a.report == "trees":
        lines = report_trees(cap)
    elif a.report == "sky":
        lines = report_sky(cap)
    else:
        lines = report_scene(cap, a.png, a.frame, a.scale, a.base)
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
