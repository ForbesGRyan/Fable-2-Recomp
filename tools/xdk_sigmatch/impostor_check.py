"""The machine check of the far woods' impostors (light_formulas.impostor_position) against the game's own
microcode (ucode_eval): vertex shader 129F7F2793DA9084. The distant-trees sub-project, plan Task 4, spec
criterion 1 (its first half: made inputs; the captured rows are plan Task 6's).

  formulas [--trials 2000] [--seed 1]       the position on made draws, and each mutation (each must fail)
  trace --instr N [--seed S] [--trial T]    the interpreter's registers after instruction N of 129F for a made
                                            draw (the transcription's oracle, register by register)
  words <capture>                           the quad words of every in-scene 129F "verts" row with its storage
  places <capture>                          the raised place (o0) of every such quad
Each exits 1 on any failure (words and places also when the capture has no such row with its storage).

What one position trial is. A draw is the vertex shader's microcode, a constant bank, the storage's bytes (one
half4 a quad) and a vertex index. The microcode runs for that index and gives oPos, o0 and o1. Beside it, from
the SAME bytes and registers but through nothing of the interpreter's: the quad's word is decoded by quad_words
(the order and endian of light_formulas.IMPOSTOR_SHADER; the interpreter's fetch goes through
capture_replay.element and the shader's own swizzle), the corner is impostor_corner's, and impostor_position
gives the three outputs. A trial agrees when every component is within TOLERANCE * max(1, |value|) (criterion
1's bound); a component that is no number must be no number on both sides, an infinity the same infinity.

Made inputs (the constants at this file's head): the word's place within 2,000 units (seven trials of ten
within 300), the size in (0.5, 40], both as halves; c47 (the eye) about the place, within 300 units of it in
seven trials of ten and anywhere within 2,000 otherwise, its height about the RAISED place so that the height
comparison of instr 31 falls either way in half of the trials; c8.z in [0, 2]; c46 a unit horizontal vector
times a scale in [0.25, 2], and in half of the trials with a third component as well (the plan's "horizontal"
alone would leave every c46.z term of instr 16, 18, 22 and 26 unchecked); c114.x in [0.1, 4], c114.y in
[0.25, 4]; c115 to c117 rows with entries in [-2, 2] and an offset in [-50, 50]; c72..c75 rows with entries in
[-2, 2] or, in half of the rows, [-200, 200]; c113 = (1, 0, 0, 1) in half of the trials and random in [-2, 2]
otherwise; every other register random (a register the formula does not take would show). Every value is
rounded to single precision before use, as the game holds it (the word's to half precision); the arithmetic
of both sides is double precision. The literal registers hold ONLY light_formulas.IMPOSTOR_SHADER's table.
The corner is the trial's number modulo 4, turned by one every 40 trials, so that each corner has a quarter.

Seven trials of 40 are made at the degenerate points (KINDS): "size0" a quad of no size; "level" a view whose
second row has no part of the lying axis (its power is 2^(-inf) = 0); "down" one with no part of the upright
axis; "flat" one with no part of either; "nan" a size or a place that is no number; "inf" an infinite size;
"still" c114.y = 0 (both powers 1). In "size0" and "flat" the microcode multiplies 0 by 1 / 0: the machine runs
with the console's product rule (ucode_eval's zero_product: a zero factor gives +0), the formula says the same,
and check_position(zero_product=False) shows that an IEEE machine disagrees there and nowhere else.

Counted beside agreement (a check that cannot fail is no check: review focus 3): the trials at each corner;
"above" / "below" by the microcode's own r2.w of instr 31; "powered": both powers (r8.x, r8.y of instr 45 and
46) positive numbers, which they are exactly when both log arguments are; "tilted": both weights strictly
inside (0.02, 0.98), so that both axes carry the corner.

Mutations (POSITION_MUTATIONS; each must fail, tests/test_impostor_formulas.py): "rows" c116 for c115;
"components" the word's first two components exchanged; "sign" the size's sign; "corners" corners 0 and 1
exchanged, and 2 and 3; "exponent" c114.y changed by 1%; "raise" the raise left out (c8.z = 0); "matrix" c72
and c73 exchanged. ("literal", name) changes one literal by 1% in the formula alone; `shift=(name, factor)`
scales it in the formula AND at its place in the microcode's constants: the two must then agree again.
"""
import argparse
import math
import random
import struct
import sys
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture_replay as cr  # noqa: E402
import light_formulas as lf  # noqa: E402
import ucode_eval as ue  # noqa: E402
from wind_check import truncated  # noqa: E402

