"""Checks the closed light formulas of light_formulas.py against the shaders' microcode.

Usage: light_check.py --all [--trials N] [--seed S] [--dumps DIR]
       light_check.py --family <name> [--shader PS_HASH] [--trials N]
       light_check.py --list
       light_check.py --coverage <native_discovery_*.jsonl> [...]     (which share of a capture's
                      main-scene draws the checked pairs are; needs no dumps)

For every (pixel shader, vertex shader) pair of FAMILIES a trial draws random vertex inputs and
constants, runs the vertex shader's microcode (ucode_eval.Machine), hands its interpolators to the
pixel shader's microcode, and compares the pixel's colour with the family's formula evaluated on the
same inputs. Exit code 0 only when every trial of every pair agrees within 1e-4 relative.

What a trial switches off, and why (docs/native-renderer/lighting-formulas.md has it per family):
  - the normal maps (tf2, the side maps tf14) read flat (0.5, 0.5): the native pass lights with the
    vertex normal (spec ruling 4);
  - the colours of lights 1 to 3 are zero (ruling 5); light 0, the sun, keeps every constant;
  - the fog predicate is off (c67.x = 1e9; ruling 8): its constants and tables are then not read, and
    the trial does not supply them, so a read would be an error;
  - bools b133 (emissive tf13), b134 (cube map tf14), b135 (shadow map tf9) are clear: their
    constants are not supplied either;
  - the specular texture tf1 reads black (ruling 4);
  - trees: bool b129 (a fog volume) is clear and the vertex shader's fog is off (vs c111.x = 1e9).
The screen mask tf8 is NOT switched off: it is a random texel and part of `sun_gate`.

Literal constants (c252..c255) are not in the dump text. Terrain: the values of the shaders' own
literal tables, "ps_lit" / "vs_lit" of the tess rows of the captures (frame-map section 13). Mesh
vertex shaders: "vs_literals" of capture native_discovery_20261005_123635.jsonl (frame-map section 12,
"Literals"). Mesh pixel shaders: no capture row holds them (draw rows carry no "ps_lit"); the values
here are read from the idioms (2 and -1 of the normal decode, the 0 of a dp2add, the 1 of `1 - x`)
and marked so in PS_LITERALS.
"""
import argparse
import json
import math
import random
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import light_formulas as lf  # noqa: E402
import ucode_eval as ue  # noqa: E402

DUMPS = Path(__file__).resolve().parents[2] / "out" / "shader_dump"
TOLERANCE = 1e-4
NAN = float("nan")

# family -> [(pixel shader, vertex shader)]: the pairs the two captures of the materials validation draw
# in the main scene (task11_bridge_up2_capture, task11_trees_up1_capture) with these pixel shaders, but
# for two whose vertex shader's literal table is in no capture (7CD5... with D7DD... and 2D40...).
# `--coverage` lists what a capture draws outside this table.
FAMILIES = {
    "terrain-layer": [("9C2144E42E95A76F", "C30A97D946FA2BE4"), ("4755A2E4A05BA678", "FB68A7F2301210E1")],
    "terrain-layer-multi": [("45243B758F9272AB", "C30A97D946FA2BE4"), ("04F23292B2F44077", "C30A97D946FA2BE4"),
                            ("AF1E16EE57E4BB06", "FB68A7F2301210E1"), ("578342593A37DD35", "FB68A7F2301210E1")],
    "terrain-far": [("7DDAD5C29B3F56EE", "5003700B7C9B1C16"), ("2F8A0341E8C09698", "5003700B7C9B1C16")],
}

# Literal tables. Terrain: tess rows of both captures, every row of the shader the same.
VS_LITERALS = {
    "C30A97D946FA2BE4": {253: [0.5, 1 / 15, -1.0, 15 / 14], 254: [0, 1, 3, 2], 255: [NAN] * 4},
    "FB68A7F2301210E1": {253: [0.5, 1 / 15, -1.0, 15 / 14], 254: [0, 1, 3, 2], 255: [NAN] * 4},
    "5003700B7C9B1C16": {254: [0.5, -1.0, 0, 0], 255: [0, 1, 3, 2]},
}
PS_LITERALS = {
    "9C2144E42E95A76F": {254: [2, 0.99609375, 8, 0], 255: [0, -1, 1, 0.0001]},
    "45243B758F9272AB": {254: [8, 2, 0.99609375, 0], 255: [0, 1, -1, 0.0001]},
    "04F23292B2F44077": {254: [2, 0.99609375, 8, 0], 255: [0, 1, -1, 0.0001]},
    "4755A2E4A05BA678": {254: [2, 0, -1, 1], 255: [8, 0, 0, 0]},
    "AF1E16EE57E4BB06": {254: [0, 1, -1, 8], 255: [2, 0, 0, 0]},
    "578342593A37DD35": {254: [0, 1, -1, 2], 255: [8, 0, 0, 0]},
    "7DDAD5C29B3F56EE": {255: [1, 0, 0, 0]},
    "2F8A0341E8C09698": {255: [1, 0, 0, 0]},
}


# --- pieces every driver uses -------------------------------------------------------------------

def tex_seed(slot, coords):
    """A seed from (slot, coords) that is the same in every process (crc32 of the packed values)."""
    return zlib.crc32(struct.pack("<i", slot) + b"".join(struct.pack("<d", c) for c in coords))


def tex(slot, coords):
    """A fixed pseudo-random texture: the same (slot, coords) gives the same four values in (0.05, 1)."""
    rng = random.Random(tex_seed(slot, coords))
    return [rng.uniform(0.05, 1.0) for _ in range(4)]


def unit(rng):
    """A random unit vector (three floats)."""
    while True:
        v = [rng.uniform(-1, 1) for _ in range(3)]
        n2 = lf.dot(v, v)
        if 0.01 < n2 <= 1.0:
            return [x / math.sqrt(n2) for x in v]


class Case:
    """One trial: what the pixel shader's microcode gets (constants, interpolator registers, sampler,
    bools) and what the formula gets (`args`, by parameter name); the microcode's oC0.xyz is divided by
    `scale` (the output scale c20.w and whatever else lies outside the formula) before the comparison."""

    def __init__(self, consts, regs, sampler, args, scale, bools=()):
        self.consts, self.regs, self.sampler, self.args, self.scale, self.bools = consts, regs, sampler, args, scale, bools


class Result:
    def __init__(self):
        self.trials = 0
        self.agree = 0
        self.worst = 0.0        # largest relative difference of a finite comparison (see check)
        self.first_bad = None   # (trial number, microcode colour, formula colour) of the first disagreement

    @property
    def ok(self):
        return self.trials > 0 and self.agree == self.trials

    def line(self):
        return f"{self.agree}/{self.trials} agree, worst relative difference {self.worst:.2e}"


