"""Checks the fog and sky formulas of light_formulas.py against the shaders' microcode.

Usage: fog_check.py [--trials N] [--dumps DIR]                 (everything: exit code 0 only when all agree)
       fog_check.py --only pixel|vertex|apply|sky [--shader HASH] [--trials N]
       fog_check.py --list

Four checks, each on random inputs run through ucode_eval.Machine:

pixel   The fog block of every pixel shader of light_formulas.FOG_SHADERS against lf.fog, as a differential:
        run A with the block's predicate off (c67.x = 1e9) gives the colour before fog times the output
        scale; run B with fog on must be lf.fog of that colour times the same scale, alpha untouched. The
        scale is c20.w, times c26.xyz where the dump reads c26, times the cross-fade for 2F8A...; in the
        blended form the in-scatter is scaled by the output alpha over c26.w. The world position is the
        interpolator FOG_SHADERS names; its angle and distance must also be the two table coordinates the
        microcode fetched. A pixel inside the fog start must come out of run B bit for bit as out of run A
        without a table fetch. The trials are those of the research script this file came from
        (out/tests/fog-research/fog_check.py: seed 1234 per shader, the same draws in the same order), so
        its counts are reproduced; what differs: the two tables are real tables (random texels, read with
        lf.table_1d, so the addressing is part of what is compared), every other texture is light_check's
        fixed pseudo-random one, and the literal registers are all 1 (the research drew 1 or 0.875 to
        show that the 1 of `1 - T` is a literal; the formula's 1 is 1).
        The two water shaders run with their own literal table (WATER_LITERALS, from the GPU log), because
        their kill and their nested predicate compare literals: trials they kill, and trials whose outer
        predicate keeps the pixel out of the block, are counted apart and are no evidence.
vertex  The trees' vertex form (VERTEX_CHECKED) against lf.vertex_fog: the vertex shader's two exported
        interpolators and the coordinates of its two fetches, from the world position it exports.
apply   The tree pixel shaders with bool b132 set and a random factor and add in the two interpolators,
        against lf.fog_apply of the family's formula (light_check's drivers supply the lit colour).
sky     Pixel shader F760... against lf.sky on random directions and on SKY_SPECIAL (the lat-long seam, the
        poles, the horizon, the octant boundaries of the arctangent), with the shader's literal table; the
        three fetch coordinates are compared too. The machine runs with zero_product (ucode_eval.py): at the
        two poles instructions 17 and 19 multiply 0 by 1 / 0.

What a trial switches off or cannot reach (docs/native-renderer/lighting-formulas.md, "Fog and sky"): the
ground-fog volume of bool b129 and the cube map of b134 are clear everywhere; the literals of the 38 pixel
shaders are not theirs; single precision is not modelled.
"""
import argparse
import math
import random
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import light_check as lc  # noqa: E402
import light_formulas as lf  # noqa: E402
import ucode_eval as ue  # noqa: E402

DUMPS = lc.DUMPS
TOLERANCE = lc.TOLERANCE      # pixel, vertex, apply: relative, per channel
SKY_TOLERANCE = 1e-9          # the sky: formula and microcode are the same arithmetic in double precision
COORD_TOLERANCE = 1e-9        # table coordinates

# The water shaders: bool-free literal comparisons decide their kill and their outer predicate, so the
# common literal value 1 would keep every trial out of the fog block. c254 and c255 at every execution of
# the two in frame 1116 of gpu_shadow_bridge2.log (24 draws each): (0, 2, 0, 0) and (0.5, -1, 1, -2).
WATER = ("A17D8AEC3A817D45", "F6D98C7B4D98438B")
WATER_LITERALS = {254: [0.0, 2.0, 0.0, 0.0], 255: [0.5, -1.0, 1.0, -2.0]}


_TEX_SIZE = {slot: (1024, 1024) for slot in range(32)}


def _rel(got, want):
    return max(abs(g - w) / max(1e-6, abs(g), abs(w)) for g, w in zip(got, want))


def _close(a, b, tol=COORD_TOLERANCE):
    return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


# --- the static scan --------------------------------------------------------------------------------

