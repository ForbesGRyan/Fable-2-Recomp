"""The machine check of the lake's surface (light_formulas.water_vertex, water_alpha, water_kept, water_opaque_dead,
water_pixel and its parts) against the game's own microcode (ucode_eval): vertex shader BEAD84BD72072E0E and
pixel shaders A17D8AEC3A817D45 (the surface) and F6D98C7B4D98438B (the opaque twin). The water sub-project, plan
Tasks 2 and 3, spec criteria 1 (its first half: made inputs; the captured rows are plan Task 4's) and 2 part (a).

  formulas [--trials 2000] [--seed 1]       the vertex shader, the alpha and the fate of both pixel shaders and the
                                            whole pixel of A17D on made inputs; each mutation (each must fail); each
                                            literal held at its place; the floors a run must reach to count
  dead [--trials 2000] [--seed 1]           criterion 2 part (a): F6D9 kills every made pixel whose c141 satisfies
                                            light_formulas.water_opaque_dead, whatever the mask holds; the control
  used                                      the constant registers each of the three dumps reads (the interpreter's
                                            own list), the components that reach an output (one component changed at
                                            a time), and the interpolator components either pixel shader reads
  trace <hash> --instr N [--seed S] [--trial T]   the interpreter's registers after instruction N of one of the
                                            three shaders for a made input (the transcription's oracle)
Each exits 1 on any failure.

What a check is, and why it is not circular. One side is the dump's text run by ucode_eval.Machine on a made input
(registers, interpolators, vertex bytes, textures as callables); the other is the closed formula on the SAME input,
through nothing of the interpreter's (the vertex is decoded from the bytes by struct, not by the machine's fetch).
The formulas were written from the dump by hand; nothing of them was fitted to the interpreter's output. Both
sides are double precision on inputs rounded to single precision (as the game holds them), so a correct
transcription agrees to rounding (the worst differences are printed), and it is the mutations that show a bound
can tell: each changes one term of the formula and must make trials disagree.

The vertex check (check_vertex). A made draw is a vertex buffer of 1 to 64 vertices of 20 bytes (a position of
three big-endian floats, then the 32_32_FLOAT pair, as the dump's two fetches read them: stride 5 dwords, the pair
at dword 3) and an index. Compared: oPos, o0.xyz, o1 and o3.xy, each component within VERTEX_TOLERANCE *
max(1, |value|). Made inputs (the ranges at this file's head): the position within 2,000 units; the pair in
[0, 1]; c0..c3 with entries within 200; c115, c116 in [-4, 4]; c117, c118 in [0.01, 0.5]; the third and fourth
components of c115 to c118 at 1e21 to 1e22 with either sign, and every other register (c9, c113, c119 to c122
among them) random: a formula that read one would differ. VERTEX_MUTATIONS, each of which must fail: "rows" c116
for c115; "scales" c118 for c117; "axes" the coordinates from p.xz; "matrix" c0 and c1 exchanged.

The alpha check (check_alpha), for A17D (kind "surface") and F6D9 ("opaque"). The whole pixel shader runs on a made
pixel; compared are the three fates (killed: the machine's kill; skipped: not killed and no fetch of slot 2;
shaded) with light_formulas.water_kept, the output's alpha with water_alpha within ALPHA_TOLERANCE where the pixel
is not killed, a skipped pixel's colour with exactly 0, and the mask's fetch with interpolator o3.xy. The mask's
sample is 0 every 16th trial and 1 every 16th, otherwise one of the 256 levels a k_8 texture holds or any value in
[0, 1]; c141 is of three families by the trial's number: fog_check._water_setup's ranges (x in [0, 0.8], y in
[0.4, 1.4]), the captured (2.003, 2, 0.0015, 0.0588), and x, y anywhere in [-1, 3]. The threshold c141.w lies in
[1, 1.2] in four of ten trials of the first and third family (the plan's 0.15 of the first alone would leave F6D9's
"skipped", alpha 1 under a threshold of 1 or more, below a tenth of the trials: the floor the test holds).
ALPHA_MUTATIONS, each of which must fail: "ramp" c141.x and c141.y exchanged; "mask" m for 1 - m; "comparison" the
other shader's comparison.

The dead draw (check_dead; spec ruling 12, criterion 2 part (a)). F6D9 is run on made pixels whose c141 satisfies
water_opaque_dead (c141.y in [-4, 4], c141.x from max(0, c141.y) + 1e-6 up to 4, both single-precision numbers on
which the condition holds), each at three masks: 0, 1 and the trial's own (a k_8 level or any value in [0, 1]); a
trial counts as killed when the machine kills all three. Every fourth trial stands AT the margin: c141.x is the
smallest single-precision number for which the condition holds for its c141.y. Beside the machine (which is
double precision) the three steps of instr 11 to 13 are made in single precision, with the multiply-add both
rounded twice and fused, and must give an alpha below 1 ("single_killed"). All 256 levels a k_8 mask holds are run
with the captured c141 ("levels"). The control: every fourth trial once more with c141.x - c141.y <= 0 and a mask
of 0, which must NOT be killed. What this shows and what it assumes is in docs/native-renderer/lighting-formulas.md,
"Water: the dead draw".

The pixel check (check_pixel), A17D with the console's product rule (ucode_eval's zero_product). A made pixel is
the shader's text, 248 registers, the five interpolators (o0 the place, o1 the two coordinates, o2 unread, o3 the
mask's coordinate, r4 the position with random signs), the bools and seven textures as smooth callables (so that a
fetch at another place shows); the sampler hands the machine the texture's value in the components the fetch takes
and 7777 in the others. Compared: the fate; the alpha within ALPHA_TOLERANCE; the colour within PIXEL_TOLERANCE *
max(1, |value|) (1e-9, with b136 clear or set: far inside the spec's 1e-5 and 1e-3, see the constants); the order
of the fetched slots; and every fetch's coordinates with the formula's (the mask, the two normal fetches, slots 13
and 14, slot 8 under b137, the two fog tables) within COORD_TOLERANCE.
Made inputs: every register component a formula reads from half to twice its captured value (the constants at
this file's head; c131.x above 1 in a tenth of the trials; c138.x in [1, 256], below 8 in four of ten; c117.x in
[-0.6, 0.6]; c0.xy 0 in half of the trials and within 0.02 otherwise); EVERY OTHER COMPONENT of those registers
1e21 to 1e22 with either sign; the fate drawn first (15 killed, 15 skipped, 70 shaded of 100) and c141 and the
mask made for it (the captured c141 in half of the shaded and skipped); b136 set in six of ten, b137 in a quarter;
the distance below 1 / c140.x in half of the trials and up to fifteen times it otherwise; the fog's start before
the pixel in six of ten; the two normal samples anywhere in [0, 1]^2, (0.5, 0.5) every 20th trial; the sun within
0.05 or 0.3 of the direction the pixel's surface mirrors into the eye in six of ten trials and anywhere otherwise
(c142 times 0.9 to 1.05: it is not normalised; the aim takes the normal from light_formulas.water_normal, which
shapes the inputs alone: see _mirrored). Three trials of 40 are degenerate (PIXEL_KINDS): "flat_zero" a flat normal with c128.x = 0 (the
normal is 0 * infinity), "at_eye" a pixel at the eye (the view vector is 0 * infinity), "power_zero" c138.x = 0
with the sun below a level surface (0 * -infinity): the console's product gives 0 in each, an IEEE machine no number
(check_pixel(zero_product=False): it must disagree in such trials and in no other; it does not disagree in all of
them, because a saturate takes no number back to 0 and the colour can then be the same).
Counted beside agreement (a check that cannot fail is no check): the three fates; of the shaded: with the sun
term, with the shadow factor, near (the machine's own sat(c140.x * d) below 1) and far, fogged (the machine fetched
the tables), lit (the glint's cosine above 0) and glinting (the sun term above 1% of the colour); the degenerate
kinds. pixel_floors() names what a run
must reach.
PIXEL_MUTATIONS, each of which must fail: "base" c126 and c127 exchanged; "centre" the lookup scaled about 0
instead of the centre; "distort" c130 for c129 at slot 13; "lookup_axes" both lookups at (v, u); "flatten" c128.x
left out; "bias" c116.x with the other sign; "mix" c131.x left out (1); "near_far" the blend's two ends exchanged;
"range" c140.x changed by 1%; "slot14" left out of the near branch; "second_normal" c136.x left out; "power"
c138.x changed by 1%; "sun_sign" c142 with the other sign; "sun_scale" c137.x left out; "fog" c28.x left out;
"exposure" c20.w left out. ("literal", name) changes one literal by 1% in the formula alone; `shift=(name,
factor)` scales it in the formula AND at its place in the machine's constants: the two must then agree again.
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
VS, PS, PS_OPAQUE = lf.WATER_SHADER["vs"], lf.WATER_SHADER["ps"], lf.WATER_SHADER["ps_opaque"]
PIXEL_HASH = {"surface": PS, "opaque": PS_OPAQUE}

# The bounds. Spec Design 2 asks 1e-5 * max(1, |value|) of the vertex outputs and of the colour with b136 clear, 1e-3
# relative of the colour with b136 set and 1e-6 of the alpha. Both sides are the same double arithmetic, so the
# check holds 1e-9 instead (plan Task 3: with the spec's bounds a 1% change of the glint's power went unseen in 384
# of 400 trials); it implies the spec's bounds wherever a colour is above 1e-6.
VERTEX_TOLERANCE = 1e-9     # |value - formula| <= 1e-9 * max(1, |value|), every component of the vertex outputs
ALPHA_TOLERANCE = 1e-9      # the alpha
PIXEL_TOLERANCE = 1e-9      # the colour, with b136 clear or set: 1e-9 * max(1, |value|)
COORD_TOLERANCE = 1e-9      # a fetch's coordinates against the formula's

# The made vertex (module text).
STRIDE_BYTES = 20           # `vfetch_full ... Stride=5`: the position at dword 0, the pair at dword 3
PAIR_OFFSET = 3
MOST_VERTICES = 64
PLACE_FAR = 2000.0
MATRIX_ENTRY = 200.0
SCROLL = (-4.0, 4.0)        # c115.xy, c116.xy
SCALE = (0.01, 0.5)         # c117.xy, c118.xy
GARBAGE = (1e21, 1e22)      # a component no formula reads, with either sign
VERTEX_MUTATIONS = ("rows", "scales", "axes", "matrix")

# The made pixel (module text). CAPTURED: the registers of the first A17D row of frame 1 of capture 155547
# (out/native-evidence/water/b_water_d1_f1.txt), the centre of each made range (half to twice the value).
CAPTURED_C141 = (2.003, 2.0, 0.0015, 0.0588235)
CAPTURED = {"c0zw": (1.0 / 1120.0, 1.0 / 720.0), "c20w": 9.15686, "c31xy": (2.0 / 3.0, 2.0 / 3.0), "c116x": 0.104081,
            "c126": (0.027451, 0.0745098, 0.0431373), "c127": (0.0588235, 0.129412, 0.121569), "c128x": 0.3,
            "c129xy": (0.420816, 0.420816), "c130xy": (0.420816, 0.420816), "c131x": 0.5, "c136x": 0.279184,
            "c137x": 0.5, "c138x": 128.0, "c139xy": (0.8, 0.2), "c140x": 0.05}
C117X = (-0.6, 0.6)         # captured: -0.420816
POWER_SMALL, POWER_LARGE = (1.0, 8.0), (8.0, 256.0)
JUNK = 7777.0               # what the sampler puts in a component the fetch does not take
ALPHA_MUTATIONS = ("ramp", "mask", "comparison")
PIXEL_KINDS = {7: "flat_zero", 19: "at_eye", 31: "power_zero"}      # by trial % 40
PIXEL_MUTATIONS = ("base", "centre", "distort", "lookup_axes", "flatten", "bias", "mix", "near_far", "range", "slot14",
                   "second_normal", "power", "sun_sign", "sun_scale", "fog", "exposure")
HELD_LITERALS = ("half", "one", "two", "minus_one", "minus_two")    # "zero" cannot be scaled
# {register: the components no formula reads}, from light_formulas.WATER_SHADER["ps_read"], which
# read_components("ps") holds to the interpreter's own account (tests/test_water_formulas.py).
UNREAD = {reg: tuple(k for k in range(4) if k not in lf.WATER_SHADER["ps_read"][reg]) for reg in lf.WATER_SHADER["ps_reg"]}

# trials; those that disagree; the largest relative error; the first failure's text (None).
Result = namedtuple("Result", "trials failed worst first_failure")
# trials; those that disagree; by the machine's fate: killed, skipped, shaded; the largest alpha difference.
AlphaResult = namedtuple("AlphaResult", "trials failed killed skipped shaded worst first_failure")
# trials; those the machine killed at all three masks; controls not killed; controls; trials whose alpha is below 1
# in single precision too; trials at the margin; the k_8 levels killed with the captured c141 (of 256).
DeadResult = namedtuple("DeadResult", "trials killed control_kept controls single_killed at_margin levels")
# trials; those that disagree; by the machine's fate: killed, skipped, shaded; of the shaded: with the sun term
# (b136), with the shadow factor (b137), near, far, fogged; the largest relative error of the colour; the first
# failure's text (None); of the shaded: the sun term above 1% of the colour; the trials of each degenerate kind; the
# failed trials by kind ("" for a trial that is not degenerate); of the shaded: the sun term above 0.
PixelResult = namedtuple("PixelResult", "trials failed killed skipped shaded sun shadow near far fogged worst first_failure "
                                        "glinting kinds failed_kinds lit")


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0] if math.isfinite(x) else x


def _text(dumps, hash_):
    return ue.load(dumps, hash_, "vert" if hash_ == VS else "frag")


def _garbage(rng):
    return f32(rng.choice((-1.0, 1.0)) * rng.uniform(*GARBAGE))


def _unit(rng):
    while True:
        v = [rng.uniform(-1.0, 1.0) for _ in range(3)]
        n = math.sqrt(sum(x * x for x in v))
        if 0.1 < n <= 1.0:
            return [x / n for x in v]


def _compare(got, want, tolerance):
    """(agree, the largest error, |got - want| / max(1, |got|), of the components that are numbers on both sides)."""
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


def _check_mutation(mutation, names, literals=()):
    if mutation is None or mutation in names:
        return
    if isinstance(mutation, tuple) and len(mutation) == 2 and mutation[0] == "literal" and mutation[1] in literals:
        return
    raise ValueError("unknown mutation %r (one of %s)" % (mutation, ", ".join(names)))


# --- the vertex shader --------------------------------------------------------------------------------------------

class VertexCase:
    """One draw of BEAD: the microcode's text, its constants ({register: [4]}), the vertex buffer's bytes, the
    index of the vertex to run, and the value the shader's own fetch of the mask (tf16) is given."""

    def __init__(self, text, consts, storage, index, mask):
        self.text, self.consts, self.storage, self.index, self.mask = text, consts, storage, index, mask


