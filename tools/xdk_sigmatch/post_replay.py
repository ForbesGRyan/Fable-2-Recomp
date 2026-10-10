"""The post chain replayed on the CPU from the scene colour of a read-back capture, with the formulas of
light_formulas.py, against the game's own final picture of the same capture.

  post_replay.py replay  <capture> <frame> [--variant A|B|N ...] [--table-offset 0.5] [--bias 0] [--ramp R]
                                           [--write <prefix>] [--png <prefix>]
  post_replay.py predict <capture> <frame> [--ramp R]
  post_replay.py export  <capture> <frame> <prefix>
  post_replay.py excerpt <capture> <frame> --at X,Y --out <file.json>
  post_replay.py compare <dump> <picture> [--spot bridge]

<capture>: a discovery capture made with --readback_resolve=full (a path, or the stamp's last digits, looked
for in out/build/win-amd64-release/logs). Read from it: the frame's post rows (their registers), c20.w of the
frame's first main-scene row, the scene colour the composite's row names (0x199E0000, by content), the table
its row names (0x1FAA0000, by content) and the front buffer 0x1ECFF000 (1280 x 720 k_8_8_8_8: the composite's
own output with the HUD, before scan-out; the first content seen at that address, so frame 1 is the fair
comparison).

Variants, each a picture of the scene's 1120 x 720 pixels in the front buffer's display values:
  A  light_formulas.post_composite with bloom 0: table, grey fade, saturation step, power.
  B  A with the bloom: post_downsample (--bias), post_bright with tf1 = tf2 = 0, post_blur13 along x and
     along y, each stage rounded to the 10 bits of its k_2_10_10_10 target, sampled linearly at the pixel
     (texel centres aligned: the place of the sample is this tool's, not read from the game).
  N  today's native mapping: sqrt(sat(scene / c20.w * 2.22)).
--table-offset is the table fetch's offset in texels: 0.5 is light_formulas.post_table_texel (the fetch's
own OffsetX); 0 is floor(u * 512), the reading of the research script this tool came from
(out/tests/post-research/replay.py), kept to reproduce its figures.
--ramp: the scan-out ramp, applied to the 8-bit value of variants A and B and of the front buffer (predict,
--write, --png; N is what the native picture shows today, which no ramp touches). R is `measured` (the eight
measured pairs of spec criterion 4, joined by straight lines: a stand-in, not the game's table) or a log
holding the line `[native] ramp: read at clay frame N: valid 1, <256 x rrrgggbbb>` (nine hex digits an
entry, 10 bits a channel).

replay prints, per region of the three region files (spot `bridge`), the variant's mean luminance over the
front buffer's and the hue difference, and over the whole picture outside the HUD the absolute difference in
8-bit levels. predict prints per region the variant after the ramp over N (spec criteria 7 and 13: what the
native shots' on / off ratio should be). export writes the two files of the oracle feed (plan Task 6):
<prefix>.scene and <prefix>.block. excerpt writes a 16 x 16 block of a frame with everything the arithmetic
needs and its results (tests/post_replay_excerpt.json holds one). compare reads two pictures in the dump
format ("F2OD", width, height, RGBA bytes: the pass's read-back and a --write of this tool).

Pure Python (no numpy, as every tracked tool): a replay of both variants takes about 20 seconds.
"""
import argparse
import json
import math
import re
import struct
import sys
from array import array
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fog_tables  # noqa: E402
import light_formulas as lf  # noqa: E402
from texture_thumb import write_png  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
LOGS = REPO / "out" / "build" / "win-amd64-release" / "logs"
REGION_FILES = ("lighting-regions", "shadow-regions", "fog-regions")

SCENE_BASE, TABLE_BASE, FRONT_BASE = 0x199E0000, 0x1FAA0000, 0x1ECFF000
SCENE_W, SCENE_H = 1120, 720
FRONT_W = 1280
NATIVE_EXPOSURE = 2.22          # clay_logic.h, kLitExposure: today's mapping (variant N)
ROLES = {s.hash: s.role for s in lf.POST_SHADERS}
# The stand-in for the scan-out ramp: front level -> shot level. The eight inner pairs are measured (spec
# criterion 4; research `ramp_092959.txt`); the two ends, (0, 0) and (255, 255), are assumed.
MEASURED_RAMP_PAIRS = ((0, 0), (23, 10), (47, 32), (63, 47), (124, 110), (156, 144), (204, 197), (236, 232),
                       (252, 250), (255, 255))
# The HUD's places, as fractions of the picture: the strip at the top and the box at the lower left.
HUD_TOP = 0.165
HUD_BOX = (0.045, 0.775, 0.15, 0.96)

