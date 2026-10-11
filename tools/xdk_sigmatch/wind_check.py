"""The machine check of the wind formulas (light_formulas.wind_plant, wind_trunk, wind_leaf) against the game's
own vertex microcode (ucode_eval): the foliage sub-project, plan Tasks 5 to 7, spec criteria 1 and 2.

  formulas [--trials 2000] [--seed 1] [--only HASH]   every shader of light_formulas.WIND_SHADERS on made inputs
  rows <capture> ... [--per-draw 64]                  ... on every wind draw row of a capture that has streams
  identities <capture> ...                            a caster's microcode on a scene draw's streams and bank
                                                      equals the scene shader's oPos exactly; the tree occluder's
                                                      on its twin's
  trace <hash> --instr N [--seed S] [--trial T]       the interpreter's registers after instruction N for a made
                                                      input (the transcription's oracle, register by register)
  inputs <capture> ... [--off] [--entries]            the capture's wind inputs (plan Task 8, criterion 3): each
                                                      wind draw row's "wind" member against the same row's bank,
                                                      fetches and streams, and each STORED entry of the frame's
                                                      scene (the frame row's "entries": what a record's
                                                      wind_index names) against a member row of its shader;
                                                      --off: a capture made with the switch off must say so and
                                                      hold no member; --entries: a capture without stored
                                                      entries fails instead of being passed unjudged
Each exits 1 on any failure.

What one trial is. A draw is a shader's microcode, a constant bank and vertex streams (bytes): made, or a
capture row's. The microcode runs for one vertex index and gives oPos. Beside it, from the SAME bytes and
registers but through nothing of the interpreter's: the elements the formula reads are decoded at the places
the row of WIND_SHADERS names ("elements": fetch ordinal, format, offset in dwords, source components; the
position as vs-transforms.json has it: yxw1, a plant through its copy's rows read as yxwz and the offset c7),
the family's formula moves the position at rest, c0..c3 make a clip position of it. A trial agrees when every
component is within TOLERANCE * max(1, |oPos|) (criterion 1's bound); it "moved" when the microcode's oPos is
more than MOVED from the clip position at rest (a check on vertices that stand proves nothing: review focus 3).
Which copy and which mesh vertex an instanced index names is taken from the microcode's own fetches (the
index arithmetic is the instance entry's, not the wind's).

Made inputs (criterion 1): the wind registers inside the spec's Design 5 bounds (REGISTER_BOUNDS; c25.x from 0
and |c17.xy| to 16: ledger rulings 26 and 28), each component at its lower or upper edge in about three trials of ten, the clock's size
spread over eight decades; positions and pivots as halves, the 8_8_8_8 elements over all their values; every
25th trial is a draw without sway (zero weights; a plant with c26 = 0), every 25th a vertex beyond a plant's
distance cut (the microcode's position is then no number, and so must the check's be) or a leaf vertex at its
pivot. Every value is rounded to single precision before use, as the game holds it; the arithmetic of both
sides is double precision. The literal registers of a made trial hold ONLY what the row of WIND_SHADERS names
(spec ruling 9: the table's literals, at the table's places), so a literal the row misplaces or leaves out fails
the check; a capture row's literals are its own "vs_lit", and the row's table is held against them bit for bit.

The four near plant shaders (48D3, FC4F, 33C0, and 36B5 which has no wind entry) push their vertices away from
up to four points in a loop over integer constant i0 after the sway. The check runs that push loop 0 times:
the push is not drawn (spec ruling 10), and no capture row holds the loop's count.

A product with a zero factor is +0 here whatever the other factor is (ucode_eval's zero_product: the console's
rule). It matters in one place: a leaf vertex AT its pivot whose flutter is exactly zero, where the microcode
multiplies 0 by 1 / 0; the formula gives the moved pivot there.

Mutations (MUTATIONS; each must fail, tests/test_wind_formulas.py::test_mutations_fail): "register" c16 for c15
(a plant: c22 for c21); "bytes" two bytes of fetch 4 exchanged (a plant: the copy's seed read from its rows'
third components); "sign" the sign of the wind's direction (c17.xy; a plant: c21); "literal" one literal
changed by 1% (MUTATED_LITERAL; ("literal", name) names another). `shift=(name, factor)` scales a literal in the
formula AND at its place in the microcode's constants: the two must then agree again.
"""
import argparse
import json
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

DUMPS = cr.DUMPS
TOLERANCE = 1e-4            # criterion 1: |oPos - formula| <= TOLERANCE * max(1, |oPos|), every component
MOVED = 1e-3                # a trial "moved": the microcode's oPos this far from the position at rest
MUTATIONS = ("register", "bytes", "sign", "literal")
MUTATED_LITERAL = {"plant": "gain", "trunk": "amp", "leaf": "amp"}
LAYER_SURFACE = "0x09000230"        # the sun layer's target: the tree occluder's rows (research C9)
NAN = math.nan
_COMP = "xyzw"
_FORMATS = {32: "FMT_16_16_16_16_FLOAT", 6: "FMT_8_8_8_8"}
_FORMAT_CODES = {name: code for code, name in _FORMATS.items()}
MESH_SLOT, COPY_SLOT = 95, 94       # vf0, vf1 (a stream vfN is fetch slot 95 - N)

# The spec's Design 5 bounds, (register, component) -> (lowest, highest), as the ledger amended them: c25.x from
# 0 (ruling 26), c17.xy to 16 (ruling 28: 12.5 occurs in the first frame after loading, capture 191202).
_TREE_BOUNDS = {(15, 0): (-1e7, 1e7), (15, 1): (0.0, 4.0), (15, 2): (0.0, 4.0), (15, 3): (0.0, 4.0),
                (16, 0): (-4.0, 4.0), (16, 1): (-4.0, 4.0), (16, 2): (-4.0, 4.0), (16, 3): (-4.0, 4.0),
                (17, 0): (-16.0, 16.0), (17, 1): (-16.0, 16.0), (17, 2): (0.0, 16.0), (17, 3): (-2e7, 2e7)}
REGISTER_BOUNDS = {
    "plant": {(10, 0): (-1e7, 1e7), (21, 0): (-4.0, 4.0), (21, 1): (-4.0, 4.0), (21, 2): (-4.0, 4.0),
              (22, 0): (-8.0, 8.0), (22, 1): (-8.0, 8.0), (22, 2): (-8.0, 8.0), (25, 0): (0.0, 64.0),
              (26, 0): (0.0, 4.0), (26, 1): (0.0, 4.0)},
    "trunk": _TREE_BOUNDS,
    "leaf": _TREE_BOUNDS,
}
_CLOCKS = ((10, 0), (15, 0), (17, 3))       # spread over decades, not uniformly


# trials; those that disagree; those the wind moved; the largest relative error of a trial that could be compared;
# the first failure's text (None); check_rows: {shader: [rows, trials, failed, moved, worst]}.
Result = namedtuple("Result", "trials failed moved worst first_failure by_shader", defaults=(None,))


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0] if math.isfinite(x) else x


def _f16(x):
    return struct.unpack(">e", struct.pack(">e", x))[0]


