"""The offline replays of the lake's surface (the water sub-project, plan Task 6; spec Design 3, criteria 4 and 5): the
game's own formula (light_formulas.water_vertex, water_pixel; held to the microcode by water_check.py) run over a
capture's A17D rows with the capture's own dumps, against the game's own pictures. Unlike water_check.py `rows`
this is NOT circular: the other side is the game's scene colour (0x199E0000), the game's depth copy and the
game's reflection pictures, which no formula of ours made. It is what holds the things `rows` cannot: the byte
order of the textures, the picture's orientation and channel order, the moment the dumps were taken.

  scene <capture> <frame> [--flip] [--swap-rb] [--no-glint] [--levels mean|stored] [--normal exact|rounded] [--place X Y]
        criterion 4. The frame's A17D rows through water_vertex, rasterised (foliage_replay.fragments) at the
        sampling place of the game's depth copy (fitted on the capture's own unmoving draws by foliage_replay, or
        given), depth-tested against that copy with the rows' own depth function (greater, written), the alpha
        test of the rows' state, shaded by water_pixel with the row's registers, literals and bools and with: the
        mask (bilinear, clamped); the normal map at the level the fetch takes (`level`), linear between two levels;
        the picture of the row's own slot 13 through its fetch constant's swizzle and addressing; slot 14's dump;
        the frame's fog tables. Printed for the pixels with alpha of 0.99 or more ("judged"; the bed under the
        water is in no capture and is at most 1% there), whole and by slot-13 base: replay over the game's scene,
        mean red, green and blue; the correlation of the two luminances; the verdict against 0.93 to 1.07 a
        channel, a correlation of at least 0.7 and at least 5,000 judged pixels. --flip reads the picture upside
        down (v for 1 - v), --swap-rb exchanges its red and blue: the two controls, each of which must miss a
        bound, or the measure does not discriminate and nothing is built on it (`decide`).
  variants <capture>... [--frame 1]   the four variants of `scene` on each capture and the verdict's rule (the plan's,
        stated before the numbers): the orientation and channel order are those of the ONE variant that meets
        both bounds on the pixels of the deciding base (0x13501000) in every capture while each other misses one.
        Beside it, reported and not part of the rule: the same with the normal map decoded to 8 bits (ledger
        ruling 32: how much the rounding moves the lake's own pixels) and without the glint.
  sky <capture> [--frame 1]           criterion 5: light_formulas.sky along the rays of diag(c31.x, c31.y, 1, 1) * M *
        Mirror(h) for each texel centre of a 256 x 256 picture (rows 0, 1 and 3, as SkyRayBasis forms them), at
        exposure 1, saturated to 8 bits, zero where the ray points below the level; against each dumped slot-13
        picture on the game's sky texels (below the horizon row, not darker than 0.8 of the picture's sky, at
        least three texels from any darker texel): the mean ratio a channel (0.95 to 1.05), with red and blue as
        the fetch constant reads them and exchanged, and the first row with content against the derived horizon
        (within one row) for the picture as stored and upside down.
  gap <capture> <scene.f2ns> [--frame 1]   the gap before (the research's b_gap.py): native over game on the
        placeholder's footprint (the native depth at a mesh's height) and on the replay's kept pixels
  lookup <capture> [--frame 1]        where each kept pixel's slot-13 lookup lands, and the share whose texel and
        every texel within 8 of it is sky in the game's picture, by base (criterion 8's set)
  glint <capture> [--frame 1]         the kept pixels whose sun term is above 5% of the pixel (criterion 12, W4)
  edge <capture> [--frame 1]          the kept pixels within 20 units of the eye, those with an alpha below 0.99, the
        nearest distance (criterion 11's sets, W3)
Each prints its inputs' names and exits 1 when a bound it states is missed or its inputs are not there.

The level a fetch takes (`level`): the SDK's translator (thirdparty/rexglue-sdk/src/graphics/pipeline/shader/
dxbc_translator_fetch.cpp, lines 1401 to 1528, and the samples at 1836 and 1866) gives a tfetch2D with computed
LOD to the host as sample_d with the coordinate's two screen derivatives (deriv_rtx_coarse, deriv_rty_coarse), each
multiplied by 2 ^ bias, where the bias is the fetch constant's LOD bias (dword 4 bits 12 to 21, signed, / 32) plus
the instruction's; the gradient exponent adjust is not applied (its FIXME). The host then takes, with anisotropy
off, log2 of the longer of the two derivatives in texels, held to the sampler's mip range (the constant's mip_min
and mip_max, and the levels the texture has), linear between the two levels about it when the mip filter is
linear. So: level = clamp(log2(max(|d(uv)/dx| in texels, |d(uv)/dy| in texels)) + bias). The derivative here is
the forward difference of the coordinate on the water's plane a pixel to the right and a pixel down (the host's is
one difference a 2 x 2 quad: the same number on a plane up to the perspective's curvature).

The normal map's levels: until a capture holds the stored ones (W1), the 2 x 2 mean of the base (`--levels mean`).
Its values: the BC5 palette entries as they are (`--normal exact`, the default: the emulator samples host BC5,
ledger ruling 32) or rounded to 8 bits (`--normal rounded`: render::DecodeDxnLevelRg8, the tests' reference).
"""
import argparse
import math
import struct
import sys
from collections import namedtuple
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture_replay as cr  # noqa: E402
import impostor_check as ic  # noqa: E402
import light_formulas as lf  # noqa: E402
import water_check as wc  # noqa: E402
import water_textures as wt  # noqa: E402

SIZE = (1120, 720)
JUDGED_ALPHA = 0.99             # the bed is at most 1% of such a pixel
JUDGED_MIN = 5000               # criterion 4: fewer judged pixels read "not decidable"
BAND = (0.93, 1.07)             # criterion 4: replay over game, the mean of each channel
CORRELATION_MIN = 0.7           # criterion 4: of the two luminances over the judged pixels
SKY_BAND = (0.95, 1.05)         # criterion 5
SKY_DARK = 0.8                  # a sky texel is not darker than this of the picture's sky
SKY_MARGIN = 3                  # ... and this many texels from any darker texel
LOOKUP_REACH = 8                # criterion 8: every texel within this of the lookup is sky
GLINT_SHARE = 0.05              # criterion 12
GLINT_MIN = 500                 # a view counts with this many
NEAR_UNITS = 20.0               # criterion 11
DECIDING_BASE = 0x13501000      # one resolve a frame (spec criterion 4); 0x13401000 is open question 4's
LUMA = (0.2125, 0.7154, 0.0721)
VARIANTS = (("as the constant reads it", False, False), ("flip", True, False), ("swap-rb", False, True), ("flip and swap-rb", True, True))


