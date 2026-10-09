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
  fog_tables.py replay <capture.jsonl> [--frame N] [--step 4]
                                             the same capture: light_formulas.sky on the pixels whose depth is
                                             clear and light_formulas.fog on the others, against the scene colour
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
    return ("base 0x%08X %s format %d endian %d %s pitch %d swizzle 0x%03X clamp %d/%d filter %d mip 0x%08X "
            "signs %d%d%d%d exp %d"
            % (fc[1] & 0xFFFFF000, size, fmt, endian, "tiled" if fc[0] >> 31 else "linear",
               ((fc[0] >> 22) & 0x1FF) << 5, (fc[3] >> 1) & 0xFFF, (fc[0] >> 10) & 7, (fc[0] >> 13) & 7,
               (fc[3] >> 19) & 3, fc[5] & 0xFFFFF000, *sign_fields(fc), exp_adjust(fc)))


def sign_fields(fc):
    """The four sign fields of a fetch constant (dword 0 bits 2 to 9, two bits a component: 0 unsigned, 1
    signed, 2 unsigned biased, 3 gamma)."""
    return [(fc[0] >> (2 + 2 * i)) & 3 for i in range(4)]


def exp_adjust(fc):
    """The exponent adjust of a fetch constant (dword 3 bits 13 to 18, signed six bits)."""
    v = (fc[3] >> 13) & 0x3F
    return v - 64 if v & 0x20 else v


def _check_reading(fc):
    """The decoders here read an integer format as unsigned fractions and apply no exponent adjust: a
    constant that asks for anything else is refused. A float format's values are its own whatever the sign
    fields say (the scene colour and tf4 have all four set to 1)."""
    fmt = fc[1] & 0x3F
    if fmt in _FORMATS and _FORMATS[fmt][3] is not None and any(sign_fields(fc)):
        raise ValueError("integer format %d with sign fields %s" % (fmt, sign_fields(fc)))
    if exp_adjust(fc):
        raise ValueError("exponent adjust %d" % exp_adjust(fc))


def decode_1d(data, fc):
    if not is_1d(fc):
        raise ValueError("not a 1D texture fetch constant")
    _check_reading(fc)
    return _texels(data, fc[1] & 0x3F, (fc[1] >> 6) & 3, _swizzle(fc), (fc[2] & 0xFFFFFF) + 1)


def decode_2d(data, fc):
    """(width, height, texels row-major) of an uncompressed 2D texture's base level."""
    if (fc[0] & 3) != 2 or ((fc[5] >> 9) & 3) != 1:
        raise ValueError("not a 2D texture fetch constant")
    fmt, endian = fc[1] & 0x3F, (fc[1] >> 6) & 3
    if fmt not in _FORMATS:
        raise ValueError("format %d" % fmt)
    _check_reading(fc)
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


def sample_2d(w, h, texels, u, v):
    """Bilinear between texel centres in both axes, clamped to the edge texels (as sample_1d in each axis)."""
    if not texels or not (math.isfinite(u) and math.isfinite(v)):
        raise ValueError("no texels or a non-finite coordinate")
    x = min(max(u * w - 0.5, 0.0), w - 1.0)
    y = min(max(v * h - 0.5, 0.0), h - 1.0)
    x0, y0 = min(int(x), max(w - 2, 0)), min(int(y), max(h - 2, 0))
    x1, y1 = min(x0 + 1, w - 1), min(y0 + 1, h - 1)
    fx, fy = x - x0, y - y0
    out = []
    for c in range(len(texels[0])):
        top = texels[y0 * w + x0][c] + (texels[y0 * w + x1][c] - texels[y0 * w + x0][c]) * fx
        bottom = texels[y1 * w + x0][c] + (texels[y1 * w + x1][c] - texels[y1 * w + x0][c]) * fx
        out.append(top + (bottom - top) * fy)
    return tuple(out)