PART_BLUR, PART_RADIAL = 1, 2   # post_block.h: kPostPartBlur, kPostPartRadial
BLOCK_FLOATS = 541              # PostBlock: composite[6], bright[5], blur[2][7], radial[4], table[512]


class ReplayError(Exception):
    pass


# --- reading a capture ---------------------------------------------------------------------------------------

_RE_FRAME = re.compile(r'"frame":(\d+)')
_RE_PS = re.compile(r'"ps_hash":"0x([0-9A-Fa-f]{16})"')


def find_capture(name):
    """A capture's path: `name` itself, or the one capture in the logs folder whose name ends in it."""
    p = Path(name)
    if p.is_file():
        return p
    found = sorted(LOGS.glob("native_discovery_*%s.jsonl" % name)) if LOGS.is_dir() else []
    if len(found) != 1:
        raise ReplayError("%s: %d captures of that name in %s" % (name, len(found), LOGS))
    return found[0]


class Frame:
    """A frame's post rows and what they name. regs: role -> {register: (x, y, z, w)} of the first row of
    each post shader; c20w: the scene's output scale."""

    def __init__(self, path, number):
        self.path, self.number = Path(path), number
        self.rows = {}
        self.regs = {}
        self.c20w = None
        self.textures = []
        self.table = self.table_bytes = None

    def reg(self, role, register, component=0):
        try:
            return self.regs[role][register][component]
        except KeyError:
            raise ReplayError("frame %d: no register c%d of the %s row" % (self.number, register, role))

    def weights(self, role):
        return list(self.regs[role][46]) + list(self.regs[role][47][:3])

    def composite(self):
        """The composite's registers in post_composite's order after the table."""
        return (self.reg("composite", 31), self.reg("composite", 46), self.reg("composite", 47),
                (self.reg("composite", 77, 0), self.reg("composite", 77, 1)), self.reg("composite", 78))

    def bright(self):
        return (self.reg("bright", 31), self.reg("bright", 46), self.reg("bright", 47),
                (self.reg("bright", 77, 0), self.reg("bright", 77, 1)))

    def texture(self, base, content=None):
        for t in self.textures:
            if t.base == base and (content is None or t.content == content):
                return t
        raise ReplayError("%s: no dump of 0x%08X%s" % (self.path.name, base, "" if content is None else " with content %016X" % content))


def load_frame(path, number):
    """The post rows of frame `number` (the first of each shader), c20.w and the capture's texture rows."""
    fr = Frame(path, number)
    with open(path, encoding="utf-8") as f:
        for line in f:
            head = line[:60]
            if head.startswith('{"kind":"texture"'):
                fr.textures.append(fog_tables.Texture(json.loads(line), Path(path).parent))
                continue
            kind = head[9:head.find('"', 9)] if head.startswith('{"kind":"') else ""
            if kind not in ("draw", "tess", "verts", "indx"):
                continue
            m = _RE_FRAME.search(head)
            if not m or int(m.group(1)) != number:
                continue
            if fr.c20w is None and '"in_scene":true' in line[:400] and '"pfull"' in line:
                c20 = fog_tables.pfull_reg(json.loads(line)["pfull"], 20)
                if c20 is not None:
                    fr.c20w = c20[3]
            if kind != "verts":
                continue
            mps = _RE_PS.search(line)
            role = ROLES.get(mps.group(1).upper()) if mps else None
            if role is None or role in fr.rows:
                continue
            row = json.loads(line)
            fr.rows[role] = row
            fr.regs[role] = {r: v for r in (31, 46, 47, 77, 78) for v in [fog_tables.pfull_reg(row["pfull"], r)] if v is not None}
    for role in ("composite", "bright", "blur-x", "blur-y"):
        if role not in fr.rows:
            raise ReplayError("%s frame %d: no %s row" % (Path(path).name, number, role))
    if fr.c20w is None:
        raise ReplayError("%s frame %d: no main-scene row with registers" % (Path(path).name, number))
    t = fr.texture(TABLE_BASE, int(fr.rows["composite"]["tfc"]["1"], 16))
    fr.table_bytes = t.read()
    fr.table = [x[0] for x in fog_tables.decode_1d(fr.table_bytes, t.fc)]
    return fr


def load_scene(fr):
    """The frame's scene colour as an array of SCENE_W * SCENE_H * 3 floats (the game's scale)."""
    content = fr.rows["composite"].get("tfc", {}).get("0")
    if content is None:
        raise ReplayError("frame %d: the composite's row names no scene content (not a read-back capture)" % fr.number)
    px = fog_tables.untile_scene(fr.texture(SCENE_BASE, int(content, 16)).read(), SCENE_W, SCENE_H)
    out = array("d", bytes(8 * SCENE_W * SCENE_H * 3))
    out[0::3], out[1::3], out[2::3] = array("d", px[0::4]), array("d", px[1::4]), array("d", px[2::4])
    return out