DUMPS = cr.DUMPS
VS, PS = lf.IMPOSTOR_SHADER["vs"], lf.IMPOSTOR_SHADER["ps"]
TOLERANCE = 1e-4            # criterion 1: |value - formula| <= TOLERANCE * max(1, |value|), every component
STRIDE_DWORDS = 2           # `vfetch_full ... Stride=2`: one half4 a quad
QUAD_BYTES = 4 * STRIDE_DWORDS
VERTEX_SLOT = 95            # vf0 (a stream vfN is fetch slot 95 - N)

# The made inputs' ranges (module text).
PLACE_NEAR, PLACE_FAR = 300.0, 2000.0
SIZE_RANGE = (0.5, 40.0)
RAISE_RANGE = (0.0, 2.0)
AXIS_SCALE = (0.25, 2.0)
C114X_RANGE, C114Y_RANGE = (0.1, 4.0), (0.25, 4.0)
VIEW_ENTRY, VIEW_OFFSET = 2.0, 50.0
CLIP_ENTRY_SMALL, CLIP_ENTRY_LARGE = 2.0, 200.0
UV_ENTRY = 2.0
MOST_QUADS = 64
KINDS = {3: "size0", 9: "level", 15: "down", 21: "flat", 27: "nan", 33: "inf", 39: "still"}     # by trial % 40
TILT = (0.02, 0.98)

POSITION_MUTATIONS = ("rows", "components", "sign", "corners", "exponent", "raise", "matrix")

# trials; those that disagree; the trials at each corner; by the height comparison of instr 31; with both powers
# on numbers; the largest relative error of a component that could be compared; the first failure's text (None);
# the trials of each degenerate kind; those with both weights inside TILT.
Result = namedtuple("Result", "trials failed corners above below powered worst first_failure kinds tilted")


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0] if math.isfinite(x) else x


def _f16(x):
    return struct.unpack(">e", struct.pack(">e", x))[0] if math.isfinite(x) else x


# --- the storage's words ------------------------------------------------------------------------------------------

_SWAPS = {0: (0, 1, 2, 3), 1: (1, 0, 3, 2), 2: (3, 2, 1, 0), 3: (2, 3, 0, 1)}      # xenos Endian: none, 8in16, 8in32, 16in32


def quad_words(verts_row, data_row, source=None, endian=None):
    """A "verts" row's quads as register r6 holds each after instr 12: a list of (x, y, z, s), one a quad
    (count / 4 of them), from the first eight bytes a quad of the row's "vertsdata" bytes.
    `endian`: the fetch constant's (0 none, 1 the bytes of each half exchanged, 2 the bytes of each dword
    reversed, 3 the halves of each dword exchanged); the element's four source components are then the dword's
    low half, its high half, the next dword's low and high half, read little-endian. `source`: for each of r6's
    four components the source component it takes (the fetch's swizzle). Defaults: light_formulas.IMPOSTOR_SHADER's
    (PROVISIONAL until plan Task 3). ValueError: the storage was not read, is no hex, is shorter than the quads
    need, the count is no whole number of quads, or the order or endian is none."""
    source = tuple(lf.IMPOSTOR_SHADER["word_source"] if source is None else source)
    endian = lf.IMPOSTOR_SHADER["word_endian"] if endian is None else endian
    if endian not in _SWAPS:
        raise ValueError("endian %r is none of 0 to 3" % (endian,))
    if len(source) != 4 or any(s not in (0, 1, 2, 3) for s in source):
        raise ValueError("the order %r is not four source components of 0 to 3" % (source,))
    if not data_row.get("read") or "bytes" not in data_row:
        raise ValueError("the storage was not read")
    count = int(verts_row["count"])
    if count < 0 or count % 4:
        raise ValueError("%d vertices are no whole number of quads" % count)
    try:
        data = bytes.fromhex(data_row["bytes"])
    except (ValueError, TypeError):
        raise ValueError("the storage's bytes are no hex") from None
    quads = count // 4
    if quads * QUAD_BYTES > len(data):
        raise ValueError("%d quads want %d bytes; the row has %d" % (quads, quads * QUAD_BYTES, len(data)))
    swap = _SWAPS[endian]
    out = []
    for q in range(quads):
        raw = data[q * QUAD_BYTES:(q + 1) * QUAD_BYTES]
        halves = []
        for d in (0, 4):
            dword = bytes(raw[d + i] for i in swap)
            halves.extend(struct.unpack("<2e", dword))
        out.append(tuple(halves[s] for s in source))
    return out


