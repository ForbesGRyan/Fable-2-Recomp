#!/usr/bin/env python3
"""The native scene's clouds held against the game's own scene, by hue (plan Task 16 of the post sub-project as
amended: "validate by hue, not by a sky region's luminance"; review of plan Tasks 16 and 17, Important 2).

What is compared. The game's side: the scene target of one captured frame as `post_replay.py export` writes it
(`<prefix>.scene`: the game's picture before post, no HUD). The native side: two screenshots of one standing
view, `--fable2_native_sun=on` and `=off` (the switch draws the clouds; with the sun behind the camera nothing
else of it shows in the sky). In a band of open sky each picture's blue share, b / (r + g + b) of the linear
values, is averaged over blocks of 8 x 8 scene pixels. The native map is "on less off" (what the switch did);
the game's map is its scene's blue share. Both lose their smooth part (a quadratic surface: the sky's own
gradient), and the two are correlated over a search of shifts in blocks: the clouds drift, and a shot is taken
seconds before or after the capture. The control is the same search with the native map mirrored left to right.

What it decides. PLACE (the native clouds lie where the game's do), DRIFT (the best shift grows with the time
between shot and capture, one way) and the hue's SIGN (the correlation is positive: the native switch lowers the
blue share where the game's scene has it lower). It does NOT decide the clouds' strength: the native side is
display values of a picture after post and the game's side scene values before it, so the regression slope it
prints is no ratio of the two.

A pair is CONFIRMED when its best correlation is at least 0.5 and the mirrored control's best is at most half of
it. Exit code 0 when every pair is confirmed, 1 otherwise.

    post_cloud_hue.py <scene> --pair <on.png> <off.png> [--pair ...] [--band x0 y0 x1 y1] [--block 8]
"""
import argparse
import math
import struct
import sys
from array import array
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import region_compare as rc  # noqa: E402

BAND = (0.30, 0.02, 0.90, 0.14)     # the bridge's open sky, high up: fractions of the picture (x0, y0, x1, y1)
BLOCK = 8
MAX_DX, MAX_DY = 12, 3              # the search, in blocks
MIN_CORR = 0.5
CONTROL_SHARE = 0.5                 # the mirrored control's best may be at most this share of the best


class HueError(Exception):
    pass