class Case:
    """One draw: the shader, its constants ({register: [4]}), its streams ({fetch slot: bytes}) and the vertex
    index to run; `kind` says what a made case is ("", "still", "cut", "pivot")."""

    def __init__(self, hash_, text, consts, streams, index, kind="", loops=None):
        self.hash_, self.text, self.consts, self.streams, self.index, self.kind = hash_, text, consts, streams, index, kind
        self.row = lf.WIND_SHADERS[hash_]
        self.family = self.row["family"]
        self.loops = loops if loops is not None else {i: 0 for i in range(8)}       # the push loop: 0 times


def _text(hash_, dumps):
    return ue.load(dumps, hash_, "vert")


def strides(text):
    """{fetch slot: the stride in dwords} of a dump's full fetches."""
    out = {}
    for ins in ue.st.parse(text):
        if ins.op == "vfetch_full" and "Stride" in ins.attrs:
            out[95 - ins.fetch_slot] = int(ins.attrs["Stride"])
    return out


def dump_fetches(text):
    """A dump's vertex fetches in order, as a capture row lists them: (fetch slot, format code or None, offset
    in dwords)."""
    out, slot = [], None
    for ins in ue.st.parse(text):
        if not ins.op.startswith("vfetch"):
            continue
        if ins.op == "vfetch_full":
            slot = 95 - ins.fetch_slot
        out.append((slot, _FORMAT_CODES.get(ins.attrs.get("DataFormat")), int(ins.attrs.get("Offset", 0))))
    return out


def run_microcode(case, consts=None, text=None):
    """(the microcode's outputs for the case's vertex index, the machine)."""
    stride = {}

    def vfetch(stream, offset, attrs, idx):
        if "Stride" in attrs:
            stride[stream] = int(attrs["Stride"])
        data = case.streams.get(95 - stream)
        if data is None:
            raise cr.Unreadable("no dump of stream slot %d" % (95 - stream))
        if not math.isfinite(idx):
            raise IndexError("vf%d index %s" % (stream, idx))
        at = (int(round(idx)) * stride[stream] + offset) * 4
        if at < 0 or at + 4 > len(data):
            raise IndexError("vf%d index %s" % (stream, idx))
        try:
            return cr.element(data, at, attrs)
        except struct.error:
            raise IndexError("vf%d index %s" % (stream, idx)) from None

    m = ue.Machine(text or case.text, consts or case.consts, lambda slot, coords, attrs: [0.0] * 4,
                   regs={0: [float(case.index), 0.0, 0.0, 0.0]}, vfetch=vfetch, loops=case.loops, zero_product=True)
    return m.run(), m


def _element(case, slot, vertex, offset, fmt, stride):
    """The source components of a vertex element, decoded from the stream's bytes (not through the machine)."""
    data = case.streams[slot]
    at = (vertex * stride + offset) * 4
    size = 8 if fmt == 32 else 4
    if at < 0 or at + size > len(data):
        raise IndexError("slot %d vertex %d" % (slot, vertex))
    return cr.element(data, at, {"DataFormat": _FORMATS[fmt]})


def _pick(source, selector):
    return tuple(source[_COMP.index(ch)] for ch in selector)


def _fetched_index(m, stream):
    """The index the microcode fetched stream vfN at (the first fetch of it)."""
    for _instr, s, _offset, index in m.vfetches:
        if s == stream:
            return int(round(index))
    raise cr.Unreadable("the microcode fetched nothing from vf%d" % stream)


def formula_inputs(case, m=None):
    """What the family's formula reads for the case's vertex, from the stream bytes: {"p": the position at
    rest, ...}. `m`: the machine of a run of the case (made here when absent), for the fetched indices."""
    if m is None:
        m = run_microcode(case)[1]
    stride = strides(case.text)
    c = case.consts
    if case.family == "plant":
        copy, vertex = _fetched_index(m, 1), _fetched_index(m, 0)
        rows = [_pick(_element(case, COPY_SLOT, copy, off, 32, stride[COPY_SLOT]), "yxwz") for off in (0, 2, 4)]
        mesh = _pick(_element(case, MESH_SLOT, vertex, 0, 32, stride[MESH_SLOT]), "yxw")
        p = tuple(sum(rows[k][i] * mesh[i] for i in range(3)) + rows[k][3] + c[7][k] for k in range(3))
        d2 = sum((c[9][k] - p[k]) ** 2 for k in range(3))
        return {"p": p, "z": mesh[2], "a3": rows[0][3], "b3": rows[1][3], "rows": rows, "mesh": mesh,
                "cut": d2 > c[13][2], "cut_margin": abs(d2 - c[13][2]), "copy": copy, "vertex": vertex}
    vertex = _fetched_index(m, 0)
    ins = {"p": _pick(_element(case, MESH_SLOT, vertex, 0, 32, stride[MESH_SLOT]), "yxw"), "vertex": vertex}
    for name, _fetch, fmt, offset, source, _count in case.row["elements"]:
        ins[name] = _pick(_element(case, MESH_SLOT, vertex, offset, fmt, stride[MESH_SLOT]), source)
    return ins


def formula_position(case, ins, lit=None, mutation=None):
    """The family's formula on `ins` with the case's registers: the moved position. `mutation`: one of
    MUTATIONS, or ("literal", name)."""
    c = case.consts
    lit = dict(case.row["lit"] if lit is None else lit)
    if isinstance(mutation, tuple):
        if mutation[0] != "literal" or mutation[1] not in lit:
            raise ValueError("unknown mutation %r" % (mutation,))
        lit[mutation[1]] *= 1.01
    elif mutation == "literal":
        lit[MUTATED_LITERAL[case.family]] *= 1.01
    elif mutation is not None and mutation not in MUTATIONS:
        raise ValueError("unknown mutation %r" % (mutation,))
    if case.family == "plant":
        c21, c22, a3, b3 = c[21][:3], c[22][:3], ins["a3"], ins["b3"]
        if mutation == "register":
            c21 = c22
        elif mutation == "sign":
            c21 = [-x for x in c21]
        elif mutation == "bytes":
            a3, b3 = ins["rows"][0][2], ins["rows"][1][2]
        return lf.wind_plant(ins["p"], ins["z"], a3, b3, c[7][:2], c[10][0], c21, c22, c[25][0], c[26][:2], lit)
    c15, c16, c17, w4 = c[15], c[16], c[17], ins["weights"]
    if mutation == "register":
        c15 = c16
    elif mutation == "sign":
        c17 = [-c17[0], -c17[1], c17[2], c17[3]]
    elif mutation == "bytes":
        w4 = (w4[1], w4[0], w4[2], w4[3])
    if case.family == "trunk":
        return lf.wind_trunk(ins["p"], w4, c15, c16, c17, lit)
    return lf.wind_leaf(ins["p"], ins["pivot"], w4, ins["flutter"], c15, c16, c17, lit)


def project(consts, p):
    return [sum(consts[r][k] * p[k] for k in range(3)) + consts[r][3] for r in range(4)]


def project_cut(consts):
    """c0..c3 of a position whose x, y and z are no numbers (w = 1)."""
    return [NAN if any(consts[r][k] != 0.0 for k in range(3)) else consts[r][3] for r in range(4)]


Trial = namedtuple("Trial", "ok error moved got want note")