def reference_of(word):
    """The alpha reference of a state word 0x210E."""
    return struct.unpack(">f", struct.pack(">I", int(word)))[0]


def level(du_dx, dv_dx, du_dy, dv_dy, fetch):
    """The level a tfetch2D with computed LOD takes (module text): log2 of the longer of the coordinate's two screen
    derivatives in texels, plus the fetch constant's LOD bias, held to the constant's mip range and to the levels
    there are. `fetch`: the constant's fields (water_textures.describe). A derivative that is no number, or a
    texture of one level, gives the lowest level of the range."""
    low = float(fetch.get("mip_min", 0))
    high = float(min(fetch.get("mip_max", 0), fetch.get("levels", 1) - 1))
    if high <= low:
        return low
    w, h = fetch["width"], fetch["height"]
    step = max(math.hypot(du_dx * w, dv_dx * h), math.hypot(du_dy * w, dv_dy * h))
    if not math.isfinite(step) or step <= 0.0:
        return low
    return min(max(math.log2(step) + float(fetch.get("lod_bias", 0.0)), low), high)


def sample_levels(levels, u, v, at, clamp=(0, 0)):
    """A texture's levels (each [h, w, c] floats, the base first) at (u, v) and a level `at`: bilinear in the two
    levels about it and linear between them; `at` is held to the levels there are."""
    at = min(max(float(at), 0.0), float(len(levels) - 1))
    low = int(math.floor(at))
    part = at - low
    out = wc.bilinear(levels[low], u, v, clamp)
    if part > 0.0 and low + 1 < len(levels):
        other = wc.bilinear(levels[low + 1], u, v, clamp)
        out = [a * (1.0 - part) + b * part for a, b in zip(out, other)]
    return out