def scan_text(text):
    """What a pixel shader's dump says about fog: pixel_form (it fetches tf4 and tf5 with tfetch1D and
    reads c67), c26 (it reads c26), bools (the bool constants it names, sorted)."""
    consts = {int(x) for x in re.findall(r"\bc(\d+)\b", text)}
    t1d = {int(x) for x in re.findall(r"tfetch1D[^\n]*tf(\d+)", text)}
    return dict(pixel_form=4 in t1d and 5 in t1d and 67 in consts, c26=26 in consts,
                bools=sorted({int(x) for x in re.findall(r"\bb(\d+)\b", text)}))


def scan(dumps=DUMPS):
    """{hash: scan_text} of every pixel shader dump of the folder."""
    out = {}
    for path in sorted(Path(dumps).glob("shader_*.ucode.frag")):
        out[path.name.split("_")[1].split(".")[0]] = scan_text(path.read_text(errors="replace"))
    return out


# --- the pixel form ---------------------------------------------------------------------------------

class PixelResult:
    def __init__(self):
        self.trials = 0
        self.fog = self.fog_ok = 0              # trials that fetched the tables / whose colour agrees
        self.alpha_ok = self.coords_ok = 0      # of the fog trials: alpha unchanged / both coordinates agree
        self.near = self.near_ok = 0            # inside the start: no fetch / run B equals run A bit for bit
        self.killed = 0                         # the microcode discarded the pixel (water)
        self.unreached = self.unreached_ok = 0  # beyond the start but kept out of the block (water)
        self.after_between = 0                  # fog trials whose factor after the block is strictly in (0, 1)
        self.blend_between = 0                  # fog trials whose blend from c68 to the table is strictly in (0, 1)
        self.errors = {}
        self.worst = 0.0
        self.first_bad = None
        self.may_miss = False

    @property
    def ok(self):
        return (self.trials > 0 and not self.errors and self.fog_ok == self.fog and self.alpha_ok == self.fog
                and self.coords_ok == self.fog and self.near_ok == self.near and self.unreached_ok == self.unreached
                and (self.may_miss or self.killed + self.unreached == 0)
                and self.fog + self.near + self.killed + self.unreached == self.trials)

    def line(self):
        text = (f"fog {self.fog_ok}/{self.fog} ({self.blend_between} inside the near blend), near "
                f"{self.near_ok}/{self.near}, alpha {self.alpha_ok}/{self.fog}, coordinates {self.coords_ok}/{self.fog}, "
                f"worst {self.worst:.1e}")
        if self.killed or self.unreached:
            text += f", killed {self.killed}, outside the block {self.unreached_ok}/{self.unreached}"
        if self.errors:
            text += f", NOT RUN {self.errors}"
        return text