def resample_width(rows, w_from, w_to, h):
    """A picture of w_from columns (flat, 3 floats a texel) brought to w_to columns, linear, centres aligned."""
    out = array("d", bytes(8 * w_to * h * 3))
    taps = []
    for i in range(w_to):
        x = min(max((i + 0.5) * w_from / w_to - 0.5, 0.0), w_from - 1.0)
        x0 = min(int(x), w_from - 2)
        taps.append((3 * x0, x - x0))
    at = 0
    for y in range(h):
        base = y * w_from * 3
        for x0, f in taps:
            o = base + x0
            for c in range(3):
                out[at] = rows[o + c] * (1.0 - f) + rows[o + c + 3] * f
                at += 1
    return out


def load_front(fr):
    """The game's final picture at the scene's width: SCENE_W * SCENE_H * 3 floats in 0..1."""
    t = fr.texture(FRONT_BASE)
    w, h, texels = fog_tables.decode_2d(t.read(), t.fc)
    if (w, h) != (FRONT_W, SCENE_H):
        raise ReplayError("the front buffer is %d x %d" % (w, h))
    flat = array("d", bytes(8 * w * h * 3))
    flat[0::3] = array("d", [p[0] for p in texels])
    flat[1::3] = array("d", [p[1] for p in texels])
    flat[2::3] = array("d", [p[2] for p in texels])
    return resample_width(flat, w, SCENE_W, h)


# --- the arithmetic ------------------------------------------------------------------------------------------

def q10(v):
    """A value as a k_2_10_10_10 target stores it: clamped to 0..1, 10 bits."""
    return round(lf.sat(v) * 1023.0) / 1023.0


def flat_texture(px, w, h, linear):
    """light_formulas' tex(px, py) over a flat array of 3 floats a texel, without copying it."""
    def texel(x, y):
        o = (y * w + x) * 3
        return px[o], px[o + 1], px[o + 2]

    def axis(p, n):
        if p != p:
            return 0, 0, 0.0
        if not linear:
            i = int(min(max(p, 0.0), n - 1.0))
            return i, i, 0.0
        x = min(max(p - 0.5, 0.0), n - 1.0)
        i = min(int(x), n - 1)
        return i, min(i + 1, n - 1), x - i

    def fetch(x, y):
        x0, x1, fx = axis(x, w)
        y0, y1, fy = axis(y, h)
        a = texel(x0, y0)
        if fx == 0.0 and fy == 0.0:
            return a
        b, c, d = texel(x1, y0), texel(x0, y1), texel(x1, y1)
        return [(a[i] + (b[i] - a[i]) * fx) * (1.0 - fy) + (c[i] + (d[i] - c[i]) * fx) * fy for i in range(3)]

    return fetch


def bloom_picture(scene, w, h, fr, bias=0.0):
    """Variant B's bloom: (width, height, rows of [r, g, b]) at a quarter of the scene's size."""
    if w % 4 or h % 4:
        raise ReplayError("the scene's size %d x %d is no multiple of 4" % (w, h))
    bw, bh = w // 4, h // 4
    tex = flat_texture(scene, w, h, True)
    bright = fr.bright()
    stage = [[[q10(v) for v in lf.post_bright(lf.post_downsample(tex, x, y, bias), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), *bright)]
              for x in range(bw)] for y in range(bh)]
    for role, vertical in (("blur-x", False), ("blur-y", True)):
        src, weights = lf.post_texture(stage), fr.weights(role)
        stage = [[[q10(v) for v in lf.post_blur13(src, x, y, weights, vertical)] for x in range(bw)] for y in range(bh)]
    return bw, bh, stage


def bloom_sampler(bw, bh, rows, w, h):
    """The bloom at pixel (x, y) of the w x h picture: linear, texel centres aligned."""
    tex = lf.post_texture(rows, linear=True)
    return lambda x, y: tex((x + 0.5) * bw / w, (y + 0.5) * bh / h)


def table_reader(table, offset):
    """The table as post_composite takes it: the list itself for the reference's addressing (offset 0.5), a
    callable for another offset in texels (0: floor(u * n))."""
    if offset == 0.5:
        return table
    n = len(table)

    def read(u):
        if not u > 0.0:
            return table[0]
        x = u * n + offset
        return table[n - 1] if not x < n else table[max(int(math.floor(x)), 0)]

    return read