def srgb_to_linear(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


_LINEAR = [srgb_to_linear(i / 255.0) for i in range(256)]


def blue_share(r, g, b):
    s = r + g + b
    return b / s if s > 1e-9 else 0.0


def band_blocks(w, h, band=BAND, block=BLOCK):
    """The band in whole blocks: (x0, y0, columns, rows) in pixels of a w x h picture."""
    x0, y0, x1, y1 = int(w * band[0]), int(h * band[1]), int(w * band[2]), int(h * band[3])
    cols, rows = (x1 - x0) // block, (y1 - y0) // block
    if cols < 4 or rows < 2:
        raise HueError("the band holds %d x %d blocks of %d pixels: too few" % (cols, rows, block))
    return x0, y0, cols, rows


def read_scene(path):
    """(w, h, floats) of a `<prefix>.scene` file: "F2OS", width, height, w * h * 3 float32."""
    data = Path(path).read_bytes()
    if len(data) < 12 or data[:4] != b"F2OS":
        raise HueError("%s: not a scene file of post_replay.py export" % path)
    w, h = struct.unpack_from("<II", data, 4)
    if w == 0 or h == 0 or len(data) != 12 + w * h * 12:
        raise HueError("%s: %d bytes for %d x %d" % (path, len(data), w, h))
    floats = array("f")
    floats.frombytes(data[12:])
    if sys.byteorder != "little":
        floats.byteswap()
    return w, h, floats


def scene_map(w, h, floats, band=BAND, block=BLOCK):
    """Rows of the blocks' mean blue share and mean luminance of a scene's band."""
    x0, y0, cols, rows = band_blocks(w, h, band, block)
    share, lum = [], []
    for by in range(rows):
        srow, lrow = [], []
        for bx in range(cols):
            s = l = 0.0
            for y in range(y0 + by * block, y0 + (by + 1) * block):
                o = (y * w + x0 + bx * block) * 3
                for _ in range(block):
                    r, g, b = floats[o], floats[o + 1], floats[o + 2]
                    s += blue_share(r, g, b)
                    l += 0.2126 * r + 0.7152 * g + 0.0722 * b
                    o += 3
            srow.append(s / (block * block))
            lrow.append(l / (block * block))
        share.append(srow)
        lum.append(lrow)
    return share, lum


def shot_map(image, w, h, band=BAND, block=BLOCK, client=None):
    """The same two maps of a screenshot, on the scene's block grid (w x h: the scene's size): each block is the
    mean over the shot's pixels whose centres fall into it, inside the game's picture (region_compare's client
    area). The shot's values are taken back through the sRGB curve."""
    left, top, cw, ch = rc.client_area(image.w, image.h, client)
    x0, y0, cols, rows = band_blocks(w, h, band, block)
    px = image.rgb
    share, lum = [], []
    for by in range(rows):
        ya = top + int(math.ceil((y0 + by * block) * ch / h - 0.5))
        yb = top + int(math.ceil((y0 + (by + 1) * block) * ch / h - 0.5))
        srow, lrow = [], []
        for bx in range(cols):
            xa = left + int(math.ceil((x0 + bx * block) * cw / w - 0.5))
            xb = left + int(math.ceil((x0 + (bx + 1) * block) * cw / w - 0.5))
            if not (0 <= xa < xb <= image.w and 0 <= ya < yb <= image.h):
                raise HueError("a block falls outside the shot (%d x %d)" % (image.w, image.h))
            s = l = 0.0
            for y in range(ya, yb):
                o = (y * image.w + xa) * 3
                for _ in range(xb - xa):
                    r, g, b = _LINEAR[px[o]], _LINEAR[px[o + 1]], _LINEAR[px[o + 2]]
                    s += blue_share(r, g, b)
                    l += 0.2126 * r + 0.7152 * g + 0.0722 * b
                    o += 3
            n = (xb - xa) * (yb - ya)
            srow.append(s / n)
            lrow.append(l / n)
        share.append(srow)
        lum.append(lrow)
    return share, lum


def _solve(a, b):
    """Gaussian elimination with partial pivoting; a singular system gives zeros."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(m[r][c]))
        if abs(m[p][c]) < 1e-12:
            return [0.0] * n
        m[c], m[p] = m[p], m[c]
        for r in range(c + 1, n):
            f = m[r][c] / m[c][c]
            for k in range(c, n + 1):
                m[r][k] -= f * m[c][k]
    x = [0.0] * n
    for c in range(n - 1, -1, -1):
        x[c] = (m[c][n] - sum(m[c][k] * x[k] for k in range(c + 1, n))) / m[c][c]
    return x


def detrend(grid):
    """The grid less its least-squares quadratic surface (1, x, y, x^2, y^2, x y)."""
    rows, cols = len(grid), len(grid[0])
    sx, sy = max(cols - 1, 1), max(rows - 1, 1)

    def basis(x, y):
        u, v = x / sx, y / sy
        return (1.0, u, v, u * u, v * v, u * v)

    ata = [[0.0] * 6 for _ in range(6)]
    atb = [0.0] * 6
    for y in range(rows):
        for x in range(cols):
            f = basis(x, y)
            for i in range(6):
                atb[i] += f[i] * grid[y][x]
                for j in range(6):
                    ata[i][j] += f[i] * f[j]
    c = _solve(ata, atb)
    return [[grid[y][x] - sum(ci * fi for ci, fi in zip(c, basis(x, y))) for x in range(cols)] for y in range(rows)]


def correlation(a, b):
    """Pearson's r of two equal lists; 0 when either does not vary or fewer than three values are given."""
    n = len(a)
    if n < 3 or n != len(b):
        return 0.0
    ma, mb = sum(a) / n, sum(b) / n
    saa = sum((x - ma) ** 2 for x in a)
    sbb = sum((y - mb) ** 2 for y in b)
    if saa <= 1e-30 or sbb <= 1e-30:
        return 0.0
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(saa * sbb)


def shifted_correlation(native, game, dx, dy):
    """r of native[y][x] against game[y - dy][x - dx] over the cells both grids hold: a positive dx says the
    native map shows the game's pattern dx blocks further right."""
    rows, cols = len(native), len(native[0])
    a, b = [], []
    for y in range(max(0, dy), rows + min(0, dy)):
        for x in range(max(0, dx), cols + min(0, dx)):
            a.append(native[y][x])
            b.append(game[y - dy][x - dx])
    return correlation(a, b)


def best_shift(native, game, max_dx=MAX_DX, max_dy=MAX_DY):
    """(r, dx, dy) of the best shift in the search; at least a third of the columns and half of the rows always
    overlap."""
    rows, cols = len(native), len(native[0])
    if rows != len(game) or cols != len(game[0]):
        raise HueError("two maps of different sizes")
    best = (-2.0, 0, 0)
    for dy in range(-min(max_dy, rows // 2), min(max_dy, rows // 2) + 1):
        for dx in range(-min(max_dx, 2 * cols // 3), min(max_dx, 2 * cols // 3) + 1):
            r = shifted_correlation(native, game, dx, dy)
            if r > best[0]:
                best = (r, dx, dy)
    return best


def compare(game_share, on_share, off_share, max_dx=MAX_DX, max_dy=MAX_DY):
    """The comparison of one pair. Returns a dict: `difference` (the native on less off map), `unshifted`,
    `best` (r, dx, dy), `control` (the same search with the native map mirrored left to right), `slope` (of the
    native map on the game's at the best shift; no ratio of strengths), `confirmed`."""
    diff = [[a - b for a, b in zip(ra, rb)] for ra, rb in zip(on_share, off_share)]
    native, game = detrend(diff), detrend(game_share)
    best = best_shift(native, game, max_dx, max_dy)
    mirrored = [row[::-1] for row in native]
    control = best_shift(mirrored, game, max_dx, max_dy)
    r, dx, dy = best
    a, b = [], []
    rows, cols = len(native), len(native[0])
    for y in range(max(0, dy), rows + min(0, dy)):
        for x in range(max(0, dx), cols + min(0, dx)):
            a.append(native[y][x])
            b.append(game[y - dy][x - dx])
    mb = sum(b) / len(b) if b else 0.0
    ma = sum(a) / len(a) if a else 0.0
    sbb = sum((v - mb) ** 2 for v in b)
    slope = sum((u - ma) * (v - mb) for u, v in zip(a, b)) / sbb if sbb > 1e-30 else 0.0
    flat = [v for row in diff for v in row]
    return {
        "difference": diff,
        "mean": sum(flat) / len(flat),
        "low": sorted(flat)[max(0, len(flat) // 100 - 1)],
        "unshifted": shifted_correlation(native, game, 0, 0),
        "best": best,
        "control": control,
        "control_unshifted": shifted_correlation(mirrored, game, 0, 0),
        "slope": slope,
        "confirmed": confirmed(best[0], control[0]),
    }


def confirmed(best_r, control_r):
    """The rule: a positive correlation of at least MIN_CORR at the best shift, and a mirrored control whose
    best is at most CONTROL_SHARE of it."""
    return best_r >= MIN_CORR and control_r <= CONTROL_SHARE * best_r


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("scene", help="<prefix>.scene of post_replay.py export: the game's scene of the same standing view")
    ap.add_argument("--pair", nargs=2, action="append", metavar=("ON", "OFF"), required=True,
                    help="two native screenshots of that view, the sun's switch on and off (repeat for more)")
    ap.add_argument("--band", nargs=4, type=float, default=list(BAND), metavar=("X0", "Y0", "X1", "Y1"))
    ap.add_argument("--block", type=int, default=BLOCK)
    args = ap.parse_args(argv)
    try:
        w, h, floats = read_scene(args.scene)
        band = tuple(args.band)
        game_share, game_lum = scene_map(w, h, floats, band, args.block)
        gd = detrend(game_share)
        flat = sorted(v for row in gd for v in row)
        print("the game's scene %s (%d x %d), band %s in %d x %d blocks of %d px: blue share mean %.4f; less its "
              "smooth part: 1%% %+.4f, 99%% %+.4f" % (
                  args.scene, w, h, band, len(game_share[0]), len(game_share), args.block,
                  sum(sum(r) for r in game_share) / (len(game_share) * len(game_share[0])),
                  flat[max(0, len(flat) // 100 - 1)], flat[min(len(flat) - 1, len(flat) - len(flat) // 100)]))
        status = 0
        for on_path, off_path in args.pair:
            on_share, on_lum = shot_map(rc.read_png(on_path), w, h, band, args.block)
            off_share, off_lum = shot_map(rc.read_png(off_path), w, h, band, args.block)
            c = compare(game_share, on_share, off_share)
            ratios = sorted(a / b for ra, rb in zip(on_lum, off_lum) for a, b in zip(ra, rb) if b > 0.0)
            print()
            print("%s less %s" % (on_path, off_path))
            print("  blue share, switch on less off: mean %+.4f, 1%% %+.4f; luminance on over off: mean %.4f, 99%% %.4f" % (
                c["mean"], c["low"], sum(ratios) / len(ratios), ratios[min(len(ratios) - 1, len(ratios) - len(ratios) // 100)]))
            print("  correlation with the game's scene: unshifted %+.3f, best %+.3f at (%+d, %+d) blocks; control (the native "
                  "map mirrored): unshifted %+.3f, its best %+.3f at (%+d, %+d)" % (
                      c["unshifted"], c["best"][0], c["best"][1], c["best"][2], c["control_unshifted"], c["control"][0],
                      c["control"][1], c["control"][2]))
            print("  slope of the native map on the game's at that shift %.2f (display values against scene values: it "
                  "does not decide the clouds' strength)" % c["slope"])
            print("  place, drift and the hue's sign: %s" % (
                "CONFIRMED" if c["confirmed"] else "NOT CONFIRMED (the rule: best at least %.2f and the control's best at most "
                "%.0f%% of it)" % (MIN_CORR, 100 * CONTROL_SHARE)))
            if not c["confirmed"]:
                status = 1
        return status
    except (HueError, rc.RegionError, OSError) as e:
        print("post_cloud_hue: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