def check_pixel_text(text, world, form, c26, trials=200, seed=1234, formula=lf.fog, bools=None, literals=None,
                     setup=None, after=None, may_miss=False, tol=TOLERANCE):
    """The pixel check on a dump's text. world: the interpolator that is the world position; form: "plain"
    or "alpha"; c26: the colour is multiplied by c26.xyz with the output scale; bools: the bool constants a
    trial may set (default: those the text names; 129 and 134 stay clear); literals: {register: values} in
    the place of the common 1; setup(rng, consts, regs): a shader's own inputs; after(consts, regs): a
    factor on the colour after the block; may_miss: killed and unreached trials are no failure."""
    if bools is None:
        bools = scan_text(text)["bools"]
    rng, aux = random.Random(seed), random.Random(seed + 1)
    res = PixelResult()
    res.may_miss = may_miss
    for k in range(trials):
        res.trials += 1
        rng.choice([1.0, 0.875])        # the research's literal value: drawn to keep its sequence, not used
        c = {r: [rng.uniform(0.1, 1.0) for _ in range(4)] for r in range(256)}
        for r in range(248, 256):
            c[r] = [1.0] * 4
        c[19] = [rng.uniform(-50, 50) for _ in range(3)] + [1.0]
        n = [rng.uniform(-1, 1) for _ in range(3)]
        ln = math.sqrt(sum(x * x for x in n)) or 1.0
        c[66] = [x / ln for x in n] + [0.0]
        c[67] = [rng.uniform(1, 30), 1900.0, rng.uniform(0.02, 1.0), rng.uniform(0.0005, 0.01)]
        c[68] = [rng.uniform(0.1, 1.0) for _ in range(3)] + [0.0]
        c[28] = [rng.uniform(0.3, 1.0), rng.uniform(0.3, 1.5), 0.0, 0.0]
        c[20] = [rng.uniform(0, 0.1) for _ in range(3)] + [rng.choice([16.0, 1.0, 2.5])]
        regs = {r: [rng.uniform(-80, 80) for _ in range(4)] for r in range(16)}
        if rng.random() < 0.25:     # a near pixel: every interpolator within a few units of the eye, the start beyond
            regs = {r: [rng.uniform(-4, 4) for _ in range(4)] for r in range(16)}
            c[19] = [rng.uniform(-2, 2) for _ in range(3)] + [1.0]
            c[67][0] = rng.uniform(15, 30)
        on = {b for b in bools if rng.random() < 0.5 and b not in (129, 134)}
        # Everything below draws from the second generator, so that the first one's sequence is the research's.
        tf4 = lf.table_1d([[aux.uniform(0.05, 3.0) for _ in range(3)] for _ in range(64)])
        tf5 = lf.table_1d([[aux.random() for _ in range(3)] for _ in range(256)])
        # The research's c67.z (0.02 to 1) completes the blend from c68 within 50 units of the start, which
        # nearly every far trial is beyond: every other trial gets a slope that leaves the pixel inside it.
        if aux.random() < 0.5:
            c[67][2] = aux.uniform(0.001, 0.02)
        if literals:
            c.update({r: list(v) for r, v in literals.items()})
        if setup:
            setup(aux, c, regs)

        def sampler(slot, coords, attrs):
            if slot == 4:
                return tf4(coords[0]) + [1.0]
            if slot == 5:
                return tf5(coords[0]) + [1.0]
            return lc.tex(slot, coords)

        def go(consts):
            m = ue.Machine(text, consts, sampler, regs={r: list(v) for r, v in regs.items()}, bools=on,
                           tex_size=_TEX_SIZE)      # getWeights2D of the blended form's shadow map block
            return m.run()["oC0"], m

        try:
            off = dict(c)
            off[67] = [1e9] + c[67][1:]
            A, mA = go(off)
            B, mB = go(c)
        except (NotImplementedError, KeyError, ValueError) as e:
            key = f"{type(e).__name__}: {str(e)[:60]}"
            res.errors[key] = res.errors.get(key, 0) + 1
            continue
        if mA.killed or mB.killed:
            res.killed += 1
            if mA.killed != mB.killed:
                res.errors["killed in one run only"] = res.errors.get("killed in one run only", 0) + 1
            continue
        if not all(math.isfinite(x) for x in A + B):
            res.errors["non-finite"] = res.errors.get("non-finite", 0) + 1
            continue
        if any(f[1] in (4, 5) for f in mA.fetches):
            res.errors["run A fetched the tables"] = res.errors.get("run A fetched the tables", 0) + 1
            continue
        f4 = [f for f in mB.fetches if f[1] == 4]
        f5 = [f for f in mB.fetches if f[1] == 5]
        P, eye = regs[world][:3], c[19][:3]
        coords = lf.fog_coords(P, eye, c[66], c[67])
        if not f4 and not f5:
            same = A == B
            if coords is None:
                res.near += 1
                res.near_ok += same
            else:
                res.unreached += 1
                res.unreached_ok += same
            if not same and res.first_bad is None:
                res.first_bad = (k, B, A)
            continue
        res.fog += 1
        res.alpha_ok += A[3] == B[3]
        res.blend_between += coords is not None and 0.0 < coords[2] < 1.0
        if coords is not None and f4 and f5 and _close(coords[0], f4[0][2][0]) and _close(coords[1], f5[0][2][0]):
            res.coords_ok += 1
        factor = after(c, regs) if after else 1.0
        if 0.0 < factor < 1.0:
            res.after_between += 1
        scale = [c[20][3] * (c[26][i] if c26 else 1.0) * factor for i in range(3)]
        alpha = B[3] / (c[26][3] if c26 else 1.0) if form == "alpha" else 1.0
        if not all(scale):
            res.errors["zero output scale"] = res.errors.get("zero output scale", 0) + 1
            continue
        fogged = formula([A[i] / scale[i] for i in range(3)], P, eye, c[28], c[66], c[67], c[68], tf4, tf5,
                         scatter_scale=alpha)
        want = [fogged[i] * scale[i] for i in range(3)]
        err = _rel(B[:3], want)
        res.worst = max(res.worst, err)
        if err <= tol:
            res.fog_ok += 1
        elif res.first_bad is None:
            res.first_bad = (k, B[:3], want)
    return res


