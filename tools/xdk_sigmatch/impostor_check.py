"""The machine check of the far woods' impostors (light_formulas.impostor_position, impostor_pixel, impostor_out,
impostor_fog) against the game's own microcode (ucode_eval): vertex shader 129F7F2793DA9084 and pixel shader
4B1D3E6E8E58D3AC. The distant-trees sub-project, plan Tasks 4 and 5, spec criterion 1 (its first half: made
inputs; the captured rows are plan Task 6's).

  formulas [--trials 2000] [--seed 1]       the position on made draws, and each mutation (each must fail)
  pixel [--trials 2000] [--seed 1]          the pixel shader and the per-tree fog on made inputs, each mutation,
                                            the pixel once more with the three captured views' constants, and
                                            which bit bool b129 is
  trace --instr N [--seed S] [--trial T]    the interpreter's registers after instruction N of 129F for a made
                                            draw (the transcription's oracle, register by register)
  words <capture> [--quiet]                 the quad words of every in-scene 129F "verts" row with its storage; the
                                            census of every endian and order (plan Task 3, open question 2); which
                                            of the row's two fetch constants names the storage
  places <capture>                          the raised place (o0) of every such quad
  rows <capture>...                         plan Task 6, criterion 1's second half: every quad and corner of every
                                            such row through the microcode and through impostor_position (the row's
                                            own bank, literals and storage), the fog pair, the pixel shader once a
                                            quad, and each position mutation (each must fail on the capture)
  identities <capture>...                   what the rows' registers and tables equal in their frame (reported)
  bounds <capture>...                       the smallest and largest of every register component and of the size
Each exits 1 on any failure (words, places and rows also when the capture has no such row with its storage).

What `rows` shows and what it does not. Both sides take the SAME captured registers and bytes: it shows that the
formula is the microcode on the game's own values (and how far the single-precision reference lies from the double
one there: ledger ruling 26), not that the bytes were read at the right moment or in the right order; that is the
rasterising replay's (foliage_replay.py impostors: the quads against the game's own depth).

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

What one pixel trial is (`pixel`, check_pixel). The pixel shader's microcode runs with bool b129 clear on made
interpolators (o0 the tree's raised place, o1 the picture's coordinate, o2 and o3 a fog pair: a real pair in three
trials of four, (1, 0) otherwise) and a made picture (a smooth function of its coordinate, so that a fetch at
another place would show; any other texture is an error). Beside it impostor_distance(o0, c19),
impostor_pixel(the picture at o1, ...) and impostor_out give oC0; the bound is PIXEL_TOLERANCE * max(1, |value|)
in each of its four components. Of 100 trials the distance is drawn nearer than the dissolve's start (c29.x) in
15, inside the dissolve in 30, up to 200 units beyond it in 25, inside the light's fall (c12.y to c12.y + 10) in
20 and anywhere within 400 units in 10; the registers within the ranges at this file's head, or, with `view`,
the c12, c29, c31.x and c20.w of one of the three captured views (CAPTURED_VIEWS). Four trials of 40 are degenerate
(PIXEL_KINDS): "texel_nan" a picture alpha that is no number on an absent tree (0 * no number), "light_inf" an
infinite c12.w on a tree nearer than c12.y (0 * infinity), "colour_inf" an infinite picture colour with c31.x = 0;
an IEEE machine disagrees in these three and nowhere else; "place_nan" a place that is no number (both
saturates give 0: the colour is the picture's times c31.x, the alpha 0).

What one fog trial is (check_fog). The position check's made draw; the microcode's o2 and o3 against
impostor_fog at impostor_position's raised place (not at the microcode's own o0) with the eye c47, the two
tables the case's on both sides, and the coordinates of the microcode's two fetches against the formula's. One
draw in four lies before the start: no fetch, exactly (1, 1, 1) and (0, 0, 0).

PIXEL_MUTATIONS, each of which must fail: "start" c12.y for c29.x; "tenth" the literal 0.1 by 1%; "exposure"
c31.x left out; "sign" the light factor's sign ((1 - c12.w) for (c12.w - 1)); "picture" the alpha without the
picture's; and of the fog (FOG_MUTATIONS, through check_fog) "one_minus" the (1 - o2) left out, "c8x" a c8.x
factor put in (the trees' vertex_fog).
"""
import argparse
import itertools
import math
import random
import struct
import sys
from collections import Counter, namedtuple
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

# The pixel check (module text).
PIXEL_TOLERANCE = 1e-5      # spec Design 5: within 1e-5 of max(1, |value|)
PIXEL_MUTATIONS = ("start", "tenth", "exposure", "sign", "picture", "one_minus", "c8x")
FOG_MUTATIONS = PIXEL_MUTATIONS[5:]         # these two are the vertex shader's: check_fog
LIGHT_START, LIGHT_FAR = (10.0, 60.0), (0.0, 1.5)           # c12.y, c12.w
DISSOLVE_START, DISSOLVE_SLOPE = (20.0, 80.0), (0.02, 1.0)  # c29.x, c29.y
EXPOSURE_UNDO = (0.05, 2.0)                                 # c31.x
OUT_SCALES = (1.0, 2.5, 8.62517, 16.0)                      # c20.w
PIXEL_KINDS = {5: "texel_nan", 17: "light_inf", 29: "colour_inf", 37: "place_nan"}      # by trial % 40
# Pixel c12, c29, c31.x and c20.w of the 129F rows of three captures, the same in every row of a frame
# (out/native-evidence/distant-trees/impostor_consts.txt, which prints six digits of each): the bridge, the held
# sunward view, the third spot.
CAPTURED_VIEWS = {
    "054648": {"c12": (20.0, 35.0, 2.0, 0.46779), "c29": (48.0, 0.125, 998.4, 25.6), "c31x": 0.366827, "c20w": 8.62517},
    "074119": {"c12": (20.0, 35.0, 2.0, 0.498972), "c29": (48.0, 0.125, 998.4, 25.6), "c31x": 0.357012, "c20w": 7.29878},
    "022921": {"c12": (20.0, 35.0, 2.0, 0.488728), "c29": (48.0, 0.125, 998.4, 25.6), "c31x": 0.359691, "c20w": 16.0},
}
CAPTURED_PBOOL = 0x00000100     # the rows' "pbool" (the device's shadow of the pixel bool word) in every 129F row there

