"""Checks the closed light formulas of light_formulas.py against the shaders' microcode.

Usage: light_check.py --all [--trials N] [--seed S] [--dumps DIR]
       light_check.py --family <name> [--shader PS_HASH] [--trials N]
       light_check.py --family mask|mask-near|mask-far [--shader PS_HASH]     (the two screen mask shaders
                      against mask_0f99 / mask_1b4b, or one cascade's block alone against mask_near / mask_far;
                      --all runs these after the pairs)
       light_check.py --list
       light_check.py --multi-sun [--trials N]     (the two- and four-light pairs as the game draws them,
                      the sun in slot 1 or 2 behind dark lanterns, against the ONE-light function)
       light_check.py --multi-lanterns [--trials N]     (every multi-light pair with lit lanterns in each
                      slot before the game's sun slot, against the one-light function plus local_lights)
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
The screen mask tf8 is NOT switched off: it is a random texel and part of `sun_gate` (for the leaves:
of the two factors leaf_mask makes of it with the pixel shader's literals 0.6 and 0.4).

Literal constants (c252..c255) are not in the dump text. Terrain: the values of the shaders' own
literal tables, "ps_lit" / "vs_lit" of the tess rows of the captures (frame-map section 13). Mesh
vertex shaders: "vs_literals" of capture native_discovery_20261005_123635.jsonl (frame-map section 12,
"Literals"); the leaves' two- and four-light vertex shaders and their pixel shader: "vs_lit" / "ps_lit"
of the draw rows of plan Task 1's captures (plan Task 8c). Other mesh pixel shaders: the values here
are read from the idioms (2 and -1 of the normal decode, the 0 of a dp2add, the 1 of `1 - x`) and
marked so in PS_LITERALS.
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
# in the main scene (task11_bridge_up2_capture, task11_trees_up1_capture) with these pixel shaders (the
# leaves' two- and four-light vertex shaders D7DD... and 2D40... since plan Task 8c, when their literal
# tables were read from a capture). `--coverage` lists what a capture draws outside this table.
FAMILIES = {
    "terrain-layer": [("9C2144E42E95A76F", "C30A97D946FA2BE4"), ("4755A2E4A05BA678", "FB68A7F2301210E1")],
    "terrain-layer-multi": [("45243B758F9272AB", "C30A97D946FA2BE4"), ("04F23292B2F44077", "C30A97D946FA2BE4"),
                            ("AF1E16EE57E4BB06", "FB68A7F2301210E1"), ("578342593A37DD35", "FB68A7F2301210E1"),
                            ("E0996BF4D9539D38", "C30A97D946FA2BE4"), ("0981301D645E2517", "FB68A7F2301210E1")],
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
    # Campfire paired captures, run 763: every row carries these ps_lit values.
    "E0996BF4D9539D38": {254: [2, 0.99609375, 8, 0], 255: [0, 1, -1, 0.0001]},
    "0981301D645E2517": {254: [0, 1, -1, 8], 255: [2, 0, 0, 0]},
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


def _extra_lights(rng, c, lights=4):
    """The constants of two-, four- and seven-light variants: light 0 random, other colours zero."""
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
    if lights == 7:
        # The additional three slots have their own range/cone/mask bank.
        for k in (84, 85, 86):
            c[k] = [rng.uniform(-40, 40) for _ in range(3)] + [rng.uniform(0.0, 1.0)]
        for k in (91, 92, 93):
            c[k] = [0.0] * 4
        c[95] = [rng.choice([0.0, 1.0, rng.random()]) for _ in range(3)] + [0.0]
        c[97] = [rng.uniform(0.0, 0.02) for _ in range(3)] + [0.0]
        for k in (102, 103, 104):
            c[k] = unit(rng) + [0.0]
        c[106] = [rng.uniform(-0.5, 0.5) for _ in range(3)] + [0.0]
        c[108] = [rng.uniform(0.5, 3.0) for _ in range(3)] + [0.0]
        c[113] = [rng.random() for _ in range(3)] + [0.0]
        c[115] = [0.0] * 4


# The lights each multi-light pixel shader has: the registers c80.. / c87.. its dump reads.
MULTI_LIGHTS = {
    "45243B758F9272AB": 2, "AF1E16EE57E4BB06": 2, "04F23292B2F44077": 4, "578342593A37DD35": 4,     # terrain layers
    "E0996BF4D9539D38": 7, "0981301D645E2517": 7,                                               # campfire terrain
    "648B965C200A5113": 2, "789266C3E42D7E59": 4,                                                 # objects
    "58D3FAAAA84A48AA": 2, "2FBE9B4EE822609A": 4, "7928BAA7A6691A5D": 7,                         # tree trunks
    "E99F4ACC7A3C78B4": 2, "F525E023E08AC9BB": 2,                                                 # plants
}
# The tree leaves' lights are their VERTEX shader's: the pixel shader 7CD5... reads no light register and
# is the same for one, two and four lights. By vertex shader: the registers c76.. / c83.. its dump reads.
MULTI_LIGHTS_VS = {"D7DD31B9BF194DE8": 2, "2D40B53C926109BE": 4}


def light_count(ps, vs):
    """The lights of a multi-light pair: the vertex shader's where it has them (the leaves), else
    the pixel shader's."""
    return MULTI_LIGHTS_VS.get(vs) or MULTI_LIGHTS[ps]


def _sun_in_slot(rng, c, slot, lights=4):
    """The light set of a two- or four-light draw as the game loads it, with the sun in `slot` (1 to 3)
    and the lanterns without their light (spec ruling 5; frame-map section 14, "Pixel constants": the
    sun is c(80 + i) with w = 0 where c112 is one-hot at i, c109.xyz is its direction, c94 is 0 at the
    sun's slot and 1 at a lantern's; a lantern is a position with w = 1). Every other light keeps its
    random position, cone and range and gets colour zero. Seven-light slots 4..6 use c95/c113
    for the cone blend and shadow strength. Returns the sun's (direction, colour)."""
    sun_dir, sun_col = unit(rng), [rng.uniform(0.2, 2.0) for _ in range(3)]
    for j in range(7 if lights == 7 else 4):
        if j == slot:
            c[80 + j] = list(sun_dir) + [0.0]
            c[87 + j] = list(sun_col) + [0.0]
        else:
            c[80 + j] = [rng.uniform(-40, 40) for _ in range(3)] + [1.0]
            c[87 + j] = [0.0] * 4
    c[94] = [0.0 if j == slot else 1.0 for j in range(4)]
    c[112] = [1.0 if j == slot else 0.0 for j in range(4)]
    if lights == 7:
        c[95] = [0.0 if j + 4 == slot else 1.0 for j in range(3)] + [0.0]
        c[113] = [1.0 if j + 4 == slot else 0.0 for j in range(3)] + [0.0]
    c[109] = list(sun_dir) + [0.0]
    return sun_dir, sun_col