def check(ps_text, make_case, formula, trials, seed=1, tol=TOLERANCE):
    """Runs `trials` random cases of one pixel shader: microcode against formula(**case.args). A trial
    agrees when every channel differs by at most `tol` times the larger of the two magnitudes (with a
    floor of 1e-6 under that magnitude, so that two zeros agree) and both colours are finite."""
    rng = random.Random(seed)
    res = Result()
    for k in range(trials):
        case = make_case(rng)
        m = ue.Machine(ps_text, case.consts, case.sampler, regs={r: list(v) for r, v in case.regs.items()},
                       bools=case.bools)
        out = m.run()["oC0"]
        got = [out[i] / case.scale[i] for i in range(3)]
        want = formula(**case.args)
        res.trials += 1
        finite = all(math.isfinite(x) for x in got) and all(math.isfinite(x) for x in want)
        err = max(abs(g - w) / max(1e-6, abs(g), abs(w)) for g, w in zip(got, want)) if finite else math.inf
        if finite:
            res.worst = max(res.worst, err)
        if err <= tol:
            res.agree += 1
        elif res.first_bad is None:
            res.first_bad = (k, got, want)
    return res


def run_vs(text, consts, sampler=None, regs=None, vfetch=None, bools=(), loops=None):
    """A vertex shader's exports as pixel shader registers: interpolator oN is rN."""
    def no_texture(slot, coords, attrs):
        raise KeyError(f"vertex texture tf{slot}")
    m = ue.Machine(text, consts, sampler or no_texture, regs=regs, bools=bools, vfetch=vfetch, loops=loops)
    out = m.run()
    regs = {int(k[1:]): list(v) for k, v in out.items() if k[0] == "o" and k[1:].isdigit()}
    return regs, out, m


def _flat_or_tex(slot, coords, attrs, mask):
    """The pixel sampler of every driver: normal maps flat, the screen mask the trial's texel (in the
    component the shaders read, x; the others differ from it), everything else the fixed pseudo-random
    texture."""
    if slot in (2, 14):
        return [0.5, 0.5, 0.5, 0.5]
    if slot == 8:
        return [mask, 1.0 - 0.5 * mask, 2.0 + mask, -1.0 - mask]
    return tex(slot, coords)


def _light_block(rng):
    """Random values of the frame constants every family reads (pixel bank)."""
    return {
        0: [rng.uniform(0.0005, 0.002), rng.uniform(0.0005, 0.002), rng.uniform(0.0005, 0.002), rng.uniform(0.0005, 0.002)],
        19: [rng.uniform(-50, 50) for _ in range(3)] + [1.0],
        20: [rng.uniform(0.0, 0.2) for _ in range(3)] + [rng.choice([1.0, 16.0])],
        21: [rng.uniform(0.5, 2.0) for _ in range(3)] + [rng.choice([1.0, 1.3, 2.0])],
        22: [rng.uniform(0.02, 0.5) for _ in range(3)] + [0.0],
        67: [1e9, 0.0, 0.01, 0.01],     # fog predicate off: distance - c67.x <= 0
        80: [rng.uniform(-1, 1) for _ in range(3)] + [0.0],
        87: [rng.uniform(0.2, 2.0) for _ in range(3)] + [0.0],
    }


def _extra_lights(rng, c):
    """The constants of the two- and four-light variants: light 0 random, colours 1 to 3 zero."""
    c[80] = [rng.uniform(-40, 40) for _ in range(3)] + [rng.choice([0.0, rng.uniform(0.2, 1.0)])]
    for k in (81, 82, 83):
        c[k] = [rng.uniform(-40, 40) for _ in range(3)] + [rng.uniform(0.0, 1.0)]
    for k in (88, 89, 90):
        c[k] = [0.0] * 4
    c[94] = [rng.choice([0.0, 1.0, rng.random()]) for _ in range(4)]
    c[96] = [rng.uniform(0.0, 0.02) for _ in range(4)]
    for k in (98, 99, 100, 101):
        c[k] = unit(rng) + [0.0]
    c[105] = [rng.uniform(-0.5, 0.5) for _ in range(4)]
    c[107] = [rng.uniform(0.5, 3.0) for _ in range(4)]
    c[112] = [rng.random() for _ in range(4)]


def _light0(c, normal, world):
    """n_dot_sun and the cone and range inputs of light 0 of a multi-light shader."""
    v = [c[80][i] - world[i] * c[80][3] for i in range(3)]
    d = math.sqrt(lf.dot(v, v))
    l0 = lf.normalize(v)
    return lf.sat(lf.dot(normal, l0)), dict(spot_blend=c[94][0], distance=d, range_scale=c[96][0],
                                             cone_dot=lf.dot(l0, c[98][:3]), cone_cos=c[105][0], cone_scale=c[107][0])


# --- terrain ------------------------------------------------------------------------------------

# Which interpolator of a layer vertex shader holds what (frame-map section 13, "Vertex shaders").
_LAYER_VS = {
    "C30A97D946FA2BE4": dict(side0=0, side1=1, detail=2, normal=3, world=4, base=5),      # the axis: o8
    "FB68A7F2301210E1": dict(detail=0, normal=1, world=2, base=3),                        # the axis: o6
}
# The steep-ground block of the three shaders paired with C30A: (threshold literal, small literal).
_STEEP = {"9C2144E42E95A76F": ((254, 1), (255, 3)), "45243B758F9272AB": ((254, 2), (255, 3)),
          "04F23292B2F44077": ((254, 1), (255, 3))}
# The pixel shader's literal 8 of sat(8 * (N . c109)).
_FACING = {"9C2144E42E95A76F": (254, 2), "4755A2E4A05BA678": (255, 0), "45243B758F9272AB": (254, 0),
           "AF1E16EE57E4BB06": (254, 3), "04F23292B2F44077": (254, 2), "578342593A37DD35": (255, 0)}


def _layer_vertex(vs, text, rng):
    """One random patch point through a layer vertex shader (the setup of vs_check.py, never culled).
    Returns (pixel registers, the tf17 normal as the shader decodes it, vertex constants)."""
    c = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3, 4, 5, 6)}
    c[19] = [rng.uniform(-1, 1) for _ in range(3)] + [rng.uniform(-1, 1)]
    c[8] = [rng.choice([0.125, 0.0625]), 0.125, 1, 0.125]
    c[11] = [2, 0.5, 0.25, 0]
    c[14] = [1 / 384, 1 / 384, 1, 1]
    c[46] = [8, 8, 89.739, 1]
    c[47] = [1 / 384, 1 / 384, 0, 0]
    c[113] = [rng.choice([80, 96, 112]), rng.choice([128, 144]), 0, 0]
    c[114] = [-1.0, 0, 0, 0]
    c[115] = [16 / 1650, 16 / 1815, (33 * rng.randrange(50) + 0.5) / 1650, (33 * rng.randrange(55) + 0.5) / 1815]
    c.update(VS_LITERALS[vs])
    nx, ny = rng.uniform(0.02, 0.98), rng.uniform(0.02, 0.98)   # one trial in six lies outside the unit disk
    height, weight = rng.random(), rng.randrange(1, 16) / 15

    def sampler(slot, coords, attrs):
        return {16: [height, 0, 0, 0], 17: [nx, ny, 0, 0], 18: [0.0, 0.0, 0.0, weight]}[slot]

    regs, _, _ = run_vs(text, c, sampler, regs={0: [rng.randrange(4), rng.random(), rng.random(), 0], 1: [0, 0, 0, 0]})
    x, y = 2 * nx - 1, 2 * ny - 1
    return regs, [x, y, math.sqrt(abs(1 - x * x - y * y))], c