def native_mapping(scene, c20w):
    """Variant N: today's output mapping of a lit pixel, sqrt(sat(scene / c20.w * 2.22))."""
    return [math.sqrt(lf.sat(v / c20w * NATIVE_EXPOSURE)) for v in scene[:3]]


def variant_picture(variant, scene, w, h, fr, bloom=None, table_offset=0.5):
    """A variant's picture: w * h * 3 floats in 0..1. bloom: bloom_sampler's callable for B."""
    out = array("d", bytes(8 * w * h * 3))
    if variant == "N":
        for i in range(0, len(scene), 3):
            out[i:i + 3] = array("d", native_mapping(scene[i:i + 3], fr.c20w))
        return out
    if variant not in ("A", "B") or (variant == "B" and bloom is None):
        raise ReplayError("variant %s" % variant)
    table, regs = table_reader(fr.table, table_offset), fr.composite()
    zero = (0.0, 0.0, 0.0)
    composite = lf.post_composite
    i = 0
    for y in range(h):
        for x in range(w):
            out[i:i + 3] = array("d", composite(scene[i:i + 3], bloom(x, y) if variant == "B" else zero, table, *regs))
            i += 3
    return out


# --- the ramp and the picture files --------------------------------------------------------------------------

def measured_ramp():
    """The stand-in ramp: MEASURED_RAMP_PAIRS joined by straight lines, the same for the three channels."""
    one = []
    for level in range(256):
        for (a, fa), (b, fb) in zip(MEASURED_RAMP_PAIRS, MEASURED_RAMP_PAIRS[1:]):
            if a <= level <= b:
                one.append((fa + (fb - fa) * (level - a) / (b - a)) / 255.0)
                break
    return [one, list(one), list(one)]


def parse_ramp_line(text):
    """The last `[native] ramp: read at clay frame` line of a log with `valid 1`: three lists of 256 floats."""
    found = None
    for line in text.splitlines():
        if "[native] ramp: read at clay frame" in line and "valid 1" in line:
            found = line
    if found is None:
        raise ReplayError("no valid `[native] ramp: read at clay frame` line")
    entries = re.findall(r"\b[0-9A-Fa-f]{9}\b", found.split("valid 1", 1)[1])
    if len(entries) != 256:
        raise ReplayError("the ramp line has %d entries, not 256" % len(entries))
    ramp = [[int(e[3 * c:3 * c + 3], 16) / 1023.0 for e in entries] for c in range(3)]
    if any(v > 1.0 for channel in ramp for v in channel):
        raise ReplayError("a ramp entry is above 1023")
    return ramp


def load_ramp(spec):
    if spec is None:
        return None
    if spec == "measured":
        return measured_ramp()
    return parse_ramp_line(Path(spec).read_text(encoding="utf-8", errors="replace"))


def to_bytes(picture, ramp=None):
    """A float picture (3 a pixel) as RGBA bytes: the front target's 8-bit level, through the ramp if one
    is given (each channel through its own table, then to 8 bits as the mapped target stores it)."""
    n = len(picture) // 3
    out = bytearray(b"\xff" * (4 * n))
    index = lf.post_ramp_index
    for c in range(3):
        if ramp is None:
            out[c::4] = bytes(index(v) for v in picture[c::3])
        else:
            lut = [index(v) for v in ramp[c]]
            out[c::4] = bytes(lut[index(v)] for v in picture[c::3])
    return bytes(out)


def write_dump(path, w, h, rgba):
    if len(rgba) != w * h * 4:
        raise ReplayError("%d bytes for %d x %d" % (len(rgba), w, h))
    Path(path).write_bytes(b"F2OD" + struct.pack("<II", w, h) + bytes(rgba))


def read_dump(path):
    """(width, height, RGBA bytes) of a picture in the dump format."""
    data = Path(path).read_bytes()
    if len(data) < 12 or data[:4] != b"F2OD":
        raise ReplayError("%s: not a picture dump" % path)
    w, h = struct.unpack_from("<II", data, 4)
    if w == 0 or h == 0 or len(data) != 12 + w * h * 4:
        raise ReplayError("%s: %d bytes for %d x %d" % (path, len(data), w, h))
    return w, h, data[12:]


def pack_scene(scene, w, h):
    """<prefix>.scene: "F2OS", width, height, then w * h * 3 float32 (little-endian)."""
    if len(scene) != w * h * 3:
        raise ReplayError("%d floats for %d x %d" % (len(scene), w, h))
    floats = array("f", scene)
    if sys.byteorder != "little":
        floats.byteswap()
    return b"F2OS" + struct.pack("<II", w, h) + floats.tobytes()