def _water_setup(rng, c, regs):
    # c141: the opacity ramp (x, y) and the threshold (w) the shaders' kill and outer predicate compare with:
    # values that kill some pixels, keep some out of the block and let the rest through.
    threshold = rng.uniform(1.0, 1.2) if rng.random() < 0.15 else rng.uniform(0.0, 0.3)
    c[141] = [rng.uniform(0.0, 0.8), rng.uniform(0.4, 1.4), 0.0, threshold]


def _far_fade_setup(rng, c, regs):
    # 2F8A...: the cross-fade sat(d^2 * c31.x + c31.y), kept above zero and often below one.
    c[31] = [-rng.uniform(0.0, 1e-5), rng.uniform(0.7, 1.3), 0.0, 0.0]


def _far_fade(c, regs):
    v = [c[19][i] - regs[1][i] for i in range(3)]
    return lf.sat(lf.dot(v, v) * c[31][0] + c[31][1])


_PIXEL_EXTRA = {
    "A17D8AEC3A817D45": dict(literals=WATER_LITERALS, setup=_water_setup, may_miss=True),
    "F6D98C7B4D98438B": dict(literals=WATER_LITERALS, setup=_water_setup, may_miss=True),
    "2F8A0341E8C09698": dict(setup=_far_fade_setup, after=_far_fade),
}


def check_pixel(shader, trials=200, seed=1234, dumps=DUMPS, formula=lf.fog, tol=TOLERANCE):
    """One pixel shader of lf.FOG_SHADERS, by hash."""
    info = next(s for s in lf.FOG_SHADERS if s.hash == shader)
    text = ue.load(dumps, shader, "frag")
    facts = scan_text(text)
    return check_pixel_text(text, info.world, info.form, facts["c26"], trials, seed, formula, bools=facts["bools"],
                            tol=tol, **_PIXEL_EXTRA.get(shader, {}))


# --- the vertex form --------------------------------------------------------------------------------

# The vertex shaders whose literal tables light_check.py has (captures; its _OBJECT_VS and _LEAF_VS).
VERTEX_CHECKED = ("475EC9F795E5EDBB", "A5846836C90E1192", "D7DD31B9BF194DE8", "2D40B53C926109BE")


class VertexResult:
    def __init__(self):
        self.trials = 0
        self.fog = self.fog_ok = self.coords_ok = 0
        self.near = self.near_ok = 0
        self.errors = {}
        self.worst = 0.0
        self.first_bad = None

    @property
    def ok(self):
        return (self.trials > 0 and not self.errors and self.fog_ok == self.fog and self.coords_ok == self.fog
                and self.near_ok == self.near and self.fog + self.near == self.trials)

    def line(self):
        text = (f"fog {self.fog_ok}/{self.fog}, near {self.near_ok}/{self.near}, coordinates {self.coords_ok}/{self.fog}, "
                f"worst {self.worst:.1e}")
        return text + (f", NOT RUN {self.errors}" if self.errors else "")


def _smooth(rng, channels):
    """A random smooth function of two coordinates per channel (a texture whose value does not jump when a
    coordinate differs in its last bit): k0 + k1 x + k2 y + k3 x y, positive on the unit square."""
    k = [[rng.uniform(0.2, 1.0), rng.uniform(-0.15, 0.6), rng.uniform(-0.15, 0.6), rng.uniform(-0.3, 0.6)]
         for _ in range(channels)]
    return lambda x, y: [a + b * x + c * y + d * x * y for a, b, c, d in k]