def _terrain_layer_driver(ps, vs, dumps, multi):
    vs_text, slots = ue.load(dumps, vs, "vert"), _LAYER_VS[vs]

    def make_case(rng):
        regs, n_vs, vc = _layer_vertex(vs, vs_text, rng)
        normal = lf.normalize(n_vs)
        world = regs[slots["world"]][:3]
        regs[max(regs) + 1] = [rng.uniform(0, 1120), rng.uniform(0, 720), 0, 0]      # the pixel position
        c = _light_block(rng)
        c[11] = [rng.uniform(0.3, 1.0) for _ in range(4)]
        c[31] = [rng.uniform(0.0002, 0.001), rng.uniform(-1.5, 0.2), 0, 0]
        c[109] = [rng.uniform(-0.3, 0.3) for _ in range(3)] + [0.0]
        c.update({k: list(v) for k, v in PS_LITERALS[ps].items()})
        if multi:
            _extra_lights(rng, c)
        mask = rng.random()
        albedo = [x * x for x in tex(0, tuple(regs[slots["detail"]][:2]))[:3]]
        if ps in _STEEP:
            # A = s * tf0^2 + (1 - s) * (wy * tf13(o1)^2 + wx * tf13(o0)^2) where s is below the threshold
            # (frame-map section 13, item 1); half of the trials are steep.
            (tr, tc), (er, ec) = _STEEP[ps]
            c[45] = [rng.uniform(-0.5, 0.2), rng.uniform(0.2, 0.9), 0, 0] if rng.random() < 0.5 else [1.0, 1.0, 0, 0]
            s = lf.sat(abs(n_vs[2]) * c[45][1] + c[45][0])
            if s < c[tr][tc]:
                e0, e1 = tex(13, tuple(regs[slots["side0"]][:2])), tex(13, tuple(regs[slots["side1"]][:2]))
                den = abs(n_vs[0]) + abs(n_vs[1]) + c[er][ec]
                wx, wy = abs(n_vs[0]) / den, abs(n_vs[1]) / den
                albedo = [a * s + (1 - s) * (wy * b * b + wx * d * d) for a, b, d in zip(albedo, e1[:3], e0[:3])]
        fr, fc = _FACING[ps]
        facing = lf.facing_gate(lf.dot(normal, c[109][:3]), c[fr][fc])
        if multi:
            n_dot_sun, cone = _light0(c, normal, world)
            gate = lf.sun_gate_multi(c[112][0], mask=mask, facing=facing, **cone)
        else:
            n_dot_sun = lf.sat(lf.dot(normal, c[80][:3]))
            gate = lf.sun_gate_single(mask=mask, facing=facing)
        args = dict(A=albedo, baked=tex(3, tuple(regs[slots["base"]][:2]))[:3], tint=c[21][:3], tint_pow=c[21][3],
                    add=c[20][:3], floor=c[22][:3], t=lf.ambient_t(lf.ambient_axis(n_vs, vc[19][:3]), n_vs),
                    n_dot_sun=n_dot_sun, sun_col=c[87][:3], c11x=c[11][0], sun_gate=gate)
        return Case(c, regs, lambda slot, coords, attrs: _flat_or_tex(slot, coords, attrs, mask), args, [c[20][3]] * 3)

    return ue.load(dumps, ps, "frag"), make_case


def _terrain_far_driver(ps, vs, dumps):
    vs_text = ue.load(dumps, vs, "vert")

    def make_case(rng):
        vc = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3)}
        cols = rng.choice([1, 2, 3, 6])
        vc[8] = [rng.choice([8, 32]), rng.choice([8, 32]), 89.739, 1]
        vc[11] = [cols, 1 / cols, 0.5 / cols, 0]
        vc[14] = [1 / 384, 1 / 300, 1, 1]
        vc[46] = [1 / 384, 1 / 128, rng.choice([0, 32.0]), rng.choice([0, 448.0])]
        vc[47] = [rng.choice([96, 144]), rng.choice([96, 192]), 0, 0]
        vc[72] = [16 / 17, 0.5 / 17, 0, 0]
        vc[113] = [rng.choice([0, 3, 19]), 0, 0, 0]
        vc[114] = [1 / 48, 1 / 48, 0, 0]
        vc[115] = [rng.randrange(40), rng.randrange(40), 0, 0]
        vc.update(VS_LITERALS[vs])
        nx, ny = rng.uniform(0.02, 0.98), rng.uniform(0.02, 0.98)
        texels = {16: [rng.random(), 0, 0, 0], 17: [nx, ny, 0, 0], 18: [rng.random(), 0.9, 0.8, 0.7],
                  19: [rng.random(), 0.6, 0.5, 0.4]}
        regs, _, _ = run_vs(vs_text, vc, lambda slot, coords, attrs: texels[slot],
                            regs={0: [rng.randrange(6), rng.random(), rng.random(), 0], 1: [0, 0, 0, 0]})
        x, y = 2 * nx - 1, 2 * ny - 1
        normal = lf.normalize([x, y, math.sqrt(abs(1 - x * x - y * y))])
        c = _light_block(rng)
        c[11] = [rng.uniform(0.3, 1.0) for _ in range(4)]
        c[23] = [rng.uniform(-1, 1) for _ in range(3)] + [4.0]
        c[31] = [rng.uniform(0.0002, 0.001), rng.uniform(0.3, 0.6), 0, 0]
        c.update({k: list(v) for k, v in PS_LITERALS[ps].items()})
        fade = 1.0
        if ps.startswith("2F8A"):       # the underlay's own fade, outside the formula
            d2 = sum((e - w) ** 2 for e, w in zip(c[19][:3], regs[1][:3]))
            fade = lf.sat(d2 * c[31][0] + c[31][1])
        args = dict(F=[v * v for v in tex(13, tuple(regs[0][:2]))[:3]], baked=tex(3, tuple(regs[0][2:]))[:3],
                    tint=c[21][:3], tint_pow=c[21][3], add=c[20][:3], floor=c[22][:3],
                    t=lf.sat(lf.dot(normal, c[23][:3])), n_dot_sun=lf.sat(lf.dot(normal, c[80][:3])),
                    sun_col=c[87][:3], c11x=c[11][0])
        return Case(c, regs, lambda slot, coords, attrs: tex(slot, coords), args, [c[20][3] * fade] * 3)

    return ue.load(dumps, ps, "frag"), make_case