def made_vertex(seed=1, trial=0, dumps=DUMPS, text=None):
    """The made draw of one trial (deterministic in seed and trial)."""
    rng = random.Random("water-vertex/%d/%d" % (seed, trial))
    text = text if text is not None else _text(dumps, VS)
    c = {reg: [f32(rng.uniform(-2.0, 2.0)) for _ in range(4)] for reg in range(256)}
    for reg in (0, 1, 2, 3):
        c[reg] = [f32(rng.uniform(-MATRIX_ENTRY, MATRIX_ENTRY)) for _ in range(4)]
    for reg in (115, 116):
        c[reg] = [f32(rng.uniform(*SCROLL)), f32(rng.uniform(*SCROLL)), _garbage(rng), _garbage(rng)]
    for reg in (117, 118):
        c[reg] = [f32(rng.uniform(*SCALE)), f32(rng.uniform(*SCALE)), _garbage(rng), _garbage(rng)]
    count = rng.randrange(1, MOST_VERTICES + 1)
    vertices = [[rng.uniform(-PLACE_FAR, PLACE_FAR) for _ in range(3)] + [rng.random(), rng.random()] for _ in range(count)]
    storage = b"".join(struct.pack(">5f", *v) for v in vertices)
    return VertexCase(text, c, storage, rng.randrange(count), rng.random())


def case_vertex(case):
    """(the position, the pair) of the case's vertex, decoded from its bytes (not through the machine)."""
    at = case.index * STRIDE_BYTES
    return struct.unpack_from(">3f", case.storage, at), struct.unpack_from(">2f", case.storage, at + 4 * PAIR_OFFSET)