def check_vertex(vs, trials=200, seed=1, dumps=DUMPS, formula=lf.vertex_fog, tol=TOLERANCE):
    """One vertex shader of VERTEX_CHECKED: its fog interpolators against formula(P, eye, c8.x, c110, c111,
    tf16, tf17), P the world position it exports. Every register is random, the literals are the shader's
    (light_check.py), tf16 is a random 256-texel table and tf17 a random smooth function."""
    info = next(s for s in lf.FOG_VERTEX_SHADERS if s.hash == vs)
    literals = (lc._OBJECT_VS.get(vs) or lc._LEAF_VS[vs])["literals"]
    text = ue.load(dumps, vs, "vert")
    rng = random.Random(seed)
    res = VertexResult()
    for k in range(trials):
        res.trials += 1
        c = {r: [rng.uniform(-1, 1) for _ in range(4)] for r in range(256)}
        c[7] = [rng.uniform(-50, 50) for _ in range(3)] + [rng.uniform(0.5, 2.0)]
        c[8] = [rng.uniform(0.5, 2.0), 0.0, 0.0, 0.0]
        c[9] = [rng.uniform(-50, 50) for _ in range(3)] + [1.0]
        c[110] = lc.unit(rng) + [0.0]
        c[111] = [rng.uniform(1, 30), 1900.0, rng.uniform(0.02, 1.0), rng.uniform(0.0005, 0.01)]
        if rng.random() < 0.25:         # a near vertex: the eye within a few units of the tree, the start beyond
            c[9] = [rng.uniform(-3, 3) for _ in range(3)] + [1.0]
            c[111][0] = rng.uniform(15, 30)
        c.update({r: list(v) for r, v in literals.items()})
        tf16 = lf.table_1d([[rng.random() for _ in range(3)] for _ in range(256)])
        tf17 = _smooth(rng, 3)

        def sampler(slot, coords, attrs):
            if slot == 16:
                return tf16(coords[0]) + [1.0]
            if slot == 17:
                return tf17(coords[0], coords[1]) + [1.0]
            raise KeyError(f"vertex texture tf{slot}")

        try:
            regs, _, m = lc.run_vs(text, c, sampler=sampler, regs={0: [float(rng.randrange(1000)), 0, 0, 0]},
                                   vfetch=lc.Vertex(rng))
        except (NotImplementedError, KeyError, ValueError) as e:
            key = f"{type(e).__name__}: {str(e)[:60]}"
            res.errors[key] = res.errors.get(key, 0) + 1
            continue
        P, got_factor, got_add = regs[info.world][:3], regs[info.factor][:3], regs[info.add][:3]
        if not all(math.isfinite(x) for x in P + got_factor + got_add):
            res.errors["non-finite"] = res.errors.get("non-finite", 0) + 1
            continue
        seen = {}

        def t16(u):
            seen[16] = (u,)
            return tf16(u)

        def t17(a, t):
            seen[17] = (a, t)
            return tf17(a, t)

        factor, add = formula(P, c[9][:3], c[8][0], c[110], c[111], t16, t17)
        fetched = {f[1]: f[2] for f in m.fetches}
        if not fetched:
            res.near += 1
            if not seen and got_factor == [1.0] * 3 == list(factor) and got_add == [0.0] * 3 == list(add):
                res.near_ok += 1
            elif res.first_bad is None:
                res.first_bad = (k, got_factor + got_add, list(factor) + list(add))
            continue
        res.fog += 1
        if (set(seen) == {16, 17} == set(fetched) and _close(seen[16][0], fetched[16][0])
                and _close(seen[17][0], fetched[17][0]) and _close(seen[17][1], fetched[17][1])):
            res.coords_ok += 1
        err = _rel(got_factor + got_add, list(factor) + list(add))
        res.worst = max(res.worst, err)
        if err <= tol:
            res.fog_ok += 1
        elif res.first_bad is None:
            res.first_bad = (k, got_factor + got_add, list(factor) + list(add))
    return res