def block_floats(fr):
    """PostBlock's 541 floats in the struct's order, and its parts."""
    out = [fr.reg("composite", 31), fr.reg("composite", 46), fr.reg("composite", 47), fr.reg("composite", 77, 0),
           fr.reg("composite", 77, 1), fr.reg("composite", 78)]
    out += [fr.reg("bright", 31), fr.reg("bright", 46), fr.reg("bright", 47), fr.reg("bright", 77, 0), fr.reg("bright", 77, 1)]
    parts = PART_BLUR
    for role in ("blur-x", "blur-y"):
        w = fr.weights(role)
        out += w
        if not (all(math.isfinite(v) and 0.0 <= v <= 1.0 for v in w) and abs(w[0] + 2.0 * sum(w[1:]) - 1.0) <= 1e-3):
            parts &= ~PART_BLUR
    if "radial" in fr.regs and all(r in fr.regs["radial"] for r in (31, 46, 47)):
        radial = [fr.reg("radial", 31, 0), fr.reg("radial", 31, 1), fr.reg("radial", 46), fr.reg("radial", 47)]
        if all(math.isfinite(v) for v in radial) and all(abs(v) <= 4.0 for v in radial[:2]) \
                and all(0.0 < v <= 1.0 for v in radial[2:]):
            parts |= PART_RADIAL
    else:
        radial = [0.0] * 4
    out += radial + list(fr.table)
    if len(out) != BLOCK_FLOATS:
        raise ReplayError("%d floats in the block" % len(out))
    return out, parts


def pack_block(fr):
    """<prefix>.block: "F2OB", u32 1, float32 c20.w, PostBlock's 541 floats, u32 parts."""
    floats, parts = block_floats(fr)
    return b"F2OB" + struct.pack("<I", 1) + struct.pack("<%df" % (1 + BLOCK_FLOATS), fr.c20w, *floats) + struct.pack("<I", parts)


# --- statistics ----------------------------------------------------------------------------------------------

def percentile(ordered, q):
    """The q-th percentile of a sorted list, linear between neighbours."""
    if not ordered:
        return float("nan")
    x = (len(ordered) - 1) * q / 100.0
    i = int(x)
    return ordered[i] if i + 1 >= len(ordered) else ordered[i] + (ordered[i + 1] - ordered[i]) * (x - i)


def hue(rgb):
    r, g, b = rgb
    mx, mn = max(rgb), min(rgb)
    if mx - mn < 1e-9:
        return float("nan")
    if mx == r:
        return 60.0 * ((g - b) / (mx - mn) % 6)
    if mx == g:
        return 60.0 * ((b - r) / (mx - mn) + 2)
    return 60.0 * ((r - g) / (mx - mn) + 4)


def hue_difference(a, b):
    d = abs(a - b) % 360.0
    return min(d, 360.0 - d)


def regions(spot="bridge", root=REPO):
    """(name, rect, kind) of the spot's regions in the three region files, each name once."""
    out, seen = [], set()
    for name in REGION_FILES:
        doc = json.loads((Path(root) / "docs" / "native-renderer" / (name + ".json")).read_text(encoding="utf-8"))
        for r in doc.get("spots", {}).get(spot, {}).get("regions", []):
            if r["name"] not in seen:
                seen.add(r["name"])
                out.append((r["name"], r["rect"], r.get("kind", "")))
    return out


def region_box(rect, w, h):
    x0, y0, x1, y1 = (int(rect[0] * w + 0.5), int(rect[1] * h + 0.5), int(rect[2] * w + 0.5), int(rect[3] * h + 0.5))
    return x0, y0, max(x1, x0 + 1), max(y1, y0 + 1)


def region_mean(picture, w, h, rect, channels=3):
    """The mean colour of a region of a flat picture."""
    x0, y0, x1, y1 = region_box(rect, w, h)
    total, n = [0.0, 0.0, 0.0], 0
    for y in range(y0, min(y1, h)):
        row = picture[(y * w + x0) * channels:(y * w + min(x1, w)) * channels]
        for c in range(3):
            total[c] += sum(row[c::channels])
        n += len(row) // channels
    return [t / n for t in total] if n else [float("nan")] * 3


def outside_hud(w, h):
    """The rows' spans outside the HUD: for each row y the list of (x0, x1) that count."""
    top = int(HUD_TOP * h)
    bx0, by0, bx1, by1 = int(HUD_BOX[0] * w), int(HUD_BOX[1] * h), int(HUD_BOX[2] * w), int(HUD_BOX[3] * h)
    spans = []
    for y in range(h):
        if y < top:
            spans.append([])
        elif by0 <= y < by1:
            spans.append([(0, bx0), (bx1, w)])
        else:
            spans.append([(0, w)])
    return spans


