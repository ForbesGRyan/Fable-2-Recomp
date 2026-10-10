"""The post chain of a discovery capture (frame-map section 17): resolves, the composite's table, the ramp.

Decoders (pure, tested on hand-made bytes in tests/test_post_tables.py):
  pm4_packets(dwords)          -> the PM4 packets of a run of command dwords (type 0: a register run, type 1:
                                  two registers, type 2: nothing, type 3: an opcode with its data), with the
                                  SDK's reading of the headers (command_processor.cpp, ExecutePacketType0..3).
  draw_events(dwords)          -> every draw packet (PM4_DRAW_INDX 0x22, PM4_DRAW_INDX_2 0x36) of such a run
                                  with its initiator, and the copy registers 0x2318..0x231B and RB_MODECONTROL
                                  as the run's own packets left them at that draw: a resolve is a draw in copy
                                  mode (RB_MODECONTROL's low three bits 6), or, in a run that never writes the
                                  mode, one whose copy registers the run wrote since its previous draw.
  decode_copy(control, base, pitch, info) -> the fields of the four copy registers (SDK registers.h).
  decode_table(bytes)          -> the composite's table: 512 floats, each stored big-endian (1D k_32_FLOAT,
                                  endian 8-in-32: fetch constant 04000956 1FAA00A4 000001FF 010016D1 .. ..).
  table_texel(L)               -> the texel scene luminance L reads with the captured c46.x = 0.25:
                                  floor(128 L), at most 511 (the reading the research prints; the fetch
                                  convention itself is settled in light_formulas, not here).
  parse_ramp_lines(text)       -> the "[native] ramp: read at clay frame N: valid V, <hex>" lines of a game log.
  find_plane(mem, ref, w, h)   -> where in a memory dump a w x h plane of 32-bit floats lies that correlates
                                  with `ref`, over page-aligned bases, linear and tiled layouts, both byte orders.
  downsample_four(scene, w, h, first) -> the game's 4 x 4 downsample as the mean of four texels of each block,
                                  (first, first), (first + 2, first), (first, first + 2), (first + 2, first + 2):
                                  first 0 with an integral pixel position, 2 with the pixel's centre (87E8:
                                  fetches at 4 p + (0.5 | 2.5) with p the pixel position).

Reports (every number is of the files named on the command line):
  post_tables.py resolves <capture.jsonl> [--frame N] [--all]
        the capture's "resolve" rows (the clear routine DrawIndx2:821EF988) and "cmdgap" rows (the command
        stream between two hooked draws that names a copy register): each resolve's RB_COPY_DEST_BASE,
        pitch, height, format, the draw packet's opcode and initiator, and the decoded row before it with its
        pixel shader; then, per frame, the resolves that follow the luminance pass 5821366EE8FDE893.
  post_tables.py table <capture.jsonl> [--full]
        per frame: c20.w, the composite's and the bright pass's registers, c77.y * c20.w, the table at
        scene luminance 0.01 .. 3.99, its change from the frame before, the rules of MakePostBlock (plan
        Task 4) it would fail, and whether the composite's row precedes the frame row.
  post_tables.py ramp <game.log> [--pairs <ramp_092959.txt>]
        the ramp lines of a log: the table at the levels of spec criterion 4 against the measured pairs,
        and how often the table changed.
  post_tables.py find-luminance <capture.jsonl> [--frame N] [--threshold 0.99]
        the capture's memory dump (mem_12A00000_f<N>.bin, made under --readback_resolve) searched for the
        luminance picture: lum of the 4 x 4 downsample of the scene colour the frame's composite sampled.
  post_tables.py downsample <capture.jsonl> [--frame N]
        the downsample buffer the luminance pass sampled against the CPU downsample of the scene colour
        the downsample pass sampled, with the pixel position integral and at the pixel's centre.
The texture dumps are read relative to the capture's folder (or --tex-root).
"""

import argparse
import json
import math
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fog_tables as ft  # noqa: E402
from texture_thumb import tiled_offset_2d  # noqa: E402

COMPOSITE_PS = "37B186F4BD3288F7"
BRIGHT_PS = "EE922CFB4426EA83"
BLUR_X_PS = "EA83C7E7E24EC4C8"
BLUR_Y_PS = "28DBE18F313350BF"
RADIAL_PS = "4BE4344C27016E04"
LUMINANCE_PS = "5821366EE8FDE893"
DOWNSAMPLE_PS = "87E85199E1E9B260"
POST_PS = {COMPOSITE_PS: "composite", BRIGHT_PS: "bright", BLUR_X_PS: "blur-x", BLUR_Y_PS: "blur-y",
           RADIAL_PS: "radial", LUMINANCE_PS: "luminance", DOWNSAMPLE_PS: "downsample"}

TABLE_TEXELS = 512
TABLE_BYTES = 2048
# The captured fetch constant of the composite's table (research section 3; plan Task 4's pin).
TABLE_FC = (0x04000956, 0x1FAA00A4, 0x000001FF, 0x010016D1, 0x00000000, 0x00000018)
LUMINANCES = (0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 3, 3.99)
# Spec criterion 4: front-buffer level -> shot level (out/tests/post-research/ramp_092959.txt).
RAMP_PAIRS = ((23, 10), (47, 32), (63, 47), (124, 110), (156, 144), (204, 197), (236, 232), (252, 250))