# --- mesh vertices ------------------------------------------------------------------------------

class Vertex:
    """Random vertex data for a mesh vertex shader: the fetch callback makes each element once, by its
    format, per (stream, dword offset, index), so the four bone fetches of a skinned shader get four
    matrices; `elements[(stream, offset)]` is the first element fetched there, for the driver to read
    what the shader was given. The fourth component of a 16_16_16_16_FLOAT position (as fetched: its
    z) is the a0 index."""

    def __init__(self, rng, a0=(0, 4, 8)):
        self.rng, self.a0, self.elements, self.by_index = rng, a0, {}, {}     # a0=None: an ordinary fourth component

    def __call__(self, stream, offset, attrs, index):
        key = (stream, offset, index)
        if key not in self.by_index:
            self.by_index[key] = self._make(stream, attrs)
            self.elements.setdefault((stream, offset), self.by_index[key])
        return self.by_index[key]

    def _make(self, stream, attrs):
        rng, fmt = self.rng, attrs.get("DataFormat")
        if fmt == "FMT_16_16_16_16_FLOAT":
            v = [rng.uniform(-2, 2) for _ in range(4)]
            if stream == 0 and self.a0:
                v[2] = float(rng.choice(self.a0))
            return v
        if fmt == "FMT_10_11_11":
            return unit(rng) + [0.0]
        if fmt == "FMT_2_10_10_10":
            return unit(rng) + [rng.choice([-1.0, 1.0])]
        if fmt == "FMT_8_8_8_8":
            if attrs.get("NumFormat") == "integer":
                return [float(rng.randrange(40)) for _ in range(4)]
            return [rng.randrange(256) / 255 for _ in range(4)]
        if fmt in ("FMT_16_16_FLOAT", "FMT_32_32_32_32_FLOAT", "FMT_32_32_FLOAT", "FMT_32_FLOAT", "FMT_32_32_32_FLOAT",
                   "FMT_16_16_16_16", "FMT_16_16"):
            return [rng.uniform(-1, 1) for _ in range(4)]
        raise NotImplementedError(f"vertex format {fmt}")


def _swizzled(sample, dest_swizzle):
    """A fetched element as the destination register receives it (for the formulas' inputs)."""
    return [0.0 if ch in "_0" else 1.0 if ch == "1" else sample["xyzw".index(ch)] for ch in dest_swizzle]


# --- objects ------------------------------------------------------------------------------------

# The vertex shaders the lit-object pixel shaders are paired with. "colour": o6 from the colour stream
# vf3 (power literal c255.x); "probe": o6 from the weights of stream vf1 and constants c[28..31 + a0].
# "normal": (stream, offset) of the 10_11_11 element when o1 is the plain c4..c6 product (the skinned
# shaders orthogonalise theirs against the tangent: their o1 is taken from the microcode's export).
_OBJECT_VS = {
    "ECD66A10092E6562": dict(kind="colour", normal=(0, 2), literals={255: [2.2, 0.5, 1.0, 0.0]}),
    "79EAC49585797037": dict(kind="probe", normal=(0, 2), literals={255: [0.0, 1.0, 0.5, 0.0]}),
    "D4D558DA6A82BDC8": dict(kind="probe", normal=None, literals={255: [0.0, 1.0, 0.5, 0.0]}),
    "A1F7E9885EC466DF": dict(kind="probe", normal=None, literals={255: [0.0, 1.0, 0.5, 0.0]}),
    # Tree trunks: the object form with wind on the position and fog in the vertex shader (off here).
    "475EC9F795E5EDBB": dict(kind="probe", normal=(0, 2), literals=dict(zip(range(248, 256), (
        [0.0] * 4, [0.0] * 4, [0.0] * 4, [0.5, 0.7, 0.0, 0.0], [0.035, 0.472, 2 * math.pi, 0.25],
        [0.04, 0.025, 0.0, 1.0], [0.23, 0.11, -math.pi, 0.0], [0.3849, 0.3, 0.019245, 0.1])))),
}
# Pixel shaders that divide the colour by its alpha squared without asking bool b128.
_UNPREMULTIPLY = ("00E09D1BC5295D52", "58D3FAAAA84A48AA", "2FBE9B4EE822609A")
# Mesh pixel shader literals, read from the idioms (no capture row holds them). Only the registers a
# trial can reach are given: the cube block's 1.5 is not.
PS_LITERALS.update({
    "8D900846800943C8": {254: [2, 0, -1, 1]},
    "2FD24989AB04A475": {254: [2, 0, -1, 1]},
    "648B965C200A5113": {254: [0, 1, -1, NAN], 255: [2, NAN, NAN, NAN]},
    "789266C3E42D7E59": {254: [0, 1, -1, NAN], 255: [2, NAN, NAN, NAN]},
    "00E09D1BC5295D52": {254: [1, 0, -1, NAN], 255: [2, NAN, NAN, NAN]},
    "58D3FAAAA84A48AA": {254: [1, 0, -1, NAN], 255: [2, NAN, NAN, NAN]},
    "2FBE9B4EE822609A": {254: [1, 0, -1, NAN], 255: [2, NAN, NAN, NAN]},
})


def _tree_wind(rng, c):
    """What the tree vertex shaders read besides light: the eye, wind (c15..c17: the position only) and
    the vertex fog, switched off (distance - c111.x <= 0, so its tables tf16 / tf17 are not fetched)."""
    c[8] = [rng.uniform(0.5, 2.0), 0.0, 0.0, 0.0]
    c[9] = [rng.uniform(-50, 50) for _ in range(3)] + [1.0]
    for k in (15, 16, 17):
        c[k] = [rng.uniform(-0.5, 0.5) for _ in range(4)]
    c[110] = unit(rng) + [0.0]
    c[111] = [1e9, 0.0, 0.01, 0.01]