def difference_stats(a, b, w, h, spans=None, channels=3, scale=255.0):
    """|a - b| * scale over the spans (all rows whole without): the median, mean, 90th and 99th percentile of
    the channel differences, and the share of pixels whose largest channel difference is within 1, 2, 4 and 8."""
    diffs, worst = [], []
    for y in range(h):
        for x0, x1 in (spans[y] if spans is not None else [(0, w)]):
            lo, hi = (y * w + x0) * channels, (y * w + x1) * channels
            ra, rb = a[lo:hi], b[lo:hi]
            d = [[abs(p - q) * scale for p, q in zip(ra[c::channels], rb[c::channels])] for c in range(3)]
            for c in range(3):
                diffs.extend(d[c])
            worst.extend(map(max, d[0], d[1], d[2]))
    if not worst:
        return None
    diffs.sort()
    n = float(len(worst))
    return {"pixels": len(worst), "median": percentile(diffs, 50), "mean": sum(diffs) / len(diffs),
            "p90": percentile(diffs, 90), "p99": percentile(diffs, 99),
            "within": {k: sum(1 for v in worst if v <= k) / n for k in (1, 2, 4, 8)}}


def region_rows(picture, front, w, h, spot="bridge"):
    """Per region: (name, kind, the front's luminance and hue, the picture's luminance over it, hue difference)."""
    rows = []
    for name, rect, kind in regions(spot):
        f, p = region_mean(front, w, h, rect), region_mean(picture, w, h, rect)
        lum_f = lf.post_lum(f)
        rows.append((name, kind, lum_f, hue(f), lf.post_lum(p) / lum_f if lum_f else float("nan"), hue_difference(hue(p), hue(f))))
    return rows


# --- the commands --------------------------------------------------------------------------------------------

def _pictures(fr, variants, table_offset, bias, say=print):
    scene = load_scene(fr)
    bad = sum(1 for v in scene if not math.isfinite(v))
    lum = sorted(lf.post_lum([lf.post_scene(scene[i]), lf.post_scene(scene[i + 1]), lf.post_scene(scene[i + 2])])
                 for i in range(0, len(scene), 3))
    say("scene luminance: median %.4f, 90%% %.4f, 99%% %.4f, max %.4f; values that are no numbers: %d" % (
        percentile(lum, 50), percentile(lum, 90), percentile(lum, 99), lum[-1], bad))
    bloom = None
    if "B" in variants:
        bw, bh, rows = bloom_picture(scene, SCENE_W, SCENE_H, fr, bias)
        flat = [v for row in rows for t in row for v in t]
        say("bloom (%d x %d, downsample bias %g): mean %.4f, max %.4f; times the composite's c31.x: mean %.4f" % (
            bw, bh, bias, sum(flat) / len(flat), max(flat), sum(flat) / len(flat) * fr.reg("composite", 31)))
        bloom = bloom_sampler(bw, bh, rows, SCENE_W, SCENE_H)
    return scene, {v: variant_picture(v, scene, SCENE_W, SCENE_H, fr, bloom, table_offset) for v in variants}


def _head(fr, say=print):
    c = fr.composite()
    say("capture %s frame %d: c20.w %.5f; composite c31.x %.4f c46.x %.4f c47.x %.2f c77 (%.5f, %.5f) c78.x %.5f" % (
        fr.path.name, fr.number, fr.c20w, c[0], c[1], c[2], c[3][0], c[3][1], c[4]))
    say("bright pass c31.x %.4f c46.x %.4f c47.x %.3f c77 (%.5f, %.5f); c77.y * c20.w %.6f; unlit scale %.4f" % (
        *fr.bright()[:3], *fr.bright()[3], fr.reg("bright", 77, 1) * fr.c20w, lf.post_unlit_scale(c[3][1], fr.table)))


