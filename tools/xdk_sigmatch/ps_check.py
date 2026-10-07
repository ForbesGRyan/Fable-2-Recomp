"""The eight terrain pixel shaders run through ucode_eval.py on random inputs (evidence of the
materials sub-project's Task 9, tracked by the lighting sub-project with the dump folder's path and
the trial count as the only changes; `python ps_check.py` prints the report `ps_check.txt` of Task 9
holds, but for its informative count of negative light factors, which varies from run to run because
`tex` below is seeded with Python's salted `hash`),
against the reading (frame-map section 13, "Terrain"). Literal constants (c252..c255 of the pixel
bank) are in no capture, so every formula below names the literal it uses and the run gives the
"one" literal a value other than 1 in half of the trials: a formula that used the wrong register
would fail there.

Checked per layer shader (trials with the cliff block on and off, bool b135 on and off):
  alpha   oC0.w = (one - sat(d2 * c31.x + c31.y)) * weight
  ambient with the light colours zero and the fog predicate off:
          oC0.xyz = A * c11.x * (base' * L + c20.xyz) * c20.w,  base' = pow(tf3, c21.w) * c21.xyz,
          L = c22 + (one - c22) * t2, t2 taken from a second run of the same pixel with c22 = 0
  unlit   c22 = one, c20.xyz = 0, c11.x = 1, c20.w = 1, lights zero, fog off: oC0.xyz = A * base' * one
  light   one light colour at a time: (oC0.xyz - ambient) / (A * c20.w) is that colour times one
          scalar, the same in the three channels (the scalar can be negative here: the cone and
          range terms of the extra lights get random constants)
  fog     with the predicate on and c28.x = 0 the colour is the colour without fog
A is tf0^2, or with the cliff block tf0^2 * s + (one - s) * (wy * tf13(o1)^2 + wx * tf13(o0)^2).
The far shaders: alpha 1; colour F^2 * (base' * L + c20) * c20.w (+ sun F^2 * c11.x * c87 * f),
times sat(d2 * c31.x + c31.y) for 2F8A."""
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ucode_eval as ue

DUMPS = Path(__file__).resolve().parents[2] / "out" / "shader_dump"


def tex(slot, coords):
    """A fixed pseudo-random texture: the same (slot, coords) gives the same four values in (0.05, 1)."""
    h = hash((slot,) + tuple(struct.pack("<d", c) for c in coords))
    rng = random.Random(h)
    return [rng.uniform(0.05, 1.0) for _ in range(4)]


def close(a, b, tol=2e-5):
    return all(abs(x - y) <= tol * max(1.0, abs(x), abs(y)) for x, y in zip(a, b))


def sat(x):
    return min(max(x, 0.0), 1.0)


# Per shader: which interpolator holds what (from the paired vertex shader's exports), and which
# literal components the reading names. "thr" is the literal the slope factor is compared with,
# "eps" the literal added to |nx| + |ny|, "one" the literal of every "1 - x".
LAYERS = {
    "9C2144E42E95A76F": dict(vs="C30A", detail=2, side0=0, side1=1, normal=3, world=4, base=5, weight=9, gen=10,
                             one=(255, 2), thr=(254, 1), eps=(255, 3), slope_reg=45, b135=False, lights=(87,)),
    "45243B758F9272AB": dict(vs="C30A", detail=2, side0=0, side1=1, normal=3, world=4, base=5, weight=9, gen=10,
                             one=(255, 1), thr=(254, 2), eps=(255, 3), slope_reg=45, b135=True, lights=(87, 88)),
    "04F23292B2F44077": dict(vs="C30A", detail=2, side0=0, side1=1, normal=3, world=4, base=5, weight=9, gen=10,
                             one=(255, 1), thr=(254, 1), eps=(255, 3), slope_reg=45, b135=True, lights=(87, 88, 89, 90)),
    "4755A2E4A05BA678": dict(vs="FB68", detail=0, normal=1, world=2, base=3, weight=7, gen=8,
                             one=(254, 3), b135=False, lights=(87,)),
    "AF1E16EE57E4BB06": dict(vs="FB68", detail=0, normal=1, world=2, base=3, weight=7, gen=8,
                             one=(254, 1), b135=True, lights=(87, 88)),
    "578342593A37DD35": dict(vs="FB68", detail=0, normal=1, world=2, base=3, weight=7, gen=8,
                             one=(254, 1), b135=True, lights=(87, 88, 89, 90)),
}
ALL_CONSTS = (0, 11, 19, 20, 21, 22, 23, 28, 31, 32, 33, 34, 35, 45, 48, 66, 67, 68, 80, 81, 82, 83, 87, 88, 89, 90,
              94, 96, 98, 99, 100, 101, 105, 107, 109, 112, 114)