REG_COPY_CONTROL, REG_COPY_DEST_BASE, REG_COPY_DEST_PITCH, REG_COPY_DEST_INFO = 0x2318, 0x2319, 0x231A, 0x231B
REG_MODECONTROL = 0x2208
COPY_REGS = (REG_COPY_CONTROL, REG_COPY_DEST_BASE, REG_COPY_DEST_PITCH, REG_COPY_DEST_INFO)
PM4_DRAW_INDX, PM4_DRAW_INDX_2 = 0x22, 0x36
PRIMITIVES = {1: "points", 2: "lines", 3: "line-strip", 4: "triangles", 5: "fan", 6: "strip", 8: "rectangles",
              13: "quads"}
COPY_COMMANDS = ("raw", "convert", "const-one", "null")
FORCE_WINDOW = 1 << 16  # the SDK's kForceReadbackWindow


# --- command packets ----------------------------------------------------------------------------------------

def hex_dwords(text):
    """The dwords of a row's "cmd" (hex of the bytes in guest order: eight digits a dword, big-endian)."""
    if len(text) % 8:
        raise ValueError("%d hex digits are no whole dwords" % len(text))
    return [int(text[i:i + 8], 16) for i in range(0, len(text), 8)]


def pm4_packets(dwords):
    """The packets of a run of command dwords. A packet whose data would end beyond the run is the last one,
    with "truncated": True and the data that is there."""
    out = []
    i, n = 0, len(dwords)
    while i < n:
        d = dwords[i]
        kind = d >> 30
        if kind == 0:
            count = ((d >> 16) & 0x3FFF) + 1
            p = {"type": 0, "at": i, "reg": d & 0x7FFF, "one": bool(d & 0x8000), "values": dwords[i + 1:i + 1 + count]}
        elif kind == 1:
            count = 2
            p = {"type": 1, "at": i, "regs": (d & 0x7FF, (d >> 11) & 0x7FF), "values": dwords[i + 1:i + 3]}
        elif kind == 2:
            count = 0
            p = {"type": 2, "at": i}
        else:
            count = ((d >> 16) & 0x3FFF) + 1
            p = {"type": 3, "at": i, "opcode": (d >> 8) & 0x7F, "predicate": bool(d & 1), "data": dwords[i + 1:i + 1 + count]}
        if i + 1 + count > n:
            p["truncated"] = True
            out.append(p)
            break
        out.append(p)
        i += 1 + count
    return out


def register_writes(packet):
    """(register, value) pairs a type-0 or type-1 packet writes, in order."""
    if packet["type"] == 0:
        return [(packet["reg"] if packet["one"] else packet["reg"] + k, v) for k, v in enumerate(packet["values"])]
    if packet["type"] == 1:
        return list(zip(packet["regs"], packet["values"]))
    return []


def decode_copy(control, base, pitch, info):
    """The copy registers' fields (SDK registers.h: RB_COPY_CONTROL, RB_COPY_DEST_PITCH, RB_COPY_DEST_INFO).
    A register the run did not write is None, and so are its fields."""
    out = {"base": base, "control": control, "pitch_word": pitch, "info": info}
    if control is not None:
        out.update(src_select=control & 7, sample_select=(control >> 4) & 7, color_clear=bool(control & 0x100),
                   depth_clear=bool(control & 0x200), command=COPY_COMMANDS[(control >> 20) & 3])
    if pitch is not None:
        out.update(pitch=pitch & 0x3FFF, height=(pitch >> 16) & 0x3FFF)
    if info is not None:
        out.update(endian=info & 7, array=bool(info & 8), slice=(info >> 4) & 7, format=(info >> 7) & 0x3F,
                   number=(info >> 13) & 7, swap=bool(info & (1 << 24)))
    return out


def draw_events(dwords):
    """Every draw packet of a run: {"at", "opcode", "initiator", "prim", "indices", "copy" (decode_copy of the
    registers as the run's own packets left them, None for one the run never wrote), "fresh" (the copy registers
    the run wrote since its previous draw), "mode" (RB_MODECONTROL as the run left it, or None), "resolve"}."""
    regs = {}
    fresh = set()
    out = []
    for p in pm4_packets(dwords):
        for reg, value in register_writes(p):
            regs[reg] = value
            if reg in COPY_REGS:
                fresh.add(reg)
        if p["type"] != 3 or p["opcode"] not in (PM4_DRAW_INDX, PM4_DRAW_INDX_2):
            continue
        data = p["data"]
        # PM4_DRAW_INDX: a viz query token, then the initiator; PM4_DRAW_INDX_2: the initiator first.
        at = 1 if p["opcode"] == PM4_DRAW_INDX else 0
        initiator = data[at] if len(data) > at else None
        mode = regs.get(REG_MODECONTROL)
        out.append({
            "at": p["at"], "opcode": p["opcode"], "initiator": initiator,
            "prim": None if initiator is None else initiator & 0x3F,
            "indices": None if initiator is None else initiator >> 16,
            "copy": decode_copy(*(regs.get(r) for r in COPY_REGS)), "fresh": sorted(fresh), "mode": mode,
            # The run's own mode word decides where it has one: the game writes the copy registers once more
            # after a resolve (control and info back to 0), before an ordinary draw in colour mode.
            "resolve": (mode & 7) == 6 if mode is not None else bool(fresh),
        })
        fresh = set()
    return out


