"""Checks the post chain's formulas of light_formulas.py against the shaders' microcode.

Usage: post_check.py [--trials N] [--dumps DIR]          (all eight: exit code 0 only when every value agrees)
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
    function of the coordinate on both sides, which no addressing could fail);
  - the formulas take pixel positions and unnormalised textures (light_formulas, "the post chain"): the
    trial's texture is 64 x 64 texels, an interpolator u is the position u * 64 (u * 32 for the reduce, whose
    target is half its source).
What a trial cannot reach: the targets' clamps and the 10-bit rounding (the render target's, not the
microcode's), single precision, the filter of a fetch (the sampler state's, not the microcode's: the field is
continuous), whether param_gen hands the downsample an integral position (--bias adds to the position on both
sides), and L = 0 in the bright pass (0 * (1 / 0) in the microcode; the formula's scene term is 0 there).
"""
import argparse
import math
import random
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

    def __init__(self, got, want, coords=None):
        self.got, self.want, self.coords = list(got), list(want), coords


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
         47: [rng.choice([50.0, rng.uniform(0.2, 3)])] * 4, 77: [rng.uniform(-0.3, 0.3), rng.uniform(0.3, 2.5), 0, 0],
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
}


def check_text(shader, text, trials=TRIALS, seed=SEED, formula=None, tol=TOLERANCE, **opts):
    """One post shader's check on a dump's text. shader: the hash, which names the trial's body and the
    formula (`formula` in its place: a wrong form for the tests). opts: bias (the downsample: added to the
    pixel position on both sides), texel (the composite: another addressing of the table, u -> texel)."""
    body, reference = CHECKS[shader]
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
    """One shader of light_formulas.POST_SHADERS, by hash, on its dump."""
    return check_text(shader, ue.load(dumps, shader, "frag"), trials, **kw)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--shader", help="only this shader (16 hex digits)")
    ap.add_argument("--trials", type=int, default=TRIALS)
    ap.add_argument("--dumps", type=Path, default=DUMPS)
    ap.add_argument("--bias", type=float, default=0.0, help="the downsample: added to the pixel position (0 or 0.5)")
    ap.add_argument("--list", action="store_true", help="print what would be checked and exit")
    args = ap.parse_args(argv)
    want = args.shader.upper().replace("0X", "") if args.shader else None
    shaders = [s for s in lf.POST_SHADERS if want in (None, s.hash)]
    if args.list:
        for s in shaders:
            print(f"ps {s.hash} {s.role}: light_formulas.{CHECKS[s.hash][1].__name__}")
        return 0
    if not args.dumps.is_dir():
        print(f"no shader dumps at {args.dumps}", file=sys.stderr)
        return 2
    if not shaders:
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
    print(f"{good} of {len(shaders)} post shaders agree in every value ({args.trials} trials each, 4 values a trial, "
          f"tolerance {TOLERANCE:g} relative)")
    return 0 if good == len(shaders) else 1


if __name__ == "__main__":
    sys.exit(main())
