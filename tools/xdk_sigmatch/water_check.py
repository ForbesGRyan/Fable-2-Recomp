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
On discovery captures (plan Task 4; a name's last digits or a path; "The captured rows" below has what each is):
  rows <capture>... [--positions 200]       criterion 1's second half: every A17D and F6D9 row, its vertices and 200
                                            pixels inside its mesh, microcode against formula on the row's own
                                            registers, literals, bools and dumps; no read component not finite
  dead <capture>...                         criterion 2 parts (b) and (c): the dead condition on every F6D9 row, no
                                            texel of a dumped mask at alpha 1; under the fetch rule (ruling 33)
  views <capture>...                        criterion 3: each reflection view against the mirrored main camera
  bools | states | bounds <capture>...      both bool members and the bits of b136 and b137; the state words by
                                            shader; the smallest and largest of every read component, against the
                                            table's sanity bounds
Each exits 1 on any failure. A capture without a water row is said to have none and is not a failure by itself.

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
shapes the inputs alone: see _mirrored). Four trials of 40 are degenerate (PIXEL_KINDS): "flat_zero" a flat normal with c128.x = 0 (the
normal is 0 * infinity), "at_eye" a pixel at the eye (the view vector is 0 * infinity), "power_zero" c138.x = 0
with the sun below a level surface (0 * -infinity), and "second_flat" c136.x = 0 on a flat pair of samples under
b136 (the sun's second normal is 0 * infinity: ledger ruling 34, after the review of Tasks 1 to 5, I2): the
console's product gives 0 in each, an IEEE machine no number
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
import impostor_check as ic  # noqa: E402
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
PIXEL_KINDS = {7: "flat_zero", 19: "at_eye", 31: "power_zero", 35: "second_flat"}      # by trial % 40
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
    if trial % 20 == 3 or label in ("flat_zero", "power_zero", "second_flat"):
        case.normal = lambda u, v: flat
    if label == "flat_zero":
        case.consts[128][0] = 0.0
    elif label == "at_eye":
        case.regs[0][:3] = case.consts[19][:3]
        case.distance = 0.0
    elif label == "second_flat":                # the sun's second normal is (0, 0, c136.x * N.z) = 0: 0 * infinity
        case.bools = case.bools | {136}
        case.consts[136][0] = 0.0
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


def formula_pixel(case, mutation=None, lit=None, consts=None, single=False):
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
    out = lf.water_pixel(q, P, coord_a, coord_b, case.m, c, tex, bools=case.bools, kind=case.kind, lit=lit, single=single)
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


def judge_pixel(case, fate, got, m, want):
    """(agree, the colour's largest relative error) of a machine's result (its fate, its oC0 and the machine) against
    a formula's result `want`: the fate, the order and coordinates of the fetches, the alpha, the colour."""
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
    return ok, error


def evaluate_pixel(case, mutation=None, shift=None, zero_product=True, lit=None):
    """One trial: PixelTrial(agree, the colour's largest relative error, the machine's fate, the machine's oC0, the
    formula's result, the machine). `lit`: the literals by name (default: the table's)."""
    consts, lit = case.consts, lit or lf.water_literals()
    if shift is not None:
        consts, lit = _shifted(consts, lit, shift)
    out, m = run_pixel(case, consts=consts, zero_product=zero_product)
    fate = machine_fate(m)
    got = [float(x) for x in out["oC0"]]
    want = formula_pixel(case, mutation, lit, consts)
    ok, error = judge_pixel(case, fate, got, m, want)
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


# --- the captured rows (plan Task 4; spec criteria 1 (second half), 2 parts (b) and (c), 3) -------------------------
#
# What these checks are, and what they are not (the review of sub-project 12's phase 12a: a check that feeds the same
# captured inputs to both sides is circular). `rows` runs the microcode and the formula on the SAME captured
# registers, vertices and texture dumps: it shows that the transcription holds on the game's own numbers (ranges no
# made input was drawn from, the garbage in the unread components, the literal table and the bools as the rows
# carry them) and that every component a formula reads is a number there. It cannot show that a register means
# what its name says, that a texture's bytes are read in the right order, or that the position register is r4 with
# integral positions: only a replay against the game's own picture holds those (water_replay.py, plan Task 6).
# `views` is not circular: one side is the game's own view rows, the other is derived from the main scene's rows.

ROW_POSITIONS = 200
BOOL_MEMBER = "pbool_load"          # the word of the last writer (spec Design 4); `bools` prints both members
VIEW_PITCH = 280
VIEW_TOLERANCE = 1e-5               # criterion 3: |derived - view| <= 1e-5 * max(1, |view|), every entry
VIEW_MUTATIONS = ("factor", "plane", "height")
HEIGHT_FLAT = 1e-3                  # spec Design 7: a mesh is flat when its heights lie within this of the first's
HEIGHT_STEP = 0.01                  # the "height" mutation
STATE_REGISTERS = (("2200", "depth"), ("2201", "blend"), ("2202", "colour control"), ("210E", "alpha reference"),
                   ("2205", "cull and face"), ("2104", "colour mask"))
SUN_MUTATIONS = ("second_normal", "power", "sun_sign", "sun_scale")

_captures = {}


def _np():
    import numpy
    return numpy


def row_kind(row):
    """"surface" (A17D), "opaque" (F6D9) or None for a row that is neither."""
    if cr.vs_hash(row) != VS:
        return None
    return {PS: "surface", PS_OPAQUE: "opaque"}.get(cr.ps_hash(row))


def read_capture(path):
    """{"water": the water rows in file order (each with "line"), "textures": {the six dwords: the first texture row
    of that constant}, "frames": {frame: its frame row}, "order": [(kind, frame, pitch, c0..c3 or None)] of every
    draw, indx, tess, verts and resolve row in file order (what `views` reads)}; kept once a path."""
    import json
    path = cr.find_capture(str(path))
    key = str(path)
    if key in _captures:
        return _captures[key]
    out = {"path": path, "water": [], "textures": {}, "frames": {}, "order": []}
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            head = line[:64]
            if not head.startswith('{"kind":"'):
                continue
            kind = head[9:head.find('"', 9)]
            if kind not in ("draw", "indx", "tess", "verts", "resolve", "texture", "frame"):
                continue
            try:
                r = json.loads(line)
            except ValueError:
                continue                            # a last line cut short by a killed capture
            r["line"] = n
            if kind == "texture":
                if len(r.get("fc") or []) == 6:
                    out["textures"].setdefault(tuple(int(str(v), 16) for v in r["fc"]), r)
            elif kind == "frame":
                out["frames"][r.get("frame")] = r
            else:
                matrix = None
                bank, consts = r.get("bank"), r.get("consts") or {}
                if bank and len(bank) >= 16:
                    matrix = tuple(bank[0:16])
                elif all("c%d" % i in consts for i in range(4)):
                    matrix = tuple(consts["c0"] + consts["c1"] + consts["c2"] + consts["c3"])
                out["order"].append((kind, r.get("frame"), int(str(r.get("surface_info", "0x0")), 16) & 0x3FFF, matrix))
                if kind == "draw" and row_kind(r) is not None:
                    for name in cr.DROP:
                        r.pop(name, None)
                    out["water"].append(r)
    _captures[key] = out
    return out


def water_rows(path):
    return read_capture(path)["water"]


def _fc(row, slot):
    fc = (row.get("tf") or {}).get(str(slot))
    if not fc:
        raise cr.Unreadable("the row names no texture at slot %d" % slot)
    return tuple(int(str(v), 16) for v in fc)


def decode_8888(data, f):
    """A k_8_8_8_8 level as uint8 [y, x, 4] in the TEXTURE's channels: channel k is byte k of a texel after the
    fetch constant's endian swap (the SDK loads the format as R8G8B8A8 from the swapped dword: fog_tables._texels;
    the fetch constant's swizzle then says which channel each of the shader's components is). `f`: the constant's
    fields (water_textures.describe). ValueError for a texel outside `data`."""
    np = _np()
    import impostor_pictures as ip
    w, h, pitch = f["width"], f["height"], max(f["pitch"], f["width"])
    if f["tiled"]:
        off = ip.tiled_offsets(w, h, pitch, 2)
    else:
        y, x = np.meshgrid(np.arange(h, dtype=np.int64), np.arange(w, dtype=np.int64), indexing="ij")
        off = y * (((pitch + 31) & ~31) * 4) + x * 4
    if int(off.max()) + 4 > len(data):
        raise ValueError("a texel lies outside the %d bytes given" % len(data))
    raw = np.frombuffer(bytes(data), dtype=np.uint8)
    texels = raw[(off.reshape(-1, 1) + np.arange(4, dtype=np.int64)).reshape(-1)].reshape(h, w, 4)
    order = {0: (0, 1, 2, 3), 1: (1, 0, 3, 2), 2: (3, 2, 1, 0), 3: (2, 3, 0, 1)}[f["endian"]]
    return np.ascontiguousarray(texels[..., order])


class RowTextures:
    """The textures of a capture as a water row's fetches read them, each decoded once: the shore mask (slot 1), the
    normal map's base level (slot 2), the reflection picture (slot 13), the near picture (slot 14). A texture is
    found by its whole fetch constant among the capture's "texture" rows; the bytes are the dump's, kept at the
    first row that named the constant (for a picture the game renders every frame that is frame 1's)."""

    def __init__(self, capture):
        self.dir, self.rows, self._kept = Path(capture["path"]).parent, capture["textures"], {}

    def fields(self, fc):
        import water_textures as wt
        f = wt.describe(list(fc))
        if any(f["sign"]):
            raise cr.Unreadable("fetch constant %s has sign fields %s" % (" ".join("%08X" % v for v in fc), f["sign"]))
        if f["dimension"] != 1:
            raise cr.Unreadable("not a 2D texture")
        return f

    def image(self, fc):
        """(the texture's channels as floats in 0..1 [h, w, c], the constant's fields)."""
        np = _np()
        import water_textures as wt
        if fc in self._kept:
            return self._kept[fc]
        row = self.rows.get(fc)
        if row is None:
            raise cr.Unreadable("no texture row of fetch constant %s" % " ".join("%08X" % v for v in fc))
        f = self.fields(fc)
        data = Path(self.dir, *str(row["file"]).split("/")).read_bytes()
        try:
            if f["format"] == wt.XENOS_K8:
                image = wt.decode_k8(data, f["width"], f["height"], f["pitch"], f["tiled"])[..., None]
            elif f["format"] == wt.XENOS_DXN:
                image = wt.decode_dxn_rg8(data, f["width"], f["height"], f["pitch"] // 4, f["tiled"], f["endian"])
            elif f["format"] == 6:
                image = decode_8888(data, f)
            else:
                raise cr.Unreadable("format %d is none of the water's" % f["format"])
        except ValueError as e:
            raise cr.Unreadable("%s: %s" % (row["file"], e)) from None
        self._kept[fc] = (image.astype(np.float64) / 255.0, f)
        return self._kept[fc]

    def sampler(self, fc, components):
        """A callable (u, v) -> the shader's first `components` components of a bilinear fetch of the base level,
        addressed as the constant says (repeat or clamp to the edge), through the constant's swizzle (a one-channel
        format gives its channel in every component, as the SDK's host format does)."""
        image, f = self.image(fc)
        swz = f["swizzle"]
        channels = image.shape[2]
        return lambda u, v: _shader(bilinear(image, u, v, f["clamp"]), swz, channels)[:components]


def _shader(texel, swz, channels):
    out = []
    for s in swz:
        if s == 4:
            out.append(0.0)
        elif s == 5:
            out.append(1.0)
        elif channels == 1:
            out.append(texel[0])
        elif channels == 2:
            out.append(texel[min(s, 1)])
        else:
            out.append(texel[s])
    return out


def _wrap(i, n, mode):
    if mode == 0:
        return i % n
    if mode == 2:
        return min(max(i, 0), n - 1)
    raise cr.Unreadable("address mode %d (neither repeat nor clamp to the edge)" % mode)


def bilinear(image, u, v, clamp=(0, 0)):
    """An image [h, w, c] at (u, v): linear between the four texel centres about it ((i + 0.5) / n), each axis
    addressed by `clamp` (0 repeat, 2 clamp to the edge). A list of c floats."""
    h, w = image.shape[0], image.shape[1]
    x, y = u * w - 0.5, v * h - 0.5
    x0, y0 = math.floor(x), math.floor(y)
    fx, fy = x - x0, y - y0
    xa, xb = _wrap(x0, w, clamp[0]), _wrap(x0 + 1, w, clamp[0])
    ya, yb = _wrap(y0, h, clamp[1]), _wrap(y0 + 1, h, clamp[1])
    a, b, c, d = image[ya, xa], image[ya, xb], image[yb, xa], image[yb, xb]
    return [float((a[k] * (1.0 - fx) + b[k] * fx) * (1.0 - fy) + (c[k] * (1.0 - fx) + d[k] * fx) * fy) for k in range(image.shape[2])]


def row_mesh(capture_dir, row):
    """(positions [(x, y, z)], pairs [(u, v)], triangles [(i, j, k)]) of a water row: every vertex of its stream
    dump (20 bytes: three big-endian floats, then the pair at dword 3, as the row's two fetches say) and the
    draw's triangle list from its index dump. cr.Unreadable for anything else."""
    import foliage_replay as fp
    fetches = row.get("fetches") or []
    if len(fetches) != 2 or fetches[0].get("stride") != STRIDE_BYTES // 4 or fetches[1].get("off") != PAIR_OFFSET:
        raise cr.Unreadable("not the water's two fetches of a 20-byte vertex")
    stream = next((s for s in row.get("streams") or [] if s.get("slot") == 95 and s.get("file")), None)
    if stream is None or int(stream.get("endian", 2)) != 2:
        raise cr.Unreadable("no dump of the vertex stream, or not 8-in-32")
    data = cr.stream_bytes(Path(capture_dir) / stream["file"])
    count = len(data) // STRIDE_BYTES
    positions = [struct.unpack_from(">3f", data, i * STRIDE_BYTES) for i in range(count)]
    pairs = [struct.unpack_from(">2f", data, i * STRIDE_BYTES + 4 * PAIR_OFFSET) for i in range(count)]
    if int(str((row.get("args") or ["0"])[0]), 16) != 4:
        raise cr.Unreadable("not a triangle list")
    try:
        indices, state, note = fp.draw_indices(row, capture_dir)
    except fp.Unreadable as e:
        raise cr.Unreadable(str(e)) from None
    base = int(row.get("base_vertex", 0) or 0)
    if state != "whole" or len(indices) % 3 or any(not 0 <= i + base < count for i in indices):
        raise cr.Unreadable("the index dump is not the draw's whole triangle list (%s)" % (note or state))
    triangles = [tuple(i + base for i in indices[k:k + 3]) for k in range(0, len(indices), 3)]
    return positions, pairs, triangles


def row_height(capture_dir, row):
    """The height of a water row's mesh, or None when it is not flat (its vertices' third coordinates not within
    HEIGHT_FLAT of the first's): spec Design 7's WaterMeshHeight."""
    positions, _, _ = row_mesh(capture_dir, row)
    z = [p[2] for p in positions]
    return z[0] if z and all(abs(v - z[0]) <= HEIGHT_FLAT for v in z) else None


def row_literal_names(row):
    """{name: value} of the literals the pixel formulas read, from the row's OWN literal table ("ps_lit")."""
    table = ic.row_literals(row, "ps")
    out = {}
    for name, (reg, comp) in lf.WATER_SHADER["ps_lit_at"].items():
        if reg not in table:
            raise cr.Unreadable("the row has no literal register c%d" % reg)
        out[name] = table[reg][comp]
    return out


def row_not_finite(row):
    """The names ("c128.x", "vertex c117.y") of the components a formula reads that are no number in a row."""
    out = []
    try:
        vbank = ic.row_bank(row)
    except ValueError:
        return ["no whole vertex bank"]
    for reg, comps in sorted(lf.WATER_SHADER["vs_read"].items()):
        out += ["vertex c%d.%s" % (reg, "xyzw"[k]) for k in comps if not math.isfinite(vbank[reg][k])]
    for reg, comps in sorted(lf.WATER_SHADER["ps_read"].items()):
        v = ic.pixel_register(row, reg)
        if v is None:
            out.append("c%d (not in the row)" % reg)
        else:
            out += ["c%d.%s" % (reg, "xyzw"[k]) for k in comps if not math.isfinite(v[k])]
    return out


def row_tables(capture, frame):
    """(tf4, tf5) of a frame as water_pixel takes them, from the frame row's fog block; None without one."""
    fog = (capture["frames"].get(frame) or {}).get("fog") or {}
    t, s = fog.get("transmit") or [], fog.get("scatter") or []
    if len(t) < 3 or len(s) < 3 or len(t) % 3 or len(s) % 3:
        return None
    return lf.table_1d([s[i:i + 3] for i in range(0, len(s), 3)]), lf.table_1d([t[i:i + 3] for i in range(0, len(t), 3)])


def _no_shadow(u, v):
    raise cr.Unreadable("bool b137 is set and the capture holds no whole screen shadow mask (slot 8)")


class RowSetup:
    """What one water row gives every pixel of it: the kind, the pixel registers with the row's literals, the
    literal names, the bools of the row's BOOL_MEMBER, the vertex bank, the mesh and the four samplers."""

    def __init__(self, capture, textures, row, text):
        self.row, self.kind, self.text = row, row_kind(row), text
        self.consts = ic.row_pixel_bank(row)
        self.vbank = ic.row_bank(row)
        self.lit = row_literal_names(row)
        self.bools = set(lf.water_bools(int(str(row.get(BOOL_MEMBER, "0x0")), 16)))
        self.positions, self.pairs, self.triangles = row_mesh(textures.dir, row)
        self.mask = textures.sampler(_fc(row, 1), 1)
        self.normal = textures.sampler(_fc(row, 2), 2)
        self.mirror = textures.sampler(_fc(row, 13), 3)
        self.near = textures.sampler(_fc(row, 14), 3)
        tables = row_tables(capture, row.get("frame"))
        if tables is None:
            raise cr.Unreadable("the frame row has no fog tables")
        self.tf4, self.tf5 = tables
        self.size = tuple(row.get("viewport") or (1120, 720))

    def point(self, triangle, weights):
        """(P, pair, clip) of a barycentric point of a triangle, P and the pair rounded to single precision as an
        interpolator holds them."""
        i, j, k = triangle
        a, b, c = weights
        P = [f32(a * self.positions[i][n] + b * self.positions[j][n] + c * self.positions[k][n]) for n in range(3)]
        pair = [f32(a * self.pairs[i][n] + b * self.pairs[j][n] + c * self.pairs[k][n]) for n in range(2)]
        return P, pair, lf.water_vertex(P, pair, self.vbank)

    def case(self, P, pair, q, coord=None):
        """The pixel at integral position q whose interpolators are the place P, the two coordinates of the vertex
        formula there and the mask's coordinate `pair`."""
        coord = coord if coord is not None else [f32(v) for v in lf.water_vertex(P, pair, self.vbank)["coord"]]
        regs = {0: list(P) + [JUNK], 1: list(coord), 2: [JUNK] * 4, 3: list(pair) + [JUNK, JUNK], 4: [float(q[0]), float(q[1]), JUNK, JUNK]}
        m = self.mask(pair[0], pair[1])[0]
        distance = math.sqrt(sum((self.consts[19][n] - P[n]) ** 2 for n in range(3)))
        return PixelCase(self.text, self.kind, self.consts, regs, self.bools, m, lambda u, v: self.mask(u, v)[0],
                         lambda u, v: tuple(self.normal(u, v)), self.mirror, self.near, _no_shadow, self.tf4, self.tf5, distance)


def _screen(clip, size):
    """(inside the picture, the pixel's integral position) of a clip position."""
    x, y, _, w = clip
    if not (w > 0.0 and math.isfinite(x) and math.isfinite(y)):
        return False, (0, 0)
    sx, sy = (x / w * 0.5 + 0.5) * size[0], (0.5 - y / w * 0.5) * size[1]
    inside = 0.0 <= sx < size[0] and 0.0 <= sy < size[1]
    return inside, (math.floor(min(max(sx, -4096.0), 4096.0)), math.floor(min(max(sy, -4096.0), 4096.0)))


def row_points(setup, count, rng):
    """`count` (P, pair, q, inside) of a row: random points of its triangles (a triangle by its share of the mesh's
    area), those whose screen place lies inside the picture first; when the mesh shows fewer than `count` in
    twenty times as many tries the rest are points outside the picture (their integral position is still the
    floor of their screen place, held to 4,096 pixels)."""
    areas = []
    for i, j, k in setup.triangles:
        a, b, c = setup.positions[i], setup.positions[j], setup.positions[k]
        u, v = [b[n] - a[n] for n in range(3)], [c[n] - a[n] for n in range(3)]
        cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        areas.append(math.sqrt(sum(x * x for x in cross)))
    if not setup.triangles or sum(areas) <= 0.0:
        raise cr.Unreadable("the mesh has no area")
    inside, outside = [], []
    for _ in range(20 * count):
        if len(inside) >= count:
            break
        triangle = rng.choices(setup.triangles, weights=areas)[0]
        a, b = rng.random(), rng.random()
        if a + b > 1.0:
            a, b = 1.0 - a, 1.0 - b
        P, pair, vertex = setup.point(triangle, (a, b, 1.0 - a - b))
        ok, q = _screen(vertex["pos"], setup.size)
        (inside if ok else outside).append((P, pair, q, ok))
    return (inside + outside)[:count]


def _row_vertex(p, pair, c, mutation=None):
    """water_vertex of a row's vertex as thirteen numbers; `mutation`: one of VERTEX_MUTATIONS."""
    c = {reg: list(c[reg]) for reg in (0, 1, 2, 3) + tuple(lf.WATER_SHADER["reg"])}
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


def _alpha_mutant_agrees(case, fate, alpha, mutation, lit):
    """Whether an alpha mutant of the formula still gives the machine's fate and alpha (check_alpha's three)."""
    c141 = list(case.consts[141])
    if mutation == "ramp":
        c141[0], c141[1] = c141[1], c141[0]
    want_alpha = lf.water_alpha(1.0 - case.m if mutation == "mask" else case.m, c141, lit)
    other = {"surface": "opaque", "opaque": "surface"}[case.kind]
    want = lf.water_kept(want_alpha, c141, other if mutation == "comparison" else case.kind, lit)
    return want == fate and (fate == "killed" or abs(alpha - want_alpha) <= ALPHA_TOLERANCE)


# rows (surface_rows of A17D, opaque_rows of F6D9); vertices compared and those that disagree, the largest error;
# positions compared and those that disagree; rows with a read component that is no number (not_finite; they are not
# compared) and rows the capture does not hold whole (unread); {kind: {fate: positions}}; the colour's largest
# relative error; the largest distance of the single-precision reference from the double one on the shaded
# positions, of max(1, |colour|); the first failure's text; positions outside the picture; {mutation: vertices or
# positions that disagree}; of the shaded surface positions: with the sun term, with a sun term above 0, with one
# above 1% of the colour, near (the blend's weight below 1), fogged; the facts rows_blind reads; the pixel
# registers the machine read.
RowsResult = namedtuple("RowsResult", "rows surface_rows opaque_rows vertices vertex_failed vertex_worst positions failed "
                                      "not_finite unread fates worst single first_failure outside caught sun lit glinting near "
                                      "fogged facts used")


def check_rows(path, positions=ROW_POSITIONS, mutations=True, dumps=DUMPS):
    """Every A17D and F6D9 row of a capture (both frames): each vertex of its dump through the vertex microcode
    and through water_vertex; `positions` pixels inside its mesh through the pixel microcode and through
    water_pixel with the row's own vertex bank, pixel registers, literal table and bools and one sampler for both
    sides (module text, above); every mutation of Tasks 2 and 3 on the formula's side against the same machine
    results. Returns RowsResult."""
    capture = read_capture(path)
    textures = RowTextures(capture)
    texts = {"surface": _text(dumps, PS), "opaque": _text(dumps, PS_OPAQUE)}
    fates = {kind: {"killed": 0, "skipped": 0, "shaded": 0} for kind in texts}
    names = VERTEX_MUTATIONS + ALPHA_MUTATIONS + PIXEL_MUTATIONS if mutations else ()
    caught = {name: 0 for name in names}
    count = {"surface": 0, "opaque": 0}
    vertices = vertex_failed = compared = failed = not_finite = unread = outside = sun = lit = glinting = near = fogged = 0
    vertex_worst = worst = single = 0.0
    first, used = None, set()
    facts = {"distort_equal": True, "min_reach": math.inf, "c28x_one": True, "sun_largest": 0.0, "shaded": 0}
    for row in capture["water"]:
        kind = row_kind(row)
        where = "frame %s line %s (%s)" % (row.get("frame"), row.get("line"), kind)
        count[kind] += 1
        bad = row_not_finite(row)
        if bad:
            not_finite += 1
            first = first or "%s: not finite: %s" % (where, ", ".join(bad))
            continue
        try:
            setup = RowSetup(capture, textures, row, texts[kind])
            draw = cr.Draw(capture["path"], row, dumps)
        except (cr.Unreadable, ValueError) as e:
            unread += 1
            first = first or "%s: NOT READ: %s" % (where, e)
            continue
        if kind == "surface":
            facts["distort_equal"] = facts["distort_equal"] and setup.consts[129][:2] == setup.consts[130][:2]
            facts["c28x_one"] = facts["c28x_one"] and setup.consts[28][0] == 1.0
        for index, (p, pair) in enumerate(zip(setup.positions, setup.pairs)):
            out, _ = draw.run(index, consts=setup.vbank)
            got = _vertex_values(out)
            ok, error = _compare(got, _row_vertex(p, pair, setup.vbank), VERTEX_TOLERANCE)
            vertices += 1
            vertex_worst = max(vertex_worst, error)
            if not ok:
                vertex_failed += 1
                first = first or "%s vertex %d: relative error %.3e; microcode %s" % (where, index, error, got)
            for name in VERTEX_MUTATIONS if mutations else ():
                caught[name] += not _compare(got, _row_vertex(p, pair, setup.vbank, name), VERTEX_TOLERANCE)[0]
        rng = random.Random("water-rows/%s/%s" % (Path(capture["path"]).name, row.get("line")))
        for P, pair, q, inside in row_points(setup, positions, rng):
            case = setup.case(P, pair, q)
            try:
                t = evaluate_pixel(case, lit=setup.lit)
            except cr.Unreadable as e:
                unread += 1
                first = first or "%s: NOT READ: %s" % (where, e)
                break
            compared += 1
            outside += not inside
            fates[kind][t.fate] += 1
            worst = max(worst, t.error)
            used |= t.machine.used_consts
            if not t.ok:
                failed += 1
                first = first or "%s at pixel %s: relative error %.3e; the machine %s with %s, the formula %s with %s alpha %r" % (
                    where, q, t.error, t.fate, t.got, t.want["fate"], t.want["colour"], t.want["alpha"])
            for name in ALPHA_MUTATIONS if mutations else ():
                caught[name] += not _alpha_mutant_agrees(case, t.fate, t.got[3], name, setup.lit)
            if kind != "surface":
                continue
            if t.fate == "shaded" and t.want["fate"] == "shaded":
                want = t.want
                facts["shaded"] += 1
                facts["min_reach"] = min(facts["min_reach"], setup.consts[140][0] * want["distance"])
                scale = setup.consts[20][3]
                term = max(abs(v) for v in want["sun"]) * abs(scale) / max(1.0, max(abs(v) for v in want["colour"]))
                facts["sun_largest"] = max(facts["sun_largest"], term)
                sun += 136 in case.bools
                lit += 136 in case.bools and max(want["sun"]) > 0.0
                glinting += want["glint"] > 0.01
                near += t.machine.r[1][3] < 1.0
                fogged += any(f[1] == 4 for f in t.machine.fetches)
                one = formula_pixel(case, lit=setup.lit, single=True)
                if one["fate"] == "shaded":
                    single = max([single] + [abs(a - b) / max(1.0, abs(a)) for a, b in zip(want["colour"], one["colour"]) if a == a and b == b])
            for name in PIXEL_MUTATIONS if mutations else ():
                caught[name] += not judge_pixel(case, t.fate, t.got, t.machine, formula_pixel(case, name, setup.lit))[0]
    return RowsResult(count["surface"] + count["opaque"], count["surface"], count["opaque"], vertices, vertex_failed, vertex_worst,
                      compared, failed, not_finite, unread, fates, worst, single, first, outside, caught, sun, lit, glinting, near,
                      fogged, facts, frozenset(used))


def rows_blind(r):
    """{mutation: why these rows cannot show it} for the pixel mutations a run of check_rows did not catch and whose
    cause is a MEASURED fact of the rows (never an excuse: a mutation that is not caught and is not here fails the
    run)."""
    f, out = r.facts, {}
    if not f["shaded"]:
        return {name: "no surface position is shaded" for name in PIXEL_MUTATIONS if not r.caught.get(name)}
    if not r.caught.get("distort") and f["distort_equal"]:
        out["distort"] = "c129.xy and c130.xy are equal, bit for bit, in every surface row"
    if f["min_reach"] >= 1.0:
        text = "the blend's weight sat(c140.x * distance) is 1 at every shaded position (the smallest c140.x * distance is %.4g): " % f["min_reach"]
        if not r.caught.get("slot14"):
            out["slot14"] = text + "the near branch reaches no pixel"
        if not r.caught.get("range") and f["min_reach"] >= 1.0 / 0.99:
            out["range"] = text + "a 1% change of c140.x leaves it 1"
    if not r.caught.get("fog") and f["c28x_one"]:
        out["fog"] = "c28.x is exactly 1 in every surface row: leaving it out changes nothing"
    if f["sun_largest"] <= PIXEL_TOLERANCE:
        for name in SUN_MUTATIONS:
            if not r.caught.get(name):
                out[name] = ("no shaded position has a sun term above the bound (the largest is %.3g of max(1, |colour|)): "
                             "the glint is not exercised by these rows" % f["sun_largest"])
    return out


def rows_problems(r):
    """What makes a run of check_rows fail, as texts."""
    out = []
    if r.not_finite:
        out.append("%d rows hold a component a formula reads that is not finite; the first: %s" % (r.not_finite, r.first_failure))
    if r.unread:
        out.append("%d rows the capture does not hold whole; the first: %s" % (r.unread, r.first_failure))
    if r.rows and not r.positions and not r.not_finite and not r.unread:
        out.append("no position was compared")
    if r.failed or r.vertex_failed:
        out.append("%d vertices and %d positions disagree; the first: %s" % (r.vertex_failed, r.failed, r.first_failure))
    blind = rows_blind(r)
    for name, n in r.caught.items():
        if n:
            continue
        if name in ALPHA_MUTATIONS + VERTEX_MUTATIONS or name not in blind:
            if name in PIXEL_MUTATIONS and not r.surface_rows:
                continue
            out.append("mutation %s is NOT CAUGHT" % name)
    return out


# --- the dead draw on rows and masks (criterion 2, parts (b) and (c)) ----------------------------------------------

# the F6D9 rows; those whose c141 satisfies water_opaque_dead; the distinct masks the water rows name; the texels of
# those masks whose alpha is 1 with their row's c141 (in double or in single precision, the multiply-add rounded
# twice or fused); the largest alpha any texel gives; [(file, width, height, texels, ones, largest alpha, share of
# texels at or below the alpha threshold, share at 0.99 or more)]; the problems as texts.
DeadRows = namedtuple("DeadRows", "rows dead masks ones largest table problems")


def check_dead_rows(path):
    """Criterion 2 parts (b) and (c) on a capture: water_opaque_dead of every F6D9 row's c141, and, for every mask
    a water row names (slot 1), the alpha each texel gives with that row's c141: none may be 1."""
    np = _np()
    capture = read_capture(path)
    textures = RowTextures(capture)
    rows = dead = ones = 0
    largest, table, problems, seen = 0.0, [], [], {}
    for row in capture["water"]:
        c141 = ic.pixel_register(row, 141)
        where = "frame %s line %s" % (row.get("frame"), row.get("line"))
        if row_kind(row) == "opaque":
            rows += 1
            if c141 is not None and lf.water_opaque_dead(c141):
                dead += 1
            else:
                problems.append("%s: F6D9's c141 %s does not satisfy the dead condition" % (where, c141))
        if c141 is None:
            continue
        try:
            fc = _fc(row, 1)
            key = (fc, tuple(c141[:2]), c141[3])
            if key in seen:
                continue
            seen[key] = True
            image, f = textures.image(fc)
            if f["format"] != 2:
                raise cr.Unreadable("slot 1 is format %d, not k_8" % f["format"])
        except cr.Unreadable as e:
            problems.append("%s: the mask is not read: %s" % (where, e))
            continue
        levels = np.rint(image[..., 0] * 255.0).astype(np.int64)
        alpha = [lf.water_alpha(f32(level / 255.0), c141) for level in range(256)]
        is_one = [alpha[level] >= 1.0 or single_alpha(f32(level / 255.0), c141) >= 1.0 or single_alpha(f32(level / 255.0), c141, True) >= 1.0
                  for level in range(256)]
        histogram = np.bincount(levels.reshape(-1), minlength=256)
        mine = int(sum(histogram[level] for level in range(256) if is_one[level]))
        top = max(alpha[level] for level in range(256) if histogram[level])
        texels = int(histogram.sum())
        land = sum(int(histogram[level]) for level in range(256) if alpha[level] <= c141[3]) / texels
        open_ = sum(int(histogram[level]) for level in range(256) if alpha[level] >= 0.99) / texels
        ones += mine
        largest = max(largest, top)
        name = str(textures.rows[fc]["file"]).split("/")[-1]
        table.append((name, f["width"], f["height"], texels, mine, top, land, open_))
        if mine:
            problems.append("%s: %d texels of mask %s give alpha 1 with c141 %s" % (where, mine, name, c141))
    return DeadRows(rows, dead, len(table), ones, largest, table, problems)


# --- the mirrored camera (criterion 3) ----------------------------------------------------------------------------

# frame; the view's number in the frame (from 1); the slot-13 base whose picture it renders; the height of the
# meshes that sample it; the view's rows; the largest error of the derived matrix against the view's own,
# |derived - view| / max(1, |view|), and the same per matrix row; within VIEW_TOLERANCE; {mutation: caught}; a
# note (why a view was not compared).
ViewResult = namedtuple("ViewResult", "frame view base height rows worst by_row ok caught note")


def mirror_matrix(h):
    np = _np()
    m = np.eye(4)
    m[2, 2], m[2, 3] = -1.0, 2.0 * h
    return m


def water_view_rows(M, c31xy, h):
    """diag(c31.x, c31.y, 1, 1) * M * Mirror(h), in double: the reflection view's rows from the main scene's
    view-projection M [4, 4], the water draw's c31.xy and the water plane's height (spec Design 7, WaterViewRows)."""
    np = _np()
    return np.diag([float(c31xy[0]), float(c31xy[1]), 1.0, 1.0]) @ np.asarray(M, dtype=np.float64) @ mirror_matrix(float(h))


def capture_views(path, frame):
    """[the c0..c3 of each reflection view of a frame, in order]: a view is a run of draw, indx, tess and verts rows
    at surface pitch 280 between resolves (the spec author's s_views.py); its matrix is the one most of its rows
    carry. Each entry is (matrix [4, 4] or None, rows)."""
    np = _np()
    views, current = [], None
    for kind, row_frame, pitch, matrix in read_capture(path)["order"]:
        if row_frame != frame:
            continue
        if kind == "resolve" or pitch != VIEW_PITCH:
            current = None
            continue
        if current is None:
            current = []
            views.append(current)
        current.append(matrix)
    out = []
    for rows in views:
        tally = {}
        for m in rows:
            if m is not None:
                tally[m] = tally.get(m, 0) + 1
        best = max(tally, key=tally.get) if tally else None
        out.append((np.array(best, dtype=np.float64).reshape(4, 4) if best is not None else None, len(rows)))
    return out


def _view_error(derived, view):
    np = _np()
    error = np.abs(derived - view) / np.maximum(1.0, np.abs(view))
    return float(error.max()), [float(x) for x in error.max(axis=1)]


def check_views(path):
    """Criterion 3 on a capture, every frame: each reflection view's own rows against water_view_rows(M, c31.xy, h)
    with M the main scene's c0..c3 of the frame's first water row, c31 that row's pixel register and h the height of
    the meshes that sample the view's picture (the k-th view's picture is the k-th smallest slot-13 base of the
    frame's water rows: a wrong pairing shows as a wrong height). [ViewResult]."""
    np = _np()
    capture = read_capture(path)
    out = []
    for frame in sorted(f for f in capture["frames"] if f is not None):
        rows = [r for r in capture["water"] if r.get("frame") == frame]
        views = capture_views(path, frame)
        if not rows and not views:
            continue
        heights, note = {}, ""
        for r in rows:
            try:
                base = _fc(r, 13)[1] & 0xFFFFF000
                h = row_height(Path(capture["path"]).parent, r)
            except cr.Unreadable as e:
                note = "a mesh is not read: %s" % e
                continue
            if h is None:
                note = "the mesh of line %s is not flat" % r.get("line")
            heights.setdefault(base, []).append(h)
        bases = sorted(heights)
        for base in bases:
            if any(h is None or abs(h - heights[base][0]) > HEIGHT_FLAT for h in heights[base]):
                note = note or "the meshes of base %08X have more than one height" % base
        if len(bases) != len(views):
            note = note or "%d views and %d slot-13 bases" % (len(views), len(bases))
        c31 = ic.pixel_register(rows[0], 31) if rows else None
        if rows and (c31 is None or not all(math.isfinite(v) for v in c31[:2])):
            note = note or "the water row has no c31.xy"
        for k, (view, count) in enumerate(views):
            if note or view is None:
                out.append(ViewResult(frame, k + 1, bases[k] if k < len(bases) else None, None, count, math.inf, [math.inf] * 4, False,
                                      {name: False for name in VIEW_MUTATIONS}, note or "the view's rows carry no matrix"))
                continue
            M = np.array(rows[0]["bank"][0:16], dtype=np.float64).reshape(4, 4)
            h = heights[bases[k]][0]
            worst, by_row = _view_error(water_view_rows(M, c31[:2], h), view)
            mutants = {"factor": water_view_rows(M, (1.0, 1.0), h), "plane": water_view_rows(M, c31[:2], 0.0),
                       "height": water_view_rows(M, c31[:2], h + HEIGHT_STEP)}
            caught = {name: _view_error(mutants[name], view)[0] > VIEW_TOLERANCE for name in VIEW_MUTATIONS}
            out.append(ViewResult(frame, k + 1, bases[k], h, count, worst, by_row, worst <= VIEW_TOLERANCE, caught, ""))
    return out


# --- bools, states, bounds -----------------------------------------------------------------------------------------

def bool_words(path):
    """{(kind, the device's shadow "pbool", the last writer's "pbool_load"): rows} of a capture's water rows (None
    for a member a row does not have)."""
    out = {}
    for r in water_rows(path):
        key = (row_kind(r),) + tuple(int(str(r[m]), 16) if r.get(m) is not None else None for m in ("pbool", "pbool_load"))
        out[key] = out.get(key, 0) + 1
    return out


def microcode_bools(dumps=DUMPS):
    """{hash: the bools its dump names (`bN`), ascending} for the two pixel shaders."""
    import re
    return {h: sorted({int(n) for n in re.findall(r"\bb(\d+)\b", _text(dumps, h))}) for h in (PS, PS_OPAQUE)}


def state_words(path):
    """{(kind, register): {value: rows}} of the water rows' state words (STATE_REGISTERS)."""
    out = {}
    for r in water_rows(path):
        for reg, _ in STATE_REGISTERS:
            value = (r.get("rs") or {}).get(reg)
            value = int(str(value), 16) if value is not None else None
            tally = out.setdefault((row_kind(r), reg), {})
            tally[value] = tally.get(value, 0) + 1
    return out


def row_bounds(paths):
    """({("vs" | "ps", register): [(smallest, largest) of each component a formula reads, None for the others]}, the
    rows counted) over the water rows of the captures given. Vertex: c0..c3 and the table's four; pixel: the 24."""
    spans, rows = {}, 0
    for path in paths:
        for r in water_rows(path):
            rows += 1
            try:
                vbank = ic.row_bank(r)
            except ValueError:
                vbank = None
            items = [(("vs", reg), vbank[reg] if vbank else None, comps) for reg, comps in lf.WATER_SHADER["vs_read"].items()]
            items += [(("ps", reg), ic.pixel_register(r, reg), comps) for reg, comps in lf.WATER_SHADER["ps_read"].items()]
            for key, values, comps in items:
                span = spans.setdefault(key, [None] * 4)
                if values is None:
                    continue
                for k in comps:
                    lo, hi = span[k] if span[k] is not None else (math.inf, -math.inf)
                    v = values[k]
                    span[k] = (min(lo, v), max(hi, v)) if v == v else (math.nan, math.nan)
    return spans, rows


def bounds_outside(spans):
    """The names ("pixel c19.x") of the read components `row_bounds` found outside the sanity bound of their kind
    (gen_water_table: a place within 1e6, a matrix term within 1e7, a factor within 65,536, c128.x, c136.x and
    c138.x in [1 / 65,536, 65,536]; the rows c0..c3 are matrix terms; no number is outside)."""
    import gen_water_table as gen
    e = gen.entry()
    out = []
    for (bank, reg), span in sorted(spans.items(), key=lambda kv: (kv[0][0] != "vs", kv[0][1])):
        lo, hi = (-gen.MATRIX, gen.MATRIX) if bank == "vs" and reg < 4 else gen.bound(e, (bank, reg))
        for k, pair in enumerate(span):
            if pair is not None and not (lo <= pair[0] <= pair[1] <= hi):
                out.append("%s c%d.%s" % ("vertex" if bank == "vs" else "pixel", reg, "xyzw"[k]))
    return out


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
    if getattr(args, "capture", None):
        return cmd_dead_rows(args, say)
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


def _captures_of(args):
    return [cr.find_capture(c) for c in args.capture]


def cmd_rows(args, say=print):
    bad = checked = 0
    for path in _captures_of(args):
        r = check_rows(path, args.positions, dumps=args.dumps)
        if not r.rows:
            say("%s: no row of %s: nothing to check" % (path.name, VS[:4]))
            continue
        checked += 1
        problems = rows_problems(r)
        blind = rows_blind(r)
        say("%s: %d rows of %s (%d with %s, %d with %s); the row's own vertex bank, pixel registers (\"pfull\"), literal table "
            "(\"ps_lit\") and bools (\"%s\"); one sampler for both sides%s" % (
                path.name, r.rows, VS[:4], r.surface_rows, PS[:4], r.opaque_rows, PS_OPAQUE[:4], BOOL_MEMBER, ": OK" if not problems else ": FAILED"))
        say("  rows with a component a formula reads that is not finite: %d; rows the capture does not hold whole: %d" % (r.not_finite, r.unread))
        say("  the vertex shader at every vertex of the rows' dumps: %d of %d agree (bound %g of max(1, |value|)); worst %.3e" % (
            r.vertices - r.vertex_failed, r.vertices, VERTEX_TOLERANCE, r.vertex_worst))
        say("  the pixel shaders at %d positions (%d a row; %d of them outside the picture): %d agree (the colour within %g of max(1, "
            "|value|), the alpha within %g, every fetch's slot and coordinates); worst %.3e" % (
                r.positions, args.positions, r.outside, r.positions - r.failed, PIXEL_TOLERANCE, ALPHA_TOLERANCE, r.worst))
        for kind in ("surface", "opaque"):
            f = r.fates[kind]
            say("    %s: killed %d, skipped %d, shaded %d" % (PIXEL_HASH[kind][:4], f["killed"], f["skipped"], f["shaded"]))
        say("    of the shaded %s positions: with the sun term %d (its cosine above 0 in %d, the term above 1%% of the colour in %d; the "
            "largest term %.3g of max(1, |colour|)), near %d, fogged %d; the smallest c140.x * distance %.4g" % (
                PS[:4], r.sun, r.lit, r.glinting, r.facts["sun_largest"], r.near, r.fogged, r.facts["min_reach"]))
        say("  the single-precision reference against the double one at the shaded positions: at most %.3e of max(1, |colour|)" % r.single)
        say("  pixel registers the machine read: %s" % " ".join("c%d" % x for x in sorted(r.used)))
        total = {name: (r.vertices if name in VERTEX_MUTATIONS else r.positions if name in ALPHA_MUTATIONS else sum(r.fates["surface"].values()))
                 for name in r.caught}
        for name, n in r.caught.items():
            note = "" if n else (": cannot fail on these rows: " + blind[name] if name in blind else ": NOT CAUGHT")
            say("  mutation %-13s: %d of %d disagree%s" % (name, n, total[name], note))
        for text in problems:
            say("  " + text)
        bad += bool(problems)
    if not checked:
        say("no capture with water rows")
        return 1
    say("all agree" if not bad else "%d captures FAILED" % bad)
    return 1 if bad else 0


def cmd_dead_rows(args, say=print):
    import water_textures as wt
    bad = checked = 0
    say("criterion 2, parts (b) and (c). They hold under the water's fetch rule (water_textures.fetch, the twin of "
        "render::WaterTextureFetch: a 2D k_8 mask whose delivered component is unsigned, a fraction and not exponent-adjusted), "
        "which is what makes the mask's sample a number in [0, 1]: each captured slot-1 constant is held to it here.")
    for path in _captures_of(args):
        capture = read_capture(path)
        if not capture["water"]:
            say("%s: no water row: nothing to check" % path.name)
            continue
        checked += 1
        r = check_dead_rows(path)
        refused = []
        for fc in sorted({_fc(row, 1) for row in capture["water"] if (row.get("tf") or {}).get("1")}):
            try:
                wt.fetch(list(fc), "k8")
            except ValueError as e:
                refused.append("slot-1 constant %s is refused by the fetch rule: %s" % (" ".join("%08X" % v for v in fc), e))
        problems = r.problems + refused
        if not r.masks:
            problems.append("no mask was read")
        c141 = sorted({tuple(ic.pixel_register(row, 141) or ()) for row in capture["water"]}, key=str)
        say("%s: (b) water_opaque_dead holds for %d of %d rows of %s; c141 of the water rows: %s" % (
            path.name, r.dead, r.rows, PS_OPAQUE[:4], "; ".join("(%s)" % ", ".join("%.9g" % v for v in c) for c in c141)))
        say("  (c) %d masks, every slot-1 constant inside the fetch rule: %s; texels whose alpha is 1 (double, and single precision "
            "with the multiply-add rounded twice and fused): %d; the largest alpha %.6f%s" % (
                r.masks, "yes" if not refused else "NO", r.ones, r.largest, ": OK" if not problems else ": FAILED"))
        for name, w, h, texels, ones, top, land, open_ in r.table:
            say("    %s %3d x %-3d: alpha == 1 in %d of %d texels; the largest alpha %.6f; at or below c141.w %.2f; 0.99 or more %.2f" % (
                name[:8], w, h, ones, texels, top, land, open_))
        for text in problems:
            say("  " + text)
        bad += bool(problems)
    if not checked:
        say("no capture with water rows")
        return 1
    say("dead on every row and mask" if not bad else "%d captures FAILED" % bad)
    return 1 if bad else 0


def cmd_views(args, say=print):
    bad = views = 0
    worst = 0.0
    for path in _captures_of(args):
        results = check_views(path)
        if not results:
            say("%s: no reflection view and no water row" % path.name)
            continue
        say("%s: each view's rows against diag(c31.x, c31.y, 1, 1) * M * Mirror(h); bound %g of max(1, |entry|)" % (path.name, VIEW_TOLERANCE))
        for v in results:
            views += 1
            missed = [name for name in VIEW_MUTATIONS if not v.caught[name]]
            ok = v.ok and not missed
            bad += not ok
            if v.note:
                say("  frame %s view %d (%d rows): NOT COMPARED: %s" % (v.frame, v.view, v.rows, v.note))
                continue
            worst = max(worst, v.worst)
            say("  frame %s view %d (%3d rows), picture %08X, h %.6g from the meshes: largest error %.3e (rows %s)%s; mutations %s" % (
                v.frame, v.view, v.rows, v.base, v.height, v.worst, " ".join("%.1e" % x for x in v.by_row), "" if v.ok else ": FAILED",
                "all fail" if not missed else "NOT CAUGHT: " + ", ".join(missed)))
    if not views:
        say("no view was compared")
        return 1
    say("%d of %d views within %g (the largest error %.3e); each of the mutations %s fails in every view" % (
        views - bad, views, VIEW_TOLERANCE, worst, ", ".join(VIEW_MUTATIONS)) if not bad else "%d of %d views FAILED" % (bad, views))
    return 1 if bad else 0


def cmd_bools(args, say=print):
    b136, b137 = lf.WATER_SHADER["b136"], lf.WATER_SHADER["b137"]
    named = microcode_bools(args.dumps)
    say("the bools the two dumps name: %s" % "; ".join("%s: %s" % (h[:4], ", ".join("b%d" % n for n in v) or "none") for h, v in named.items()))
    say("bool bN is bit N & 31 of its word (the SDK's interpreter; light_formulas.IMPOSTOR_B129): b136 is 0x%08X and b137 0x%08X of the "
        "pixel word b128..b159" % (b136, b137))
    rows = differ = 0
    for path in _captures_of(args):
        for (kind, shadow, load), n in sorted(bool_words(path).items(), key=str):
            rows += n
            same = shadow is not None and load is not None and (shadow & (b136 | b137)) == (load & (b136 | b137))
            differ += 0 if same else n
            say("%s: %s: %d rows: \"pbool\" (the device's shadow) %s: b136 %s, b137 %s; \"pbool_load\" (the last writer's word) %s: b136 %s, b137 %s%s" % (
                path.name, PIXEL_HASH[kind][:4], n,
                "none" if shadow is None else "0x%08X" % shadow, "-" if shadow is None else int(bool(shadow & b136)), "-" if shadow is None else int(bool(shadow & b137)),
                "none" if load is None else "0x%08X" % load, "-" if load is None else int(bool(load & b136)), "-" if load is None else int(bool(load & b137)),
                "" if same else ": THE TWO MEMBERS DIFFER IN THESE BITS"))
    if not rows:
        say("no water row")
        return 1
    say("%d water rows; the two members agree in b136 and b137 in %d (they cannot be told apart by these rows where they agree); the "
        "formulas take \"%s\"" % (rows, rows - differ, BOOL_MEMBER))
    return 0


def cmd_states(args, say=print):
    total = {}
    for path in _captures_of(args):
        for key, tally in state_words(path).items():
            merged = total.setdefault(key, {})
            for value, n in tally.items():
                merged[value] = merged.get(value, 0) + n
    if not total:
        say("no water row")
        return 1
    for kind in ("opaque", "surface"):
        for reg, name in STATE_REGISTERS:
            tally = total.get((kind, reg))
            if not tally:
                continue
            rows = sum(tally.values())
            text = ", ".join("%s in %d" % ("none" if v is None else "0x%08X" % v, n) for v, n in sorted(tally.items(), key=str))
            if len(tally) == 1:
                value = next(iter(tally))
                text = "one value in %d of %d rows: %s" % (rows, rows, "none" if value is None else "0x%08X" % value)
                if reg == "210E" and value is not None:
                    text += " = %.9g (15 / 255 is %.9g, bits 0x%08X)" % (struct.unpack(">f", struct.pack(">I", value))[0], f32(15.0 / 255.0),
                                                                        struct.unpack(">I", struct.pack(">f", 15.0 / 255.0))[0])
            say("%s %s (%s): %s" % (PIXEL_HASH[kind][:4], reg, name, text))
    return 0


def cmd_bounds(args, say=print):
    import gen_water_table as gen
    paths = _captures_of(args)
    spans, rows = row_bounds(paths)
    if not rows:
        say("no water row")
        return 1
    e = gen.entry()
    say("%d water rows of %s: the smallest and largest of every component a formula reads" % (rows, ", ".join(p.name for p in paths)))
    for (bank, reg), span in sorted(spans.items(), key=lambda kv: (kv[0][0] != "vs", kv[0][1])):
        say("  %s c%-3d %s   (%s)" % ("vertex" if bank == "vs" else "pixel ", reg,
                                    "  ".join("not read" if p is None else "[%.9g, %.9g]" % p for p in span),
                                    "the draw record's rows: a matrix term" if bank == "vs" and reg < 4 else gen.kind_text(e, (bank, reg))))
    out = bounds_outside(spans)
    if out:
        say("OUTSIDE the sanity bounds: %s" % ", ".join(out))
        return 1
    say("every read component inside the sanity bound of its kind (gen_water_table.py: fitted to no capture)")
    return 0


def main(argv=None, say=print):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dumps", type=Path, default=DUMPS)
    sub = ap.add_subparsers(dest="command", required=True)
    for name, run in (("formulas", cmd_formulas), ("dead", cmd_dead)):
        p = sub.add_parser(name)
        p.add_argument("--trials", type=int, default=2000)
        p.add_argument("--seed", type=int, default=1)
        if name == "dead":
            p.add_argument("capture", nargs="*")
        p.set_defaults(run=run)
    for name, run in (("rows", cmd_rows), ("views", cmd_views), ("bools", cmd_bools), ("states", cmd_states), ("bounds", cmd_bounds)):
        p = sub.add_parser(name)
        p.add_argument("capture", nargs="+")
        if name == "rows":
            p.add_argument("--positions", type=int, default=ROW_POSITIONS)
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