def _lanterns_in_slots(rng, c, sun_slot, world):
    """After _sun_in_slot: every slot before the sun's a lit lantern (the game's sets list lanterns first).
    A position within 8 of `world` (the vertex's world position) on each axis, so |V| runs from 0 to about
    14 and, with c96 in [0, 0.5], the range term sat(1 - |V| * c96) is 1, 0 or between in a share of the
    trials each, w = 1, a colour in [0.2, 2], c94 = 1 (one trial in three a blend in (0, 1): the general form), c96 in
    [0, 0.5], a random cone axis, c105 in [-1, 0.5], c107 in [0.5, 3], c112 = 0 (one trial in three a random
    strength: the shadow form). Returns the lanterns as local_light's keyword dicts, in slot order."""
    out = []
    for j in range(sun_slot):
        bank = j >= 4
        comp = j - 4 if bank else j
        blend_reg, range_reg, cos_reg, scale_reg, shadow_reg = (95, 97, 106, 108, 113) if bank else (94, 96, 105, 107, 112)
        for reg in (blend_reg, range_reg, cos_reg, scale_reg, shadow_reg):
            c.setdefault(reg, [0.0] * 4)
        L = dict(pos=[world[i] + rng.uniform(-8.0, 8.0) for i in range(3)], w=1.0,
                 col=[rng.uniform(0.2, 2.0) for _ in range(3)],
                 blend=rng.choice([1.0, 1.0, rng.random()]), range_scale=rng.uniform(0.0, 0.5),
                 axis=unit(rng), cone_cos=rng.uniform(-1.0, 0.5), cone_scale=rng.uniform(0.5, 3.0),
                 shadow=rng.choice([0.0, 0.0, rng.random()]))
        c[80 + j] = list(L["pos"]) + [1.0]
        c[87 + j] = list(L["col"]) + [1.0]
        c[98 + j] = list(L["axis"]) + [0.0]
        c[blend_reg][comp] = L["blend"]
        c[range_reg][comp] = L["range_scale"]
        c[cos_reg][comp] = L["cone_cos"]
        c[scale_reg][comp] = L["cone_scale"]
        c[shadow_reg][comp] = L["shadow"]
        out.append(L)
    return out


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
# The steep-ground block of the four shaders paired with C30A: (threshold literal, small literal).
_STEEP = {"9C2144E42E95A76F": ((254, 1), (255, 3)), "45243B758F9272AB": ((254, 2), (255, 3)),
          "04F23292B2F44077": ((254, 1), (255, 3)), "E0996BF4D9539D38": ((254, 1), (255, 3))}
# The pixel shader's literal 8 of sat(8 * (N . c109)).
_FACING = {"9C2144E42E95A76F": (254, 2), "4755A2E4A05BA678": (255, 0), "45243B758F9272AB": (254, 0),
           "AF1E16EE57E4BB06": (254, 3), "04F23292B2F44077": (254, 2), "578342593A37DD35": (255, 0),
           "E0996BF4D9539D38": (254, 2), "0981301D645E2517": (254, 3)}


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


def _terrain_layer_driver(ps, vs, dumps, multi, sun_slot=None, lanterns=False):
    """`sun_slot` (a multi-light shader only): the light set is the game's with the sun in that slot
    (_sun_in_slot), and the formula is fed as a ONE-light draw is: the sun's direction as it is, the
    one-light gate. That is the check of controller amendment C2 (check_multi_sun)."""
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
        sun = 80
        if multi:
            _extra_lights(rng, c, lights=light_count(ps, vs))
            if sun_slot is not None:
                _sun_in_slot(rng, c, sun_slot, lights=light_count(ps, vs))
                lantern_list = _lanterns_in_slots(rng, c, sun_slot, world) if lanterns else []
                sun = 80 + sun_slot
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
        if multi and sun_slot is None:
            n_dot_sun, cone = _light0(c, normal, world)
            gate = lf.sun_gate_multi(c[112][0], mask=mask, facing=facing, **cone)
        else:
            n_dot_sun = lf.sat(lf.dot(normal, c[sun][:3]))
            gate = lf.sun_gate_single(mask=mask, facing=facing)
        lights = lf.local_lights(world, normal, lantern_list, mask=mask, facing=facing) if lanterns else None
        args = dict(A=albedo, baked=tex(3, tuple(regs[slots["base"]][:2]))[:3], tint=c[21][:3], tint_pow=c[21][3],
                    add=c[20][:3], floor=c[22][:3], t=lf.ambient_t(lf.ambient_axis(n_vs, vc[19][:3]), n_vs),
                    n_dot_sun=n_dot_sun, sun_col=c[sun + 7][:3], c11x=c[11][0], sun_gate=gate, lights=lights)
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
_UNPREMULTIPLY = ("00E09D1BC5295D52", "58D3FAAAA84A48AA", "2FBE9B4EE822609A", "7928BAA7A6691A5D")
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
    "7928BAA7A6691A5D": {254: [1, 0, -1, 0.5], 255: [2, 0, 0, 0]},  # run 764 ps_lit
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


def _object_driver(ps, vs, dumps, multi, sun_slot=None, lanterns=False):
    """`sun_slot`: as _terrain_layer_driver's."""
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
        sun = 80
        if multi:
            _extra_lights(rng, c, lights=light_count(ps, vs))
            if sun_slot is not None:
                _sun_in_slot(rng, c, sun_slot, lights=light_count(ps, vs))
                lantern_list = _lanterns_in_slots(rng, c, sun_slot, regs[2][:3]) if lanterns else []
                sun = 80 + sun_slot
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
        if multi and sun_slot is None:
            n_dot_sun, cone = _light0(c, normal, regs[2][:3])
            gate = lf.sun_gate_multi(c[112][0], mask=mask, **cone)
        else:
            n_dot_sun = lf.sat(lf.dot(normal, c[sun][:3]))
            gate = lf.sun_gate_single(mask=mask)
        lights = lf.local_lights(regs[2][:3], normal, lantern_list, mask=mask, facing=1.0) if lanterns else None
        args = dict(A=albedo, vcolour=vcolour, tint=c[21][:3], tint_pow=c[21][3], add=c[20][:3], floor=c[22][:3],
                    t=lf.ambient_t(lf.ambient_axis(n_vs, vc[19][:3]), n_vs), n_dot_sun=n_dot_sun, sun_col=c[sun + 7][:3],
                    c27w=c[27][3], sun_gate=gate, c49=c[49][:3], lights=lights)
        return Case(c, regs, sampler, args, [c[20][3] * c[26][i] for i in range(3)], bools)

    return ue.load(dumps, ps, "frag"), make_case


FAMILIES.update({
    "object": [("8D900846800943C8", "ECD66A10092E6562"), ("8D900846800943C8", "79EAC49585797037"),
               ("8D900846800943C8", "D4D558DA6A82BDC8"), ("8D900846800943C8", "A1F7E9885EC466DF"),
               ("2FD24989AB04A475", "79EAC49585797037"), ("2FD24989AB04A475", "ECD66A10092E6562")],
    "object-multi": [("648B965C200A5113", "ECD66A10092E6562"), ("648B965C200A5113", "79EAC49585797037"),
                     ("648B965C200A5113", "D4D558DA6A82BDC8"), ("648B965C200A5113", "A1F7E9885EC466DF"),
                     ("789266C3E42D7E59", "ECD66A10092E6562"), ("789266C3E42D7E59", "D4D558DA6A82BDC8"),
                     ("789266C3E42D7E59", "A1F7E9885EC466DF")],
})


