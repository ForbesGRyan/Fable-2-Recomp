"""The three terrain vertex shaders run through ucode_eval.py on random patch points, against the
closed formulas of the reading (frame-map section 13, "Terrain"). Evidence of the materials
sub-project's Task 9, tracked by the lighting sub-project with the dump folder's path and the trial
count as the only changes; `python vs_check.py` prints what `vs_check.txt` of Task 9 holds."""
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ucode_eval as ue

DUMPS = Path(__file__).resolve().parents[2] / "out" / "shader_dump"
NAN = float("nan")
LIT_LAYER = {253: [0.5, 1 / 15, -1.0, 15 / 14], 254: [0, 1, 3, 2], 255: [NAN] * 4}   # [vtess] log, GPU register file
LIT_FAR = {254: [0.5, -1.0, 0, 0], 255: [0, 1, 3, 2]}


def close(a, b, tol=1e-4):
    return all((x != x and y != y) or abs(x - y) <= tol * max(1.0, abs(x), abs(y)) for x, y in zip(a, b))


def layer(hash_, rng):
    c = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3, 4, 5, 6, 19)}
    c[8] = [rng.choice([0.125, 0.0625]), rng.choice([0.125, 0.25]), 1, 0.125]
    c[11] = [2, 0.5, 0.25, 0]
    c[14] = [1 / 384, 1 / 300, 1, 1]
    c[46] = [8, 8, 89.739, 1]
    c[47] = [1 / 384, 1 / 384, rng.choice([0, 3.0]), rng.choice([0, -5.0])]
    c[113] = [rng.choice([80, 96, 112]), rng.choice([128, 144]), 0, 0]
    c[114] = [rng.choice([1 / 255, -1.0]), 0, 0, 0]
    c[115] = [16 / 1650, 16 / 1815, (33 * rng.randrange(50) + 0.5) / 1650, (33 * rng.randrange(55) + 0.5) / 1815]
    c.update(LIT_LAYER)
    patch = rng.randrange(4)
    u, v = rng.random(), rng.random()
    n = rng.randrange(16)
    hval = rng.random()
    nx, ny = rng.uniform(0.3, 0.7), rng.uniform(0.3, 0.7)
    seen = {}

    def sampler(slot, coords, attrs):
        seen[slot] = coords
        if slot == 16:
            return [hval, 0, 0, 0]
        if slot == 18:
            return [0.11, 0.22, 0.33, n / 15]   # only .w is the DXT3A value in this test
        if slot == 17:
            return [nx, ny, 0, 0]
        raise KeyError(slot)

    m = ue.Machine(ue.load(DUMPS, hash_, "vert"), c, sampler, regs={0: [patch, u, v, 0], 1: [0, 0, 0, 0]})
    out = m.run()
    row = math.floor(patch * c[11][1])
    col = patch - row * c[11][0]
    wx = (col + u) * c[46][0] + c[113][0]
    wy = (row + v) * c[46][1] + c[113][1]
    h = hval * c[46][2]
    exp_h_uv = ((wx - c[47][2]) * c[47][0], (wy - c[47][3]) * c[47][1])
    exp_w_uv = ((col + u) * c[115][0] + c[115][2], (row + v) * c[115][1] + c[115][3])
    ok = close(seen[16], exp_h_uv) and close(seen[18], exp_w_uv) and close(seen[17], exp_h_uv)
    hole = c[114][0] > n / 15
    pos = [sum(c[r][k] * p for k, p in zip(range(4), (wx, wy, h, 1))) for r in range(4)]
    weight = c[253][3] * (n / 15 - c[253][1])
    detail = (c[8][1] * wx, -c[8][1] * wy)
    base = (c[14][0] * (wx - c[47][2]), c[14][1] * (wy - c[47][3]))
    if hole:
        ok = ok and all(x != x for x in out["oPos"])
    else:
        ok = ok and close(out["oPos"], pos, 1e-3)
        if hash_.startswith("C30A"):
            ok = ok and close(out["o2"][:2], detail) and close(out["o5"][:2], base) and close([out["o9"][3]], [weight])
            ok = ok and close(out["o0"][:2], (c[8][0] * wy, -c[8][0] * h)) and close(out["o1"][:2], (c[8][0] * wx, -c[8][0] * h))
            wpos = [sum(c[r][k] * p for k, p in zip(range(4), (wx, wy, h, 1))) for r in (4, 5, 6)]
            ok = ok and close(out["o4"][:3], wpos, 1e-3)
            N = (2 * nx - 1, 2 * ny - 1)
            nz = math.sqrt(abs(1 - N[0] ** 2 - N[1] ** 2))
            ok = ok and close(out["o3"][:3], (N[0], N[1], nz))
        else:
            ok = ok and close(out["o0"][:2], detail) and close(out["o3"][:2], base) and close([out["o7"][3]], [weight])
            wpos = [sum(c[r][k] * p for k, p in zip(range(4), (wx, wy, h, 1))) for r in (4, 5, 6)]
            ok = ok and close(out["o2"][:3], wpos, 1e-3)
            N = (2 * nx - 1, 2 * ny - 1)
            nz = math.sqrt(abs(1 - N[0] ** 2 - N[1] ** 2))
            ok = ok and close(out["o1"][:3], (N[0], N[1], nz))
    return ok, hole, sorted(m.used_consts)