def float20e4(bits):
    """The Xenos 24-bit depth float (20 bits of mantissa under 4 of exponent) as a number in 0..2: exponent
    15 and mantissa 0 is 1.0; exponent 0 is the denormal range."""
    e, m = (bits >> 20) & 0xF, bits & 0xFFFFF
    if e == 0:
        return m * 2.0 ** -34
    return (1.0 + m / 1048576.0) * 2.0 ** (e - 15)


def decode_depth(data, fc):
    """(width, height, values row-major) of a 2D k_24_8_FLOAT level (format 23) as a read-back resolve leaves
    it in guest memory: after the constant's endian swap a little-endian dword with the depth float in its
    upper 24 bits. The game's depth is reversed: the value is 1 at the near plane and 0 where nothing was
    drawn (frame-map section 15)."""
    if (fc[0] & 3) != 2 or ((fc[5] >> 9) & 3) != 1 or (fc[1] & 0x3F) != 23:
        raise ValueError("not a 2D k_24_8_FLOAT fetch constant")
    w, h = (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1
    pitch = max(((fc[0] >> 22) & 0x1FF) << 5, w)
    tiled = (fc[0] >> 31) != 0
    row_bytes = ((pitch + 31) & ~31) * 4
    raw = bytearray(w * h * 4)
    at = 0
    for y in range(h):
        for x in range(w):
            o = tiled_offset_2d(x, y, pitch, 2) if tiled else y * row_bytes + x * 4
            if o + 4 > len(data):
                raise ValueError("texel (%d, %d) at %d lies outside the %d bytes" % (x, y, o, len(data)))
            raw[at:at + 4] = data[o:o + 4]
            at += 4
    words = struct.unpack("<%dI" % (w * h), _swap(bytes(raw), (fc[1] >> 6) & 3))
    return w, h, [float20e4(v >> 8) for v in words]


def invert4(m):
    """The inverse of a 4 x 4 matrix (rows of four), by elimination with pivoting; ValueError if singular."""
    a = [list(map(float, m[i])) + [1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    for c in range(4):
        pivot = max(range(c, 4), key=lambda r: abs(a[r][c]))
        if abs(a[pivot][c]) < 1e-30:
            raise ValueError("singular matrix")
        a[c], a[pivot] = a[pivot], a[c]
        d = a[c][c]
        a[c] = [x / d for x in a[c]]
        for r in range(4):
            if r != c and a[r][c] != 0.0:
                f = a[r][c]
                a[r] = [x - f * y for x, y in zip(a[r], a[c])]
    return [row[4:] for row in a]


def screen_point(x, y, w, h, stored_depth):
    """The homogeneous screen point of pixel (x, y)'s centre as the shadow mask builds it (light_formulas
    mask_screen_point): x and y in -1..1 with y up, z = 1 - the stored (reversed) depth, w = 1."""
    return [(2.0 * x + 1.0) / w - 1.0, 1.0 - (2.0 * y + 1.0) / h, 1.0 - stored_depth, 1.0]


def unproject(vp_inv, S):
    """screen -> homogeneous world: vp_inv . S (not divided)."""
    return [sum(vp_inv[i][k] * S[k] for k in range(4)) for i in range(4)]


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


def tf17_grid_difference(w, h, texels, tf4, c68, steps=33):
    """How far a 2D tf17, read bilinearly at (a, t), is from the pixel form's c68 + t * (tf4(a) - c68), over
    a grid of `steps` values of a and of t in 0..1: (largest, median) absolute difference over rgb."""
    d = []
    for j in range(steps):
        t = j / (steps - 1.0)
        for i in range(steps):
            a = i / (steps - 1.0)
            got = sample_2d(w, h, texels, a, t)
            s4 = sample_1d(tf4, a)
            d += [abs(got[k] - (c68[k] + t * (s4[k] - c68[k]))) for k in range(3)]
    d.sort()
    return d[-1], d[len(d) // 2]


def _tree_table_lines(fr, slot, fc, content, data):
    """The lines of report_trees for one dumped texture of slot 16 or 17."""
    lines = []
    if is_1d(fc):
        table = decode_1d(data, fc)
        lines.append("    %d texels, range %s" % (len(table), _table_range(table)))
        ref, name, ref_content = (fr.tf5, "tf5", fr.tf5_content) if slot == 16 else (fr.tf4, "tf4", fr.tf4_content)
        if ref is not None:
            lines.append("    same content hash as the frame's %s: %s" % (name, content == ref_content))
            if len(table) == len(ref):
                lines.append("    against %s texel by texel: largest difference %.6g, mean %.6g"
                             % ((name,) + _table_diff(table, ref)))
            else:
                d = [abs(x - y) for i, row in enumerate(table)
                     for x, y in zip(row[:3], sample_1d(ref, (i + 0.5) / len(table))[:3])]
                lines.append("    against %s resampled: largest difference %.6g" % (name, max(d)))
        if slot == 17:
            lines.append("    a 1D texture: the fetch's second coordinate addresses nothing, tf17(a, t) = "
                         "its texel row at a for every t (no blend from c68)")
    else:
        w, h, texels = decode_2d(data, fc)
        lines.append("    %d x %d texels, range %s" % (w, h, _table_range(texels)))
        if slot == 17 and fr.tf4 is not None and 68 in fr.regs:
            mx, med = tf17_grid_difference(w, h, texels, fr.tf4, fr.regs[68])
            lines.append("    tf17(a, t) read bilinearly against c68 + t * (tf4(a) - c68) over 33 x 33 points: "
                         "largest difference %.6g, median %.6g" % (mx, med))
    return lines


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
                    lines += _tree_table_lines(fr, slot, list(fc), content, data)
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


def scene_texels(data, fc):
    """The scene colour as untile_scene gives it; a constant whose pitch field is not the width rounded up
    to 32, or that is not tiled, is read by its own fields (decode_2d)."""
    w, h = (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1
    if (fc[0] >> 31) and ((fc[0] >> 22) & 0x1FF) << 5 == (w + 31) & ~31 and (fc[1] >> 6) & 3 == 1 \
            and _swizzle(fc) == [0, 1, 2, 3]:
        return untile_scene(data, w, h)
    return [c for t in decode_2d(data, list(fc))[2] for c in t]


def _median(values):
    v = sorted(values)
    return v[len(v) // 2] if v else float("nan")


def frame_targets(cap, n, scene_base=0x199E0000, depth_base=0x1A02A000):
    """(scene fc, scene content, depth fc, depth content) of frame n: the scene colour the downsample 87E8
    sampled (any row's, failing that) and the depth the in-scene mask rectangles sampled; None where absent."""
    scene = depth = None
    for (kind, ps, in_scene, slot, fc, content), count in sorted(cap.frames[n].uses.items(), key=str):
        if content is None or is_1d(fc):
            continue
        base = fc[1] & 0xFFFFF000
        if base == scene_base and (scene is None or ps.startswith("87E8")):
            scene = (fc, content)
        if base == depth_base and in_scene and kind == "verts":
            depth = (fc, content)
    return (scene or (None, None)) + (depth or (None, None))


BANDS = ((0, 20), (20, 100), (100, 300), (300, 600), (600, 1000), (1000, 1e9))


def report_replay(cap, frame=None, step=4):
    """light_formulas.sky and light_formulas.fog on the pixels of the read-back scene colour.
    Sky: pixels whose stored depth is 0 (nothing drawn); the prediction is sky(direction) * c20.w with the
    frame's tf4 and the sky row's registers (the sky's textures are left out: see report_sky for what they
    hold); reported: median of observed / predicted per channel, also with tf4 read half a texel to either
    side. Ground: every other pixel, its world position from the depth through the inverse of the sky row's
    vertex c0..c3; with T and the in-scatter term from fog() of black and of white, the colour before fog
    that the pixel implies is (observed / c20.w - floor) / T: reported by distance band with the share of
    pixels below the floor (observed darker than the in-scatter alone, which no colour explains)."""
    import light_formulas as lf
    n = frame if frame is not None else min(cap.frames)
    fr = cap.frames[n]
    lines = ["frame %d of %s, every %d. pixel" % (n, cap.path.name, step)]
    sky_row = next((r for r in fr.indx if r.get("in_scene") and r.get("ps_hash", "")[2:].upper() == SKY_PS), None)
    scene_fc, scene_content, depth_fc, depth_content = frame_targets(cap, n)
    if sky_row is None or not sky_row.get("bank") or not sky_row.get("pfull") or fr.tf4 is None or fr.tf5 is None:
        return lines + ["no in-scene sky row with its registers, or no table dump: nothing replayed"]
    if scene_content is None or depth_content is None:
        return lines + ["no scene colour or depth content in this frame's rows: nothing replayed"]
    ts, td = cap.by_content.get((scene_fc[1] & 0xFFFFF000, scene_content)), \
        cap.by_content.get((depth_fc[1] & 0xFFFFF000, depth_content))
    if ts is None or td is None:
        return lines + ["no dump of the scene colour or the depth: nothing replayed"]
    lines.append("scene %s (rows of frame %d), depth %s" % (ts.file, n, td.file))
    w, h = (scene_fc[2] & 0x1FFF) + 1, ((scene_fc[2] >> 13) & 0x1FFF) + 1
    px = scene_texels(ts.read(), scene_fc)
    dw, dh, depth = decode_depth(td.read(), list(depth_fc))
    if (dw, dh) != (w, h):
        return lines + ["depth %d x %d is not the scene's %d x %d" % (dw, dh, w, h)]
    bank = sky_row["bank"]
    vp = [bank[4 * i:4 * i + 4] for i in range(4)]
    eye = bank[36:39]
    vp_inv = invert4(vp)
    reg = lambda r: pfull_reg(sky_row["pfull"], r)
    scale = fr.regs[20][3]
    block = {"c31x": reg(31)[0], "c46x": reg(46)[0], "c47x": reg(47)[0], "c64": reg(64)[:3], "c65": reg(65)[:3],
             "c66": reg(66)[:3], "c67y": reg(67)[1]}
    c28, c66, c67, c68 = fr.regs[28], fr.regs[66], fr.regs[67], fr.regs[68]
    lines.append("eye %s, c20.w %.7g, c67 %s, c28 %s" % (_fmt(eye), scale, _fmt(c67), _fmt(c28[:2])))
    tf4, tf5 = lf.table_1d(fr.tf4), lf.table_1d(fr.tf5)
    half = 0.5 / len(fr.tf4)
    shifted = {"half a texel lower": lambda a: tf4(a - half), "half a texel higher": lambda a: tf4(a + half)}
    sky_ratio = [[] for _ in range(3)]
    sky_shift = {k: [[] for _ in range(3)] for k in shifted}
    sky_abs = [[] for _ in range(3)]
    bands = {b: {"n": 0, "below": 0, "T": [[], [], []], "obs": [[], [], []], "floor": [[], [], []],
                 "C": [[], [], []]} for b in BANDS}
    n_sky = n_ground = sky_inside = ground_inside = ground_fogged = 0
    for y in range(step // 2, h, step):
        for x in range(step // 2, w, step):
            i = y * w + x
            obs = [px[4 * i + k] for k in range(3)]
            Ph = unproject(vp_inv, screen_point(x, y, w, h, depth[i]))
            if depth[i] == 0.0:
                n_sky += 1
                direction = [Ph[k] - eye[k] * Ph[3] for k in range(3)]
                if Ph[3] < 0:
                    direction = [-c for c in direction]
                pred = lf.sky(direction, block, tf4, None, None)
                a = lf.sat(lf.dot(lf.normalize(direction), block["c66"]))
                sky_inside += half < a < 1.0 - half
                for k in range(3):
                    if pred[k] > 0:
                        sky_ratio[k].append(obs[k] / (pred[k] * scale))
                        sky_abs[k].append(abs(obs[k] / scale - pred[k]))
                for name, table in shifted.items():
                    alt = lf.sky(direction, block, table, None, None)
                    for k in range(3):
                        if alt[k] > 0:
                            sky_shift[name][k].append(obs[k] / (alt[k] * scale))
                continue
            if Ph[3] == 0:
                continue
            n_ground += 1
            P = [Ph[k] / Ph[3] for k in range(3)]
            d = math.sqrt(sum((P[k] - eye[k]) ** 2 for k in range(3)))
            floor = lf.fog([0.0, 0.0, 0.0], P, eye, c28, c66, c67, c68, tf4, tf5)
            coords = lf.fog_coords(P, eye, c66, c67)
            if coords is not None:
                ground_fogged += 1
                ground_inside += half < coords[0] < 1.0 - half
            white = lf.fog([1.0, 1.0, 1.0], P, eye, c28, c66, c67, c68, tf4, tf5)
            band = bands[next(b for b in BANDS if b[0] <= d < b[1])]
            band["n"] += 1
            below = False
            for k in range(3):
                T = white[k] - floor[k]
                o = obs[k] / scale
                band["T"][k].append(T)
                band["obs"][k].append(o)
                band["floor"][k].append(floor[k])
                if T > 1e-3:
                    band["C"][k].append((o - floor[k]) / T)
                below = below or o < floor[k] * 0.98
            band["below"] += below
    lines.append("sky pixels (stored depth 0): %d; other pixels: %d" % (n_sky, n_ground))
    if n_sky:
        lines.append("  sky, observed / (sky() * c20.w), median per channel: %s; 10th and 90th percentile of red: "
                     "%.4f, %.4f" % (_fmt([_median(c) for c in sky_ratio], 5),
                                     sorted(sky_ratio[0])[len(sky_ratio[0]) // 10],
                                     sorted(sky_ratio[0])[len(sky_ratio[0]) * 9 // 10]))
        lines.append("  sky, median of |observed / c20.w - sky()| per channel: %s" % _fmt([_median(c) for c in sky_abs], 4))
        for name in shifted:
            lines.append("  sky with tf4 read %s: median ratio %s" % (
                name, _fmt([_median(c) for c in sky_shift[name]], 5)))
        lines.append("  tf4's coordinate lies between the first and the last texel centre (where half a texel "
                     "changes what is read) in %d of %d sky pixels and %d of %d fogged ground pixels"
                     % (sky_inside, n_sky, ground_inside, ground_fogged))
    for b in BANDS:
        band = bands[b]
        if not band["n"]:
            continue
        lines.append("  ground %g..%s units: %d pixels; medians: T %s, observed / c20.w %s, in-scatter floor %s, "
                     "implied colour before fog %s; below the floor in a channel (by more than 2%%): %.4f" % (
                         b[0], "%g" % b[1] if b[1] < 1e9 else "", band["n"], _fmt([_median(c) for c in band["T"]], 3),
                         _fmt([_median(c) for c in band["obs"]], 3), _fmt([_median(c) for c in band["floor"]], 3),
                         _fmt([_median(c) for c in band["C"]], 3), band["below"] / band["n"]))
    return lines


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
    px = scene_texels(data, fc)
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
    ap.add_argument("report", choices=("tables", "trees", "sky", "scene", "replay"))
    ap.add_argument("capture")
    ap.add_argument("--tex-root")
    ap.add_argument("--dump", help="tables: also write the decoded tables and registers per frame as JSON")
    ap.add_argument("--png")
    ap.add_argument("--frame", type=int)
    ap.add_argument("--scale", type=float, default=16.0)
    ap.add_argument("--step", type=int, default=4, help="replay: every n-th pixel in x and y")
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
    elif a.report == "replay":
        lines = report_replay(cap, a.frame, a.step)
    else:
        lines = report_scene(cap, a.png, a.frame, a.scale, a.base)
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