def base_consts(rng, one):
    c = {i: [rng.uniform(0.1, 1.0) for _ in range(4)] for i in ALL_CONSTS}
    c[19] = [rng.uniform(-50, 50) for _ in range(3)] + [1.0]
    c[21] = [rng.uniform(0.5, 2.0) for _ in range(3)] + [rng.choice([1.0, 1.3, 2.0])]
    c[31] = [rng.uniform(0.0002, 0.001), rng.uniform(-1.5, 0.2), 0, 0]
    c[67] = [1e9, 0, 0.01, 0.01]              # fog predicate off: d - c67.x <= 0
    c[254] = [2.0, 0.0, -1.0, 1.0]
    c[255] = [0.0, -1.0, 1.0, 0.001]
    return c


def run_layer(hash_, cfg, rng, cliff, b135, trials_log):
    one = rng.choice([1.0, 0.875])
    c = base_consts(rng, one)
    # Literal idioms by shader; the named ones are then overwritten below.
    if cfg["vs"] == "C30A":
        if hash_.startswith("9C21"):
            c[254] = [2.0, 1.0, rng.uniform(1, 4), 0.0]
            c[255] = [0.0, -1.0, 1.0, 0.001]
        elif hash_.startswith("4524"):
            c[254] = [rng.uniform(1, 4), 2.0, 1.0, 0.0]
            c[255] = [0.0, 1.0, -1.0, 0.001]
        else:
            c[254] = [2.0, 1.0, rng.uniform(1, 4), 0.0]
            c[255] = [0.0, 1.0, -1.0, 0.001]
    else:
        if hash_.startswith("4755"):
            c[254] = [2.0, 0.0, -1.0, 1.0]
            c[255] = [rng.uniform(1, 4), 0, 0, 0]
        elif hash_.startswith("AF1E"):
            c[254] = [0.0, 1.0, -1.0, rng.uniform(1, 4)]
            c[255] = [2.0, 0, 0, 0]
        else:
            c[254] = [0.0, 1.0, -1.0, 2.0]
            c[255] = [rng.uniform(1, 4), 0, 0, 0]
    c[cfg["one"][0]][cfg["one"][1]] = one
    thr = eps = None
    regs = {}
    n = [rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(0.05, 1)]
    world = [rng.uniform(-60, 60) for _ in range(3)]
    weight = rng.uniform(-0.1, 1.0)
    regs[cfg["normal"]] = n + [0]
    regs[cfg["world"]] = world + [0]
    regs[cfg["detail"]] = [rng.uniform(-9, 9), rng.uniform(-9, 9), 0, 0]
    regs[cfg["base"]] = [rng.random(), rng.random(), 0, 0]
    regs[cfg["weight"]] = [rng.random(), rng.random(), rng.random(), weight]
    regs[cfg["gen"]] = [rng.uniform(0, 1120), rng.uniform(0, 720), 0, 0]
    for r in range(11):
        regs.setdefault(r, [rng.uniform(-1, 1) for _ in range(4)])
    s = None
    if "slope_reg" in cfg:
        regs[cfg["side0"]] = [rng.uniform(-9, 9), rng.uniform(-9, 9), 0, 0]
        regs[cfg["side1"]] = [rng.uniform(-9, 9), rng.uniform(-9, 9), 0, 0]
        thr = c[cfg["thr"][0]][cfg["thr"][1]] = 1.0
        eps = c[cfg["eps"][0]][cfg["eps"][1]]
        # s = sat(|nz| * c45.y + c45.x): choose c45 so the block runs (s < thr) or not (s >= thr).
        c[45] = [rng.uniform(-0.5, 0.2), rng.uniform(0.2, 0.9), 0, 0] if cliff else [1.0, 1.0, 0, 0]
        s = sat(abs(n[2]) * c[45][1] + c[45][0])
        if (s < thr) != cliff:
            return None
    bools = {135} if b135 else set()
    text = ue.load(DUMPS, hash_, "frag")

    def sampler(slot, coords, attrs):
        if slot == 9:   # the four shadow taps differ by their texel offsets
            return tex(900 + 2 * int(float(attrs.get("OffsetX", 0))) + int(float(attrs.get("OffsetY", 0))), coords)
        return tex(slot, coords)

    def go(consts, r=regs):
        m = ue.Machine(text, consts, sampler, regs={k: list(v) for k, v in r.items()}, bools=bools)
        out = m.run()["oC0"]
        return out, m

    # Expected pieces.
    D = tex(0, tuple(regs[cfg["detail"]][:2]))
    A = [x * x for x in D[:3]]
    if s is not None and cliff:
        e1 = tex(13, tuple(regs[cfg["side1"]][:2]))
        e0 = tex(13, tuple(regs[cfg["side0"]][:2]))
        den = abs(n[0]) + abs(n[1]) + eps
        wx, wy = abs(n[0]) / den, abs(n[1]) / den
        A = [d * s + (one - s) * (wy * a * a + wx * b * b) for d, a, b in zip(A, e1[:3], e0[:3])]
    B = tex(3, tuple(regs[cfg["base"]][:2]))
    d2 = sum((e - w) ** 2 for e, w in zip(c[19][:3], world))
    fade = one - sat(d2 * c[31][0] + c[31][1])

    def basep(cc):
        return [B[i] ** cc[21][3] * cc[21][i] for i in range(3)]

    res = {}
    lights_off = dict(c)
    for k in (87, 88, 89, 90):
        lights_off[k] = [0.0, 0.0, 0.0, 0.0]
    # t2 from a probe run: c22 = 0, c20 = (0,0,0,1), c21 = (1,1,1,gamma), c11.x = 1.
    probe = dict(lights_off)
    probe[22] = [0.0] * 4
    probe[20] = [0.0, 0.0, 0.0, 1.0]
    probe[11] = [1.0] * 4
    out, _ = go(probe)
    bp = basep(probe)
    t2 = [out[i] / (A[i] * bp[i]) / one for i in range(3)]
    res["t2 is one number in [0, 1]"] = close(t2, [t2[0]] * 3, 1e-4) and -1e-6 <= t2[0] <= 1 + 1e-6
    t2 = t2[0] * one
    # (the probe's L is (one - 0) * t2; divide the literal out again so t2 is the bare factor)
    t2 = t2 / one if one else t2
    out, m = go(lights_off)
    res["alpha"] = close([out[3]], [fade * weight])
    bp = basep(c)
    L = [c[22][i] + (one - c[22][i]) * t2 for i in range(3)]
    amb = [A[i] * c[11][0] * (bp[i] * L[i] + c[20][i]) * c[20][3] for i in range(3)]
    res["ambient"] = close(out[:3], amb, 1e-4)
    unlit = dict(lights_off)
    unlit[22] = [one] * 4
    unlit[20] = [0.0, 0.0, 0.0, 1.0]
    unlit[11] = [1.0] * 4
    out_u, _ = go(unlit)
    res["unlit"] = close(out_u[:3], [A[i] * bp[i] * one for i in range(3)], 1e-4) and close([out_u[3]], [fade * weight])
    ok_light = True
    neg = False
    for k in cfg["lights"]:
        one_light = dict(lights_off)
        one_light[k] = c[k]
        out_l, _ = go(one_light)
        f = [(out_l[i] - amb[i]) / (A[i] * c[20][3] * c[k][i]) for i in range(3)]
        ok_light = ok_light and close(f, [f[0]] * 3, 2e-3)
        neg = neg or f[0] < -1e-6
    res["light"] = ok_light
    res["(a light factor below 0, from the random cone and range constants)"] = neg
    fog = dict(c)
    fog[67] = [0.0, 0, 0.01, 0.01]   # predicate on: d > 0
    fog[28] = [0.0, c[28][1], 0, 0]
    out_f, mf = go(fog)
    out_n, _ = go(c)
    res["fog off at c28.x = 0"] = close(out_f, out_n, 1e-4) and any(f[1] in (4, 5) for f in mf.fetches)
    fog[28] = [0.7, c[28][1], 0, 0]
    out_f2, _ = go(fog)
    res["fog changes the colour only"] = (not close(out_f2[:3], out_n[:3], 1e-6)) and close([out_f2[3]], [out_n[3]])
    slots = sorted({f[1] for f in m.fetches})
    trials_log.setdefault((cliff, b135), set()).add(tuple(slots))
    return res