def run_vertex(case, consts=None, text=None):
    """(the microcode's outputs for the case's vertex, the machine). The fetch floors its index and decodes through
    capture_replay.element; the shader's own fetch of the mask (tf16) is given the case's value."""

    def vfetch(stream, offset, attrs, idx):
        if stream != 0 or not math.isfinite(idx):
            raise IndexError("vf%d index %s" % (stream, idx))
        at = int(math.floor(idx)) * STRIDE_BYTES + 4 * offset
        if at < 0 or at + 8 > len(case.storage):
            raise IndexError("vf%d index %s" % (stream, idx))
        return cr.element(case.storage, at, attrs)

    def sampler(slot, coords, attrs):
        if slot == 16:
            return [case.mask, JUNK, JUNK, JUNK]
        raise KeyError("vertex texture tf%d" % slot)

    m = ue.Machine(text or case.text, consts or case.consts, sampler, regs={0: [float(case.index), 0.0, 0.0, 0.0]},
                   vfetch=vfetch, zero_product=True)
    return m.run(), m


def _vertex_values(out):
    return ([float(x) for x in out["oPos"]] + [float(x) for x in out["o0"][:3]] + [float(x) for x in out["o1"]]
            + [float(x) for x in out["o3"][:2]])


def formula_vertex(case, mutation=None):
    """water_vertex for the case's vertex from its bytes and registers: oPos, o0.xyz, o1 and o3.xy as one list of
    thirteen. `mutation`: one of VERTEX_MUTATIONS."""
    p, pair = case_vertex(case)
    c = {reg: list(case.consts[reg]) for reg in (0, 1, 2, 3) + tuple(lf.WATER_SHADER["reg"])}
    if mutation == "rows":
        c[115] = c[116]
    elif mutation == "scales":
        c[117] = c[118]
    elif mutation == "matrix":
        c[0], c[1] = c[1], c[0]
    out = lf.water_vertex(p, pair, c)
    if mutation == "axes":
        out["coord"] = lf.water_vertex((p[0], p[2], p[1]), pair, c)["coord"]
    return list(out["pos"]) + list(out["place"]) + list(out["coord"]) + list(out["mask"])


def check_vertex(trials=2000, seed=1, mutation=None, dumps=DUMPS):
    """`trials` made draws of BEAD against water_vertex: Result."""
    _check_mutation(mutation, VERTEX_MUTATIONS)
    text = _text(dumps, VS)
    failed, worst, first = 0, 0.0, None
    for k in range(trials):
        case = made_vertex(seed, k, text=text)
        got = _vertex_values(run_vertex(case)[0])
        want = formula_vertex(case, mutation)
        ok, error = _compare(got, want, VERTEX_TOLERANCE)
        worst = max(worst, error)
        if not ok:
            failed += 1
            first = first or "trial %d (seed %d, vertex %d): relative error %.3e; microcode %s, formula %s" % (
                k, seed, case.index, error, got, want)
    return Result(trials, failed, worst, first)


# --- a made pixel -------------------------------------------------------------------------------------------------

class PixelCase:
    """One pixel of A17D or F6D9 (`kind`): the microcode's text, its constants, the interpolators ({0: o0, the
    place; 1: o1, the two coordinates; 2: o2, read by neither shader; 3: o3, the mask's coordinate; 4: the position
    register}), the bools set, the mask's sample `m` at o3.xy, the textures (callables: mask(u, v) -> float,
    normal(u, v) -> (x, y), mirror and near14 (u, v) -> rgb, shadow(u, v) -> float, tf4 and tf5 (x) -> rgb), the
    made distance, and what a made case is (`label`: "" or the name of a degenerate kind)."""

    def __init__(self, text, kind, consts, regs, bools, m, mask, normal, mirror, near14, shadow, tf4, tf5, distance, label=""):
        (self.text, self.kind, self.consts, self.regs, self.bools, self.m, self.mask, self.normal, self.mirror, self.near14,
         self.shadow, self.tf4, self.tf5, self.distance, self.label) = (
            text, kind, consts, regs, bools, m, mask, normal, mirror, near14, shadow, tf4, tf5, distance, label)

    def tex(self):
        """The textures as light_formulas.water_pixel takes them."""
        return {"normal": self.normal, 13: self.mirror, 14: self.near14, 8: self.shadow, "tf4": self.tf4, "tf5": self.tf5}


def _wave(rng, channels, low, high):
    """A random smooth function of two coordinates per channel with values in [low, high]."""
    k = [[rng.uniform(0.3, 2.0), rng.uniform(0.3, 2.0), rng.uniform(0.0, 6.0)] for _ in range(channels)]
    mid, half = (low + high) / 2.0, (high - low) / 2.0
    return lambda u, v: [mid + half * math.sin(a * u + b * v + p) for a, b, p in k]


def _around(rng, value):
    return f32(value * rng.uniform(0.5, 2.0))


def _mirrored(view, normal, coords, c128x, c136x):
    """The direction a made pixel's surface mirrors into the eye: `view` reflected about the sun's second normal.
    It only AIMS a made sun (through light_formulas.water_normal: a wrong normal there would aim the made suns
    badly, and every trial would still be compared with the machine)."""
    _, n = lf.water_normal(normal(coords[0], coords[1]), normal(coords[2], coords[3]), c128x)
    n2 = lf.normalize([n[0], n[1], c136x * n[2]])
    twice = 2.0 * lf.dot(n2, view)
    return [twice * n2[k] - view[k] for k in range(3)]


def _base_pixel(rng, text, kind):
    """A made pixel but for its mask sample and c141: every register a formula reads about its captured value,
    every other component of those registers garbage, every other register random."""
    c = {reg: [f32(rng.uniform(-1.0, 1.0)) for _ in range(4)] for reg in range(248)}
    for reg in lf.WATER_SHADER["ps_reg"]:
        c[reg] = [_garbage(rng) for _ in range(4)]
    for reg, values in lf.WATER_SHADER["ps_lit"].items():
        c[reg] = [float(v) for v in values]
    offset = (0.0, 0.0) if rng.random() < 0.5 else (rng.uniform(-0.02, 0.02), rng.uniform(-0.02, 0.02))
    c[0] = [f32(offset[0]), f32(offset[1]), _around(rng, CAPTURED["c0zw"][0]), _around(rng, CAPTURED["c0zw"][1])]
    eye = [f32(rng.uniform(-300.0, 300.0)), f32(rng.uniform(-300.0, 300.0)), f32(rng.uniform(20.0, 80.0))]
    c[19][:3] = eye
    c[20][3] = _around(rng, CAPTURED["c20w"])
    c[28][:2] = [f32(rng.uniform(0.3, 1.0)), f32(rng.uniform(0.3, 1.5))]
    c[31][:2] = [_around(rng, v) for v in CAPTURED["c31xy"]]
    c[66][:3] = [f32(x) for x in _unit(rng)]
    c[68][:3] = [f32(rng.uniform(0.1, 1.0)) for _ in range(3)]
    c[116][0] = _around(rng, CAPTURED["c116x"])
    c[117][0] = f32(rng.uniform(*C117X))
    c[126][:3] = [_around(rng, v) for v in CAPTURED["c126"]]
    c[127][:3] = [_around(rng, v) for v in CAPTURED["c127"]]
    c[128][0] = _around(rng, CAPTURED["c128x"])
    c[129][:2] = [_around(rng, v) for v in CAPTURED["c129xy"]]
    c[130][:2] = [_around(rng, v) for v in CAPTURED["c130xy"]]
    c[131][0] = f32(rng.uniform(1.0, 3.0)) if rng.random() < 0.1 else _around(rng, CAPTURED["c131x"])
    c[136][0] = _around(rng, CAPTURED["c136x"])
    c[137][0] = _around(rng, CAPTURED["c137x"])
    c[138][0] = f32(rng.uniform(*(POWER_SMALL if rng.random() < 0.4 else POWER_LARGE)))
    c[139][:2] = [_around(rng, v) for v in CAPTURED["c139xy"]]
    c[140][0] = _around(rng, CAPTURED["c140x"])
    c[143][:3] = [f32(rng.uniform(0.2, 3.0)) for _ in range(3)]
    # The place: below the eye and away from it, on either side of the range 1 / c140.x.
    reach = 1.0 / c[140][0]
    distance = reach * (rng.uniform(0.05, 0.98) if rng.random() < 0.5 else rng.uniform(1.02, 15.0))
    way = _unit(rng)
    way[2] = -abs(way[2])
    place = [f32(eye[k] + distance * way[k]) for k in range(3)]
    distance = math.sqrt(sum((eye[k] - place[k]) ** 2 for k in range(3)))
    start = distance * rng.uniform(0.1, 0.9) if rng.random() < 0.6 else distance + rng.uniform(1.0, 50.0)
    slope = rng.uniform(0.001, 0.02) if rng.random() < 0.5 else rng.uniform(0.02, 1.0)
    c[67][0], c[67][2], c[67][3] = f32(start), f32(slope), f32(rng.uniform(0.0005, 0.01))
    # The sun: in six trials of ten near the direction that the surface mirrors into the eye (so that the power of
    # the cosine is not 0 nearly everywhere), anywhere otherwise; it is aimed below, once the normal map is made.
    view = [(eye[k] - place[k]) / distance for k in range(3)] if distance > 0.0 else [0.0, 0.0, 1.0]
    aim = [rng.uniform(-s, s) for s in [rng.choice((0.05, 0.3))] * 3] if rng.random() < 0.6 else None
    light = _unit(rng) if aim is None else None
    length = rng.uniform(0.9, 1.05)
    bools = set()
    if rng.random() < 0.6:
        bools.add(136)
    if rng.random() < 0.25:
        bools.add(137)
    position = [rng.choice((-1.0, 1.0)) * float(rng.randrange(0, 1120)), rng.choice((-1.0, 1.0)) * float(rng.randrange(0, 720)),
                f32(rng.uniform(-9.0, 9.0)), f32(rng.uniform(-9.0, 9.0))]
    at = [f32(rng.random()), f32(rng.random())]
    regs = {0: place + [f32(rng.uniform(-9.0, 9.0))],
            1: [f32(rng.uniform(-50.0, 50.0)) for _ in range(4)],
            2: [f32(rng.uniform(-9.0, 9.0)) for _ in range(4)],
            3: at + [f32(rng.uniform(-9.0, 9.0)), f32(rng.uniform(-9.0, 9.0))],
            4: position}
    wave2, mirror, near14, shadow1 = _wave(rng, 2, 0.0, 1.0), _wave(rng, 3, 0.0, 1.5), _wave(rng, 3, 0.0, 1.5), _wave(rng, 1, 0.0, 1.0)
    tf4 = lf.table_1d([[rng.uniform(0.05, 3.0) for _ in range(3)] for _ in range(64)])
    tf5 = lf.table_1d([[rng.random() for _ in range(3)] for _ in range(256)])
    if light is None:
        light = [mirrored + off for mirrored, off in zip(_mirrored(view, wave2, regs[1], c[128][0], c[136][0]), aim)]
        n = math.sqrt(sum(x * x for x in light)) or 1.0
        light = [x / n for x in light]
    c[142][:3] = [f32(-x * length) for x in light]      # c142 points FROM the sun and is not a unit vector
    return PixelCase(text, kind, c, regs, bools, 0.0, None, lambda u, v: tuple(wave2(u, v)), mirror, near14,
                     lambda u, v: shadow1(u, v)[0], tf4, tf5, distance)