def _object_vertex(vs, text, rng):
    """One random vertex through an object vertex shader. Returns (pixel registers, the normal the pixel
    shader lights with before normalising, the colour o6 by the closed formula, vertex constants)."""
    info = _OBJECT_VS[vs]
    c = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3, 4, 5, 6)}
    c[7] = [rng.uniform(-50, 50) for _ in range(3)] + [rng.uniform(0.5, 2.0)]
    c[19] = [rng.uniform(-1, 1) for _ in range(4)]
    c[20] = [float(rng.randrange(4)), 0.0, 0.0, 0.0]
    for k in range(28, 40):
        c[k] = [rng.uniform(-0.5, 0.5) for _ in range(4)]
    _tree_wind(rng, c)
    c.update(info["literals"])
    vertex = Vertex(rng)
    regs, out, m = run_vs(text, c, regs={0: [float(rng.randrange(1000)), 0, 0, 0]}, vfetch=vertex,
                          bools=rng.choice([(), (0,)]))
    if info["normal"]:
        n = vertex.elements[info["normal"]]
        normal = [lf.dot(c[r][:3], n[:3]) * c[7][3] for r in (4, 5, 6)]
    else:
        normal = regs[1][:3]
    if info["kind"] == "colour":
        fetch = next(i for i in ue.st.parse(text) if i.op == "vfetch_full" and i.fetch_slot == 3)
        colour = lf.vertex_colour(_swizzled(vertex.elements[(3, 0)], fetch.dest_swz), c[255][0])[:3]
        # The colour stream is read at the vertex index plus vs c20.x.
        index = next(v[3] for v in m.vfetches if v[1] == 3)
        if index != m.vfetches[0][3] + c[20][0]:
            raise AssertionError(f"{vs}: colour stream index {index}")
    else:
        a0 = int(vertex.elements[(0, 0)][2])
        colour = lf.probe_colour(c[28 + a0][:3], [c[29 + a0], c[30 + a0], c[31 + a0]], vertex.elements[(1, 0)])
    return regs, normal, colour, c


def _object_driver(ps, vs, dumps, multi):
    vs_text = ue.load(dumps, vs, "vert")

    def make_case(rng):
        regs, n_vs, vcolour, vc = _object_vertex(vs, vs_text, rng)
        normal = lf.normalize(n_vs)
        regs[max(regs) + 1] = [rng.uniform(0, 1120), rng.uniform(0, 720), 0, 0]
        c = _light_block(rng)
        for k in (8, 9, 10):
            c[k] = [rng.uniform(0.5, 2.0), rng.uniform(0.5, 2.0), rng.random(), rng.random()]
        c[23] = [rng.uniform(-1, 1) for _ in range(3)] + [rng.uniform(0.0, 4.0)]
        if ps == "2FD24989AB04A475":
            # Its specular exponent is lerp(c24.x, c24.y, tf1.a), computed outside any bool block (the
            # other three use a fixed fourth power); the specular term itself is off with tf1 black.
            c[24] = [rng.uniform(1.0, 5.0), rng.uniform(10.0, 100.0), 1.0, 0.0]
        c[15] = [0.0, 0.0, 0.0, rng.uniform(0.5, 1.0)]             # trees: an alpha factor only
        c[26] = [rng.uniform(0.5, 1.0) for _ in range(4)]
        c[27] = [rng.uniform(0.5, 1.0) for _ in range(4)]
        c[49] = [rng.uniform(0.5, 1.5) for _ in range(4)]
        c.update({k: list(v) for k, v in PS_LITERALS[ps].items()})
        if multi:
            _extra_lights(rng, c)
        mask = rng.random()
        texel = [rng.uniform(0.05, 1.0) for _ in range(4)]         # tf0: colour and alpha
        gloss = rng.random()
        # b128 (objects): the colour is divided by its alpha squared. b132 (trees): the colour is
        # multiplied by interpolator o6 and o7 is added, the vertex shader's fog, which is 1 and 0 while
        # the vertex fog is off, so the bool makes no difference here. b129 (trees) stays clear.
        bools = rng.choice([(), (128,), (132,), (128, 132)])

        def sampler(slot, coords, attrs):
            if slot == 0:
                return texel
            if slot == 1:
                return [0.0, 0.0, 0.0, gloss]                       # specular colour black: no specular
            return _flat_or_tex(slot, coords, attrs, mask)

        albedo = [x * x for x in texel[:3]]
        if 128 in bools or ps in _UNPREMULTIPLY:                    # colour stored times alpha
            albedo = [x / (texel[3] * texel[3]) for x in albedo]
        if multi:
            n_dot_sun, cone = _light0(c, normal, regs[2][:3])
            gate = lf.sun_gate_multi(c[112][0], mask=mask, **cone)
        else:
            n_dot_sun = lf.sat(lf.dot(normal, c[80][:3]))
            gate = lf.sun_gate_single(mask=mask)
        args = dict(A=albedo, vcolour=vcolour, tint=c[21][:3], tint_pow=c[21][3], add=c[20][:3], floor=c[22][:3],
                    t=lf.ambient_t(lf.ambient_axis(n_vs, vc[19][:3]), n_vs), n_dot_sun=n_dot_sun, sun_col=c[87][:3],
                    c27w=c[27][3], sun_gate=gate, c49=c[49][:3])
        return Case(c, regs, sampler, args, [c[20][3] * c[26][i] for i in range(3)], bools)

    return ue.load(dumps, ps, "frag"), make_case


FAMILIES.update({
    "object": [("8D900846800943C8", "ECD66A10092E6562"), ("8D900846800943C8", "79EAC49585797037"),
               ("8D900846800943C8", "D4D558DA6A82BDC8"), ("8D900846800943C8", "A1F7E9885EC466DF"),
               ("2FD24989AB04A475", "79EAC49585797037")],
    "object-multi": [("648B965C200A5113", "ECD66A10092E6562"), ("648B965C200A5113", "79EAC49585797037"),
                     ("789266C3E42D7E59", "ECD66A10092E6562")],
})


# --- tree leaves --------------------------------------------------------------------------------

# Vertex shader A584...: "vs_literals" of capture native_discovery_20261005_123635.jsonl, registers 248
# to 255. The two- and four-light variants D7DD... and 2D40... are in no capture with literals and are
# not run.
_LEAF_VS_LITERALS = dict(zip(range(248, 256), (
    [0.0] * 4, [-math.pi, 0.7, 100.0, 0.45454543828964233], [-0.3, 0.05, 0.25, 1.0], [2 * math.pi, 0.25, 0.11, 0.23],
    [0.04, 0.025, 0.0, 1.0], [0.035, 0.472, 0.5, 1.0], [0.3849, 0.3, 0.019245, 0.2886750102043152],
    [0.55, 0.9, 0.15, 0.1])))
_LEAF_GAMMA = (249, 3)      # the literal 1 / 2.2 on the ambient exponent