# --- tree leaves --------------------------------------------------------------------------------

# The three leaf vertex shaders (one, two and four lights) with their literal tables, registers 248 to 255.
# A584...: "vs_literals" of capture native_discovery_20261005_123635.jsonl. D7DD... and 2D40... (plan Task
# 8c): "vs_lit" of every row of theirs (56 and 40) in the capture by the trees of plan Task 1
# (out/native-evidence/lighting/task1_trees2_capture; listed in task8c/leaf_literals.txt), where A584...'s
# rows (312, and 288 at the bridge) carry the same table as the older capture. "gamma": where the literal
# 1 / 2.2 on the ambient exponent stands (the three tables hold the same values in another order).
_LEAF_WIND = ([-0.3, 0.05, 0.25, 1.0], [2 * math.pi, 0.25, 0.11, 0.23], [0.04, 0.025, 0.0, 1.0], [0.035, 0.472, 0.5, 1.0],
              [0.3849, 0.3, 0.019245, 0.2886750102043152], [0.55, 0.9, 0.15, 0.1])
_LEAF_GAMMA_VALUE = 0.45454543828964233
_LEAF_VS = {
    "A5846836C90E1192": dict(lights=1, gamma=(249, 3), literals=dict(zip(range(248, 256), (
        [0.0] * 4, [-math.pi, 0.7, 100.0, _LEAF_GAMMA_VALUE]) + _LEAF_WIND))),
    "D7DD31B9BF194DE8": dict(lights=2, gamma=(248, 3), literals=dict(zip(range(248, 256), (
        [-math.pi, 0.7, 100.0, _LEAF_GAMMA_VALUE],) + _LEAF_WIND + ([-1.0, 0.0, 0.0, 0.0],)))),
    "2D40B53C926109BE": dict(lights=4, gamma=(255, 0), literals=dict(zip(range(248, 256), (
        [-math.pi, -1.0, 0.7, 100.0],) + _LEAF_WIND + ([_LEAF_GAMMA_VALUE, 0.0, 0.0, 0.0],)))),
}
# The leaf pixel shader's literals (plan Task 8c): "ps_lit" of every row of 7CD5... in the two captures of
# plan Task 1 (288 and 408 rows; task8c/leaf_literals.txt), and c255 in every execution of the shader in
# the GPU logs of that task (frame-map section 14, "The shaders' literals"). c255.w = 0.6 and c255.z = 0.4
# shape the mask (leaf_mask); c255.x = 1 and c255.y = 0 and c254.x = 0.5 belong to the fog volume under
# b129, which no trial reaches.
PS_LITERALS.update({"7CD57B81550F19E3": {254: [0.5, 0.0, 0.0, 0.0], 255: [1.0, 0.0, 0.4, 0.6]}})


def _leaf_driver(ps, vs, dumps, sun_slot=None, lanterns=False):
    """`sun_slot` (a two- or four-light vertex shader only): the vertex shader's light set is the game's
    with the sun in that slot (_sun_in_slot, its registers 80.., 87.. and 94 taken as the vertex side's
    76.., 83.. and 90), and the formula is fed as a ONE-light draw is: the sun's direction as it is and no
    cone or range (spot 1). Without it: light 0 with every constant random, the colours of lights 1 to 3
    zero."""
    vs_text, info = ue.load(dumps, vs, "vert"), _LEAF_VS[vs]

    def make_case(rng):
        vc = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3, 4, 5, 6)}
        vc[7] = [rng.uniform(-50, 50) for _ in range(3)] + [rng.uniform(0.5, 2.0)]
        _tree_wind(rng, vc)
        vc[18] = [rng.uniform(0.5, 2.0) for _ in range(3)] + [rng.choice([2.86, 2.2, 1.3])]
        for k in range(28, 40):
            vc[k] = [rng.uniform(-0.5, 0.5) for _ in range(4)]
        vc[50] = [0.0, 0.0, 0.0, rng.uniform(0.0, 1.0)]
        vc[54] = [rng.uniform(0.0, 0.2) for _ in range(3)] + [0.0]
        # The light set: light j reads c(76 + j) (position or direction, w), c(83 + j) (colour),
        # c(94 + j) (cone axis) and component j of c90 (blend of cone and range), c92 (range), c101 and
        # c103 (cone).
        for j in range(4):
            vc[76 + j] = [rng.uniform(-40, 40) for _ in range(3)] + [rng.choice([0.0, rng.uniform(0.2, 1.0)])]
            vc[83 + j] = [rng.uniform(0.2, 2.0) for _ in range(3)] + [0.0] if j == 0 else [0.0] * 4
            vc[94 + j] = unit(rng) + [0.0]
        vc[90] = [rng.choice([0.0, 1.0, rng.random()]) for _ in range(4)]
        vc[92] = [rng.uniform(0.0, 0.02) for _ in range(4)]
        vc[101] = [rng.uniform(-0.5, 0.5) for _ in range(4)]
        vc[103] = [rng.uniform(0.5, 3.0) for _ in range(4)]
        sun = 0
        if sun_slot is not None:
            game = {}
            _sun_in_slot(rng, game, sun_slot)
            for j in range(4):
                vc[76 + j], vc[83 + j] = game[80 + j], game[87 + j]
            vc[90] = game[94]
            sun = sun_slot
        vc.update(info["literals"])
        vertex = Vertex(rng)
        index = float(rng.randrange(1000))
        regs, _, _ = run_vs(vs_text, {k: list(v) for k, v in vc.items()}, regs={0: [index, 0, 0, 0]}, vfetch=vertex)
        if lanterns:
            # The lanterns about the vertex's own world position: a first run (the position reads no light
            # constant; Vertex keeps each fetched element, so the second run fetches the same vertex) gives
            # o0, the lanterns are placed about it, and the second run is the one checked.
            placed = regs[0][:3]
            _lanterns_in_slots(rng, game, sun_slot, placed)
            for j in range(sun_slot):
                vc[76 + j], vc[83 + j], vc[94 + j] = game[80 + j], game[87 + j], game[98 + j]
            vc[90], vc[92], vc[101], vc[103] = game[94], game[96], game[105], game[107]
            regs, _, _ = run_vs(vs_text, vc, regs={0: [index, 0, 0, 0]}, vfetch=vertex)
            assert regs[0][:3] == placed, "the leaf position moved between the two runs"
        e = vertex.elements
        world = regs[0][:3]                                     # o0: the swayed world position
        normal = [lf.dot(vc[r][:3], e[(0, 2)][:3]) * vc[7][3] for r in (4, 5, 6)]
        view = lf.normalize([vc[9][i] - world[i] for i in range(3)])
        if sun_slot is None:
            to_light = [vc[76][i] - world[i] * vc[76][3] for i in range(3)]
            light_dir = lf.normalize(to_light)
            rng_ = lf.sat(1.0 - math.sqrt(lf.dot(to_light, to_light)) * vc[92][0])
            cone = lf.sat((lf.dot(light_dir, vc[94][:3]) - vc[101][0]) * vc[103][0])
            spot = 1.0 + vc[90][0] * (cone * rng_ - 1.0)
        else:
            light_dir, spot = lf.normalize(vc[76 + sun][:3]), 1.0
        front, back = lf.leaf_light(normal, view, light_dir, vc[50][3], spot)
        front3 = [s * front for s in vc[83 + sun][:3]]
        back3 = [s * back for s in vc[83 + sun][:3]]
        if sun_slot is not None and lanterns:
            for j in range(sun_slot):
                to_light = [vc[76 + j][i] - world[i] * vc[76 + j][3] for i in range(3)]
                ld = lf.normalize(to_light)
                rng_ = lf.sat(1.0 - math.sqrt(lf.dot(to_light, to_light)) * vc[92][j])
                cone = lf.sat((lf.dot(ld, vc[94 + j][:3]) - vc[101][j]) * vc[103][j])
                spot = 1.0 + vc[90][j] * (cone * rng_ - 1.0)
                f, b = lf.leaf_light(normal, view, ld, vc[50][3], spot)
                front3 = [front3[i] + vc[83 + j][i] * f for i in range(3)]
                back3 = [back3[i] + vc[83 + j][i] * b for i in range(3)]
        a0 = int(e[(0, 0)][2])
        probe = lf.probe_colour(vc[28 + a0][:3], [vc[29 + a0], vc[30 + a0], vc[31 + a0]], e[(1, 0)])
        gr, gc = info["gamma"]
        ambient = lf.instance_ambient(probe, vc[18][:3], vc[18][3] * vc[gr][gc], vc[54][:3])

        regs[max(regs) + 1] = [rng.uniform(0, 1120), rng.uniform(0, 720), 0, 0]
        c = {0: [rng.uniform(0.0005, 0.002) for _ in range(4)],
             8: [rng.uniform(0.5, 2.0), rng.uniform(0.5, 2.0), rng.random(), rng.random()],
             15: [0.0, 0.0, 0.0, rng.uniform(0.5, 1.0)],
             20: [0.0, 0.0, 0.0, rng.choice([1.0, 16.0])],
             26: [rng.uniform(0.5, 1.0) for _ in range(4)],
             27: [rng.uniform(0.5, 1.0) for _ in range(4)],
             49: [rng.uniform(0.5, 1.5) for _ in range(4)]}
        c.update({k: list(v) for k, v in PS_LITERALS[ps].items()})
        mask = rng.random()
        texel = [rng.uniform(0.05, 1.0) for _ in range(4)]

        def sampler(slot, coords, attrs):
            return texel if slot == 0 else _flat_or_tex(slot, coords, attrs, mask)

        # The mask's two factors with the shader's own literals: gain c255.w = 0.6, bias c255.z = 0.4.
        m1, m2 = lf.leaf_mask(mask, c[255][3], c[255][2])
        args = dict(A=[x * x / (texel[3] * texel[3]) for x in texel[:3]], vs_ambient=ambient, front3=front3, back3=back3,
                    c27w=c[27][3], m1=m1, m2=m2, c49=c[49][:3])
        # b132: the colour times o4 plus o5, the vertex fog (1 and 0 while it is off). b129 stays clear.
        return Case(c, regs, sampler, args, [c[20][3] * c[26][i] for i in range(3)], rng.choice([(), (132,)]))

    return ue.load(dumps, ps, "frag"), make_case