def mean_levels(base):
    """The 2 x 2 mean pyramid of a float image [h, w, c], the base first, down to 1 x 1 (nothing rounded)."""
    out = [np.asarray(base, dtype=np.float64)]
    while out[-1].shape[0] > 1 or out[-1].shape[1] > 1:
        a = out[-1]
        h, w = a.shape[0], a.shape[1]
        ys = (np.arange(max(1, h // 2)) * 2, np.minimum(np.arange(max(1, h // 2)) * 2 + 1, h - 1))
        xs = (np.arange(max(1, w // 2)) * 2, np.minimum(np.arange(max(1, w // 2)) * 2 + 1, w - 1))
        out.append(sum(a[y][:, x] for y in ys for x in xs) / 4.0)
    return out


def normal_levels(textures, fc, source="mean", decode="exact"):
    """(the normal map's levels as floats in 0..1 [h, w, 2], the base first; the constant's fields; a note).
    `decode`: "exact" the BC5 palette entries as they are, "rounded" to 8 bits. `source`: "mean" the 2 x 2 mean of
    the base, "stored" the capture's own levels (cr.Unreadable when the capture has none)."""
    row = textures.rows.get(fc)
    if row is None:
        raise cr.Unreadable("no texture row of the normal map's fetch constant")
    f = textures.fields(fc)
    if f["format"] != wt.XENOS_DXN:
        raise cr.Unreadable("slot 2 is format %d, not k_DXN" % f["format"])
    rounded = decode == "rounded"
    try:
        base = wt.normal(textures.dir, row, 0, rounded=rounded).astype(np.float64) / 255.0
        if source == "stored":
            stored = (row.get("mips") or {}).get("levels") or []
            if not stored:
                raise cr.Unreadable("the capture holds no stored levels of the normal map (made before water discovery)")
            levels = [base] + [wt.normal(textures.dir, row, int(lv["level"]), rounded=rounded).astype(np.float64) / 255.0
                               for lv in sorted(stored, key=lambda lv: lv["level"])]
            return levels, f, "the capture's stored levels (%d)" % len(levels)
    except ValueError as e:
        raise cr.Unreadable(str(e)) from None
    levels = mean_levels(base)
    return levels, f, "levels made by a 2 x 2 mean of the base (%d)" % len(levels)


# colour [h, w, 3]: the replay's scene colour, the water over a bed of 0 at its alpha (0 where nothing is kept);
# alpha, kept (drawn by the state's alpha test and the depth test), base (the slot-13 base a kept pixel samples),
# lookup [h, w, 2] (slot 13's coordinate), glint (the sun term's share of the pixel before fog), distance (from the
# eye), fate counts {"killed", "skipped", "shaded"}; rows replayed; fragments the depth copy hid; the mean level of
# the two normal fetches; problems (texts); notes (texts).
Replay = namedtuple("Replay", "colour alpha kept base lookup glint distance fates rows hidden levels problems notes")


def _plane_xy(M, h, px, py, size):
    """The world (x, y) of the points of the plane z = h that the window positions (px, py) show (arrays)."""
    nx = px / size[0] * 2.0 - 1.0
    ny = 1.0 - py / size[1] * 2.0
    a = M[0][None, :] - nx[:, None] * M[3][None, :]
    b = M[1][None, :] - ny[:, None] * M[3][None, :]
    # a . (x, y, h, 1) = 0 and b . (x, y, h, 1) = 0
    ra = -(a[:, 2] * h + a[:, 3])
    rb = -(b[:, 2] * h + b[:, 3])
    det = a[:, 0] * b[:, 1] - a[:, 1] * b[:, 0]
    with np.errstate(divide="ignore", invalid="ignore"):
        x = (ra * b[:, 1] - a[:, 1] * rb) / det
        y = (a[:, 0] * rb - ra * b[:, 0]) / det
    return x, y


def replay(path, frame, flip=False, swap_rb=False, glint=True, levels="mean", decode="exact", place=(0.0, 0.0), depth=None,
           zscale=-1.0, zoffset=1.0, dumps=wc.DUMPS):
    """The frame's A17D rows as the game's formula draws them (module text, `scene`): Replay. `depth`: the game's
    depth copy [h, w] as stored (None: no depth test); `place`: the sampling place, offsets from the integral
    position."""
    import foliage_replay as fp
    capture = wc.read_capture(path)
    textures = wc.RowTextures(capture)
    text = wc._text(dumps, wc.PS)
    rows = [r for r in capture["water"] if r.get("frame") == frame and wc.row_kind(r) == "surface"]
    height, width = SIZE[1], SIZE[0]
    colour = np.zeros((height, width, 3))
    alpha = np.zeros((height, width))
    kept = np.zeros((height, width), dtype=bool)
    base = np.zeros((height, width), dtype=np.int64)
    lookup = np.zeros((height, width, 2))
    shine = np.zeros((height, width))
    distance = np.full((height, width), np.inf)
    zbuf = None if depth is None else np.array(depth, dtype=np.float64)
    fates = {"killed": 0, "skipped": 0, "shaded": 0}
    problems, notes, hidden, done = [], [], 0, 0
    level_sum = level_count = 0.0
    for row in rows:
        where = "line %s" % row.get("line")
        try:
            setup = wc.RowSetup(capture, textures, row, text)
            maps, nf, note = normal_levels(textures, wc._fc(row, 2), levels, decode)
            plane = wc.row_height(textures.dir, row)
        except (cr.Unreadable, ValueError) as e:
            problems.append("%s: NOT REPLAYED: %s" % (where, e))
            continue
        if note not in notes:
            notes.append(note)
        if tuple(row.get("viewport") or SIZE) != SIZE:
            problems.append("%s: NOT REPLAYED: a viewport of %s" % (where, row.get("viewport")))
            continue
        rs = row.get("rs") or {}
        word = int(str(rs.get("2200", "0x0")), 16)
        if (word >> 4) & 7 != 4 or not word & 2:
            problems.append("%s: NOT REPLAYED: depth word 0x%08X is not tested with function 4 (greater)" % (where, word))
            continue
        control = int(str(rs.get("2202", "0x0")), 16)
        reference = reference_of(int(str(rs.get("210E", "0x0")), 16)) if control & 8 else -math.inf
        if control & 8 and control & 7 != 4:
            problems.append("%s: NOT REPLAYED: alpha function %d, not 4 (greater)" % (where, control & 7))
            continue
        done += 1
        slot13 = wc._fc(row, 13)[1] & 0xFFFFF000
        bools = set(setup.bools) if glint else set(setup.bools) - {136}
        M = np.array([setup.vbank[reg] for reg in (0, 1, 2, 3)], dtype=np.float64)
        clip = []
        for p, pair in zip(setup.positions, setup.pairs):
            v = lf.water_vertex(p, pair, setup.vbank)
            clip.append(list(v["pos"]) + list(v["place"]) + list(v["coord"]) + list(v["mask"]))
        picture = setup.mirror
        if flip or swap_rb:
            def picture(u, v, read=setup.mirror):
                t = read(u, 1.0 - v if flip else v)
                return [t[2], t[1], t[0]] if swap_rb else t
        c117, c118 = setup.vbank[117], setup.vbank[118]
        for _, ys, xs, zw, attrs in fp.fragments(np.array(clip, dtype=np.float64), np.array(setup.triangles, dtype=np.int64), SIZE, place):
            stored = fp.stored_depth(zw, zscale, zoffset)
            if zbuf is not None:
                front = stored > zbuf[ys, xs]
                hidden += int((~front).sum())
                if not front.any():
                    continue
                ys, xs, stored, attrs = ys[front], xs[front], stored[front], attrs[front]
            # the coordinates' screen derivatives on the water's plane (module text)
            if plane is not None:
                px, py = xs + place[0], ys + place[1]
                x0, y0 = _plane_xy(M, plane, px, py, SIZE)
                x1, y1 = _plane_xy(M, plane, px + 1.0, py, SIZE)
                x2, y2 = _plane_xy(M, plane, px, py + 1.0, SIZE)
                grads = np.stack([x1 - x0, y1 - y0, x2 - x0, y2 - y0], axis=1)
            else:
                grads = np.zeros((len(ys), 4))
            for k in range(len(ys)):
                y, x = int(ys[k]), int(xs[k])
                a = attrs[k]
                P, coord, pair = [wc.f32(v) for v in a[0:3]], [wc.f32(v) for v in a[3:7]], [wc.f32(v) for v in a[7:9]]
                m = setup.mask(pair[0], pair[1])[0]
                gx, gy, hx, hy = grads[k]
                la = level(gx * c117[0], gy * c117[1], hx * c117[0], hy * c117[1], nf)
                lb = level(gx * c118[0], gy * c118[1], hx * c118[0], hy * c118[1], nf)
                at = iter((la, lb))
                tex = {"normal": lambda u, v: sample_levels(maps, u, v, next(at), nf["clamp"]), 13: picture, 14: setup.near,
                       8: wc._no_shadow, "tf4": setup.tf4, "tf5": setup.tf5}
                try:
                    out = lf.water_pixel((x, y), P, coord[:2], coord[2:], m, setup.consts, tex, bools=bools, lit=setup.lit)
                except cr.Unreadable as e:
                    problems.append("%s: NOT REPLAYED: %s" % (where, e))
                    break
                fates[out["fate"]] += 1
                if out["fate"] == "killed" or not out["alpha"] > reference:
                    continue
                al = out["alpha"]
                colour[y, x] = [c * al + old * (1.0 - al) for c, old in zip(out["colour"], colour[y, x])]
                alpha[y, x], kept[y, x], base[y, x] = al, True, slot13
                if zbuf is not None and word & 4:
                    zbuf[y, x] = stored[k]
                if out["fate"] == "shaded":
                    lookup[y, x] = out["lookup"]
                    shine[y, x] = out["glint"]
                    distance[y, x] = out["distance"]
                    level_sum += la + lb
                    level_count += 2
    return Replay(colour, alpha, kept, base, lookup, shine, distance, fates, done, hidden,
                  level_sum / level_count if level_count else 0.0, problems, notes)


def luminance(picture):
    return (np.asarray(picture, dtype=np.float64) * LUMA).sum(-1)


# judged pixels; replay over game, the mean of red, green and blue; the correlation of the two luminances; the
# verdict ("met", "not met", "not decidable"); the bounds missed (names); the game's and the replay's mean colour.
Measure = namedtuple("Measure", "judged ratio correlation verdict missed game replay")


def _measure(sel, replayed, game, judged_min):
    n = int(sel.sum())
    if n == 0:
        return Measure(0, (math.nan,) * 3, math.nan, "not decidable", ("no judged pixel",), (math.nan,) * 3, (math.nan,) * 3)
    r, g = replayed[sel], np.asarray(game, dtype=np.float64)[sel]
    rm, gm = r.mean(0), g.mean(0)
    with np.errstate(divide="ignore", invalid="ignore"):
        ratio = tuple(float(x) for x in rm / gm)
    lr, lg = luminance(r), luminance(g)
    correlation = float(np.corrcoef(lr, lg)[0, 1]) if n > 1 and lr.std() > 0.0 and lg.std() > 0.0 else math.nan
    missed = [name for name, x in zip(("red", "green", "blue"), ratio) if not BAND[0] <= x <= BAND[1]]
    if not correlation >= CORRELATION_MIN:
        missed.append("correlation")
    if n < judged_min:
        verdict = "not decidable"
    else:
        verdict = "not met" if missed else "met"
    return Measure(n, ratio, correlation, verdict, tuple(missed), tuple(float(x) for x in gm), tuple(float(x) for x in rm))


def measure(r, game, judged_min=JUDGED_MIN):
    """{None: the Measure over all judged pixels, base: the Measure of those that sample that slot-13 base}:
    criterion 4's measure of a Replay against the game's scene [h, w, 3]. Judged: kept with alpha of JUDGED_ALPHA or
    more."""
    judged = r.kept & (r.alpha >= JUDGED_ALPHA)
    out = {None: _measure(judged, r.colour, game, judged_min)}
    for b in sorted(int(x) for x in np.unique(r.base[r.kept])):
        out[b] = _measure(judged & (r.base == b), r.colour, game, judged_min)
    return out


# judged pixels at which two variants differ by more than PREFER_STEP of the game's luminance; those at which the
# game is nearer the first, and the second; the median of the variants' difference there, of the game's luminance.
Prefer = namedtuple("Prefer", "differ first second gap")
PREFER_STEP = 0.03              # two variants differ at a pixel when their luminances lie this far apart, of the game's
PREFER_MIN = 500                # fewer differing pixels cannot tell
PREFER_SHARE = 0.75             # the game must be nearer one variant at this share of them


def prefer(a, b, game, base=None):
    """The paired measure of two Replays of one frame against the game's scene: only the judged pixels (of slot-13
    base `base`, if given) at which the two differ are asked, and at each which of the two the game's luminance is
    nearer to. It cannot be carried by anything the two variants share (the glint, the fog, the base colour), which
    the bounds of criterion 4 can be."""
    sel = a.kept & b.kept & (a.alpha >= JUDGED_ALPHA) & (b.alpha >= JUDGED_ALPHA)
    if base is not None:
        sel &= a.base == base
    la, lb, lg = luminance(a.colour)[sel], luminance(b.colour)[sel], luminance(np.asarray(game, dtype=np.float64))[sel]
    with np.errstate(divide="ignore", invalid="ignore"):
        apart = np.abs(la - lb) / np.maximum(lg, 1e-9)
    differ = apart > PREFER_STEP
    da, db = np.abs(la - lg)[differ], np.abs(lb - lg)[differ]
    return Prefer(int(differ.sum()), int((da < db).sum()), int((db < da).sum()), float(np.median(apart[differ])) if differ.any() else 0.0)


def prefer_verdict(p, least=PREFER_MIN):
    """"the first", "the second" or "cannot tell" (fewer than `least` differing pixels, or the game nearer
    neither at PREFER_SHARE of them)."""
    if p.differ < least:
        return "cannot tell"
    if p.first >= PREFER_SHARE * p.differ:
        return "the first"
    if p.second >= PREFER_SHARE * p.differ:
        return "the second"
    return "cannot tell"


def decide(verdicts):
    """The verdict's rule over the four variants (module text): `verdicts` is {variant: [its verdict on the deciding
    base in each capture]}. Returns (the variant, "decided") or (None, why not)."""
    if any(v == "not decidable" for vs in verdicts.values() for v in vs):
        return None, "not decidable: a capture has too few judged pixels of the deciding base"
    whole = [name for name, vs in verdicts.items() if vs and all(v == "met" for v in vs)]
    if len(whole) == 1:
        return whole[0], "decided"
    if not whole:
        return None, "the formula does not close: no variant meets both bounds in every capture"
    return None, "the measure does not discriminate: %s each meet both bounds in every capture" % " and ".join(whole)


def edge_sets(r):
    """{"kept", "near" (kept within NEAR_UNITS of the eye), "fading" (kept with an alpha below JUDGED_ALPHA),
    "nearest" (the smallest distance of a kept pixel; infinity without one)}."""
    d = r.distance[r.kept]
    return {"kept": int(r.kept.sum()), "near": int((d < NEAR_UNITS).sum()), "fading": int((r.kept & (r.alpha < JUDGED_ALPHA)).sum()),
            "nearest": float(d.min()) if len(d) else math.inf}


def glint_set(r):
    """{"kept", "above" (kept pixels whose sun term is above GLINT_SHARE of the pixel), "largest"}."""
    g = r.glint[r.kept]
    return {"kept": int(r.kept.sum()), "above": int((g > GLINT_SHARE).sum()), "largest": float(g.max()) if len(g) else 0.0}


# --- the mirrored sky (criterion 5) --------------------------------------------------------------------------------

def view_rows(M, c31xy, h):
    return wc.water_view_rows(M, c31xy, h)


def horizon_row(view, size=256):
    """The picture row (a number; texel j spans j to j + 1) at which the ray of the picture's middle column is
    level: above it (smaller rows) the mirrored view shows what lies below the water plane, and nothing is drawn."""
    T = np.asarray(view, dtype=np.float64)
    a, b0, b1 = T[0, :3], T[1, :3], T[3, :3]
    # the ray at (0, Y) is a x (b0 - Y b1); its third component is linear in Y
    z0, z1 = np.cross(a, b0)[2], np.cross(a, b1)[2]
    Y = z0 / z1
    return (1.0 - Y) / 2.0 * size


def view_rays(view, size=256):
    """The view rays [size, size, 3] of the texel centres of a size x size picture under a view-projection: from
    rows 0, 1 and 3 (SkyRayBasis), pointing into the view."""
    T = np.asarray(view, dtype=np.float64)
    c = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    X, Y = np.meshgrid(c, -c)
    a = T[0, :3][None, None, :] - X[..., None] * T[3, :3][None, None, :]
    b = T[1, :3][None, None, :] - Y[..., None] * T[3, :3][None, None, :]
    d = np.cross(a, b)
    sign = np.sign((d * T[3, :3]).sum(-1))
    return d * sign[..., None]


def sky_picture(view, block, tf4, tf13=None, tf14=None, size=256):
    """The mirrored sky as the native target holds it (spec Design 7): light_formulas.sky along the view's rays at
    exposure 1, saturated and rounded to 8 bits, zero where the ray's third component is below 0. uint8
    [size, size, 3]."""
    rays = view_rays(view, size)
    out = np.zeros((size, size, 3), dtype=np.uint8)
    for j in range(size):
        for i in range(size):
            n = rays[j, i]
            if not n[2] >= 0.0:
                continue
            rgb = lf.sky(n, block, tf4, tf13, tf14)
            out[j, i] = [int(min(max(v, 0.0), 1.0) * 255.0 + 0.5) for v in rgb]
    return out


def _grow(mask, steps):
    out = mask.copy()
    for _ in range(steps):
        g = out.copy()
        g[1:] |= out[:-1]
        g[:-1] |= out[1:]
        g[:, 1:] |= out[:, :-1]
        g[:, :-1] |= out[:, 1:]
        g[1:, 1:] |= out[:-1, :-1]
        g[:-1, :-1] |= out[1:, 1:]
        g[1:, :-1] |= out[:-1, 1:]
        g[:-1, 1:] |= out[1:, :-1]
        out = g
    return out


def picture_sky(picture):
    """The colour of a reflection picture's sky: the median of its lowest eighth of rows (floats, [h, w, 3])."""
    h = picture.shape[0]
    return np.median(np.asarray(picture, dtype=np.float64)[-max(1, h // 8):].reshape(-1, 3), axis=0)


def sky_texels(picture, horizon, margin=SKY_MARGIN, dark=SKY_DARK):
    """The game's sky texels of a reflection picture [h, w, 3] (floats): below the horizon row, not darker than `dark`
    of the picture's sky, and at least `margin` texels from any darker texel (the rows above the horizon are darker:
    nothing is drawn there)."""
    lum = luminance(picture)
    low = lum < dark * float(luminance(picture_sky(picture)))
    rows = np.arange(picture.shape[0])[:, None] + 0.5
    low |= np.broadcast_to(rows < horizon, low.shape)
    return ~_grow(low, margin)


def first_content_row(picture, floor=0.01):
    """The median over the columns of the first row at which a column has content (a channel sum above `floor`)."""
    has = np.asarray(picture, dtype=np.float64).sum(-1) > floor
    first = np.where(has.any(axis=0), has.argmax(axis=0), picture.shape[0])
    return float(np.median(first))


# --- the commands -------------------------------------------------------------------------------------------------

def _inputs(path, frame, place, say):
    """(capture path, the frame with the game's depth copy, the game's scene or None, the sampling place)."""
    import foliage_replay as fp
    import foliage_trace as ft
    path = cr.find_capture(str(path))
    try:
        fr = fp.load_frame(path, frame)
    except cr.Unreadable as e:
        say("%s frame %d: %s" % (path.name, frame, e))
        return path, None, None, None
    say("%s frame %d: the game's depth copy: %s; the copy stands at line %s; the frame's A17D rows at lines %s" % (
        path.name, frame, fr.depth_note, fr.copy_line,
        ", ".join(str(r["line"]) for r in wc.water_rows(path) if r.get("frame") == frame and wc.row_kind(r) == "surface") or "none"))
    try:
        game, c20w = ft.game_scene(path, frame)
        say("  the game's scene 0x199E0000 of the frame: read (c20.w %.9g)" % c20w)
    except cr.Unreadable as e:
        game = None
        say("  the game's scene: NOT READ: %s" % e)
    if place is not None:
        say("  the sampling place of the depth copy: GIVEN (%g, %g)" % tuple(place))
    elif fr.depth is None:
        place = (fp.SAMPLE_ORIGIN, fp.SAMPLE_ORIGIN)
        say("  the sampling place: (%g, %g), not fitted (no depth copy)" % place)
    else:
        items = fp.replay_rows(fr, fr.rows_before_copy(), fp.load_table(), cache=Path(cr.REPO) / "out" / "tests" / "water-replay-cache")
        place = fp.sample_place(fr, items, say)
    return path, fr, game, tuple(place)


def _base_name(base):
    return "all" if base is None else "0x%08X" % base


def _say_measure(say, name, r, m):
    say("  %-26s kept %6d (killed %d, skipped %d, hidden by the depth copy %d); the mean level of the normal fetches %.2f" % (
        name, int(r.kept.sum()), r.fates["killed"], r.fates["skipped"], r.hidden, r.levels))
    for base, x in m.items():
        say("    %-10s judged %6d: replay / game red %.3f green %.3f blue %.3f; correlation %.3f; game mean %s, replay mean %s: %s%s" % (
            _base_name(base), x.judged, x.ratio[0], x.ratio[1], x.ratio[2], x.correlation,
            " ".join("%.4f" % v for v in x.game), " ".join("%.4f" % v for v in x.replay), x.verdict,
            " (misses %s)" % ", ".join(x.missed) if x.missed and x.verdict != "met" else ""))


def cmd_scene(args, say=print):
    path, fr, game, place = _inputs(args.capture, args.frame, args.place, say)
    if fr is None:
        return 1
    r = replay(path, args.frame, args.flip, args.swap_rb, not args.no_glint, args.levels, args.normal, place, fr.depth, fr.zscale, fr.zoffset)
    for text in r.problems + r.notes:
        say("  " + text)
    say("  the normal map sampled %s" % ("as BC5's own palette entries" if args.normal == "exact" else "rounded to 8 bits"))
    if game is None or fr.depth is None:
        say("criterion 4: not decidable (the capture holds no %s)" % ("scene" if game is None else "depth copy"))
        return 1
    m = measure(r, game)
    name = ", ".join(n for n, on in (("flip", args.flip), ("swap-rb", args.swap_rb), ("no glint", args.no_glint)) if on) or "as the constant reads it"
    _say_measure(say, name, r, m)
    deciding = m.get(DECIDING_BASE)
    say("criterion 4 on the pixels of 0x%08X (bounds %.2f to %.2f a channel, correlation at least %.1f, at least %d judged): %s" % (
        DECIDING_BASE, BAND[0], BAND[1], CORRELATION_MIN, JUDGED_MIN, deciding.verdict if deciding else "no such pixel"))
    return 0 if deciding and deciding.verdict == "met" and not r.problems else 1


def cmd_variants(args, say=print):
    verdicts = {name: [] for name, _, _ in VARIANTS}
    whole = {name: [] for name, _, _ in VARIANTS}
    pairs = {name: [] for name, _, _ in VARIANTS[1:]}
    bad = False
    for capture in args.capture:
        path, fr, game, place = _inputs(capture, args.frame, args.place, say)
        if fr is None or game is None or fr.depth is None:
            say("  NOT COMPARED")
            bad = True
            continue
        own = None
        for name, flip, swap in VARIANTS:
            r = replay(path, args.frame, flip, swap, True, args.levels, "exact", place, fr.depth, fr.zscale, fr.zoffset)
            if own is None:
                own = r
                for text in r.problems + r.notes:
                    say("  " + text)
                bad = bad or bool(r.problems)
            m = measure(r, game)
            _say_measure(say, name, r, m)
            verdicts[name].append(m[DECIDING_BASE].verdict if DECIDING_BASE in m else "not decidable")
            whole[name].append(m[None].verdict)
            if r is not own:
                p = prefer(own, r, game)
                pairs[name].append(prefer_verdict(p))
                say("    the paired measure against the first variant: %d judged pixels at which the two differ by more than %.0f%% of "
                    "the game's luminance (median %.1f%%); the game is nearer the first at %d, nearer this one at %d: %s" % (
                        p.differ, 100 * PREFER_STEP, 100 * p.gap, p.first, p.second, prefer_verdict(p)))
        # reported, not part of the rule
        judged = own.kept & (own.alpha >= JUDGED_ALPHA)
        for name, kw in (("normal map rounded to 8 bits", {"decode": "rounded"}), ("without the glint", {"glint": False})):
            r = replay(path, args.frame, False, False, kw.get("glint", True), args.levels, kw.get("decode", "exact"), place, fr.depth, fr.zscale, fr.zoffset)
            _say_measure(say, name, r, measure(r, game))
            both = judged & r.kept
            if both.any():
                d = np.abs(r.colour[both] - own.colour[both]) / np.maximum(own.colour[both], 1e-6)
                say("    against the first variant on %d judged pixels: relative difference a pixel, the largest channel: median %.2e, "
                    "95th percentile %.2e, largest %.2e; pixels that differ by more than 1%%: %d" % (
                        int(both.sum()), float(np.median(d.max(1))), float(np.percentile(d.max(1), 95)), float(d.max()), int((d.max(1) > 0.01).sum())))
    choice, why = decide(verdicts)
    say("the rule: the orientation and channel order are those of the one variant that meets both bounds on the pixels of 0x%08X in "
        "every capture while each other misses one in at least one" % DECIDING_BASE)
    for name, vs in verdicts.items():
        say("  %-26s %s" % (name, ", ".join(vs) or "-"))
    say("verdict as the criterion is written: %s" % ("%s: %s" % (choice, why) if choice else why))
    choice2, why2 = decide(whole)
    say("the same rule on ALL judged pixels (a substituted measure: most of the lake samples 0x13401000, not the deciding base):")
    for name, vs in whole.items():
        say("  %-26s %s" % (name, ", ".join(vs) or "-"))
    say("verdict by the substituted measure: %s" % ("%s: %s" % (choice2, why2) if choice2 else why2))
    say("the paired measure (only the pixels at which a variant differs from the first; needs %d of them and the game nearer one at "
        "%.0f%%):" % (PREFER_MIN, 100 * PREFER_SHARE))
    for name, vs in pairs.items():
        say("  the first against %-18s %s" % (name + ":", ", ".join(vs) or "-"))
    return 0 if choice and not bad else 1


def _sky_inputs(path, frame):
    """(the frame's sky block for light_formulas.sky, tf4, tf13 or None, tf14 or None, a note)."""
    capture = wc.read_capture(path)
    fog = (capture["frames"].get(frame) or {}).get("fog") or {}
    sky = fog.get("sky") or {}
    tables = wc.row_tables(capture, frame)
    if tables is None or not sky.get("valid"):
        raise cr.Unreadable("the frame row has no valid fog and sky block")
    block = {"c31x": sky["c31x"], "c46x": sky["c46x"], "c47x": sky["c47x"], "c64": fog["c64"], "c65": fog["c65"], "c66": fog["c66"],
             "c67y": fog["c67"][1]}
    textures = wc.RowTextures(capture)
    out, notes = [], []
    for slot in ("tf13", "tf14"):
        fc = tuple(int(str(v), 16) for v in sky.get(slot) or ())
        try:
            if fc not in textures.rows:         # the texture row of the same texture under another sampler word (dword 0)
                same = [k for k in textures.rows if k[1:] == fc[1:] and (k[0] >> 31) == (fc[0] >> 31) and ((k[0] >> 22) & 0x1FF) == ((fc[0] >> 22) & 0x1FF)]
                if same:
                    textures.rows[fc] = textures.rows[same[0]]
            image, f = textures.image(fc)
            if image.shape[2] != 4:
                raise cr.Unreadable("not four channels")
            swz = f["swizzle"]
            out.append(lambda u, v, image=image, f=f, swz=swz: wc._shader(wc.bilinear(image, u, v, f["clamp"]), swz, 4))
            notes.append("%s %d x %d, swizzle %s, clamp %s" % (slot, f["width"], f["height"], swz, f["clamp"]))
        except (cr.Unreadable, ValueError) as e:
            out.append(None)
            notes.append("%s NOT READ (%s): the clear-sky term stands in" % (slot, e))
    if out[0] is None:
        out[1] = None
    return block, tables[0], out[0], out[1], "; ".join(notes)


def cmd_sky(args, say=print):
    bad = compared = 0
    for capture in args.capture:
        path = cr.find_capture(capture)
        cap = wc.read_capture(path)
        rows = [r for r in cap["water"] if r.get("frame") == args.frame and wc.row_kind(r) == "surface"]
        if not rows:
            say("%s frame %d: no water row" % (path.name, args.frame))
            continue
        try:
            block, tf4, tf13, tf14, note = _sky_inputs(path, args.frame)
        except cr.Unreadable as e:
            say("%s frame %d: NOT COMPARED: %s" % (path.name, args.frame, e))
            bad += 1
            continue
        say("%s frame %d: the sky's block of the frame row (c31.x %g, c46.x %g, c47.x %g); %s" % (
            path.name, args.frame, block["c31x"], block["c46x"], block["c47x"], note))
        textures = wc.RowTextures(cap)
        M = np.array(rows[0]["bank"][0:16], dtype=np.float64).reshape(4, 4)
        c31 = ic.pixel_register(rows[0], 31)
        seen, made = set(), {}
        for row in rows:
            fc = wc._fc(row, 13)
            if fc in seen:
                continue
            seen.add(fc)
            h = wc.row_height(textures.dir, row)
            try:
                image, f = textures.image(fc)
            except cr.Unreadable as e:
                say("  picture %08X: NOT READ: %s" % (fc[1] & 0xFFFFF000, e))
                bad += 1
                continue
            view = view_rows(M, c31[:2], h)
            horizon = horizon_row(view, f["height"])
            if "sky" not in made:               # the rays need rows 0, 1 and 3: one sky for every plane
                made["sky"] = sky_picture(view, block, tf4, tf13, tf14, f["height"]).astype(np.float64) / 255.0
            native = made["sky"]
            swz = f["swizzle"]
            game = image[..., list(swz[:3])]
            compared += 1
            name = str(textures.rows[fc]["file"]).split("/")[-1]
            first = first_content_row(game)
            flipped = first_content_row(game[::-1])
            say("  picture %08X (%s, h %.6g): the derived horizon at row %.2f; the first row with content: %.1f as stored, %.1f upside "
                "down; the replay's own %.1f" % (fc[1] & 0xFFFFF000, name, h, horizon, first, flipped, first_content_row(native)))
            sel = sky_texels(game, horizon)
            n = int(sel.sum())
            if n == 0:
                say("    no sky texel: NOT COMPARED")
                bad += 1
                continue
            ratios = {}
            for label, order in (("as the constant reads it", (0, 1, 2)), ("red and blue exchanged", (2, 1, 0))):
                g = game[..., list(order)][sel].mean(0)
                ratios[label] = native[sel].mean(0) / g
                say("    %d sky texels, the picture's channels %s: game mean %s, mirrored sky mean %s, sky / game %s" % (
                    n, label, " ".join("%.4f" % v for v in g), " ".join("%.4f" % v for v in native[sel].mean(0)),
                    " ".join("%.3f" % v for v in ratios[label])))
            ok_colour = all(SKY_BAND[0] <= v <= SKY_BAND[1] for v in ratios["as the constant reads it"])
            other = all(SKY_BAND[0] <= v <= SKY_BAND[1] for v in ratios["red and blue exchanged"])
            ok_row = abs(first - math.ceil(horizon - 0.5)) <= 1.0
            ok_flip = abs(flipped - math.ceil(horizon - 0.5)) <= 1.0
            say("    criterion 5: the channels within %.2f to %.2f: %s (exchanged: %s); the first row within one of the horizon's: %s "
                "(upside down: %s)" % (SKY_BAND[0], SKY_BAND[1], "met" if ok_colour else "NOT MET", "met" if other else "not met",
                                       "met" if ok_row else "NOT MET", "met" if ok_flip else "not met"))
            bad += not (ok_colour and ok_row)
        if args.png and "sky" in made:
            import impostor_pictures as ip
            fc = wc._fc(rows[0], 13)
            image, f = textures.image(fc)
            game = (image[..., list(f["swizzle"][:3])] * 255.0 + 0.5).astype(np.uint8)
            ip.write_png(Path(args.png), np.concatenate([game, (made["sky"] * 255.0 + 0.5).astype(np.uint8)], axis=1))
            say("  sheet %s: the game's picture %08X (left) beside the mirrored sky (right), as stored" % (args.png, fc[1] & 0xFFFFF000))
    if not compared:
        say("no picture was compared")
        return 1
    say("criterion 5: %s" % ("met on every picture" if not bad else "%d pictures do not meet it or were not compared" % bad))
    return 1 if bad else 0


def _replayed(args, say):
    path, fr, game, place = _inputs(args.capture, args.frame, args.place, say)
    if fr is None:
        return None
    r = replay(path, args.frame, place=place, depth=fr.depth, zscale=fr.zscale, zoffset=fr.zoffset)
    for text in r.problems + r.notes:
        say("  " + text)
    return path, fr, game, r


def cmd_glint(args, say=print):
    got = _replayed(args, say)
    if got is None:
        return 1
    path, fr, game, r = got
    if not r.rows:
        say("%s: no A17D row in the frame: the lake is not in this view" % path.name)
        return 1
    g = glint_set(r)
    say("%s frame %d: %d kept pixels; the sun term above %.0f%% of the pixel in %d (the largest share %.3f); a view counts with %d: %s" % (
        path.name, args.frame, g["kept"], 100 * GLINT_SHARE, g["above"], g["largest"], GLINT_MIN, "counts" if g["above"] >= GLINT_MIN else "does not count"))
    return 0 if g["above"] >= GLINT_MIN else 1


def cmd_edge(args, say=print):
    got = _replayed(args, say)
    if got is None:
        return 1
    path, fr, game, r = got
    if not r.rows:
        say("%s: no A17D row in the frame: the lake is not in this view" % path.name)
        return 1
    e = edge_sets(r)
    say("%s frame %d: %d kept pixels; within %g units of the eye: %d (the nearest %.1f units); with an alpha between the reference "
        "and %.2f: %d" % (path.name, args.frame, e["kept"], NEAR_UNITS, e["near"], e["nearest"], JUDGED_ALPHA, e["fading"]))
    return 0


def cmd_lookup(args, say=print):
    got = _replayed(args, say)
    if got is None:
        return 1
    path, fr, game, r = got
    cap = wc.read_capture(path)
    textures = wc.RowTextures(cap)
    sampling = np.zeros(r.kept.shape, dtype=bool)
    done = set()
    for row in [x for x in cap["water"] if x.get("frame") == args.frame and wc.row_kind(x) == "surface"]:
        fc = wc._fc(row, 13)
        b = fc[1] & 0xFFFFF000
        sel = r.kept & (r.base == b)
        if not sel.any() or b in done:
            continue
        done.add(b)
        image, f = textures.image(fc)
        picture = image[..., list(f["swizzle"][:3])]
        M = np.array(row["bank"][0:16], dtype=np.float64).reshape(4, 4)
        horizon = horizon_row(view_rows(M, ic.pixel_register(row, 31)[:2], wc.row_height(textures.dir, row)), f["height"])
        lum = luminance(picture)
        low = lum < SKY_DARK * float(luminance(picture_sky(picture)))
        clear = ~_grow(low, LOOKUP_REACH)
        at = r.lookup[sel]
        px = np.floor(at[:, 0] * f["width"]).astype(np.int64) % f["width"]
        py = np.floor(at[:, 1] * f["height"]).astype(np.int64) % f["height"]
        ok = clear[py, px]
        sampling[sel] = ok
        say("  picture %08X (horizon row %.1f): %d kept pixels, lookups at rows %d to %d, columns %d to %d; sky-sampling (the texel and "
            "every texel within %d of it not darker than %.1f of the picture's sky): %d (%.1f%%)" % (
                b, horizon, int(sel.sum()), int(py.min()), int(py.max()), int(px.min()), int(px.max()), LOOKUP_REACH, SKY_DARK,
                int(ok.sum()), 100.0 * ok.mean()))
    total = int(r.kept.sum())
    say("%s frame %d: %d kept pixels, %d sky-sampling (%.1f%%)" % (path.name, args.frame, total, int(sampling.sum()),
                                                                     100.0 * sampling.sum() / total if total else math.nan))
    if args.out:
        np.save(args.out, np.stack([r.kept, sampling]))
        say("  written %s (two planes: kept, sky-sampling)" % args.out)
    return 0 if total else 1


def _hue(rgb):
    import colorsys
    return colorsys.rgb_to_hsv(*[float(max(v, 0.0)) for v in rgb])[0] * 360.0


def cmd_gap(args, say=print):
    import foliage_trace as ft
    got = _replayed(args, say)
    if got is None:
        return 1
    path, fr, game, r = got
    if game is None or fr.depth is None:
        say("the gap: not decidable (no scene or no depth copy)")
        return 1
    head, nscene, nlev, _ = ft.read_dump(args.dump)
    scale = (ft.game_scene(path, args.frame)[1] / head["exposure"]) if head["exposure"] else 1.0
    say("the stored native scene %s: frame %s (the capture's frame row says %s), %d x %d, exposure %g: the native colour times %.6g" % (
        Path(args.dump).name, head["scene_frame"], ft.scene_frame_of(path, args.frame), head["width"], head["height"], head["exposure"], scale))
    H, W = fr.depth.shape
    if nlev.shape != (H, W):
        yi, xi = np.arange(H) * nlev.shape[0] // H, np.arange(W) * nlev.shape[1] // W
        nlev, nscene = nlev[yi][:, xi], nscene[yi][:, xi]
    rows = [x for x in wc.water_rows(path) if x.get("frame") == args.frame and wc.row_kind(x) == "surface"]
    M = np.array(rows[0]["bank"][0:16], dtype=np.float64).reshape(4, 4)
    Mi = np.linalg.inv(M)
    ys, xs = np.mgrid[0:H, 0:W]

    def world(zw):
        c = np.stack([(xs + 0.5) / W * 2 - 1, 1 - (ys + 0.5) / H * 2, zw, np.ones((H, W))], -1) @ Mi.T
        return c[..., :3] / c[..., 3:4]

    nnone = nlev >= 16777215
    nw = world(nlev.astype(np.float64) / 16777215.0)
    gw = world((fr.depth - fr.zoffset) / fr.zscale)
    eye = np.array(wc.read_capture(path)["frames"][args.frame]["light"]["eye"])
    native = nscene[..., :3].astype(np.float64) * scale
    heights = sorted({round(wc.row_height(path.parent, x), 4) for x in rows})

    def line(name, sel):
        n = int(sel.sum())
        if not n:
            say("  %-44s 0 pixels" % name)
            return
        g, a = game[sel].mean(0), native[sel].mean(0)
        dh = (_hue(a) - _hue(g) + 180.0) % 360.0 - 180.0
        nd, gd = np.linalg.norm(nw[sel] - eye, axis=-1), np.linalg.norm(gw[sel] - eye, axis=-1)
        gl, nl = luminance(game[sel]), luminance(native[sel])
        say("  %-44s %6d px %5.2f%% | native / game red %.3f green %.3f blue %.3f luminance %.3f | hue %+.1f degrees | luminance 5th, "
            "50th, 95th: game %.3f %.3f %.3f native %.3f %.3f %.3f | native depth within 1%% of the game's copy %.1f%%" % (
                name, n, 100.0 * n / sel.size, a[0] / g[0], a[1] / g[1], a[2] / g[2], nl.mean() / gl.mean(), dh,
                *np.percentile(gl, [5, 50, 95]), *np.percentile(nl, [5, 50, 95]), 100.0 * (np.abs(nd / gd - 1.0) < 0.01).mean()))

    for h in heights:
        line("the placeholder's footprint, native depth at %.4f" % h, ~nnone & (np.abs(nw[..., 2] - h) < 0.05))
    line("the replay's kept pixels", r.kept)
    line("the replay's judged pixels (alpha 0.99 or more)", r.kept & (r.alpha >= JUDGED_ALPHA))
    for b in sorted(int(x) for x in np.unique(r.base[r.kept])):
        line("  kept, sampling 0x%08X" % b, r.kept & (r.base == b))
    line("the whole picture", np.ones((H, W), dtype=bool))
    return 0


def main(argv=None, say=print):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    def common(p, many=False):
        p.add_argument("capture", nargs="+" if many else None)
        p.add_argument("--place", type=float, nargs=2, default=None)
        return p

    p = common(sub.add_parser("scene"))
    p.add_argument("frame", type=int)
    p.add_argument("--flip", action="store_true")
    p.add_argument("--swap-rb", action="store_true")
    p.add_argument("--no-glint", action="store_true")
    p.add_argument("--levels", choices=("mean", "stored"), default="mean")
    p.add_argument("--normal", choices=("exact", "rounded"), default="exact")
    p.set_defaults(run=cmd_scene)
    p = common(sub.add_parser("variants"), many=True)
    p.add_argument("--frame", type=int, default=1)
    p.add_argument("--levels", choices=("mean", "stored"), default="mean")
    p.set_defaults(run=cmd_variants)
    p = sub.add_parser("sky")
    p.add_argument("capture", nargs="+")
    p.add_argument("--frame", type=int, default=1)
    p.add_argument("--png")
    p.set_defaults(run=cmd_sky)
    p = common(sub.add_parser("gap"))
    p.add_argument("dump")
    p.add_argument("--frame", type=int, default=1)
    p.set_defaults(run=cmd_gap)
    for name, run in (("lookup", cmd_lookup), ("glint", cmd_glint), ("edge", cmd_edge)):
        p = common(sub.add_parser(name))
        p.add_argument("--frame", type=int, default=1)
        if name == "lookup":
            p.add_argument("--out")
        p.set_defaults(run=run)
    args = ap.parse_args(argv)
    try:
        return args.run(args, say)
    except cr.Unreadable as e:
        say("NOT READ: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