# trials; those that disagree; those with a real fog pair in o2, o3; by the distance: nearer than the dissolve's
# start, inside the dissolve, beyond it; with the light factor strictly between its ends; the largest relative
# error; the first failure's text (None); the trials of each degenerate kind.
PixelResult = namedtuple("PixelResult", "trials failed fogged absent dissolving whole falling worst first_failure kinds")
# trials; those that disagree; those in which the microcode fetched the tables; those before the start; those not
# compared (a raised place that is no number); the begun ones whose two fetch coordinates are the formula's; the
# largest relative error; the first failure's text (None).
FogResult = namedtuple("FogResult", "trials failed fogged near unnumbered coords worst first_failure")

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
    (fixed by plan Task 3: endian 2, order (1, 0, 3, 2)). ValueError: the storage was not read, is no hex, is shorter than the quads
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


# --- the pixel shader ---------------------------------------------------------------------------------------------

class PixelCase:
    """One pixel of 4B1D: the microcode's text, its constants, the four interpolators ({0: o0, the raised place;
    1: o1, the picture's coordinate; 2: o2 and 3: o3, the fog pair}, each four floats), the picture (a callable
    (u, v) -> four values), the made distance, whether the fog pair is a real pair, and the kind ("" or a name
    of PIXEL_KINDS)."""

    def __init__(self, text, consts, regs, picture, distance, fogged, kind=""):
        self.text, self.consts, self.regs, self.picture, self.distance, self.fogged, self.kind = (
            text, consts, regs, picture, distance, fogged, kind)