def evaluate(case, mutation=None, shift=None):
    """One trial: Trial(ok, the largest relative error or None, moved, the microcode's oPos, the formula's,
    a note)."""
    consts, lit = case.consts, dict(case.row["lit"])
    if shift is not None:
        name, factor = shift
        reg, comp = case.row["lit_at"][name]
        consts = dict(consts)
        consts[reg] = list(consts[reg])
        consts[reg][comp] *= factor
        lit[name] *= factor
    shifted = Case(case.hash_, case.text, consts, case.streams, case.index, case.kind, case.loops) if shift else case
    out, m = run_microcode(shifted)
    got = [float(x) for x in out["oPos"]]
    ins = formula_inputs(shifted, m)
    none = [x != x for x in got]
    if case.family == "plant" and (ins["cut"] or (any(none) and ins["cut_margin"] <= 1e-6 * max(1.0, abs(consts[13][2])))):
        # Beyond the distance cut the microcode's position is its literal c255.xyz, which is no number: so is
        # every component of oPos whose row has a factor on x, y or z; a row without one (the w of a shadow
        # map's projection, 0 0 0 1) gives its fourth component, a zero factor making +0 (module text).
        want = project_cut(consts)
        same = all((g != g) == (w != w) and (w != w or abs(g - w) <= TOLERANCE * max(1.0, abs(g))) for g, w in zip(got, want))
        return Trial(same and any(none), None, False, got, want, "beyond the cut")
    want = project(consts, formula_position(shifted, ins, lit, mutation))
    rest = project(consts, ins["p"])
    if any(none) or any(x != x for x in want):
        return Trial(False, None, False, got, want, "a position that is no number")
    error = max(abs(g - w) / max(1.0, abs(g)) for g, w in zip(got, want))
    moved = max(abs(g - r) for g, r in zip(got, rest)) > MOVED
    return Trial(error <= TOLERANCE, error, moved, got, want, "")


# --- made inputs ----------------------------------------------------------------------------------------------

def _bounded(rng, reg, comp, lo, hi):
    edge = rng.random()
    if edge < 0.15:
        return lo
    if edge < 0.30:
        return hi
    if (reg, comp) in _CLOCKS:
        return rng.choice((-1.0, 1.0)) * 10.0 ** rng.uniform(-1.0, math.log10(hi))
    return rng.uniform(lo, hi)


def _half4(values):
    return struct.pack(">4e", *values)


def _byte4(source):
    """An 8_8_8_8 element whose SOURCE components (x to w) are the four bytes over 255."""
    return struct.pack(">I", sum(int(b) << (8 * k) for k, b in enumerate(source)))


def _coordinate(rng):
    pick = rng.random()
    if pick < 0.8:
        return rng.uniform(-30.0, 30.0)
    return rng.uniform(-2000.0, 2000.0) if pick < 0.9 else rng.uniform(-0.01, 0.01)


def _literal_registers(row, plant_cut=True):
    """Registers 248 to 255 holding only what the row names, at the row's places (module text)."""
    consts = {reg: [0.0] * 4 for reg in range(248, 256)}
    for name, (reg, comp) in row["lit_at"].items():
        consts[reg][comp] = row["lit"][name]
    if row["family"] == "plant" and plant_cut:
        # The position of a vertex beyond the distance cut: c255.xyz, no numbers in every shader object's table
        # (frame-map section 12, "Literals").
        consts[255][:3] = [NAN] * 3
    return consts


def made_case(hash_, seed=1, trial=0, dumps=DUMPS, text=None):
    """The made draw of one trial (deterministic in hash, seed and trial)."""
    row = lf.WIND_SHADERS[hash_]
    family = row["family"]
    rng = random.Random("%s/%d/%d" % (hash_, seed, trial))
    text = text if text is not None else _text(hash_, dumps)
    kind = {0: "still", 1: "cut" if family == "plant" else "pivot" if family == "leaf" else ""}.get(trial % 25, "")
    c = {reg: [f32(rng.uniform(-1.0, 1.0)) for _ in range(4)] for reg in range(128)}
    c[3][3] = f32(rng.uniform(2.0, 4.0))                        # a w that is not near zero
    for (reg, comp), (lo, hi) in REGISTER_BOUNDS[family].items():
        c[reg][comp] = f32(_bounded(rng, reg, comp, lo, hi))
    stride = strides(text)
    streams = {}
    if family == "plant":
        per, copies, first = rng.randrange(3, 13), 3, rng.randrange(3)
        c[7] = [f32(rng.uniform(-200.0, 200.0)) for _ in range(3)] + [1.0]              # the draw's offset
        c[9] = [f32(c[7][k] + rng.uniform(-1.0, 1.0)) for k in range(3)] + [1.0]        # the eye
        c[12] = [f32(1.0 / per), float(per), float(first), f32(1.0 / copies)]
        c[13] = [0.0, 0.0, -1.0 if kind == "cut" else 1e12, 0.0]                         # the cut's squared distance
        c[49][0] = f32(rng.uniform(0.1, 1.0))
        if kind == "still":
            c[26][0] = c[26][1] = 0.0
            if rng.random() < 0.5:
                c[25][0] = 0.0                                                          # ledger ruling 26
        data = bytearray()
        for _ in range(first + copies):
            for _row in range(3):
                data += _half4([_f16(rng.uniform(-2.0, 2.0)) for _ in range(3)] + [_f16(rng.uniform(-50.0, 50.0))])
            data += _byte4([rng.randrange(256) for _ in range(4)])
            data += bytes(4 * (stride[COPY_SLOT] - 7))
        streams[COPY_SLOT] = bytes(data)
        data = bytearray()
        for _ in range(per):
            height = rng.random()
            z = rng.uniform(-0.3, 0.0) if height < 0.2 else rng.uniform(0.0, 0.2) if height < 0.7 else rng.uniform(0.2, 3.0)
            data += _half4([_f16(rng.uniform(-3.0, 3.0)), _f16(rng.uniform(-3.0, 3.0)), _f16(z), _f16(rng.random())])
            for _dword in range(stride[MESH_SLOT] - 2):
                data += struct.pack(">2e", _f16(rng.uniform(-1.0, 1.0)), _f16(rng.uniform(-1.0, 1.0)))
        streams[MESH_SLOT] = bytes(data)
        index = rng.randrange(per * copies)
    else:
        c[111] = [1e9, 0.0, 0.01, 0.01]             # the vertex fog off: its tables are not fetched
        if kind == "still":
            c[15][3] = c[16][3] = 0.0               # a leaf's flutter (c15.w, c16.w); a trunk does not read them
        vertices = 4
        data = bytearray()
        for _ in range(vertices):
            p = [_f16(_coordinate(rng)) for _ in range(3)]
            spread = rng.random()
            delta = [0.0] * 3 if kind == "pivot" else [rng.uniform(-3.0, 3.0) if spread < 0.75 else rng.uniform(-0.15, 0.15)
                                                       for _ in range(3)]
            words = {0: _half4(p + [float(rng.choice((0, 4, 8)))]),                     # w: the probe set's index
                     5: _half4([_f16(p[k] + delta[k]) for k in range(3)] + [_f16(rng.uniform(-1.0, 1.0))]),
                     7: _byte4([0] * 4 if kind == "still" else
                               [rng.choice((0, 255)) if rng.random() < 0.1 else rng.randrange(256) for _ in range(4)]),
                     8: _byte4([rng.choice((0, 255)) if rng.random() < 0.2 else rng.randrange(256) for _ in range(4)])}
            dword = 0
            while dword < stride[MESH_SLOT]:
                piece = words.get(dword) or struct.pack(">2e", _f16(rng.uniform(-1.0, 1.0)), _f16(rng.uniform(-1.0, 1.0)))
                data += piece
                dword += len(piece) // 4
        streams[MESH_SLOT] = bytes(data)
        if COPY_SLOT in stride:                     # the probe weights (the light's; not on the position's path)
            streams[COPY_SLOT] = b"".join(struct.pack(">4f", *[rng.random() for _ in range(4)]) for _ in range(vertices))
        index = rng.randrange(vertices)
    c.update(_literal_registers(row))
    return Case(hash_, text, c, streams, index, kind)