# --- a made draw ----------------------------------------------------------------------------------------------------

class Case:
    """One draw of 129F: the microcode's text, its constants ({register: [4]}), the storage's bytes (the vertex
    stream), the vertex index to run, the two fog tables (callables, with their texels' makers' state) and what
    a made case is (`kind`: "" or a name of KINDS)."""

    def __init__(self, text, consts, storage, index, tf16, tf17, kind=""):
        self.text, self.consts, self.storage, self.index, self.tf16, self.tf17, self.kind = (
            text, consts, storage, index, tf16, tf17, kind)


def _text(dumps, hash_=VS, kind="vert"):
    return ue.load(dumps, hash_, kind)


def _smooth(rng, channels=3):
    """A random smooth function of two coordinates per channel, positive on the unit square (a table whose
    value does not jump when a coordinate differs in its last bit)."""
    k = [[rng.uniform(0.2, 1.0), rng.uniform(-0.15, 0.6), rng.uniform(-0.15, 0.6), rng.uniform(-0.3, 0.6)]
         for _ in range(channels)]
    return lambda x, y: [a + b * x + c * y + d * x * y for a, b, c, d in k]


def _unit(rng):
    while True:
        v = [rng.uniform(-1.0, 1.0) for _ in range(3)]
        n = math.sqrt(sum(x * x for x in v))
        if 0.1 < n <= 1.0:
            return [x / n for x in v]


def _literal_registers(kind="vs"):
    """Registers 248 to 255 holding only the shader's table (module text)."""
    consts = {reg: [0.0] * 4 for reg in range(248, 256)}
    for reg, values in lf.IMPOSTOR_SHADER[kind + "_lit"].items():
        consts[reg] = [float(v) for v in values]
    return consts


def _coordinate(rng):
    return rng.uniform(-PLACE_NEAR, PLACE_NEAR) if rng.random() < 0.7 else rng.uniform(-PLACE_FAR, PLACE_FAR)