def _set_mask(case, m, c141):
    """Gives the case its mask sample (exactly `m` at o3.xy, another value elsewhere) and c141's read components."""
    at = case.regs[3][:2]
    case.m = m
    case.mask = lambda u, v: m + 0.25 * (u - at[0]) - 0.125 * (v - at[1])
    case.consts[141][0], case.consts[141][1], case.consts[141][3] = f32(c141[0]), f32(c141[1]), f32(c141[3])
    return case


def _mask_sample(rng, trial):
    if trial % 16 == 0:
        return 0.0
    if trial % 16 == 8:
        return 1.0
    return f32(rng.randrange(256) / 255.0) if rng.random() < 0.5 else f32(rng.random())


def made_alpha(seed=1, trial=0, dumps=DUMPS, text=None, kind="surface"):
    """The made pixel of one trial of the alpha check (deterministic in seed and trial; module text)."""
    rng = random.Random("water-alpha/%d/%d" % (seed, trial))
    case = _base_pixel(rng, text if text is not None else _text(dumps, PIXEL_HASH[kind]), kind)
    m = _mask_sample(rng, trial)
    family = trial % 3
    high = rng.random() < 0.4
    if family == 0:         # fog_check._water_setup's ranges
        c141 = [rng.uniform(0.0, 0.8), rng.uniform(0.4, 1.4), 0.0, rng.uniform(1.0, 1.2) if high else rng.uniform(0.0, 0.3)]
    elif family == 1:
        c141 = list(CAPTURED_C141)
    else:
        c141 = [rng.uniform(-1.0, 3.0), rng.uniform(-1.0, 3.0), 0.0, rng.uniform(1.0, 1.2) if high else rng.uniform(0.0, 1.0)]
    return _set_mask(case, m, c141)


def shaded_case(seed=1, trial=0, dumps=DUMPS, text=None, kind="surface"):
    """A made pixel that either shader shades with both bools set and the fog begun: what `used` and the read
    lists run (a killed or skipped pixel reads next to nothing)."""
    rng = random.Random("water-shaded/%d/%d" % (seed, trial))
    case = _base_pixel(rng, text if text is not None else _text(dumps, PIXEL_HASH[kind]), kind)
    case.bools = {136, 137}
    case.consts[67][0] = f32(case.distance * rng.uniform(0.1, 0.9))
    if kind == "surface":
        return _set_mask(case, f32(rng.uniform(0.0, 0.45)), CAPTURED_C141)       # alpha 0.9 to 0.997
    return _set_mask(case, f32(rng.uniform(0.0, 0.4)), (0.5, 1.0, 0.0, 0.0588235))   # the ramp below 0: alpha 1


def run_pixel(case, consts=None, text=None, bools=None, zero_product=True, regs=None):
    """(the pixel shader's outputs, the machine). Each texture is the case's at the coordinate the microcode
    fetches at, in the components the fetch takes, JUNK in the others; any other slot is an error."""

    def sampler(slot, coords, attrs):
        if slot == 1:
            return [case.mask(coords[0], coords[1]), JUNK, JUNK, JUNK]
        if slot == 2:
            x, y = case.normal(coords[0], coords[1])
            return [x, y, JUNK, JUNK]
        if slot == 13:
            return list(case.mirror(coords[0], coords[1])) + [JUNK]
        if slot == 14:
            return list(case.near14(coords[0], coords[1])) + [JUNK]
        if slot == 8:
            return [case.shadow(coords[0], coords[1]), JUNK, JUNK, JUNK]
        if slot == 4:
            return list(case.tf4(coords[0])) + [JUNK]
        if slot == 5:
            return list(case.tf5(coords[0])) + [JUNK]
        raise KeyError("pixel texture tf%d" % slot)

    m = ue.Machine(text or case.text, consts or case.consts, sampler,
                   regs={r: list(v) for r, v in (regs or case.regs).items()},
                   bools=case.bools if bools is None else bools, zero_product=zero_product)
    return m.run(), m


def machine_fate(m):
    """What the machine did with a pixel: "killed", "skipped" (no fetch of the normal map) or "shaded"."""
    if m.killed:
        return "killed"
    return "shaded" if any(f[1] == 2 for f in m.fetches) else "skipped"


# --- the alpha and the fate ---------------------------------------------------------------------------------------

def check_alpha(trials=2000, seed=1, mutation=None, kind="surface", dumps=DUMPS):
    """`trials` made pixels of A17D ("surface") or F6D9 ("opaque") against water_alpha and water_kept: AlphaResult."""
    _check_mutation(mutation, ALPHA_MUTATIONS)
    text = _text(dumps, PIXEL_HASH[kind])
    counts = {"killed": 0, "skipped": 0, "shaded": 0}
    failed, worst, first = 0, 0.0, None
    for k in range(trials):
        case = made_alpha(seed, k, text=text, kind=kind)
        out, m = run_pixel(case)
        got = machine_fate(m)
        counts[got] += 1
        c141 = list(case.consts[141])
        if mutation == "ramp":
            c141[0], c141[1] = c141[1], c141[0]
        alpha = lf.water_alpha(1.0 - case.m if mutation == "mask" else case.m, c141)
        other = {"surface": "opaque", "opaque": "surface"}[kind]
        want = lf.water_kept(alpha, c141, other if mutation == "comparison" else kind)
        ok = got == want and m.fetches[0][1:] == (1, tuple(case.regs[3][:2]))
        if ok and got != "killed":
            error = abs(out["oC0"][3] - alpha)
            worst = max(worst, error)
            ok = error <= ALPHA_TOLERANCE
            if got == "skipped":
                ok = ok and list(out["oC0"][:3]) == [0.0, 0.0, 0.0]
        if not ok:
            failed += 1
            first = first or "trial %d (seed %d, %s): mask %r, c141 %s: the machine %s with alpha %r, the formula %s with %r" % (
                k, seed, kind, case.m, case.consts[141], got, out["oC0"][3], want, alpha)
    return AlphaResult(trials, failed, counts["killed"], counts["skipped"], counts["shaded"], worst, first)