# --- the tree pixel shaders' use of the pair --------------------------------------------------------------

# (family, pixel shader, vertex shader) of light_check's tree families, and the two interpolators.
APPLY_PAIRS = tuple((family, ps, vs) for family in ("tree-trunk", "tree-trunk-multi", "tree-leaf", "tree-leaf-multi")
                    for ps, vs in lc.FAMILIES[family])


def check_apply(family, ps, vs, trials=200, seed=1, dumps=DUMPS, formula=lf.fog_apply, tol=TOLERANCE):
    """A tree pixel shader with b132 set: light_check's case of the pair (the vertex fog off, so the vertex
    shader exports 1 and 0), the two fog interpolators replaced by a random factor and add; the colour must
    be formula(the family's formula, factor, add) times the output scale."""
    info = next(s for s in lf.FOG_VERTEX_SHADERS if s.hash == vs)
    ps_text, make_case = lc.DRIVERS[family](ps, vs, dumps)
    rng, aux = random.Random(seed), random.Random(seed + 1)
    res = lc.Result()
    for k in range(trials):
        case = make_case(rng)
        factor = [aux.uniform(0.05, 1.0) for _ in range(3)]
        add = [aux.uniform(0.0, 2.0) for _ in range(3)]
        regs = {r: list(v) for r, v in case.regs.items()}
        if regs[info.factor][:3] != [1.0] * 3 or regs[info.add][:3] != [0.0] * 3:
            raise AssertionError(f"{vs}: the vertex fog is not off in light_check's case")
        regs[info.factor], regs[info.add] = factor + [0.0], add + [0.0]
        m = ue.Machine(ps_text, case.consts, case.sampler, regs=regs, bools=set(case.bools) | {132})
        out = m.run()["oC0"]
        got = [out[i] / case.scale[i] for i in range(3)]
        want = formula(lc.FORMULAS[family](**case.args), factor, add)
        res.trials += 1
        finite = all(math.isfinite(x) for x in got) and all(math.isfinite(x) for x in want)
        err = _rel(got, want) if finite else math.inf
        if finite:
            res.worst = max(res.worst, err)
        if err <= tol:
            res.agree += 1
        elif res.first_bad is None:
            res.first_bad = (k, got, want)
    return res


# --- the sky ------------------------------------------------------------------------------------------

# Directions every sky check runs besides the random ones: (name, direction).
SKY_SPECIAL = (
    ("zenith", [0.0, 0.0, 1.0]),
    ("nadir", [0.0, 0.0, -1.0]),
    ("beside the zenith", [1e-6, -2e-6, 1.0]),
    ("beside the nadir", [-1e-6, 1e-6, -1.0]),
    ("horizon", [0.6, 0.8, 0.0]),
    ("seam, on -x", [-1.0, 0.0, 0.25]),
    ("seam, north of -x", [-1.0, 1e-7, 0.25]),
    ("seam, south of -x", [-1.0, -1e-7, 0.25]),
    ("seam, on -x at the horizon", [-1.0, 0.0, 0.0]),
    ("seam, on -x below the horizon", [-1.0, 0.0, -0.6]),
    ("+x", [1.0, 0.0, 0.0]),
    ("+y", [0.0, 1.0, 0.0]),
    ("-y", [0.0, -1.0, 0.0]),
    ("octant boundary, +x +y", [1.0, 1.0, 0.5]),
    ("octant boundary, -x +y", [-1.0, 1.0, 0.5]),
    ("octant boundary, -x -y", [-1.0, -1.0, -0.5]),
    ("octant boundary, +x -y", [1.0, -1.0, 0.2]),
    ("below the horizon", [0.3, -0.5, -0.4]),
)


class SkyResult(lc.Result):
    def __init__(self):
        super().__init__()
        self.coords_ok = 0
        self.bad_names = []     # the special directions that disagree

    @property
    def ok(self):
        return self.trials > 0 and self.agree == self.trials and self.coords_ok == self.trials

    def line(self):
        text = f"{self.agree}/{self.trials} agree, coordinates {self.coords_ok}/{self.trials}, worst relative difference {self.worst:.2e}"
        return text + (f", special directions that disagree: {self.bad_names}" if self.bad_names else "")