def _leaf_driver(ps, vs, dumps):
    vs_text = ue.load(dumps, vs, "vert")

    def make_case(rng):
        vc = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3, 4, 5, 6)}
        vc[7] = [rng.uniform(-50, 50) for _ in range(3)] + [rng.uniform(0.5, 2.0)]
        _tree_wind(rng, vc)
        vc[18] = [rng.uniform(0.5, 2.0) for _ in range(3)] + [rng.choice([2.86, 2.2, 1.3])]
        for k in range(28, 40):
            vc[k] = [rng.uniform(-0.5, 0.5) for _ in range(4)]
        vc[50] = [0.0, 0.0, 0.0, rng.uniform(0.0, 1.0)]
        vc[54] = [rng.uniform(0.0, 0.2) for _ in range(3)] + [0.0]
        vc[76] = [rng.uniform(-40, 40) for _ in range(3)] + [rng.choice([0.0, rng.uniform(0.2, 1.0)])]
        vc[83] = [rng.uniform(0.2, 2.0) for _ in range(3)] + [0.0]
        vc[90] = [rng.choice([0.0, 1.0, rng.random()]), 0.0, 0.0, 0.0]
        vc[92] = [rng.uniform(0.0, 0.02), 0.0, 0.0, 0.0]
        vc[94] = unit(rng) + [0.0]
        vc[101] = [rng.uniform(-0.5, 0.5), 0.0, 0.0, 0.0]
        vc[103] = [rng.uniform(0.5, 3.0), 0.0, 0.0, 0.0]
        vc.update(_LEAF_VS_LITERALS)
        vertex = Vertex(rng)
        regs, _, _ = run_vs(vs_text, vc, regs={0: [float(rng.randrange(1000)), 0, 0, 0]}, vfetch=vertex)
        e = vertex.elements
        world = regs[0][:3]                                     # o0: the swayed world position
        normal = [lf.dot(vc[r][:3], e[(0, 2)][:3]) * vc[7][3] for r in (4, 5, 6)]
        view = lf.normalize([vc[9][i] - world[i] for i in range(3)])
        to_light = [vc[76][i] - world[i] * vc[76][3] for i in range(3)]
        light_dir = lf.normalize(to_light)
        rng_ = lf.sat(1.0 - math.sqrt(lf.dot(to_light, to_light)) * vc[92][0])
        cone = lf.sat((lf.dot(light_dir, vc[94][:3]) - vc[101][0]) * vc[103][0])
        front, back = lf.leaf_light(normal, view, light_dir, vc[50][3], 1.0 + vc[90][0] * (cone * rng_ - 1.0))
        a0 = int(e[(0, 0)][2])
        probe = lf.probe_colour(vc[28 + a0][:3], [vc[29 + a0], vc[30 + a0], vc[31 + a0]], e[(1, 0)])
        gr, gc = _LEAF_GAMMA
        ambient = lf.instance_ambient(probe, vc[18][:3], vc[18][3] * vc[gr][gc], vc[54][:3])

        regs[max(regs) + 1] = [rng.uniform(0, 1120), rng.uniform(0, 720), 0, 0]
        c = {0: [rng.uniform(0.0005, 0.002) for _ in range(4)],
             8: [rng.uniform(0.5, 2.0), rng.uniform(0.5, 2.0), rng.random(), rng.random()],
             15: [0.0, 0.0, 0.0, rng.uniform(0.5, 1.0)],
             20: [0.0, 0.0, 0.0, rng.choice([1.0, 16.0])],
             26: [rng.uniform(0.5, 1.0) for _ in range(4)],
             27: [rng.uniform(0.5, 1.0) for _ in range(4)],
             49: [rng.uniform(0.5, 1.5) for _ in range(4)]}
        # The pixel literals c255.w and c255.z shape the mask. They are 0.6 and 0.4 (frame-map section
        # 14, "The shaders' literals": known since plan Task 1, after this check was written); a trial
        # still draws them at random and the formula is given the two factors they make, so the form
        # is checked for any values.
        gain, bias = rng.uniform(0.25, 2.0), rng.uniform(-1.0, 0.75)
        c[255] = [1.0, 0.0, bias, gain]
        mask = rng.random()
        texel = [rng.uniform(0.05, 1.0) for _ in range(4)]

        def sampler(slot, coords, attrs):
            return texel if slot == 0 else _flat_or_tex(slot, coords, attrs, mask)

        m1, m2 = lf.leaf_mask(mask, gain, bias)
        args = dict(A=[x * x / (texel[3] * texel[3]) for x in texel[:3]], vs_ambient=ambient, front=front, back=back,
                    sun_col=vc[83][:3], c27w=c[27][3], m1=m1, m2=m2, c49=c[49][:3])
        # b132: the colour times o4 plus o5, the vertex fog (1 and 0 while it is off). b129 stays clear.
        return Case(c, regs, sampler, args, [c[20][3] * c[26][i] for i in range(3)], rng.choice([(), (132,)]))

    return ue.load(dumps, ps, "frag"), make_case


FAMILIES.update({
    "tree-trunk": [("00E09D1BC5295D52", "475EC9F795E5EDBB")],
    "tree-trunk-multi": [("58D3FAAAA84A48AA", "475EC9F795E5EDBB"), ("2FBE9B4EE822609A", "475EC9F795E5EDBB")],
    "tree-leaf": [("7CD57B81550F19E3", "A5846836C90E1192")],
})


# --- instanced plants ---------------------------------------------------------------------------

# Vertex shader literals: "vs_literals" of capture native_discovery_20261005_123635.jsonl.
_PLANT_VS = {
    "8123C16DBF583F92": {253: [-math.pi, 2 * math.pi, 1 / (2 * math.pi), 0.0], 254: [0.0, 1.0, 0.5, 0.1],
                         255: [NAN, NAN, NAN, 5.0]},
    "B636821F95DC9D8E": {253: [-math.pi, 5.0, 1 / (2 * math.pi), 0.0], 254: [0.0, 1.0, 2 * math.pi, 0.5],
                         255: [NAN, NAN, NAN, 0.1]},
    "6AD4108C3FF07966": {254: [0.0, 1.0, 0.5, 0.0], 255: [NAN, NAN, NAN, 0.0]},
    "48D30ECCF684F488": {253: [-math.pi, 4.0, 2 * math.pi, 1 / (2 * math.pi)], 254: [0.0, 1.0, 0.5, 0.1],
                         255: [NAN, NAN, NAN, 5.0]},
    "FC4F2EF6930768BE": {253: [-math.pi, 4.0, 2 * math.pi, 1 / (2 * math.pi)], 254: [0.0, 1.0, 0.5, 0.1],
                         255: [NAN, NAN, NAN, 5.0]},
    "36B543CECAE5C781": {254: [0.0, 1.0, 4.0, 0.5], 255: [NAN, NAN, NAN, 0.0]},
    "33C00226C154C1F2": {253: [-math.pi, 4.0, 1 / (2 * math.pi), 2 * math.pi], 254: [0.0, 1.0, 0.5, 0.1],
                         255: [NAN, NAN, NAN, 5.0]},
}
# Four of them push the plant away from points (vs c56 onwards, xyz and radius, four per iteration) in a loop over
# integer constant i0: positions only. A trial gives the loop 0 to 4 iterations.
_PLANT_LOOP = ("48D30ECCF684F488", "FC4F2EF6930768BE", "36B543CECAE5C781", "33C00226C154C1F2")
PS_LITERALS.update({     # by idiom, as for the objects
    "A7E45D07F5CF6627": {255: [1, NAN, NAN, NAN]},
    "26052DEFBA688AF2": {},
    "E99F4ACC7A3C78B4": {255: [0, 1, -1, NAN]},
    "165EDCD2CB963868": {255: [0, 1, -1, 2]},
    "F525E023E08AC9BB": {255: [0, 1, -1, 2]},
})


