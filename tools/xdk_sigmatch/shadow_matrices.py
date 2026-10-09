"""Check the sun shadow maps' light matrices in a discovery capture.

Usage: python tools\\xdk_sigmatch\\shadow_matrices.py <native_discovery_*.jsonl> [--tolerance 1e-3]
           [--size 1024] [--direct <vertex shader hash> ...]

What it reads (rows as the capture writes them, one JSON object a line):
  - the clear that opens a shadow pass: a "raw" row of DrawIndx2:821EF988 whose surface word
    (args[5]) has pitch 1040;
  - shadow-pass draws: "draw" rows outside the main scene with surface pitch 1040
    ("surface_info") and colour mask 0 ("rs" 2104), with the vertex bank ("bank": c0.. as floats);
  - the frame's scene rows ("in_scene" true): pixel registers c12, c13, c14 and c36..c43 of
    "pfull" (a row with one of them unread is counted and left out).
The first pass of a frame is the near map, the second the far map; a later one is "extra".

Per frame and pass:
  M = [c0..c3] * inverse([c4..c6; 0 0 0 1]) of every row (c4..c6 are the world rows of the
  caster shaders; for a shader of DIRECT_VS or named with --direct, whose c0..c3 already is the
  light matrix, M = [c0..c3]). A row whose world block is singular or not finite is skipped and counted. The
  pass's matrix is the element-wise median; the largest deviation of a row from it is printed and
  every row beyond the tolerance is named.
  The median against the scene's registers (near: c36..c39, far: c40..c43), which hold world to
  shadow-map texture space: u = 0.5 x + (0.5 + h) w, v = -0.5 y + (0.5 + h) w, depth = z, w = w,
  with h the half texel 0.5 / size in either sign or none (the best is taken and printed).
Then, per map, the map position of the world origin in texels (x * size / 2, y * size / 2), its
distance from a whole texel (a map whose origin snaps to texels keeps it at 0 while the light
turns), and its difference between consecutive frames with how far the axes turned.

Exit code 0, with "RESULT: OK", only when at least one pass was compared and every pass agrees
within the tolerance: every used row with the pass's median, and the median with every distinct
value of the frame's scene registers. A pass with no usable row, a pass without scene registers,
a frame whose scene rows hold more than one value, or a third pass fails.
"""
import argparse
import json
import math
import re
import struct
import sys
from collections import Counter

SHADOW_PITCH = 1040
PITCH_MASK = 0x3FFF
CLEAR_FUNC = "821EF988"
PASS_NAMES = ("near", "far")
MAP_REGISTERS = ((36, 37, 38, 39), (40, 41, 42, 43))
SCENE_REGISTERS = (12, 13, 14, 36, 37, 38, 39, 40, 41, 42, 43)
MAX_NAMED = 8
# Vertex shaders that build the world position themselves and multiply it by c0..c3, so that
# c0..c3 is the light matrix and c4..c6 are not world rows. 0xA8A5...: the instanced bushes
# (oPos = c0..c3 . world, instr 47 to 50 of its dump; captures native_discovery_20261008_105316,
# _105659, _105833: read as world rows its rows are off by 3 to 12, read directly by under 1e-6).
DIRECT_VS = ("0xA8A5B2A89F70336A",)
_FRAME = re.compile(r'"frame":\s*(\d+)')
_SURFACE = re.compile(r'"surface_info":\s*"0x([0-9A-Fa-f]{8})"')


class Row:
    """A shadow-pass draw row: where it is, its vertex shader and its bank."""

    def __init__(self, line, vs, bank):
        self.line, self.vs, self.bank = line, vs, bank


class Pass:
    def __init__(self, frame, index, opened_by_clear):
        self.frame, self.index, self.opened_by_clear = frame, index, opened_by_clear
        self.rows = []
        self.used = 0
        self.skipped = 0
        self.direct = 0
        self.median = None
        self.max_dev = None
        self.named = []       # (row ordinal, line, vs, deviation) beyond the tolerance
        self.residual = None  # against the scene registers, the best half texel
        self.parts = None     # residual of u, v, depth, w
        self.half = None      # ("+", "-", "none") for u and v
        self.agrees = False
        self.why = ""

    @property
    def name(self):
        return PASS_NAMES[self.index] if self.index < len(PASS_NAMES) else "extra"


class Frame:
    def __init__(self, number):
        self.number = number
        self.passes = []
        self.scene = Counter()  # tuple of the SCENE_REGISTERS values -> rows
        self.scene_unread = 0
        self.odd_rows = 0       # pitch 1040 with a colour mask