def far(rng):
    h_ = "5003700B7C9B1C16"
    c = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3)}
    cols = rng.choice([1, 2, 3, 6])
    c[8] = [rng.choice([8, 32]), rng.choice([8, 32]), 89.739, 1]
    c[11] = [cols, 1 / cols, 0.5 / cols, 0]
    c[14] = [1 / 384, 1 / 300, 1, 1]
    c[46] = [1 / 384, 1 / 128, rng.choice([0, 32.0]), rng.choice([0, 448.0])]
    c[47] = [rng.choice([96, 144]), rng.choice([96, 192]), 0, 0]
    c[72] = [16 / 17, 0.5 / 17, 0, 0]
    c[113] = [rng.choice([0, 3, 19]), 0, 0, 0]
    c[114] = [1 / 48, 1 / 48, 0, 0]
    c[115] = [rng.randrange(40), rng.randrange(40), 0, 0]
    c.update(LIT_FAR)
    k = rng.randrange(6)
    u, v = rng.random(), rng.random()
    hval, ta, tb = rng.random(), rng.random(), rng.random()
    nx, ny = rng.uniform(0.3, 0.7), rng.uniform(0.3, 0.7)
    seen = {}

    def sampler(slot, coords, attrs):
        seen[slot] = coords
        return {16: [hval, 0, 0, 0], 18: [ta, 0.9, 0.8, 0.7], 19: [tb, 0.6, 0.5, 0.4], 17: [nx, ny, 0, 0]}[slot]

    m = ue.Machine(ue.load(DUMPS, h_, "vert"), c, sampler, regs={0: [k, u, v, 0], 1: [0, 0, 0, 0]})
    out = m.run()
    p = k + c[113][0]
    row = math.floor(p * c[11][1] + 1e-9)
    col = p - row * c[11][0]
    wx = (col + u) * c[8][0] + c[47][0]
    wy = (row + v) * c[8][1] + c[47][1]
    h = hval * c[8][2]
    huv = ((wx - c[46][2]) * c[46][0], (wy - c[46][3]) * c[46][1])
    tu = ((c[115][0] + col + u * c[72][0] + c[72][1]) * c[114][0], (c[115][1] + row + c[254][0]) * c[114][1])
    tv = ((c[115][0] + col + v * c[72][0] + c[72][1]) * c[114][0], (c[115][1] + row + c[254][0]) * c[114][1])
    pos = [sum(c[r][j] * q for j, q in zip(range(4), (wx, wy, h, 1))) for r in range(4)]
    ok = close(seen[16], huv) and close(seen[17], huv) and close(seen[18], tu) and close(seen[19], tv)
    ok = ok and close(out["oPos"], pos, 1e-3)
    ok = ok and close(out["o0"], (ta, tb, (wx - c[46][2]) * c[14][0], (wy - c[46][3]) * c[14][1]))
    ok = ok and close(out["o1"][:3], (wx, wy, h))
    N = (2 * nx - 1, 2 * ny - 1)
    ok = ok and close(out["o2"][:3], (N[0], N[1], math.sqrt(abs(1 - N[0] ** 2 - N[1] ** 2))))
    return ok, sorted(m.used_consts)


def run(trials=400):
    """Runs the three shaders on `trials` random points each; returns the report lines."""
    lines = []
    rng = random.Random(9)
    for h_ in ("C30A97D946FA2BE4", "FB68A7F2301210E1"):
        n_ok = n_hole = 0
        used = None
        for _ in range(trials):
            ok, hole, used = layer(h_, rng)
            n_ok += ok
            n_hole += hole
        lines.append(f"{h_}: {n_ok} of {trials} random points agree with the reading ({n_hole} culled by the layer test); constants read {used}")
    n_ok = 0
    for _ in range(trials):
        ok, used = far(rng)
        n_ok += ok
    lines.append(f"5003700B7C9B1C16: {n_ok} of {trials} random points agree with the reading; constants read {used}")
    return lines


def main():
    print("\n".join(run()))


if __name__ == "__main__":
    main()
