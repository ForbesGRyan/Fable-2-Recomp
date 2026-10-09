"""The numbers the C++ mirrors of the mask are pinned to (tests/native/test_clay_logic.cpp, cases 560
on; tests/test_light_check.py, MaskPinnedForTheRenderer), made by light_formulas.py. Written in shadows
plan Task 5 as a scratch file (out/tests/task5/mask_pins.py) and checked in by the final fix wave, with
its literals named (RADIUS, TAP_WEIGHT, FAR_REACH, SLOPE, MAP_SIZE, W, H): tests/test_mask_literals.py
holds them against src/native/render/clay_logic.h, the mask's pixel shader and light_formulas.py.

Inputs: frame 1 of out/native-evidence/shadows/task4_bridge_capture1_native_discovery_20261009_015622.jsonl
(evidence, not in the repository: without it the script says so and makes nothing): the
view-projection (c0..c3 of a terrain row), the scene's pixel c12, c19, c36..c43. Every number is
rounded to single precision before use, as the renderer holds it.

Usage: python tools/xdk_sigmatch/mask_pins.py [capture.jsonl]
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import light_formulas as lf  # noqa: E402

CAPTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'out', 'native-evidence', 'shadows',
                       'task4_bridge_capture1_native_discovery_20261009_015622.jsonl')


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


# The native target, the maps' size, and the mask's literals as single-precision numbers: the kernel
# radius (2.5 texels of the near map), a tap's weight, how far beyond split.y the far map is read,
# the fade's slope; the taps are the reference's.
W, H = 1120, 720
MAP_SIZE = 1024
RADIUS = f32(2.5 / MAP_SIZE)
TAP_WEIGHT = f32(1.0 / 12.0)
FAR_REACH = 10.0
SLOPE = f32(0.1)
TAPS = lf.MASK_NEAR_TAPS


def pfull_reg(pfull, reg):
    return list(struct.unpack('>4f', bytes.fromhex(pfull[reg * 32:reg * 32 + 32])))


def inv4(m):
    n = 4
    a = [list(map(float, m[i])) + [1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(a[r][c]))
        a[c], a[p] = a[p], a[c]
        d = a[c][c]
        a[c] = [v / d for v in a[c]]
        for r in range(n):
            if r != c:
                f = a[r][c]
                a[r] = [x - f * y for x, y in zip(a[r], a[c])]
    return [row[n:] for row in a]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def near_texel(ix, iy):
    return ((ix * 37 + iy * 101) % 129) / 128.0


def far_texel(ix, iy):
    return ((ix * 53 + iy * 17 + 5) % 129) / 128.0


def rot_texel(tx, ty):
    return (((tx * 73 + ty * 29) % 256) / 255.0, ((tx * 31 + ty * 57) % 256) / 255.0)


def clampi(v, hi):
    return min(max(v, 0), hi)


def load(capture=CAPTURE):
    vp = c12 = c19 = near = far = None
    with open(capture) as f:
        for line in f:
            if '"tess"' not in line:
                continue
            r = json.loads(line)
            if r.get('kind') != 'tess' or r.get('frame') != 1 or not r.get('in_scene') or not r.get('pfull'):
                continue
            consts = r['consts']
            vp = [[f32(v) for v in consts['c%d' % i]] for i in range(4)]
            p = r['pfull']
            c12, c19 = pfull_reg(p, 12), pfull_reg(p, 19)
            near = [pfull_reg(p, k) for k in (36, 37, 38, 39)]
            far = [pfull_reg(p, k) for k in (40, 41, 42, 43)]
            break
    return vp, c12, c19, near, far


def mask(px, py, depth, vp_inv, eye, split, near, far, rotate=True):
    """mask_0f99 at native pixel (px, py) with the native depth `depth` (0 near plane, 1 clear)."""
    return lf.mask_0f99(
        pixel=(float(px), float(py)), sample_depth=lambda u, v: 1.0 - depth, depth_scale=(f32(1.0 / W), f32(1.0 / H)),
        depth_offset=(0.0, 0.0), screen_to_world=vp_inv, eye=eye, split=split,
        near_rows=mul(near, vp_inv), far_rows=mul(far, vp_inv),
        sample_rot=(lambda u, v: rot_texel(int(math.floor(u * 32)) % 32, int(math.floor(v * 32)) % 32)) if rotate
        else (lambda u, v: (1.0, 0.5)),
        rot_offset=(0.5, 0.5), rot_scale=(1.0 / 32, 1.0 / 32), offsets=TAPS, radius=RADIUS,
        sample_near=lambda u, v: 1.0 - near_texel(clampi(int(math.floor(u * MAP_SIZE)), MAP_SIZE - 1),
                                                  clampi(int(math.floor(v * MAP_SIZE)), MAP_SIZE - 1)),
        sample_far_texel=lambda ix, iy: 1.0 - far_texel(clampi(ix, MAP_SIZE - 1), clampi(iy, MAP_SIZE - 1)),
        far_size=(MAP_SIZE, MAP_SIZE), tap_weight=TAP_WEIGHT, far_reach=FAR_REACH, slope=SLOPE)


def distance(px, py, depth, vp_inv, eye):
    S = [(2 * px + 1) * f32(1.0 / W) - 1, 1 - (2 * py + 1) * f32(1.0 / H), depth, 1.0]
    Ph = [lf.dot(r, S) for r in vp_inv]
    P = [x / Ph[3] for x in Ph[:3]]
    return math.sqrt(sum((P[i] - eye[i]) ** 2 for i in range(3))), P


def depth_for(px, py, want, vp_inv, eye):
    lo, hi = 0.0, 1.0
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        if distance(px, py, mid, vp_inv, eye)[0] < want:
            lo = mid
        else:
            hi = mid
    return f32(0.5 * (lo + hi))


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    capture = argv[0] if argv else CAPTURE
    if not os.path.isfile(capture):
        print(f"mask_pins: capture not found: {capture}", file=sys.stderr)
        return 2
    vp, c12, c19, near, far = load(capture)
    if vp is None:
        print(f"mask_pins: no terrain row of frame 1 with pixel registers in {capture}", file=sys.stderr)
        return 2
    vp_inv = [[f32(v) for v in row] for row in inv4(vp)]
    eye = c19[:3]

    def show(name, m):
        print(f"{name} = {{")
        for row in m:
            print("    {" + ", ".join(f"{v!r}f" for v in row) + "},")
        print("}")
    show("view_proj", vp)
    show("inv_view_proj (inverse of view_proj in double, rounded to float)", vp_inv)
    show("near_rows (c36..c39)", near)
    show("far_rows (c40..c43)", far)
    print("split (c12) =", [repr(v) for v in c12])
    print("eye (c19) =", [repr(v) for v in eye])
    print()
    print("mask pins: px, py, depth (float), d, mask (rotated), mask (unrotated: (a, b) = (1, 0))")
    for px, py, want in ((560, 600, 6.0), (300, 500, 12.5), (700, 420, 19.0), (640, 400, 20.5), (200, 380, 21.5),
                         (900, 350, 27.0), (500, 340, 34.0), (333, 333, 38.0), (800, 300, 44.0), (100, 250, 60.0),
                         (17, 5, None), (1119, 719, None)):
        depth = 1.0 if want is None else depth_for(px, py, want, vp_inv, eye)
        d, P = distance(px, py, depth, vp_inv, eye)
        m = mask(px, py, depth, vp_inv, eye, c12, near, far)
        m0 = mask(px, py, depth, vp_inv, eye, c12, near, far, rotate=False)
        print(f"    {{{px}, {py}, {depth!r}f, {d!r}, {m!r}, {m0!r}}},")
    print()
    # The parts alone, on a world point (world rows): MaskNear, MaskFar.
    print("part pins: world point, near term (rot (0.6, 0.8)), far term")
    for P in ([84.5, 150.25, 44.0, 1.0], [90.0, 140.0, 43.5, 1.0], [70.125, 165.5, 46.0, 1.0], [100.0, 120.0, 40.0, 1.0]):
        n = lf.mask_near(P, near, lambda u, v: 1.0 - near_texel(clampi(int(math.floor(u * MAP_SIZE)), MAP_SIZE - 1),
                                                                clampi(int(math.floor(v * MAP_SIZE)), MAP_SIZE - 1)),
                         (f32(0.6), f32(0.8)), TAPS, RADIUS, TAP_WEIGHT)
        fa = lf.mask_far(P, far, lambda ix, iy: 1.0 - far_texel(clampi(ix, MAP_SIZE - 1), clampi(iy, MAP_SIZE - 1)),
                         (MAP_SIZE, MAP_SIZE))
        uvz = [lf.dot(near[i], P) for i in range(3)]
        print(f"    {{{{{P[0]!r}f, {P[1]!r}f, {P[2]!r}f, 1.0f}}, {n!r}, {fa!r}}},   // near (u, v, z) = {uvz}")
    print()
    print("combine pins: d, near, far -> value  (split c12, slope float(0.1))")
    for d, n, fa in ((10.0, 0.25, 0.75), (21.0, 0.25, 0.75), (30.0, 0.25, 0.75), (40.0, 0.25, 0.75), (50.0, 0.25, 0.75),
                     (20.5, 1.0, 0.0), (36.5, 0.0, 1.0)):
        print(f"    {{{d!r}f, {n!r}f, {fa!r}f, {lf.mask_combine(d, n, fa, c12, SLOPE)!r}}},")
    return 0


if __name__ == '__main__':
    sys.exit(main())