def describe_event(e):
    c = e["copy"]
    base = "base ?" if c["base"] is None else "base 0x%08X" % c["base"]
    pitch = "pitch ? height ?" if c.get("pitch") is None else "pitch %d height %d" % (c["pitch"], c["height"])
    fmt = "format ?" if c.get("format") is None else "format %d endian %d" % (c["format"], c["endian"])
    cmd = "control ?" if c["control"] is None else "control 0x%08X (%s, source %d%s%s)" % (
        c["control"], c["command"], c["src_select"], ", colour clear" if c["color_clear"] else "",
        ", depth clear" if c["depth_clear"] else "")
    draw = "%s initiator %s (%s, %s indices)" % (
        "PM4_DRAW_INDX_2" if e["opcode"] == PM4_DRAW_INDX_2 else "PM4_DRAW_INDX",
        "?" if e["initiator"] is None else "0x%08X" % e["initiator"],
        PRIMITIVES.get(e["prim"], "prim %s" % e["prim"]), e["indices"])
    mode = "" if e["mode"] is None else ", RB_MODECONTROL 0x%X" % e["mode"]
    return "%s %s %s | %s | %s%s" % (base, pitch, fmt, cmd, draw, mode)


def forced_by_sdk(e):
    """Whether the SDK's forced read-back would take this resolve were its base listed: issued as
    PM4_DRAW_INDX_2, a rectangle list of 3 indices (CommandProcessor::ShouldForceReadbackResolve)."""
    return e["opcode"] == PM4_DRAW_INDX_2 and e["prim"] == 8 and e["indices"] == 3


# --- the table ----------------------------------------------------------------------------------------------

def decode_table(data):
    if len(data) < TABLE_BYTES:
        raise ValueError("the table needs %d bytes, got %d" % (TABLE_BYTES, len(data)))
    return list(struct.unpack(">%df" % TABLE_TEXELS, data[:TABLE_BYTES]))


def table_texel(luminance):
    return max(0, min(TABLE_TEXELS - 1, int(math.floor(128.0 * luminance))))


def table_at(table, luminance):
    return table[table_texel(luminance)]


def block_violations(composite, bright, table, fc=None):
    """The rules of MakePostBlock (plan Task 4) that these values would fail: composite (c31.x, c46.x, c47.x,
    c77.x, c77.y, c78.x), bright (c31.x, c46.x, c47.x, c77.x, c77.y), the 512 texels, the table's fetch constant."""
    bad = []
    names_c = ("composite c31.x", "composite c46.x", "composite c47.x", "composite c77.x", "composite c77.y",
               "composite c78.x")
    names_b = ("bright c31.x", "bright c46.x", "bright c47.x", "bright c77.x", "bright c77.y")
    for name, v in list(zip(names_c, composite)) + list(zip(names_b, bright)):
        if v is None or not math.isfinite(v):
            bad.append("%s is %s (register)" % (name, v))
    if bad:
        return bad
    c31, c46, c47, c77x, c77y, c78 = composite
    rules = (
        ("composite c31.x in [0, 64]", 0 <= c31 <= 64), ("composite c46.x in (0, 64]", 0 < c46 <= 64),
        ("composite c47.x in [0, 1e6]", 0 <= c47 <= 1e6), ("composite |c77.x| <= 4", abs(c77x) <= 4),
        ("composite c77.y in (0, 64]", 0 < c77y <= 64), ("composite c78.x in (0, 1]", 0 < c78 <= 1),
        ("bright c31.x in [0, 64]", 0 <= bright[0] <= 64), ("bright c46.x in [0, 64]", 0 <= bright[1] <= 64),
        ("bright c47.x in [1e-3, 64]", 1e-3 <= bright[2] <= 64), ("bright |c77.x| <= 4", abs(bright[3]) <= 4),
        ("bright c77.y in (0, 64]", 0 < bright[4] <= 64),
    )
    bad += ["%s (range)" % name for name, ok in rules if not ok]
    if fc is not None and tuple(fc) != TABLE_FC:
        # The address field may differ in its low bits only by what the constant itself holds: compared whole.
        bad.append("fetch constant %s is not the pin %s" % (" ".join("%08X" % x for x in fc),
                                                            " ".join("%08X" % x for x in TABLE_FC)))
    if table is None:
        bad.append("no table (table-bytes)")
    else:
        worst = [i for i, t in enumerate(table) if not math.isfinite(t) or not 0 <= t < 4]
        if worst:
            bad.append("%d texels not finite or outside [0, 4), the first at %d: %r (table-texel)" % (
                len(worst), worst[0], table[worst[0]]))
    return bad


# --- the ramp -----------------------------------------------------------------------------------------------

_RE_RAMP = re.compile(r"\[native\] ramp: read at clay frame (\d+): valid (\d), ([0-9A-Fa-f]{2304})")
_RE_RAMP_CHANGES = re.compile(r"\[native\] ramp: changes (\d+)")
_RE_PAIR = re.compile(r"^\s*(\d+) ->\s+([0-9.]+)")


def parse_ramp_lines(text):
    """[(clay frame, valid, [(r, g, b)] * 256)] of a game log's ramp lines, each channel 0..1023."""
    out = []
    for m in _RE_RAMP.finditer(text):
        h = m.group(3)
        table = [tuple(int(h[9 * i + 3 * c:9 * i + 3 * c + 3], 16) for c in range(3)) for i in range(256)]
        out.append((int(m.group(1)), int(m.group(2)), table))
    return out