FAMILIES.update({
    "tree-trunk": [("00E09D1BC5295D52", "475EC9F795E5EDBB")],
    "tree-trunk-multi": [("58D3FAAAA84A48AA", "475EC9F795E5EDBB"), ("2FBE9B4EE822609A", "475EC9F795E5EDBB"),
                         ("7928BAA7A6691A5D", "475EC9F795E5EDBB")],
    "tree-leaf": [("7CD57B81550F19E3", "A5846836C90E1192")],
    # (plan Task 8c; the light count is the vertex shader's: MULTI_LIGHTS_VS)
    "tree-leaf-multi": [("7CD57B81550F19E3", "D7DD31B9BF194DE8"), ("7CD57B81550F19E3", "2D40B53C926109BE")],
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


def _plant_driver(ps, vs, dumps, multi, lit, sun_slot=None, lanterns=False):
    """`sun_slot`: as _terrain_layer_driver's."""
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
        sun = 80
        if multi:
            _extra_lights(rng, c)
            if sun_slot is not None:
                _sun_in_slot(rng, c, sun_slot)
                lantern_list = _lanterns_in_slots(rng, c, sun_slot, regs[2][:3]) if lanterns else []
                sun = 80 + sun_slot
        mask = rng.random()
        texel = [rng.uniform(0.05, 1.0) for _ in range(4)]

        def sampler(slot, coords, attrs):
            if slot == 0:
                return texel
            if slot == 1:
                return [0.0, 0.0, 0.0, 0.0]                         # specular colour black
            return _flat_or_tex(slot, coords, attrs, mask)

        albedo = [x * x / (texel[3] * texel[3]) for x in texel[:3]]
        if multi and sun_slot is None:
            n_dot_sun, cone = _light0(c, n_lit, regs[2][:3])
            gate = lf.sun_gate_multi(c[112][0], mask=mask, **cone)
        else:
            n_dot_sun = lf.sat(lf.dot(n_lit, c[sun][:3]))
            gate = lf.sun_gate_single(mask=mask)
        lights = lf.local_lights(regs[2][:3], n_lit, lantern_list, mask=mask, facing=1.0) if lanterns else None
        if lit:
            # These shaders read neither c27 nor c49: the object formula with both at 1.
            args = dict(A=albedo, vcolour=colour, tint=vc[18][:3], tint_pow=vc[18][3], add=c[20][:3], floor=c[22][:3],
                        t=lf.ambient_t(lf.ambient_axis(normal, vc[19][:3]), n_lit), n_dot_sun=n_dot_sun,
                        sun_col=c[sun + 7][:3], c27w=1.0, sun_gate=gate, c49=[1.0, 1.0, 1.0], lights=lights)
        else:
            args = dict(A=albedo, vs_ambient=lf.instance_ambient(colour, vc[18][:3], vc[18][3], vc[54][:3]),
                        n_dot_sun=n_dot_sun, sun_col=c[sun + 7][:3], sun_gate=gate, lights=lights)
        return Case(c, regs, sampler, args, [c[20][3]] * 3)

    return ue.load(dumps, ps, "frag"), make_case


FAMILIES.update({
    "plant": [("A7E45D07F5CF6627", "8123C16DBF583F92"), ("A7E45D07F5CF6627", "FC4F2EF6930768BE"),
              ("26052DEFBA688AF2", "48D30ECCF684F488")],
    "plant-multi": [("E99F4ACC7A3C78B4", "8123C16DBF583F92"), ("E99F4ACC7A3C78B4", "FC4F2EF6930768BE")],
    "plant-lit": [("165EDCD2CB963868", "B636821F95DC9D8E"), ("165EDCD2CB963868", "6AD4108C3FF07966"),
                  ("165EDCD2CB963868", "36B543CECAE5C781"), ("165EDCD2CB963868", "33C00226C154C1F2")],
    # (F525... with 36B5...: drawn at the third spot of plan Task 8a's runs, one draw a frame; run for plan Task 8b)
    "plant-lit-multi": [("F525E023E08AC9BB", "B636821F95DC9D8E"), ("F525E023E08AC9BB", "6AD4108C3FF07966"),
                        ("F525E023E08AC9BB", "33C00226C154C1F2"), ("F525E023E08AC9BB", "36B543CECAE5C781")],
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
    "tree-leaf-multi": _leaf_driver,
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
    "tree-leaf": lf.tree_leaf_sums,
    "tree-leaf-multi": lf.tree_leaf_sums,
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


# --- the sun of a multi-light draw (controller amendment C2) --------------------------------------

# The families whose two- and four-light pairs --multi-sun checks, and the slot the sun stands in at
# the game's draws of a shader with that many lights: slot 1 behind one lantern at every draw of a
# two-light shader, slot 2 behind two lanterns at every draw of a four-light shader (all 1,520 and 944
# main-scene -multi rows of the two captures of plan Task 6's final runs, read from their "pfull":
# out/native-evidence/lighting/task7/multi_slots_bridge_trees.txt). The plants' pairs (two lights each; plan
# Task 8b): slot 1 in all 200, 360 and 136 plant -multi rows of the three captures of plan Task 8a's runs
# (out/native-evidence/lighting/task8b/plant_rows_check.txt). The leaves' pairs (plan Task 8c; the lights are
# the vertex shader's): slot 1 in all 56 rows of D7DD... and slot 2 in all 40 of 2D40... in the capture by the
# trees of plan Task 1, the sun there bit for bit the frame's, w = 0 and c90 zero at its slot, a lantern (w = 1,
# c90 = 1) in every slot before it (out/native-evidence/lighting/task8c/leaf_rows_check.txt).
MULTI_SUN_FAMILIES = ("terrain-layer-multi", "object-multi", "tree-trunk-multi", "plant-multi", "plant-lit-multi",
                      "tree-leaf-multi")
# Run 763 campfire captures: c84/c91 equals the frame sun; c113 = (1,0,0,0).
GAME_SUN_SLOT = {2: 1, 4: 2, 7: 4}
_SUN_DRIVERS = {
    "terrain-layer-multi": _terrain_layer_driver,
    "object-multi": _object_driver,
    "tree-trunk-multi": _object_driver,
    "plant-multi": lambda ps, vs, dumps, multi, sun_slot: _plant_driver(ps, vs, dumps, multi, lit=False, sun_slot=sun_slot),
    "plant-lit-multi": lambda ps, vs, dumps, multi, sun_slot: _plant_driver(ps, vs, dumps, multi, lit=True, sun_slot=sun_slot),
    "tree-leaf-multi": lambda ps, vs, dumps, multi, sun_slot: _leaf_driver(ps, vs, dumps, sun_slot=sun_slot),
}


def check_multi_sun(family, ps, vs, slot, trials=200, seed=1, dumps=DUMPS, tol=TOLERANCE):
    """Whether a multi-light pair, drawn as the game draws it with the sun in light slot `slot`
    and its lanterns dark, is its family's ONE-light function of that sun: the microcode with the light
    set of _sun_in_slot against the family's formula fed the sun's direction as it is, the one-light
    gate (mask, and for the layers the facing factor of c109) and the sun's colour. Everything else of a
    trial is check_pair's. A pair that agrees here may be lit by the native pass with the frame's sun
    through the one-light function; one that does not stays light-unknown."""
    if family not in _SUN_DRIVERS:
        raise KeyError(f"{family}: not a family --multi-sun checks")
    if not 1 <= slot < light_count(ps, vs):
        raise ValueError(f"{ps} with {vs} has {light_count(ps, vs)} lights: no sun slot {slot}")
    ps_text, make_case = _SUN_DRIVERS[family](ps, vs, dumps, multi=True, sun_slot=slot)
    return check(ps_text, make_case, FORMULAS[family], trials, seed, tol)


def print_multi_sun(trials, seed, dumps):
    """Every pair of MULTI_SUN_FAMILIES at every slot its shader has behind light 0. Returns the number
    of pairs that do not agree at the slot the game uses (GAME_SUN_SLOT)."""
    bad = 0
    for family in MULTI_SUN_FAMILIES:
        for ps, vs in FAMILIES[family]:
            lights = light_count(ps, vs)
            for slot in range(1, lights):
                res = check_multi_sun(family, ps, vs, slot, trials, seed, dumps)
                game = slot == GAME_SUN_SLOT[lights]
                print(f"{family}: ps {ps} vs {vs} ({lights} lights), sun in slot {slot}"
                      f"{' (the game)' if game else ''}: {res.line()}")
                if not res.ok:
                    bad += game
                    if res.first_bad:
                        k, got, want_ = res.first_bad
                        print(f"    first disagreement, trial {k}: microcode {got} formula {want_}")
    return bad


_LANTERN_DRIVERS = {
    "terrain-layer-multi": lambda ps, vs, dumps, slot: _terrain_layer_driver(ps, vs, dumps, multi=True, sun_slot=slot, lanterns=True),
    "object-multi": lambda ps, vs, dumps, slot: _object_driver(ps, vs, dumps, multi=True, sun_slot=slot, lanterns=True),
    "tree-trunk-multi": lambda ps, vs, dumps, slot: _object_driver(ps, vs, dumps, multi=True, sun_slot=slot, lanterns=True),
    "plant-multi": lambda ps, vs, dumps, slot: _plant_driver(ps, vs, dumps, True, lit=False, sun_slot=slot, lanterns=True),
    "plant-lit-multi": lambda ps, vs, dumps, slot: _plant_driver(ps, vs, dumps, True, lit=True, sun_slot=slot, lanterns=True),
    "tree-leaf-multi": lambda ps, vs, dumps, slot: _leaf_driver(ps, vs, dumps, sun_slot=slot, lanterns=True),
}


def check_multi_lanterns(family, ps, vs, trials=200, seed=1, dumps=DUMPS, tol=TOLERANCE):
    """Whether a multi-light pair, drawn with the game's set (the sun in GAME_SUN_SLOT) and lit lanterns in
    every slot before it, is its family's one-light function of the sun plus local_lights over the
    lanterns (lighting-formulas.md, the multi form per slot). A pair that agrees may be drawn with its
    lanterns by the native pass (local lights design, ruling 2); one that does not stays sun-only."""
    if family not in _LANTERN_DRIVERS:
        raise KeyError(f"{family}: not a family --multi-lanterns checks")
    slot = GAME_SUN_SLOT[light_count(ps, vs)]
    ps_text, make_case = _LANTERN_DRIVERS[family](ps, vs, dumps, slot)
    return check(ps_text, make_case, FORMULAS[family], trials, seed, tol)


def print_multi_lanterns(trials, seed, dumps):
    """Every pair of MULTI_SUN_FAMILIES with lanterns before the game's sun slot. Returns the pairs that disagree."""
    bad = 0
    for family in MULTI_SUN_FAMILIES:
        for ps, vs in FAMILIES[family]:
            lights = light_count(ps, vs)
            res = check_multi_lanterns(family, ps, vs, trials, seed, dumps)
            print(f"{family}: ps {ps} vs {vs} ({lights} lights, sun in slot {GAME_SUN_SLOT[lights]}, "
                  f"{GAME_SUN_SLOT[lights]} lanterns): {res.line()}")
            if not res.ok:
                bad += 1
                if res.first_bad:
                    k, got, want_ = res.first_bad
                    print(f"    first disagreement, trial {k}: microcode {got} formula {want_}")
    return bad


# --- the screen mask (shadows sub-project, plan Task 2) -------------------------------------------

def _f32(bits):
    """Four single-precision values from 32 hex digits, as the GPU logs print a register."""
    return [struct.unpack(">f", bytes.fromhex(bits[i:i + 8]))[0] for i in range(0, 32, 8)]


# The literal registers of the two mask shaders. Not in the dumps (the binary dump is the microcode alone)
# and in no capture row: the emulated GPU's registers at every execution of the shaders in the four GPU
# constant logs of the lighting sub-project's Task 1 (60 + 60 + 16 + 16 frames at the bridge and by the
# trees; one value per register in all of them), mined into
# out/native-evidence/shadows/task2_gpu_log_mask_registers.txt by task2_mine_gpu_log.py there.
MASK_LITERALS = {
    "0F99810F072BF0FC": {
        248: _f32("412000003DAAAAABBF80000040000000"),      # 10, 1/12, -1, 2
        249: _f32("000000003F8000003F0000003DCCCCCD"),      # 0, 1, 0.5, 0.1
        250: _f32("BEF5C5F8BF190386BE47A9A03F445B8E"),
        251: _f32("3EF265F1BF4AA39CBE5039AC3E3DE97D"),
        252: _f32("BD96B11C3ED32DB2BF32276B3F01E6FF"),
        253: _f32("BECFC654BF6EBFDB3F765BEA3F04FB12"),
        254: _f32("BEA70542BEA4D5523F1EE73EBF64A3C6"),
        255: _f32("BF5713AD3F657BC83EEA0DDC3D83F141"),
    },
    "1B4B5AE12EF270CA": {
        254: _f32("3F0000003F80000000000000BF800000"),      # 0.5, 1, 0, -1
        255: _f32("40000000000000000000000000000000"),      # 2, 0, 0, 0
    },
}
MASK_FAMILIES = {
    "mask-near": ["0F99810F072BF0FC"],                          # the near block alone, against mask_near
    "mask-far": ["0F99810F072BF0FC", "1B4B5AE12EF270CA"],       # the far block alone, against mask_far
    "mask": ["0F99810F072BF0FC", "1B4B5AE12EF270CA"],           # the whole shaders: mask_0f99, mask_1b4b
}
MASK_REGIMES = {"0F99810F072BF0FC": ("near only", "blend", "far only", "fade", "beyond"),
                "1B4B5AE12EF270CA": ("not far", "far", "fade", "beyond")}
_MASK_ALONE = {"mask-near": "near only", "mask-far": {"0F99810F072BF0FC": "far only", "1B4B5AE12EF270CA": "far"}}


def mask_taps(lit):
    """The twelve near taps (x, y) of 0F99... from its literal registers: the x of a tap and its y stand
    in different registers (instr 33 to 50: one register's pair is multiplied by the rotation's first
    term and negated second term, the other's is added times the second and first)."""
    return [(lit[251][2], lit[254][2]), (lit[251][3], lit[254][3]), (lit[253][2], lit[250][2]), (lit[253][3], lit[250][3]),
            (lit[252][2], lit[255][2]), (lit[252][3], lit[255][3]), (lit[255][0], lit[252][0]), (lit[255][1], lit[252][1]),
            (lit[251][0], lit[250][0]), (lit[251][1], lit[250][1]), (lit[254][0], lit[253][0]), (lit[254][1], lit[253][1])]


def _map_value(salt, slot, ix, iy):
    """A fixed pseudo-random value in [0, 1) per (trial, texture, texel)."""
    return random.Random(zlib.crc32(struct.pack("<qqqq", salt, slot, ix, iy))).random()


class MaskCase:
    """One trial of a mask shader: what the microcode gets (constants, registers, sampler, texture sizes),
    the whole formula's arguments, the two cascades' terms alone (thunks), and which part of the shader
    the trial reaches."""

    def __init__(self, consts, regs, sampler, tex_size, args, near_alone, far_alone, regime, flags):
        self.consts, self.regs, self.sampler, self.tex_size, self.args = consts, regs, sampler, tex_size, args
        self.near_alone, self.far_alone, self.regime, self.flags = near_alone, far_alone, regime, flags


def _mask_screen(rng):
    """A random pixel of a 1120 x 720 target with its depth fetch: (pixel, scale and offset register,
    depth, the screen point the shaders build, the depth sampler). The sampler answers only at the
    coordinates the shaders must compute (the pixel's centre plus the offset); anywhere else it is NaN."""
    px, py = (float(rng.randrange(1120)), float(rng.randrange(720))) if rng.random() < 0.5 else \
        (rng.uniform(0, 1120), rng.uniform(0, 720))
    ox, oy = (0.0, 0.0) if rng.random() < 0.5 else (rng.uniform(-0.5, 0.5), rng.uniform(-0.5, 0.5))
    reg = [1 / 1120, 1 / 720, ox, oy]
    depth = rng.uniform(0.05, 0.95)
    eu, ev = (px + 0.5 + ox) / 1120, (py + 0.5 + oy) / 720

    def depth_at(u, v):
        return depth if abs(u - eu) <= 1e-9 and abs(v - ev) <= 1e-9 else NAN

    screen = [(2 * px + 1 + 2 * ox) / 1120 - 1, 1 - (2 * py + 1 + 2 * oy) / 720, 1 - depth, 1.0]
    return (px, py), reg, depth, screen, depth_at


def _mask_cascade(rng, equal):
    """Random rows of a cascade for the screen point: u and v anywhere, 1 - depth within (0, 1). `equal`:
    the depth rows are constants, so the compared value 1 - depth is one exact number in the microcode
    and in the formula, and the trial's map can hold that very number (the comparisons' equal case)."""
    rows = [[rng.uniform(-0.2, 0.2) for _ in range(3)] + [rng.uniform(0.3, 0.7)] for _ in range(2)]
    if equal:
        rows += [[0.0, 0.0, 0.0, rng.choice([0.25, 0.5, 0.625])], [0.0, 0.0, 0.0, 1.0]]
    else:
        rows += [[rng.uniform(-0.1, 0.1) for _ in range(3)] + [rng.uniform(0.3, 0.7)],
                 [rng.uniform(-0.1, 0.1) for _ in range(3)] + [rng.uniform(0.8, 1.3)]]
    return rows


def _mask_map(salt, slot, rows, equal):
    """A depth map as a function of the texel: uniform in [0, 1), or in an `equal` trial the compared
    value itself in half of the texels and an eighth to either side in the others."""
    compared = 1.0 - rows[2][3]

    def texel(ix, iy):
        r = _map_value(salt, slot, ix, iy)
        return (compared, compared, compared - 0.125, compared + 0.125)[int(r * 4)] if equal else r
    return texel


def _mask_0f99_case(rng, regime, literals, taps):
    lit = {k: list(v) for k, v in literals.items()}
    flags = set()
    if rng.random() < 0.5:
        # The form holds for any literal table: other offsets, reach, tap weight and slope. The 0, 1, 0.5,
        # -1 and 2 of the arithmetic stay.
        flags.add("random literals")
        for reg in lit:
            if reg >= 250:
                lit[reg] = [rng.uniform(-1, 1) for _ in range(4)]
        lit[248][0], lit[248][1], lit[249][3] = rng.uniform(5.0, 15.0), rng.uniform(0.05, 0.5), rng.uniform(0.05, 0.3)
    reach = lit[248][0]
    regime = regime or rng.choice(MASK_REGIMES["0F99810F072BF0FC"])
    s0, s2 = rng.uniform(10, 30), rng.uniform(1, 4)
    split = [s0, s0 + s2 + rng.uniform(5, 20), s2, rng.uniform(0.2, 0.8)]
    lo, hi = {"near only": (0.5, s0), "blend": (s0, s0 + s2), "far only": (s0 + s2, split[1]),
              "fade": (split[1], split[1] + reach), "beyond": (split[1] + reach, split[1] + reach + 20)}[regime]
    d = rng.uniform(lo + 1e-3, hi - 1e-3)
    pixel, c46, depth, screen, depth_at = _mask_screen(rng)
    # Screen-to-world rows that put this pixel at distance d from the eye: random rows, then each of the
    # first three shifted by a multiple of the fourth (which moves the divided point by that multiple).
    eye = [rng.uniform(-100, 100) for _ in range(3)]
    target = [e + d * x for e, x in zip(eye, unit(rng))]
    row3 = [rng.uniform(-0.2, 0.2) for _ in range(3)] + [rng.uniform(0.8, 1.5)]
    w = lf.dot(row3, screen)
    to_world = []
    for i in range(3):
        row = [rng.uniform(-1, 1) for _ in range(4)]
        shift = target[i] - lf.dot(row, screen) / w
        to_world.append([a + shift * b for a, b in zip(row, row3)])
    to_world.append(row3)
    equal = rng.random() < 0.25
    if equal:
        flags.add("equal depth")
    near_rows, far_rows = _mask_cascade(rng, equal), _mask_cascade(rng, equal)
    c47 = [0.5, 0.5, 1 / 32, 1 / 32] if rng.random() < 0.5 else \
        [rng.random(), rng.random(), rng.uniform(0.02, 0.05), rng.uniform(0.02, 0.05)]
    radius = 2.5 / 1024 if rng.random() < 0.5 else rng.uniform(1.0, 4.0) / 1024
    far_size = rng.choice([(1024, 1024), (1024, 1024), (512, 2048)])
    salt = rng.getrandbits(31)
    near_texel, far_texel = _mask_map(salt, 6, near_rows, equal), _mask_map(salt, 7, far_rows, equal)

    def near_at(u, v):
        return near_texel(math.floor(u * 1024), math.floor(v * 1024))

    def rot_at(u, v):
        ix, iy = math.floor(u * 64), math.floor(v * 64)
        return (0.05 + 0.95 * _map_value(salt, 14, ix, iy), 0.05 + 0.95 * _map_value(salt, 15, ix, iy))

    def sampler(slot, coords, attrs):
        if slot == 13:
            return [depth_at(*coords), 7.0, 7.0, 7.0]
        if slot == 14:
            return list(rot_at(*coords)) + [5.0, 5.0]
        if slot == 6:
            return [near_at(*coords), 3.0, 3.0, 3.0]
        if slot == 7:
            return [far_texel(*ue.point_texel(coords, far_size, attrs)), 3.0, 3.0, 3.0]
        raise KeyError(f"mask shader fetch tf{slot}")

    consts = {12: split, 31: eye + [rng.uniform(0.5, 2.0)], 46: c46, 47: c47,
              77: [radius, rng.random(), rng.random(), rng.random()]}
    for i in range(4):
        consts[36 + i], consts[40 + i], consts[116 + i] = near_rows[i], far_rows[i], to_world[i]
    consts.update(lit)
    # The position register is read through its absolute value: either sign, and unread components.
    regs = {0: [rng.choice([-1, 1]) * pixel[0], rng.choice([-1, 1]) * pixel[1], rng.random(), rng.random()]}
    offsets = taps(lit)
    args = dict(pixel=pixel, sample_depth=depth_at, depth_scale=c46[:2], depth_offset=c46[2:], screen_to_world=to_world,
                eye=eye, split=split, near_rows=near_rows, far_rows=far_rows, sample_rot=rot_at, rot_offset=c47[:2],
                rot_scale=c47[2:], offsets=offsets, radius=radius, sample_near=near_at, sample_far_texel=far_texel,
                far_size=far_size, tap_weight=lit[248][1], far_reach=reach, slope=lit[249][3])

    def near_alone():
        t = rot_at((pixel[0] + c47[0]) * c47[2], (pixel[1] + c47[1]) * c47[3])
        point = lf.mask_screen_point(pixel, depth, c46[:2], c46[2:])
        return lf.mask_near(point, near_rows, near_at, (2 * t[0] - 1, 2 * t[1] - 1), offsets, radius, lit[248][1]), 1.0

    def far_alone():
        return lf.mask_far(lf.mask_screen_point(pixel, depth, c46[:2], c46[2:]), far_rows, far_texel, far_size), 1.0

    return MaskCase(consts, regs, sampler, {7: far_size}, args, near_alone, far_alone, regime, flags)


def _mask_1b4b_case(rng, regime, literals, taps):
    regime = regime or rng.choice(MASK_REGIMES["1B4B5AE12EF270CA"])
    start = rng.uniform(0.3, 0.6)
    fade_start = start + rng.uniform(0.05, 0.2)
    width = rng.uniform(0.02, 0.1)
    lo, hi = {"not far": (0.02, start), "far": (start, fade_start), "fade": (fade_start, fade_start + width),
              "beyond": (fade_start + width, 0.98)}[regime]
    pixel, c31, _, _, _ = _mask_screen(rng)
    depth = 1.0 - rng.uniform(lo + 1e-4, hi - 1e-4)
    eu, ev = (pixel[0] + 0.5 + c31[2]) / 1120, (pixel[1] + 0.5 + c31[3]) / 720

    def depth_at(u, v):
        return depth if abs(u - eu) <= 1e-9 and abs(v - ev) <= 1e-9 else NAN

    equal = rng.random() < 0.25
    flags = {"equal depth"} if equal else set()
    far_rows = _mask_cascade(rng, equal)
    far_size = rng.choice([(1024, 1024), (1024, 1024), (512, 2048)])
    far_texel = _mask_map(rng.getrandbits(31), 7, far_rows, equal)
    fade_to = rng.uniform(0.2, 0.8)

    def sampler(slot, coords, attrs):
        if slot == 13:
            return [depth_at(*coords), 7.0, 7.0, 7.0]
        if slot == 7:
            return [far_texel(*ue.point_texel(coords, far_size, attrs)), 3.0, 3.0, 3.0]
        raise KeyError(f"mask shader fetch tf{slot}")

    consts = {12: [rng.uniform(10, 30), rng.uniform(30, 50), rng.uniform(1, 4), fade_to], 31: c31,
              46: [start, rng.random(), rng.random(), rng.random()], 47: [fade_start, rng.random(), rng.random(), rng.random()],
              77: [width, rng.random(), rng.random(), rng.random()]}
    for i in range(4):
        consts[40 + i] = far_rows[i]
    consts.update({k: list(v) for k, v in literals.items()})
    # Its vertex shader has one interpolator (r0, which the shader overwrites before reading); the
    # position is r1.
    regs = {0: [rng.random() for _ in range(4)],
            1: [rng.choice([-1, 1]) * pixel[0], rng.choice([-1, 1]) * pixel[1], rng.random(), rng.random()]}
    args = dict(pixel=pixel, sample_depth=depth_at, depth_scale=c31[:2], depth_offset=c31[2:], far_rows=far_rows,
                sample_far_texel=far_texel, far_size=far_size, start=start, fade_start=fade_start, fade_width=width,
                fade_to=fade_to)

    def far_alone():
        return lf.mask_far(lf.mask_screen_point(pixel, depth, c31[:2], c31[2:]), far_rows, far_texel, far_size), 1.0

    return MaskCase(consts, regs, sampler, {7: far_size}, args, None, far_alone, regime, flags)


_MASK_CASES = {"0F99810F072BF0FC": _mask_0f99_case, "1B4B5AE12EF270CA": _mask_1b4b_case}


def _mask_cases(family, shader, trials, seed, literals, taps):
    if shader not in MASK_FAMILIES.get(family, ()):
        raise KeyError(f"{shader} is not a shader of family {family}")
    regime = _MASK_ALONE.get(family)
    if isinstance(regime, dict):
        regime = regime[shader]
    rng = random.Random(seed)
    for _ in range(trials):
        yield _MASK_CASES[shader](rng, regime, literals or MASK_LITERALS[shader], taps or mask_taps)


def mask_regimes(shader, trials=200, seed=1, family="mask", literals=None, taps=None):
    """Which parts of a mask shader the trials of check_mask reach: {name: trials}, the regimes of
    MASK_REGIMES and the flags "equal depth" and "random literals"."""
    seen = {}
    for case in _mask_cases(family, shader, trials, seed, literals, taps):
        for name in [case.regime] + sorted(case.flags):
            seen[name] = seen.get(name, 0) + 1
    return seen


def check_mask(family, shader, trials=200, seed=1, dumps=DUMPS, tol=TOLERANCE, text=None, literals=None, taps=None,
               formula=None):
    """Runs `trials` random pixels of a mask shader: the microcode's oC0 (the mask in x, y and z; the
    alpha) against the formulas of light_formulas.py.
        mask       the whole shader over every regime, against mask_0f99 / mask_1b4b (or `formula`)
        mask-near  0F99... where only its near block runs (d < split[0]), against mask_near alone
        mask-far   where only the far block decides (0F99...: split[0] + split[2] < d < split[1]; 1B4B...:
                   beyond its start depth and before its fade), against mask_far alone
    A trial: a random pixel and depth; screen-to-world rows that put it at a chosen distance; random
    cascade rows; procedural depth maps and rotation texture (a value per texel); for 0F99... in half of
    the trials a random literal table (`literals`, with `taps` the function that pairs its offsets) and
    in a quarter maps that hold the compared depth itself. `text`: the shader text in the place of the
    dump (the tests' hand-written shader). Agreement as in check(), over all four components."""
    text = text if text is not None else ue.load(dumps, shader, "frag")
    res = Result()
    for k, case in enumerate(_mask_cases(family, shader, trials, seed, literals, taps)):
        m = ue.Machine(text, case.consts, case.sampler, regs={r: list(v) for r, v in case.regs.items()},
                       tex_size=case.tex_size)
        got = list(m.run()["oC0"])
        if family == "mask-near":
            value, alpha = case.near_alone()
        elif family == "mask-far":
            value, alpha = case.far_alone()
        else:
            out = (formula or (lf.mask_0f99 if case.near_alone else lf.mask_1b4b))(**case.args)
            value, alpha = out if isinstance(out, tuple) else (out, 1.0)
        want = [value, value, value, alpha]
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--multi-sun", action="store_true",
                    help="the two- and four-light pairs of the terrain, object, trunk, plant and leaf families with the sun "
                         "in slot 1 to 3 and dark lanterns, against their family's one-light function")
    ap.add_argument("--multi-lanterns", action="store_true",
                    help="every multi-light pair with lit lanterns before the game's sun slot, against the one-light "
                         "function plus local_lights")
    ap.add_argument("--all", action="store_true", help="every pair of FAMILIES, then the mask shaders (MASK_FAMILIES)")
    ap.add_argument("--family", choices=sorted(FAMILIES) + sorted(MASK_FAMILIES))
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
        for family, shaders in MASK_FAMILIES.items():
            for ps in shaders:
                print(f"{family}: ps {ps}")
        return 0
    if not args.all and not args.family and not args.multi_sun and not args.multi_lanterns:
        ap.print_usage(sys.stderr)
        return 2
    if not args.dumps.is_dir():
        print(f"no shader dumps at {args.dumps}", file=sys.stderr)
        return 2
    if args.multi_sun or args.multi_lanterns:
        bad = 0
        if args.multi_sun:
            n = print_multi_sun(args.trials, args.seed, args.dumps)
            print(f"{n} pairs disagree at the slot the game puts the sun in ({args.trials} trials each, tolerance "
                  f"{TOLERANCE:g} relative)")
            bad += n
        if args.multi_lanterns:
            n = print_multi_lanterns(args.trials, args.seed, args.dumps)
            print(f"{n} pairs disagree with lanterns before the game's sun slot ({args.trials} trials each, tolerance "
                  f"{TOLERANCE:g} relative)")
            bad += n
        return 0 if bad == 0 else 1
    want = args.shader.upper().replace("0X", "") if args.shader else None
    todo = [(f, ps, vs) for f, pairs in FAMILIES.items() for ps, vs in pairs
            if (args.all or f == args.family) and (want is None or ps == want)]
    masks = [(f, ps) for f, shaders in MASK_FAMILIES.items() for ps in shaders
             if (args.all or f == args.family) and (want is None or ps == want)]
    if not todo and not masks:
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
    if todo:
        print(f"{len(todo) - bad} of {len(todo)} pairs agree in every trial ({args.trials} trials each, tolerance {TOLERANCE:g} relative)")
    bad_masks = 0
    for family, ps in masks:
        res = check_mask(family, ps, args.trials, args.seed, dumps=args.dumps)
        print(f"{family}: ps {ps}: {res.line()}")
        if not res.ok:
            bad_masks += 1
            if res.first_bad:
                k, got, want_ = res.first_bad
                print(f"    first disagreement, trial {k}: microcode {got} formula {want_}")
    if masks:
        print(f"{len(masks) - bad_masks} of {len(masks)} mask checks agree in every trial ({args.trials} trials each, "
              f"tolerance {TOLERANCE:g} relative)")
    return 0 if bad + bad_masks == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