# --- the dead draw ------------------------------------------------------------------------------------------------

def _next_single(x):
    """The next single-precision number above x (a finite single)."""
    bits = struct.unpack("<i", struct.pack("<f", x))[0]
    if x == 0.0:
        return struct.unpack("<f", struct.pack("<i", 1))[0]
    return struct.unpack("<f", struct.pack("<i", bits + 1 if x > 0.0 else bits - 1))[0]


def dead_margin_x(y, margin=1e-6):
    """The smallest single-precision c141.x for which water_opaque_dead holds with this single-precision c141.y."""
    x = f32(max(0.0, y) + margin)
    while not lf.water_opaque_dead((x, y, 0.0, 0.0), margin):
        x = _next_single(x)
    return x


def single_alpha(m, c141, fused=False):
    """Instr 11 to 13 with every step rounded to single precision: 1 - sat(-(1 - m) * c141.y + c141.x); `fused`:
    the multiply-add rounded once (the product exact), as a fused operation makes it."""
    inverse = f32(1.0 - f32(m))
    y, x = f32(c141[1]), f32(c141[0])
    ramp = f32(-inverse * y + x) if fused else f32(f32(-inverse * y) + x)
    return f32(1.0 - min(max(ramp, 0.0), 1.0))


def check_dead(trials=2000, seed=1, dumps=DUMPS):
    """Criterion 2 part (a) on made inputs (module text): DeadResult."""
    text = _text(dumps, PS_OPAQUE)
    killed = control_kept = controls = single_killed = at_margin = 0
    for k in range(trials):
        rng = random.Random("water-dead/%d/%d" % (seed, k))
        case = _base_pixel(rng, text, "opaque")
        y = f32(rng.uniform(-4.0, 4.0))
        own = _mask_sample(rng, k)
        if k % 4 == 1:
            at_margin += 1
            x = dead_margin_x(y)
        else:
            x = f32(rng.uniform(max(0.0, y) + 1e-6, 4.0))
            while not lf.water_opaque_dead((x, y, 0.0, 0.0)):       # the rounding to single precision fell below
                x = _next_single(x)
        c141 = (x, y, 0.0, rng.uniform(0.0, 1.2))
        if not lf.water_opaque_dead(c141) or x > 4.0:
            raise AssertionError("a made c141 %s does not satisfy the condition" % (c141,))
        masks = (0.0, 1.0, own)
        killed += all(run_pixel(_set_mask(case, m, c141))[1].killed for m in masks)
        single_killed += all(single_alpha(m, c141, fused) < 1.0 for m in masks for fused in (False, True))
        if k % 4 == 0:
            controls += 1
            low = f32(y - rng.uniform(0.0, 1.0))                    # c141.x - c141.y <= 0: with a mask of 0 the alpha is 1
            control_kept += not run_pixel(_set_mask(case, 0.0, (low, y, 0.0, 0.0588235)))[1].killed
    rng = random.Random("water-dead-levels/%d" % seed)
    case = _base_pixel(rng, text, "opaque")
    levels = sum(run_pixel(_set_mask(case, f32(level / 255.0), CAPTURED_C141))[1].killed for level in range(256))
    return DeadResult(trials, killed, control_kept, controls, single_killed, at_margin, levels)


# --- the pixel ----------------------------------------------------------------------------------------------------

def _for_fate(rng, fate, kind, m):
    """A c141 that gives the made pixel `fate` under shader `kind` with the mask sample m (module text)."""
    y = rng.uniform(0.4, 3.0)
    lit = fate == ("killed" if kind == "opaque" else "shaded") or (kind == "surface" and fate == "skipped")
    if lit:                                     # the ramp above 0: alpha below 1
        ramp = rng.uniform(0.02, 0.9) if fate != "skipped" or rng.random() < 0.5 else rng.uniform(0.9, 1.5)
        alpha = 1.0 - min(ramp, 1.0)
        if fate == "skipped":
            threshold = min(alpha + rng.uniform(0.0, 0.2), 1.2)
        else:
            threshold = alpha * rng.uniform(0.0, 0.95)
    else:                                       # the ramp at or below 0: alpha 1
        ramp = -rng.uniform(0.0, 0.5)
        threshold = rng.uniform(1.0, 1.2) if fate == "skipped" else rng.uniform(0.0, 0.95)
    return [ramp + (1.0 - m) * y, y, 0.0, threshold]


def made_pixel(seed=1, trial=0, dumps=DUMPS, text=None, kind="surface"):
    """The made pixel of one trial of the pixel check (deterministic in seed and trial; module text)."""
    rng = random.Random("water-pixel/%d/%d" % (seed, trial))
    case = _base_pixel(rng, text if text is not None else _text(dumps, PIXEL_HASH[kind]), kind)
    label = PIXEL_KINDS.get(trial % 40, "")
    draw = rng.random()
    fate = "shaded" if label or draw < 0.7 else "killed" if draw < 0.85 else "skipped"
    captured = kind == "surface" and fate != "killed" and rng.random() < 0.5
    if captured:            # the captured ramp: alpha above the threshold for a mask below 0.47, 0 from 0.5015
        m = f32(rng.uniform(0.0, 0.47) if fate == "shaded" else rng.uniform(0.502, 1.0))
        c141 = list(CAPTURED_C141)
    else:
        m = _mask_sample(rng, trial)
        c141 = _for_fate(rng, fate, kind, m)
    _set_mask(case, m, c141)
    flat = (0.5, 0.5)
    if trial % 20 == 3 or label in ("flat_zero", "power_zero"):
        case.normal = lambda u, v: flat
    if label == "flat_zero":
        case.consts[128][0] = 0.0
    elif label == "at_eye":
        case.regs[0][:3] = case.consts[19][:3]
        case.distance = 0.0
    elif label == "power_zero":
        case.bools = case.bools | {136}
        case.consts[138][0] = 0.0
        case.consts[142][:3] = [0.0, 0.0, f32(rng.uniform(0.9, 1.05))]         # the sun below a level surface: the cosine is 0
    case.label = label
    return case


def _case_inputs(case, consts=None):
    c = consts or case.consts
    return (case.regs[4][:2], case.regs[0][:3], case.regs[1][:2], case.regs[1][2:4],
            {reg: list(c[reg]) for reg in lf.WATER_SHADER["ps_reg"]})


def formula_pixel(case, mutation=None, lit=None, consts=None):
    """light_formulas.water_pixel for the case (its whole result). `mutation`: one of PIXEL_MUTATIONS or
    ("literal", name); a mutant is the same function on changed inputs (a register, a texture read elsewhere),
    so that it is a mutant of the formula that is checked."""
    q, P, coord_a, coord_b, c = _case_inputs(case, consts)
    lit = dict(lit or lf.water_literals())
    tex = case.tex()
    moved = None
    if isinstance(mutation, tuple):
        lit[mutation[1]] *= 1.01
    elif mutation == "base":
        c[126], c[127] = c[127], c[126]
    elif mutation == "centre":                  # (x * c31) in the place of ((x - 1/2) * c31 + 1/2)
        dx, dy = 0.5 * c[31][0] - 0.5, 0.5 * c[31][1] - 0.5
        moved = lambda u, v: (u + dx, v + dy)                                   # noqa: E731
    elif mutation == "distort":
        c[129] = c[130]
    elif mutation == "lookup_axes":
        moved = lambda u, v: (v, u)                                             # noqa: E731
    elif mutation == "flatten":
        c[128][0] = 1.0
    elif mutation == "bias":
        c[116][0] = -c[116][0]
    elif mutation == "mix":
        c[131][0] = 1.0
    elif mutation == "near_far":                # the blend's weight w for 1 - w
        first = lf.water_pixel(q, P, coord_a, coord_b, case.m, c, tex, bools=case.bools, kind=case.kind, lit=lit)
        if first["fate"] == "shaded" and first["distance"] > 0.0:
            w = lf.sat(c[140][0] * first["distance"])
            c[140][0] = (1.0 - w) / first["distance"]
    elif mutation == "range":
        c[140][0] *= 1.01
    elif mutation == "slot14":                  # the near branch's own blend at 1: slot 13 alone
        c[117][0] = 1e9
    elif mutation == "second_normal":
        c[136][0] = 1.0
    elif mutation == "power":
        c[138][0] *= 1.01
    elif mutation == "sun_sign":
        c[142] = [-x for x in c[142]]
    elif mutation == "sun_scale":
        c[137][0] = 1.0
    elif mutation == "fog":
        c[28][0] = 1.0
    elif mutation == "exposure":
        c[20][3] = 1.0
    if moved is not None:
        tex[13] = lambda u, v, f=tex[13]: f(*moved(u, v))                       # noqa: E731
        tex[14] = lambda u, v, f=tex[14]: f(*moved(u, v))                       # noqa: E731
    out = lf.water_pixel(q, P, coord_a, coord_b, case.m, c, tex, bools=case.bools, kind=case.kind, lit=lit)
    if moved is not None and out["fate"] == "shaded":
        out["lookup"], out["lookup14"] = moved(*out["lookup"]), moved(*out["lookup14"])
    return out