def run_far(hash_, rng):
    one = rng.choice([1.0, 0.875])
    c = base_consts(rng, one)
    c[255] = [one, 0, 0, 0]
    regs = {r: [rng.uniform(-1, 1) for _ in range(4)] for r in range(8)}
    regs[0] = [rng.random(), rng.random(), rng.random(), rng.random()]
    world = [rng.uniform(-60, 60) for _ in range(3)]
    regs[1] = world + [0]
    text = ue.load(DUMPS, hash_, "frag")

    def go(consts):
        m = ue.Machine(text, consts, lambda slot, coords, attrs: tex(slot, coords), regs={k: list(v) for k, v in regs.items()})
        return m.run()["oC0"], m

    F = [x * x for x in tex(13, (regs[0][0], regs[0][1]))[:3]]
    B = tex(3, (regs[0][2], regs[0][3]))
    d2 = sum((e - w) ** 2 for e, w in zip(c[19][:3], world))
    k = sat(d2 * c[31][0] + c[31][1]) if hash_.startswith("2F8A") else 1.0
    off = dict(c)
    off[87] = [0.0] * 4
    probe = dict(off)
    probe[22] = [0.0] * 4
    probe[20] = [0.0, 0.0, 0.0, 1.0]
    out, _ = go(probe)
    bp = [B[i] ** c[21][3] * c[21][i] for i in range(3)]
    res = {}
    if k == 0:
        return None
    t2 = [out[i] / (F[i] * bp[i] * k) / one for i in range(3)]
    res["t2 is one number in [0, 1]"] = close(t2, [t2[0]] * 3, 1e-4) and -1e-6 <= t2[0] <= 1 + 1e-6
    t2 = t2[0]
    out, m = go(off)
    L = [c[22][i] + (one - c[22][i]) * t2 for i in range(3)]
    amb = [F[i] * (bp[i] * L[i] + c[20][i]) * c[20][3] * k for i in range(3)]
    res["alpha is 1"] = out[3] == 1.0
    res["ambient"] = close(out[:3], amb, 1e-4)
    unlit = dict(off)
    unlit[22] = [one] * 4
    unlit[20] = [0.0, 0.0, 0.0, 1.0]
    out_u, _ = go(unlit)
    res["unlit"] = close(out_u[:3], [F[i] * bp[i] * one * k for i in range(3)], 1e-4)
    out_l, _ = go(c)
    f = [(out_l[i] - amb[i]) / (F[i] * c[20][3] * k * c[87][i] * c[11][0]) for i in range(3)]
    res["sun is F^2 * c11.x * c87 * f"] = close(f, [f[0]] * 3, 2e-3) and f[0] >= -1e-6
    fog = dict(c)
    fog[67] = [0.0, 0, 0.01, 0.01]
    fog[28] = [0.0, c[28][1], 0, 0]
    out_f, mf = go(fog)
    res["fog off at c28.x = 0"] = close(out_f, out_l, 1e-4) and any(x[1] in (4, 5) for x in mf.fetches)
    return res


