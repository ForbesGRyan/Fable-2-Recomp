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
  post_tables.py block <capture.jsonl>
        the post block the capture built for each frame (the frame row's "post", written by a build with plan
        Task 4) against the same frame's rows and dump: its registers against "pfull" of the composite's, the
        bright pass's and the blurs' rows bit for bit, its 512 texels against the dump the composite's row
        names, its age, its blur part, its pairing with c20.w (spec criterion 3).
  post_tables.py ramp <game.log> [--pairs <ramp_092959.txt>]
        the ramp lines of a log: the table at the levels of spec criterion 4 against the measured pairs,
        and how often the table changed.
  post_tables.py find-luminance <capture.jsonl> [--frame N] [--threshold 0.99]
        the capture's memory dump (mem_12A00000_f<N>.bin, made under --readback_resolve) searched for the
        luminance picture: lum of the 4 x 4 downsample of the scene colour the frame's composite sampled.
  post_tables.py downsample <capture.jsonl> [--frame N]
        the downsample buffer the luminance pass sampled against the CPU downsample of the scene colour
        the downsample pass sampled, with the pixel position integral and at the pixel's centre.
  post_tables.py graph <capture.jsonl> --frame N [--all] [--no-stats]
        the sun layer and the post chain as a graph (plan Task 10): every render target of the frame after
        the main scene as a node (the rows drawn into it, the colour clears before them, the resolve that
        copies it: base, size, format), each sampled buffer with the node that last resolved to its base and,
        where the capture kept its content (formats 6, 7, 54), what the dump holds (--png <prefix> writes
        each as a picture); then the bright pass's
        tf1 and tf2 traced back to the layer that was drawn, the nodes no later row reads, and the indirect
        buffers called.
  post_tables.py quads <capture.jsonl> [--frame N]
        the furniture of every frame: rows a frame by kind and target, the alpha test (enable bit, function,
        reference) with the frames it holds in, the depth and blend words, the builder each kind is drawn
        through; with --frame every quad of that frame with its corners, registers and index pattern.
  post_tables.py quads <capture.jsonl> --check
        the quad records the capture built for each frame (the frame row's "quads", written by a build with plan
        Task 12 and --fable2_native_sun=on) against the same frame's "indx" and "indxdata" rows: which rows the
        capture reads (kind by target, between the main scene and the composite), then each record's corners,
        matrix, registers, fetch constants and state words bit for bit, its place, and the bound (spec
        criterion 17). The capture must hold every draw (DISC=frames/1). Exit code 1 on any difference.
  post_tables.py occluders <capture.jsonl> --frame N
        the sun layer's cubes against the main scene's draws of the same unit cube and vertex c119.x; its tree
        occluder draws against the main scene's leaf draws of the same buffers, before and after the scene's
        depth copy.
  post_tables.py signs <capture.jsonl> [<capture.jsonl> ...]
        the sign fields and exponent adjusts of every texture bound at every decoded row: what is not all
        zero, and whether the native pass knows it (review-task-8b, Minor 2). Exit code 1 if anything is not.
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
        self.block = None         # "post" of the frame row: the post block the capture built (plan Task 4)
        self.quads_on = False     # "quads" of the frame row says the quads were wanted (plan Task 12): the radial read's gate
        self.has_frame_row = False


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
                fr.has_frame_row = True
                fr.block = r.get("post")
                fr.quads_on = bool((r.get("quads") or {}).get("on"))
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


def blur_registers(row):
    """(c46.x, c46.y, c46.z, c46.w, c47.x, c47.y, c47.z) of a blur row: the centre tap's weight and those of
    the taps 1 to 6 texels away; None where the row gives no register."""
    c46, c47 = _reg(row, 46), _reg(row, 47)
    if c46 is None or c47 is None:
        return None
    return tuple(c46) + tuple(c47[:3])


def radial_registers(row):
    """(c31.x, c31.y, c46.x, c47.x) of a radial blur's row (4BE4): the four values the block takes; None where
    the row gives no register."""
    c31, c46, c47 = _reg(row, 31), _reg(row, 46), _reg(row, 47)
    if c31 is None or c46 is None or c47 is None:
        return None
    return [c31[0], c31[1], c46[0], c47[0]]


def radial_part(values):
    """SetPostRadial's rule for the radial part (post_block.h, kPostPartRadial): c31.x and c31.y finite and in
    [-4, 4], c46.x and c47.x in (0, 1]. values: radial_registers of a row, or None."""
    if values is None or len(values) != 4 or not all(math.isfinite(v) for v in values):
        return False
    return all(-4.0 <= v <= 4.0 for v in values[:2]) and all(0.0 < v <= 1.0 for v in values[2:])


def blur_part(blur_x_row, blur_y_row):
    """MakePostBlock's rule for the blur part (post_block.h, kPostPartBlur): both blurs' rows are there, every
    weight is in [0, 1] and each set's w0 + 2 (w1 + .. + w6) is 1 within 1e-3."""
    for row in (blur_x_row, blur_y_row):
        w = blur_registers(row)
        if w is None or not all(math.isfinite(x) and 0.0 <= x <= 1.0 for x in w):
            return False
        if not abs(w[0] + 2.0 * sum(w[1:]) - 1.0) <= 1e-3:
            return False
    return True


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


def ring_period(bases):
    """The period of a ring of bases, frame by frame: the number of different bases P when every frame's base is
    the base P frames before and every base was seen at least twice (a ring of one needs one frame); None when
    the bases do not repeat so (a base that was seen once shows no period)."""
    if not bases:
        return None
    period = len(set(bases))
    if period == 1:
        return 1
    if len(bases) < 2 * period or any(bases[i] != bases[i - period] for i in range(period, len(bases))):
        return None
    return period