def made_ranges(hash_, trials=400, seed=1, dumps=DUMPS):
    """What the made trials cover: {(register, component): (lowest, highest)} of the bounded registers, and the
    numbers of trials of each kind ("still", "cut", "pivot")."""
    row = lf.WIND_SHADERS[hash_]
    text = _text(hash_, dumps)
    seen = {"still": 0, "cut": 0, "pivot": 0}
    for t in range(trials):
        case = made_case(hash_, seed, t, text=text)
        if case.kind:
            seen[case.kind] += 1
        for key in REGISTER_BOUNDS[row["family"]]:
            v = case.consts[key[0]][key[1]]
            lo, hi = seen.get(key, (v, v))
            seen[key] = (min(lo, v), max(hi, v))
    return seen


def _check_mutation(mutation):
    if mutation is None or mutation in MUTATIONS or (isinstance(mutation, tuple) and len(mutation) == 2
                                                      and mutation[0] == "literal"):
        return
    raise ValueError("unknown mutation %r (one of %s, or (\"literal\", name))" % (mutation, ", ".join(MUTATIONS)))


def _describe_trial(case, t, where):
    if t.error is None:
        return "%s: %s; oPos %s, formula %s" % (where, t.note, t.got, t.want)
    return "%s: relative error %.3e; oPos %s, formula %s" % (where, t.error, t.got, t.want)


def check_formula(hash_, trials=2000, seed=1, mutation=None, dumps=DUMPS, shift=None):
    """`trials` made draws of one shader of WIND_SHADERS (KeyError for another): Result."""
    lf.WIND_SHADERS[hash_]
    _check_mutation(mutation)
    text = _text(hash_, dumps)
    failed = moved = 0
    worst, first = 0.0, None
    for k in range(trials):
        case = made_case(hash_, seed, k, text=text)
        t = evaluate(case, mutation, shift)
        if t.error is not None:
            worst = max(worst, t.error)
        moved += t.moved
        if not t.ok:
            failed += 1
            if first is None:
                first = _describe_trial(case, t, "%s trial %d (seed %d%s)" % (hash_, k, seed, ", " + case.kind if case.kind else ""))
    return Result(trials, failed, moved, worst, first)


def describe(hash_, dumps=DUMPS):
    """How a shader is checked, in words (the formulas' report prints it)."""
    row = lf.WIND_SHADERS[hash_]
    text = _text(hash_, dumps)
    places = ", ".join("%s c%d.%s" % (name, reg, _COMP[comp]) for name, (reg, comp) in row["lit_at"].items())
    out = "%s: %s, %s; registers %s; literals %s" % (hash_, row["family"], row["role"],
                                                      ", ".join("c%d" % r for r in row["reg"]), places)
    if any(line.split("*/")[-1].strip().startswith("loop ") for line in text.splitlines()):
        out += "; the push loop (integer constant i0) runs 0 times: the push is not drawn (spec ruling 10)"
    return out


# --- trace ----------------------------------------------------------------------------------------------------

def _instruction_number(line):
    head = line.strip()
    if not head.startswith("/*"):
        return None
    word = head[2:head.find("*/")].strip()
    return int(word) if word.isdigit() else None


def truncated(text, instr):
    """The dump up to and including instruction `instr` (its co-issued row too), with the labels a jump may
    still name at the end."""
    lines = text.splitlines()
    at = next((i for i, line in enumerate(lines) if _instruction_number(line) == instr), None)
    if at is None:
        raise ValueError("no instruction %d in the dump" % instr)
    end = at + 1
    while end < len(lines) and not lines[end].strip().startswith("/*") and not lines[end].strip().startswith("label "):
        end += 1
    kept = lines[:end]
    labels = [line for line in lines[end:] if line.strip().startswith("label ")]
    return "\n".join(kept + labels) + "\n"


def trace(case, instr):
    """The interpreter's registers ({register: [4]}) after instruction `instr` of the case's shader."""
    return run_microcode(case, text=truncated(case.text, instr))[1].r


# --- capture rows ---------------------------------------------------------------------------------------------

def _f32_consts(draw):
    return {reg: [f32(x) for x in v] for reg, v in draw.consts.items()}


def _bits(x):
    return struct.pack("<f", x)


def literal_mismatches(hash_, consts):
    """The named literals of the shader's row that the constants do not hold bit for bit at the row's places."""
    row = lf.WIND_SHADERS[hash_]
    return [name for name, (reg, comp) in row["lit_at"].items() if _bits(consts[reg][comp]) != _bits(row["lit"][name])]


def element_mismatches(hash_, fetches):
    """The row's elements that a fetch list (a capture row's "fetches") does not have at the named ordinal."""
    bad = []
    for name, fetch, fmt, offset, _source, _count in lf.WIND_SHADERS[hash_]["elements"]:
        f = fetches[fetch] if 0 <= fetch < len(fetches) else None
        if not f or f.get("fmt") != fmt or f.get("off") != offset or f.get("slot") != MESH_SLOT:
            bad.append(name)
    return bad


def row_case(path, row, dumps=DUMPS):
    """A capture row as a Case (index None until a vertex is chosen). Its registers are rounded to single
    precision (the capture prints nine digits of each)."""
    draw = cr.Draw(path, row, dumps=dumps)
    return Case(draw.h, draw.text, _f32_consts(draw), draw.streams, None)


def _row_vertices(path, row, per_draw):
    import foliage_replay as fr
    idx, _state, _note = fr.draw_indices(row, Path(path).parent)
    base = int(row.get("base_vertex", 0))
    ids = sorted({int(i) + base for i in idx if int(i) >= 0})
    if len(ids) > per_draw:
        ids = sorted(random.Random(int(row.get("line", 0))).sample(ids, per_draw))
    return ids


def wind_rows(path, hash_=None, frames=None):
    """The capture's draw rows whose vertex shader has a row of WIND_SHADERS and that hold streams."""
    out = []
    for r in cr.rows(path, kinds=("draw",)):
        h = cr.vs_hash(r)
        if h not in lf.WIND_SHADERS or (hash_ and h != hash_) or (frames and r.get("frame") not in frames):
            continue
        if r.get("streams") and r.get("bank"):
            out.append(r)
    return out