def made_pixel(seed=1, trial=0, dumps=DUMPS, text=None, view=None):
    """The made pixel of one trial (deterministic in seed and trial). `view`: a name of CAPTURED_VIEWS, whose c12,
    c29, c31.x and c20.w then stand in the made ones' place."""
    rng = random.Random("impostor-pixel/%d/%d" % (seed, trial))
    text = text if text is not None else _text(dumps, PS, "frag")
    kind = PIXEL_KINDS.get(trial % 40, "")
    c = {reg: [f32(rng.uniform(-1.0, 1.0)) for _ in range(4)] for reg in range(248)}
    c[12][1], c[12][3] = f32(rng.uniform(*LIGHT_START)), f32(rng.uniform(*LIGHT_FAR))
    c[29][0], c[29][1] = f32(rng.uniform(*DISSOLVE_START)), f32(rng.uniform(*DISSOLVE_SLOPE))
    c[31][0] = f32(rng.uniform(*EXPOSURE_UNDO))
    c[20][3] = f32(rng.choice(OUT_SCALES))
    if view is not None:
        v = CAPTURED_VIEWS[view]
        c[12], c[29] = [f32(x) for x in v["c12"]], [f32(x) for x in v["c29"]]
        c[31][0], c[20][3] = f32(v["c31x"]), f32(v["c20w"])
    c[19] = [f32(rng.uniform(-PLACE_NEAR, PLACE_NEAR)) for _ in range(3)] + [1.0]
    band = 1.0 / c[29][1]
    where = rng.random()
    if where < 0.15 or kind == "texel_nan":
        distance = rng.uniform(0.0, c[29][0])                               # absent
    elif where < 0.45:
        distance = c[29][0] + rng.uniform(0.0, band)                        # dissolving
    elif where < 0.7:
        distance = c[29][0] + band + rng.uniform(0.0, 200.0)                # whole
    elif where < 0.9:
        distance = c[12][1] + rng.uniform(0.0, 1.0 / lf.impostor_literals("ps")["tenth"])     # the light falling
    else:
        distance = rng.uniform(0.0, 400.0)
    if kind == "light_inf":
        distance = rng.uniform(0.0, c[12][1])
        c[12][3] = math.inf
    way = _unit(rng)
    place = [f32(c[19][k] + distance * way[k]) for k in range(3)]
    if kind == "place_nan":
        place[trial // 40 % 3] = math.nan
    fogged = rng.random() < 0.75
    factor = [f32(rng.uniform(0.05, 1.0)) for _ in range(3)] if fogged else [1.0, 1.0, 1.0]
    add = [f32(rng.uniform(0.0, 2.0)) for _ in range(3)] if fogged else [0.0, 0.0, 0.0]
    regs = {0: place + [f32(rng.uniform(-1.0, 1.0))],
            1: [f32(rng.uniform(-0.5, 1.5)), f32(rng.uniform(-0.5, 1.5)), f32(rng.uniform(-1.0, 1.0)), f32(rng.uniform(-1.0, 1.0))],
            2: factor + [f32(rng.uniform(-1.0, 1.0))], 3: add + [f32(rng.uniform(-1.0, 1.0))]}
    smooth = _smooth(rng, 4)
    picture = smooth
    if kind == "texel_nan":
        picture = lambda u, v: smooth(u, v)[:3] + [math.nan]                # noqa: E731
    elif kind == "colour_inf":
        c[31][0] = 0.0
        picture = lambda u, v: [math.inf, -math.inf, math.inf] + smooth(u, v)[3:]      # noqa: E731
    c.update(_literal_registers("ps"))
    return PixelCase(text, c, regs, picture, distance, fogged, kind)


def run_pixel(case, consts=None, zero_product=True, bools=()):
    """(the pixel shader's outputs, the machine). tf0 is the case's picture at the coordinate the microcode
    fetches at; any other texture is an error (tf11 and tf12 are read only under bool b129)."""

    def sampler(slot, coords, attrs):
        if slot == 0:
            return list(case.picture(coords[0], coords[1]))
        raise KeyError("pixel texture tf%d" % slot)

    m = ue.Machine(case.text, consts or case.consts, sampler, regs={r: list(v) for r, v in case.regs.items()},
                   bools=bools, zero_product=zero_product)
    return m.run(), m


def formula_pixel(case, mutation=None, lit=None, consts=None):
    """impostor_out of impostor_pixel for the case, and the alpha: oC0 as a list of four. The picture is read at
    interpolator o1, the distance is impostor_distance of o0 and c19. `mutation`: one of PIXEL_MUTATIONS' first
    five or ("literal", name)."""
    c = consts or case.consts
    lit = dict(lit or lf.impostor_literals("ps"))
    if isinstance(mutation, tuple):
        lit[mutation[1]] *= 1.01
    texel = list(case.picture(case.regs[1][0], case.regs[1][1]))
    c12, c29, c31x = list(c[12]), list(c[29]), c[31][0]
    if mutation == "start":
        c29[0] = c12[1]
    elif mutation == "tenth":
        lit["tenth"] *= 1.01
    elif mutation == "exposure":
        c31x = 1.0
    elif mutation == "sign":
        c12[3] = 2.0 * lit["one"] - c12[3]                  # (c12.w - 1) with its sign changed
    elif mutation == "picture":
        texel[3] = 1.0
    d = lf.impostor_distance(case.regs[0][:3], c[19][:3])
    colour, alpha = lf.impostor_pixel(texel, d, c12, c29, c31x, lit=lit)
    return list(lf.impostor_out(colour, case.regs[2][:3], case.regs[3][:3], c[20][3])) + [alpha]


def check_pixel(trials=2000, seed=1, mutation=None, dumps=DUMPS, shift=None, zero_product=True, view=None):
    """`trials` made pixels of 4B1D with bool b129 clear against impostor_out(impostor_pixel(...)): PixelResult.
    The bound: PIXEL_TOLERANCE * max(1, |value|) in every component of oC0."""
    _check_mutation(mutation, PIXEL_MUTATIONS[:5], lf.IMPOSTOR_SHADER["ps_lit_at"])
    text = _text(dumps, PS, "frag")
    failed = fogged = absent = dissolving = whole = falling = 0
    worst, first, kinds = 0.0, None, {}
    for k in range(trials):
        case = made_pixel(seed, k, text=text, view=view)
        consts, lit = case.consts, lf.impostor_literals("ps")
        if shift is not None:
            consts, lit = _shifted(consts, "ps", lit, shift)
        out, m = run_pixel(case, consts=consts, zero_product=zero_product)
        got = [float(x) for x in out["oC0"]]
        want = formula_pixel(case, mutation, lit, consts)
        ok, error = _compare(got, want, PIXEL_TOLERANCE)
        ok = ok and [f[1] for f in m.fetches] == [0] and list(m.fetches[0][2]) == case.regs[1][:2]
        worst = max(worst, error)
        past = (case.distance - consts[29][0]) * consts[29][1]
        fall = (case.distance - consts[12][1]) * lf.impostor_literals("ps")["tenth"]
        fogged += case.fogged
        absent += past <= 0.0
        dissolving += 0.0 < past < 1.0
        whole += past >= 1.0
        falling += 0.0 < fall < 1.0
        if case.kind:
            kinds[case.kind] = kinds.get(case.kind, 0) + 1
        if not ok:
            failed += 1
            first = first or "trial %d (seed %d%s, %.6g units): relative error %.3e; microcode %s, formula %s" % (
                k, seed, ", " + case.kind if case.kind else "", case.distance, error, got, want)
    return PixelResult(trials, failed, fogged, absent, dissolving, whole, falling, worst, first, kinds)


def b129_reads_the_ground_fog(dumps=DUMPS, seed=1, trial=0):
    """Whether the pixel shader with bool b129 set reaches the ground-fog block (its first fetch is tf12, which a
    made pixel does not have): the block the formula does not model."""
    try:
        run_pixel(made_pixel(seed, trial, dumps), bools={129})
    except KeyError as e:
        return "tf12" in str(e)
    return False


# --- the fog of the vertex shader -----------------------------------------------------------------------------

def check_fog(trials=2000, seed=1, mutation=None, dumps=DUMPS, zero_product=True):
    """`trials` made draws of 129F (made_case: the position check's draws), the microcode's o2 and o3 against
    impostor_fog at impostor_position's raised place with the eye c47: FogResult. The two tables are the case's
    (tf16 a random 256-texel table read by lf.table_1d, tf17 a random smooth function of its two coordinates) on
    both sides; the coordinates of the microcode's two fetches must be the formula's. A draw before the start
    must fetch nothing and give exactly (1, 1, 1) and (0, 0, 0). A draw whose raised place is no number
    ("unnumbered": the kinds nan and inf) is not compared: the formula gives the pair of "before the start" there
    by its guard, the microcode fetches at a coordinate that is no number."""
    if mutation is not None and mutation not in FOG_MUTATIONS:
        raise ValueError("unknown fog mutation %r (one of %s)" % (mutation, ", ".join(FOG_MUTATIONS)))
    text = _text(dumps)
    failed = fogged = near = unnumbered = coords_ok = 0
    worst, first = 0.0, None
    for k in range(trials):
        case = made_case(seed, k, text=text)
        c = {reg: case.consts[reg] for reg in lf.IMPOSTOR_SHADER["reg"]}
        quad, corner = lf.impostor_corner(case.index)
        place = lf.impostor_position(corner, case_words(case)[quad], c)["place"]
        if not all(math.isfinite(x) for x in place):
            unnumbered += 1
            continue
        out, m = run_microcode(case, zero_product=zero_product)
        got = [float(x) for x in out["o2"][:3]] + [float(x) for x in out["o3"][:3]]
        seen = {}

        def t16(s):
            seen[16] = (s,)
            return case.tf16(s)

        def t17(a, t):
            seen[17] = (a, t)
            seen["scatter"] = case.tf17(a, t)
            return seen["scatter"]

        if mutation == "c8x":
            factor, add = lf.vertex_fog(place, c[47][:3], c[8][0], c[110], c[111], t16, t17)
        else:
            factor, add = lf.impostor_fog(place, c[47][:3], c[110], c[111], t16, t17)
            if mutation == "one_minus" and "scatter" in seen:
                add = list(seen["scatter"][:3])
        want = list(factor) + list(add)
        fetched = {f[1]: f[2] for f in m.fetches}
        if not fetched:
            near += 1
            ok, error = (not seen and got == [1.0, 1.0, 1.0, 0.0, 0.0, 0.0] == want), 0.0
        else:
            fogged += 1
            same = (set(fetched) == {16, 17} and set(seen) == {16, 17, "scatter"}
                    and abs(seen[16][0] - fetched[16][0]) <= 1e-9 * max(1.0, abs(fetched[16][0]))
                    and all(abs(a - b) <= 1e-9 for a, b in zip(seen[17], fetched[17])))
            coords_ok += same
            ok, error = _compare(got, want, PIXEL_TOLERANCE)
            ok = ok and same
        worst = max(worst, error)
        if not ok:
            failed += 1
            first = first or "trial %d (seed %d%s): relative error %.3e; microcode o2, o3 %s, formula %s; fetched at %s, formula at %s" % (
                k, seed, ", " + case.kind if case.kind else "", error, got, want, fetched, {s: seen[s] for s in (16, 17) if s in seen})
    return FogResult(trials, failed, fogged, near, unnumbered, coords_ok, worst, first)


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


def gpu_address(cpu_virtual):
    """The GPU's physical address of a guest virtual one (src/native/capture/xdk_layout.h, GpuAddress: the
    0xE0000000 view of 4 KB pages lies 0x1000 above its address's low 29 bits)."""
    return (cpu_virtual & 0x1FFFFFFF) + (((cpu_virtual >> 20) + 0x200) & 0x1000)


def storage_fetch(row):
    """Which of a "verts" row's two vertex fetch constants names the row's storage: {"shadow": bool or None,
    "packet": bool or None, "endian": the naming pair's endian or None}. A constant (six dwords: fetch slot 31's
    three pairs) names the storage when its LAST pair (vertex fetch constant 95, the one BeginVertices writes) has
    the address gpu_address(storage), type 3 and the size storage_dwords; None for a constant the row does not
    have."""
    out = {"shadow": None, "packet": None, "endian": None}
    try:
        storage, dwords = int(row["storage"], 16), int(row["storage_dwords"])
    except (KeyError, TypeError, ValueError):
        return out
    for name in ("shadow", "packet"):
        six = (row.get("vfetch") or {}).get(name)
        if not six or len(six) != 6:
            continue
        a, b = int(six[4], 16), int(six[5], 16)
        out[name] = (a & 0xFFFFFFFC) == gpu_address(storage) and (a & 3) == 3 and (b >> 2) & 0xFFFFFF == dwords
        if out[name]:
            out["endian"] = b & 3
    return out


def pixel_register(row, reg):
    """Pixel register `reg` of a row as four floats from its "pfull" (the registers' bits, 32 hex digits each), or
    None when the row does not have it."""
    text = (row.get("pfull") or "")[32 * reg:32 * reg + 32]
    if len(text) < 32 or "-" in text:
        return None
    try:
        return list(struct.unpack(">4f", bytes.fromhex(text)))
    except ValueError:
        return None


def row_eye(row):
    """The frame's eye at a row: pixel c19, or vertex c47 for a row without its pixel registers."""
    c19 = pixel_register(row, 19)
    return c19[:3] if c19 is not None else row_registers(row)[47][:3]


WORD_SIZE_MAX, WORD_REACH = 1000.0, 10000.0     # plan Task 3: a size in (0, 1000], a place within 10,000 units of the eye
ORDERS = tuple(itertools.permutations(range(4)))


def word_plausible(word, eye):
    """Plan Task 3's rule for one decoded quad: four finite values, a size in (0, WORD_SIZE_MAX], a place within
    WORD_REACH of the eye."""
    return (all(math.isfinite(x) for x in word) and 0.0 < word[3] <= WORD_SIZE_MAX
            and math.sqrt(sum((word[k] - eye[k]) ** 2 for k in range(3))) <= WORD_REACH)


def word_census(pairs):
    """(quads, {(endian, order): the quads that decode plausibly}) over the rows of `pairs` ([(verts row, its
    vertsdata row)]) for every endian 0 to 3 and every order of the four halves. ValueError as quad_words."""
    held, quads = {(e, o): 0 for e in _SWAPS for o in ORDERS}, 0
    for row, data in pairs:
        eye = row_eye(row)
        quads += int(row["count"]) // 4
        for e in _SWAPS:
            plain = quad_words(row, data, source=(0, 1, 2, 3), endian=e)
            for o in ORDERS:
                held[(e, o)] += sum(1 for w in plain if word_plausible(tuple(w[s] for s in o), eye))
    return quads, held


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
    """The quads' words by the table's order and endian, then plan Task 3's census (open question 2): how many
    quads decode plausibly under each endian and order, and where the fetch constant that names the storage is.
    Exit 1 when a row's storage is not read, a word of the table's decoding is no number or not plausible, or a
    row's naming constant has another endian than the table's."""
    def each(where, row, words):
        say("%s: %d quads" % (where, len(words)))
        eye = row_eye(row)
        for q, w in enumerate(words):
            if not getattr(args, "quiet", False):
                say("  %4d  %s" % (q, "  ".join("% .9g" % x for x in w)))
            if not word_plausible(w, eye):
                raise ValueError("quad %d does not decode to a finite place within %g of the eye with a size in (0, %g]: %s" % (
                    q, WORD_REACH, WORD_SIZE_MAX, w))
    code = _capture_quads(args.capture, say, each)
    pairs = [(r, d) for r, d in impostor_rows(cr.find_capture(args.capture)) if d is not None and d.get("read")]
    if not pairs:
        return 1
    quads, held = word_census(pairs)
    whole = sorted(k for k, n in held.items() if n == quads)
    table = (lf.IMPOSTOR_SHADER["word_endian"], tuple(lf.IMPOSTOR_SHADER["word_source"]))
    say("the census: %d quads; of the %d (endian, order) pairs %d decode EVERY quad plausibly (finite, a size in (0, %g], "
        "a place within %g of the eye)%s" % (quads, len(held), len(whole), WORD_SIZE_MAX, WORD_REACH,
                                           "" if len(whole) == 1 else ": the rule does not single one out"))
    for e in sorted(_SWAPS):
        mine = [o for (ee, o) in whole if ee == e]
        say("  endian %d: %d orders hold every quad; the most any order holds: %d of %d" % (
            e, len(mine), max(held[(e, o)] for o in ORDERS), quads))
    say("  the table's (endian %d, order %s): %d of %d" % (table[0], table[1], held[table], quads))
    named = Counter()
    for row, _ in pairs:
        s = storage_fetch(row)
        named["shadow %s, packet %s, endian %s" % (s["shadow"], s["packet"], s["endian"])] += 1
        if s["endian"] is not None and s["endian"] != table[0]:
            code = 1
    for text, n in sorted(named.items()):
        say("  the fetch constant that names the storage (address gpu_address(storage), type 3, size storage_dwords): %s: %d rows" % (text, n))
    return 1 if code or held[table] != quads else 0


def cmd_places(args, say=print):
    def each(where, row, words):
        c = row_registers(row)
        say("%s: %d quads, c8.z %.9g, the eye %s" % (where, len(words), c[8][2], "  ".join("%.9g" % x for x in c[47][:3])))
        for q, w in enumerate(words):
            place = lf.impostor_position(0, w, c)["place"]
            distance = math.sqrt(sum((c[47][k] - place[k]) ** 2 for k in range(3)))
            say("  %4d  %s   size %.9g   from the eye %.6g" % (q, "  ".join("% .9g" % x for x in place), w[3], distance))
    return _capture_quads(args.capture, say, each)


# --- the formulas against captured rows (plan Task 6) ---------------------------------------------------------------

SINGLE_TOLERANCE = 1e-4     # ledger ruling 26: criterion 1's bound for a single-precision mirror, against impostor_position(single=True)
ROW_MUTATIONS = POSITION_MUTATIONS

# rows; quads; corners compared; corners that disagree; rows whose storage was not read; the largest relative error of
# the position (double formula against the microcode); the largest distance of the single-precision reference from
# the double one, of max(1, |oPos|); the first failure's text; corners above / below by instr 31; corners with both
# powers on numbers; the fog: corners compared, that disagree, with the tables fetched, the largest error; the
# pixel: quads compared, that disagree, the largest error, by the dissolve (absent, dissolving, whole); the
# registers the two machines read (sets).
RowsResult = namedtuple("RowsResult", "rows quads corners failed unread worst single first_failure above below powered "
                                      "fog fog_failed fogged fog_worst pixels pixel_failed pixel_worst absent dissolving whole "
                                      "vs_used ps_used")


def row_literals(row, kind="vs"):
    """{register: [4]} of a row's own shader literals ("vs_lit" / "ps_lit": blocks of 16 floats from a register;
    the pixel shader's are numbered from 256)."""
    out = {}
    for block in row.get(kind + "_lit") or []:
        first = int(block["reg"]) - (256 if kind == "ps" else 0)
        values = block["v"]
        for i in range(len(values) // 4):
            out[first + i] = [f32(x) for x in values[4 * i:4 * i + 4]]
    return out


def row_bank(row):
    """{register: [4]} of a row's whole vertex bank with the shader's own literals laid over it (the bank's literal
    registers are another draw's)."""
    bank = row.get("bank") or []
    if len(bank) < 4 * 256:
        raise ValueError("the row has no whole vertex bank (%d floats)" % len(bank))
    c = {reg: [math.nan if x is None else f32(x) for x in bank[4 * reg:4 * reg + 4]] for reg in range(256)}
    c.update(row_literals(row, "vs"))
    return c


def row_pixel_bank(row):
    """{register: [4]} of a row's pixel registers ("pfull") with the pixel shader's own literals laid over them; a
    register the row does not have is left out."""
    c = {}
    for reg in range(256):
        v = pixel_register(row, reg)
        if v is not None:
            c[reg] = v
    c.update(row_literals(row, "ps"))
    return c


def frame_tables(path):
    """{frame: (tf16, tf17)} from a capture's frame rows: the fog block's two tables as the frame row prints them
    ("transmit": tf5's texels, "scatter": tf4's; three floats a texel), tf16 = tf5 and tf17(a, t) = tf4(a) (the
    identity `identities` holds per row). A frame without them has no entry."""
    out = {}
    for r in cr.rows(path, kinds=("frame",)):
        fog = r.get("fog") or {}
        t, s = fog.get("transmit") or [], fog.get("scatter") or []
        if len(t) >= 3 and len(s) >= 3 and len(t) % 3 == 0 and len(s) % 3 == 0:
            tf5 = lf.table_1d([t[i:i + 3] for i in range(0, len(t), 3)])
            tf4 = lf.table_1d([s[i:i + 3] for i in range(0, len(s), 3)])
            out[r.get("frame")] = (tf5, lambda a, _t, tf4=tf4: tf4(a))
    return out


def _mutated(mutation, k, word, c):
    """(corner, word, registers) of one of ROW_MUTATIONS applied on the formula's side."""
    c = {reg: list(v) for reg, v in c.items()}
    if mutation == "rows":
        c[115] = list(c[116])
    elif mutation == "components":
        word = (word[1], word[0], word[2], word[3])
    elif mutation == "sign":
        word = (word[0], word[1], word[2], -word[3])
    elif mutation == "corners":
        k = {0: 1, 1: 0, 2: 3, 3: 2}[k]
    elif mutation == "exponent":
        c[114][1] *= 1.01
    elif mutation == "raise":
        c[8][2] = 0.0
    elif mutation == "matrix":
        c[72], c[73] = c[73], c[72]
    return k, word, c


def _nine(out):
    return list(out["pos"]) + list(out["place"]) + list(out["uv"])


def check_rows(path, mutations=(), dumps=DUMPS, pixel=True, cache=None):
    """Every quad and corner of every in-scene 129F row of a capture: the row's own "bank" (its literals from the
    row's "vs_lit") and "vertsdata" through the microcode and through impostor_position, criterion 1's bound
    (TOLERANCE of max(1, |value|), every component of oPos, o0, o1); the microcode's o2 and o3 against impostor_fog
    with the frame's tables; and once a quad the pixel shader (the row's pixel registers and "ps_lit", the bools of
    the row's "pbool_load", the interpolators the vertex microcode gave for that corner, a made smooth picture)
    against impostor_out(impostor_pixel). Returns (RowsResult, {mutation: corners that disagree}): each mutation is
    applied on the formula's side and compared with the same microcode outputs."""
    text, ptext = _text(dumps), _text(dumps, PS, "frag")
    tables = frame_tables(path)
    pairs = impostor_rows(path)
    quads = corners = failed = unread = above = below = powered = 0
    fog = fog_failed = fogged = pixels = pixel_failed = absent = dissolving = whole = 0
    worst = single = fog_worst = pixel_worst = 0.0
    first, vs_used, ps_used = None, set(), set()
    caught = {m: 0 for m in mutations}
    for row, data in pairs:
        where = "frame %s line %s" % (row.get("frame"), row.get("line"))
        try:
            if data is None:
                raise ValueError("no \"vertsdata\" row")
            words = quad_words(row, data)
            consts = row_bank(row)
        except ValueError as e:
            unread += 1
            first = first or "%s: NOT READ: %s" % (where, e)
            continue
        c = {reg: consts[reg] for reg in lf.IMPOSTOR_SHADER["reg"]}
        storage = bytes.fromhex(data["bytes"])
        tf16, tf17 = tables.get(row.get("frame"), (None, None))
        pconsts = row_pixel_bank(row) if pixel else None
        bools = {128 + b for b in range(32) if int(row.get("pbool_load", "0x0"), 16) >> b & 1}
        rng = random.Random("impostor-rows/%s" % row.get("line"))
        picture = _smooth(rng, 4)
        quads += len(words)
        for q, word in enumerate(words):
            for k in range(4):
                case = Case(text, consts, storage, 4 * q + k, tf16, tf17)
                out, m = run_microcode(case)
                vs_used |= m.used_consts
                got = [float(x) for x in out["oPos"]] + [float(x) for x in out["o0"][:3]] + [float(x) for x in out["o1"][:2]]
                ref = lf.impostor_position(k, word, c)
                ok, error = _compare(got, _nine(ref), TOLERANCE)
                corners += 1
                worst = max(worst, error)
                one = lf.impostor_position(k, word, c, single=True)["pos"]
                single = max([single] + [abs(a - b) / max(1.0, abs(a)) for a, b in zip(ref["pos"], one) if a == a and b == b])
                up = m.r[2][3] == 1.0
                above += up
                below += not up
                ev, eh = m.r[8][0], m.r[8][1]
                powered += math.isfinite(ev) and math.isfinite(eh) and ev > 0.0 and eh > 0.0
                if not ok:
                    failed += 1
                    first = first or "%s quad %d corner %d: relative error %.3e; microcode %s, formula %s" % (where, q, k, error, got, _nine(ref))
                for name in mutations:
                    mk, mword, mc = _mutated(name, k, word, c)
                    caught[name] += not _compare(got, _nine(lf.impostor_position(mk, mword, mc)), TOLERANCE)[0]
                if tf16 is not None:
                    factor, add = lf.impostor_fog(ref["place"], c[47][:3], c[110], c[111], tf16, tf17)
                    fok, ferror = _compare([float(x) for x in out["o2"][:3]] + [float(x) for x in out["o3"][:3]],
                                           list(factor) + list(add), PIXEL_TOLERANCE)
                    fog += 1
                    fogged += bool(m.fetches)
                    fog_worst = max(fog_worst, ferror)
                    if not fok:
                        fog_failed += 1
                        first = first or "%s quad %d corner %d: the fog: relative error %.3e" % (where, q, k, ferror)
                if pixel and k == q % 4:
                    regs = {0: [float(x) for x in out["o0"]], 1: [float(x) for x in out["o1"]],
                            2: [float(x) for x in out["o2"]], 3: [float(x) for x in out["o3"]]}
                    pcase = PixelCase(ptext, pconsts, regs, picture, 0.0, True)
                    pout, pm = run_pixel(pcase, bools=bools)
                    ps_used |= pm.used_consts
                    want = formula_pixel(pcase)
                    pok, perror = _compare([float(x) for x in pout["oC0"]], want, PIXEL_TOLERANCE)
                    pok = pok and [f[1] for f in pm.fetches] == [0]
                    d = lf.impostor_distance(regs[0][:3], pconsts[19][:3])
                    past = (d - pconsts[29][0]) * pconsts[29][1]
                    absent += past <= 0.0
                    dissolving += 0.0 < past < 1.0
                    whole += past >= 1.0
                    pixels += 1
                    pixel_worst = max(pixel_worst, perror)
                    if not pok:
                        pixel_failed += 1
                        first = first or "%s quad %d: the pixel: relative error %.3e; microcode %s, formula %s" % (where, q, perror, list(pout["oC0"]), want)
    result = RowsResult(len(pairs), quads, corners, failed, unread, worst, single, first, above, below, powered,
                        fog, fog_failed, fogged, fog_worst, pixels, pixel_failed, pixel_worst, absent, dissolving, whole,
                        frozenset(vs_used), frozenset(ps_used))
    return result, caught


def rows_problems(r, caught):
    """What makes a run of check_rows fail, as texts: a row not read, a corner, fog pair or pixel that disagrees,
    nothing compared, a mutation that no corner catches."""
    out = []
    if r.unread:
        out.append("%d rows without their storage or their bank" % r.unread)
    if not r.corners:
        out.append("no corner was compared")
    if r.failed or r.fog_failed or r.pixel_failed:
        out.append("%d corners, %d fog pairs, %d pixels disagree; the first: %s" % (r.failed, r.fog_failed, r.pixel_failed, r.first_failure))
    elif r.first_failure:
        out.append(r.first_failure)
    out.extend("mutation %s is NOT CAUGHT" % name for name, n in caught.items() if n == 0)
    return out


def cmd_rows(args, say=print):
    bad = 0
    for capture in args.capture:
        path = cr.find_capture(capture)
        r, caught = check_rows(path, ROW_MUTATIONS, args.dumps)
        problems = rows_problems(r, caught)
        say("%s: %d rows of %s, %d quads, %d corners: %d agree (bound %g of max(1, |value|), oPos, o0, o1); worst relative "
            "error %.3e; above %d, below %d; both powers on numbers %d%s" % (
                path.name, r.rows, VS[:4], r.quads, r.corners, r.corners - r.failed, TOLERANCE, r.worst, r.above, r.below,
                r.powered, ": OK" if not problems else ": FAILED"))
        say("  the single-precision reference (ruling 26) against the double one on these registers: at most %.3e of "
            "max(1, |oPos|) (bound for a mirror %g)" % (r.single, SINGLE_TOLERANCE))
        say("  the fog (o2, o3 against impostor_fog at the raised place, the frame's tables): %d of %d corners agree, the "
            "tables fetched at %d; worst %.3e" % (r.fog - r.fog_failed, r.fog, r.fogged, r.fog_worst))
        say("  the pixel shader, once a quad (the row's pixel registers, bools of \"pbool_load\", the vertex microcode's own "
            "interpolators, a made picture): %d of %d agree; absent %d, dissolving %d, whole %d; worst %.3e" % (
                r.pixels - r.pixel_failed, r.pixels, r.absent, r.dissolving, r.whole, r.pixel_worst))
        say("  registers read: vertex %s; pixel %s" % (" ".join("c%d" % x for x in sorted(r.vs_used)), " ".join("c%d" % x for x in sorted(r.ps_used))))
        for name in ROW_MUTATIONS:
            say("  mutation %-10s: %d of %d corners disagree%s" % (name, caught[name], r.corners, "" if caught[name] else ": NOT CAUGHT"))
        for text in problems:
            say("  " + text)
        bad += bool(problems)
    say("all agree" if not bad else "%d captures FAILED" % bad)
    return 1 if bad else 0


def _bits(values):
    return [struct.unpack("<I", struct.pack("<f", x))[0] for x in values]


def _scene_rows(path, frame):
    return cr.rows(path, kinds=("draw", "tess", "indx", "verts", "frame"), frame=frame, drop=("ptiles", "pbank_diff", "positions", "device_dwords", "idx"))


def identities(path):
    """Plan Task 6, open questions 5 and 7, per in-scene 129F row: [(name, rows that hold, rows, the largest
    difference or a note)]."""
    pairs = impostor_rows(path)
    frames = sorted({r.get("frame") for r, _ in pairs})
    names = ("c72..c75 times the rows c115..c117 (and 0 0 0 1) equal the scene's view-projection c0..c3 of the frame's first world-space (tess) row, within 1e-5 of the row's largest entry",
             "vertex c47.xyz bit-equal to pixel c19.xyz",
             "vertex slot 16 is the frame's tf5 and slot 17 its tf4, by base address and content",
             "c110.xyz and c111 bit-equal to the frame's fog c66 and c67 (pixel c66 / c67 of the frame row)",
             "pixel c31.x times a pixel c20.w of the frame's rows at pitch 320 outside the scene (the pictures' own renders among them) is 1 within 1e-5",
             "pixel c31.x times the row's own c20.w is 1 within 1e-5")
    held, worst, note = [0] * len(names), [0.0] * len(names), [""] * len(names)
    total = 0
    for frame in frames:
        rows = _scene_rows(path, frame)
        frame_row = next((r for r in rows if r.get("kind") == "frame"), {})
        fog = frame_row.get("fog") or {}
        tess = next((r for r in rows if r.get("kind") == "tess" and r.get("in_scene") and all("c%d" % i in (r.get("consts") or {}) for i in range(4))), None)
        render_scale = [pixel_register(r, 20) for r in rows if r.get("kind") in ("draw", "indx") and not r.get("in_scene")
                        and int(r.get("surface_info", "0x0"), 16) & 0x3FFF == 320]
        render_scale = sorted({round(v[3], 6) for v in render_scale if v is not None})
        note[4] = "c20.w of the rows at pitch 320: %s" % (", ".join("%g" % v for v in render_scale[:6]) or "none read")
        for row, _ in pairs:
            if row.get("frame") != frame:
                continue
            total += 1
            c = row_registers(row)
            view = [c[115], c[116], c[117], [0.0, 0.0, 0.0, 1.0]]
            if tess is not None:
                product = [[sum(c[72 + i][k] * view[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
                scale = max(abs(x) for i in range(4) for x in tess["consts"]["c%d" % i])
                diff = max(abs(product[i][j] - tess["consts"]["c%d" % i][j]) for i in range(4) for j in range(4)) / scale
                worst[0] = max(worst[0], diff)
                held[0] += diff <= 1e-5
            c19, c20, c31 = pixel_register(row, 19), pixel_register(row, 20), pixel_register(row, 31)
            if c19 is not None:
                held[1] += _bits(c[47][:3]) == _bits(c19[:3])
                worst[1] = max(worst[1], max(abs(a - b) for a, b in zip(c[47][:3], c19[:3])))
            tf, tfc = row.get("tf") or {}, row.get("tfc") or {}
            if "16" in tf and "17" in tf:
                b16, b17 = int(tf["16"][1], 16) & 0xFFFFF000, int(tf["17"][1], 16) & 0xFFFFF000
                held[2] += ((fog.get("tf5") or {}).get("base") == "0x%08X" % b16 and (fog.get("tf4") or {}).get("base") == "0x%08X" % b17
                            and (fog.get("tf5") or {}).get("content") == tfc.get("16") and (fog.get("tf4") or {}).get("content") == tfc.get("17"))
            if fog.get("c66") and fog.get("c67"):
                held[3] += _bits(c[110][:3]) == _bits(fog["c66"][:3]) and _bits(c[111]) == _bits(fog["c67"])
                worst[3] = max(worst[3], max(abs(a - f32(b)) for a, b in zip(c[110][:3] + c[111], list(fog["c66"][:3]) + list(fog["c67"]))))
            if c31 is not None and c20 is not None:
                if render_scale:
                    diff = min(abs(c31[0] * v - 1.0) for v in render_scale)
                    worst[4] = max(worst[4], diff)
                    held[4] += diff <= 1e-5
                diff = abs(c31[0] * c20[3] - 1.0)
                worst[5] = max(worst[5], diff)
                held[5] += diff <= 1e-5
    return [(names[i], held[i], total, worst[i], note[i]) for i in range(len(names))]


def cmd_identities(args, say=print):
    """Reported, not a check: what does not hold is carried per draw as read."""
    for capture in args.capture:
        path = cr.find_capture(capture)
        say("%s:" % path.name)
        for name, held, total, worst, note in identities(path):
            say("  holds in %d of %d rows (largest difference %.3e%s): %s" % (held, total, worst, "; " + note if note else "", name))
    return 0


def bounds(paths):
    """Over the in-scene 129F rows of the captures given: {("vs" | "ps", register): [(lo, hi) per component]},
    the size's (lo, hi), the rows and quads counted."""
    out, size, rows, quads = {}, [math.inf, -math.inf], 0, 0
    for path in paths:
        for row, data in impostor_rows(path):
            rows += 1
            c = row_registers(row)
            regs = [(("vs", reg), c[reg]) for reg in lf.IMPOSTOR_SHADER["reg"]]
            regs += [(("ps", reg), pixel_register(row, reg)) for reg in lf.IMPOSTOR_SHADER["ps_reg"]]
            for key, v in regs:
                if v is None:
                    continue
                span = out.setdefault(key, [[math.inf, -math.inf] for _ in range(4)])
                for i in range(4):
                    span[i][0], span[i][1] = min(span[i][0], v[i]), max(span[i][1], v[i])
            if data is not None and data.get("read"):
                for w in quad_words(row, data):
                    quads += 1
                    size[0], size[1] = min(size[0], w[3]), max(size[1], w[3])
    return out, tuple(size), rows, quads


def cmd_bounds(args, say=print):
    paths = [cr.find_capture(c) for c in args.capture]
    spans, size, rows, quads = bounds(paths)
    say("%d rows, %d quads of %s" % (rows, quads, ", ".join(p.name for p in paths)))
    for (bank, reg), span in sorted(spans.items(), key=lambda kv: (kv[0][0] != "vs", kv[0][1])):
        largest = max(max(abs(lo), abs(hi)) for lo, hi in span)
        say("  %s c%-3d %s   largest magnitude %.9g, four times it %.9g" % (
            "vertex" if bank == "vs" else "pixel ", reg, "  ".join("[%.9g, %.9g]" % (lo, hi) for lo, hi in span), largest, 4.0 * largest))
    say("  the size: [%.9g, %.9g], four times the largest %.9g" % (size[0], size[1], 4.0 * size[1]))
    return 0 if rows else 1


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


def _pixel_line(name, r):
    return ("%s %d / %d; with a fog pair %d; absent %d, dissolving %d, whole %d; the light falling %d; degenerate %s; "
            "worst relative error %.3e" % (name, r.trials - r.failed, r.trials, r.fogged, r.absent, r.dissolving, r.whole,
                                           r.falling, ", ".join("%s %d" % kv for kv in sorted(r.kinds.items())) or "none", r.worst))


def pixel_floors(r):
    """The floors a run of check_pixel must reach to count, as problems (texts)."""
    out = []
    if r.fogged < r.trials // 2:
        out.append("the fog pair is a real pair in fewer than half of the trials")
    if r.absent < r.trials // 10:
        out.append("fewer than a tenth of the trials are nearer than the dissolve's start")
    if min(r.dissolving, r.whole) < r.trials // 5:
        out.append("fewer than a fifth of the trials are inside the dissolve, or beyond it")
    if r.falling < r.trials // 10:
        out.append("the light factor is strictly between its ends in fewer than a tenth of the trials")
    return out


def fog_floors(r):
    """The floors a run of check_fog must reach to count, as problems (texts)."""
    out = []
    if r.fogged < r.trials // 2:
        out.append("the fog is begun in fewer than half of the trials")
    if r.near < r.trials // 10:
        out.append("fewer than a tenth of the trials lie before the start")
    return out


def cmd_pixel(args, say=print):
    bad = 0
    say("pixel shader %s (bool b129 clear) against light_formulas.impostor_out(impostor_pixel): %d made pixels, seed %d; "
        "bound %g * max(1, |value|) in every component of oC0" % (PS, args.trials, args.seed, PIXEL_TOLERANCE))
    r = check_pixel(args.trials, args.seed, dumps=args.dumps)
    problems = pixel_floors(r) + (["first failure: %s" % r.first_failure] if r.failed else [])
    say(_pixel_line("pixel", r) + (": OK" if not problems else ": FAILED"))
    for text in problems:
        say("  " + text)
    bad += bool(problems)
    ieee = check_pixel(args.trials, args.seed, dumps=args.dumps, zero_product=False)
    say("without the console's product rule in the interpreter: %d of %d disagree (the trials of kind texel_nan, "
        "light_inf, colour_inf: %d)" % (ieee.failed, ieee.trials, sum(n for kind, n in ieee.kinds.items() if kind != "place_nan")))
    say("vertex shader %s, o2 and o3, against light_formulas.impostor_fog at the raised place: %d made draws, seed %d" % (
        VS, args.trials, args.seed))
    f = check_fog(args.trials, args.seed, dumps=args.dumps)
    problems = fog_floors(f) + (["first failure: %s" % f.first_failure] if f.failed else [])
    say("fog %d / %d; begun %d (both fetches at the formula's coordinates in %d), before the start %d, not compared "
        "(a place that is no number) %d; worst relative error %.3e%s" % (
            f.trials - f.unnumbered - f.failed, f.trials - f.unnumbered, f.fogged, f.coords, f.near, f.unnumbered, f.worst,
            ": OK" if not problems else ": FAILED"))
    for text in problems:
        say("  " + text)
    bad += bool(problems)
    for name in PIXEL_MUTATIONS:
        check = check_fog if name in FOG_MUTATIONS else check_pixel
        m = check(min(args.trials, 400), args.seed, mutation=name, dumps=args.dumps)
        bad += m.failed == 0
        say("  mutation %-10s: %d of %d trials disagree%s" % (name, m.failed, m.trials, "" if m.failed else ": NOT CAUGHT"))
    for name in ("one", "tenth"):
        alone = check_pixel(min(args.trials, 400), args.seed, mutation=("literal", name), dumps=args.dumps)
        both = check_pixel(min(args.trials, 400), args.seed, shift=(name, 1.01), dumps=args.dumps)
        ok = alone.failed > 0 and both.failed == 0
        bad += not ok
        say("  literal %-9s (c%d.%s) by 1%%: in the formula alone %d of %d disagree; in the formula and at its place %d%s" % (
            name, lf.IMPOSTOR_SHADER["ps_lit_at"][name][0], "xyzw"[lf.IMPOSTOR_SHADER["ps_lit_at"][name][1]],
            alone.failed, alone.trials, both.failed, "" if ok else ": NOT HELD"))
    for view, v in CAPTURED_VIEWS.items():
        r = check_pixel(args.trials, args.seed, dumps=args.dumps, view=view)
        problems = pixel_floors(r) + (["first failure: %s" % r.first_failure] if r.failed else [])
        say(_pixel_line("pixel with the constants of capture %s (c12 %s, c29 %s, c31.x %g, c20.w %g):" % (
            view, v["c12"], v["c29"], v["c31x"], v["c20w"]), r) + (": OK" if not problems else ": FAILED"))
        for text in problems:
            say("  " + text)
        bad += bool(problems)
    reads = b129_reads_the_ground_fog(args.dumps, args.seed)
    clear = not CAPTURED_PBOOL & lf.IMPOSTOR_B129
    ok = reads and clear and lf.IMPOSTOR_B129 == 1 << (129 & 31)
    bad += not ok
    say("bool b129 is bit %d of the pixel bool word (mask 0x%08X); with it set the microcode goes on to the ground-fog "
        "block (its fetch of tf12): %s; the captured word 0x%08X has it clear: %s%s" % (
            129 & 31, lf.IMPOSTOR_B129, "yes" if reads else "NO", CAPTURED_PBOOL, "yes" if clear else "NO", "" if ok else ": FAILED"))
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
    p = sub.add_parser("pixel")
    p.add_argument("--trials", type=int, default=2000)
    p.add_argument("--seed", type=int, default=1)
    p.set_defaults(run=cmd_pixel)
    p = sub.add_parser("trace")
    p.add_argument("--instr", type=int, required=True)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--trial", type=int, default=2)
    p.set_defaults(run=cmd_trace)
    p = sub.add_parser("words")
    p.add_argument("capture")
    p.add_argument("--quiet", action="store_true", help="the census without the words")
    p.set_defaults(run=cmd_words)
    p = sub.add_parser("places")
    p.add_argument("capture")
    p.set_defaults(run=cmd_places)
    for name, run in (("rows", cmd_rows), ("identities", cmd_identities), ("bounds", cmd_bounds)):
        p = sub.add_parser(name)
        p.add_argument("capture", nargs="+")
        p.set_defaults(run=run)
    args = ap.parse_args(argv)
    if args.command in ("formulas", "pixel", "trace", "rows") and not args.dumps.is_dir():
        say("no shader dumps at %s (made by fable_2.exe --dump_shaders=<dir>)" % args.dumps)
        return 1
    return args.run(args, say)


if __name__ == "__main__":
    sys.exit(main())