def _real(element):
    """A 16_16_16_16_FLOAT element in the order the shaders use it: the fetch swizzle `yxwz` (frame-map
    section 9, "Position element") undoes the 16-bit pair swap, so real (x, y, z, w) = fetched (y, x, w, z)."""
    return [element[1], element[0], element[3], element[2]]


def _plant_vertex(vs, text, rng):
    """One random vertex of one random instance through a plant vertex shader. Returns (pixel registers,
    instance-rotated unit normal, unit up axis, height blend, instance colour, vertex constants), the four
    middle ones by their closed formulas from the fetched elements."""
    c = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3)}
    c[7] = [rng.uniform(-50, 50) for _ in range(3)] + [0.0]
    c[9] = [rng.uniform(-50, 50) for _ in range(3)] + [0.0]
    c[10] = [rng.uniform(0.0, 2.0), 0.0, 0.0, 0.0]
    verts = rng.choice([4, 6, 86])
    c[12] = [1.0 / verts, float(verts), float(rng.randrange(8)), 0.0]
    c[13] = [0.0, 0.0, 1e12, 0.0]                               # never beyond the instancing cut
    c[18] = [rng.uniform(0.5, 2.0) for _ in range(3)] + [rng.choice([1.3, 2.86, 2.0])]
    c[19] = [rng.uniform(-1, 1) for _ in range(4)]
    for k in (21, 22, 25, 26):                                  # wind: moves the position, not the light
        c[k] = [rng.uniform(-0.3, 0.3) for _ in range(4)]
    c[45] = [rng.uniform(0.3, 1.5), rng.uniform(0.2, 1.0), rng.uniform(-0.3, 0.3), 0.0]
    c[54] = [rng.uniform(0.0, 0.2) for _ in range(3)] + [0.0]
    c[49] = [rng.uniform(0.1, 1.0) for _ in range(4)]
    for k in range(56, 72):
        c[k] = [rng.uniform(-50, 50) for _ in range(3)] + [rng.uniform(0.5, 3.0)]
    c.update(_PLANT_VS[vs])
    vertex = Vertex(rng, a0=None)
    index = float(rng.randrange(10 * verts))
    loops = {0: rng.randrange(5)} if vs in _PLANT_LOOP else None
    regs, _, _ = run_vs(text, c, regs={0: [index, 0, 0, 0]}, vfetch=vertex, loops=loops)
    e = vertex.elements
    rows = [_real(e[(1, 0)]), _real(e[(1, 2)]), _real(e[(1, 4)])]
    n = _real(e[(0, 2)])[:3]
    normal = lf.normalize([lf.dot(row[:3], n) for row in rows])
    up = lf.normalize([row[2] for row in rows])
    height = lf.sat(_real(e[(0, 0)])[2] * c[45][1] + c[45][2])
    colour = [e[(1, 6)][2], e[(1, 6)][1], e[(1, 6)][0]]
    return regs, normal, up, height, colour, c


def _plant_driver(ps, vs, dumps, multi, lit):
    vs_text = ue.load(dumps, vs, "vert")

    def make_case(rng):
        regs, normal, up, height, colour, vc = _plant_vertex(vs, vs_text, rng)
        n_lit = lf.plant_normal(normal, up, height, vc[45][0])
        regs[max(regs) + 1] = [rng.uniform(0, 1120), rng.uniform(0, 720), 0, 0]
        c = _light_block(rng)
        for k in (8, 9, 10):
            c[k] = [rng.uniform(0.5, 2.0), rng.uniform(0.5, 2.0), rng.random(), rng.random()]
        c[15] = [62.4, 0.625, 62.4, 0.0]                        # the dissolve: alpha only
        c[23] = [rng.uniform(-1, 1) for _ in range(3)] + [rng.uniform(0.0, 4.0)]
        c[50] = [rng.uniform(0.01, 0.1), rng.uniform(0.01, 0.1), 0.0, 0.0]
        c.update({k: list(v) for k, v in PS_LITERALS[ps].items()})
        if multi:
            _extra_lights(rng, c)
        mask = rng.random()
        texel = [rng.uniform(0.05, 1.0) for _ in range(4)]

        def sampler(slot, coords, attrs):
            if slot == 0:
                return texel
            if slot == 1:
                return [0.0, 0.0, 0.0, 0.0]                         # specular colour black
            return _flat_or_tex(slot, coords, attrs, mask)

        albedo = [x * x / (texel[3] * texel[3]) for x in texel[:3]]
        if multi:
            n_dot_sun, cone = _light0(c, n_lit, regs[2][:3])
            gate = lf.sun_gate_multi(c[112][0], mask=mask, **cone)
        else:
            n_dot_sun = lf.sat(lf.dot(n_lit, c[80][:3]))
            gate = lf.sun_gate_single(mask=mask)
        if lit:
            # These shaders read neither c27 nor c49: the object formula with both at 1.
            args = dict(A=albedo, vcolour=colour, tint=vc[18][:3], tint_pow=vc[18][3], add=c[20][:3], floor=c[22][:3],
                        t=lf.ambient_t(lf.ambient_axis(normal, vc[19][:3]), n_lit), n_dot_sun=n_dot_sun,
                        sun_col=c[87][:3], c27w=1.0, sun_gate=gate, c49=[1.0, 1.0, 1.0])
        else:
            args = dict(A=albedo, vs_ambient=lf.instance_ambient(colour, vc[18][:3], vc[18][3], vc[54][:3]),
                        n_dot_sun=n_dot_sun, sun_col=c[87][:3], sun_gate=gate)
        return Case(c, regs, sampler, args, [c[20][3]] * 3)

    return ue.load(dumps, ps, "frag"), make_case


FAMILIES.update({
    "plant": [("A7E45D07F5CF6627", "8123C16DBF583F92"), ("A7E45D07F5CF6627", "FC4F2EF6930768BE"),
              ("26052DEFBA688AF2", "48D30ECCF684F488")],
    "plant-multi": [("E99F4ACC7A3C78B4", "8123C16DBF583F92"), ("E99F4ACC7A3C78B4", "FC4F2EF6930768BE")],
    "plant-lit": [("165EDCD2CB963868", "B636821F95DC9D8E"), ("165EDCD2CB963868", "6AD4108C3FF07966"),
                  ("165EDCD2CB963868", "36B543CECAE5C781"), ("165EDCD2CB963868", "33C00226C154C1F2")],
    "plant-lit-multi": [("F525E023E08AC9BB", "B636821F95DC9D8E"), ("F525E023E08AC9BB", "6AD4108C3FF07966"),
                        ("F525E023E08AC9BB", "33C00226C154C1F2")],
})