def _shifted(consts, lit, shift):
    """(constants, literals) with literal `shift[0]` scaled by `shift[1]` at its place and by its name."""
    name, factor = shift
    reg, comp = lf.WATER_SHADER["ps_lit_at"][name]
    consts = dict(consts)
    consts[reg] = list(consts[reg])
    consts[reg][comp] *= factor
    lit = dict(lit)
    lit[name] *= factor
    return consts, lit


def expected_fetches(case, want):
    """[(slot, coordinates)] the machine must have fetched, in order, for the formula's result `want`."""
    out = [(1, tuple(case.regs[3][:2]))]
    if want["fate"] != "shaded":
        return out
    out += [(2, tuple(case.regs[1][:2])), (2, tuple(case.regs[1][2:4])), (13, tuple(want["lookup"])), (14, tuple(want["lookup14"]))]
    if want["shadow_at"] is not None:
        out.append((8, tuple(want["shadow_at"])))
    if want["fog_at"] is not None:
        out += [(4, (want["fog_at"][0],)), (5, (want["fog_at"][1],))]
    return out


def _same_fetches(machine, expected):
    got = [(f[1], f[2]) for f in machine.fetches]
    if [g[0] for g in got] != [e[0] for e in expected]:
        return False
    return all(_compare(g[1], e[1], COORD_TOLERANCE)[0] for g, e in zip(got, expected))


PixelTrial = namedtuple("PixelTrial", "ok error fate got want machine")


def evaluate_pixel(case, mutation=None, shift=None, zero_product=True):
    """One trial: PixelTrial(agree, the colour's largest relative error, the machine's fate, the machine's oC0, the
    formula's result, the machine)."""
    consts, lit = case.consts, lf.water_literals()
    if shift is not None:
        consts, lit = _shifted(consts, lit, shift)
    out, m = run_pixel(case, consts=consts, zero_product=zero_product)
    fate = machine_fate(m)
    got = [float(x) for x in out["oC0"]]
    want = formula_pixel(case, mutation, lit, consts)
    ok, error = fate == want["fate"], 0.0
    if ok and fate == "killed":                 # a killed pixel has no output; the kill precedes every other fetch
        ok = m.fetches[0][1:] == (1, tuple(case.regs[3][:2]))
    elif ok:
        ok = _same_fetches(m, expected_fetches(case, want)) and abs(got[3] - want["alpha"]) <= ALPHA_TOLERANCE
        if fate == "skipped":
            ok = ok and got[:3] == [0.0, 0.0, 0.0] == list(want["colour"])
        else:
            same, error = _compare(got[:3], want["colour"], PIXEL_TOLERANCE)
            ok = ok and same
    return PixelTrial(ok, error, fate, got, want, m)


def check_pixel(trials=2000, seed=1, mutation=None, dumps=DUMPS, shift=None, zero_product=True):
    """`trials` made pixels of A17D against water_pixel: PixelResult (module text)."""
    _check_mutation(mutation, PIXEL_MUTATIONS, lf.WATER_SHADER["ps_lit_at"])
    text = _text(dumps, PS)
    counts = {"killed": 0, "skipped": 0, "shaded": 0}
    failed = sun = shadow = near = far = fogged = glinting = lit = 0
    worst, first, kinds, failed_kinds = 0.0, None, {}, {}
    for k in range(trials):
        case = made_pixel(seed, k, text=text)
        t = evaluate_pixel(case, mutation, shift, zero_product)
        counts[t.fate] += 1
        worst = max(worst, t.error)
        if case.label:
            kinds[case.label] = kinds.get(case.label, 0) + 1
        if t.fate == "shaded":
            sun += 136 in case.bools
            shadow += 137 in case.bools
            weight = t.machine.r[1][3]          # instr 58: sat(c140.x * d); nothing writes r1.w after it
            near += weight < 1.0
            far += weight == 1.0
            fogged += any(f[1] == 4 for f in t.machine.fetches)
            glinting += t.want["glint"] > 0.01
            lit += 136 in case.bools and t.want["fate"] == "shaded" and max(t.want["sun"]) > 0.0
        if not t.ok:
            failed += 1
            failed_kinds[case.label] = failed_kinds.get(case.label, 0) + 1
            first = first or "trial %d (seed %d%s, bools %s, %.6g units): relative error %.3e; the machine %s with %s, the formula %s with %s alpha %r" % (
                k, seed, ", " + case.label if case.label else "", sorted(case.bools), case.distance, t.error, t.fate, t.got,
                t.want["fate"], t.want["colour"], t.want["alpha"])
    return PixelResult(trials, failed, counts["killed"], counts["skipped"], counts["shaded"], sun, shadow, near, far, fogged,
                       worst, first, glinting, kinds, failed_kinds, lit)