class Result:
    def __init__(self):
        self.frames = {}
        self.compared = 0
        self.agreeing = 0
        self.ok = False


def _finite(values):
    return all(isinstance(v, (int, float)) and math.isfinite(v) for v in values)


def inverse_affine(w):
    """Inverse of [w; 0 0 0 1] for three rows of four, as three rows; None when singular."""
    a, b, c = w[0][:3], w[1][:3], w[2][:3]
    cof = [[b[1] * c[2] - b[2] * c[1], a[2] * c[1] - a[1] * c[2], a[1] * b[2] - a[2] * b[1]],
           [b[2] * c[0] - b[0] * c[2], a[0] * c[2] - a[2] * c[0], a[2] * b[0] - a[0] * b[2]],
           [b[0] * c[1] - b[1] * c[0], a[1] * c[0] - a[0] * c[1], a[0] * b[1] - a[1] * b[0]]]
    det = a[0] * cof[0][0] + a[1] * cof[1][0] + a[2] * cof[2][0]
    scale = max(abs(v) for r in (a, b, c) for v in r)
    if not math.isfinite(det) or scale == 0.0 or abs(det) <= 1e-12 * scale ** 3:
        return None
    inv = [[v / det for v in r] for r in cof]
    t = [w[0][3], w[1][3], w[2][3]]
    return [inv[i] + [-(inv[i][0] * t[0] + inv[i][1] * t[1] + inv[i][2] * t[2])] for i in range(3)]