def check_sky(trials=200, seed=1, dumps=DUMPS, formula=lf.sky, zero_product=True, tol=SKY_TOLERANCE):
    """Pixel shader F760... against formula(n, block, tf4, tf13, tf14) on SKY_SPECIAL and `trials` random
    directions. Every register is random within the ranges the captures show, the literals are
    lf.SKY_LITERALS, tf4 is a random 64-texel table, tf13 and tf14 random smooth functions; bool b129 is
    clear. The interpolator is the direction times a random length (the shader normalises it)."""
    text = ue.load(dumps, lf.SKY_SHADER, "frag")
    rng = random.Random(seed)
    res = SkyResult()
    cases = [(name, list(d)) for name, d in SKY_SPECIAL] + [(None, lc.unit(rng)) for _ in range(trials)]
    for k, (name, d) in enumerate(cases):
        length = rng.uniform(100.0, 5000.0)
        interp = [x * length for x in d]
        c = {r: [rng.uniform(0.1, 1.0) for _ in range(4)] for r in range(256)}
        c.update({r: list(v) for r, v in lf.SKY_LITERALS.items()})
        c[19] = [rng.uniform(-50, 50) for _ in range(3)] + [1.0]
        c[20] = [0.0, 0.0, 0.0, rng.choice([16.0, 1.0, 2.5])]
        c[31] = [rng.uniform(50.0, 300.0), 0.0, 0.0, 0.0]
        c[46] = [rng.uniform(50.0, 300.0), 0.0, 0.0, 0.0]
        c[47] = [rng.choice([0.0, 1.0, rng.random()]), 0.0, 0.0, 0.0]
        c[64] = [rng.uniform(0.0, 0.002) for _ in range(3)] + [0.0]
        c[65] = [rng.uniform(0.0, 0.01) for _ in range(3)] + [0.0]
        c[66] = lc.unit(rng) + [0.0]
        c[67] = [rng.uniform(1, 30), rng.uniform(500.0, 3000.0), 1.0, 0.0005]
        tf4 = lf.table_1d([[rng.uniform(0.05, 3.0) for _ in range(3)] for _ in range(64)])
        tf13, tf14 = _smooth(rng, 4), _smooth(rng, 4)

        def sampler(slot, coords, attrs):
            if slot == 4:
                return tf4(coords[0]) + [1.0]
            return {13: tf13, 14: tf14}[slot](coords[0], coords[1])

        m = ue.Machine(text, c, sampler, regs={0: interp + [0.0]}, zero_product=zero_product)
        out = m.run()["oC0"]
        got = [out[i] / c[20][3] for i in range(3)]
        block = dict(c31x=c[31][0], c46x=c[46][0], c47x=c[47][0], c64=c[64][:3], c65=c[65][:3], c66=c[66][:3],
                     c67y=c[67][1])
        want = formula(interp, block, tf4, tf13, tf14)
        res.trials += 1
        finite = all(math.isfinite(x) for x in got) and all(math.isfinite(x) for x in want)
        err = _rel(got, want) if finite else math.inf
        if finite:
            res.worst = max(res.worst, err)
        fetched = {f[1]: f[2] for f in m.fetches}
        u, v = lf.sky_latlong(interp)
        a = lf.sat(lf.dot(lf.normalize(interp), c[66][:3]))
        coords = (finite and set(fetched) == {4, 13, 14} and _close(fetched[4][0], a)
                  and all(_close(fetched[s][0], u) and _close(fetched[s][1], v) for s in (13, 14)))
        res.coords_ok += bool(coords)
        if err <= tol and out[3] == 1.0:
            res.agree += 1
        else:
            if res.first_bad is None:
                res.first_bad = (name or k, got, want)
            if name:
                res.bad_names.append(name)
    return res


# --- the command line -------------------------------------------------------------------------------------