def check_rows(capture_path, hash_=None, per_draw=64, frames=None, per_shader=None, mutation=None, dumps=DUMPS, say=None):
    """Every wind draw row of a capture that has streams (the main scene's, the casters' own, the reflection
    views'), at least `per_draw` vertices a draw and all of a smaller one: Result, with by_shader.
    `frames`: these frame numbers alone; `per_shader`: at most so many rows a shader (a reduced check)."""
    _check_mutation(mutation)
    path = cr.find_capture(str(capture_path))
    by = {}
    failed = moved = trials = 0
    worst, first = 0.0, None
    for row in wind_rows(path, hash_, frames):
        h = cr.vs_hash(row)
        stat = by.setdefault(h, [0, 0, 0, 0, 0.0])
        if per_shader is not None and stat[0] >= per_shader:
            continue
        stat[0] += 1
        where = "%s line %d" % (h, row["line"])
        try:
            case = row_case(path, row, dumps)
            problems = ["literal %s" % n for n in literal_mismatches(h, case.consts)] + \
                       ["element %s" % n for n in element_mismatches(h, row.get("fetches") or [])]
            vertices = _row_vertices(path, row, per_draw) if not problems else []
        except (cr.Unreadable, OSError, KeyError, ValueError) as e:
            problems, vertices = ["not read: %s" % e], []
        if problems:
            trials += 1
            failed += 1
            stat[1] += 1
            stat[2] += 1
            first = first or "%s: %s" % (where, "; ".join(problems))
            continue
        for v in vertices:
            case.index = v
            try:
                t = evaluate(case, mutation)
            except (IndexError, cr.Unreadable, KeyError, NotImplementedError) as e:
                t = Trial(False, None, False, None, None, "%s: %s" % (type(e).__name__, e))
            trials += 1
            stat[1] += 1
            moved += t.moved
            stat[3] += t.moved
            if t.error is not None:
                worst = max(worst, t.error)
                stat[4] = max(stat[4], t.error)
            if not t.ok:
                failed += 1
                stat[2] += 1
                first = first or _describe_trial(case, t, "%s vertex %d" % (where, v))
        if say and stat[0] % 100 == 0:
            say("  ... %s: %d rows" % (h, stat[0]))
    return Result(trials, failed, moved, worst, first, by)


# --- identities -----------------------------------------------------------------------------------------------

# (the caster, the scene shader whose streams and bank it is run on); criterion 2 names the first four.
IDENTITIES = (("FBD39C64463E180B", "475EC9F795E5EDBB"), ("432563420047C96C", "A5846836C90E1192"),
              ("A8A5B2A89F70336A", "8123C16DBF583F92"), ("A8A5B2A89F70336A", "B636821F95DC9D8E"),
              ("432563420047C96C", "D7DD31B9BF194DE8"), ("432563420047C96C", "2D40B53C926109BE"))
OCCLUDER = "432563420047C96C"


def _as_other(case, hash_, consts, dumps):
    """`case`'s streams and vertex under another shader, with `consts`' registers and that shader's literals
    (its row's, at its row's places)."""
    c = dict(consts)
    c.update(_literal_registers(lf.WIND_SHADERS[hash_]))
    return Case(hash_, _text(hash_, dumps), c, case.streams, case.index)


