"""Checks the post chain's formulas of light_formulas.py against the shaders' microcode.

Usage: post_check.py [--trials N] [--dumps DIR] [--literals random]
                                 (the eight post shaders and the fourteen furniture shaders: exit code 0 only
                                  when every value of every one agrees)
       post_check.py --shader HASH [--trials N]
       post_check.py --list

Each of light_formulas.POST_SHADERS runs in ucode_eval.Machine on random inputs against its closed formula:
random interpolators or pixel positions, random registers, the literal c255 as the capture rows hold it
(native_discovery_20261009_092959.jsonl, "ps_lit" of the rows; research section 1), and textures that are
smooth analytic functions of (slot, coordinate + the fetch's offset), so that a wrong coordinate or a wrong
offset gives a wrong value. A trial compares the four components of oC0; the tolerance is 1e-9 relative to
max(1, |a|, |b|) (double arithmetic on both sides). The counts are of values: 4 a trial, 2,000 at the default
500 trials (spec criterion 1).

The trials are those of the research script this file came from (out/tests/post-research/post_check.py:
seed 20261009 per shader, the same draws in the same order). What differs:
  - the composite's table is a table: 512 random texels, read by the interpreter's sampler at
    ucode_eval.point_texel of the fetch (its own OffsetX=0.5 included) and by the formula at
    light_formulas.post_table_texel. So the check holds the formula's addressing to the interpreter's rule
    for a point fetch, texel for texel, and the coordinate c46.x * L besides (the research read a smooth
    function of the coordinate on both sides, which no addressing could fail), and half its trials draw
    c77.x from 1.5 to 4, where the research's -0.3 to 0.3 never made t negative;
  - the formulas take pixel positions and unnormalised textures (light_formulas, "the post chain"): the
    trial's texture is 64 x 64 texels, an interpolator u is the position u * 64 (u * 32 for the reduce, whose
    target is half its source).
What a trial cannot reach: the targets' clamps and the 10-bit rounding (the render target's, not the
microcode's), single precision, the filter of a fetch (the sampler state's, not the microcode's: the field is
continuous), whether param_gen hands the downsample an integral position (--bias adds to the position on both
sides), and L = 0 in the bright pass (0 * (1 / 0) in the microcode; the formula's scene term is 0 there).

The furniture (light_formulas.POST_FURNITURE_SHADERS: eleven pixel and three vertex shaders of the sun layer and
the sky furniture; plan Task 11, spec criterion 16) is checked the same way, with what its shaders need besides:
  - every register a shader reads is drawn with four different components, and the texel's four channels
    differ, so a wrong component, channel or slot gives a wrong value; a third of the quad shaders' trials
    read texels whose channels go below 0 and above 1 (_texels_wide), so a clamp or an absolute value on a
    texel, which no quad shader has, gives a wrong value too;
  - a trial compares oC0's four components (the layer's depth shader: oDepth too; a vertex shader: oPos and
    every interpolator it writes), so a shader has 2,000 values or more at 500 trials;
  - the clouds' trials are spread over both sides of every branch and clamp, and the result counts them
    (`reached`): pixels inside, across and beyond the fade; alpha below, above and (a tenth of the trials)
    exactly at the threshold c254.y, where the shader takes the dark branch; a light term clamped at 1;
    c69.x and c70.x of either sign. The cube's trials take vertices on the face the shader singles out and
    off it;
  - the literals (c254, c255) are the capture rows' (FURNITURE_LITERALS); with --literals random they are
    drawn, which holds what a literal of 0 or 1 hides: the absolute values under 6197's square root, its
    reciprocal square root and its power, the term D1CC adds, and the thin branch's comparison with c254.x:
    a third of the drawn trials put c254.x between -|P.x| and -|P.y| or exactly at -|P.x| under a thin
    cloud, which holds the comparison's component and its strictness (with the literal 0 the branch's
    colour is 0 whatever is compared).
What a furniture trial cannot reach: everything of the draw's state (blend, depth, the alpha test and its
reference, the colour mask), the sampler's filter, mip level and addressing (the texel offsets of 6197 are in
texels of "the level the sampler reads": here a field of 64 texels), single precision, and inputs for which
the microcode gives no number (a pixel at the clouds' far point, a slope and c70.x both 0). The clouds' and
their occluder's texture stays the smooth field (0.25 to 0.95): a density below 0 or above 1 is not drawn.
"""
import argparse
import math
import random
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import light_formulas as lf  # noqa: E402
import ucode_eval as ue  # noqa: E402

DUMPS = Path(__file__).resolve().parents[2] / "out" / "shader_dump"
TOLERANCE = 1e-9
COORD_TOLERANCE = 1e-9
SEED = 20261009
SIZE = 64.0                 # the trial texture's width and height in texels
TRIALS = 500

# The literal register c255 of each shader that reads one.
LITERALS = {
    "87E85199E1E9B260": [0.25, 4.0, 0.0, 0.0],
    "5821366EE8FDE893": [0.0721, 0.2125, 0.7154, 0.0],
    "EE922CFB4426EA83": [0.0721, 0.2125, 0.7154, 6.0],
    "4BE4344C27016E04": [0.5, 1.0, 4.0 / 7.0, 0.0],
    "37B186F4BD3288F7": [0.0721, 0.2125, 0.7154, 0.0],
}