def ramp_level(entry10):
    """A 10-bit ramp entry as an 8-bit level."""
    return entry10 * 255.0 / 1023.0


def ramp_valid(table):
    """The rule of MakePostRamp: every entry at most 1023 and no channel decreasing."""
    return all(v <= 1023 for e in table for v in e) and all(
        table[i][c] >= table[i - 1][c] for i in range(1, len(table)) for c in range(3))


def parse_pairs(text):
    """(level, shot level) of the lines "<level> -> <median shot level> ..." (ramp_092959.txt)."""
    return [(int(m.group(1)), float(m.group(2))) for m in map(_RE_PAIR.match, text.splitlines()) if m]


# --- finding a float plane in memory ------------------------------------------------------------------------

def _correlation(a, b):
    n = len(a)
    if n < 2 or n != len(b):
        return 0.0
    ma, mb = sum(a) / n, sum(b) / n
    saa = sum((x - ma) ** 2 for x in a)
    sbb = sum((y - mb) ** 2 for y in b)
    if not saa > 0 or not sbb > 0 or not math.isfinite(saa) or not math.isfinite(sbb):
        return 0.0
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(saa * sbb)


def plane_layouts(w):
    """(name, pitch in texels, tiled) of the layouts a w-wide plane of 4-byte texels is looked for in: the
    width rounded up to 32 (a texture's pitch) and 32 more (a 280-wide surface's pitch 320), linear and tiled."""
    p = (w + 31) & ~31
    return [("linear pitch %d" % p, p, False), ("linear pitch %d" % (p + 32), p + 32, False),
            ("tiled pitch %d" % p, p, True), ("tiled pitch %d" % (p + 32), p + 32, True)]


def _plane_offset(x, y, pitch, tiled):
    return tiled_offset_2d(x, y, pitch, 2) if tiled else (y * pitch + x) * 4