def pixel_floors(r):
    """The floors a run of check_pixel must reach to count (review focus 1), as problems (texts)."""
    out = []
    tenth = r.trials // 10
    if min(r.killed, r.skipped, r.shaded) < tenth:
        out.append("a fate has fewer than a tenth of the trials")
    if min(r.sun, r.shaded - r.sun) < tenth:
        out.append("bool b136 is set, or clear, in fewer than a tenth of the trials among the shaded")
    if r.shadow < max(1, r.trials // 20) or r.shaded - r.shadow < tenth:
        out.append("bool b137 is set in fewer than a twentieth, or clear in fewer than a tenth")
    if min(r.near, r.far) < tenth:
        out.append("a side of the range 1 / c140.x has fewer than a tenth of the trials")
    if min(r.fogged, r.shaded - r.fogged) < tenth:
        out.append("a side of the fog's start has fewer than a tenth of the trials")
    if 3 * r.lit < r.sun:
        out.append("the glint's cosine is above 0 in fewer than a third of the trials with the sun term")
    if r.glinting < r.trials // 20:
        out.append("the sun term is above 1% of the colour in fewer than a twentieth of the trials")
    return out


def single_gap(trials=2000, seed=1, dumps=DUMPS):
    """How far water_pixel in single precision lies from the double reference on the made pixels that are shaded
    and not degenerate: {with the sun term (bool): (pixels, the median, the 99th percentile, the largest)} of the
    colour's largest difference, |single - double| / max(1, |double|). Reported, not a check: it says what bound a
    single-precision mirror pinned to the DOUBLE reference would need (the mirror is pinned to the single one)."""
    text = _text(dumps, PS)
    gaps = {False: [], True: []}
    for k in range(trials):
        case = made_pixel(seed, k, text=text)
        if case.label:
            continue
        q, P, coord_a, coord_b, c = _case_inputs(case)
        double = lf.water_pixel(q, P, coord_a, coord_b, case.m, c, case.tex(), bools=case.bools)
        if double["fate"] != "shaded":
            continue
        single = lf.water_pixel(q, P, coord_a, coord_b, case.m, c, case.tex(), bools=case.bools, single=True)
        if single["fate"] != "shaded":
            continue
        gaps[136 in case.bools].append(max(abs(x - y) / max(1.0, abs(x)) for x, y in zip(double["colour"], single["colour"])))
    out = {}
    for key, g in gaps.items():
        g.sort()
        n = len(g)
        out[key] = (n, g[n // 2], g[min(n - 1, int(n * 0.99))], g[-1]) if n else (0, 0.0, 0.0, 0.0)
    return out


# --- what the shaders read ----------------------------------------------------------------------------------------

def used_registers(which="vs", seed=1, trials=8, dumps=DUMPS):
    """The constant registers the interpreter read over a few made inputs: "vs" (BEAD), "ps" (A17D) or
    "ps_opaque" (F6D9); the pixel shaders on shaded pixels with both bools set and the fog begun."""
    used = set()
    if which == "vs":
        text = _text(dumps, VS)
        for t in range(trials):
            used |= run_vertex(made_vertex(seed, t, text=text))[1].used_consts
        return used
    kind = {"ps": "surface", "ps_opaque": "opaque"}[which]
    text = _text(dumps, PIXEL_HASH[kind])
    for t in range(trials):
        used |= run_pixel(shaded_case(seed, t, text=text, kind=kind))[1].used_consts
    return used


def _changed(x):
    return x * 1.5 + 0.37


def _pixel_account(m, out):
    return (m.killed, tuple(out.get("oC0", ())), tuple((f[1], f[2]) for f in m.fetches))


def read_components(which="vs", seed=1, cases=None, dumps=DUMPS, bools=None):
    """{register: the components a change of which moves an output}, by the interpreter alone: each component of
    each register the shader uses is changed in turn (x * 1.5 + 0.37) on made inputs, and it counts as read when
    any output differs in any of them. "vs": the outputs a pixel shader reads (oPos, o0.xyz, o1, o3.xy) over
    `cases` made draws (default 8). "ps" / "ps_opaque": the kill, oC0 and every fetch's slot and coordinates over
    `cases` shaded pixels (default 64) with both bools set and the fog begun (`bools`: another set) and over 16
    pixels of the alpha check (where a change of c141 moves the fate: on F6D9's shaded pixels the ramp is saturated
    and only c141.x shows). The literal registers are left out."""
    if which == "vs":
        text = _text(dumps, VS)
        made = [made_vertex(seed, t, text=text) for t in range(cases or 8)]

        def account(case, consts):
            return tuple(_vertex_values(run_vertex(case, consts=consts)[0]))
    else:
        kind = {"ps": "surface", "ps_opaque": "opaque"}[which]
        text = _text(dumps, PIXEL_HASH[kind])
        made = [shaded_case(seed, t, text=text, kind=kind) for t in range(cases or 64)]
        made += [made_alpha(seed, t, text=text, kind=kind) for t in range(16)]      # the fates: c141 past a saturated ramp

        def account(case, consts):
            out, m = run_pixel(case, consts=consts, bools=bools)
            return _pixel_account(m, out)
    base = [account(case, case.consts) for case in made]
    registers = sorted(used_registers(which, seed, dumps=dumps) - set(range(248, 256)))
    read = {}
    for reg in registers:
        comps = []
        for comp in range(4):
            for case, before in zip(made, base):
                consts = dict(case.consts)
                consts[reg] = list(consts[reg])
                consts[reg][comp] = _changed(consts[reg][comp])
                if account(case, consts) != before:
                    comps.append(comp)
                    break
        if comps:
            read[reg] = tuple(comps)
    return read


def read_interpolators(kind="surface", seed=1, cases=16, dumps=DUMPS):
    """{interpolator register: the components a change of which moves the pixel}, for A17D ("surface") or F6D9
    ("opaque"), by the interpreter alone as read_components does it; registers r0 to r15."""
    text = _text(dumps, PIXEL_HASH[kind])
    made = [shaded_case(seed, t, text=text, kind=kind) for t in range(cases)]
    base = [_pixel_account(*reversed(run_pixel(case))) for case in made]
    read = {}
    for reg in range(16):
        comps = []
        for comp in range(4):
            for case, before in zip(made, base):
                regs = {r: list(v) for r, v in case.regs.items()}
                regs.setdefault(reg, [0.0] * 4)
                regs[reg][comp] = _changed(regs[reg][comp])
                out, m = run_pixel(case, regs=regs)
                if _pixel_account(m, out) != before:
                    comps.append(comp)
                    break
        if comps:
            read[reg] = tuple(comps)
    return read


# --- the commands -------------------------------------------------------------------------------------------------

def _named(read, prefix="c"):
    return ", ".join("%s%d.%s" % (prefix, reg, "".join("xyzw"[k] for k in comps)) for reg, comps in sorted(read.items())) or "none"


def cmd_formulas(args, say=print):
    bad = 0
    say("vertex shader %s against light_formulas.water_vertex: %d made draws, seed %d; bound %g * max(1, |value|) in "
        "every component of oPos, o0.xyz, o1 and o3.xy" % (VS, args.trials, args.seed, VERTEX_TOLERANCE))
    r = check_vertex(args.trials, args.seed, dumps=args.dumps)
    say("vertex %d / %d; worst relative error %.3e%s" % (r.trials - r.failed, r.trials, r.worst, ": OK" if not r.failed else ": FAILED"))
    if r.failed:
        say("  first failure: %s" % r.first_failure)
    bad += bool(r.failed)
    for name in VERTEX_MUTATIONS:
        m = check_vertex(min(args.trials, 400), args.seed, mutation=name, dumps=args.dumps)
        bad += m.failed == 0
        say("  mutation %-13s: %d of %d trials disagree%s" % (name, m.failed, m.trials, "" if m.failed else ": NOT CAUGHT"))
    for kind in ("surface", "opaque"):
        say("pixel shader %s (%s): the fate and the alpha against light_formulas.water_kept and water_alpha: %d made "
            "pixels, seed %d; the alpha within %g" % (PIXEL_HASH[kind], kind, args.trials, args.seed, ALPHA_TOLERANCE))
        r = check_alpha(args.trials, args.seed, kind=kind, dumps=args.dumps)
        problems = ["first failure: %s" % r.first_failure] if r.failed else []
        if min(r.killed, r.skipped, r.shaded) < r.trials // 10:
            problems.append("a fate has fewer than a tenth of the trials")
        say("alpha %d / %d; killed %d, skipped %d, shaded %d; worst difference %.3e%s" % (
            r.trials - r.failed, r.trials, r.killed, r.skipped, r.shaded, r.worst, ": OK" if not problems else ": FAILED"))
        for text in problems:
            say("  " + text)
        bad += bool(problems)
        for name in ALPHA_MUTATIONS:
            m = check_alpha(min(args.trials, 400), args.seed, mutation=name, kind=kind, dumps=args.dumps)
            bad += m.failed == 0
            say("  mutation %-13s: %d of %d trials disagree%s" % (name, m.failed, m.trials, "" if m.failed else ": NOT CAUGHT"))
    say("pixel shader %s against light_formulas.water_pixel: %d made pixels, seed %d; the colour within %g * max(1, "
        "|value|) with b136 clear or set (the spec: 1e-5 and, with it set, 1e-3 relative); the alpha within %g; every "
        "fetch's slot and coordinates within %g" % (PS, args.trials, args.seed, PIXEL_TOLERANCE, ALPHA_TOLERANCE, COORD_TOLERANCE))
    r = check_pixel(args.trials, args.seed, dumps=args.dumps)
    problems = pixel_floors(r) + (["first failure: %s" % r.first_failure] if r.failed else [])
    say("pixel %d / %d; killed %d, skipped %d, shaded %d; of the shaded: with the sun term %d (its cosine above 0 in %d, "
        "the term above 1%% of the colour in %d), with the shadow factor %d, near %d, far %d, fogged %d; degenerate %s; "
        "worst relative error %.3e%s" % (
            r.trials - r.failed, r.trials, r.killed, r.skipped, r.shaded, r.sun, r.lit, r.glinting, r.shadow, r.near, r.far, r.fogged,
            ", ".join("%s %d" % kv for kv in sorted(r.kinds.items())) or "none", r.worst, ": OK" if not problems else ": FAILED"))
    for text in problems:
        say("  " + text)
    bad += bool(problems)
    ieee = check_pixel(args.trials, args.seed, dumps=args.dumps, zero_product=False)
    elsewhere = ieee.failed_kinds.get("", 0)
    bad += elsewhere > 0 or ieee.failed == 0
    say("without the console's product rule in the interpreter: %d of %d disagree, %d of them outside the %d trials of a "
        "degenerate kind (%s; a saturate takes some of an IEEE machine's no-numbers back to 0)%s" % (
            ieee.failed, ieee.trials, elsewhere, sum(ieee.kinds.values()),
            ", ".join("%s %d of %d" % (kind, ieee.failed_kinds.get(kind, 0), n) for kind, n in sorted(ieee.kinds.items())),
            "" if not elsewhere and ieee.failed else ": FAILED"))
    for sun, (n, median, p99, largest) in sorted(single_gap(args.trials, args.seed, args.dumps).items()):
        say("single precision against the double reference (reported, not a check), %d shaded pixels %s the sun term: "
            "median %.2e, 99th percentile %.2e, largest %.2e of max(1, |colour|)" % (n, "with" if sun else "without", median, p99, largest))
    for name in PIXEL_MUTATIONS:
        m = check_pixel(min(args.trials, 400), args.seed, mutation=name, dumps=args.dumps)
        bad += m.failed == 0
        say("  mutation %-13s: %d of %d trials disagree%s" % (name, m.failed, m.trials, "" if m.failed else ": NOT CAUGHT"))
    for name in HELD_LITERALS:
        alone = check_pixel(min(args.trials, 400), args.seed, mutation=("literal", name), dumps=args.dumps)
        both = check_pixel(min(args.trials, 400), args.seed, shift=(name, 1.01), dumps=args.dumps)
        ok = alone.failed > 0 and both.failed == 0
        bad += not ok
        reg, comp = lf.WATER_SHADER["ps_lit_at"][name]
        say("  literal %-9s (c%d.%s) by 1%%: in the formula alone %d of %d disagree; in the formula and at its place %d%s" % (
            name, reg, "xyzw"[comp], alone.failed, alone.trials, both.failed, "" if ok else ": NOT HELD"))
    say("all agree" if not bad else "%d checks FAILED" % bad)
    return 1 if bad else 0


def cmd_dead(args, say=print):
    say("pixel shader %s (the opaque twin), criterion 2 part (a): %d made pixels whose c141 satisfies "
        "light_formulas.water_opaque_dead (min(c141.x, c141.x - c141.y) >= 1e-6, magnitudes at most 4), seed %d, each at "
        "the masks 0, 1 and its own" % (PS_OPAQUE, args.trials, args.seed))
    r = check_dead(args.trials, args.seed, dumps=args.dumps)
    ok = (r.killed == r.trials and r.single_killed == r.trials and r.levels == 256 and r.controls > 0
          and r.control_kept == r.controls and r.at_margin >= r.trials // 10)
    say("killed by the machine at all three masks: %d / %d (at the margin itself: %d)" % (r.killed, r.trials, r.at_margin))
    say("alpha below 1 with instr 11 to 13 in single precision, the multiply-add rounded twice and fused: %d / %d" % (
        r.single_killed, r.trials))
    say("the 256 levels of a k_8 mask with the captured c141 %s: %d / 256 killed" % (CAPTURED_C141, r.levels))
    say("control (c141.x - c141.y <= 0, a mask of 0): not killed in %d / %d" % (r.control_kept, r.controls))
    say("dead on the microcode" if ok else "FAILED")
    return 0 if ok else 1


def cmd_used(args, say=print):
    bad = 0
    table = {"vs": set(lf.WATER_SHADER["reg"]) | {0, 1, 2, 3}, "ps": set(lf.WATER_SHADER["ps_reg"]), "ps_opaque": set(lf.WATER_SHADER["ps_reg"])}
    for which, hash_ in (("vs", VS), ("ps", PS), ("ps_opaque", PS_OPAQUE)):
        used = used_registers(which, dumps=args.dumps)
        literals = sorted(used & set(range(248, 256)))
        say("%s (%s) reads c%s; literals c%s" % (hash_, which, ", c".join(str(r) for r in sorted(used - set(literals))),
                                                 ", c".join(str(r) for r in literals)))
        extra = sorted(used - set(literals) - table[which])
        missing = sorted(table[which] - used)
        if which == "vs":
            say("  beyond the table (into o0.w, o2 and o3.z alone): c%s" % ", c".join(str(r) for r in extra))
            ok = extra == [9, 113, 119, 120, 121, 122] and not missing
        else:
            ok = not extra and not missing
        bad += not ok
        say("  the table (light_formulas.WATER_SHADER): %s" % ("as read" if ok else "DIFFERS: beyond it %s, not read %s" % (extra, missing)))
    read = read_components("vs", dumps=args.dumps)
    ok = read == lf.WATER_SHADER["vs_read"]
    bad += not ok
    say("vertex components that reach oPos, o0.xyz, o1 or o3.xy: %s%s" % (_named(read), "" if ok else ": NOT THE TABLE'S"))
    for kind in ("surface", "opaque"):
        read = read_interpolators(kind, dumps=args.dumps)
        ok = read == lf.WATER_SHADER["ps_in"]
        bad += not ok
        say("interpolator components %s (%s) reads: %s%s" % (PIXEL_HASH[kind], kind, _named(read, "r"), "" if ok else ": NOT THE TABLE'S"))
    for which, hash_ in (("ps", PS), ("ps_opaque", PS_OPAQUE)):
        read = read_components(which, dumps=args.dumps)
        ok = read == lf.WATER_SHADER["ps_read"]
        bad += not ok
        say("pixel components %s reads (64 shaded pixels, both bools set): %s%s" % (hash_, _named(read), "" if ok else ": NOT THE TABLE'S"))
    for bool_, registers in sorted(lf.WATER_SHADER["ps_read_under"].items()):
        read = read_components("ps", dumps=args.dumps, bools={136, 137} - {bool_})
        gone = sorted(set(lf.WATER_SHADER["ps_read"]) - set(read))
        ok = gone == sorted(registers)
        bad += not ok
        say("with bool b%d clear the registers no longer read: c%s%s" % (bool_, ", c".join(str(r) for r in gone), "" if ok else ": NOT THE TABLE'S"))
    say("components no formula reads (water_check.UNREAD): %s" % _named({r: k for r, k in UNREAD.items() if k}))
    say("as the tables say" if not bad else "%d checks FAILED" % bad)
    return 1 if bad else 0


def cmd_trace(args, say=print):
    hash_ = args.hash.upper()
    if hash_ == VS:
        case = made_vertex(args.seed, args.trial, args.dumps)
        regs = run_vertex(case, text=truncated(case.text, args.instr))[1].r
        say("%s, made trial %d of seed %d (vertex %d: %s), after instruction %d:" % (
            hash_, args.trial, args.seed, case.index, case_vertex(case), args.instr))
        shown = (0, 1, 2, 3, 9, 113) + tuple(range(115, 123))
    elif hash_ in (PS, PS_OPAQUE):
        kind = "surface" if hash_ == PS else "opaque"
        case = shaded_case(args.seed, args.trial, args.dumps, kind=kind)
        regs = run_pixel(case, text=truncated(case.text, args.instr))[1].r
        say("%s, made shaded pixel %d of seed %d (mask %r, bools %s), after instruction %d:" % (
            hash_, args.trial, args.seed, case.m, sorted(case.bools), args.instr))
        shown = lf.WATER_SHADER["ps_reg"] + (254, 255)
    else:
        say("%s is none of %s, %s, %s" % (hash_, VS, PS, PS_OPAQUE))
        return 1
    for reg in sorted(regs):
        if any(regs[reg]):
            say("  r%-2d %s" % (reg, "  ".join("% .9g" % x for x in regs[reg])))
    for reg in shown:
        say("  c%-3d %s" % (reg, "  ".join("% .9g" % x for x in case.consts[reg])))
    return 0


def main(argv=None, say=print):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dumps", type=Path, default=DUMPS)
    sub = ap.add_subparsers(dest="command", required=True)
    for name, run in (("formulas", cmd_formulas), ("dead", cmd_dead)):
        p = sub.add_parser(name)
        p.add_argument("--trials", type=int, default=2000)
        p.add_argument("--seed", type=int, default=1)
        p.set_defaults(run=run)
    p = sub.add_parser("used")
    p.set_defaults(run=cmd_used)
    p = sub.add_parser("trace")
    p.add_argument("hash")
    p.add_argument("--instr", type=int, required=True)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--trial", type=int, default=0)
    p.set_defaults(run=cmd_trace)
    args = ap.parse_args(argv)
    if not args.dumps.is_dir():
        say("no shader dumps at %s (made by fable_2.exe --dump_shaders=<dir>)" % args.dumps)
        return 1
    return args.run(args, say)


if __name__ == "__main__":
    sys.exit(main())