class Result:
    def __init__(self):
        self.trials = 0
        self.values = self.agree = 0        # components of oC0 compared / within the tolerance
        self.coords = self.coords_ok = 0    # trials whose fetch coordinates are compared / agree
        self.worst = 0.0
        self.first_bad = None               # (trial, microcode, formula)
        self.errors = {}
        self.reached = {}                   # what the trials reached (a body's words) -> trials

    @property
    def ok(self):
        return (self.trials > 0 and self.values > 0 and self.agree == self.values and not self.errors
                and self.coords_ok == self.coords)

    def line(self):
        text = f"{self.agree} of {self.values} values agree, largest relative difference {self.worst:.3g}"
        if self.coords:
            text += f", fetch coordinates {self.coords_ok}/{self.coords}"
        return text + (f", NOT RUN {self.errors}" if self.errors else "")


def field(slot, x, y):
    """A smooth positive texture of real coordinates in units of the texture's size: four channels."""
    return [0.6 + 0.35 * math.sin(1.3 * x * (i + 1) + 0.7 * y + slot + i) * math.cos(0.9 * y * (i + 2) - slot)
            for i in range(4)]


def _close(a, b, tol=COORD_TOLERANCE):
    return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def _normalised(slot, coords, attrs):
    """The interpreter's sampler for a fetch with normalised coordinates of a SIZE x SIZE texture."""
    ox, oy, _ = ue.fetch_offsets(attrs)
    return field(slot, coords[0] + ox / SIZE, coords[1] + oy / SIZE)


def _texels(slot):
    """The same texture as the formulas take it: tex(px, py) of unnormalised coordinates."""
    return lambda px, py: field(slot, px / SIZE, py / SIZE)


def _uv(rng):
    return [rng.uniform(0, 1), rng.uniform(0, 1), 0, 0]


class _Trial:
    """What a body hands back: the microcode's oC0, the formula's four values, and whether the fetch
    coordinates agree (None where the body compares none)."""

    def __init__(self, got, want, coords=None, reached=()):
        self.got, self.want, self.coords, self.reached = list(got), list(want), coords, tuple(reached)


def _run(text, shader, consts, regs, sampler):
    c = dict(consts)
    if shader in LITERALS:
        c[255] = LITERALS[shader]
    m = ue.Machine(text, c, sampler, regs=regs)
    return m, m.run()["oC0"]