# --- the table ----------------------------------------------------------------------------------

DRIVERS = {
    "terrain-layer": lambda ps, vs, dumps: _terrain_layer_driver(ps, vs, dumps, multi=False),
    "terrain-layer-multi": lambda ps, vs, dumps: _terrain_layer_driver(ps, vs, dumps, multi=True),
    "terrain-far": _terrain_far_driver,
    "object": lambda ps, vs, dumps: _object_driver(ps, vs, dumps, multi=False),
    "object-multi": lambda ps, vs, dumps: _object_driver(ps, vs, dumps, multi=True),
    "tree-trunk": lambda ps, vs, dumps: _object_driver(ps, vs, dumps, multi=False),
    "tree-trunk-multi": lambda ps, vs, dumps: _object_driver(ps, vs, dumps, multi=True),
    "tree-leaf": _leaf_driver,
    "plant": lambda ps, vs, dumps: _plant_driver(ps, vs, dumps, multi=False, lit=False),
    "plant-multi": lambda ps, vs, dumps: _plant_driver(ps, vs, dumps, multi=True, lit=False),
    "plant-lit": lambda ps, vs, dumps: _plant_driver(ps, vs, dumps, multi=False, lit=True),
    "plant-lit-multi": lambda ps, vs, dumps: _plant_driver(ps, vs, dumps, multi=True, lit=True),
}
FORMULAS = {
    "terrain-layer": lf.terrain_layer,
    "terrain-layer-multi": lf.terrain_layer,
    "terrain-far": lf.terrain_far,
    "object": lf.lit_object,
    "object-multi": lf.lit_object,
    "tree-trunk": lf.lit_object,
    "tree-trunk-multi": lf.lit_object,
    "tree-leaf": lf.tree_leaf,
    "plant": lf.plant,
    "plant-multi": lf.plant,
    "plant-lit": lf.lit_object,
    "plant-lit-multi": lf.lit_object,
}


def family_of(ps, vs):
    for family, pairs in FAMILIES.items():
        if (ps, vs) in pairs:
            return family
    raise KeyError(f"{ps} with {vs} is in no family")


def classify(ps, vs):
    """Where a draw's shader pair stands: (label, checked). A pair of FAMILIES is its family, checked; a
    pixel shader of FAMILIES with a vertex shader that was not run is "<family>, vertex shader not run"
    (the family or families its pixel shader is listed under); everything else is "no family"."""
    families = [f for f, pairs in FAMILIES.items() if any(p == ps for p, _ in pairs)]
    for family in families:
        if (ps, vs) in FAMILIES[family]:
            return family, True
    if families:
        return " / ".join(families) + ", vertex shader not run", False
    return "no family", False


def coverage(rows):
    """rows: an iterable of capture rows (dicts). Counts the decoded main-scene draws ("draw" and "tess"
    rows with in_scene) per label of classify(). Returns (frames, {label: rows}, {(label, ps, vs): rows})."""
    frames, by_label, by_pair = set(), {}, {}
    for row in rows:
        if row.get("kind") not in ("draw", "tess") or not row.get("in_scene"):
            continue
        frames.add(row.get("frame"))
        ps = (row.get("ps_hash") or "0x").upper()[2:]
        vs = (row.get("vs_hash") or (row.get("vs") or {}).get("hash") or "0x").upper()[2:]
        label, _ = classify(ps, vs)
        by_label[label] = by_label.get(label, 0) + 1
        by_pair[(label, ps, vs)] = by_pair.get((label, ps, vs), 0) + 1
    return len(frames), by_label, by_pair


def _read_rows(path):
    with open(path, "r", errors="replace") as f:
        for line in f:
            if '"in_scene":true' in line:
                yield json.loads(line)


def print_coverage(paths):
    for path in paths:
        frames, by_label, by_pair = coverage(_read_rows(path))
        total = sum(by_label.values())
        print(f"{path}: {frames} frames, {total} decoded main-scene draws ({total / max(frames, 1):.1f} per frame)")
        if not total:
            continue
        checked = sum(n for label, n in by_label.items() if label in FAMILIES)
        for label in list(FAMILIES) + sorted(k for k in by_label if k not in FAMILIES):
            n = by_label.get(label, 0)
            if n:
                print(f"  {label}: {n / frames:.1f} per frame, {100.0 * n / total:.1f}%")
        print(f"  checked pairs together: {checked / frames:.1f} per frame, {100.0 * checked / total:.1f}%")
        print("  not checked, by pair:")
        for (label, ps, vs), n in sorted(by_pair.items(), key=lambda kv: -kv[1]):
            if label not in FAMILIES:
                print(f"    ps {ps} vs {vs}: {n / frames:.1f} per frame, {100.0 * n / total:.1f}% ({label})")


def check_pair(family, ps, vs, trials=200, seed=1, formula=None, dumps=DUMPS, tol=TOLERANCE):
    ps_text, make_case = DRIVERS[family](ps, vs, dumps)
    return check(ps_text, make_case, formula or FORMULAS[family], trials, seed, tol)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--all", action="store_true", help="every pair of FAMILIES")
    ap.add_argument("--family", choices=sorted(FAMILIES))
    ap.add_argument("--shader", help="with --family: only this pixel shader (16 hex digits)")
    ap.add_argument("--trials", type=int, default=200)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--dumps", type=Path, default=DUMPS)
    ap.add_argument("--list", action="store_true", help="print the table and exit")
    ap.add_argument("--coverage", nargs="+", metavar="JSONL", help="share of a capture's main-scene draws per family")
    args = ap.parse_args(argv)
    if args.coverage:
        print_coverage(args.coverage)
        return 0
    if args.list:
        for family, pairs in FAMILIES.items():
            for ps, vs in pairs:
                print(f"{family}: ps {ps} vs {vs}")
        return 0
    if not args.all and not args.family:
        ap.print_usage(sys.stderr)
        return 2
    if not args.dumps.is_dir():
        print(f"no shader dumps at {args.dumps}", file=sys.stderr)
        return 2
    want = args.shader.upper().replace("0X", "") if args.shader else None
    todo = [(f, ps, vs) for f, pairs in FAMILIES.items() for ps, vs in pairs
            if (args.all or f == args.family) and (want is None or ps == want)]
    if not todo:
        print("no pair matches", file=sys.stderr)
        return 2
    bad = 0
    for family, ps, vs in todo:
        res = check_pair(family, ps, vs, args.trials, args.seed, dumps=args.dumps)
        print(f"{family}: ps {ps} vs {vs}: {res.line()}")
        if not res.ok:
            bad += 1
            if res.first_bad:
                k, got, want_ = res.first_bad
                print(f"    first disagreement, trial {k}: microcode {got} formula {want_}")
    print(f"{len(todo) - bad} of {len(todo)} pairs agree in every trial ({args.trials} trials each, tolerance {TOLERANCE:g} relative)")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