def row_matrix(bank, direct):
    """The light matrix a shadow row implies, or None (singular or non-finite world rows)."""
    if len(bank) < 28 or not _finite(bank[:16]):
        return None
    m = [list(bank[4 * i:4 * i + 4]) for i in range(4)]
    if direct:
        return m
    if not _finite(bank[16:28]):
        return None
    inv = inverse_affine([list(bank[16 + 4 * i:20 + 4 * i]) for i in range(3)])
    if inv is None:
        return None
    inv4 = inv + [[0.0, 0.0, 0.0, 1.0]]
    return [[sum(m[i][k] * inv4[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def _median(values):
    s = sorted(values)
    n = len(s)
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def _max_diff(a, b):
    return max(abs(x - y) for ra, rb in zip(a, b) for x, y in zip(ra, rb))


def map_residual(m, regs, size):
    """Largest difference of the texture-space form of `m` from four register rows, with the best
    half texel per axis: (residual, (u, v, depth, w), (sign of u, sign of v))."""
    x, y, z, w = m
    half = 0.5 / size
    parts, signs = [], []
    for row, k, target in ((x, 0.5, regs[0]), (y, -0.5, regs[1])):
        best = None
        for label, h in (("+", half), ("-", -half), ("none", 0.0)):
            r = max(abs(k * a + (0.5 + h) * b - t) for a, b, t in zip(row, w, target))
            if best is None or r < best[0]:
                best = (r, label)
        parts.append(best[0])
        signs.append(best[1])
    parts.append(max(abs(a - t) for a, t in zip(z, regs[2])))
    parts.append(max(abs(a - t) for a, t in zip(w, regs[3])))
    return max(parts), tuple(parts), tuple(signs)


def read_rows_from(lines):
    """Yield ("clear", frame, line) / ("shadow", frame, line, vs, bank) / ("odd", frame) /
    ("scene", frame, registers or None) for the lines of a capture."""
    for number, line in enumerate(lines, 1):
        head = line[:800]
        if head.startswith('{"kind":"raw"'):
            if CLEAR_FUNC not in head:
                continue
            row = json.loads(line)
            args = row.get("args") or []
            if not str(row.get("func", "")).endswith(CLEAR_FUNC) or len(args) < 6:
                continue
            try:
                word = int(args[5], 16)
            except (TypeError, ValueError):
                continue
            # The clear's second call passes a packet header in r7 and no surface word.
            if word & PITCH_MASK == SHADOW_PITCH and int(args[4], 16) < 0x10000:
                yield ("clear", int(row["frame"]), number)
            continue
        if not head.startswith('{"kind":"draw"') and not head.startswith('{"kind":"tess"'):
            continue
        fm = _FRAME.search(head)
        if not fm:
            continue
        frame = int(fm.group(1))
        if '"in_scene":true' in head:
            at = line.find('"pfull":"')
            if at < 0:
                continue
            full = line[at + 9:at + 9 + 32 * 256]
            regs = []
            for r in SCENE_REGISTERS:
                h = full[32 * r:32 * r + 32]
                if len(h) < 32 or "-" in h:
                    regs = None
                    break
                regs.append(struct.unpack(">4f", bytes.fromhex(h)))
            yield ("scene", frame, tuple(regs) if regs is not None else None)
            continue
        sm_ = _SURFACE.search(line)
        if not sm_ or int(sm_.group(1), 16) & PITCH_MASK != SHADOW_PITCH or not head.startswith('{"kind":"draw"'):
            continue
        row = json.loads(line)
        if row.get("in_scene"):
            continue
        try:
            mask = int((row.get("rs") or {}).get("2104", ""), 16)
        except ValueError:
            continue
        if mask != 0:
            yield ("odd", frame)
            continue
        yield ("shadow", frame, number, str(row.get("vs_hash")), row.get("bank") or [])


def analyse_rows(events, tolerance=1e-3, size=1024, direct=()):
    direct = {d.upper().replace("0X", "0x") for d in tuple(direct) + DIRECT_VS}
    res = Result()
    for ev in events:
        frame = res.frames.setdefault(ev[1], Frame(ev[1]))
        if ev[0] == "clear":
            frame.passes.append(Pass(frame.number, len(frame.passes), True))
        elif ev[0] == "shadow":
            if not frame.passes:
                frame.passes.append(Pass(frame.number, 0, False))
            frame.passes[-1].rows.append(Row(ev[2], ev[3], ev[4]))
        elif ev[0] == "odd":
            frame.odd_rows += 1
        elif ev[2] is None:
            frame.scene_unread += 1
        else:
            frame.scene[ev[2]] += 1
    for frame in res.frames.values():
        for p in frame.passes:
            _analyse_pass(p, frame, tolerance, size, direct)
            res.compared += 1
            res.agreeing += p.agrees
    res.ok = res.compared > 0 and res.agreeing == res.compared
    return res


def _analyse_pass(p, frame, tolerance, size, direct):
    mats = []
    for ordinal, row in enumerate(p.rows):
        is_direct = row.vs.upper().replace("0X", "0x") in direct
        m = row_matrix(row.bank, is_direct)
        if m is None:
            p.skipped += 1
            continue
        p.direct += is_direct
        mats.append((ordinal, row, m))
    p.used = len(mats)
    if p.index >= len(PASS_NAMES):
        p.why = "a pass beyond the second: not compared"
        return
    if not mats:
        p.why = "no usable row"
        return
    p.median = [[_median([m[i][j] for _, _, m in mats]) for j in range(4)] for i in range(4)]
    p.max_dev = 0.0
    for ordinal, row, m in mats:
        d = _max_diff(m, p.median)
        p.max_dev = max(p.max_dev, d)
        if d > tolerance:
            p.named.append((ordinal, row.line, row.vs, d))
    if not frame.scene:
        p.why = "no scene row with c36..c43"
        return
    worst = None
    for regs in frame.scene:
        target = [regs[SCENE_REGISTERS.index(r)] for r in MAP_REGISTERS[p.index]]
        got = map_residual(p.median, target, size)
        if worst is None or got[0] > worst[0]:
            worst = got
    p.residual, p.parts, p.half = worst
    if p.named:
        p.why = f"{len(p.named)} row(s) beyond the tolerance"
    elif p.residual > tolerance:
        p.why = "the scene registers hold another matrix"
    elif len(frame.scene) > 1:
        p.why = "the scene rows hold more than one value"
    else:
        p.agrees = True


def _fmt(m):
    return "\n".join("      " + " ".join(f"{v: .9g}" for v in r) for r in m)


def report(res, tolerance, size, out=None):
    out = out or sys.stdout

    def w(s=""):
        print(s, file=out)

    for number in sorted(res.frames):
        frame = res.frames[number]
        rows = sum(frame.scene.values())
        if not frame.passes:
            w(f"frame {number}: no shadow pass rows ({rows} scene rows with the registers); nothing compared")
            continue
        n = len(frame.scene)
        unread = f" ({frame.scene_unread} unread)" if frame.scene_unread else ""
        w(f"frame {number}: {len(frame.passes)} passes; {n} distinct value{'' if n == 1 else 's'} of c36..c43 in "
          f"{rows} scene rows{unread}" + (f"; {frame.odd_rows} rows at pitch 1040 with a colour mask (not shadow rows)"
                                         if frame.odd_rows else ""))
        if frame.scene:
            regs = frame.scene.most_common(1)[0][0]
            for r in (12, 13, 14):
                v = regs[SCENE_REGISTERS.index(r)]
                w(f"    scene c{r} = (" + ", ".join(f"{x:.9g}" for x in v) + ")")
        for p in frame.passes:
            text = f"frame {number} pass {p.index} ({p.name}): {len(p.rows)} rows, {p.used} used"
            if p.skipped:
                text += f", {p.skipped} skipped (singular or non-finite world rows)"
            if p.direct:
                text += f", {p.direct} direct"
            if not p.opened_by_clear:
                text += ", NO CLEAR ROW before it"
            if p.max_dev is not None:
                text += f"; largest deviation from the median {p.max_dev:.3g}"
            w(text)
            shaders = Counter(r.vs for r in p.rows)
            w("    vertex shaders: " + ", ".join(f"{k} x{v}" for k, v in shaders.most_common()))
            if p.median is not None:
                w("    M (rows x, y, z, w):")
                w(_fmt(p.median))
                x, y, z = (math.sqrt(sum(v * v for v in p.median[i][:3])) for i in range(3))
                if x and y and z:
                    w(f"    world units across: {2 / x:.4f} x {2 / y:.4f} ({2 / x / size:.5f} a texel); depth span "
                      f"{1 / z:.2f}; depth axis ({', '.join(f'{v / z:.6f}' for v in p.median[2][:3])})")
            for ordinal, line, vs, d in p.named[:MAX_NAMED]:
                w(f"frame {number} pass {p.index} ({p.name}) row {ordinal} (line {line}) vs {vs}: deviation {d:.6g}")
            if len(p.named) > MAX_NAMED:
                w(f"    ... and {len(p.named) - MAX_NAMED} more rows beyond the tolerance")
            if p.residual is not None:
                first = MAP_REGISTERS[p.index][0]
                w(f"frame {number} pass {p.index} ({p.name}) against c{first}..c{first + 3}: residual {p.residual:.3g} "
                  f"(u {p.parts[0]:.3g}, v {p.parts[1]:.3g}, depth {p.parts[2]:.3g}, w {p.parts[3]:.3g}), "
                  f"half texel u {p.half[0]}, v {p.half[1]}" + ("" if p.residual <= tolerance else "  DISAGREES"))
            if not p.agrees:
                w(f"frame {number} pass {p.index} ({p.name}): NOT PROVEN: {p.why}")
    w()
    w(f"map origin (the world origin's place in the map, texels = clip * {size} / 2)")
    for index, name in enumerate(PASS_NAMES):
        prev = None
        off = [0.0, 0.0]
        frames = 0
        for number in sorted(res.frames):
            passes = res.frames[number].passes
            m = passes[index].median if index < len(passes) else None
            if m is None:
                prev = None
                continue
            tx, ty = m[0][3] * size / 2, m[1][3] * size / 2
            ox, oy = tx - round(tx), ty - round(ty)
            off = [max(off[0], abs(ox)), max(off[1], abs(oy))]
            frames += 1
            w(f"  {name}, frame {number}: ({tx:.4f}, {ty:.4f}) texels, off a whole texel by ({ox + 0.0:.4f}, "
              f"{oy + 0.0:.4f})")
            if prev is not None and prev[0] == number - 1:
                dx, dy = round(tx - prev[1], 3) + 0.0, round(ty - prev[2], 3) + 0.0
                turn = max(abs(a - b) for i in range(3) for a, b in zip(m[i][:3], prev[3][i][:3]))
                w(f"  {name}, frame {number - 1} -> {number}: origin moves ({dx:.3f}, {dy:.3f}) texels; axes change "
                  f"by at most {turn:.3g}")
            prev = (number, tx, ty, m)
        if frames:
            w(f"  {name}: largest distance of the origin from a whole texel ({off[0]:.4f}, {off[1]:.4f}) over "
              f"{frames} frames")
    w()
    w(f"passes compared: {res.compared}, agreeing: {res.agreeing} (tolerance {tolerance:g})")
    w("RESULT: OK" if res.ok else "RESULT: FAIL")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("capture")
    ap.add_argument("--tolerance", type=float, default=1e-3)
    ap.add_argument("--size", type=int, default=1024, help="the map's width in texels")
    ap.add_argument("--direct", nargs="*", default=[], metavar="VS_HASH",
                    help="more vertex shaders whose c0..c3 is the light matrix itself (besides DIRECT_VS)")
    args = ap.parse_args(argv)
    with open(args.capture, "r", encoding="utf-8", errors="replace") as f:
        res = analyse_rows(read_rows_from(f), args.tolerance, args.size, args.direct)
    report(res, args.tolerance, args.size)
    return 0 if res.ok else 1


if __name__ == "__main__":
    sys.exit(main())