def made_case(seed=1, trial=0, dumps=DUMPS, text=None):
    """The made draw of one trial (deterministic in seed and trial)."""
    rng = random.Random("impostor/%d/%d" % (seed, trial))
    text = text if text is not None else _text(dumps)
    kind = KINDS.get(trial % 40, "")
    c = {reg: [f32(rng.uniform(-1.0, 1.0)) for _ in range(4)] for reg in range(248)}
    quads = rng.randrange(1, MOST_QUADS + 1)
    quad = rng.randrange(quads)
    words = [[_f16(_coordinate(rng)) for _ in range(3)] + [_f16(rng.uniform(*SIZE_RANGE))] for _ in range(quads)]
    word = words[quad]
    if kind == "size0":
        word[3] = 0.0
    elif kind == "nan":
        word[3 if trial % 80 < 40 else 0] = math.nan
    elif kind == "inf":
        word[3] = math.inf
    c[8] = [f32(rng.uniform(0.5, 2.0)), f32(rng.uniform(-1.0, 1.0)), f32(rng.uniform(*RAISE_RANGE)), 0.0]
    turn, scale = rng.uniform(0.0, 2.0 * math.pi), rng.uniform(*AXIS_SCALE)
    third = rng.uniform(-1.0, 1.0) if rng.random() < 0.5 else 0.0
    c[46] = [f32(math.cos(turn) * scale), f32(math.sin(turn) * scale), f32(third * scale), f32(rng.uniform(-1.0, 1.0))]
    size = word[3] if math.isfinite(word[3]) else 1.0
    place = [word[0] if math.isfinite(word[0]) else 0.0, word[1], word[2] + size * c[8][2]]      # the raised place
    if rng.random() < 0.7:
        eye = [place[k] + rng.uniform(-PLACE_NEAR, PLACE_NEAR) for k in range(3)]
    else:
        eye = [rng.uniform(-PLACE_FAR, PLACE_FAR) for _ in range(2)] + [place[2] + rng.uniform(-PLACE_NEAR, PLACE_NEAR)]
    c[47] = [f32(x) for x in eye] + [1.0]
    c[113] = [1.0, 0.0, 0.0, 1.0] if rng.random() < 0.5 else [f32(rng.uniform(-UV_ENTRY, UV_ENTRY)) for _ in range(4)]
    c[114] = [f32(rng.uniform(*C114X_RANGE)), f32(rng.uniform(*C114Y_RANGE)), f32(rng.uniform(-1.0, 1.0)), f32(rng.uniform(-1.0, 1.0))]
    for reg in (115, 116, 117):
        c[reg] = [f32(rng.uniform(-VIEW_ENTRY, VIEW_ENTRY)) for _ in range(3)] + [f32(rng.uniform(-VIEW_OFFSET, VIEW_OFFSET))]
    for reg in (72, 73, 74, 75):
        bound = CLIP_ENTRY_LARGE if rng.random() < 0.5 else CLIP_ENTRY_SMALL
        c[reg] = [f32(rng.uniform(-bound, bound)) for _ in range(4)]
    if kind in ("level", "flat"):
        c[116][0] = c[116][1] = 0.0
        c[46][2] = 0.0
    if kind in ("down", "flat"):
        c[116][2] = 0.0
    if kind == "still":
        c[114][1] = 0.0
    # The fog: one trial of four lies before the start (no table is fetched there).
    distance = math.sqrt(sum((c[47][k] - place[k]) ** 2 for k in range(3)))
    start = distance + rng.uniform(1.0, 50.0) if rng.random() < 0.25 else min(rng.uniform(1.0, 30.0), distance * rng.uniform(0.1, 0.9))
    c[110] = [f32(x) for x in _unit(rng)] + [0.0]
    c[111] = [f32(start), 1900.0, f32(rng.uniform(0.001, 0.02) if rng.random() < 0.5 else rng.uniform(0.02, 1.0)),
              f32(rng.uniform(0.0005, 0.01))]
    tf16 = lf.table_1d([[rng.random() for _ in range(3)] for _ in range(256)])
    tf17 = _smooth(rng)
    c.update(_literal_registers("vs"))
    storage = b"".join(struct.pack(">4e", *w) for w in words)
    corner = (trial + trial // 40) % 4
    return Case(text, c, storage, 4 * quad + corner, tf16, tf17, kind)


def case_words(case):
    """The case's quad words, decoded from its storage's bytes by quad_words (not through the machine)."""
    quads = len(case.storage) // QUAD_BYTES
    return quad_words({"count": 4 * quads}, {"read": True, "bytes": case.storage.hex()})


def case_word(case):
    """The word of the quad the case's vertex belongs to."""
    return case_words(case)[lf.impostor_corner(case.index)[0]]


def run_microcode(case, consts=None, text=None, zero_product=True):
    """(the microcode's outputs for the case's vertex index, the machine). The fetch floors its index (the dump
    has no RoundIndex) and decodes through capture_replay.element; the tables are the case's."""

    def vfetch(stream, offset, attrs, idx):
        if stream != 0 or not math.isfinite(idx):
            raise IndexError("vf%d index %s" % (stream, idx))
        at = (int(math.floor(idx)) * int(attrs.get("Stride", STRIDE_DWORDS)) + offset) * 4
        if at < 0 or at + QUAD_BYTES > len(case.storage):
            raise IndexError("vf%d index %s" % (stream, idx))
        return cr.element(case.storage, at, attrs)

    def sampler(slot, coords, attrs):
        if slot == 16:
            return case.tf16(coords[0]) + [1.0]
        if slot == 17:
            return case.tf17(coords[0], coords[1]) + [1.0]
        raise KeyError("vertex texture tf%d" % slot)

    m = ue.Machine(text or case.text, consts or case.consts, sampler, regs={0: [float(case.index), 0.0, 0.0, 0.0]},
                   vfetch=vfetch, zero_product=zero_product)
    return m.run(), m


def trace(case, instr):
    """The interpreter's registers ({register: [4]}) after instruction `instr` of the case's shader."""
    return run_microcode(case, text=truncated(case.text, instr))[1].r


def used_registers(seed=1, trials=8, dumps=DUMPS):
    """The constant registers the interpreter read over a few made trials (the fog begun in most)."""
    text = _text(dumps)
    used = set()
    for t in range(trials):
        used |= run_microcode(made_case(seed, t, text=text))[1].used_consts
    return used


# --- the position -------------------------------------------------------------------------------------------------

def _check_mutation(mutation, names, literals):
    if mutation is None or mutation in names:
        return
    if isinstance(mutation, tuple) and len(mutation) == 2 and mutation[0] == "literal" and mutation[1] in literals:
        return
    raise ValueError("unknown mutation %r (one of %s, or (\"literal\", name))" % (mutation, ", ".join(names)))


def _shifted(consts, kind, lit, shift):
    """(constants, literals) with literal `shift[0]` scaled by `shift[1]` at its place and by its name."""
    name, factor = shift
    reg, comp = lf.IMPOSTOR_SHADER[kind + "_lit_at"][name]
    consts = dict(consts)
    consts[reg] = list(consts[reg])
    consts[reg][comp] *= factor
    lit = dict(lit)
    lit[name] *= factor
    return consts, lit


def _compare(got, want, tolerance):
    """(agree, the largest relative error of the components that are numbers on both sides)."""
    ok, worst = True, 0.0
    for g, w in zip(got, want):
        if g != g or w != w:
            ok = ok and g != g and w != w
        elif g == w:
            continue
        elif math.isinf(g) or math.isinf(w):
            ok = False
        else:
            error = abs(g - w) / max(1.0, abs(g))
            worst = max(worst, error)
            ok = ok and error <= tolerance
    return ok, worst


def formula_position(case, mutation=None, lit=None):
    """impostor_position for the case's vertex from its storage's bytes and registers: oPos, o0 and o1 as one
    list of nine. `mutation`: one of POSITION_MUTATIONS or ("literal", name)."""
    lit = dict(lit or lf.impostor_literals("vs"))
    c = {reg: list(case.consts[reg]) for reg in lf.IMPOSTOR_SHADER["reg"]}
    if isinstance(mutation, tuple):
        lit[mutation[1]] *= 1.01
    quad, k = lf.impostor_corner(case.index, lit)
    words = case_words(case)
    if not 0 <= quad < len(words):
        raise IndexError("vertex %d names quad %d of %d" % (case.index, quad, len(words)))
    word = words[quad]
    if mutation == "rows":
        c[115] = c[116]
    elif mutation == "components":
        word = (word[1], word[0], word[2], word[3])
    elif mutation == "sign":
        word = (word[0], word[1], word[2], -word[3])
    elif mutation == "corners":
        k = {0: 1, 1: 0, 2: 3, 3: 2}.get(k, k)
    elif mutation == "exponent":
        c[114][1] *= 1.01
    elif mutation == "raise":
        c[8][2] = 0.0
    elif mutation == "matrix":
        c[72], c[73] = c[73], c[72]
    out = lf.impostor_position(k, word, c, lit=lit)
    return list(out["pos"]) + list(out["place"]) + list(out["uv"])


Trial = namedtuple("Trial", "ok error got want corner above powered tilted")


def evaluate_position(case, mutation=None, shift=None, zero_product=True):
    """One trial: Trial(agree, the largest relative error, the microcode's nine values, the formula's, the
    corner, above, powered, tilted)."""
    consts, lit = case.consts, lf.impostor_literals("vs")
    if shift is not None:
        consts, lit = _shifted(consts, "vs", lit, shift)
    out, m = run_microcode(case, consts=consts, zero_product=zero_product)
    got = [float(x) for x in out["oPos"]] + [float(x) for x in out["o0"][:3]] + [float(x) for x in out["o1"][:2]]
    shown = Case(case.text, consts, case.storage, case.index, case.tf16, case.tf17, case.kind)
    want = formula_position(shown, mutation, lit)
    ok, error = _compare(got, want, TOLERANCE)
    ev, eh = m.r[8][0], m.r[8][1]                       # instr 45, 46: the two powers; nothing writes r8 after
    powered = math.isfinite(ev) and math.isfinite(eh) and ev > 0.0 and eh > 0.0
    tilted = powered and TILT[0] < ev / (ev + eh) < TILT[1]
    corner = lf.impostor_corner(case.index)[1]
    return Trial(ok, error, got, want, corner, m.r[2][3] == 1.0, powered, tilted)   # r2.w: instr 31


def _describe(trial, t, seed, kind, names=("oPos", "o0", "o1")):
    return "trial %d (seed %d%s, corner %d): relative error %.3e; microcode %s, formula %s" % (
        trial, seed, ", " + kind if kind else "", t.corner, t.error, t.got, t.want)


def check_position(trials=2000, seed=1, mutation=None, dumps=DUMPS, shift=None, zero_product=True):
    """`trials` made draws of 129F against impostor_position: Result."""
    _check_mutation(mutation, POSITION_MUTATIONS, lf.IMPOSTOR_SHADER["vs_lit_at"])
    text = _text(dumps)
    failed = above = below = powered = tilted = 0
    corners, kinds = [0, 0, 0, 0], {}
    worst, first = 0.0, None
    for k in range(trials):
        case = made_case(seed, k, text=text)
        t = evaluate_position(case, mutation, shift, zero_product)
        worst = max(worst, t.error)
        if t.corner in (0, 1, 2, 3):
            corners[t.corner] += 1
        above += t.above
        below += not t.above
        powered += t.powered
        tilted += t.tilted
        if case.kind:
            kinds[case.kind] = kinds.get(case.kind, 0) + 1
        if not t.ok:
            failed += 1
            first = first or _describe(k, t, seed, case.kind)
    return Result(trials, failed, tuple(corners), above, below, powered, worst, first, kinds, tilted)


def single_gap(trials=2000, seed=1, dumps=DUMPS):
    """How far impostor_position in single precision (every register, the word and every operation's result
    rounded, as the console computes) lies from the double reference on the made draws that are not of a
    degenerate kind: (draws, the median, the 99th percentile, the largest) of the largest relative difference
    of oPos's components, |single - double| / max(1, |double|). Reported, not a check: it says what tolerance a
    single-precision mirror pinned to the double reference would need on inputs of this size."""
    text = _text(dumps)
    gaps = []
    for t in range(trials):
        case = made_case(seed, t, text=text)
        if case.kind:
            continue
        quad, k = lf.impostor_corner(case.index)
        word = case_words(case)[quad]
        c = {reg: case.consts[reg] for reg in lf.IMPOSTOR_SHADER["reg"]}
        double = lf.impostor_position(k, word, c)["pos"]
        single = lf.impostor_position(k, word, c, single=True)["pos"]
        gaps.append(max(abs(x - y) / max(1.0, abs(x)) for x, y in zip(double, single)))
    gaps.sort()
    n = len(gaps)
    return (n, gaps[n // 2], gaps[min(n - 1, int(n * 0.99))], gaps[-1]) if n else (0, 0.0, 0.0, 0.0)


# --- capture rows (the rows' own check is plan Task 6's) ----------------------------------------------------------

def impostor_rows(path):
    """[(the "verts" row, its "vertsdata" row or None)] of a capture's in-scene 129F rows, in file order."""
    rows = cr.rows(path, kinds=("verts", "vertsdata"))
    data = {(r.get("frame"), r.get("n")): r for r in rows if r.get("kind") == "vertsdata"}
    out = []
    for r in rows:
        if r.get("kind") == "verts" and r.get("in_scene") and cr.vs_hash(r) == VS:
            out.append((r, data.get((r.get("frame"), r.get("n"))) if "n" in r else None))
    return out


def row_registers(row):
    """{register: [4]} of IMPOSTOR_SHADER["reg"] from a "verts" row's whole vertex bank, rounded to single
    precision (the capture prints nine digits of each). ValueError when the row has no whole bank."""
    bank = row.get("bank") or []
    if len(bank) < 4 * 256:
        raise ValueError("the row has no whole vertex bank (%d floats)" % len(bank))
    return {reg: [math.nan if x is None else f32(x) for x in bank[4 * reg:4 * reg + 4]] for reg in lf.IMPOSTOR_SHADER["reg"]}


def _capture_quads(capture, say, each):
    """Calls each(row, words) for every impostor row of a capture that has its storage; returns the exit code."""
    path = cr.find_capture(capture)
    pairs = impostor_rows(path)
    bad = shown = 0
    for row, data in pairs:
        where = "frame %s line %s" % (row.get("frame"), row.get("line"))
        try:
            if data is None:
                raise ValueError("no \"vertsdata\" row")
            each(where, row, quad_words(row, data))
            shown += 1
        except ValueError as e:
            bad += 1
            say("%s: NOT READ: %s" % (where, e))
    say("%s: %d rows of %s in the scene, %d with their storage" % (path.name, len(pairs), VS[:4], shown))
    if not shown:
        say("no row with its storage: a capture from before plan Task 1, or one without the far woods")
    return 1 if bad or not shown else 0


def cmd_words(args, say=print):
    def each(where, row, words):
        say("%s: %d quads" % (where, len(words)))
        for q, w in enumerate(words):
            say("  %4d  %s" % (q, "  ".join("% .9g" % x for x in w)))
            if not all(math.isfinite(x) for x in w):
                raise ValueError("quad %d holds a value that is no number" % q)
    return _capture_quads(args.capture, say, each)


def cmd_places(args, say=print):
    def each(where, row, words):
        c = row_registers(row)
        say("%s: %d quads, c8.z %.9g, the eye %s" % (where, len(words), c[8][2], "  ".join("%.9g" % x for x in c[47][:3])))
        for q, w in enumerate(words):
            place = lf.impostor_position(0, w, c)["place"]
            distance = math.sqrt(sum((c[47][k] - place[k]) ** 2 for k in range(3)))
            say("  %4d  %s   size %.9g   from the eye %.6g" % (q, "  ".join("% .9g" % x for x in place), w[3], distance))
    return _capture_quads(args.capture, say, each)


# --- command line -------------------------------------------------------------------------------------------------

def _position_line(r):
    return ("position %d / %d; corners %s; above %d, below %d; powered %d, tilted %d; degenerate %s; worst relative error %.3e"
            % (r.trials - r.failed, r.trials, " ".join(str(n) for n in r.corners), r.above, r.below, r.powered, r.tilted,
               ", ".join("%s %d" % kv for kv in sorted(r.kinds.items())) or "none", r.worst))


def position_floors(r):
    """The floors a run of check_position must reach to count (review focus 3), as problems (texts)."""
    out = []
    if min(r.corners) < r.trials // 5:
        out.append("a corner has fewer than a fifth of the trials")
    if min(r.above, r.below) < r.trials // 4:
        out.append("a side of the height comparison has fewer than a quarter of the trials")
    if r.powered < r.trials // 2:
        out.append("the log / exp block is on numbers in fewer than half of the trials")
    if r.tilted < r.trials // 4:
        out.append("both weights are inside (0, 1) in fewer than a quarter of the trials")
    return out


def cmd_formulas(args, say=print):
    bad = 0
    say("vertex shader %s against light_formulas.impostor_position: %d made draws, seed %d; bound %g * max(1, |value|) "
        "in every component of oPos, o0 and o1" % (VS, args.trials, args.seed, TOLERANCE))
    r = check_position(args.trials, args.seed, dumps=args.dumps)
    problems = position_floors(r) + (["first failure: %s" % r.first_failure] if r.failed else [])
    say(_position_line(r) + (": OK" if not problems else ": FAILED"))
    for text in problems:
        say("  " + text)
    bad += bool(problems)
    ieee = check_position(args.trials, args.seed, dumps=args.dumps, zero_product=False)
    say("without the console's product rule in the interpreter: %d of %d disagree (the trials of kind size0, flat: %d)" % (
        ieee.failed, ieee.trials, ieee.kinds.get("size0", 0) + ieee.kinds.get("flat", 0)))
    n, median, p99, largest = single_gap(args.trials, args.seed, args.dumps)
    say("single precision against the double reference (reported, not a check), %d draws that are not degenerate: "
        "median %.2e, 99th percentile %.2e, largest %.2e of max(1, |oPos|)" % (n, median, p99, largest))
    for name in POSITION_MUTATIONS:
        m = check_position(min(args.trials, 400), args.seed, mutation=name, dumps=args.dumps)
        bad += m.failed == 0
        say("  mutation %-10s: %d of %d trials disagree%s" % (name, m.failed, m.trials, "" if m.failed else ": NOT CAUGHT"))
    for name in ("two", "one", "minus_one", "four"):
        alone = check_position(min(args.trials, 400), args.seed, mutation=("literal", name), dumps=args.dumps)
        both = check_position(min(args.trials, 400), args.seed, shift=(name, 1.01), dumps=args.dumps)
        ok = alone.failed > 0 and both.failed == 0
        bad += not ok
        say("  literal %-9s (c%d.%s) by 1%%: in the formula alone %d of %d disagree; in the formula and at its place %d%s" % (
            name, lf.IMPOSTOR_SHADER["vs_lit_at"][name][0], "xyzw"[lf.IMPOSTOR_SHADER["vs_lit_at"][name][1]],
            alone.failed, alone.trials, both.failed, "" if ok else ": NOT HELD"))
    say("all agree" if not bad else "%d checks FAILED" % bad)
    return 1 if bad else 0


def cmd_trace(args, say=print):
    case = made_case(args.seed, args.trial, args.dumps)
    regs = trace(case, args.instr)
    quad, corner = lf.impostor_corner(case.index)
    say("%s, made trial %d of seed %d (vertex index %d: quad %d, corner %d%s), after instruction %d:" % (
        VS, args.trial, args.seed, case.index, quad, corner, ", " + case.kind if case.kind else "", args.instr))
    for reg in sorted(regs):
        if any(regs[reg]):
            say("  r%-2d %s" % (reg, "  ".join("% .9g" % x for x in regs[reg])))
    say("the quad's word (x, y, z, s): %s" % (case_word(case),))
    for reg in lf.IMPOSTOR_SHADER["reg"]:
        say("  c%-3d %s" % (reg, "  ".join("% .9g" % x for x in case.consts[reg])))
    return 0


def main(argv=None, say=print):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dumps", type=Path, default=DUMPS)
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("formulas")
    p.add_argument("--trials", type=int, default=2000)
    p.add_argument("--seed", type=int, default=1)
    p.set_defaults(run=cmd_formulas)
    p = sub.add_parser("trace")
    p.add_argument("--instr", type=int, required=True)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--trial", type=int, default=2)
    p.set_defaults(run=cmd_trace)
    p = sub.add_parser("words")
    p.add_argument("capture")
    p.set_defaults(run=cmd_words)
    p = sub.add_parser("places")
    p.add_argument("capture")
    p.set_defaults(run=cmd_places)
    args = ap.parse_args(argv)
    if args.command in ("formulas", "pixel", "trace") and not args.dumps.is_dir():
        say("no shader dumps at %s (made by fable_2.exe --dump_shaders=<dir>)" % args.dumps)
        return 1
    return args.run(args, say)


if __name__ == "__main__":
    sys.exit(main())