def run(trials=600, out=None):
    """Runs every shader `trials` times; returns {hash: (trials that had a colour to compare, {check:
    count}, {(cliff, b135): slot sets})} and reports each line through `out` when given."""
    out = out or (lambda line: None)
    results = {}
    rng = random.Random(909)
    for hash_, cfg in LAYERS.items():
        tally = {}
        log = {}
        n = 0
        for i in range(trials):
            cliff = bool(i & 1) and "slope_reg" in cfg
            b135 = bool(i & 2) and cfg["b135"]
            res = run_layer(hash_, cfg, rng, cliff, b135, log)
            if res is None:
                continue
            n += 1
            for k, v in res.items():
                tally[k] = tally.get(k, 0) + bool(v)
        out(f"{hash_} ({n} trials): " + ", ".join(f"{k} {v}/{n}" for k, v in tally.items()))
        for (cliff, b135), slots in sorted(log.items()):
            out(f"    cliff block {'on ' if cliff else 'off'} b135 {'on ' if b135 else 'off'}: texture slots fetched {sorted(slots)}")
        results[hash_] = (n, tally, {k: sorted(v) for k, v in log.items()})
    for hash_ in ("7DDAD5C29B3F56EE", "2F8A0341E8C09698"):
        tally = {}
        n = 0
        for _ in range(trials):
            res = run_far(hash_, rng)
            if res is None:
                continue
            n += 1
            for k, v in res.items():
                tally[k] = tally.get(k, 0) + bool(v)
        out(f"{hash_} ({n} trials): " + ", ".join(f"{k} {v}/{n}" for k, v in tally.items()))
        results[hash_] = (n, tally, {})
    return results


def main():
    run(600, print)


if __name__ == "__main__":
    main()