def _reduce(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    _, out = _run(text, shader, {}, {0: uv}, _normalised)
    kw = {"clamp": False} if formula is lf.post_reduce else {}
    return _Trial(out, formula(_texels(0), uv[0] * SIZE / 2.0, uv[1] * SIZE / 2.0, **kw))


def _downsample(text, shader, rng, aux, formula, opts):
    x, y = rng.choice([-1, 1]) * rng.randrange(0, 280), rng.choice([-1, 1]) * rng.randrange(0, 180)
    bias = opts.get("bias", 0.0)
    pos = [math.copysign(abs(x) + bias, x), math.copysign(abs(y) + bias, y), 0, 0]

    def sampler(slot, coords, attrs):
        if str(attrs.get("UnnormalizedTextureCoords")).lower() != "true":
            raise ValueError("the downsample's fetch is not unnormalised")
        ox, oy, _ = ue.fetch_offsets(attrs)
        return field(slot, coords[0] + ox, coords[1] + oy)

    _, out = _run(text, shader, {}, {0: pos}, sampler)
    return _Trial(out, formula(lambda px, py: field(0, px, py), x, y, bias))


def _luminance(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    _, out = _run(text, shader, {}, {0: uv}, _normalised)
    L = formula(_texels(0)(uv[0] * SIZE + 0.5, uv[1] * SIZE + 0.5))
    return _Trial(out, [L, L, L, L])


def _bright(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    c = {31: [rng.uniform(0.5, 3)] * 4, 46: [rng.uniform(0.05, 1)] * 4, 47: [rng.uniform(0.5, 4)] * 4,
         77: [rng.uniform(-0.2, 0.6), rng.uniform(0.05, 1.5), 0, 0]}
    _, out = _run(text, shader, c, {0: uv}, _normalised)
    d, tf1, tf2 = (_texels(s)(uv[0] * SIZE + 0.5, uv[1] * SIZE + 0.5) for s in (0, 1, 2))
    return _Trial(out, list(formula(d, tf1, tf2, c[31][0], c[46][0], c[47][0], c[77], clamp=False)) + [1.0])


def _blur(vertical):
    def body(text, shader, rng, aux, formula, opts):
        uv = _uv(rng)
        c = {46: [rng.uniform(0, 0.3) for _ in range(4)], 47: [rng.uniform(0, 0.1) for _ in range(4)]}
        m, out = _run(text, shader, c, {0: uv}, _normalised)
        if len(m.fetches) != 13:
            raise ValueError(f"{len(m.fetches)} fetches, not 13")
        weights = c[46] + c[47][:3]
        return _Trial(out, formula(_texels(0), uv[0] * SIZE, uv[1] * SIZE, weights, vertical, clamp=False))
    return body


def _radial(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    c = {31: [rng.uniform(-0.5, 1.5), rng.uniform(-0.5, 1.5), 0, 0], 46: [rng.uniform(0.5, 1.0)] * 4,
         47: [rng.uniform(0.8, 1.0)] * 4}
    m, out = _run(text, shader, c, {0: uv}, _normalised)
    want = formula(lambda u, v: field(0, u + 0.5 / SIZE, v + 0.5 / SIZE), uv[0], uv[1], c[31], c[46][0], c[47][0],
                   clamp=False)
    fetched = sorted(tuple(f[2]) for f in m.fetches)
    mine = sorted(p for p, _ in lf.post_radial_taps(uv[0], uv[1], c[31], c[46][0], c[47][0]))
    coords = len(fetched) == 14 and all(_close(a[0], b[0]) and _close(a[1], b[1]) for a, b in zip(fetched, mine))
    return _Trial(out, want, coords)


def _composite(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    c = {31: [rng.uniform(0, 4)] * 4, 46: [rng.uniform(0.05, 0.5)] * 4,
         47: [rng.choice([50.0, rng.uniform(0.2, 3)])] * 4,
         # Half the trials get a saturation step strong enough to drive a channel of t below zero: the
         # shader's |t| (instructions 17 to 19) is then held (a formula with max(t, 0) fails).
         77: [rng.choice([rng.uniform(-0.3, 0.3), rng.uniform(1.5, 4.0)]), rng.uniform(0.3, 2.5), 0, 0],
         78: [rng.uniform(0.3, 1.0)] * 4}
    scale = rng.choice([0.2, 1.0, 3.0])
    table = [aux.uniform(0.0, 0.6) for _ in range(lf.POST_TABLE_TEXELS)]

    def sampler(slot, coords, attrs):
        if slot == 1:
            if len(coords) != 1 or attrs.get("MagFilter") != "point" or attrs.get("MinFilter") != "point":
                raise ValueError(f"the table's fetch is not a 1D point fetch: {attrs}")
            texel = ue.point_texel(coords, (len(table),), attrs)[0]
            return [table[min(max(texel, 0), len(table) - 1)]] * 4
        f = _normalised(slot, coords, attrs)
        return [x * scale for x in f] if slot == 0 else f

    m, out = _run(text, shader, c, {0: uv}, sampler)
    scene = [x * scale for x in _texels(0)(uv[0] * SIZE + 0.5, uv[1] * SIZE + 0.5)][:3]
    bloom = _texels(2)(uv[0] * SIZE + 0.5, uv[1] * SIZE + 0.5)[:3]
    texel = opts.get("texel")
    want = formula(scene, bloom, table if texel is None else (lambda u: table[texel(u)]), c[31][0], c[46][0], c[47][0],
                   c[77], c[78][0], clamp=False)
    t1 = [f for f in m.fetches if f[1] == 1]
    coords = len(t1) == 1 and _close(t1[0][2][0], c[46][0] * lf.post_lum(scene))
    return _Trial(out, list(want) + [want[2]], coords)


# --- the sun layer and the sky furniture -------------------------------------------------------------------

# The literal registers of the furniture shaders that read any, as the capture rows hold them ("ps_lit" /
# "vs_lit" of native_discovery_20261009_134312.jsonl, register 252 on; tests/test_post_sun_layer.py compares).
FURNITURE_LITERALS = {
    "8E4DBB9E9A63EF67": {255: [lf.POST_BEAM_C255X, 0.0, 0.0, 0.0]},
    "4B58837D42F08C89": {255: [lf.POST_GLOW_C255X, 0.0, 0.0, 0.0]},
    "61976295517D7015": {254: list(lf.POST_CLOUD_C254), 255: [lf.POST_CLOUD_C255X, 0.0, 0.0, 0.0]},
    "3FF79524FCDB71C5": {255: list(lf.POST_CLOUD_OCCLUDER_C255)},
    "D1CC6DC28244C55A": {254: list(lf.POST_CUBE_C254), 255: [1.0, 0.0, 0.0, 0.0]},
}
_STAGE_WORD = {"frag": "ps", "vert": "vs"}
QUAD_SLOT = 13


def _reg4(rng, lo=-0.5, hi=2.0):
    """A register of four different components."""
    return [rng.uniform(lo, hi) for _ in range(4)]


def _fade_literals(rng):
    """(a zero that is added, a rate, a start, a one) drawn: the clouds' fade with other numbers. The "one" goes
    down to 0.2, so that 6197's 1 - a1 is often well below zero."""
    return (rng.choice([rng.uniform(-0.002, 0.002), rng.uniform(-4e6, 4e6)]), rng.uniform(0.0003, 0.003),
            rng.uniform(200.0, 1500.0), rng.uniform(0.2, 1.5))


def _random_literals(shader, rng):
    if shader in ("8E4DBB9E9A63EF67", "4B58837D42F08C89"):
        return {255: _reg4(rng, 0.05, 1.0)}
    if shader == "61976295517D7015":
        # Half the exponents are small: a power of |1 - a1| that a large exponent would bury under the tolerance.
        return {254: list(_fade_literals(rng)), 255: [rng.choice([rng.uniform(1.0, 3.0), rng.uniform(1.0, 40.0)])] + _reg4(rng)[:3]}
    if shader == "3FF79524FCDB71C5":
        zero, rate, start, one = _fade_literals(rng)
        return {255: [start, zero, rate, one]}
    if shader == "D1CC6DC28244C55A":
        return {254: [rng.uniform(0.5, 3.0), rng.uniform(-2.0, 2.0), rng.uniform(100.0, 3000.0), rng.uniform(-2.0, 2.0)],
                255: [rng.choice([0.0, 0.5, 1.0])] + _reg4(rng)[:3]}
    return {}


def _furniture(text, shader, consts, regs, sampler, rng, opts, vfetch=None):
    """Runs a furniture shader: the machine, its outputs and the registers it ran with (the literals among
    them: the capture rows', or drawn with opts["literals"] == "random")."""
    c = dict(consts)
    if opts.get("literals") == "random":
        c.update(_random_literals(shader, rng))
    else:
        c.update({k: list(v) for k, v in FURNITURE_LITERALS.get(shader, {}).items()})
    m = ue.Machine(text, c, sampler, regs=regs, vfetch=vfetch)
    return m, m.run(), c


def _texels_wide(rng):
    """(the interpreter's sampler, tex(slot, x, y)) of a trial's texture. Two trials of three read the smooth
    field (0.25 to 0.95); the third reads it stretched and moved, so that its channels go below 0 and above 1:
    a clamp or an absolute value on a texel's channel, which the microcode does not have (its `maxs x, x` is
    x), then gives another value."""
    k, o = (3.0, rng.uniform(-2.6, -0.4)) if rng.random() < 1.0 / 3.0 else (1.0, 0.0)

    def tex(slot, x, y):
        return [v * k + o for v in field(slot, x, y)]

    def sampler(slot, coords, attrs):
        ox, oy, _ = ue.fetch_offsets(attrs)
        return tex(slot, coords[0] + ox / SIZE, coords[1] + oy / SIZE)

    return sampler, tex


def _moon(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    sampler, tex = _texels_wide(rng)
    _, out, c = _furniture(text, shader, {31: _reg4(rng), 20: _reg4(rng, 0.5, 16.0)}, {0: uv}, sampler, rng, opts)
    return _Trial(out["oC0"], formula(tex(QUAD_SLOT, uv[0], uv[1]), c[31], c[20][3], clamp=False))


_disc = _moon       # the same inputs; another alpha


def _glare(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    sampler, tex = _texels_wide(rng)
    _, out, c = _furniture(text, shader, {31: _reg4(rng)}, {0: uv}, sampler, rng, opts)
    return _Trial(out["oC0"], formula(tex(QUAD_SLOT, uv[0], uv[1]), c[31], clamp=False))


_flare = _glare


def _beam(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    sampler, tex = _texels_wide(rng)
    _, out, c = _furniture(text, shader, {31: _reg4(rng)}, {0: uv}, sampler, rng, opts)
    return _Trial(out["oC0"], formula(tex(QUAD_SLOT, uv[0], uv[1]), c[31], c[255][0], clamp=False))


def _glow(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    sampler, tex = _texels_wide(rng)
    _, out, c = _furniture(text, shader, {}, {0: uv}, sampler, rng, opts)
    return _Trial(out["oC0"], formula(tex(QUAD_SLOT, uv[0], uv[1]), c[255][0], clamp=False))


def _layer_depth(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)

    def wide(slot, x, y):       # a depth of -0.5 .. 1.5 (both sides of the output's clamp); other channels differ
        d = -0.5 + 2.0 * ((7.3 * x + 3.1 * y + 0.01 * slot) % 1.0)
        return [d, 0.9 - 0.5 * d, 0.3 + 0.25 * d, 1.0 - d]

    _, out, _ = _furniture(text, shader, {}, {0: uv}, lambda slot, coords, attrs: wide(slot, coords[0], coords[1]), rng, opts)
    t = wide(QUAD_SLOT, uv[0], uv[1])
    colour, depth = formula(t)
    reached = "below 0" if t[0] < 0.0 else "above 1" if t[0] > 1.0 else "inside"
    return _Trial(list(out["oC0"]) + [out["oDepth"][0]], list(colour) + [depth], reached=[reached])


def _previous(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    sampler, tex = _texels_wide(rng)
    m, out, _ = _furniture(text, shader, {}, {0: uv}, sampler, rng, opts)
    if len(m.fetches) != 1:
        raise ValueError(f"{len(m.fetches)} fetches, not 1")
    t = tex(QUAD_SLOT, uv[0] + 0.5 / SIZE, uv[1] + 0.5 / SIZE)         # the fetch's half texel in each axis
    return _Trial(out["oC0"], formula(t, clamp=False) if formula is lf.post_previous_frame else formula(t))


def _tree_occluder(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    sampler, tex = _texels_wide(rng)
    _, out, c = _furniture(text, shader, {8: _reg4(rng, 0.2, 3.0), 15: _reg4(rng)}, {0: uv}, sampler, rng, opts)
    u, v = lf.post_tree_occluder_uv(uv, c[8])
    return _Trial(out["oC0"], formula(tex(0, u, v), c[15][3], clamp=False))


def _cloud_point(rng):
    """(the eye, a pixel's world position, where it lies in the literals' fade): a third of the pixels inside
    1000 of the eye on the ground plane, half between, the rest beyond 2000."""
    eye = [rng.uniform(-200.0, 200.0) for _ in range(3)] + [1.0]
    where = rng.choice(["inside", "between", "between", "between", "beyond", "inside"])
    d = {"inside": rng.uniform(0.0, 900.0), "between": rng.uniform(1050.0, 1950.0), "beyond": rng.uniform(2100.0, 3500.0)}[where]
    a = rng.uniform(0.0, 2.0 * math.pi)
    return eye, [eye[0] + d * math.cos(a), eye[1] + d * math.sin(a), rng.uniform(300.0, 600.0), rng.uniform(-1.0, 1.0)], where


def _cloud_texture(alpha=None):
    """(the interpreter's sampler, the formula's tex) of the cloud's texture; alpha: one value for the
    whole alpha channel."""
    def fix(texel):
        return texel if alpha is None else texel[:3] + [alpha]

    def sampler(slot, coords, attrs):
        return fix(_normalised(slot, coords, attrs))

    return sampler, lambda u, v, dx, dy: fix(field(QUAD_SLOT, u + dx / SIZE, v + dy / SIZE))


def _cloud_occluder(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    eye, P, _ = _cloud_point(rng)
    sampler, tex = _cloud_texture()
    consts = {31: _reg4(rng, 1.0, 8.0), 46: _reg4(rng, 0.0, 1.0), 47: eye, 77: _reg4(rng, 0.2, 1.5)}
    _, out, c = _furniture(text, shader, consts, {0: P, 1: uv}, sampler, rng, opts)
    ramp = c[255][2] * (math.sqrt(abs((P[0] - eye[0]) ** 2 + (P[1] - eye[1]) ** 2 + c[255][1])) - c[255][0])
    reached = "fade 1" if ramp <= 0.0 else "fade 0" if ramp >= 1.0 else "fade between"
    want = formula(tex, uv, P, c[31], c[46], c[47], c[77][0], c[255], clamp=False)
    return _Trial(out["oC0"], [0.0, 0.0, 0.0, want], reached=[reached])


def _cloud(text, shader, rng, aux, formula, opts):
    uv = _uv(rng)
    eye, P, where = _cloud_point(rng)
    direction = [rng.uniform(-1.0, 1.0) for _ in range(3)]
    length = math.sqrt(sum(x * x for x in direction)) or 1.0
    strength = rng.uniform(0.3, 2.5)        # c78 is not a unit vector here: the light term's clamp at 1 is reached
    consts = {20: _reg4(rng, 0.5, 16.0), 31: _reg4(rng, 1.0, 8.0), 46: _reg4(rng, 0.0, 1.0), 47: _reg4(rng, 0.2, 1.2),
              51: _reg4(rng, 0.3, 1.5), 69: _reg4(rng, 0.2, 1.0), 70: _reg4(rng, 0.05, 1.0), 71: _reg4(rng, 0.2, 1.2),
              77: [rng.uniform(-2000.0, 2000.0) for _ in range(3)] + [rng.uniform(-1.0, 1.0)],
              78: [x / length * strength for x in direction] + [rng.uniform(-1.0, 1.0)], 79: eye}
    for reg in (69, 70):                    # a fifth of each with the other sign: |c69.x| or |c70.x| is another value
        if rng.random() < 0.2:
            consts[reg][0] = -consts[reg][0]
    if rng.random() < 0.3:                  # c78 against the normal's height: the light term a1 at its clamp of 1
        consts[78][:3] = [0.1 * consts[78][0], 0.1 * consts[78][1], -math.copysign(2.5, consts[70][0])]
    kind = rng.random()
    if kind < 0.15:                         # a thin cloud: alpha near the threshold of 0.001, on both sides of it
        consts[69][0] = rng.uniform(0.0, 0.004)
    edge = kind > 0.9
    if edge:
        # Alpha exactly at the threshold: a texture whose alpha is c254.y, c69.x = 1 and a pixel inside the
        # fade's start (fade = c254.w - 0). The shader takes the dark branch there (not above the threshold).
        consts[69][0] = 1.0
        P[0], P[1], where = eye[0] + 300.0, eye[1] - 400.0, "inside"
    lits = _random_literals(shader, rng) if opts.get("literals") == "random" else FURNITURE_LITERALS[shader]
    if opts.get("literals") == "random" and not edge and rng.random() < 1.0 / 3.0:
        # The thin branch's comparison (-|P.x| > c254.x) near its operand: c254.x between -|P.x| and
        # -|P.y| (another component gives the other answer), or exactly -|P.x| (not above it); and a thin
        # cloud, so that the branch is the one taken.
        lo, hi = sorted((-abs(P[0]), -abs(P[1])))
        lits = dict(lits)
        lits[254] = [rng.choice([rng.uniform(lo, hi), -abs(P[0])])] + list(lits[254][1:])
        consts[69][0] = rng.uniform(-0.002, 0.002)
    sampler, tex = _cloud_texture(lits[254][1] if edge else None)
    c = dict(consts)
    c.update({k: list(v) for k, v in lits.items()})
    m = ue.Machine(text, c, sampler, regs={0: P, 1: uv})
    out = m.run()
    c254 = c[254]
    want = formula(tex, uv, P, c[20][3], c[31], c[46], c[47], c[51][0], c[69][0], c[70][0], c[71][0], c[77], c[78], c[79],
                   c254, c[255][0], clamp=False)
    # What the trial reached, from the trial's own inputs (the counts are for the tests and the report).
    u, v = lf.post_cloud_uv(uv, c[31], c[46])
    alpha = tex(u, v, 0, 0)[3] * c[69][0] * lf.post_cloud_fade(P, eye, c254[2], c254[1], c254[0], c254[3])
    reached = ["fade " + where]
    if c[69][0] < 0.0:
        reached.append("c69.x below 0")
    if c[70][0] < 0.0:
        reached.append("c70.x below 0")
    if alpha == c254[1]:
        reached.append("at the threshold")
    if not alpha > c254[1]:
        reached.append("thin")
        if -abs(P[0]) == c254[0]:
            reached.append("thin, -|P.x| at c254.x")
        elif (-abs(P[0]) > c254[0]) != (-abs(P[1]) > c254[0]):
            reached.append("thin, c254.x between -|P.x| and -|P.y|")
        if len(m.fetches) != 1:
            raise ValueError(f"{len(m.fetches)} fetches in the thin branch, not 1")
    else:
        reached.append("lit")
        if len(m.fetches) != 5:
            raise ValueError(f"{len(m.fetches)} fetches in the lit branch, not 5")
        gx = tex(u, v, 1, 0)[3] - tex(u, v, -1, 0)[3]
        gy = tex(u, v, 0, 1)[3] - tex(u, v, 0, -1)[3]
        n = math.sqrt(abs(c[70][0] ** 2 + gx * gx + gy * gy + c254[0]))
        light = -(gx * c[78][0] + gy * c[78][1] + c[70][0] * c[78][2]) / n
        reached.append("light clamped at 1" if light >= 1.0 else "light clamped at 0" if light <= 0.0 else "light between")
    return _Trial(out["oC0"], want, reached=reached)


def _quad_vs(with_position):
    """81BA (False) and C304 (True): oPos, the coordinate's interpolator and, for C304, the position's."""
    def body(text, shader, rng, aux, formula, opts):
        p = [rng.uniform(-3000.0, 3000.0) for _ in range(3)]
        uv = _uv(rng)[:2]
        consts = {k: _reg4(rng, -2.0, 2.0) for k in range(4)}

        def vfetch(stream, offset, attrs, index):
            if stream != 0:
                raise ValueError(f"vertex stream {stream}")
            return p + [7.0] if offset == 0 else uv + [5.0, 6.0] if offset == 3 else [9.0, 9.0, 9.0, 9.0]

        _, out, c = _furniture(text, shader, consts, {0: [float(rng.randrange(4)), 0, 0, 0]}, None, rng, opts, vfetch=vfetch)
        want = list(formula(p, c[0], c[1], c[2], c[3])) + uv
        if with_position:
            return _Trial(list(out["oPos"]) + list(out["o1"][:2]) + list(out["o0"][:3]), want + p)
        return _Trial(list(out["oPos"]) + list(out["o0"][:2]), want)
    return body


def _cube_vs(text, shader, rng, aux, formula, opts):
    v = [float(rng.randrange(2)), float(rng.randrange(2)), rng.choice([0.0, 1.0, 1.0, 0.5])]
    consts = {k: _reg4(rng, -2.0, 2.0) for k in range(4)}
    consts.update({9: _reg4(rng, -200.0, 200.0), 117: _reg4(rng), 119: _reg4(rng, -50.0, 50.0)})
    _, out, c = _furniture(text, shader, consts, {0: [0.0, 0, 0, 0]}, None, rng, opts,
                           vfetch=lambda stream, offset, attrs, index: v + [7.0])
    want = formula(v, c[0], c[1], c[2], c[3], c[9], c[119][0], c[254], c[255][0])
    return _Trial(out["oPos"], want, reached=["the eye's face" if v[2] == c[255][0] else "the other faces"])


def _water_blur(text, shader, rng, aux, formula, opts):
    """The reflection views' blur: the seven fetch coordinates and the sum, with the offsets drawn (the captured
    hexagon would not tell two taps apart that a wrong form exchanges) and no number in what no instruction reads."""
    uv = _uv(rng)
    c = {reg: [rng.uniform(-0.05, 0.05), rng.uniform(-0.05, 0.05), math.nan, math.nan] for reg in lf.WATER_BLUR_SHADER["offsets"]}
    weight = rng.uniform(0.1, 0.2)
    c[255] = [weight, math.nan, math.nan, math.nan]
    m = ue.Machine(text, c, _normalised, regs={0: [uv[0], uv[1], math.nan, math.nan]})
    out = m.run()["oC0"]
    if len(m.fetches) != 7:
        raise ValueError(f"{len(m.fetches)} fetches, not 7")
    offsets = [c[reg][:2] for reg in lf.WATER_BLUR_SHADER["offsets"]]
    want = sorted((uv[0] + a, uv[1] + b) for a, b in offsets)
    got = sorted((f[2][0], f[2][1]) for f in m.fetches)
    coords = all(_close(g[0], w[0]) and _close(g[1], w[1]) for g, w in zip(got, want))
    return _Trial(out, formula(lambda u, v: field(0, u, v), uv[0], uv[1], offsets, weight), coords)
# hash -> (the body of a trial, the formula it checks)
CHECKS = {
    "0A96B3FD5121E331": (_reduce, lf.post_reduce),
    "87E85199E1E9B260": (_downsample, lf.post_downsample),
    "5821366EE8FDE893": (_luminance, lf.post_lum),
    "EE922CFB4426EA83": (_bright, lf.post_bright),
    "EA83C7E7E24EC4C8": (_blur(False), lf.post_blur13),
    "28DBE18F313350BF": (_blur(True), lf.post_blur13),
    "4BE4344C27016E04": (_radial, lf.post_radial),
    "37B186F4BD3288F7": (_composite, lf.post_composite),
    "18D39DCEBEE4C2FC": (_moon, lf.post_moon),
    "61976295517D7015": (_cloud, lf.post_cloud),
    "04985A0F7296E130": (_glare, lf.post_glare),
    "8E4DBB9E9A63EF67": (_beam, lf.post_beam_source),
    "D72FD0903CCE7DD6": (_flare, lf.post_flare),
    "4B58837D42F08C89": (_glow, lf.post_moon_glow),
    "3E77B1AD4BC5B900": (_disc, lf.post_sun_disc),
    "3FF79524FCDB71C5": (_cloud_occluder, lf.post_cloud_occluder),
    "5D6768847B0602B8": (_layer_depth, lf.post_layer_depth),
    "F1EB2497108F159D": (_tree_occluder, lf.post_tree_occluder),
    "686DE15FC84D8B73": (_previous, lf.post_previous_frame),
    "81BADE63A169E3DC": (_quad_vs(False), lf.post_quad_position),
    "C304C6A1AF18B46F": (_quad_vs(True), lf.post_quad_position),
    "D1CC6DC28244C55A": (_cube_vs, lf.post_cube_position),
}
# The reflection views' blur (the water sub-project, plan Task 15) is checked by the same machine and is no shader of
# the post chain: CHECKS stays the post chain's and the furniture's alone (tests/test_post_formulas.py holds that).
WATER_CHECKS = {lf.WATER_BLUR_SHADER["ps"]: (_water_blur, lf.water_blur7)}
STAGES = {s.hash: s.stage for s in lf.POST_FURNITURE_SHADERS}      # the others are pixel shaders

_REGISTER = re.compile(r"(?<![A-Za-z0-9_])c(\d+)\b")
_SLOT = re.compile(r"(?<![A-Za-z0-9_])tf(\d+)\b")


def registers_read(text):
    """(the registers below c252, the texture slots) a dump's instructions name, as two sets: what a record
    of the shader's draw must hold. The literals (c252 to c255) are the shader's own."""
    code = "\n".join(line.split("//")[0] for line in text.splitlines())
    return ({int(n) for n in _REGISTER.findall(code) if int(n) < 252}, {int(n) for n in _SLOT.findall(code)})


def check_text(shader, text, trials=TRIALS, seed=SEED, formula=None, tol=TOLERANCE, **opts):
    """One post shader's check on a dump's text. shader: the hash, which names the trial's body and the
    formula (`formula` in its place: a wrong form for the tests). opts: bias (the downsample: added to the
    pixel position on both sides), texel (the composite: another addressing of the table, u -> texel),
    literals ("random": a furniture shader's literal registers are drawn, not the capture rows')."""
    body, reference = CHECKS[shader] if shader in CHECKS else WATER_CHECKS[shader]
    rng, aux = random.Random(seed), random.Random(seed + 1)
    res = Result()
    for k in range(trials):
        res.trials += 1
        try:
            trial = body(text, shader, rng, aux, formula or reference, opts)
        except (NotImplementedError, KeyError, ValueError, ZeroDivisionError) as e:
            key = f"{type(e).__name__}: {str(e)[:60]}"
            res.errors[key] = res.errors.get(key, 0) + 1
            continue
        if len(trial.got) < len(trial.want):
            res.errors["fewer components than the formula"] = res.errors.get("fewer components than the formula", 0) + 1
            continue
        for word in trial.reached:
            res.reached[word] = res.reached.get(word, 0) + 1
        if trial.coords is not None:
            res.coords += 1
            res.coords_ok += bool(trial.coords)
        bad = False
        for a, b in zip(trial.got, trial.want):
            res.values += 1
            if not (math.isfinite(a) and math.isfinite(b)):
                bad = True
                continue
            err = abs(a - b) / max(1.0, abs(a), abs(b))
            res.worst = max(res.worst, err)
            if err <= tol:
                res.agree += 1
            else:
                bad = True
        if bad and res.first_bad is None:
            res.first_bad = (k, trial.got, trial.want)
    return res


def check(shader, trials=TRIALS, dumps=DUMPS, **kw):
    """One shader of light_formulas.POST_SHADERS or POST_FURNITURE_SHADERS, by hash, on its dump."""
    return check_text(shader, ue.load(dumps, shader, STAGES.get(shader, "frag")), trials, **kw)


WATER_BLUR_MUTATIONS = ("a tap's offset", "the weight", "the axes")


def water_blur_mutant(name):
    """A wrong form of light_formulas.water_blur7, by name (WATER_BLUR_MUTATIONS)."""
    def wrong(tex, u, v, offsets, weight):
        if name == "a tap's offset":
            offsets = [list(o) for o in offsets]
            offsets[3][0] += 1.0 / 256.0
        elif name == "the weight":
            weight = weight * 7.0 / 6.0
        else:
            offsets = [(b, a) for a, b in offsets]
        return lf.water_blur7(tex, u, v, offsets, weight)
    return wrong


def water_blur_report(trials=TRIALS, dumps=DUMPS, say=print):
    """The reflection views' blur against its microcode, and each wrong form: 0 when the formula agrees in every
    value and coordinate and every mutation fails."""
    shader = lf.WATER_BLUR_SHADER["ps"]
    res = check(shader, trials, dumps)
    say(f"ps {shader} water blur: {res.line()}")
    bad = not res.ok
    for name in WATER_BLUR_MUTATIONS:
        m = check(shader, trials, dumps, formula=water_blur_mutant(name))
        say(f"    mutation, {name}: {m.values - m.agree} of {m.values} values disagree: {'caught' if not m.ok else 'NOT CAUGHT'}")
        bad = bad or m.ok
    return 1 if bad else 0
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--shader", help="only this shader (16 hex digits)")
    ap.add_argument("--trials", type=int, default=TRIALS)
    ap.add_argument("--dumps", type=Path, default=DUMPS)
    ap.add_argument("--bias", type=float, default=0.0, help="the downsample: added to the pixel position (0 or 0.5)")
    ap.add_argument("--literals", choices=("captured", "random"), default="captured",
                    help="the furniture shaders' literal registers: the capture rows' (default) or drawn")
    ap.add_argument("--list", action="store_true", help="print what would be checked and exit")
    ap.add_argument("--water-blur", action="store_true", help="the reflection views' blur (F186) and its three mutations, alone")
    args = ap.parse_args(argv)
    if args.water_blur:
        return water_blur_report(args.trials, args.dumps)
    want = args.shader.upper().replace("0X", "") if args.shader else None
    shaders = [s for s in lf.POST_SHADERS if want in (None, s.hash)]
    furniture = [s for s in lf.POST_FURNITURE_SHADERS if want in (None, s.hash)]
    if args.list:
        for s in shaders:
            print(f"ps {s.hash} {s.role}: light_formulas.{CHECKS[s.hash][1].__name__}")
        for s in furniture:
            print(f"{_STAGE_WORD[s.stage]} {s.hash} {s.role}: light_formulas.{CHECKS[s.hash][1].__name__}")
        return 0
    if not args.dumps.is_dir():
        print(f"no shader dumps at {args.dumps}", file=sys.stderr)
        return 2
    if not shaders and not furniture:
        print("no shader matches", file=sys.stderr)
        return 2
    good = 0
    for s in shaders:
        res = check(s.hash, args.trials, args.dumps, bias=args.bias)
        print(f"ps {s.hash} {s.role:10s}: {res.line()}")
        good += res.ok
        if not res.ok and res.first_bad:
            k, got, wanted = res.first_bad
            print(f"    first disagreement, trial {k}: microcode {got} formula {wanted}")
    if shaders:
        print(f"{good} of {len(shaders)} post shaders agree in every value ({args.trials} trials each, 4 values a trial, "
              f"tolerance {TOLERANCE:g} relative)")
    fine = 0
    literals = None if args.literals == "captured" else args.literals
    for s in furniture:
        res = check(s.hash, args.trials, args.dumps, literals=literals)
        reached = "".join(f"; {word} {n}" for word, n in sorted(res.reached.items()))
        print(f"{_STAGE_WORD[s.stage]} {s.hash} {s.role:14s}: {res.line()}{reached}")
        fine += res.ok
        if not res.ok and res.first_bad:
            k, got, wanted = res.first_bad
            print(f"    first disagreement, trial {k}: microcode {got} formula {wanted}")
    if furniture:
        print(f"{fine} of {len(furniture)} furniture shaders agree in every value ({args.trials} trials each, literals "
              f"{'drawn at random' if literals else 'as captured'}, tolerance {TOLERANCE:g} relative)")
    return 0 if good == len(shaders) and fine == len(furniture) else 1


if __name__ == "__main__":
    sys.exit(main())