def _bad(res):
    if res.first_bad:
        k, got, want = res.first_bad
        print(f"    first disagreement, trial {k}: microcode {got} formula {want}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", choices=("pixel", "vertex", "apply", "sky"))
    ap.add_argument("--shader", help="with --only pixel or vertex: only this shader (16 hex digits)")
    ap.add_argument("--trials", type=int, default=200)
    ap.add_argument("--dumps", type=Path, default=DUMPS)
    ap.add_argument("--list", action="store_true", help="print what would be checked and exit")
    args = ap.parse_args(argv)
    want = args.shader.upper().replace("0X", "") if args.shader else None
    pixels = [s for s in lf.FOG_SHADERS if want in (None, s.hash)] if args.only in (None, "pixel") else []
    vertices = [vs for vs in VERTEX_CHECKED if want in (None, vs)] if args.only in (None, "vertex") else []
    applies = list(APPLY_PAIRS) if args.only in (None, "apply") else []
    sky = args.only in (None, "sky")
    if args.list:
        for s in pixels:
            print(f"pixel: ps {s.hash} {s.form}, world position r{s.world}, family {s.family}")
        for vs in vertices:
            print(f"vertex: vs {vs}")
        for family, ps, vs in applies:
            print(f"apply: {family}: ps {ps} vs {vs}")
        if sky:
            print(f"sky: ps {lf.SKY_SHADER}, {len(SKY_SPECIAL)} special directions")
        return 0
    if not args.dumps.is_dir():
        print(f"no shader dumps at {args.dumps}", file=sys.stderr)
        return 2
    if not (pixels or vertices or applies or sky):
        print("no shader matches", file=sys.stderr)
        return 2
    bad = 0
    if pixels:
        total, research = PixelResult(), PixelResult()
        good = 0
        for s in pixels:
            res = check_pixel(s.hash, args.trials, dumps=args.dumps)
            print(f"ps {s.hash} {s.form:5s} world r{s.world}: {res.line()}")
            passed = res.ok and res.fog > 0
            good += passed
            if not passed:
                _bad(res)
            for t in (total,) + ((research,) if s.hash not in WATER else ()):
                for field in ("fog", "fog_ok", "alpha_ok", "coords_ok", "near", "near_ok", "killed", "unreached", "unreached_ok",
                              "blend_between"):
                    setattr(t, field, getattr(t, field) + getattr(res, field))
                t.worst = max(t.worst, res.worst)
        bad += len(pixels) - good
        print(f"{good} of {len(pixels)} pixel shaders agree in every trial ({args.trials} trials each, tolerance "
              f"{TOLERANCE:g} relative): {total.line()}")
        if want is None:
            print(f"without the two water shaders (the 38 of the research): {research.line()}")
            found = {h for h, facts in scan(args.dumps).items() if facts["pixel_form"]}
            listed = {s.hash for s in lf.FOG_SHADERS} | set(lf.FOG_NOT_OF_THE_FORM)
            if found != listed:
                bad += 1
                print(f"THE LIST IS NOT THE DUMPS': only in the dumps {sorted(found - listed)}, only in the list "
                      f"{sorted(listed - found)}")
            else:
                print(f"the dumps' pixel shaders with the block are the {len(lf.FOG_SHADERS)} of the list and "
                      f"{', '.join(lf.FOG_NOT_OF_THE_FORM)} (not of the form, not checked)")
    for vs in vertices:
        res = check_vertex(vs, args.trials, dumps=args.dumps)
        print(f"vs {vs}: {res.line()}")
        if not (res.ok and res.fog > 0):
            bad += 1
            _bad(res)
    for family, ps, vs in applies:
        res = check_apply(family, ps, vs, args.trials, dumps=args.dumps)
        print(f"apply {family}: ps {ps} vs {vs}: {res.line()}")
        if not res.ok:
            bad += 1
            _bad(res)
    if sky:
        res = check_sky(args.trials, dumps=args.dumps)
        print(f"sky {lf.SKY_SHADER}: {res.line()} ({len(SKY_SPECIAL)} special directions and {args.trials} random, "
              f"tolerance {SKY_TOLERANCE:g} relative)")
        if not res.ok:
            bad += 1
            _bad(res)
    print("all agree" if bad == 0 else f"{bad} checks DISAGREE")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