def cmd_replay(args):
    fr = load_frame(find_capture(args.capture), args.frame)
    variants = [v.upper() for v in (args.variant or ["A", "B"])]
    _head(fr)
    print("table offset %g texels%s" % (args.table_offset, "" if args.table_offset == 0.5 else " (not the reference's 0.5)"))
    scene, pictures = _pictures(fr, variants, args.table_offset, args.bias)
    front = load_front(fr)
    spans = outside_hud(SCENE_W, SCENE_H)
    for v in variants:
        print()
        print("variant %s against the front buffer 0x%08X" % (v, FRONT_BASE))
        print("%-32s %-10s | front lum   hue | ratio  d hue" % ("region", "kind"))
        rows = region_rows(pictures[v], front, SCENE_W, SCENE_H, args.spot)
        for name, kind, lum_f, hue_f, ratio, dh in rows:
            print("%-32s %-10s | %.3f %7.1f | %.3f %6.1f" % (name[:32], kind[:10], lum_f, hue_f, ratio, dh))
        ratios = [r[4] for r in rows]
        print("%d regions: ratio %.3f to %.3f" % (len(rows), min(ratios), max(ratios)))
        s = difference_stats(pictures[v], front, SCENE_W, SCENE_H, spans)
        print("whole picture outside the HUD, %s: |d| in 8-bit levels: median %.2f, mean %.2f, 90%% %.2f, 99%% %.2f; share "
              "within 4 levels %.1f%%, within 8 %.1f%%" % (v, s["median"], s["mean"], s["p90"], s["p99"],
                                                           100 * s["within"][4], 100 * s["within"][8]))
    ramp = load_ramp(args.ramp)
    for prefix, png in ((args.write, False), (args.png, True)):
        if not prefix:
            continue
        for name, picture in list(pictures.items()) + [("final", front)]:
            rgba = to_bytes(picture, None if name == "N" else ramp)      # today's mapping bypasses scan-out
            if png:
                write_png("%s_%s.png" % (prefix, name), SCENE_W, SCENE_H, rgba, max_edge=SCENE_W)
            else:
                write_dump("%s_%s.rgba" % (prefix, name), SCENE_W, SCENE_H, rgba)
        print("wrote %s_{%s}.%s%s" % (prefix, ",".join(list(pictures) + ["final"]), "png" if png else "rgba",
                                      " (no ramp)" if not ramp else " through the measured STAND-IN ramp, not the game's table"
                                      if args.ramp == "measured" else " through the ramp `%s`" % args.ramp))
    return 0


def cmd_predict(args):
    fr = load_frame(find_capture(args.capture), args.frame)
    ramp = load_ramp(args.ramp)
    _head(fr)
    print("ramp: %s" % ("the measured stand-in (spec criterion 4's pairs, straight lines between)"
                        if args.ramp == "measured" else args.ramp))
    scene, pictures = _pictures(fr, ["A", "B", "N"], 0.5, args.bias)
    shown = {v: array("d", [b / 255.0 for b in to_bytes(p, None if v == "N" else ramp)]) for v, p in pictures.items()}
    print()
    print("mean luminance of the display values, by region: the variant after the ramp over N (today's mapping, no ramp)")
    print("%-32s %-10s |  N lum |  A / N   B / N" % ("region", "kind"))
    for name, rect, kind in regions(args.spot):
        lum = {v: lf.post_lum(region_mean(shown[v], SCENE_W, SCENE_H, rect, 4)) for v in shown}
        print("%-32s %-10s | %.4f | %6.3f  %6.3f" % (name[:32], kind[:10], lum["N"], lum["A"] / lum["N"], lum["B"] / lum["N"]))
    return 0


def cmd_export(args):
    fr = load_frame(find_capture(args.capture), args.frame)
    scene = load_scene(fr)
    bad = sum(1 for v in scene if not math.isfinite(v))
    Path(args.prefix).parent.mkdir(parents=True, exist_ok=True)
    Path(args.prefix + ".scene").write_bytes(pack_scene(scene, SCENE_W, SCENE_H))
    Path(args.prefix + ".block").write_bytes(pack_block(fr))
    _head(fr)
    print("wrote %s.scene (%d x %d, %d values that are no numbers) and %s.block (parts %d)" % (
        args.prefix, SCENE_W, SCENE_H, bad, args.prefix, block_floats(fr)[1]))
    return 0


def excerpt(fr, scene, front, x0, y0, size=16, bias=0.0):
    """A size x size block of a frame as a JSON document: the scene texels, the bloom at them, the table's
    bytes, the registers, and variants A, B and N at them; the front buffer's values beside."""
    bw, bh, rows = bloom_picture(scene, SCENE_W, SCENE_H, fr, bias)
    bloom = bloom_sampler(bw, bh, rows, SCENE_W, SCENE_H)
    table, regs = fr.table, fr.composite()
    doc = {"about": "A %d x %d block of a read-back capture for tests/test_post_replay.py and tools/xdk_sigmatch/post_pins.py, "
                    "written by post_replay.py excerpt: scene texels (the game's scale), the replay's bloom at them, the "
                    "frame's table (the 2,048 bytes as dumped) and post registers (single precision, as the rows hold them), "
                    "variants A, B and N at them, and the game's front buffer there (resampled from 1280 columns)." % (size, size),
           "capture": fr.path.name, "frame": fr.number, "x0": x0, "y0": y0, "size": size, "bias": bias, "c20w": fr.c20w,
           "regs": {role: {"c%d" % r: list(v) for r, v in sorted(regs_.items())} for role, regs_ in sorted(fr.regs.items())},
           "table": fr.table_bytes.hex(), "scene": [], "bloom": [], "A": [], "B": [], "N": [], "front": []}
    for y in range(y0, y0 + size):
        for x in range(x0, x0 + size):
            i = (y * SCENE_W + x) * 3
            s, b = list(scene[i:i + 3]), list(bloom(x, y))
            doc["scene"].append(s)
            doc["bloom"].append(b)
            doc["A"].append(lf.post_composite(s, (0.0, 0.0, 0.0), table, *regs))
            doc["B"].append(lf.post_composite(s, b, table, *regs))
            doc["N"].append(native_mapping(s, fr.c20w))
            doc["front"].append(list(front[i:i + 3]))
    return doc