def report_resolves(cap, frame=None, show_all=False):
    after_lum = {}
    counts = {"resolve": 0, "cmdgap": 0, "cmd_ok": 0}
    skipped = {}
    skipped_frames = set()
    for n in sorted(cap.frames):
        if frame is not None and n != frame:
            continue
        fr = cap.frames[n]
        for ev in fr.events:
            if ev[0] == "cmdgap" and ev[2].get("skipped"):
                # A stretch the capture did not look at (over its cap, the pointer gone back, unreadable).
                skipped[ev[2]["skipped"]] = skipped.get(ev[2]["skipped"], 0) + 1
                skipped_frames.add(n)
            elif ev[0] in counts:
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
    if skipped:
        print("stretches not looked at (a resolve issued inside one is in no row): %d (%s), in %d of %d frames" % (
            sum(skipped.values()), ", ".join("%s %d" % (k, skipped[k]) for k in sorted(skipped)),
            len(skipped_frames), len(frames)))
    else:
        print("stretches not looked at (a resolve issued inside one is in no row): none recorded "
              "(a capture made before the \"skipped\" rows existed does not record them)")
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
    period = ring_period([after_lum[n][0]["copy"]["base"] for n in frames]) if frames and complete else None
    if frames and complete and period is None:
        print("after the luminance pass: %d bases in %d frames that do not repeat with a period" % (len(bases), len(frames)))
    if period is not None:
        ring = sorted(bases)
        if len(ring) == 1:
            print("outcome (i): one resolve after the luminance pass in each of %d frames, one base: A = 0x%08X" % (
                len(frames), ring[0]))
        else:
            print("outcome (i), with a ring: one resolve after the luminance pass in each of %d frames, %d bases in turn: %s" % (
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
                print("  no other resolve has its base in [A, A + 64 KiB) among the stretches the capture holds")
        return 0
    print("outcome (i) NOT reached: the rows do not name one resolve after the luminance pass in every frame, "
          "with one base or bases in turn")
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


def _hex9(values):
    return "(" + ", ".join("%.9g" % v for v in values) + ")"


def report_block(cap):
    """The post block the capture built for each frame (the frame row's "post") against the same frame's rows
    and dump. Per frame:
      registers   "composite" (c31.x, c46.x, c47.x, c77.x, c77.y, c78.x) against "pfull" of the composite's row,
                  "bright" (c31.x, c46.x, c47.x, c77.x, c77.y) against the bright pass's, bit for bit;
      blur        "parts" bit 0 against MakePostBlock's rule on the blurs' rows (blur_part), and with the part
                  the 14 weights against those rows;
      radial      "parts" bit 1 and "radial" (c31.x, c31.y, c46.x, c47.x) against the frame's radial blur rows
                  (4BE4) by SetPostRadial's rule: with the part, at least one row, every row the same four
                  values, inside the rule's bounds, and the block's four equal to them bit for bit; without it
                  the four are 0, and the rows must not give the part in a frame whose quads were wanted (the
                  frame row's "quads" says "on": the capture reads the registers only then);
      table       the (address, content) pair the block read against the composite's row's slot 1 ("tf",
                  "tfc"), and the 512 texels against decode_table of the dump with that content;
      age         0: the frame's own block (ruling 10);
      pairing     the block's bright c77.y times the frame's c20.w is 1 within 1e-3.
    Returns 0 when every frame has a valid block and nothing differs or is missing, else 1."""
    print("capture %s: %d frames" % (cap.path.name, len(cap.frames)))
    good = 0
    for n in sorted(cap.frames):
        fr = cap.frames[n]
        b = fr.block
        if b is None:
            print("frame %d: no post block in the frame row (%s)" % (
                n, "a build from before the post block, or a frame without records" if fr.has_frame_row
                else "the capture has no row of this frame's end"))
            continue
        faults = []
        radial_text = "radial not checked"
        comp = (fr.rows.get(COMPOSITE_PS) or [None])[0]
        bright = (fr.rows.get(BRIGHT_PS) or [None])[0]
        bx = fr.rows.get(BLUR_X_PS) or []
        by = fr.rows.get(BLUR_Y_PS) or []
        product = None
        if not b.get("valid"):
            faults.append("block invalid(%s), on %s, tries %s" % (b.get("reason"), b.get("on"), b.get("tries")))
        elif comp is None or bright is None:
            faults.append("no %s row to hold the block against" % ("composite" if comp is None else "bright-pass"))
        else:
            if b.get("age") != 0:
                faults.append("age %s: not the frame's own block" % b.get("age"))
            for key, want in (("composite", composite_registers(comp)), ("bright", bright_registers(bright))):
                got = b.get(key, [])
                if any(v is None for v in want):
                    faults.append("%s: the row has no value" % key)
                elif not ft._same_floats(got, list(want)):
                    faults.append("%s: block %s, row %s" % (key, _hex9(got), _hex9(want)))
            part = blur_part(bx[0] if bx else None, by[0] if by else None)
            has_part = bool(b.get("parts", 0) & 1)
            if has_part and not part:
                faults.append("parts %s, but the blurs' rows do not give the part (%d and %d blur rows)" % (
                    b.get("parts"), len(bx), len(by)))
            elif part and not has_part:
                faults.append("parts %s, but the blurs' rows give the part by MakePostBlock's rule" % b.get("parts"))
            want_blur = list(blur_registers(bx[0]) + blur_registers(by[0])) if part else [0.0] * 14
            if not ft._same_floats(b.get("blur", []), want_blur):
                faults.append("blur: block %s, rows %s" % (_hex9(b.get("blur", [])), _hex9(want_blur)))
            radial_rows = [radial_registers(r) for r in fr.rows.get(RADIAL_PS) or []]
            got_radial = list(b.get("radial") or [0.0] * 4)
            rule = bool(radial_rows) and all(radial_part(v) for v in radial_rows) \
                and all(ft._same_floats(v, radial_rows[0]) for v in radial_rows)
            if b.get("parts", 0) & 2:
                if not radial_rows:
                    faults.append("radial: parts %s, but the frame has no 4BE4 row" % b.get("parts"))
                elif any(v is None for v in radial_rows):
                    faults.append("radial: a 4BE4 row has no value")
                elif not all(ft._same_floats(v, radial_rows[0]) for v in radial_rows):
                    faults.append("radial: the frame's %d 4BE4 rows differ: %s" % (
                        len(radial_rows), ", ".join(_hex9(v) for v in radial_rows)))
                elif not rule:
                    faults.append("radial: the rows %s fail SetPostRadial's rule, the block has the part" % _hex9(radial_rows[0]))
                elif not ft._same_floats(got_radial, radial_rows[0]):
                    faults.append("radial: block %s, rows %s" % (_hex9(got_radial), _hex9(radial_rows[0])))
                else:
                    radial_text = "radial %s equal to %s 4BE4 rows" % (
                        _hex9(got_radial), "both" if len(radial_rows) == 2 else "the %d" % len(radial_rows))
            else:
                if any(v != 0.0 for v in got_radial):
                    faults.append("radial: parts %s, but the block holds %s" % (b.get("parts"), _hex9(got_radial)))
                elif fr.quads_on and rule:
                    faults.append("radial: parts %s, but the 4BE4 rows give the part by SetPostRadial's rule in a frame whose "
                                  "quads were wanted" % b.get("parts"))
                else:
                    radial_text = "no radial part (%s)" % (
                        "the quads were not wanted" if not fr.quads_on else "%d 4BE4 rows that do not give it" % len(radial_rows))
            src = b.get("table") or {}
            pair = (int(src.get("base", "0"), 16), int(src.get("content", "0"), 16))
            table, fc, content, why = frame_table(cap, fr)
            row_pair = (fc[1] & 0xFFFFF000 if fc else 0, content or 0)
            if pair != row_pair:
                faults.append("table: the block read 0x%08X / %016X, the composite's row names 0x%08X / %016X" % (
                    pair + row_pair))
            got = b.get("texels", [])
            if table is None:
                faults.append("table: no dump to decode (%s)" % why)
            elif not ft._same_floats(got, table):
                bad = [i for i in range(min(len(got), len(table))) if ft._f32(got[i]) != ft._f32(table[i])]
                faults.append("table: %d texels, the dump %d; %d differ%s" % (
                    len(got), len(table), len(bad),
                    " (the first: texel %d, block %.9g, dump %.9g)" % (bad[0], got[bad[0]], table[bad[0]]) if bad else ""))
            c77y = (b.get("bright") or [None] * 5)[4]
            if fr.out_scale is None or c77y is None:
                faults.append("pairing: no c20.w of the frame to hold c77.y against")
            else:
                product = c77y * fr.out_scale
                if not abs(product - 1.0) <= 1e-3:
                    faults.append("pairing: c77.y * c20.w is %.6f (c20.w %s from %s)" % (
                        product, _f(fr.out_scale), fr.out_scale_from))
        if not faults:
            good += 1
            src = b["table"]
            print("frame %d: post block equal to the rows: age 0, 6 + 5 registers, %s, table %s / %s, 512 texels equal "
                  "to the dump's, c77.y * c20.w %.6f; %s" % (
                      n, "14 blur weights (parts %d)" % b["parts"] if b.get("parts", 0) & 1
                      else "no blur weights (parts %d: %d and %d blur rows)" % (b.get("parts", 0), len(bx), len(by)),
                      src["base"], src["content"][2:], product, radial_text))
        for text in faults:
            print("frame %d: POST %s" % (n, text))
    print("post block valid, at age 0 and equal to the frame's rows and dump in %d of %d frames" % (good, len(cap.frames)))
    return 0 if good == len(cap.frames) and good else 1


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


# --- the sun layer: its graph, its quads, its occluders (plan Task 10) ------------------------------------------

PM4_INDIRECT_BUFFER = 0x3F
REG_SURFACE_INFO, REG_COLOR_INFO = 0x2000, 0x2001
PACKED10_FORMATS = (7, 54)   # k_2_10_10_10 and k_2_10_10_10_AS_16_16_16_16: one storage, 10 bits a channel
QUAD_BUILDER = "DrawIndx:82217EE8"
LEAF_PS, TREE_OCCLUDER_PS = "7CD5", "F1EB"
CUBE_VS = "D1CC6DC28244C55A"
SCENE_DEPTH_BASE = 0x1A02A000


def fetch_texture(fc):
    """What a texture fetch constant (six dwords) names: base, width, height (1 for a 1D texture), format,
    tiled, the four sign fields, the exponent adjust."""
    two_d = ((fc[5] >> 9) & 3) == 1
    return {
        "base": fc[1] & 0xFFFFF000, "format": fc[1] & 0x3F, "tiled": bool(fc[0] >> 31),
        "width": ((fc[2] & 0x1FFF) if two_d else (fc[2] & 0xFFFFFF)) + 1,
        "height": ((fc[2] >> 13) & 0x1FFF) + 1 if two_d else 1,
        "signs": tuple(ft.sign_fields(fc)), "exp": ft.exp_adjust(fc),
    }


def packed10(word):
    """(r, g, b) of a k_2_10_10_10 texel: red in the low ten bits, green and blue above it, each over 1023."""
    return (word & 0x3FF) / 1023.0, ((word >> 10) & 0x3FF) / 1023.0, ((word >> 20) & 0x3FF) / 1023.0


def decode_packed10(data, fc):
    """(width, height, [(r, g, b)] row-major) of a 2D texture of format 7 or 54 (the sun layer's and the bloom's
    buffers as the rows that sample them name them)."""
    t = fetch_texture(fc)
    if t["format"] not in PACKED10_FORMATS or ((fc[5] >> 9) & 3) != 1:
        raise ValueError("format %d is not a 2D k_2_10_10_10" % t["format"])
    w, h = t["width"], t["height"]
    pitch = max(((fc[0] >> 22) & 0x1FF) << 5, w)
    row_bytes = ((pitch + 31) & ~31) * 4
    endian = (fc[1] >> 6) & 3
    order = ">I" if endian == 2 else "<I"
    if endian not in (0, 2):
        raise ValueError("endian %d" % endian)
    out = []
    for y in range(h):
        for x in range(w):
            o = tiled_offset_2d(x, y, pitch, 2) if t["tiled"] else y * row_bytes + x * 4
            if o + 4 > len(data):
                raise ValueError("texel (%d, %d) at %d lies outside the %d bytes" % (x, y, o, len(data)))
            out.append(packed10(struct.unpack_from(order, data, o)[0]))
    return w, h, out


def stream_steps(dwords):
    """What a run of command dwords does to a render target, in order: every draw as {"type": "resolve"} (with
    the copy's base, pitch, height, format, source) or {"type": "draw"} ("mode" RB_MODECONTROL's low bits as
    the run left them, 4 colour and depth, 5 depth only; "colour_format" of RB_COLOR_INFO and "surface"
    RB_SURFACE_INFO as the run left them; None where the run wrote none; in a "resolve" row, the clear
    routine's, such a draw is a clear rectangle; in a "cmdgap" row it is a hooked draw's own packet), and
    every PM4_INDIRECT_BUFFER call as {"type": "indirect", "address", "dwords"}: its packets are not in the
    row."""
    regs = {}
    out = []
    draws = {e["at"]: e for e in draw_events(dwords)}
    for p in pm4_packets(dwords):
        for reg, value in register_writes(p):
            regs[reg] = value
        if p["type"] != 3:
            continue
        if p["opcode"] == PM4_INDIRECT_BUFFER and len(p["data"]) >= 2:
            out.append({"type": "indirect", "address": p["data"][0], "dwords": p["data"][1] & 0xFFFFF})
        e = draws.get(p["at"])
        if e is None:
            continue
        if e["resolve"]:
            c = e["copy"]
            out.append({"type": "resolve", "base": c["base"], "pitch": c.get("pitch"), "height": c.get("height"),
                        "format": c.get("format"), "source": c.get("src_select")})
        else:
            mode, colour = regs.get(REG_MODECONTROL), regs.get(REG_COLOR_INFO)
            out.append({"type": "draw", "mode": None if mode is None else mode & 7,
                        "colour_format": None if colour is None else (colour >> 16) & 0xF,
                        "surface": regs.get(REG_SURFACE_INFO)})
    return out


def build_graph(events):
    """The frame's render targets as nodes, from its events in row order: {"type": "row", "line", "name",
    "pitch", "in_scene", "reads": [(slot, fetch_texture)]}, {"type": "resolve", "line", "base", "pitch", "height",
    "format", "source"}, {"type": "clear", "line", "mode", "colour_format"}.
    A node is what one resolve copies: {"index", "draws" (the rows outside the main scene since the resolve
    before, or since the last colour clear after it), "earlier" (the rows before that clear: they are not in
    the colour picture; depth, or another target), "scene_rows" (a count of the main scene's), "clears" (the
    clear rectangles that write colour, mode 4), "again" (the node whose picture this resolve copies once more:
    nothing drawn and no colour cleared since), "dest" (the resolve; None for the rows after the last one),
    "reads": [(row name, slot, texture, the node that last resolved to that base or None, a note or None)] of
    "draws", "earlier_reads" the same of "earlier"}.
    The guest's order of issue is the GPU's order of execution, so "the last resolve to a base before the
    row" is what the row samples."""
    nodes = []
    writer = {}      # base -> node index of the last resolve there
    cur = None
    last_colour = None   # the node that holds the colour target's picture as it stands

    def fresh():
        return {"index": len(nodes), "draws": [], "earlier": [], "scene_rows": 0, "clears": [], "again": None, "dest": None,
                "reads": [], "earlier_reads": []}

    for e in events:
        if cur is None:
            cur = fresh()
        if e["type"] == "row":
            if e.get("in_scene"):
                cur["scene_rows"] += 1
            else:
                cur["draws"].append(e)
            for slot, t in e.get("reads", ()):
                producer = writer.get(t["base"])
                note = None
                if producer is not None:
                    d = nodes[producer]["dest"]
                    if d["height"] != t["height"] or d["pitch"] != ((t["width"] + 31) & ~31):
                        note = "the last resolve there is %d x %d, the row reads %d x %d" % (
                            d["pitch"], d["height"], t["width"], t["height"])
                if not e.get("in_scene"):
                    cur["reads"].append((e["name"], slot, t, producer, note))
        elif e["type"] == "clear":
            if e.get("mode") == 5:
                continue    # depth only: the colour stands
            # A colour clear after draws without a resolve between: those draws are not in the colour picture.
            cur["earlier"] += cur["draws"]
            cur["earlier_reads"] += cur["reads"]
            cur["draws"], cur["reads"] = [], []
            cur["clears"].append(e)
        elif e["type"] == "resolve":
            cur["dest"] = e
            if not (cur["draws"] or cur["earlier"] or cur["scene_rows"] or cur["clears"]):
                cur["again"] = last_colour
            nodes.append(cur)
            if e.get("base") is not None:
                writer[e["base"]] = cur["index"]
            if cur["again"] is None:
                last_colour = cur["index"]
            cur = None
    if cur is not None and (cur["draws"] or cur["earlier"] or cur["scene_rows"]):
        nodes.append(cur)
    return nodes


def node_kind(node):
    """"drawn" (rows since the resolve before), "cleared" (colour clears alone), "again" (neither: the picture
    of the resolve before, copied once more)."""
    if node["draws"] or (node["scene_rows"] and not node["clears"]):
        return "drawn"
    return "cleared" if node["clears"] else "again"


def node_name(node):
    kind = node_kind(node)
    if kind == "cleared":
        return "(cleared, nothing drawn)"
    if kind == "again":
        return "(the picture of node %s again)" % node["again"]
    names = []
    for d in node["draws"]:
        if not names or names[-1] != d["name"]:
            names.append(d["name"])
    if node["scene_rows"] and not node["clears"]:
        names.insert(0, "the main scene (%d rows)" % node["scene_rows"])
    return " + ".join(names)


def chain_names(nodes, index, limit=16):
    """The names of a node and of what it was made from, following each node's first read that a node of the
    frame produced (a reduction, a blur: one input), down to a node that reads none."""
    out = []
    seen = set()
    while index is not None and index not in seen and len(out) < limit:
        seen.add(index)
        node = nodes[index]
        if node_kind(node) == "again" and node["again"] is not None:
            index = node["again"]
            continue
        out.append(node_name(node))
        index = next((producer for _, _, _, producer, _ in node["reads"] if producer is not None), None)
    return out


def unread_nodes(nodes):
    """The nodes with a destination that no later row of the frame reads before the base is resolved to again
    (by base alone: what a later frame or the CPU reads is not in the rows)."""
    read = {producer for n in nodes for _, _, _, producer, _ in n["reads"] if producer is not None}
    again = {n["again"] for n in nodes if n["again"] is not None}
    return [n["index"] for n in nodes if n["dest"] is not None and n["index"] not in read and n["index"] not in again]


def _row_name(ps, vs, roles):
    if ps in POST_PS:
        return POST_PS[ps]
    return roles.get(ps) or roles.get(vs) or ("ps %s" % ps[:4] if ps else "vs %s" % vs[:4])


def _furniture_roles():
    import light_formulas as lf
    roles = {s.hash: s.role for s in lf.POST_FURNITURE_SHADERS}
    roles.update({s.hash: s.role for s in lf.POST_SHADERS})
    return roles


def _row_hashes(r):
    ps = (r.get("ps_hash") or "0x")[2:].upper()
    vs = (r.get("vs_hash") or (r.get("vs") or {}).get("hash") or (r.get("vs") or {}).get("object_hash") or "0x")[2:].upper()
    return ps, vs


def _rs(r, reg, default=None):
    v = (r.get("rs") or {}).get("%04X" % reg)
    return default if v is None else int(v, 16)


def frame_rows(path, frame, kinds=("draw", "tess", "verts", "indx", "resolve", "cmdgap", "frame")):
    """(line number, parsed row) of one frame's rows of the kinds named, in the file's order."""
    mark = '"frame":%d,' % frame
    with open(path, encoding="utf-8") as f:
        for line_no, line in enumerate(f):
            head = line[:48]
            if not head.startswith('{"kind":"') or mark not in head:
                continue
            if head[9:head.find('"', 9)] in kinds:
                yield line_no, json.loads(line)


def graph_events(path, frame):
    """build_graph's events of one frame of a capture, and the rows by line."""
    roles = _furniture_roles()
    events, rows = [], {}
    for line_no, r in frame_rows(path, frame):
        kind = r["kind"]
        if kind == "frame":
            break
        if kind in ("resolve", "cmdgap"):
            if r.get("skipped"):
                # A stretch of the command stream the capture did not look at: a resolve inside it is in no row.
                events.append({"type": "skipped", "line": line_no, "why": r["skipped"]})
            if r.get("cmd"):
                for step in stream_steps(hex_dwords(r["cmd"])):
                    if step["type"] == "draw":
                        if kind != "resolve":
                            continue    # a hooked draw's own packet: the draw has its row
                        step["type"] = "clear"
                    step["line"] = line_no
                    step["n"] = r.get("n")
                    events.append(step)
            continue
        ps, vs = _row_hashes(r)
        reads = []
        for slot, fc in sorted((r.get("tf") or {}).items(), key=lambda kv: int(kv[0])):
            t = fetch_texture([int(x, 16) for x in fc])
            content = (r.get("tfc") or {}).get(slot)
            t["content"] = int(content, 16) if content else None
            t["fc"] = [int(x, 16) for x in fc]
            reads.append((int(slot), t))
        rows[line_no] = r
        events.append({"type": "row", "line": line_no, "name": _row_name(ps, vs, roles), "ps": ps, "vs": vs,
                       "pitch": int(r.get("surface_info", "0x0"), 16) & 0x3FFF, "in_scene": bool(r.get("in_scene")),
                       "rt_format": (_rs(r, 0x2001, 0) >> 16) & 0xF, "mask": _rs(r, 0x2104, 0xF) & 0xF, "reads": reads})
    return events, rows


def _picture_stats(cap, t, png=None):
    """A line on a small buffer's dump (formats 6, 7, 54), or why there is none; with `png` (a path prefix) the
    dump is also written as <prefix>_<base>_<width>x<height>_f<format>_<content>.png."""
    if t.get("content") is None:
        return "no content kept"
    tex = cap.by_content.get((t["base"], t["content"]))
    if tex is None or not tex.path.is_file():
        return "content %016X: no dump" % t["content"]
    try:
        if t["format"] in PACKED10_FORMATS:
            _, _, texels = decode_packed10(tex.read(), t["fc"])
        elif t["format"] == 6:
            _, _, texels = ft.decode_2d(tex.read(), t["fc"])
        else:
            return "content %016X (format %d: not decoded here)" % (t["content"], t["format"])
    except ValueError as e:
        return "content %016X: %s" % (t["content"], e)
    n = len(texels)
    if png:
        import post_sun_layer as sl
        w = t["width"]
        sl._rows_png("%s_%08X_%dx%d_f%d_%016X.png" % (png, t["base"], w, t["height"], t["format"], t["content"]),
                     [[tuple(q[:3]) for q in texels[y * w:(y + 1) * w]] for y in range(t["height"])])
    lum = [luminance(p[0], p[1], p[2]) for p in texels]
    lit = sum(1 for v in lum if v > 0.0)
    return "content %016X: mean (%.4f, %.4f, %.4f), largest luminance %.4f, %d of %d texels above 0" % (
        t["content"], sum(p[0] for p in texels) / n, sum(p[1] for p in texels) / n, sum(p[2] for p in texels) / n,
        max(lum), lit, n)


def _dest_text(d):
    if d is None:
        return "no resolve follows in the frame's rows"
    return "-> 0x%08X %s x %s format %s%s" % (
        d["base"] or 0, d.get("pitch"), d.get("height"), d.get("format"), " (depth)" if d.get("source") == 4 else "")


def report_graph(path, frame, tex_root=None, stats=True, all_nodes=False, png=None):
    """Every pass of the frame after the main scene with its real destination (the resolve rows), what it reads
    and which pass wrote that; then the bright pass's tf1 and tf2 traced back."""
    events, _ = graph_events(path, frame)
    if not events:
        print("frame %d: no rows" % frame)
        return 1
    nodes = build_graph(events)
    cap = load_capture(path, tex_root) if stats else None
    resolves = sum(1 for e in events if e["type"] == "resolve")
    print("%s frame %d: %d rows, %d resolves, %d nodes" % (Path(path).name, frame, sum(1 for e in events if e["type"] == "row"),
                                                          resolves, len(nodes)))
    if not resolves:
        print("no resolve in the frame's rows: a capture made before the \"cmdgap\" rows (plan Task 1) cannot give the graph")
        return 1
    last_scene = max((n["index"] for n in nodes if n["scene_rows"]), default=-1)
    first_line = min((d["line"] for n in nodes if n["index"] > last_scene for d in n["draws"] + n["earlier"]), default=0)
    skipped = [e for e in events if e["type"] == "skipped" and e["line"] >= first_line]
    if skipped:
        print("%d stretches of the command stream after the main scene were not looked at (lines %s): a resolve inside one "
              "is in no row, and a node below may then name the wrong writer" % (
                  len(skipped), ", ".join("%d (%s)" % (e["line"], e["why"]) for e in skipped[:8])))
    indirect = {}
    for e in events:
        if e["type"] == "indirect":
            indirect[(e["address"], e["dwords"])] = indirect.get((e["address"], e["dwords"]), 0) + 1
    for n in nodes:
        if not all_nodes and n["index"] <= last_scene:
            continue
        pitches = sorted({d["pitch"] for d in n["draws"]})
        formats = sorted({d["rt_format"] for d in n["draws"] if "rt_format" in d})
        masks = sorted({d["mask"] for d in n["draws"] if "mask" in d})
        print("node %d: %s%s %s" % (
            n["index"], node_name(n),
            " | target pitch %s, colour format %s, colour mask %s" % (
                "/".join(map(str, pitches)), "/".join(map(str, formats)), "/".join("%X" % m for m in masks)) if n["draws"] else "",
            _dest_text(n["dest"])))
        if n["clears"]:
            print("    colour clears: %d (colour format %s)" % (
                len(n["clears"]), "/".join(str(c.get("colour_format")) for c in n["clears"])))
        if n["earlier"]:
            names = []
            for d in n["earlier"]:
                if not names or names[-1] != d["name"]:
                    names.append(d["name"])
            print("    drawn before the last colour clear (not in the colour picture: depth, or another target): %s" % " + ".join(names))
        for name, slot, t, producer, note in n["reads"]:
            if producer is None and t["base"] not in {m["dest"]["base"] for m in nodes if m["dest"]}:
                continue    # a texture no resolve of the frame writes: an asset
            print("    %s tf%d reads 0x%08X %d x %d format %d: %s%s%s" % (
                name, slot, t["base"], t["width"], t["height"], t["format"],
                "written by node %d (%s)" % (producer, node_name(nodes[producer])) if producer is not None
                else "resolved to later in the frame only (an earlier frame's)",
                "; " + note if note else "", "; " + _picture_stats(cap, t, png) if cap else ""))
    status = 0
    bright = next((n for n in nodes if any(d.get("ps") == BRIGHT_PS for d in n["draws"])), None)
    print()
    if cap:
        print("(a content is the base's memory when the row was issued on the guest's thread: the GPU's thread may not have run")
        print(" this frame's resolves by then, and several of these bases are resolved to more than once a frame; so a dump")
        print(" is some earlier state of the base, not what the row's draw sampled. The nodes are by address, not by content.)")
    if bright is None:
        print("no bright pass row (%s) in the frame" % BRIGHT_PS[:4])
        status = 1
    else:
        for name, slot, t, producer, note in bright["reads"]:
            if name == "bright" and slot in (1, 2):
                chain = chain_names(nodes, producer) if producer is not None else ["(no node of the frame)"]
                print("the bright pass's tf%d (0x%08X) <- %s" % (slot, t["base"], " <- ".join(chain)))
                if producer is None or note:
                    status = 1
    for i in unread_nodes(nodes):
        if i > last_scene or all_nodes:
            d = nodes[i]["dest"]
            print("read by no later row of the frame: node %d, %s, 0x%08X %s x %s format %s" % (
                i, " <- ".join(chain_names(nodes, i)), d["base"] or 0, d.get("pitch"), d.get("height"), d.get("format")))
    for (address, dwords), count in sorted(indirect.items()):
        print("indirect buffer 0x%08X, %d dwords: called %d times in the frame's rows (its packets are not in a row)" % (
            address, dwords, count))
    return status


# --- the furniture's census -----------------------------------------------------------------------------------

def furniture_census(rows):
    """Counts over rows {"frame", "role", "kind", "func", "pitch", "in_scene", "depth", "blend", "mask",
    "colorcontrol" (register 0x2202), "alpha_ref" (register 0x210E), "vertices", "indices"}:
      "counts"   (role, pitch, in scene) -> {frame: rows}
      "alpha"    (role, pitch, in scene) -> {(test on, function, reference word or None when off): [frames]}
      "words"    (role, pitch, in scene) -> {(depth word, blend word, colour mask)}
      "builders" role -> {(row kind, hooked function): rows}
      "shapes"   role -> {(vertices, indices): rows}"""
    out = {"counts": {}, "alpha": {}, "words": {}, "builders": {}, "shapes": {}}
    for r in rows:
        key = (r["role"], r["pitch"], bool(r["in_scene"]))
        frames = out["counts"].setdefault(key, {})
        frames[r["frame"]] = frames.get(r["frame"], 0) + 1
        on = bool(r["colorcontrol"] & 8)
        state = (on, r["colorcontrol"] & 7, r["alpha_ref"] if on else None)
        seen = out["alpha"].setdefault(key, {}).setdefault(state, [])
        if r["frame"] not in seen:
            seen.append(r["frame"])
        out["words"].setdefault(key, set()).add((r["depth"], r["blend"], r["mask"]))
        b = out["builders"].setdefault(r["role"], {})
        b[(r["kind"], r["func"])] = b.get((r["kind"], r["func"]), 0) + 1
        s = out["shapes"].setdefault(r["role"], {})
        s[(r.get("vertices"), r.get("indices"))] = s.get((r.get("vertices"), r.get("indices")), 0) + 1
    return out


_RE_KIND_FRAME = re.compile(r'^\{"kind":"(draw|tess|verts|indx)","frame":(\d+),')


def furniture_rows(path):
    """furniture_census's rows of a whole capture: every decoded row whose pixel shader is a furniture shader, or
    whose vertex shader is the cubes'."""
    roles = _furniture_roles()
    furniture = {h: role for h, role in roles.items() if h not in POST_PS and role != "reduce"}
    marks = ['"0x%s"' % h for h in furniture]
    out = []
    with open(path, encoding="utf-8") as f:
        for line_no, line in enumerate(f):
            m = _RE_KIND_FRAME.match(line[:48])
            if not m or not any(k in line for k in marks):
                continue
            r = json.loads(line)
            ps, vs = _row_hashes(r)
            role = furniture.get(ps) or (furniture.get(vs) if vs == CUBE_VS else None)
            if role is None:
                continue
            out.append({"frame": int(m.group(2)), "role": role, "kind": m.group(1), "func": r.get("func", ""), "line": line_no,
                        "pitch": int(r.get("surface_info", "0x0"), 16) & 0x3FFF, "in_scene": bool(r.get("in_scene")),
                        "depth": _rs(r, 0x2200, 0), "blend": _rs(r, 0x2201, 0), "mask": _rs(r, 0x2104, 0xF) & 0xF,
                        "colorcontrol": _rs(r, 0x2202, 0), "alpha_ref": _rs(r, 0x210E, 0),
                        "vertices": r.get("vertices"),
                        "indices": r.get("indices") if m.group(1) == "indx" else (r.get("ib") or {}).get("count")})
    return out


def _word_float(word):
    return struct.unpack(">f", struct.pack(">I", word & 0xFFFFFFFF))[0]


def report_quads(path, frame=None):
    """The furniture of a capture: per kind and target the rows a frame, the alpha test, the state words, the
    builder; then every quad of one frame with its corners and registers (post_sun_layer's reading)."""
    rows = furniture_rows(path)
    if not rows:
        print("%s: no furniture row" % Path(path).name)
        return 1
    c = furniture_census(rows)
    frames = sorted({r["frame"] for r in rows})
    print("%s: %d furniture rows in frames %d to %d" % (Path(path).name, len(rows), frames[0], frames[-1]))
    print()
    print("rows a frame, by kind and target (pitch; in the main scene or not):")
    for key in sorted(c["counts"], key=lambda k: (k[0], -k[1])):
        per = c["counts"][key]
        counts = [per.get(n, 0) for n in frames]
        alpha = c["alpha"][key]
        print("  %-14s pitch %4d %-9s %s a frame%s | alpha test %s | depth, blend, mask %s" % (
            key[0], key[1], "scene" if key[2] else "outside",
            str(counts[0]) if len(set(counts)) == 1 else "%d to %d (%s)" % (min(counts), max(counts), " ".join(map(str, counts))),
            "" if len(set(counts)) == 1 else " VARIES",
            "; ".join("%s in %d frames" % (
                "greater than %.4f (0x%08X)" % (_word_float(ref), ref) if on and func == 4 else
                "function %d against %.4f" % (func, _word_float(ref)) if on else "off", len(fr))
                for (on, func, ref), fr in sorted(alpha.items(), key=str)),
            ", ".join("0x%08X 0x%08X %X" % w for w in sorted(c["words"][key]))))
    print()
    status = 0
    for role in sorted(c["builders"]):
        b = c["builders"][role]
        shapes = ", ".join("%s vertices %s indices x %d" % (v, i, n) for (v, i), n in sorted(c["shapes"][role].items(), key=str))
        print("  %-14s drawn through %s | %s" % (role, ", ".join("%s %s x %d" % (k, f, n) for (k, f), n in sorted(b.items())), shapes))
    quad_roles = {"moon", "cloud", "glare", "beam-source", "flare", "moon-glow", "sun-disc", "cloud-occluder", "layer-depth",
                  "previous-frame"}
    other = sorted({(role, k, f) for role, b in c["builders"].items() if role in quad_roles for (k, f) in b if (k, f) != ("indx", QUAD_BUILDER)})
    print("every quad kind through the indexed builder %s alone: %s" % (
        QUAD_BUILDER, "yes" if not other else "NO: " + ", ".join("%s as %s %s" % o for o in other)))
    if frame is None:
        return status
    import post_sun_layer as sl
    print()
    print("frame %d, every quad (corners x y z u v; the registers POST_QUAD_SHADERS names):" % frame)
    draws, _ = sl._furniture_rows(path, frame)
    pattern = set()
    for d in draws:
        if d.role is None:
            continue    # a row that only names a furniture shader beside its own (a HUD quad's alternative hash)
        print("  n %-4s %s" % (d.row.get("n"), sl.describe(d)))
        print("         " + " ".join("(" + ", ".join("%.6g" % x for x in v) + ")" for v in d.vertices[:8]))
        if len(d.vertices) == 4:
            pattern.add(tuple(d.indices))
    print("index pattern of the 4-vertex quads: %s" % ", ".join(" ".join(map(str, p)) for p in sorted(pattern)))
    return status


# --- quads --check: the capture's quad records against the frame's rows (spec criterion 17) --------------------

MAX_QUAD_RECORDS = 24                   # quad_record.h, kMaxQuadRecords
QUAD_RECORD_REGISTERS, QUAD_RECORD_SLOTS = 12, 3


def _quad_table():
    """pixel shader hash -> light_formulas.PostQuadShader, for the kinds the capture records."""
    import light_formulas as lf
    return {q.hash: q for q in lf.POST_QUAD_SHADERS}


def _pitch(r):
    return int(r.get("surface_info", "0x0"), 16) & 0x3FFF


def quad_rows_expected(rows):
    """The rows of one frame, in the file's order, that the capture makes a quad record of (capture.cpp,
    ObserveQuadDraw; quad_record.h, QuadTargetMatches): calls of the indexed builder whose pixel shader is in
    POST_QUAD_SHADERS, a scene kind inside the main scene, a layer kind outside it at a target of half the
    scene's pitch (the pitch of the frame's first main-scene row of the builder), after that row and before the
    frame's composite. rows: the frame's decoded "verts" and "indx" rows."""
    table = _quad_table()
    out = []
    scene_pitch = 0
    for r in rows:
        ps, _ = _row_hashes(r)
        if r.get("kind") == "verts":
            if ps == COMPOSITE_PS and not r.get("in_scene"):
                break
            continue
        if r.get("kind") != "indx" or r.get("func") != QUAD_BUILDER:
            continue
        in_scene = bool(r.get("in_scene"))
        if in_scene and not scene_pitch:
            scene_pitch = _pitch(r)
        q = table.get(ps)
        if q is None:
            continue
        if q.where == "scene":
            if in_scene:
                out.append(r)
        elif not in_scene and scene_pitch and _pitch(r) * 2 == scene_pitch:
            out.append(r)
    return out


def _bank_bits(values):
    return "".join("%08X" % struct.unpack(">I", struct.pack(">f", v))[0] for v in values)


def quad_record_faults(record, row):
    """What differs between one record of a frame row's "quads" and the "indx" row of its draw (with the draw's
    "indxdata" row under "data"), as texts; [] when nothing does. Floats are compared by their bits."""
    table = _quad_table()
    ps, vs = _row_hashes(row)
    q = table[ps]
    faults = []
    if record.get("kind") != q.kind:
        return ["kind %s, the row's shader is %s" % (record.get("kind"), q.kind)]
    layer = 0 if q.where == "scene" else 1
    if record.get("layer") != layer:
        faults.append("layer %s, the kind's is %d" % (record.get("layer"), layer))
    for key, want in (("ps", ps), ("vs", vs)):
        got = (record.get(key) or "0x")[2:].upper()
        if got != want:
            faults.append("%s %s, the row's %s" % (key, got, want))
    data = row.get("data")
    if not data or "verts" not in data or "idx" not in data:
        faults.append("the row has no \"indxdata\" to hold the corners against")
    else:
        if list(data["idx"]) != [0, 1, 2, 1, 3, 2]:
            faults.append("indices %s in the row's data: no quad of the pattern 0 1 2 1 3 2" % list(data["idx"]))
        if data.get("stride") != 20 or data.get("vbytes") != 80:
            faults.append("the row's data is %s bytes at stride %s" % (data.get("vbytes"), data.get("stride")))
        if (record.get("corners") or "").upper() != data["verts"].upper():
            faults.append("corners %s, the row's data %s" % (record.get("corners"), data["verts"]))
    bank = row.get("bank") or []
    if len(bank) < 16:
        faults.append("the row has no vertex bank to hold view_proj against")
    elif (record.get("view_proj") or "").upper() != _bank_bits(bank[:16]):
        faults.append("view_proj %s, the row's c0..c3 %s" % (record.get("view_proj"), _bank_bits(bank[:16])))
    regs = (record.get("regs") or "").upper()
    pf = row.get("pfull") or ""
    if len(regs) != 32 * QUAD_RECORD_REGISTERS:
        faults.append("regs: %d hex digits, not %d" % (len(regs), 32 * QUAD_RECORD_REGISTERS))
    else:
        for i, reg in enumerate(q.registers):
            want = pf[32 * reg:32 * reg + 32].upper()
            if len(want) != 32 or "-" in want:
                faults.append("regs: the row has no value of c%d" % reg)
            elif regs[32 * i:32 * i + 32] != want:
                faults.append("regs: c%d %s, the row's %s" % (reg, regs[32 * i:32 * i + 32], want))
        if regs[32 * len(q.registers):].strip("0"):
            faults.append("regs: not zero after the kind's %d" % len(q.registers))
    fetch = record.get("fetch") or []
    if len(fetch) != QUAD_RECORD_SLOTS:
        faults.append("fetch: %d slots, not %d" % (len(fetch), QUAD_RECORD_SLOTS))
    else:
        for i in range(QUAD_RECORD_SLOTS):
            got = [int(x, 16) for x in fetch[i]]
            if i < len(q.slots):
                want = [int(x, 16) for x in (row.get("tf") or {}).get(str(q.slots[i]), [])]
                if got != want:
                    faults.append("fetch %d (slot %d) %s, the row's %s" % (
                        i, q.slots[i], " ".join("%08X" % x for x in got), " ".join("%08X" % x for x in want)))
            elif any(got):
                faults.append("fetch %d: not zero (the kind has %d slots)" % (i, len(q.slots)))
    for key, reg in (("blend", 0x2201), ("depth", 0x2200)):
        got, want = int(record.get(key, "0x0"), 16), _rs(row, reg)
        if got != want:
            faults.append("%s 0x%08X, the row's %s" % (key, got, "none" if want is None else "0x%08X" % want))
    mask = _rs(row, 0x2104)
    if mask is None or record.get("mask") != mask & 0xF:
        faults.append("mask %s, the row's %s" % (record.get("mask"), "none" if mask is None else "%X" % (mask & 0xF)))
    control = _rs(row, 0x2202)
    if control is None or record.get("alpha_control") != control & 0xF:
        faults.append("alpha_control %s, the row's %s" % (record.get("alpha_control"), "none" if control is None else control & 0xF))
    else:
        # The reference with the test on; 0 with it off (the register is stale then: ruling 23).
        want = _rs(row, 0x210E, 0) if control & 8 else 0
        got = int(record.get("alpha_ref", "0x0"), 16)
        if got != want:
            faults.append("alpha_ref 0x%08X, the row's 0x%08X (%s)" % (got, want, "test on" if control & 8 else "test off: 0"))
    return faults


def frame_quad_faults(quads, rows):
    """What is wrong with a frame row's "quads" against the frame's rows (quad_rows_expected), as texts."""
    if quads is None:
        return ["no \"quads\" in the frame row"]
    want = quad_rows_expected(rows)
    faults = []
    if quads.get("over") or quads.get("count", 0) > MAX_QUAD_RECORDS:
        faults.append("over the bound of %d: count %s, over %s" % (MAX_QUAD_RECORDS, quads.get("count"), quads.get("over")))
    if quads.get("reason") != "none":
        if want or quads.get("reason") != "no-draw" or quads.get("count"):
            faults.append("no set (%s, seen %s, taken %s, over %s) for %d furniture rows" % (
                quads.get("reason"), quads.get("seen"), quads.get("taken"), quads.get("over"), len(want)))
        return faults
    records = quads.get("records") or []
    if len(records) != len(want) or quads.get("count") != len(records):
        faults.append("%d records, %d furniture rows (count %s)" % (len(records), len(want), quads.get("count")))
    for i, (record, row) in enumerate(zip(records, want)):
        where = "record %d (%s, row n %s)" % (i, _quad_table()[_row_hashes(row)[0]].kind, row.get("n"))
        if record.get("seq") != i:
            faults.append("%s: seq %s" % (where, record.get("seq")))
        faults.extend("%s: %s" % (where, text) for text in quad_record_faults(record, row))
    return faults


def _frames_with_data(path):
    """frame -> (the frame's decoded "verts" and "indx" rows in the file's order, each "indx" row with its
    "indxdata" row under "data"; the frame row's "quads" or None; whether the frame has a frame row)."""
    frames = {}
    by_n = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            head = line[:48]
            if not head.startswith('{"kind":"'):
                continue
            kind = head[9:head.find('"', 9)]
            if kind not in ("verts", "indx", "indxdata", "frame"):
                continue
            r = json.loads(line)
            fr = frames.setdefault(r["frame"], {"rows": [], "quads": None, "frame_row": False})
            if kind == "frame":
                fr["quads"] = r.get("quads")
                fr["frame_row"] = True
            elif kind == "indxdata":
                row = by_n.get((r["frame"], r.get("n")))
                if row is not None:
                    row["data"] = r
            else:
                if kind == "indx":
                    by_n[(r["frame"], r.get("n"))] = r
                fr["rows"].append(r)
    return frames


def report_quads_check(path):
    """Every frame's quad records against its rows (spec criterion 17). Returns 0 when every frame with a frame
    row has nothing wrong and at least one frame has a record, else 1."""
    frames = _frames_with_data(path)
    table = _quad_table()
    print("capture %s: %d frames" % (Path(path).name, len(frames)))
    good = with_records = checked = 0
    for n in sorted(frames):
        fr = frames[n]
        if not fr["frame_row"]:
            print("frame %d: the capture has no row of this frame's end (not checked)" % n)
            continue
        checked += 1
        faults = frame_quad_faults(fr["quads"], fr["rows"])
        for text in faults:
            print("frame %d: QUADS %s" % (n, text))
        if faults:
            continue
        good += 1
        records = fr["quads"].get("records") or []
        if not records:
            print("frame %d: no furniture row and no quad record (%s)" % (n, fr["quads"].get("reason")))
            continue
        with_records += 1
        kinds = [r["kind"] for r in records]
        order = [q.kind for q in table.values()]
        print("frame %d: %d quad records equal to their rows (%s)" % (
            n, len(records), ", ".join("%s %d" % (k, kinds.count(k)) for k in order if k in kinds)))
    print("quad records equal to the frame's rows in %d of %d frames; %s" % (
        good, checked, "none over the bound of %d" % MAX_QUAD_RECORDS
        if all(not (fr["quads"] or {}).get("over") for fr in frames.values()) else "SOME OVER THE BOUND"))
    if not with_records:
        print("no frame of the capture has a quad record: nothing was checked (a build without plan Task 12, "
              "--fable2_native_sun off, or a capture without furniture)")
    return 0 if checked and good == checked and with_records else 1


# --- the occluders: the cubes and the tree draws ----------------------------------------------------------------

def match_tree_occluders(rows, layer_pitch, depth_copy_line):
    """The tree occluder draws (pixel shader F1EB at the layer's target) against the frame's main-scene leaf
    draws (pixel shader 7CD5), over rows {"line", "vs", "ps", "buffers" (index buffer address, index count,
    vertex buffer address), "pitch", "in_scene", "indices"}:
      "matches"            [(occluder row, [main-scene leaf rows with the same buffers])]
      "scene_leaf_before"  main-scene leaf draws issued before the scene's depth copy (None without the line)
      "scene_leaf_after"   and after it
      "after_all_matched"  whether every leaf draw after the copy has an occluder with its buffers"""
    leaf = [r for r in rows if r["in_scene"] and r["ps"].startswith(LEAF_PS)]
    occ = [r for r in rows if not r["in_scene"] and r["pitch"] == layer_pitch and r["ps"].startswith(TREE_OCCLUDER_PS)]
    matches = [(o, [r for r in leaf if r["buffers"] == o["buffers"]]) for o in occ]
    if depth_copy_line is None:
        return {"matches": matches, "scene_leaf_before": None, "scene_leaf_after": None, "after_all_matched": None}
    after = [r for r in leaf if r["line"] > depth_copy_line]
    return {"matches": matches, "scene_leaf_before": len(leaf) - len(after), "scene_leaf_after": len(after),
            "after_all_matched": all(any(o["buffers"] == r["buffers"] for o in occ) for r in after)}


def report_occluders(path, frame):
    """What the sun layer's occluders are, from one frame's rows: each cube (vertex shader D1CC) against the
    main scene's draws of the same unit cube and the same vertex c119.x; each tree occluder draw against the
    main scene's leaf draws of the same buffers, and where the scene's depth copy lies between them."""
    scene_pitch = None
    depth_copy = None
    draws, cubes = [], []
    for line_no, r in frame_rows(path, frame, kinds=("draw", "indx", "cmdgap", "resolve")):
        kind = r["kind"]
        if kind in ("cmdgap", "resolve"):
            if r.get("cmd") and depth_copy is None and "1A02A000" in r["cmd"].upper():
                for step in stream_steps(hex_dwords(r["cmd"])):
                    if step["type"] == "resolve" and step["base"] == SCENE_DEPTH_BASE:
                        depth_copy = line_no
            continue
        ps, vs = _row_hashes(r)
        pitch = int(r.get("surface_info", "0x0"), 16) & 0x3FFF
        if r.get("in_scene") and scene_pitch is None:
            scene_pitch = pitch
        if kind == "draw":
            ib, vb = r.get("ib") or {}, r.get("vb") or {}
            draws.append({"line": line_no, "vs": vs[:4], "ps": ps[:4], "pitch": pitch, "in_scene": bool(r.get("in_scene")),
                          "buffers": (ib.get("phys_addr"), ib.get("count"), vb.get("phys_addr")), "indices": ib.get("count"),
                          "vertices": vb.get("vertices"), "depth": _rs(r, 0x2200, 0), "colorcontrol": _rs(r, 0x2202, 0),
                          "alpha_ref": _rs(r, 0x210E, 0), "matrix": tuple((r.get("bank") or [])[:16])})
        elif r.get("vertices") == 8 and r.get("indices") == 30:
            bank = r.get("bank") or []
            cubes.append({"line": line_no, "n": r.get("n"), "vs": vs[:4], "ps": ps[:4] or "none", "pitch": pitch,
                          "in_scene": bool(r.get("in_scene")), "c119x": bank[476] if len(bank) > 476 else None,
                          "c9": tuple(bank[36:40]) if len(bank) >= 40 else None, "depth": _rs(r, 0x2200, 0)})
    if scene_pitch is None:
        print("frame %d: no main-scene row" % frame)
        return 1
    layer_pitch = scene_pitch // 2
    print("%s frame %d: the scene's target has pitch %d, the layer's %d; the scene's depth copy (resolve to 0x%08X) is at line %s" % (
        Path(path).name, frame, scene_pitch, layer_pitch, SCENE_DEPTH_BASE, depth_copy))
    print()
    print("unit cubes (8 vertices, 30 indices, the indexed builder):")
    for c in cubes:
        where = "main scene" if c["in_scene"] else "outside, pitch %d" % c["pitch"]
        twins = [o for o in cubes if o is not c and o["in_scene"] != c["in_scene"] and o["c119x"] == c["c119x"]]
        print("  line %d n %s vs %s ps %s %s: c119.x %s, depth word 0x%08X%s%s" % (
            c["line"], c["n"], c["vs"], c["ps"], where, "%.6f" % c["c119x"] if c["c119x"] is not None else "?", c["depth"],
            "" if depth_copy is None or not c["in_scene"] else ", %s the depth copy" % ("after" if c["line"] > depth_copy else "BEFORE"),
            "; the same c119.x as " + ", ".join("line %d (vs %s ps %s)" % (o["line"], o["vs"], o["ps"]) for o in twins) if twins
            else "; no cube with the same c119.x on the other side"))
    m = match_tree_occluders(draws, layer_pitch, depth_copy)
    print()
    print("tree occluder draws (ps %s at pitch %d): %d" % (TREE_OCCLUDER_PS, layer_pitch, len(m["matches"])))
    for o, same in m["matches"]:
        print("  line %d vs %s: %s indices, %s vertices, depth 0x%08X, register 0x2202 0x%08X, alpha reference %.4f" % (
            o["line"], o["vs"], o["indices"], o["vertices"], o["depth"], o["colorcontrol"], _word_float(o["alpha_ref"])))
        for r in same:
            print("      the same index buffer, count and vertex buffer: main-scene line %d, vs %s ps %s, depth 0x%08X, 0x2202 0x%08X%s; "
                  "vertex c0..c3 %s" % (
                      r["line"], r["vs"], r["ps"], r["depth"], r["colorcontrol"],
                      "" if depth_copy is None else ", %s the depth copy" % ("after" if r["line"] > depth_copy else "before"),
                      "equal to the occluder's (the same instance)" if r["matrix"] and r["matrix"] == o["matrix"]
                      else "not the occluder's (another instance of the mesh)"))
        if not same:
            print("      no main-scene leaf draw has the same buffers")
    if m["scene_leaf_before"] is not None:
        print("main-scene leaf draws (ps %s): %d before the depth copy, %d after it; each of those after it has an occluder with its buffers: %s" % (
            LEAF_PS, m["scene_leaf_before"], m["scene_leaf_after"], "yes" if m["after_all_matched"] else "NO"))
    return 0


# --- sign fields of every bound texture (review-task-8b, Minor 2) -----------------------------------------------

FLOAT_FORMATS = (29, 30, 31, 32, 36, 37, 38)


def sign_known(kind, slot, fmt, signs):
    """Whether a non-zero set of sign fields is one the native pass handles: the terrain's base map (a "tess"
    row's slot 3 with gamma on r, g, b and nothing on alpha: the curve, since 5d0f974), or a float format with
    all four fields 1 (its values are its own whatever the fields say; the fog and post blocks pin theirs)."""
    if kind == "tess" and slot == 3 and tuple(signs) == (3, 3, 3, 0):
        return True
    return fmt in FLOAT_FORMATS and tuple(signs) == (1, 1, 1, 1)


def sign_census(rows):
    """Over rows with "kind" and "tf" (slot -> six hex words): {"textures": fetch constants looked at,
    "fields": {(row kind, slot, format, sign fields): count} for every constant with a field that is not 0,
    "exp": {(row kind, slot, format, exponent adjust): count} for every adjust that is not 0}."""
    out = {"textures": 0, "fields": {}, "exp": {}}
    for r in rows:
        for slot, fc in (r.get("tf") or {}).items():
            fc = [int(x, 16) for x in fc]
            out["textures"] += 1
            signs = tuple(ft.sign_fields(fc))
            fmt = fc[1] & 0x3F
            if any(signs):
                key = (r["kind"], int(slot), fmt, signs)
                out["fields"][key] = out["fields"].get(key, 0) + 1
            e = ft.exp_adjust(fc)
            if e:
                key = (r["kind"], int(slot), fmt, e)
                out["exp"][key] = out["exp"].get(key, 0) + 1
    return out


_RE_TF = re.compile(r'"tf":(\{[^}]*\})')
_RE_ANY_KIND = re.compile(r'^\{"kind":"(draw|tess|verts|indx)"')


def report_signs(paths):
    """The sign fields and exponent adjusts of every texture bound at every decoded row of the captures named:
    what is not all zero, and whether the native pass knows it."""
    total = {"textures": 0, "fields": {}, "exp": {}}
    unknown = 0
    for path in paths:
        def rows():
            with open(path, encoding="utf-8") as f:
                for line in f:
                    m = _RE_ANY_KIND.match(line[:24])
                    if not m:
                        continue
                    t = _RE_TF.search(line)
                    if t and len(t.group(1)) > 2:
                        yield {"kind": m.group(1), "tf": json.loads(t.group(1))}
        c = sign_census(rows())
        bad = sum(n for k, n in c["fields"].items() if not sign_known(*k)) + sum(c["exp"].values())
        unknown += bad
        print("%s: %d bound textures, %d with a sign field that is not 0 (%d not known to the pass), %d with an exponent adjust" % (
            Path(path).name, c["textures"], sum(c["fields"].values()), bad - sum(c["exp"].values()), sum(c["exp"].values())))
        total["textures"] += c["textures"]
        for part in ("fields", "exp"):
            for k, n in c[part].items():
                total[part][k] = total[part].get(k, 0) + n
    print()
    print("all %d captures: %d bound textures" % (len(paths), total["textures"]))
    for (kind, slot, fmt, signs), n in sorted(total["fields"].items(), key=str):
        print("  %-5s slot %2d format %2d signs %s: %d rows%s" % (
            kind, slot, fmt, " ".join(map(str, signs)), n, "" if sign_known(kind, slot, fmt, signs) else "   <-- NOT KNOWN TO THE PASS"))
    for (kind, slot, fmt, e), n in sorted(total["exp"].items(), key=str):
        print("  %-5s slot %2d format %2d exponent adjust %d: %d rows   <-- NOT KNOWN TO THE PASS" % (kind, slot, fmt, e, n))
    print("sign fields or exponent adjusts the pass does not know: %s" % ("none" if not unknown else "%d rows" % unknown))
    return 1 if unknown else 0


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
    p = sub.add_parser("block")
    p.add_argument("capture")
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
    p = sub.add_parser("graph")
    p.add_argument("capture")
    p.add_argument("--frame", type=int, required=True)
    p.add_argument("--all", action="store_true", help="every node, not only those after the main scene")
    p.add_argument("--no-stats", action="store_true", help="do not read the buffers' dumps")
    p.add_argument("--png", help="a path prefix: every decoded buffer is also written as a picture")
    p = sub.add_parser("quads")
    p.add_argument("capture")
    p.add_argument("--frame", type=int)
    p.add_argument("--check", action="store_true", help="the capture's quad records against the frame's rows (criterion 17)")
    p = sub.add_parser("occluders")
    p.add_argument("capture")
    p.add_argument("--frame", type=int, required=True)
    p = sub.add_parser("signs")
    p.add_argument("captures", nargs="+")
    for name in ("resolves", "table", "block", "find-luminance", "downsample", "graph"):
        sub.choices[name].add_argument("--tex-root")
    args = ap.parse_args(argv)
    if args.cmd == "graph":
        return report_graph(args.capture, args.frame, args.tex_root, not args.no_stats, args.all, args.png)
    if args.cmd == "quads":
        return report_quads_check(args.capture) if args.check else report_quads(args.capture, args.frame)
    if args.cmd == "occluders":
        return report_occluders(args.capture, args.frame)
    if args.cmd == "signs":
        return report_signs(args.captures)
    if args.cmd == "ramp":
        return report_ramp(args.log, args.pairs)
    cap = load_capture(args.capture, args.tex_root)
    if args.cmd == "resolves":
        return report_resolves(cap, args.frame, args.all)
    if args.cmd == "table":
        return report_table(cap, args.full)
    if args.cmd == "block":
        return report_block(cap)
    if args.cmd == "find-luminance":
        return report_find_luminance(cap, args.frame, args.threshold)
    return report_downsample(cap, args.frame)


if __name__ == "__main__":
    sys.exit(main())