def find_plane(mem, ref, w, h, threshold=0.99, step=0x1000, samples=600):
    """Where `mem` holds a w x h plane of 32-bit floats that correlates with `ref` (w * h values, row-major)
    at `threshold` or above: [(correlation, offset, layout name, byte order)], best first. Bases every `step`
    bytes; a base is first judged on `samples` texels spread over the plane, and read whole only if those
    correlate at 0.9 * threshold or above. A plane with a value that is no number does not count."""
    if len(ref) != w * h:
        raise ValueError("the reference needs %d values, got %d" % (w * h, len(ref)))
    n = w * h
    stride = max(1, n // samples)
    picks = list(range(stride // 2, n, stride))
    ref_picks = [ref[i] for i in picks]
    found = []
    for name, pitch, tiled in plane_layouts(w):
        offsets = [_plane_offset(i % w, i // w, pitch, tiled) for i in range(n)]
        extent = max(offsets) + 4
        pick_offsets = [offsets[i] for i in picks]
        for base in range(0, len(mem) - extent + 1, step):
            for order, code in (("big-endian", ">f"), ("little-endian", "<f")):
                unpack = struct.Struct(code).unpack_from
                some = [unpack(mem, base + o)[0] for o in pick_offsets]
                if not all(map(math.isfinite, some)) or _correlation(some, ref_picks) < 0.9 * threshold:
                    continue
                whole = [unpack(mem, base + o)[0] for o in offsets]
                if not all(map(math.isfinite, whole)):
                    continue
                r = _correlation(whole, ref)
                if r >= threshold:
                    found.append((r, base, name, order))
    found.sort(key=lambda t: -t[0])
    return found


# --- the downsample -----------------------------------------------------------------------------------------

def luminance(r, g, b):
    return 0.2125 * r + 0.7154 * g + 0.0721 * b


def downsample_four(scene, w, h, first):
    """The mean of four texels of each 4 x 4 block of a w x h picture of 4 floats a texel (row-major):
    columns and rows first and first + 2 of the block, clamped to the picture. (w // 4) * (h // 4) * 4 floats."""
    ow, oh = w // 4, h // 4
    out = [0.0] * (ow * oh * 4)
    for y in range(oh):
        ys = (min(h - 1, 4 * y + first), min(h - 1, 4 * y + first + 2))
        for x in range(ow):
            xs = (min(w - 1, 4 * x + first), min(w - 1, 4 * x + first + 2))
            o = (y * ow + x) * 4
            for c in range(4):
                out[o + c] = 0.25 * sum(scene[(yy * w + xx) * 4 + c] for yy in ys for xx in xs)
    return out


def downsample_box(scene, w, h):
    """The mean of all sixteen texels of each 4 x 4 block."""
    ow, oh = w // 4, h // 4
    out = [0.0] * (ow * oh * 4)
    for y in range(oh):
        for x in range(ow):
            o = (y * ow + x) * 4
            for c in range(4):
                out[o + c] = sum(scene[((4 * y + j) * w + 4 * x + i) * 4 + c] for j in range(4) for i in range(4)) / 16.0
    return out


def median(values):
    s = sorted(values)
    return s[len(s) // 2] if s else float("nan")


def colour_difference(a, b):
    """Per texel the largest absolute difference of red, green and blue between two pictures of 4 floats a
    texel: (median, 90th percentile, largest), and the median of the reference's own largest component."""
    n = min(len(a), len(b)) // 4
    d = sorted(max(abs(a[4 * i + c] - b[4 * i + c]) for c in range(3)) for i in range(n))
    level = median([max(b[4 * i:4 * i + 3]) for i in range(n)])
    if not d:
        return float("nan"), float("nan"), float("nan"), level
    return d[len(d) // 2], d[len(d) * 9 // 10], d[-1], level


# --- reading a capture --------------------------------------------------------------------------------------

class PostFrame:
    def __init__(self, number):
        self.number = number
        self.events = []          # in row order: ("row", line, kind, ps hash, in scene) | ("resolve" | "cmdgap", line, row)
        self.rows = {}            # post shader hash -> [parsed "verts" rows outside the main scene]
        self.row_lines = {}       # post shader hash -> [line numbers]
        self.frame_line = None    # the "frame" row's line
        self.out_scale = None     # c20.w: the frame row's light block, or the first main-scene row's c20
        self.out_scale_from = None
        self.memdump = None       # the "memdump" row


class PostCapture:
    def __init__(self, path, root):
        self.path = Path(path)
        self.root = Path(root)
        self.frames = {}
        self.by_content = {}      # (base, content) -> fog_tables.Texture
        self.by_base = {}         # base -> the first Texture row at it

    def frame(self, n):
        if n not in self.frames:
            self.frames[n] = PostFrame(n)
        return self.frames[n]

    def texture(self, fc, content):
        base = fc[1] & 0xFFFFF000
        t = self.by_content.get((base, content)) if content is not None else None
        return t if t is not None else self.by_base.get(base)


_RE_FRAME = re.compile(r'"frame":(\d+)')
_RE_PS = re.compile(r'"ps_hash":"0x([0-9A-Fa-f]{16})"')


def load_capture(path, tex_root=None):
    """Reads a capture without parsing every row: the "texture", "frame", "memdump", "resolve" and "cmdgap"
    rows, the decoded rows of the post shaders, and of every other decoded row its kind and pixel shader."""
    path = Path(path)
    cap = PostCapture(path, tex_root if tex_root else path.parent)
    with open(path, encoding="utf-8") as f:
        for line_no, line in enumerate(f):
            head = line[:48]
            if not head.startswith('{"kind":"'):
                continue
            kind = head[9:head.find('"', 9)]
            if kind == "texture":
                t = ft.Texture(json.loads(line), cap.root)
                if t.content is not None:
                    cap.by_content.setdefault((t.base, t.content), t)
                cap.by_base.setdefault(t.base, t)
                continue
            if kind in ("raw", "aluload", "indxdata"):
                continue
            m = _RE_FRAME.search(head)
            if not m:
                continue
            fr = cap.frame(int(m.group(1)))
            if kind == "frame":
                r = json.loads(line)
                fr.frame_line = line_no
                light = r.get("light") or {}
                if light.get("valid") and isinstance(light.get("out_scale"), (int, float)):
                    fr.out_scale, fr.out_scale_from = float(light["out_scale"]), "the frame row's light block"
            elif kind == "memdump":
                fr.memdump = json.loads(line)
            elif kind in ("resolve", "cmdgap"):
                fr.events.append((kind, line_no, json.loads(line)))
            elif kind in ("draw", "tess", "verts", "indx"):
                mps = _RE_PS.search(line)
                ps = mps.group(1).upper() if mps else ""
                in_scene = '"in_scene":true' in line[:400]
                fr.events.append(("row", line_no, kind, ps, in_scene))
                if ps in POST_PS and not in_scene:
                    fr.rows.setdefault(ps, []).append(json.loads(line))
                    fr.row_lines.setdefault(ps, []).append(line_no)
                elif in_scene and fr.out_scale_from is None and '"pfull"' in line:
                    c20 = ft.pfull_reg(json.loads(line)["pfull"], 20)
                    if c20 is not None:
                        fr.out_scale, fr.out_scale_from = c20[3], "c20.w of the first main-scene row"
    return cap


def _reg(row, n):
    return ft.pfull_reg(row["pfull"], n) if row and "pfull" in row else None


def _x(v, i=0):
    return None if v is None else v[i]


def composite_registers(row):
    """(c31.x, c46.x, c47.x, c77.x, c77.y, c78.x) of a composite row."""
    return (_x(_reg(row, 31)), _x(_reg(row, 46)), _x(_reg(row, 47)), _x(_reg(row, 77)), _x(_reg(row, 77), 1),
            _x(_reg(row, 78)))


def bright_registers(row):
    """(c31.x, c46.x, c47.x, c77.x, c77.y) of a bright-pass row."""
    return (_x(_reg(row, 31)), _x(_reg(row, 46)), _x(_reg(row, 47)), _x(_reg(row, 77)), _x(_reg(row, 77), 1))


def row_texture(cap, row, slot):
    """(fetch constant, content hash or None, Texture or None) of a row's texture slot."""
    fc = (row.get("tf") or {}).get(str(slot))
    if not fc:
        return None, None, None
    fc = [int(x, 16) for x in fc]
    content = (row.get("tfc") or {}).get(str(slot))
    content = int(content, 16) if content else None
    return fc, content, cap.texture(fc, content)


def frame_table(cap, fr):
    """(table or None, fetch constant, content, why not) of a frame's composite row."""
    rows = fr.rows.get(COMPOSITE_PS)
    if not rows:
        return None, None, None, "no composite row"
    fc, content, t = row_texture(cap, rows[0], 1)
    if fc is None:
        return None, None, None, "the composite row has no slot 1"
    if content is None or t is None or t.content != content:
        return None, fc, content, "the table's content was not dumped"
    if not t.path.is_file():
        return None, fc, content, "the dump %s is missing" % t.file
    try:
        return decode_table(t.read()), fc, content, None
    except ValueError as e:
        return None, fc, content, str(e)


# --- reports ------------------------------------------------------------------------------------------------

def _f(v, digits=5):
    return "?" if v is None else "%.*f" % (digits, v)


def frame_resolves(fr):
    """[(event, the decoded row before it as (kind, ps hash, in scene) or None, the source row's kind and n)]
    of a frame, in row order."""
    out = []
    last = None
    for ev in fr.events:
        if ev[0] == "row":
            last = ev[2:]
            continue
        row = ev[2]
        if not row.get("cmd"):
            continue
        for e in draw_events(hex_dwords(row["cmd"])):
            if e["resolve"]:
                out.append((e, last, "%s %s" % (ev[0], row.get("n"))))
    return out


def report_resolves(cap, frame=None, show_all=False):
    after_lum = {}
    counts = {"resolve": 0, "cmdgap": 0, "cmd_ok": 0}
    for n in sorted(cap.frames):
        if frame is not None and n != frame:
            continue
        fr = cap.frames[n]
        for ev in fr.events:
            if ev[0] in counts:
                counts[ev[0]] += 1
                counts["cmd_ok"] += 1 if ev[0] == "resolve" and ev[2].get("cmd_ok") else 0
        res = frame_resolves(fr)
        print("frame %d: %d resolves in %d rows with command dwords" % (
            n, len(res), sum(1 for ev in fr.events if ev[0] != "row" and ev[2].get("cmd"))))
        for e, last, src in res:
            ps = last[1] if last else ""
            if ps == LUMINANCE_PS:
                after_lum.setdefault(n, []).append(e)
            if show_all or ps in POST_PS:
                print("  [%s] after %s %s%s%s: %s" % (
                    src, last[0] if last else "no row", ps[:4] if ps else "-",
                    " (%s)" % POST_PS[ps] if ps in POST_PS else "", " in scene" if last and last[2] else "",
                    describe_event(e)))
    print("rows: %d \"resolve\" (%d with dwords), %d \"cmdgap\"" % (counts["resolve"], counts["cmd_ok"], counts["cmdgap"]))
    frames = [n for n in sorted(cap.frames) if frame is None or n == frame]
    bases = set()
    complete = True
    for n in frames:
        ev = after_lum.get(n, [])
        b = sorted({e["copy"]["base"] for e in ev if e["copy"]["base"] is not None})
        print("frame %d: after the luminance pass %s: %d resolves, bases %s" % (
            n, LUMINANCE_PS[:4], len(ev), ", ".join("0x%08X" % x for x in b) or "none"))
        if len(ev) != 1 or len(b) != 1:
            complete = False
        bases.update(b)
    if frames and complete:
        ring = sorted(bases)
        if len(ring) == 1:
            print("outcome (i): one resolve after the luminance pass in each of %d frames, one base: A = 0x%08X" % (
                len(frames), ring[0]))
        else:
            print("outcome (i), with a ring: one resolve after the luminance pass in each of %d frames, %d bases: %s" % (
                len(frames), len(ring), ", ".join("0x%08X" % a for a in ring)))
            print("  by frame: " + " ".join("%d:%08X" % (n, after_lum[n][0]["copy"]["base"]) for n in frames))
        every = [(x, last) for n in frames for x, last, _ in frame_resolves(cap.frames[n])]
        for a in ring:
            mine = [after_lum[n][0] for n in frames if after_lum[n][0]["copy"]["base"] == a]
            print("  A = 0x%08X in %d frames: %s" % (a, len(mine), describe_event(mine[0])))
            print("  the SDK's forced read-back takes it (PM4_DRAW_INDX_2, rectangles, 3 indices): %s" % (
                "yes" if all(forced_by_sdk(x) for x in mine) else "NO"))
            near = {}
            for x, last in every:
                b = x["copy"]["base"]
                if b is not None and b != a and a <= b < a + FORCE_WINDOW:
                    near.setdefault(b, []).append(last[1][:4] if last else "-")
            for b in sorted(near):
                print("  another resolve within 64 KiB above A: 0x%08X, %d times (after %s)" % (
                    b, len(near[b]), ", ".join(sorted(set(near[b])))))
            if not near:
                print("  no other resolve has its base in [A, A + 64 KiB)")
        return 0
    print("outcome (i) NOT reached: the rows do not name one resolve after the luminance pass in every frame")
    return 1


def report_table(cap, full=False):
    prev = None
    order_ok = order_all = 0
    pair_worst = 0.0
    for n in sorted(cap.frames):
        fr = cap.frames[n]
        comp = (fr.rows.get(COMPOSITE_PS) or [None])[0]
        bright = (fr.rows.get(BRIGHT_PS) or [None])[0]
        if comp is None:
            print("frame %d: no composite row" % n)
            continue
        order_all += 1
        before = fr.frame_line is not None and fr.row_lines[COMPOSITE_PS][0] < fr.frame_line
        order_ok += 1 if before else 0
        c, b = composite_registers(comp), bright_registers(bright) if bright else (None,) * 5
        table, fc, content, why = frame_table(cap, fr)
        product = None if b[4] is None or fr.out_scale is None else b[4] * fr.out_scale
        if product is not None:
            pair_worst = max(pair_worst, abs(product - 1))
        print("frame %d c20.w %s (%s) | 37B1 c31.x %s c46.x %s c47.x %s c77 (%s, %s) c78.x %s | EE92 c31.x %s "
              "c46.x %s c47.x %s c77 (%s, %s) | c77.y * c20.w %s%s | table %s | composite row %s the frame row" % (
                  n, _f(fr.out_scale), fr.out_scale_from or "not found", _f(c[0], 4), _f(c[1], 4), _f(c[2], 3),
                  _f(c[3]), _f(c[4]), _f(c[5]), _f(b[0], 4), _f(b[1], 3), _f(b[2], 3), _f(b[3]), _f(b[4]),
                  _f(product, 6), "" if product is None or abs(product - 1) <= 1e-3 else " (OFF 1 by more than 1e-3)",
                  "0x%016X" % content if content is not None else "-", "before" if before else "NOT before"))
        if table is None:
            print("    no table: %s" % why)
        else:
            if prev is not None:
                print("    change from the previous frame's table: max |d| %.6g" % max(abs(x - y) for x, y in zip(table, prev)))
            prev = table
            lo, hi = min(table), max(table)
            print("    T at L: " + " ".join("%g: %.4f" % (lum, table_at(table, lum)) for lum in LUMINANCES))
            t01, t3 = table_at(table, 0.1), table_at(table, 3)
            print("    min %.6g at %d, max %.6g at %d; T(3) / T(0.1) %s" % (
                lo, table.index(lo), hi, table.index(hi), "%.3f" % (t3 / t01) if t01 else "?"))
            if full:
                print("    " + " ".join("%.5g" % x for x in table))
        bad = block_violations(c, b, table, fc)
        print("    MakePostBlock's rules: %s" % ("all hold" if not bad else "; ".join(bad)))
    print("the composite's row precedes the frame row of the same frame in %d of %d frames" % (order_ok, order_all))
    print("largest |c77.y * c20.w - 1| over the frames: %.3g" % pair_worst)
    return 0


def report_ramp(log_path, pairs_path=None):
    text = Path(log_path).read_text(encoding="utf-8", errors="replace")
    reads = parse_ramp_lines(text)
    changes = [int(x) for x in _RE_RAMP_CHANGES.findall(text)]
    print("%s: %d ramp tables logged (the first read and every change); \"changes\" lines: %d, the last says %s" % (
        Path(log_path).name, len(reads), len(changes), changes[-1] if changes else "-"))
    if "[native] ramp: no reader registered" in text:
        print("the log says no reader was registered: the ramp was not read")
    if not reads:
        return 1
    pairs = [(a, float(b)) for a, b in RAMP_PAIRS]
    extra = parse_pairs(Path(pairs_path).read_text(encoding="utf-8")) if pairs_path else []
    status = 0
    for clay, valid, table in reads:
        print("clay frame %d: valid %d (the rule here: %d)" % (clay, valid, 1 if ramp_valid(table) else 0))
        print("  entries 0, 64, 128, 192, 255: " + ", ".join("%d: (%d, %d, %d)" % ((i,) + table[i]) for i in (0, 64, 128, 192, 255)))
        grey = all(e[0] == e[1] == e[2] for e in table)
        print("  the three channels are %s" % ("equal in all 256 entries" if grey else "NOT equal"))
        worst = 0.0
        for level, shot in pairs:
            got = [ramp_level(v) for v in table[level]]
            d = max(abs(g - shot) for g in got)
            worst = max(worst, d)
            print("  %3d -> %s  (measured %g, largest difference %.2f)" % (level, ", ".join("%.1f" % g for g in got), shot, d))
        print("  criterion 4 (the eight pairs within 1 level): %s (largest difference %.2f)" % (
            "met" if worst <= 1.0 else "NOT met", worst))
        status |= 0 if worst <= 1.0 else 1
        if extra:
            d = [max(abs(ramp_level(v) - shot) for v in table[level]) for level, shot in extra if 0 <= level < 256]
            print("  against the %d pairs of %s: largest difference %.2f, median %.2f" % (
                len(d), Path(pairs_path).name, max(d), median(d)))
    return status


def _scene_of_row(cap, row, slot=0):
    """(floats, w, h, Texture) of the scene colour a row sampled in `slot` (2D k_16_16_16_16_EXPAND, tiled)."""
    fc, content, t = row_texture(cap, row, slot)
    if fc is None or t is None or (content is not None and t.content != content) or not t.path.is_file():
        return None, 0, 0, t
    w, h = (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1
    return ft.untile_scene(t.read(), w, h), w, h, t


def _pick_frame(cap, frame, need):
    for n in sorted(cap.frames):
        if (frame is None or n == frame) and need(cap.frames[n]):
            return cap.frames[n]
    return None


def report_find_luminance(cap, frame=None, threshold=0.99):
    fr = _pick_frame(cap, frame, lambda f: f.memdump is not None and f.rows.get(COMPOSITE_PS))
    if fr is None:
        print("no frame with a memory dump and a composite row (the capture needs --readback_resolve)")
        return 1
    dump = cap.root / fr.memdump["file"]
    if not dump.is_file():
        print("frame %d: the memory dump %s is missing" % (fr.number, dump))
        return 1
    scene, w, h, t = _scene_of_row(cap, fr.rows[COMPOSITE_PS][0])
    if scene is None:
        print("frame %d: the scene colour the composite sampled was not dumped" % fr.number)
        return 1
    mem = dump.read_bytes()
    base = int(fr.memdump["base"], 16)
    print("frame %d: %s (%d bytes from 0x%08X, %d unreadable pages); scene %s %d x %d" % (
        fr.number, dump.name, len(mem), base, fr.memdump.get("unreadable", 0), t.file, w, h))
    status = 1
    for first, label in ((0, "pixel position integral"), (2, "pixel's centre")):
        d = downsample_four(scene, w, h, first)
        ref = [luminance(d[4 * i], d[4 * i + 1], d[4 * i + 2]) for i in range(len(d) // 4)]
        found = find_plane(mem, ref, w // 4, h // 4, threshold)
        print("  reference: lum of the four-texel downsample, %s: %d planes at correlation %.2f or above" % (
            label, len(found), threshold))
        for r, off, name, order in found[:8]:
            print("    0x%08X %s, %s: correlation %.5f" % (base + off, name, order, r))
        if found:
            status = 0
    if status:
        print("outcome (ii) NOT reached: no page-aligned float plane of the dump correlates with the luminance")
    else:
        print("outcome (ii): the best plane above is the luminance resolve, found by content")
    return status


def report_downsample(cap, frame=None):
    """Every frame (or one): the buffer's content is whatever its memory held when the luminance pass's row
    was written, which need not be this frame's downsample (the GPU runs behind the guest, and the memory
    serves other passes); the frame whose buffer lies nearest a form is named at the end."""
    frames = [cap.frames[n] for n in sorted(cap.frames)
              if (frame is None or n == frame) and cap.frames[n].rows.get(DOWNSAMPLE_PS) and cap.frames[n].rows.get(LUMINANCE_PS)]
    if not frames:
        print("no frame with a downsample row and a luminance row")
        return 1
    results = [r for r in (_frame_downsample(cap, fr) for fr in frames) if r is not None]
    if not results:
        return 1
    med, level, name, n = min(results)
    print("nearest over %d frames: frame %d, %s: median difference %.6g against a median level of %.6g%s" % (
        len(results), n, name, med, level,
        "" if level > 0 and med <= 0.05 * level else " (more than 5% of the level: the buffer's memory holds no downsample of this scene)"))
    return 0


def _frame_downsample(cap, fr):
    """Prints one frame's comparison; (smallest median difference, the form's median level, its name, the frame)."""
    scene, w, h, ts = _scene_of_row(cap, fr.rows[DOWNSAMPLE_PS][0])
    fc, content, tb = row_texture(cap, fr.rows[LUMINANCE_PS][0], 0)
    if scene is None:
        print("frame %d: the scene colour the downsample pass sampled was not dumped" % fr.number)
        return None
    if fc is None or tb is None or content is None or tb.content != content or not tb.path.is_file():
        print("frame %d: the downsample buffer the luminance pass sampled was not dumped by content" % fr.number)
        return None
    try:
        bw, bh, texels = ft.decode_2d(tb.read(), fc)
    except ValueError as e:
        print("frame %d: the buffer %s cannot be decoded: %s" % (fr.number, tb.file, e))
        return None
    buf = [c for t in texels for c in t]
    print("frame %d: scene %s %d x %d; buffer %s %d x %d (%s)" % (fr.number, ts.file, w, h, tb.file, bw, bh,
                                                                 ft.describe_fc(fc)))
    if (bw, bh) != (w // 4, h // 4):
        print("  the buffer is not a quarter of the scene")
        return None
    best = None
    for name, d in (("four texels, pixel position integral (texels 0 and 2 of each block)", downsample_four(scene, w, h, 0)),
                    ("four texels, pixel's centre (texels 2 and 4)", downsample_four(scene, w, h, 2)),
                    ("box of sixteen texels", downsample_box(scene, w, h))):
        med, p90, worst, level = colour_difference(buf, d)
        print("  %s: median difference %.6g, 90th percentile %.6g, largest %.6g (median of the form's largest "
              "component %.6g)" % (name, med, p90, worst, level))
        if math.isfinite(med) and (best is None or med < best[0]):
            best = (med, level, name, fr.number)
    return best


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("resolves")
    p.add_argument("capture")
    p.add_argument("--frame", type=int)
    p.add_argument("--all", action="store_true", help="every resolve, not only those after a post rectangle")
    p = sub.add_parser("table")
    p.add_argument("capture")
    p.add_argument("--full", action="store_true")
    p = sub.add_parser("ramp")
    p.add_argument("log")
    p.add_argument("--pairs")
    p = sub.add_parser("find-luminance")
    p.add_argument("capture")
    p.add_argument("--frame", type=int)
    p.add_argument("--threshold", type=float, default=0.99)
    p = sub.add_parser("downsample")
    p.add_argument("capture")
    p.add_argument("--frame", type=int)
    for name in ("resolves", "table", "find-luminance", "downsample"):
        sub.choices[name].add_argument("--tex-root")
    args = ap.parse_args(argv)
    if args.cmd == "ramp":
        return report_ramp(args.log, args.pairs)
    cap = load_capture(args.capture, args.tex_root)
    if args.cmd == "resolves":
        return report_resolves(cap, args.frame, args.all)
    if args.cmd == "table":
        return report_table(cap, args.full)
    if args.cmd == "find-luminance":
        return report_find_luminance(cap, args.frame, args.threshold)
    return report_downsample(cap, args.frame)


if __name__ == "__main__":
    sys.exit(main())