def cmd_excerpt(args):
    fr = load_frame(find_capture(args.capture), args.frame)
    x0, y0 = (int(v) for v in args.at.split(","))
    doc = excerpt(fr, load_scene(fr), load_front(fr), x0, y0)
    # One key a line: the arrays stay on their key's line.
    text = "{\n" + ",\n".join(" %s: %s" % (json.dumps(k), json.dumps(v)) for k, v in doc.items()) + "\n}\n"
    Path(args.out).write_text(text, encoding="ascii", newline="\n")
    print("wrote %s: %s frame %d, %d x %d at (%d, %d)" % (args.out, fr.path.name, fr.number, doc["size"], doc["size"], x0, y0))
    return 0


def compare_report(a, b, w, h, spot=None, say=print):
    """The comparison of two RGBA byte pictures: whole and by region."""
    def line(name, s):
        say("%-32s %8d px: median %.2f, 90%% %.2f; within 1 level %.2f%%, within 2 %.2f%%, within 8 %.2f%%" % (
            name[:32], s["pixels"], s["median"], s["p90"], 100 * s["within"][1], 100 * s["within"][2], 100 * s["within"][8]))

    whole = difference_stats(a, b, w, h, channels=4, scale=1.0)
    line("whole picture", whole)
    line("outside the HUD", difference_stats(a, b, w, h, outside_hud(w, h), channels=4, scale=1.0))
    for name, rect, kind in (regions(spot) if spot else []):
        x0, y0, x1, y1 = region_box(rect, w, h)
        spans = [[(x0, min(x1, w))] if y0 <= y < y1 else [] for y in range(h)]
        s = difference_stats(a, b, w, h, spans, channels=4, scale=1.0)
        if s:
            line(name, s)
    return whole


def cmd_compare(args):
    w, h, a = read_dump(args.dump)
    w2, h2, b = read_dump(args.picture)
    if (w, h) != (w2, h2):
        raise ReplayError("%d x %d against %d x %d" % (w, h, w2, h2))
    print("%s against %s (%d x %d), absolute difference in 8-bit levels" % (args.dump, args.picture, w, h))
    compare_report(a, b, w, h, args.spot)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)

    def capture(p):
        p.add_argument("capture")
        p.add_argument("frame", type=int)
        p.add_argument("--bias", type=float, default=0.0, help="the downsample's pixel position: 0 integral, 0.5 the centre")
        p.add_argument("--spot", default="bridge")

    p = sub.add_parser("replay")
    capture(p)
    p.add_argument("--variant", action="append", choices=("A", "B", "N", "a", "b", "n"))
    p.add_argument("--table-offset", type=float, default=0.5)
    p.add_argument("--ramp")
    p.add_argument("--write", metavar="PREFIX")
    p.add_argument("--png", metavar="PREFIX")
    p.set_defaults(run=cmd_replay)
    p = sub.add_parser("predict")
    capture(p)
    p.add_argument("--ramp", default="measured")
    p.set_defaults(run=cmd_predict)
    p = sub.add_parser("export")
    capture(p)
    p.add_argument("prefix")
    p.set_defaults(run=cmd_export)
    p = sub.add_parser("excerpt")
    capture(p)
    p.add_argument("--at", required=True, metavar="X,Y")
    p.add_argument("--out", required=True)
    p.set_defaults(run=cmd_excerpt)
    p = sub.add_parser("compare")
    p.add_argument("dump")
    p.add_argument("picture")
    p.add_argument("--spot", default="bridge", help="the regions' spot; `none` for the whole picture alone")
    p.set_defaults(run=cmd_compare)
    args = ap.parse_args(argv)
    if getattr(args, "spot", None) == "none":
        args.spot = None
    try:
        return args.run(args)
    except (ReplayError, OSError, ValueError) as e:
        print("post_replay: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