def _identity(path, pairs, per_draw, dumps):
    """pairs: [(scene row, other shader's hash, the bank's row)] -> (rows, vertices, the largest |oPos - oPos|,
    the largest distance of the scene's oPos from rest, unread rows)."""
    rows = vertices = unread = 0
    worst = moved = 0.0
    for scene_row, other, bank_row in pairs:
        try:
            case = row_case(path, scene_row, dumps)
            bank = case.consts if bank_row is scene_row else {
                reg: [f32(x) for x in bank_row["bank"][4 * reg:4 * reg + 4]] for reg in range(len(bank_row["bank"]) // 4)}
            ids = _row_vertices(path, scene_row, per_draw)
            for v in ids:
                case.index = v
                a, m = run_microcode(case)
                b = run_microcode(_as_other(case, other, bank, dumps))[0]
                pa, pb = a["oPos"], b["oPos"]
                if all(x != x for x in pa) and all(x != x for x in pb):
                    continue                                    # both beyond the cut
                diff = max(abs(x - y) for x, y in zip(pa, pb))
                worst = max(worst, diff if diff == diff else math.inf)
                rest = project(case.consts, formula_inputs(case, m)["p"])
                moved = max(moved, max(abs(x - y) for x, y in zip(pa, rest)))
                vertices += 1
            rows += 1
        except (cr.Unreadable, OSError, IndexError, KeyError):
            unread += 1
    return rows, vertices, worst, moved, unread


def check_identities(capture_path, frames=None, per_pair=None, per_draw=16, dumps=DUMPS):
    """Criterion 2 on one capture: (report lines, the number of comparisons that are not exact). A pair without
    a scene row in the capture is said and not counted."""
    path = cr.find_capture(str(capture_path))
    rows = [r for r in cr.rows(path, kinds=("draw",)) if not frames or r.get("frame") in frames]
    scene = [r for r in rows if r.get("in_scene") and r.get("streams") and r.get("bank")]
    lines, bad = [], 0
    for caster, shader in IDENTITIES:
        if caster not in lf.WIND_SHADERS or shader not in lf.WIND_SHADERS:
            continue
        mine = [r for r in scene if cr.vs_hash(r) == shader]
        if per_pair is not None:
            mine = mine[:per_pair]
        if not mine:
            lines.append("%s on %s: no main-scene row of %s in the capture" % (caster[:4], shader[:4], shader[:4]))
            continue
        n, vertices, worst, moved, unread = _identity(path, [(r, caster, r) for r in mine], per_draw, dumps)
        ok = vertices > 0 and worst == 0.0 and unread == 0
        bad += not ok
        lines.append("%s on %s: %d rows, %d vertices: largest |oPos(%s) - oPos(%s)| = %.3e (the scene's oPos is up to %.3f "
                     "from rest)%s: %s" % (caster[:4], shader[:4], n, vertices, caster[:4], shader[:4], worst, moved,
                                           "; %d rows not read" % unread if unread else "", "exact" if ok else "NOT EXACT"))
    if OCCLUDER in lf.WIND_SHADERS:
        import foliage_replay as fr
        occluders = [r for r in rows if cr.vs_hash(r) == OCCLUDER and r.get("surface_info") == LAYER_SURFACE and r.get("bank")]
        if not occluders:
            lines.append("tree occluder on its twin: no row of %s at the layer target in the capture" % OCCLUDER[:4])
        pairs, lost = [], 0
        for r in occluders:
            twin = fr.twin_of([s for s in scene if s.get("frame") == r.get("frame")], r)
            if twin is None or cr.vs_hash(twin) not in lf.WIND_SHADERS:
                lost += 1
            else:
                pairs.append((twin, OCCLUDER, r))
        if occluders:
            n, vertices, worst, moved, unread = _identity(path, pairs, per_draw, dumps)
            ok = vertices > 0 and worst == 0.0 and unread == 0 and lost == 0
            bad += not ok
            lines.append("tree occluder (%s at the layer, its own bank) on its twin's streams: %d of %d rows with a twin, %d "
                         "vertices: largest |oPos - the twin's oPos| = %.3e (the twin's oPos is up to %.3f from rest)%s: %s" % (
                             OCCLUDER[:4], n, len(occluders), vertices, worst, moved,
                             "; %d rows not read" % unread if unread else "", "exact" if ok else "NOT EXACT"))
    return lines, bad


# --- the capture's wind inputs (plan Task 8; spec criterion 3) ---------------------------------------------------

# capture::WindReasonName, in WindReason's order (tests/test_wind_inputs.py holds the header to it).
WIND_REASONS = ("none", "off", "no-entry", "register-unread", "register-not-finite", "register-range", "literal",
                "element", "stream", "instance", "over-limit")
REASON_SHARE = 0.01         # criterion 3: no reason but "no-entry" in more than this share of the wind draws
TABLE = cr.REPO / "src" / "native" / "capture" / "wind_table.inc"
_ELEMENT_BYTES = {6: 4, 32: 8}      # capture::LightElementBytes of the two formats a wind element has
_PROBLEMS_KEPT = 40


def table_order(path=TABLE):
    """The hashes of wind_table.inc's FABLE2_WIND lines in the file's order: a row's place is WindInputs::spec."""
    out = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if line.startswith("FABLE2_WIND(0x"):
            out.append(line[len("FABLE2_WIND(0x"):line.index("ull")].upper())
    return out


def _hex_bits(x):
    return "%08X" % struct.unpack("<I", struct.pack("<f", x))[0]


def _row_literals(row):
    """({(register, component): value}, {registers named}) of a row's "vs_lit" (the shader object's own literal
    table: runs of values from a first register on; a later run over an earlier one), or None when the capture
    could not read the table."""
    runs = row.get("vs_lit")
    if runs is None:
        return None
    values, named = {}, set()
    for run in runs:
        for k, v in enumerate(run["v"]):
            reg = run["reg"] + k // 4
            named.add(reg)
            values[(reg, k % 4)] = math.nan if v is None else v
    return values, named


def _max_vertex(row):
    """DrawShaders::max_vertex of a row's draw: its largest index plus the base vertex, or its last vertex; -1
    when it is not known."""
    ib = row.get("ib")
    if ib:
        base = int(ib.get("base_vertex", row.get("base_vertex", 0)))
        base -= 1 << 32 if base >= 1 << 31 else 0
        return int(ib["max_index"]) + base if int(ib.get("max_index", -1)) >= 0 else -1
    args = [int(a, 16) for a in row.get("args") or []]
    return args[1] + args[2] - 1 if len(args) >= 3 and args[2] else -1


def expected_inputs(row, order=None):
    """What capture::MakeWindInputs must give for a draw row, from the row's own evidence and through nothing of
    the capture's wind code: the row's "wind" member as the capture writes it ({"reason": "none", "family",
    "spec", "reg": the registers' bits, and a tree's elements}, or {"reason", "family"}); {"reason": "no-entry"}
    for a shader without a row of WIND_SHADERS. The rules and their order are wind_inputs.h's."""
    h = cr.vs_hash(row)
    if h not in lf.WIND_SHADERS:
        return {"reason": "no-entry"}
    spec = lf.WIND_SHADERS[h]
    family = spec["family"]

    def refused(reason):
        return {"reason": reason, "family": family}

    literals = _row_literals(row)
    if literals is None:
        return refused("literal")
    values, named = literals
    if any(reg in named for reg in spec["reg"]):
        return refused("literal")
    for name, place in spec["lit_at"].items():
        if place not in values or _bits(f32(values[place])) != _bits(spec["lit"][name]):
            return refused("literal")
    bank = row.get("bank")
    if not bank:
        return refused("register-unread")
    words = []
    for reg in spec["reg"]:
        v = bank[4 * reg:4 * reg + 4]
        if len(v) != 4:
            return refused("register-unread")
        if any(x is None or not math.isfinite(x) for x in v):
            return refused("register-not-finite")
        v = [f32(x) for x in v]
        for comp in range(4):
            lo, hi = REGISTER_BOUNDS[family].get((reg, comp), (-math.inf, math.inf))
            if not lo <= v[comp] <= hi:
                return refused("register-range")
        words.extend(_hex_bits(x) for x in v)
    words.extend(["00000000"] * (20 - len(words)))
    out = {"reason": "none", "family": family, "spec": (order or table_order()).index(h), "reg": "".join(words)}
    if family == "plant":
        return out if row.get("ib") else refused("instance")
    fetches = row.get("fetches") or []
    max_vertex = _max_vertex(row)
    for name, fetch, fmt, offset, source, count in spec["elements"]:
        f = fetches[fetch] if 0 <= fetch < len(fetches) else None
        if not f or f.get("fmt") != fmt or f.get("off") != offset:
            return refused("element")
        stride = int(f.get("stride", 0))
        if fmt not in _ELEMENT_BYTES or stride == 0 or stride > 255 or offset * 4 + _ELEMENT_BYTES[fmt] > stride * 4:
            return refused("element")
        delivered = {(int(f.get("swz", 0)) >> (3 * c)) & 7 for c in range(4)}
        if len(source) != count or len(set(source)) != count or any(_COMP.index(ch) not in delivered for ch in source):
            return refused("element")
        stream = next((s for s in row.get("streams") or [] if s.get("slot") == f.get("slot")), None)
        if not stream or int(stream.get("endian", 0)) > 3 or int(stream["size"]) < stride * 4:
            return refused("stream")
        if max_vertex < 0 or (max_vertex + 1) * stride * 4 > int(stream["size"]):
            return refused("stream")
        out[name] = {"phys": int(stream["phys"]), "size": int(stream["size"]), "off": offset * 4, "stride": stride * 4,
                     "fmt": fmt, "src": source,
                     "flags": (1 if f.get("signed") else 0) | (2 if f.get("norm") else 0) | (int(stream.get("endian", 0)) << 2)}
    return out


# rows: the capture's wind draw rows; compared: those with a "wind" member in a frame with the wind on; agree:
# those of them whose member is what the row's own evidence gives; reasons: the members' reasons; predicted: expected_inputs' reasons for every wind draw row; frames_on / frames_off: frame
# rows whose "wind" is an object / "off"; off_clean: an off capture that holds no member; frames: the frame rows'
# own counts, for the report; ok: something was compared and nothing disagrees.
# entries / entries_matched: the STORED entries of the frames' scenes (the frame rows' "entries" and
# "caster_entries": what the records' wind_index names in the side arrays) and those of them that a draw row's
# member of the same shader holds value for value (review of plan Tasks 8 and 9, Important 1); frames_entries: the
# frames with the wind on whose row has them (a capture from before the review's fix has none: not judged).
InputsResult = namedtuple("InputsResult", "rows compared agree problems problem_count reasons predicted frames_on frames_off "
                                          "off_clean frames ok entries entries_matched frames_entries")


def _entry_key(vs, fields):
    """What a stored entry and a draw row's member must have in common: the shader, and every field of the inputs
    (spec, the registers' bits, each element) as one canonical text."""
    return vs.upper(), json.dumps({k: v for k, v in fields.items() if k not in ("reason", "seq", "vs")}, sort_keys=True)


def _entries_problems(frame, wind, scene_rows, other_rows):
    """A frame's stored entries against its draw rows' members: (problems, entries, matched). `scene_rows`,
    `other_rows`: {key: count} of the members of reason "none" on the frame's draw rows in the main scene and
    outside it. Each scene entry must be held by a member row in the scene of the same shader with the same
    values, each caster entry by one outside it, a row serving one entry; the counts must be the frame row's own;
    no entry of a side array may be one that no record names."""
    out = []
    entries, casters = wind.get("entries"), wind.get("caster_entries")
    if not isinstance(entries, list) or not isinstance(casters, list):
        return ["frame %d: \"entries\" or \"caster_entries\" is no list" % frame], 0, 0
    caster_list = [e for cascade in casters for e in cascade]
    if len(entries) != int(wind.get("records", -1)):
        out.append("frame %d: %d stored entries, \"records\" says %s" % (frame, len(entries), wind.get("records")))
    if len(caster_list) != int(wind.get("casters", -1)):
        out.append("frame %d: %d stored caster entries, \"casters\" says %s" % (frame, len(caster_list), wind.get("casters")))
    if int(wind.get("unreferenced", -1)) != 0:
        out.append("frame %d: %s entries of the side arrays are named by no record" % (frame, wind.get("unreferenced")))
    matched = 0
    for name, group, rows in (("scene", entries, scene_rows), ("caster", caster_list, other_rows)):
        left = dict(rows)
        for e in group:
            key = _entry_key(str(e.get("vs", ""))[2:], e)
            if left.get(key, 0) > 0:
                left[key] -= 1
                matched += 1
            else:
                out.append("frame %d: a stored %s entry (seq %s, %s, spec %s) has no %s member row with its values" % (
                    frame, name, e.get("seq"), key[0][:4], e.get("spec"), "in-scene" if name == "scene" else "out-of-scene"))
    return out, len(entries) + len(caster_list), matched


def _frame_problems(frame, wind):
    """What a frame row's own wind counts must satisfy."""
    out = []
    family, reason, caster = wind.get("family") or {}, wind.get("reason") or {}, wind.get("caster_reason") or {}
    records, casters = int(wind.get("records", -1)), int(wind.get("casters", -1))
    if records != sum(int(v) for v in family.values()):
        out.append("frame %d: \"records\" %d is not the sum of \"family\" %s" % (frame, records, family))
    if records != int(reason.get("none", 0)):
        out.append("frame %d: \"records\" %d is not \"reason\".none %s" % (frame, records, reason.get("none", 0)))
    if casters != int(caster.get("none", 0)):
        out.append("frame %d: \"casters\" %d is not \"caster_reason\".none %s" % (frame, casters, caster.get("none", 0)))
    for name in set(reason) | set(caster):
        if name not in WIND_REASONS:
            out.append("frame %d: unknown reason %r" % (frame, name))
    at_rest = {n: int(reason.get(n, 0)) + int(caster.get(n, 0)) for n in WIND_REASONS if n not in ("none", "no-entry")}
    wind_draws = max(records, 0) + max(casters, 0) + sum(at_rest.values())
    for name, n in at_rest.items():
        if n > REASON_SHARE * wind_draws:
            out.append("frame %d: the frame's records count \"%s\" %d times in %d wind draws: more than 1%%" % (
                frame, name, n, wind_draws))
    return out


def check_inputs(capture_path):
    """A capture's wind inputs against its own rows (criterion 3): InputsResult."""
    path = cr.find_capture(str(capture_path))
    order = table_order()
    state, frames = {}, {}
    problems, problem_count = [], 0

    def problem(text):
        nonlocal problem_count
        problem_count += 1
        if len(problems) < _PROBLEMS_KEPT:
            problems.append(text)

    for r in cr.rows(path, kinds=("frame",)):
        wind = r.get("wind")
        state[r["frame"]] = "off" if wind == "off" else "on" if isinstance(wind, dict) else None
        if isinstance(wind, dict):
            frames[r["frame"]] = wind
            for text in _frame_problems(r["frame"], wind):
                problem(text)
    rows = compared = agree = members_off = 0
    entries = entries_matched = frames_entries = 0
    reasons, predicted = {}, {}
    for frame in sorted(state):
        scene_rows, other_rows = {}, {}
        for r in cr.rows(path, kinds=("draw",), frame=frame):
            if isinstance(r.get("wind"), dict) and r["wind"].get("reason") == "none":
                members = scene_rows if r.get("in_scene") else other_rows
                key = _entry_key(cr.vs_hash(r), r["wind"])
                members[key] = members.get(key, 0) + 1
            h = cr.vs_hash(r)
            member = r.get("wind")
            where = "line %d (%s, frame %d)" % (r["line"], h[:4] or "no shader", frame)
            if h not in lf.WIND_SHADERS:
                if member is not None:
                    problem("%s: a \"wind\" member on a draw whose shader has no row" % where)
                continue
            rows += 1
            want = expected_inputs(r, order)
            predicted[want["reason"]] = predicted.get(want["reason"], 0) + 1
            if state[frame] != "on":
                if member is not None:
                    members_off += 1
                    problem("%s: a \"wind\" member in a frame whose row says %s" % (
                        where, "off" if state[frame] == "off" else "nothing of the wind"))
                continue
            if member is None:
                problem("%s: no \"wind\" member on a wind draw in a frame with the wind on" % where)
                continue
            compared += 1
            reasons[member.get("reason")] = reasons.get(member.get("reason"), 0) + 1
            differ = [key for key in sorted(set(want) | set(member)) if want.get(key) != member.get(key)]
            agree += not differ
            for key in differ:
                problem("%s: \"%s\" is %r, the row's own evidence gives %r" % (where, key, member.get(key), want.get(key)))
        # The frame's stored entries against the members just read (after the rows: a member is first held to its
        # own row's evidence, above, and then the stored entry to the member).
        if state[frame] == "on" and "entries" in frames[frame]:
            frames_entries += 1
            texts, n, matched = _entries_problems(frame, frames[frame], scene_rows, other_rows)
            entries += n
            entries_matched += matched
            for text in texts:
                problem(text)
    for name, n in sorted(reasons.items()):
        if name != "none" and n > REASON_SHARE * compared:
            problem("reason \"%s\" in %d of %d wind draws: more than 1%%" % (name, n, compared))
    frames_on = sum(1 for v in state.values() if v == "on")
    frames_off = sum(1 for v in state.values() if v == "off")
    return InputsResult(rows, compared, agree, problems, problem_count, reasons, predicted, frames_on, frames_off,
                        frames_off > 0 and frames_on == 0 and members_off == 0, frames,
                        frames_on > 0 and compared > 0 and problem_count == 0, entries, entries_matched, frames_entries)


def cmd_inputs(args, say=print):
    bad = 0
    for capture in args.capture:
        path = cr.find_capture(capture)
        r = check_inputs(path)
        say("%s: %d wind draw rows; frames with the wind on %d, off %d" % (path.name, r.rows, r.frames_on, r.frames_off))
        say("  what the rows' own evidence gives (bank, literal table, fetches, streams): %s" % (
            ", ".join("%s %d" % kv for kv in sorted(r.predicted.items())) or "no wind draw row"))
        for frame, wind in sorted(r.frames.items()):
            say("  frame %d: records %s (%s), casters %s; by reason %s; casters by reason %s" % (
                frame, wind.get("records"), ", ".join("%s %s" % kv for kv in sorted((wind.get("family") or {}).items())),
                wind.get("casters"), json.dumps(wind.get("reason"), sort_keys=True),
                json.dumps(wind.get("caster_reason"), sort_keys=True)))
        if r.compared:
            say("  %d of %d compared rows agree with their own bank, fetches and streams; the members' reasons: %s" % (
                r.agree, r.compared, ", ".join("%s %d" % kv for kv in sorted(r.reasons.items()))))
        entries_judged = r.frames_on > 0 and r.frames_entries == r.frames_on
        if r.frames_on:
            if entries_judged:
                say("  stored entries (what the records' wind_index names in the scene's side arrays): %d of %d are held by "
                    "a draw row's member of the same shader, value for value, in %d frames" % (
                        r.entries_matched, r.entries, r.frames_entries))
            else:
                say("  stored entries: NOT JUDGED: %d of %d frames with the wind on have none in their row (a capture from "
                    "before the review's fix)" % (r.frames_on - r.frames_entries, r.frames_on))
        for text in r.problems:
            say("  PROBLEM: " + text)
        if r.problem_count > len(r.problems):
            say("  ... and %d more" % (r.problem_count - len(r.problems)))
        if getattr(args, "entries", False) and not args.off and not (entries_judged and r.entries > 0):
            say("  FAILED: --entries asks for the stored entries, and the capture has none to judge")
            bad += 1
            continue
        if args.off:
            ok = r.off_clean and r.problem_count == 0
            say("  off capture: %s" % ("every frame row says \"wind\":\"off\" and no row has a member: OK" if ok else "FAILED"))
        elif not r.compared and not r.problem_count:
            ok = False
            say("  NOT A RESULT: no wind draw row lies in a frame whose row has wind counts (%s)" % (
                "the capture was made with the switch off" if r.frames_off else
                "a capture from before plan Task 8, or made without the renderer's records"))
        else:
            ok = r.ok
            say("  " + ("OK" if ok else "FAILED"))
        bad += not ok
    return 1 if bad else 0


# --- command line ---------------------------------------------------------------------------------------------

def _result_line(name, r):
    return "%s: %d of %d trials agree, %d moved, worst relative error %.3e%s" % (
        name, r.trials - r.failed, r.trials, r.moved, r.worst, "" if not r.failed else "; first failure: %s" % r.first_failure)


def _one_shader(prefix):
    """The wind shader whose hash begins with `prefix`, or None."""
    return next((h for h in lf.WIND_SHADERS if h.startswith(prefix.upper())), None)


def cmd_formulas(args, say=print):
    bad = 0
    say("made inputs: %d trials a shader, seed %d; bound %g * max(1, |oPos|); moved: more than %g from rest" % (
        args.trials, args.seed, TOLERANCE, MOVED))
    for hash_ in lf.WIND_SHADERS:
        if args.only and not hash_.startswith(args.only.upper()):
            continue
        say(describe(hash_, args.dumps))
        r = check_formula(hash_, args.trials, args.seed, dumps=args.dumps)
        ok = r.failed == 0 and r.moved >= r.trials // 2
        bad += not ok
        say("  " + _result_line(hash_[:4], r) + (": OK" if ok else ": FAILED"))
        if args.mutations:
            for name in MUTATIONS:
                m = check_formula(hash_, min(args.trials, 200), args.seed, mutation=name, dumps=args.dumps)
                bad += m.failed == 0
                say("    mutation %-8s: %d of %d trials disagree%s" % (name, m.failed, m.trials, "" if m.failed else ": NOT CAUGHT"))
    return 1 if bad else 0


def cmd_rows(args, say=print):
    bad = 0
    only = _one_shader(args.only) if args.only else None
    if args.only and only is None:
        say("no wind shader %s" % args.only)
        return 1
    for capture in args.capture:
        path = cr.find_capture(capture)
        say("%s: wind draw rows with streams, %d vertices a draw (all of a smaller one)" % (path.name, args.per_draw))
        r = check_rows(path, hash_=only, per_draw=args.per_draw, dumps=args.dumps)
        for h, (rows, trials, failed, moved, worst) in sorted(r.by_shader.items()):
            say("  %s (%s, %s): %d rows, %d of %d vertices agree, %d moved, worst relative error %.3e" % (
                h, lf.WIND_SHADERS[h]["family"], lf.WIND_SHADERS[h]["role"], rows, trials - failed, trials, moved, worst))
        absent = [h[:4] for h in lf.WIND_SHADERS if h not in r.by_shader and only in (None, h)]
        if absent:
            say("  no row with streams: %s" % ", ".join(absent))
        ok = r.failed == 0 and r.trials > 0
        bad += not ok
        say("  " + _result_line("all", r) + (": OK" if ok else ": FAILED"))
    return 1 if bad else 0


def cmd_identities(args, say=print):
    bad = 0
    for capture in args.capture:
        path = cr.find_capture(capture)
        say("%s: a caster's microcode on a scene draw's streams and bank, %d vertices a draw" % (path.name, args.per_draw))
        lines, n = check_identities(path, per_draw=args.per_draw, per_pair=args.per_pair, dumps=args.dumps)
        for line in lines:
            say("  " + line)
        bad += n
    return 1 if bad else 0


def cmd_trace(args, say=print):
    hash_ = _one_shader(args.hash)
    if hash_ is None:
        say("no wind shader %s" % args.hash)
        return 1
    case = made_case(hash_, args.seed, args.trial, args.dumps)
    regs = trace(case, args.instr)
    say("%s, made trial %d of seed %d (vertex index %d), after instruction %d:" % (hash_, args.trial, args.seed, case.index, args.instr))
    for reg in sorted(regs):
        if any(regs[reg]):
            say("  r%-2d %s" % (reg, "  ".join("% .9g" % x for x in regs[reg])))
    ins = formula_inputs(case)
    say("the formula's inputs: %s" % {k: v for k, v in ins.items() if k not in ("rows", "mesh")})
    return 0


def main(argv=None, say=print):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dumps", type=Path, default=DUMPS)
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("formulas")
    p.add_argument("--trials", type=int, default=2000)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--only")
    p.add_argument("--mutations", action="store_true", help="also the four mutations of each shader (each must fail)")
    p.set_defaults(run=cmd_formulas)
    p = sub.add_parser("rows")
    p.add_argument("capture", nargs="+")
    p.add_argument("--per-draw", type=int, default=64)
    p.add_argument("--only")
    p.set_defaults(run=cmd_rows)
    p = sub.add_parser("identities")
    p.add_argument("capture", nargs="+")
    p.add_argument("--per-draw", type=int, default=16)
    p.add_argument("--per-pair", type=int, default=None)
    p.set_defaults(run=cmd_identities)
    p = sub.add_parser("trace")
    p.add_argument("hash")
    p.add_argument("--instr", type=int, required=True)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--trial", type=int, default=2)
    p.set_defaults(run=cmd_trace)
    p = sub.add_parser("inputs")
    p.add_argument("capture", nargs="+")
    p.add_argument("--off", action="store_true", help="the capture was made with the switch off: it must say so")
    p.add_argument("--entries", action="store_true",
                   help="the capture must hold the scenes' stored wind entries in every frame with the wind on (a capture "
                        "from before the review's fix has none and then fails here instead of being passed unjudged)")
    p.set_defaults(run=cmd_inputs)
    args = ap.parse_args(argv)
    if args.command != "inputs" and not args.dumps.is_dir():      # (the inputs' check reads no microcode)
        say("no shader dumps at %s (made by fable_2.exe --dump_shaders=<dir>)" % args.dumps)
        return 1
    return args.run(args, say)


if __name__ == "__main__":
    sys.exit(main())
