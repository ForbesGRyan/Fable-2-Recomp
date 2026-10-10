"""The rasterising replay of a discovery capture's draws (foliage sub-project, plan Task 3; spec Design 2,
ruling 19, criterion 5): the triangles of a capture row, their positions from the game's own vertex microcode
(with the wind) or from the row's table entry (at rest), rasterised on the CPU with numpy and held against the
game's depth copy of the same frame.

  foliage_replay.py selftest
  foliage_replay.py fit            <capture> <frame> [--cache DIR]
  foliage_replay.py depth-relation <capture> <frame> [--cache DIR]
  foliage_replay.py wind           <capture> <frame> [--cache DIR]
  foliage_replay.py pixels         <capture> <frame> [--spot NAME] [--cache DIR]
  foliage_replay.py mask           <capture> <frame> --out <npy> [--cache DIR]
  foliage_replay.py motion         <mask npy> <shot a> <shot b> ... [--box name=x0,y0,x1,y1]
  foliage_replay.py occluder       <capture> <frame> [--origin 0.0] [--out <npy>] [--cache DIR]

<capture> is a path or the end of a capture's name in the logs folder (120714).

The rasteriser (`raster`, `fragments`). A pixel (i, j) is sampled at (i + origin, j + origin) pixels from the
picture's upper left corner (origin 0.0: the pixel's integral position, the game's PA_SU_VTX_CNTL = 4, which
is NOT where the scene's depth copy holds its sample; 0.5: D3D's pixel centre; -0.25, SAMPLE_ORIGIN: the
sample the depth copy holds, below; a pair gives x and y their own). Window positions are snapped to
1 / 256 pixel and the edge functions are then whole numbers, so the top-left rule is exact: two triangles that
share an edge share no pixel and miss none (what is interpolated takes its weights from the positions as
computed, not snapped). Depth is z / w, linear on the screen; the nearest (smallest) is
kept and an equal depth goes to the later triangle (the game's test is greater-or-equal on its reversed
range). Nothing is culled by winding. A triangle with a vertex of w <= 0, or one that reaches beyond eight
pictures from the centre, is first cut in clip space (w >= 1e-6 and the guard band); a triangle with a vertex
that is no number is not drawn. With `zclip` (the default) a pixel whose z / w lies outside 0..1 is not drawn:
the clip volume's near and far planes.

What a capture row gives (`row_geometry`). Indices: the row's index file when Task 1's capture named one
("ib": "file", "file_offset", "file_bytes"), else the row's own "idx" (the first 512). A draw with fewer
indices than it issued is `partial`: its first triangles are drawn so that they hide what they hid, it is
named in every report, and it is never judged and never counted as a result. A row that cannot be replayed is
counted under its reason (no streams, a tess row, a primitive type, no microcode dump, a fetch outside a
stream's dump, a position that depends on a vertex texture, ...).

The stored depth (`load_frame`). The game's depth copy 0x1A02A000 as the sun layer's depth draw (5D67 at
pitch 560) names it: a read-back capture has its content. The copy is taken in the middle of the scene (the
last write of RB_COPY_CONTROL 0x2318 with source 4 in a "cmdgap" row, as the research's coverage_census.py
found it): only draws before it are in it. The rows' depth range is scale -1, offset 1: stored = 1 - z / w,
1 at the near plane, 0 where nothing was drawn. `depth-relation` tries both forms and three sampling places
and prints which holds. Measured in plan Task 4 on the two captures with whole index buffers (182130 sunward,
182313 bridge; out/native-evidence/foliage/t4_origin_scan_*.txt): the copy holds the scene's sample a QUARTER
PIXEL UP AND LEFT of the pixel's integral position, (i - 0.25, j - 0.25): there 98.6% and 76.6% of all
proving pixels (99.6% and 99.99% of the judged ones) lie within 16 steps at a median of 1.0 and 0.6 steps,
against 44.1% and 3.9% at the integral position, and a sixteenth of a pixel to either side loses a quarter of
them. The scene has two samples a pixel; this is the one the copy keeps. The place is measured per capture, not
assumed: `fit_place` solves it from the slope of the proving set's surfaces (stored - replayed at the integral
position is the surface's change over the offset), `fit` prints it, and `depth-relation`, `wind`, `pixels` and
`mask` judge at the fitted place rounded to 1 / 8 pixel (at SAMPLE_ORIGIN, said, when nothing can be fitted).
NOT KNOWN: whether that quarter pixel is the console's first sample of two or only how this emulator's host
renders and reads back the two-sample surface; and where the second sample sits (the copy holds one sample;
no capture row holds the other). The one-sample sun layer target has not been measured either: `occluder`
keeps its own --origin, 0.0 by default.

What `depth-relation` judges, and its bound. The proving set is the draws that do not move (a table entry
that is not "deformed" and has no "skin"), write depth, have no alpha test and no alpha-to-mask, lie before
the copy and have every index. Over the pixels the replay calls nearest for them:
  - the tolerance printed is the 99th percentile of |stored - replayed| (the plan's definition). 99% of the
    pixels hold it by construction, so it proves nothing alone, and one pixel in a hundred hidden behind what
    the replay did not draw makes it meaningless;
  - the bound is therefore taken at a tolerance fixed beforehand, FIXED_TOLERANCE, 16 steps of 2^-24. Why
    that size: the game's vertex shader forms z and w in single precision and the stored depth is their
    difference over w, so its error is a few steps of 2^-24 whatever the distance (measured on the bridge
    capture's visible unmoving draws: a median of 1.4 to 1.6 steps); a surface a pixel aside differs by far
    more. The constant is Task 3's choice.
  - the bound is one-sided, on visible pixels (the ledger's ruling 24). A pixel where the stored depth shows
    something NEARER than the replay by more than the tolerance is "hidden, not judged": the replay does not
    draw terrain or immediate rows, so a prop behind a hill is no fault of it (an inference from the sign
    alone). Among the rest, the judged pixels, at least 95% (SHARE_MIN) must lie within the tolerance; the
    others are pixels where the game has nothing as near, the replay in front of the game's depth: the error
    a wrong position makes. A set with fewer than JUDGED_MIN (1,000) judged pixels reads "not decidable".
    What the ruling costs: a draw replayed too far BACK hides among the hidden pixels. The two-sided share
    (within over all nearest pixels) is printed beside every one-sided share for that reason, and `wind`
    prints the shares with the wind and at rest side by side.
`wind` (criterion 5's second part) judges the trunk draws without an alpha test the same way: their share
with the wind must be within 3 points of the proving set's and their share at rest at least 2 points below
their share with the wind, each of the two on at least JUDGED_MIN judged pixels (`wind_verdict`), and both on
the same draws: a draw that replays whole on one side only is left out of both and counted (`common_whole`).
`mask` holds a wind draw's pixel only where the stored depth does not show something nearer (`seen_cover`:
terrain is not replayed, so a tree behind a hill would be in the mask otherwise); draws issued after the depth
copy are not in the copy and are not tested. Without a stored depth `mask` and `pixels` say so and exit 1.
Exit code: 0 when the bound is met on a frame without a partial draw, 1 when it is not or cannot be decided
(no stored depth, no copy, too few judged pixels) or the output is not a result (partial draws; `pixels`,
`mask` and `occluder` also when a draw could not be made), 2 when the capture cannot be read.

Limits, said once: terrain ("tess") and immediate ("indx", "verts") rows are not replayed; a vertex texture
is not read; the alpha of a leaf's edge is the base level's, bilinear (`alpha_cover`), where the game takes a
level by its own rule; both samples of a pixel take the first sample's place; the microcode runs in double
precision. numpy is required; `motion` also needs Pillow to read the shots.
"""
import argparse
import json
import math
import struct
import sys
from collections import Counter, namedtuple
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture_replay as cr  # noqa: E402
import fog_tables  # noqa: E402
import position_check as pc  # noqa: E402

REPO = cr.REPO
SIZE = (1120, 720)
W_MIN = 1e-6                # the near cut: w at least this
GUARD = 8.0                 # the guard band: |x|, |y| at most this many w
SUBPIXEL = 256.0            # window positions are snapped to 1 / 256 pixel
Z_EPSILON = 1e-9            # a z / w this far outside 0..1 is still on the near or the far plane
TABLE = REPO / "docs" / "native-renderer" / "vs-transforms.json"
ALBEDO = REPO / "docs" / "native-renderer" / "ps-albedo.json"
REGIONS = REPO / "docs" / "native-renderer" / "post-regions.json"
DEPTH_BASE = 0x1A02A000
LAYER_DEPTH_PS = "5D6768847B0602B8"     # light_formulas.POST_FURNITURE_SHADERS, "layer-depth"
TREE_OCCLUDER_PS = "F1EB2497108F159D"   # "tree-occluder"
LAYER_PITCH = SIZE[0] // 2
FAMILY_A = ("7CD57B81550F19E3", "00E09D1BC5295D52", "58D3FAAAA84A48AA", "2FBE9B4EE822609A")   # leaf, trunk and branch
WIND_KINDS = {"475E": "trunk", "A584": "leaf", "D7DD": "leaf", "2D40": "leaf", "8123": "plant", "B636": "plant",
              "48D3": "plant", "FC4F": "plant", "33C0": "plant", "36B5": "plant"}      # research A1, by the hash's start
RELATIONS = {"1 - z / w": (-1.0, 1.0), "z / w": (1.0, 0.0)}      # name -> (scale, offset) of stored = z / w * scale + offset
SAMPLE_ORIGIN = -0.25       # the sample the game's depth copy holds: a quarter pixel up and left (Task 4, measured)
ORIGINS = (("quarter (-0.25, -0.25)", SAMPLE_ORIGIN), ("integral (0, 0)", 0.0), ("centre (0.5, 0.5)", 0.5))
PLACE_UNIT = 0.125          # a fitted sampling place is rounded to this before anything is judged at it
DEPTH_STEP = 2.0 ** -24     # single precision's step below 1: the unit of the stored depth's error
FIXED_TOLERANCE = 16 * DEPTH_STEP    # this task's choice (module text): the share within it must reach SHARE_MIN
SHARE_MIN = 0.95            # criterion 5, first part: of the judged pixels (ruling 24)
JUDGED_MIN = 1000           # ruling 24: a judged set needs this many judged pixels, or it reads "not decidable"
WIND_NEAR_POINTS = 3.0      # criterion 5, second part: with wind within this of the unmoving draws' share
WIND_REST_POINTS = 2.0      # ... and at rest at least this far below the share with wind
MOTION_LEVELS = 8
BODY_ROWS = (0.17, 0.77)    # shot_motion.py: without the text box and the HUD's corner

Unreadable = cr.Unreadable


# --- the rasteriser ------------------------------------------------------------------------------------------

def _origin(origin):
    if np.ndim(origin) == 0:
        return float(origin), float(origin)
    ox, oy = origin
    return float(ox), float(oy)


def window(clip, size=SIZE):
    """x, y in pixels from the picture's upper left corner (y down), z / w and w of clip positions [n, 4]."""
    clip = np.asarray(clip, dtype=np.float64)
    w = clip[..., 3]
    with np.errstate(divide="ignore", invalid="ignore"):
        x = (clip[..., 0] / w * 0.5 + 0.5) * size[0]
        y = (0.5 - clip[..., 1] / w * 0.5) * size[1]
        return x, y, clip[..., 2] / w, w


def stored_depth(zw, zscale=-1.0, zoffset=1.0):
    """The depth the game stores for a clip depth z / w: the rows' viewport range (0x2113, 0x2114), scale -1
    and offset 1 in every captured row: 1 at the near plane, 0 at the far one."""
    return zw * zscale + zoffset


def _cut(poly, distance):
    """Sutherland-Hodgman: the polygon's part where distance(vertex) >= 0."""
    out = []
    d = [distance(v) for v in poly]
    for i, a in enumerate(poly):
        j = (i + 1) % len(poly)
        if d[i] >= 0.0:
            out.append(a)
        if (d[i] >= 0.0) != (d[j] >= 0.0):
            t = d[i] / (d[i] - d[j])
            out.append(a + (poly[j] - a) * t)
    return out


_PLANES = (lambda v: v[3] - W_MIN, lambda v: GUARD * v[3] + v[0], lambda v: GUARD * v[3] - v[0],
           lambda v: GUARD * v[3] + v[1], lambda v: GUARD * v[3] - v[1])


def _clip_space(v):
    """Triangles [m, 3, c] in clip space -> (triangles ready for the window, the triangle each came from).
    Triangles with no number or wholly outside the picture are left out; those that need it are cut."""
    src = np.arange(len(v))
    ok = np.isfinite(v).all(axis=(1, 2))
    x, y, w = v[..., 0], v[..., 1], v[..., 3]
    with np.errstate(invalid="ignore"):
        ok &= ~((w <= 0).all(1) | (x < -w).all(1) | (x > w).all(1) | (y < -w).all(1) | (y > w).all(1))
        v, src = v[ok], src[ok]
        x, y, w = v[..., 0], v[..., 1], v[..., 3]
        need = (w <= 0).any(1) | (np.abs(x) > GUARD * w).any(1) | (np.abs(y) > GUARD * w).any(1)
    if not need.any():
        return v, src
    extra, extra_src = [], []
    for k in np.flatnonzero(need):
        poly = list(v[k])
        for plane in _PLANES:
            poly = _cut(poly, plane)
            if len(poly) < 3:
                break
        for i in range(1, len(poly) - 1):
            extra.append(np.stack([poly[0], poly[i], poly[i + 1]]))
            extra_src.append(src[k])
    v, src = v[~need], src[~need]
    if extra:
        v = np.concatenate([v, np.stack(extra)])
        src = np.concatenate([src, np.array(extra_src, dtype=src.dtype)])
    return v, src


def fragments(clip, tris, size=SIZE, origin=0.0, zclip=True):
    """Yields (triangle, ys, xs, depth, attributes) for every triangle of `tris` [m, 3] over the vertices
    `clip` [n, 4 + a] that covers a sampling place: the pixels' rows and columns, their z / w and the extra
    columns of `clip` interpolated correct in perspective [k, a]. The module text has the rules."""
    clip = np.asarray(clip, dtype=np.float64)
    tris = np.asarray(tris, dtype=np.int64).reshape(-1, 3)
    if clip.ndim != 2 or clip.shape[1] < 4:
        raise ValueError("clip positions are [n, 4 or more]")
    if len(tris) == 0 or len(clip) == 0:
        return
    ox, oy = _origin(origin)
    width, height = size
    v, src = _clip_space(clip[tris])
    if len(v) == 0:
        return
    iw = 1.0 / v[..., 3]
    fx = (v[..., 0] * iw * 0.5 + 0.5) * width          # the window position as computed, for what is interpolated
    fy = (0.5 - v[..., 1] * iw * 0.5) * height
    x = np.rint(fx * SUBPIXEL)                          # and snapped, for the cover
    y = np.rint(fy * SUBPIXEL)
    zw = v[..., 2] * iw
    area = (x[:, 1] - x[:, 0]) * (y[:, 2] - y[:, 0]) - (y[:, 1] - y[:, 0]) * (x[:, 2] - x[:, 0])
    flip = area < 0
    for a in (x, y, fx, fy, zw, iw):
        a[flip, 1], a[flip, 2] = a[flip, 2].copy(), a[flip, 1].copy()
    v[flip, 1], v[flip, 2] = v[flip, 2].copy(), v[flip, 1].copy()
    area = np.abs(area)
    x_lo = np.maximum(np.ceil(x.min(1) / SUBPIXEL - ox), 0).astype(np.int64)
    x_hi = np.minimum(np.floor(x.max(1) / SUBPIXEL - ox), width - 1).astype(np.int64)
    y_lo = np.maximum(np.ceil(y.min(1) / SUBPIXEL - oy), 0).astype(np.int64)
    y_hi = np.minimum(np.floor(y.max(1) / SUBPIXEL - oy), height - 1).astype(np.int64)
    attrs = v[..., 4:] * iw[..., None]
    n_attr = attrs.shape[2]
    for t in np.flatnonzero((area > 0) & (x_lo <= x_hi) & (y_lo <= y_hi)):
        sx = ((np.arange(x_lo[t], x_hi[t] + 1) + ox) * SUBPIXEL)[None, :]
        sy = ((np.arange(y_lo[t], y_hi[t] + 1) + oy) * SUBPIXEL)[:, None]
        inside = None
        for i in range(3):                      # the edge opposite vertex i, from a to b; positive inside
            a, b = (i + 1) % 3, (i + 2) % 3
            ax, ay, bx, by = x[t, a], y[t, a], x[t, b], y[t, b]
            ei = (bx - ax) * (sy - ay) - (by - ay) * (sx - ax)
            top_left = (ay == by and bx > ax) or by < ay
            inside_i = (ei >= 0) if top_left else (ei > 0)
            inside = inside_i if inside is None else inside & inside_i
        if not inside.any():
            continue
        ys, xs = np.nonzero(inside)
        px, py = xs + x_lo[t] + ox, ys + y_lo[t] + oy   # the weights: of the positions as computed, not snapped
        e = [(fx[t, b] - fx[t, a]) * (py - fy[t, a]) - (fy[t, b] - fy[t, a]) * (px - fx[t, a])
             for a, b in ((1, 2), (2, 0), (0, 1))]
        whole = e[0] + e[1] + e[2]
        if (whole > 0.0).all():
            b0, b1, b2 = (ei / whole for ei in e)
        else:                                           # a sliver that has its area from the snapping alone
            b0 = b1 = b2 = np.full(len(ys), 1.0 / 3.0)
        depth = b0 * zw[t, 0] + b1 * zw[t, 1] + b2 * zw[t, 2]
        if zclip:
            keep = (depth >= -Z_EPSILON) & (depth <= 1.0 + Z_EPSILON)
            if not keep.all():
                ys, xs, b0, b1, b2, depth = (a[keep] for a in (ys, xs, b0, b1, b2, depth))
                if len(ys) == 0:
                    continue
            depth = np.clip(depth, 0.0, 1.0)
        if n_attr:
            den = b0 * iw[t, 0] + b1 * iw[t, 1] + b2 * iw[t, 2]
            out = (b0[:, None] * attrs[t, 0] + b1[:, None] * attrs[t, 1] + b2[:, None] * attrs[t, 2]) / den[:, None]
        else:
            out = np.empty((len(ys), 0))
        yield int(src[t]), ys + y_lo[t], xs + x_lo[t], depth, out


def raster(draws, size=SIZE, origin=0.0, zclip=True):
    """(depth, ident), each [height, width]: `draws` is a list of (clip [n, 4], tris [m, 3], ident), drawn in
    order; depth is the nearest z / w (infinity where nothing is drawn), ident the draw that gave it (0 where
    nothing is drawn; an ident of 0 is refused)."""
    depth = np.full((size[1], size[0]), np.inf)
    ident = np.zeros((size[1], size[0]), dtype=np.int32)
    for clip, tris, mark in draws:
        if int(mark) == 0:
            raise ValueError("ident 0 means nothing drawn")
        for _, ys, xs, d, _ in fragments(np.asarray(clip)[:, :4], tris, size, origin, zclip):
            keep = d <= depth[ys, xs]
            depth[ys[keep], xs[keep]] = d[keep]
            ident[ys[keep], xs[keep]] = mark
    return depth, ident


# --- shots ---------------------------------------------------------------------------------------------------

def fit_mask(mask, shape):
    """A mask brought to another picture size (rows, columns): the nearest of its pixels."""
    mask = np.asarray(mask, dtype=bool)
    rows = np.minimum((np.arange(shape[0]) + 0.5) * mask.shape[0] / shape[0], mask.shape[0] - 1).astype(np.int64)
    cols = np.minimum((np.arange(shape[1]) + 0.5) * mask.shape[1] / shape[1], mask.shape[1] - 1).astype(np.int64)
    return mask[rows][:, cols]


def motion_share(a, b, mask, levels=MOTION_LEVELS):
    """(inside, outside): the share of the pixels inside the mask, and outside it, whose largest channel
    difference between the pictures a and b [rows, columns, channels] is more than `levels`. NaN for an
    empty side."""
    a, b = np.asarray(a), np.asarray(b)
    if a.shape != b.shape or a.shape[:2] != np.shape(mask):
        raise ValueError("pictures of %s and %s under a mask of %s" % (a.shape, b.shape, np.shape(mask)))
    moved = np.abs(a.astype(np.int64) - b.astype(np.int64)).max(axis=2) > levels
    mask = np.asarray(mask, dtype=bool)
    inside = float(moved[mask].mean()) if mask.any() else math.nan
    outside = float(moved[~mask].mean()) if (~mask).any() else math.nan
    return inside, outside


# --- a row's geometry ----------------------------------------------------------------------------------------

Geometry = namedtuple("Geometry", "clip tris ids state note reason outputs")


def _args(row):
    return [int(str(a), 16) for a in row.get("args") or []]


def _index_file(ib, capture_dir):
    """(the draw's index words from the row's index file, None) or (None, why it was not read)."""
    index32 = bool(ib.get("index32"))
    isz = 4 if index32 else 2
    endian = int(ib.get("endian", 1 if not index32 else 2))
    if not index32 and endian not in (0, 1):
        return None, "endian %d with 16-bit indices" % endian
    first = int(ib["start"]) * isz - int(ib.get("file_offset", 0))
    need = int(ib["count"]) * isz
    try:
        data = (Path(capture_dir) / ib["file"]).read_bytes()
    except OSError:
        return None, "missing"
    if len(data) != int(ib.get("file_bytes", -1)):
        return None, "%d bytes, not the row's %s" % (len(data), ib.get("file_bytes"))
    if first < 0 or first + need > len(data):
        return None, "the draw's range is not inside it"
    words = np.frombuffer(data, dtype="<u4" if index32 else "<u2", count=int(ib["count"]), offset=first).astype(np.int64)
    if endian == 1:                                     # stream_resolve.h, ScanIndexWords
        words = ((words & 0x00FF00FF) << 8) | ((words >> 8) & 0x00FF00FF)
    elif endian == 2:
        words = (words >> 24) | ((words >> 8) & 0xFF00) | ((words << 8) & 0xFF0000) | ((words << 24) & 0xFF000000)
    elif endian == 3:
        words = (words >> 16) | ((words << 16) & 0xFFFF0000)
    words[words == (0xFFFFFFFF if index32 else 0xFFFF)] = -1
    return [int(i) for i in words], None


def draw_indices(row, capture_dir):
    """(indices, state, note) of a "draw" row: the indices the draw issued, a reset index as -1, the base
    vertex not added; state "whole" or "partial" (fewer than the draw issued: the row's first 512); the note
    says how many of how many and why an index file was not read."""
    ib = row.get("ib")
    args = _args(row)
    if not ib:                                          # DrawVertices: start vertex, vertex count
        if len(args) < 3 or not 0 < args[2] <= 1 << 22:
            raise Unreadable("no index buffer and no vertex count")
        return list(range(args[1], args[1] + args[2])), "whole", ""
    count = int(ib["count"])
    note = ""
    if ib.get("file"):
        words, why = _index_file(ib, capture_dir)
        if words is not None:
            return words, "whole", ""
        note = "; index file not read: %s" % why
    own = [int(i) for i in row.get("idx") or []]
    if len(own) >= count:
        return own[:count], "whole", note.lstrip("; ")
    return own, "partial", "%d of %d indices" % (len(own), count) + note


def triangles_of(prim, idx):
    """(the distinct vertex indices, ascending; the triangles [m, 3] as places in them) of a draw's indices,
    None for a primitive type that is not a triangle list, fan or strip."""
    tris = pc.triangles(prim, idx)
    if tris is None:
        return None
    flat = np.array([int(i) for i in idx if int(i) >= 0], dtype=np.int64)
    if len(flat) == 0:
        return flat, np.zeros((0, 3), dtype=np.int64)
    ids, inverse = np.unique(flat, return_inverse=True)
    return ids, inverse[np.array(tris, dtype=np.int64).reshape(-1, 3)]


_tables = {}


def load_table(path=TABLE):
    if str(path) not in _tables:
        _tables[str(path)] = json.loads(Path(path).read_text(encoding="utf-8"))
    return _tables[str(path)]


def moving(row, table):
    """Whether the row's vertex shader moves its vertices with the wind: a "deformed" table entry."""
    return bool((table.get("0x" + cr.vs_hash(row)) or {}).get("deformed"))


def kind_of(row, table):
    """What a draw row is to the judgement: "trunk", "leaf", "plant" or "moving" (a "deformed" table entry:
    the wind), "skinned" (an entry with "skin": it moves with its bones), "unlisted" (no entry with a
    transform: nothing says that it stands), else "still"."""
    entry = table.get("0x" + cr.vs_hash(row)) or {}
    if entry.get("deformed"):
        return WIND_KINDS.get(cr.vs_hash(row)[:4], "moving")
    if "skin" in entry:
        return "skinned"
    return "still" if "base" in entry and "base2" not in entry else "unlisted"


def _none(state, note, reason):
    return Geometry(None, None, None, state, note, reason, {})


def row_geometry(row, capture_dir, wind=True, dumps=cr.DUMPS, table=None, outputs=()):
    """A row's triangles: Geometry(clip [n, 4] of its distinct vertices, tris [m, 3], the vertices' indices
    with the base vertex added, state, note, reason, outputs). wind=True: oPos from the game's microcode on
    the row's streams and bank; wind=False: the table entry's position at rest under the row's constants.
    `outputs`: names of other microcode outputs to keep ("o1": [n, 4]; wind=True only). A row that cannot be
    replayed has clip None and its reason; nothing is raised for it."""
    kind = row.get("kind")
    if kind != "draw":
        return _none("none", "", "a tess row (terrain)" if kind == "tess" else "a%s %s row (immediate vertices)" % (
            "n" if kind == "indx" else "", kind))
    if not row.get("streams"):
        return _none("none", "", "no streams in the row")
    args = _args(row)
    try:
        idx, state, note = draw_indices(row, capture_dir)
    except Unreadable as e:
        return _none("none", "", str(e))
    made = triangles_of(args[0] if args else -1, idx)
    if made is None:
        return _none(state, note, "primitive type %d" % (args[0] if args else -1))
    ids, tris = made
    base = int(row.get("base_vertex", 0))
    ids = ids + base
    extra = {}
    if wind:
        if not (Path(dumps) / ("shader_%s.ucode.vert" % cr.vs_hash(row))).is_file():
            return _none(state, note, "no microcode dump")
        try:
            draw = cr.Draw(Path(capture_dir) / "capture", row, dumps=dumps)     # Draw reads beside the capture's path
            clip = np.empty((len(ids), 4))
            extra = {name: np.full((len(ids), 4), np.nan) for name in outputs}
            probed = None
            for k, v in enumerate(ids):
                out = draw.run(int(v))[0]
                clip[k] = out["oPos"]
                for name in outputs:
                    if name in out:
                        extra[name][k] = out[name]
                if probed is None and np.isfinite(clip[k]).all():
                    probed = draw.texture_position(int(v))
                    if probed:
                        return _none(state, note, "a position that depends on a vertex texture")
        except IndexError:
            return _none(state, note, "a fetch outside the stream's dump")
        except Unreadable as e:
            return _none(state, note, str(e))
        except NotImplementedError as e:
            return _none(state, note, "not interpreted: %s" % e)
        except (ValueError, KeyError, ZeroDivisionError, OverflowError) as e:
            return _none(state, note, "microcode: %s" % type(e).__name__)
    else:
        if outputs:
            raise ValueError("outputs come from the microcode (wind=True)")
        entry = table.get("0x" + cr.vs_hash(row)) if table is not None else load_table().get("0x" + cr.vs_hash(row))
        try:
            spec = pc.parse_entry(entry) if entry else None
        except (ValueError, KeyError, TypeError):
            spec = None
        if spec is None:
            return _none(state, note, "no table entry")
        at_rest = dict(row, idx=[int(i) - base for i in ids])
        try:
            positions = pc.positions_for(at_rest, entry, capture_dir)
            clip = np.array([pc.project(row, spec, p) for p in positions], dtype=np.float64).reshape(-1, 4)
        except pc.Unreadable as e:
            return _none(state, note, "table entry: %s" % e)
        except (pc.Unsupported, pc.BadIndex) as e:
            return _none(state, note, "table entry: %s" % type(e).__name__)
    return Geometry(clip, tris, ids, state, note, None, extra)


def row_clip(row, capture_dir, wind=True, dumps=cr.DUMPS, table=None):
    """The row's clip positions [n, 4], one for each of its indices that is not a reset, in their order (as
    position_check.positions_for lists positions). Unreadable with the reason when the row cannot be replayed."""
    g = row_geometry(row, capture_dir, wind=wind, dumps=dumps, table=table)
    if g.reason:
        raise Unreadable(g.reason)
    idx = draw_indices(row, capture_dir)[0]
    flat = np.array([i for i in idx if i >= 0], dtype=np.int64) + int(row.get("base_vertex", 0))
    return g.clip[np.searchsorted(g.ids, flat)]


def _state_word(row, reg):
    return int((row.get("rs") or {}).get(reg, "0"), 16)


def writes_depth(row):
    return bool(_state_word(row, "2200") & 4)


def alpha_tested(row):
    return bool(_state_word(row, "2202") & 0x18)       # the alpha test or alpha-to-mask


def judged(row, geometry, kind):
    """Whether a draw belongs to the proving set: replayed with every index, of kind "still" (kind_of),
    writing depth, no alpha test and no alpha-to-mask (the replay does not know a texture's holes)."""
    return (geometry.reason is None and geometry.state == "whole" and kind == "still" and writes_depth(row)
            and not alpha_tested(row))


# --- the judgement against a stored depth --------------------------------------------------------------------

class Agreement(namedtuple("Agreement", "pixels within nearer farther")):
    @property
    def share(self):
        return self.within / self.pixels if self.pixels else math.nan

    @property
    def judged(self):
        """The pixels that are judged (ruling 24): those where the game has nothing nearer beyond the
        tolerance. `nearer` is the hidden ones."""
        return self.within + self.farther

    @property
    def share_judged(self):
        """The share of the judged pixels that lie within the tolerance: criterion 5's share."""
        return self.within / self.judged if self.judged else math.nan


Relation = namedtuple("Relation", "name pixels tolerance median at_tolerance fixed")


def _nearest(ident, ids):
    return np.isin(ident, np.asarray(list(ids), dtype=np.int64)) if len(ids) else np.zeros(ident.shape, dtype=bool)


def agreement(stored, depth, ident, ids, tolerance, relation="1 - z / w"):
    """Over the pixels the replay calls nearest for the draws `ids`: how many hold the stored depth within
    `tolerance` of the replayed one (stored units), in how many the game has something nearer, in how many
    nothing as near."""
    scale, offset = RELATIONS[relation]
    sel = _nearest(ident, ids)
    diff = stored[sel] - stored_depth(depth[sel], scale, offset)
    if scale < 0:                       # reversed: the larger, the nearer
        nearer, farther = diff > tolerance, diff < -tolerance
    else:
        nearer, farther = diff < -tolerance, diff > tolerance
    n = int(sel.sum())
    return Agreement(n, n - int(nearer.sum()) - int(farther.sum()), int(nearer.sum()), int(farther.sum()))


def relations(stored, depth, ident, ids):
    """Each form of the relation over the draws' nearest pixels: its tolerance (the 99th percentile of
    |stored - replayed|), the median, and the agreement at that tolerance and at FIXED_TOLERANCE."""
    out = {}
    sel = _nearest(ident, ids)
    for name, (scale, offset) in RELATIONS.items():
        diff = np.abs(stored[sel] - stored_depth(depth[sel], scale, offset))
        if len(diff) == 0:
            out[name] = Relation(name, 0, math.nan, math.nan, Agreement(0, 0, 0, 0), Agreement(0, 0, 0, 0))
            continue
        tol = float(np.percentile(diff, 99))
        out[name] = Relation(name, int(len(diff)), tol, float(np.median(diff)),
                             agreement(stored, depth, ident, ids, tol, name),
                             agreement(stored, depth, ident, ids, FIXED_TOLERANCE, name))
    return out


def _better(a, b):
    """Whether relation a holds better than b: more of its pixels within the fixed tolerance, then the
    smaller median (a relation without a pixel is never better)."""
    if a.pixels == 0 or b.pixels == 0:
        return a.pixels > 0
    return (a.fixed.share, -a.median) > (b.fixed.share, -b.median)


def best_relation(rel):
    """The name of the form that holds best (the first, when neither has a pixel)."""
    best = None
    for name in rel:
        if best is None or _better(rel[name], rel[best]):
            best = name
    return best


Place = namedtuple("Place", "x y pixels")


def fit_place(stored, draws, ids, size=SIZE, relation="1 - z / w", step=0.25, reach=1.5):
    """Where in the pixel the stored depth was sampled, from the slope of the surfaces of the draws `ids`:
    Place(x, y, pixels) as offsets from the integral position, or None when nothing can be fitted. On a plane
    z / w = a x + b y + c the depth at the place (ox, oy) differs from the depth at the integral position by
    a ox + b oy, so stored - replayed(0, 0) = scale * (a ox + b oy): two unknowns, least squares over every
    pixel that (1) is the same draw's at the integral position and a quarter pixel to each side, on one plane
    (so that a and b are the plane's own), (2) changes by more than the tolerance over a pixel, and (3) differs
    from the stored depth by at most `reach` pixels' change (a hidden pixel, or one where the game has nothing,
    differs by more); the fit is made twice, the second time without the pixels that miss the first by more
    than an eighth of a pixel's change."""
    scale, offset = RELATIONS[relation]
    d0, i0 = raster(draws, size, 0.0)
    sel = _nearest(i0, ids)
    grads = []
    with np.errstate(invalid="ignore"):
        for o in ((step, 0.0), (0.0, step)):
            dp, ip = raster(draws, size, o)
            dm, im = raster(draws, size, (-o[0], -o[1]))
            g = (dp - dm) / (2.0 * step)
            sel &= (ip == i0) & (im == i0) & np.isfinite(g)
            sel &= np.abs(dp + dm - 2.0 * d0) <= 0.01 * step * np.abs(g) + 1e-12       # one plane through the three
            grads.append(g)
        a, b = grads
        change = np.abs(a) + np.abs(b)
        diff = stored - stored_depth(d0, scale, offset)
        keep = sel & (change > FIXED_TOLERANCE) & (np.abs(diff) <= reach * change)
    place = None
    for _ in range(2):
        n = int(keep.sum())
        if n < 16:
            return None
        m = np.stack([a[keep], b[keep]], axis=1) * scale
        solution, _, rank, _ = np.linalg.lstsq(m, diff[keep], rcond=None)
        if rank < 2:
            return None
        place = Place(float(solution[0]), float(solution[1]), n)
        with np.errstate(invalid="ignore"):
            miss = np.abs(diff - scale * (a * place.x + b * place.y))
            keep = keep & (miss <= 0.125 * change + FIXED_TOLERANCE)
    return place


def round_place(place, unit=PLACE_UNIT):
    """A fitted place as the pair a raster takes, each coordinate at the nearest `unit`."""
    return (round(place.x / unit) * unit + 0.0, round(place.y / unit) * unit + 0.0)


def seen_cover(stored, depth, ident, tested, untested, tolerance, zscale=-1.0, zoffset=1.0):
    """The pixels the replay calls nearest for the draws `tested` where the stored depth does not show something
    nearer by more than `tolerance`, and every nearest pixel of the draws `untested` (issued after the depth
    copy: the copy does not hold them)."""
    mine = _nearest(ident, tested)
    with np.errstate(invalid="ignore"):
        diff = stored - stored_depth(depth, zscale, zoffset)
        hidden = diff > tolerance if zscale < 0 else diff < -tolerance
    return (mine & ~hidden) | _nearest(ident, untested)


def _whole(item):
    return item.geometry.reason is None and item.geometry.state == "whole"


def common_whole(a_items, b_items, pick):
    """(idents in a, idents in b, dropped) of the rows `pick` takes that are replayed whole in both lists (two
    replays of the same rows in the same order); dropped counts the picked rows that are not."""
    a_ids, b_ids, dropped = [], [], 0
    for i, j in zip(a_items, b_items):
        if not pick(i):
            continue
        if _whole(i) and _whole(j):
            a_ids.append(i.ident)
            b_ids.append(j.ident)
        else:
            dropped += 1
    return a_ids, b_ids, dropped


def agreement_verdict(a, judged_min=JUDGED_MIN):
    """(met, text) for criterion 5's bound on one Agreement at the fixed tolerance, as ruling 24 has it: the
    text begins "met", "NOT MET" or "not decidable" (fewer than `judged_min` judged pixels)."""
    counts = "of the %d nearest pixels %d hidden, not judged (the game has something nearer)" % (a.pixels, a.nearer)
    if a.judged < judged_min or a.judged == 0:
        return False, "not decidable: %d judged pixels, fewer than %d; %s" % (a.judged, max(judged_min, 1), counts)
    text = "%.2f%% of the %d judged pixels within %.2e (%d steps of 2^-24); %s" % (
        100.0 * a.share_judged, a.judged, FIXED_TOLERANCE, round(FIXED_TOLERANCE / DEPTH_STEP), counts)
    if not a.share_judged >= SHARE_MIN:
        return False, "NOT MET: %s; below %.0f%%" % (text, 100.0 * SHARE_MIN)
    return True, "met: %s; at least %.0f%%" % (text, 100.0 * SHARE_MIN)


def relation_verdict(rel, judged_min=JUDGED_MIN):
    """(met, text) for criterion 5's first part on one relation (the module text has the bound)."""
    return agreement_verdict(rel.fixed, judged_min)


def wind_verdict(proving, wind, rest, judged_min=JUDGED_MIN):
    """(met, text) for criterion 5's second part from three Agreements at the fixed tolerance: the proving
    set's, the judged trunk draws' with the wind and theirs at rest. Shares are of judged pixels."""
    if not agreement_verdict(proving, judged_min)[0]:
        return False, "not decidable (the first part is not met)"
    for name, a in (("with the wind", wind), ("at rest", rest)):
        if a.judged < judged_min or a.judged == 0:
            return False, "not decidable: %d judged pixels of the trunk draws %s, fewer than %d" % (
                a.judged, name, max(judged_min, 1))
    near = abs(wind.share_judged - proving.share_judged) * 100.0 <= WIND_NEAR_POINTS + 1e-9
    below = (wind.share_judged - rest.share_judged) * 100.0 >= WIND_REST_POINTS - 1e-9
    return near and below, "%s: with wind %.2f%% of %d judged pixels against the proving set's %.2f%% (within %.0f points: %s); " \
        "at rest %.2f%% of %d (at least %.0f points below: %s)" % (
            "met" if near and below else "NOT MET", 100.0 * wind.share_judged, wind.judged, 100.0 * proving.share_judged,
            WIND_NEAR_POINTS, "yes" if near else "no", 100.0 * rest.share_judged, rest.judged, WIND_REST_POINTS,
            "yes" if below else "no")


# --- alpha ---------------------------------------------------------------------------------------------------

class Alpha:
    """A texture's alpha at its base level, [rows, columns] in 0..1, sampled bilinear."""

    def __init__(self, alpha, wrap=True):
        self.alpha = np.asarray(alpha, dtype=np.float64)
        if self.alpha.ndim != 2 or self.alpha.size == 0:
            raise ValueError("an alpha plane is [rows, columns]")
        self.wrap = wrap

    def sample(self, uv):
        uv = np.asarray(uv, dtype=np.float64).reshape(-1, 2)
        h, w = self.alpha.shape
        ok = np.isfinite(uv).all(1) & (np.abs(uv) < 1e6).all(1)
        u, v = np.where(ok, uv[:, 0], 0.0), np.where(ok, uv[:, 1], 0.0)
        x, y = u * w - 0.5, v * h - 0.5
        x0, y0 = np.floor(x), np.floor(y)
        fx, fy = x - x0, y - y0
        x0, y0 = x0.astype(np.int64), y0.astype(np.int64)
        if self.wrap:
            xa, xb, ya, yb = x0 % w, (x0 + 1) % w, y0 % h, (y0 + 1) % h
        else:
            xa, xb = np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1)
            ya, yb = np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)
        a = self.alpha
        top = a[ya, xa] * (1.0 - fx) + a[ya, xb] * fx
        bottom = a[yb, xa] * (1.0 - fx) + a[yb, xb] * fx
        return np.where(ok, top * (1.0 - fy) + bottom * fy, 0.0)


def alpha_scale(row, c26w=None):
    """A family A row's alpha scale: pixel c15.w (the fade word) times c26.w. c26 is read from the row's
    pixel registers ("pfull"); a row without them raises Unreadable unless `c26w` is given."""
    pconst = row.get("pconst") or []
    if len(pconst) < 64:
        raise Unreadable("no pixel c15 in the row")
    if c26w is None:
        c26 = fog_tables.pfull_reg(row.get("pfull") or "", 26)
        if c26 is None:
            raise Unreadable("no pixel c26 in the row")
        c26w = c26[3]
    scale = float(pconst[63]) * float(c26w)
    if not math.isfinite(scale):
        raise Unreadable("an alpha scale that is no number")
    return scale


def alpha_cover(row, uv, texture, parity, samples=2, scale=None):
    """(first, second): whether the first and the second sample of each pixel is covered, for a row's alpha
    state (0x2202: bit 3 the test "greater" against 0x210E, bit 4 alpha-to-mask, bits 24 to 31 the offsets).
    uv [n, 2]: the texture coordinates; texture: an Alpha; parity [n]: (x & 1) * 2 + (y & 1) of each pixel;
    samples: the target's sample count, 2 (the scene: alpha >= 0.5 - o / 8 and alpha >= 1 - o / 8) or 1 (the
    sun layer: alpha >= 1 - o / 4, no second sample); scale: the alpha's scale, the row's (alpha_scale) when
    not given. The alpha is the base level's, bilinear."""
    if samples not in (1, 2):
        raise ValueError("%r samples" % (samples,))
    word = _state_word(row, "2202")
    alpha = texture.sample(uv) * (alpha_scale(row) if scale is None else scale)
    parity = np.asarray(parity, dtype=np.int64)
    keep = np.ones(len(alpha), dtype=bool)
    if word & 8:
        if word & 7 != 4:
            raise Unreadable("alpha function %d, not 4 (greater)" % (word & 7))
        ref = struct.unpack(">f", struct.pack(">I", _state_word(row, "210E")))[0]
        keep = alpha > ref
    if not word & 0x10:
        return keep, (keep if samples == 2 else np.zeros(len(alpha), dtype=bool))
    o = ((word >> 24) >> (2 * parity)) & 3
    if samples == 2:
        return keep & (alpha >= 0.5 - o / 8.0), keep & (alpha >= 1.0 - o / 8.0)
    return keep & (alpha >= 1.0 - o / 4.0), np.zeros(len(alpha), dtype=bool)


# --- a frame of a capture ------------------------------------------------------------------------------------

def _register_writes(words, reg):
    """The values a command stream (dwords) writes to register `reg` (type 0 and type 1 packets)."""
    out, i, n = [], 0, len(words)
    while i < n:
        h = words[i]
        kind = h >> 30
        if kind == 0:
            base, one, count = h & 0x7FFF, (h >> 15) & 1, ((h >> 16) & 0x3FFF) + 1
            for k in range(count):
                if i + 1 + k < n and (base if one else base + k) == reg:
                    out.append(words[i + 1 + k])
            i += 1 + count
        elif kind == 1:
            a, b = h & 0x7FF, (h >> 11) & 0x7FF
            if a == reg and i + 1 < n:
                out.append(words[i + 1])
            if b == reg and i + 2 < n:
                out.append(words[i + 2])
            i += 3
        elif kind == 2:
            i += 1
        else:
            i += 2 + ((h >> 16) & 0x3FFF)
    return out


def depth_copy_line(rows):
    """The "line" of the frame's last "cmdgap" row inside the main scene that writes RB_COPY_CONTROL (0x2318)
    with source 4, the depth; None if there is none. (The research's coverage_census.py.)"""
    line, inside = None, False
    for r in rows:
        if r.get("kind") == "cmdgap":
            c = r.get("cmd")
            if inside and c:
                words = [int(c[i:i + 8], 16) for i in range(0, len(c) - 7, 8)]
                if any((v & 7) == 4 for v in _register_writes(words, 0x2318)):
                    line = r["line"]
        elif r.get("in_scene") and r.get("kind") in ("draw", "tess"):
            inside = True
    return line


class Frame:
    """A capture frame: scene (its main-scene rows, in order), copy_line, before_copy (the scene's draw and
    tess rows before the depth copy), layer (the rows outside the scene at the sun layer's target), textures,
    depth ([720, 1120] as stored, or None with depth_note saying why)."""

    def __init__(self, path, number):
        self.path, self.number = Path(path), number
        self.scene, self.layer, self.textures, self.before_copy = [], [], [], []
        self.copy_line = None
        self.depth, self.depth_note, self.depth_content = None, "not read", None
        self.zscale, self.zoffset = -1.0, 1.0

    def rows_before_copy(self):
        """Every main-scene row before the depth copy, the immediate rows among them: what a replay of the
        copy has to account for."""
        return [r for r in self.scene if self.copy_line is not None and r["line"] < self.copy_line]

    def texture(self, base, content=None):
        for t in self.textures:
            if t.base == base and (content is None or t.content == content):
                return t
        raise Unreadable("no dump of texture 0x%08X%s" % (base, "" if content is None else " with content %016X" % content))


def _pitch(row):
    return int(row.get("surface_info", "0x0"), 16) & 0x3FFF


def load_frame(path, number, depth=True):
    """Frame `number` of a capture. depth=False leaves the stored depth unread."""
    fr = Frame(path, number)
    rows = cr.rows(path, cr.SCENE_KINDS + ("cmdgap", "texture"), frame=number)
    if not any(r["kind"] != "texture" for r in rows):
        raise Unreadable("%s has no frame %d" % (Path(path).name, number))
    fr.textures = [fog_tables.Texture(r, Path(path).parent) for r in rows if r["kind"] == "texture"]
    fr.scene = [r for r in rows if r["kind"] in cr.SCENE_KINDS and r.get("in_scene")]
    fr.layer = [r for r in rows if r["kind"] in cr.SCENE_KINDS and not r.get("in_scene") and _pitch(r) == LAYER_PITCH]
    fr.copy_line = depth_copy_line(rows)
    if fr.copy_line is not None:
        fr.before_copy = [r for r in fr.scene if r["kind"] in ("draw", "tess") and r["line"] < fr.copy_line]
    if not depth:
        return fr
    first = next((r for r in fr.layer if cr.ps_hash(r) == LAYER_DEPTH_PS), None)
    if first is None:
        fr.depth_note = "no layer depth draw (5D67 at pitch %d) in the frame" % LAYER_PITCH
        return fr
    fc = {int(s): [int(x, 16) for x in v] for s, v in (first.get("tf") or {}).items()}
    tfc = {int(s): int(v, 16) for s, v in (first.get("tfc") or {}).items()}
    slot = min(fc) if fc else None
    if slot is None or tfc.get(slot) is None:
        fr.depth_note = "the layer's depth draw names no depth content (not a read-back capture)"
        return fr
    try:
        t = fr.texture(fc[slot][1] & 0xFFFFF000, tfc[slot])
        w, h, values = fog_tables.decode_depth(t.read(), fc[slot])
    except (Unreadable, OSError, ValueError) as e:
        fr.depth_note = "the depth copy cannot be read: %s" % e
        return fr
    if (w, h) != SIZE:
        fr.depth_note = "the depth copy is %d x %d" % (w, h)
        return fr
    rs = first.get("rs") or {}
    for name, reg in (("zscale", "2113"), ("zoffset", "2114")):
        if reg in rs:
            setattr(fr, name, struct.unpack(">f", struct.pack(">I", int(rs[reg], 16)))[0])
    fr.depth = np.array(values, dtype=np.float64).reshape(h, w)
    fr.depth_content = tfc[slot]
    fr.depth_note = "content %016X of 0x%08X" % (tfc[slot], fc[slot][1] & 0xFFFFF000)
    return fr


# --- replaying a frame ---------------------------------------------------------------------------------------

Item = namedtuple("Item", "ident row geometry moving kind judged")
MOVING_KINDS = ("trunk", "leaf", "plant", "moving")


def _cached_geometry(row, fr, wind, table, outputs, cache):
    if cache is None:
        return row_geometry(row, fr.path.parent, wind=wind, table=table, outputs=outputs)
    name = Path(cache) / ("%s_f%d_l%d_%s%s.npz" % (fr.path.stem, fr.number, row["line"], "wind" if wind else "rest",
                                                    "".join("_" + o for o in outputs)))
    if name.is_file():
        z = np.load(name, allow_pickle=False)
        reason = str(z["reason"]) or None
        if reason:
            return _none(str(z["state"]), str(z["note"]), reason)
        return Geometry(z["clip"], z["tris"], z["ids"], str(z["state"]), str(z["note"]), None, {o: z["out_" + o] for o in outputs})
    g = row_geometry(row, fr.path.parent, wind=wind, table=table, outputs=outputs)
    Path(cache).mkdir(parents=True, exist_ok=True)
    empty = np.zeros((0, 4))
    np.savez(name, clip=g.clip if g.clip is not None else empty, tris=g.tris if g.tris is not None else np.zeros((0, 3), np.int64),
             ids=g.ids if g.ids is not None else np.zeros(0, np.int64), state=g.state, note=g.note, reason=g.reason or "",
             **{"out_" + o: g.outputs.get(o, empty) for o in outputs})
    return g


def replay_rows(fr, rows, table, wind=True, at_rest=(), cache=None):
    """Items for `rows` in order (ident from 1): each row's geometry from the microcode, or at rest for the
    wind kinds named in `at_rest` (and for every row with wind=False). Rows that do not write depth get no
    geometry (reason "no depth write")."""
    items = []
    for n, row in enumerate(rows, 1):
        kind = kind_of(row, table) if row.get("kind") == "draw" else str(row.get("kind"))
        if row.get("kind") == "draw" and not writes_depth(row):
            g = _none("none", "", "no depth write")
        else:
            g = _cached_geometry(row, fr, wind and kind not in at_rest, table, (), cache)
        items.append(Item(n, row, g, kind in MOVING_KINDS, kind, judged(row, g, kind)))
    return items


def reasons(items):
    """What was not replayed, as text lines: counts per reason, and the partial draws."""
    c = Counter(i.geometry.reason for i in items if i.geometry.reason)
    lines = ["  not replayed: %d rows" % sum(c.values())] + ["    %5d  %s" % (v, k) for k, v in c.most_common()]
    partial = [i for i in items if i.geometry.reason is None and i.geometry.state == "partial"]
    lines.append("  partial (fewer indices than the draw issued; drawn as far as they go, never judged): %d draws" % len(partial))
    return lines


def _draw_list(items):
    return [(i.geometry.clip, i.geometry.tris, i.ident) for i in items if i.geometry.reason is None]


def _head(fr, say):
    say("%s frame %d: %d main-scene rows (%s); depth copy %s; stored depth: %s" % (
        fr.path.name, fr.number, len(fr.scene),
        ", ".join("%d %s" % (v, k) for k, v in Counter(r["kind"] for r in fr.scene).most_common()),
        "after %d of the %d draw and tess rows (line %d)" % (
            len(fr.before_copy), sum(1 for r in fr.scene if r["kind"] in ("draw", "tess")), fr.copy_line)
        if fr.copy_line is not None else "NOT FOUND", fr.depth_note))


def _partial_lines(items, say, limit=400):
    for i in items[:limit]:
        if i.geometry.reason is None and i.geometry.state == "partial":
            say("    partial: line %d vs %s (%s): %s" % (i.row["line"], cr.vs_hash(i.row)[:4], i.kind, i.geometry.note))


def _relation_name(fr):
    return "1 - z / w" if fr.zscale < 0 else "z / w"


def sample_place(fr, items, say):
    """The pair at which the frame's stored depth is judged: the place fitted over the proving set of `items`
    (rows before the depth copy), rounded to PLACE_UNIT; (SAMPLE_ORIGIN, SAMPLE_ORIGIN), said, when the frame
    has no stored depth or fewer than JUDGED_MIN pixels carry the fit."""
    fallback = (SAMPLE_ORIGIN, SAMPLE_ORIGIN)
    if fr.depth is None:
        say("  the sampling place: not fitted (no stored depth: %s); (%g, %g) is taken, Task 4's measurement" % (
            (fr.depth_note,) + fallback))
        return fallback
    ids = [i.ident for i in items if i.judged]
    place = fit_place(fr.depth, _draw_list(items), ids, relation=_relation_name(fr))
    if place is None or place.pixels < JUDGED_MIN:
        say("  the sampling place: not fitted (%d sloped pixels of the proving set, fewer than %d); (%g, %g) is taken, "
            "Task 4's measurement" % ((place.pixels if place else 0, JUDGED_MIN) + fallback))
        return fallback
    rounded = round_place(place)
    say("  the sampling place of the stored depth, fitted over %d sloped pixels of the proving set: (%.3f, %.3f) pixel from "
        "the integral position; judged at (%g, %g)" % ((place.pixels, place.x, place.y) + rounded))
    return rounded


def _proving(fr, items, say, origins=ORIGINS):
    """Rasters the items at the fitted sampling place and at each place of `origins`, and judges the proving
    set; returns (origin, relation name, Relation, depth, ident) of the best, or None when nothing can be
    judged."""
    ids = [i.ident for i in items if i.judged]
    place = sample_place(fr, items, say)
    places = [("fitted (%g, %g)" % place, place)] + [(label, o) for label, o in origins if _origin(o) != place]
    best = None
    for label, origin in places:
        depth, ident = raster(_draw_list(items), SIZE, origin)
        rel = relations(fr.depth, depth, ident, ids)
        for name in RELATIONS:
            r = rel[name]
            say("  sampled at %-28s stored = %-9s: %7d pixels, median |stored - replayed| %.3e, 99th percentile %.3e; "
                "within %.2e: %s of all, %s of the %d judged" % (
                    label + ",", name, r.pixels, r.median, r.tolerance, FIXED_TOLERANCE,
                    "%.2f%%" % (100.0 * r.fixed.share) if r.pixels else "-",
                    "%.2f%%" % (100.0 * r.fixed.share_judged) if r.fixed.judged else "-", r.fixed.judged))
            if r.pixels and (best is None or _better(r, best[2])):
                best = (origin, name, r, depth, ident)
    return best


def _undecidable(fr, say):
    if fr.depth is None:
        say("not decidable: %s" % fr.depth_note)
        return True
    if fr.copy_line is None:
        say("not decidable: the depth copy's place in the scene was not found")
        return True
    return False


def cmd_fit(args, say=print):
    """The stored depth's sampling place, fitted on each kind of whole draw without an alpha test."""
    fr = load_frame(cr.find_capture(args.capture), args.frame)
    _head(fr, say)
    if _undecidable(fr, say):
        return 1
    table = load_table()
    items = replay_rows(fr, fr.rows_before_copy(), table, cache=args.cache)
    draws = _draw_list(items)
    say("the sampling place of the stored depth (offsets from the pixel's integral position; least squares over sloped "
        "pixels, `fit_place`), by kind of draw, whole draws that write depth without an alpha test:")
    found = None
    for kind in ("still", "trunk", "skinned", "leaf", "plant"):
        ids = [i.ident for i in items if i.kind == kind and _whole(i) and writes_depth(i.row) and not alpha_tested(i.row)]
        place = fit_place(fr.depth, draws, ids, relation=_relation_name(fr)) if ids else None
        if place is None:
            say("  %-8s %3d draws: no fit" % (kind, len(ids)))
            continue
        rounded = round_place(place)
        depth, ident = raster(draws, SIZE, rounded)
        a = agreement(fr.depth, depth, ident, ids, FIXED_TOLERANCE, _relation_name(fr))
        say("  %-8s %3d draws: (%.3f, %.3f) over %d pixels; at (%g, %g): %d nearest pixels, %d hidden, %.2f%% of the %d "
            "judged within %.2e" % (kind, len(ids), place.x, place.y, place.pixels, rounded[0], rounded[1], a.pixels,
                                    a.nearer, 100.0 * a.share_judged, a.judged, FIXED_TOLERANCE))
        if kind == "still":
            found = place
    partial = any(i.geometry.reason is None and i.geometry.state == "partial" for i in items)
    if partial:
        say("NOT A RESULT: draws of this frame are partial (a capture without whole index buffers)")
    if found is None or found.pixels < JUDGED_MIN:
        say("the proving set (kind 'still') carries no fit on %d pixels or more: not decidable" % JUDGED_MIN)
        return 1
    return 1 if partial else 0


def cmd_depth_relation(args, say=print):
    fr = load_frame(cr.find_capture(args.capture), args.frame)
    _head(fr, say)
    if _undecidable(fr, say):
        return 1
    table = load_table()
    items = replay_rows(fr, fr.rows_before_copy(), table, cache=args.cache)
    drawn = [i for i in items if i.geometry.reason is None]
    proving = [i for i in items if i.judged]
    say("main-scene rows before the copy: %d; replayed %d (whole %d, partial %d); the proving set (not moving, depth write, no "
        "alpha test, every index): %d draws" % (len(items), len(drawn), sum(1 for i in drawn if i.geometry.state == "whole"),
                                               sum(1 for i in drawn if i.geometry.state == "partial"), len(proving)))
    say("  replayed, by kind: %s" % ", ".join("%d %s" % (v, k) for k, v in Counter(i.kind for i in drawn).most_common()))
    for line in reasons(items):
        say(line)
    _partial_lines(items, say)
    say("the range of the layer's depth draw: scale %g, offset %g (stored = z / w * scale + offset)" % (fr.zscale, fr.zoffset))
    best = _proving(fr, items, say)
    if best is None:
        say("criterion 5, first part: not decidable: no pixel of the proving set is nearest")
        return 1
    origin, name, rel, depth, ident = best
    say("the best: sampled at %s, stored = %s; tolerance (99th percentile of |stored - replayed|, stored units) %.3e" % (
        origin, name, rel.tolerance))
    a = rel.at_tolerance
    say("  at that tolerance: %d of %d pixels within (%.2f%%: 99%% by construction); the game has something nearer in %d, "
        "nothing as near in %d" % (a.within, a.pixels, 100.0 * a.share, a.nearer, a.farther))
    a = rel.fixed
    say("  at the fixed tolerance %.2e: %d of %d within (%.2f%% of all: the two-sided share, not the criterion); something "
        "nearer in %d (hidden by what was not replayed: inferred from the sign; not judged, ruling 24), nothing as near in "
        "%d; judged %d, within %.2f%% of them" % (FIXED_TOLERANCE, a.within, a.pixels, 100.0 * a.share, a.nearer, a.farther,
                                                  a.judged, 100.0 * a.share_judged))
    say("  by kind and vertex shader, whole draws without an alpha test (fixed tolerance; the proving set is 'still'):")
    by_vs = {}
    for i in drawn:
        if i.geometry.state == "whole" and writes_depth(i.row) and not alpha_tested(i.row):
            by_vs.setdefault((i.kind, cr.vs_hash(i.row)), []).append(i.ident)
    for (kind, h), ids in sorted(by_vs.items(), key=lambda kv: (kv[0][0] != "still", -len(kv[1]))):
        a = agreement(fr.depth, depth, ident, ids, FIXED_TOLERANCE, name)
        say("    %-8s %s: %3d draws, %7d pixels, hidden %7d, judged %7d, within %6.2f%% of the judged (%6.2f%% of all), "
            "nothing as near %d" % (kind, h, len(ids), a.pixels, a.nearer, a.judged, 100.0 * a.share_judged,
                                    100.0 * a.share if a.pixels else math.nan, a.farther))
    met, text = relation_verdict(rel)
    say("criterion 5, first part: %s" % text)
    partial = any(i.geometry.reason is None and i.geometry.state == "partial" for i in items)
    if partial:
        say("NOT A RESULT: draws of this frame are partial (a capture without whole index buffers); what a partial draw "
            "would hide is not hidden")
    return 0 if met and not partial else 1


def cmd_wind(args, say=print):
    fr = load_frame(cr.find_capture(args.capture), args.frame)
    _head(fr, say)
    if _undecidable(fr, say):
        return 1
    table = load_table()
    with_wind = replay_rows(fr, fr.rows_before_copy(), table, cache=args.cache)
    at_rest = replay_rows(fr, fr.rows_before_copy(), table, at_rest=("trunk", "leaf", "plant", "moving"), cache=args.cache)
    for line in reasons(with_wind):
        say(line)
    _partial_lines(with_wind, say)
    partial = any(i.geometry.reason is None and i.geometry.state == "partial" for i in with_wind)
    if partial:
        say("NOT A RESULT: draws of this frame are partial (a capture without whole index buffers); what a partial draw "
            "would hide is not hidden")
    best = _proving(fr, with_wind, say)
    if best is None:
        say("criterion 5: not decidable: no pixel of the proving set is nearest")
        return 1
    origin, name, rel, depth, ident = best
    met, text = relation_verdict(rel)
    say("criterion 5, first part: %s" % text)
    depth_rest, ident_rest = raster(_draw_list(at_rest), SIZE, origin)
    say("shares of the judged pixels within the fixed tolerance %.2e (sampled at %s, stored = %s; judged: the game has "
        "nothing nearer, ruling 24); in brackets the two-sided share of all nearest pixels (not the criterion):" % (
            FIXED_TOLERANCE, origin, name))
    say("  %-37s %8d pixels, %7d judged  %6.2f%% [%6.2f%%]" % ("the proving set (not moving)", rel.fixed.pixels,
                                                               rel.fixed.judged, 100.0 * rel.fixed.share_judged,
                                                               100.0 * rel.fixed.share))
    shares = {}
    for label, pick in (("trunks without an alpha test (judged)", lambda i: i.kind == "trunk" and not alpha_tested(i.row)),
                        ("trunks with an alpha test (reported)", lambda i: i.kind == "trunk" and alpha_tested(i.row)),
                        ("leaves (reported)", lambda i: i.kind == "leaf"),
                        ("plants (reported)", lambda i: i.kind == "plant")):
        ids, ids_rest, dropped = common_whole(with_wind, at_rest, pick)
        if dropped:
            say("  %-37s %3d draws left out of both sides: not replayed whole with the wind and at rest alike" % (label, dropped))
        a = agreement(fr.depth, depth, ident, ids, FIXED_TOLERANCE, name)
        b = agreement(fr.depth, depth_rest, ident_rest, ids_rest, FIXED_TOLERANCE, name)
        shares[label] = (a, b)
        say("  %-37s %3d draws: with wind %7d pixels, %7d judged %6.2f%% [%6.2f%%]; at rest %3d draws %7d pixels, %7d judged "
            "%6.2f%% [%6.2f%%]" % (label, len(ids), a.pixels, a.judged, 100.0 * a.share_judged, 100.0 * a.share,
                                   len(ids_rest), b.pixels, b.judged, 100.0 * b.share_judged, 100.0 * b.share))
    a, b = shares["trunks without an alpha test (judged)"]
    second, text = wind_verdict(rel.fixed, a, b)
    say("criterion 5, second part: %s" % text)
    return 0 if second and not partial else 1


# --- pixel counts, the mask, the occluder --------------------------------------------------------------------

_albedo = {}


def _albedo_entry(ps):
    if not _albedo:
        _albedo.update(json.loads(ALBEDO.read_text(encoding="utf-8")))
    return _albedo.get("0x" + ps)


def _component(text):
    """"r1.w" or "c8.x" -> (1, 3) / (8, 0)."""
    name, comp = text.split(".")
    return int(name[1:]), "xyzw".index(comp)


def cover_inputs(fr, row, entry):
    """(interpolators needed, a function of the outputs -> uv [n, 2], the Alpha of the row's albedo) for a
    row whose pixel shader's albedo entry is `entry` (ps-albedo.json): the coordinate's interpolator and its
    scale and offset stage from the row's pixel constants."""
    if not entry or "u" not in entry or "v" not in entry:
        raise Unreadable("no albedo coordinates for pixel shader %s" % cr.ps_hash(row))
    pconst = row.get("pconst") or []
    parts = []
    for axis in ("u", "v"):
        reg, comp = _component(entry[axis]["input"])
        scale, offset = 1.0, 0.0
        for stage in entry[axis].get("stages") or []:
            (sr, sc), (orr, oc) = _component(stage["scale"]), _component(stage["offset"])
            if max(sr, orr) * 4 + 3 >= len(pconst):
                raise Unreadable("the coordinate's stage reads a pixel register the row does not hold")
            s, o = pconst[sr * 4 + sc], pconst[orr * 4 + oc]
            scale, offset = scale * s, offset * s + o
        parts.append(("o%d" % reg, comp, scale, offset))
    slot = int(entry.get("slot", 0))
    tf = (row.get("tf") or {}).get(str(slot))
    if not tf:
        raise Unreadable("no fetch constant of the albedo's slot %d" % slot)
    fc = [int(x, 16) for x in tf]
    content = (row.get("tfc") or {}).get(str(slot))
    from texture_thumb import decode_rgba
    t = fr.texture(fc[1] & 0xFFFFF000, int(content, 16) if content else None)
    w, h, rgba = decode_rgba(t.read(), fc)
    plane = np.frombuffer(bytes(rgba), dtype=np.uint8).reshape(h, w, 4)[:, :, "xyzw".index(entry.get("alpha", {}).get("component", "w"))] / 255.0
    alpha = Alpha(plane, wrap=((fc[0] >> 10) & 7) == 0)

    def uv(outputs):
        return np.stack([outputs[name][:, comp] * scale + offset for name, comp, scale, offset in parts], axis=1)

    return tuple(sorted({p[0] for p in parts})), uv, alpha


def covered(fr, row, table, size=SIZE, origin=0.0, samples=2, scale=None, cache=None, geometry_row=None):
    """(count [rows, columns] of covered samples (0, 1, 2) of the draw's nearest covered fragment, its z / w,
    the Geometry): the row's triangles (those of `geometry_row` when given: a twin with the same mesh) with
    the alpha rule of `row` on the albedo of the geometry's row. Unreadable when it cannot be made."""
    source = geometry_row or row
    entry = _albedo_entry(cr.ps_hash(source))
    names, uv_of, alpha = cover_inputs(fr, source, entry)
    g = _cached_geometry(source, fr, True, table, names, cache)
    if g.reason:
        raise Unreadable(g.reason)
    count = np.zeros((size[1], size[0]), dtype=np.int8)
    depth = np.full((size[1], size[0]), np.inf)
    clip = np.concatenate([g.clip, uv_of(g.outputs)], axis=1)
    if scale is None:
        scale = alpha_scale(row)
    for _, ys, xs, d, attrs in fragments(clip, g.tris, size, origin):
        first, second = alpha_cover(row, attrs, alpha, (xs & 1) * 2 + (ys & 1), samples=samples, scale=scale)
        n = first.astype(np.int8) + second.astype(np.int8)
        keep = (n > 0) & (d <= depth[ys, xs])
        depth[ys[keep], xs[keep]] = d[keep]
        count[ys[keep], xs[keep]] = n[keep]
    return count, depth, g


def _fade(row):
    pconst = row.get("pconst") or []
    return float(pconst[63]) if len(pconst) >= 64 else math.nan


def cmd_pixels(args, say=print):
    fr = load_frame(cr.find_capture(args.capture), args.frame)
    _head(fr, say)
    table = load_table()
    regions = []
    if args.spot:
        spots = json.loads(REGIONS.read_text(encoding="utf-8"))["spots"]
        if args.spot not in spots:
            say("no spot %r in %s" % (args.spot, REGIONS.name))
            return 2
        regions = spots[args.spot]["regions"]
    picked = []
    for r in fr.scene:
        if r["kind"] != "draw" or cr.ps_hash(r) not in FAMILY_A or not _state_word(r, "2202") & 0x10:
            continue
        late = fr.copy_line is not None and r["line"] > fr.copy_line
        if late or _fade(r) < 1.0:
            picked.append((r, late))
    say("family A draws under alpha-to-mask that fade (pixel c15.w below 1) or are late (after the depth copy): %d" % len(picked))
    place = sample_place(fr, replay_rows(fr, fr.rows_before_copy(), table, cache=args.cache) if fr.depth is not None else [], say)
    say("  a pixel's samples: both take the stored depth's sample (%g, %g); alpha at the base level, bilinear. 'seen': not behind "
        "the stored depth (%s)" % (place[0], place[1],
                                   "within %.2e" % FIXED_TOLERANCE if fr.depth is not None else "NOT AVAILABLE: " + fr.depth_note))
    total = Counter()
    not_made = Counter()
    any_partial = False
    for r, late in picked:
        try:
            count, depth, g = covered(fr, r, table, origin=place, cache=args.cache)
        except Unreadable as e:
            not_made[str(e)] += 1
            continue
        partial = g.state == "partial"
        any_partial |= partial
        one, two = count == 1, count == 2
        line = "  line %6d %s/%s offsets %02X fade %.4f %-5s %-8s one sample %7d, two %7d" % (
            r["line"], cr.vs_hash(r)[:4], cr.ps_hash(r)[:4], _state_word(r, "2202") >> 24, _fade(r), "late" if late else "early",
            "partial" if partial else "whole", int(one.sum()), int(two.sum()))
        if fr.depth is not None:
            seen = stored_depth(depth, fr.zscale, fr.zoffset) >= fr.depth - FIXED_TOLERANCE
            line += "; seen: one %7d, two %7d" % (int((one & seen).sum()), int((two & seen).sum()))
            one, two = one & seen, two & seen
        for reg in regions:
            x0, y0, x1, y1 = reg["rect"]
            box = (slice(int(y0 * SIZE[1]), int(y1 * SIZE[1])), slice(int(x0 * SIZE[0]), int(x1 * SIZE[0])))
            total[(reg["name"], "late" if late else "early", 1)] += int(one[box].sum())
            total[(reg["name"], "late" if late else "early", 2)] += int(two[box].sum())
        total[("the picture", "late" if late else "early", 1)] += int(one.sum())
        total[("the picture", "late" if late else "early", 2)] += int(two.sum())
        say(line + (" [%s]" % g.note if g.note else ""))
    for why, n in not_made.most_common():
        say("  not made: %d draws: %s" % (n, why))
    say("sums (%s; a pixel under two draws counts twice):" % ("seen" if fr.depth is not None else "not depth-tested"))
    for name in ["the picture"] + [reg["name"] for reg in regions]:
        say("  %-20s early: one sample %7d, two %7d; late: one %7d, two %7d" % (
            name, total[(name, "early", 1)], total[(name, "early", 2)], total[(name, "late", 1)], total[(name, "late", 2)]))
    if any_partial or not_made or fr.depth is None:
        say("NOT A RESULT: %s" % ("draws are partial (a capture without whole index buffers): the counts are lower bounds"
                                  if any_partial else "some draws could not be made" if not_made else
                                  "no stored depth: the counts are not depth-tested"))
        return 1
    return 0


def cmd_mask(args, say=print):
    fr = load_frame(cr.find_capture(args.capture), args.frame)
    _head(fr, say)
    table = load_table()
    rows = [r for r in fr.scene if r["kind"] in ("draw", "tess")]
    items = replay_rows(fr, rows, table, cache=args.cache)
    for line in reasons(items):
        say(line)
    _partial_lines(items, say)
    tested_depth = fr.depth is not None and fr.copy_line is not None
    before = [i for i in items if tested_depth and i.row["line"] < fr.copy_line]
    place = sample_place(fr, before, say)
    depth, ident = raster(_draw_list(items), SIZE, place)
    wind_ids = [i.ident for i in items if i.moving]
    cover = _nearest(ident, wind_ids)
    if tested_depth:
        early = [i.ident for i in items if i.moving and i.row["line"] < fr.copy_line]
        late = [i.ident for i in items if i.moving and i.row["line"] > fr.copy_line]
        mask = seen_cover(fr.depth, depth, ident, early, late, FIXED_TOLERANCE, fr.zscale, fr.zoffset)
    else:
        mask = cover
    np.save(args.out, mask)
    by_kind = Counter()
    for i in items:
        if i.moving and i.geometry.reason is None:
            by_kind[i.kind] += int((mask & (ident == i.ident)).sum())
    say("the wind draws' cover (nearest among what was replayed; triangles whole, no texture cut-out): %d of %d pixels "
        "(%.2f%%)" % (int(cover.sum()), cover.size, 100.0 * cover.mean()))
    if tested_depth:
        say("the mask: of them, where the stored depth shows nothing nearer (draws before the depth copy; the %d wind draws "
            "after it are not in the copy and are kept untested): %d pixels (%.2f%%): %s" % (
                len(late), int(mask.sum()), 100.0 * mask.mean(), ", ".join("%s %d" % kv for kv in by_kind.most_common())))
    else:
        say("the mask is that cover, NOT DEPTH-TESTED (%s): wind draws behind terrain, which is not replayed, are in it" % (
            fr.depth_note if fr.depth is None else "the depth copy's place was not found"))
    say("written: %s" % args.out)
    partial = any(i.geometry.reason is None and i.geometry.state == "partial" for i in items if i.moving)
    missing = sum(1 for i in items if i.moving and i.geometry.reason)
    if partial or missing:
        say("NOT A RESULT: %d wind draws are partial and %d were not replayed: the mask is smaller than the cover" % (
            sum(1 for i in items if i.moving and i.geometry.reason is None and i.geometry.state == "partial"), missing))
        return 1
    return 0 if tested_depth else 1


def _shot(path):
    try:
        from PIL import Image
    except ImportError:
        raise Unreadable("the motion subcommand needs Pillow to read a shot") from None
    a = np.asarray(Image.open(path).convert("RGB"))
    h, w = a.shape[:2]
    ch = min(h, int(round(w * 9 / 16)))                  # a window shot: the client area below its title bar
    return a[h - ch:h]


def cmd_motion(args, say=print):
    mask = np.load(args.mask)
    boxes = []
    for text in args.box or []:
        name, _, rect = text.partition("=")
        boxes.append((name, [float(x) for x in rect.split(",")]))
    shots = [(Path(p).name, _shot(p)) for p in args.shots]
    if len(shots) < 2:
        say("two shots or more")
        return 2
    say("D = the share of pixels whose largest channel difference is above %d levels; rows %.2f to %.2f of the picture "
        "(without the text box and the HUD's corner); mask %s: %.2f%% of its pixels" % (
            MOTION_LEVELS, BODY_ROWS[0], BODY_ROWS[1], args.mask, 100.0 * mask.mean()))
    for (na, a), (nb, b) in zip(shots, shots[1:]):
        if a.shape != b.shape:
            say("%s and %s differ in size" % (na, nb))
            return 2
        h, w = a.shape[:2]
        m = fit_mask(mask, (h, w))
        rows = slice(int(BODY_ROWS[0] * h), int(BODY_ROWS[1] * h))
        inside, outside = motion_share(a[rows], b[rows], m[rows])
        line = "%s -> %s: D inside the mask %.2f%%, outside %.2f%%" % (na, nb, 100.0 * inside, 100.0 * outside)
        for name, (x0, y0, x1, y1) in boxes:
            box = np.zeros((h, w), dtype=bool)
            box[int(y0 * h):int(y1 * h), int(x0 * w):int(x1 * w)] = True
            line += "; %s %.2f%%" % (name, 100.0 * motion_share(a, b, box)[0])
        say(line)
    return 0


def twin_of(scene, row):
    """The main-scene draw with a layer row's mesh in its place: the same index buffer, range, vertex buffer
    and c0..c3 (research C9); None when there is none or the row has no index buffer."""
    ib, vb, bank = row.get("ib") or {}, row.get("vb") or {}, row.get("bank") or []
    if not ib or len(bank) < 16:
        return None
    for r in scene:
        rib, rvb = r.get("ib") or {}, r.get("vb") or {}
        same_mesh = rib and all(rib.get(k) == ib.get(k) for k in ("phys_addr", "start", "count"))
        if r.get("kind") == "draw" and same_mesh and rvb.get("phys_addr") == vb.get("phys_addr") \
                and (r.get("bank") or [])[:16] == bank[:16]:
            return r
    return None


def cmd_occluder(args, say=print):
    fr = load_frame(cr.find_capture(args.capture), args.frame)
    _head(fr, say)
    table = load_table()
    rows = [r for r in fr.layer if r["kind"] == "draw" and cr.ps_hash(r) == TREE_OCCLUDER_PS]
    size = (SIZE[0] // 2, SIZE[1] // 2)
    say("tree occluder draws at the layer (pixel shader F1EB, pitch %d): %d; target %d x %d, one sample, sampled at %g" % (
        LAYER_PITCH, len(rows), size[0], size[1], args.origin))
    layer = None
    if fr.depth is not None:
        layer = np.clip(fr.depth[::2, ::2], 0.0, 1.0)     # the layer's depth: the scene's at every second texel
    else:
        say("  the layer's depth is not available (%s): no depth test" % fr.depth_note)
    closed = np.zeros((size[1], size[0]), dtype=bool)
    failed = partial = 0
    for r in rows:
        twin = twin_of(fr.scene, r)
        if twin is None:
            say("  line %d: no main-scene twin (the same index buffer, range, vertex buffer and c0..c3)" % r["line"])
            failed += 1
            continue
        try:
            scale = _fade(r)                              # the caster's alpha: the texture's times pixel c15.w
            if not math.isfinite(scale):
                raise Unreadable("no pixel c15 in the layer's row")
            count, depth, g = covered(fr, r, table, size=size, origin=args.origin, samples=1, scale=scale,
                                      cache=args.cache, geometry_row=twin)
        except Unreadable as e:
            say("  line %d (twin line %d): not made: %s" % (r["line"], twin["line"], e))
            failed += 1
            continue
        kept = count > 0
        line = "  line %d (twin line %d, %s/%s, %d indices, %s): alpha scale %.4f, kept by the alpha rule %d texels" % (
            r["line"], twin["line"], cr.vs_hash(twin)[:4], cr.ps_hash(twin)[:4], (twin.get("ib") or {}).get("count", 0),
            g.state, scale, int(kept.sum()))
        if layer is not None:
            kept &= stored_depth(depth, fr.zscale, fr.zoffset) >= layer      # the layer's test: greater or equal
            line += ", of them not behind the layer's depth %d" % int(kept.sum())
        partial += g.state == "partial"
        closed |= kept
        say(line + (" [%s]" % g.note if g.note else ""))
    say("texels the replay's tree occluders close: %d of %d" % (int(closed.sum()), closed.size))
    if args.out:
        np.save(args.out, closed)
        say("written: %s" % args.out)
    if failed or partial or not rows:
        say("NOT A RESULT: %s" % ("no tree occluder row at the layer" if not rows else
                                  "%d of %d occluders not made, %d partial" % (failed, len(rows), partial)))
        return 1
    return 0


# --- the selftest --------------------------------------------------------------------------------------------

def selftest(say=print):
    """The rasteriser's rules on triangles with known answers; returns the failures."""
    tri = np.array([[0, 1, 2]])
    upper = np.array([[-1.0, 1.0, 0.5, 1.0], [1.0, 1.0, 0.5, 1.0], [-1.0, -1.0, 0.5, 1.0]])
    lower = np.array([[1.0, 1.0, 0.5, 1.0], [1.0, -1.0, 0.5, 1.0], [-1.0, -1.0, 0.5, 1.0]])
    checks = []
    for origin, want in ((0.0, 36), (0.5, 28)):
        a = raster([(upper, tri, 1)], (8, 8), origin)[1]
        b = raster([(lower, tri, 2)], (8, 8), origin)[1]
        checks.append(("the upper left half sampled at %g covers %d pixels" % (origin, want), int((a == 1).sum()) == want))
        checks.append(("the two halves sampled at %g share no pixel and miss none" % origin,
                       int(((a == 1) & (b == 2)).sum()) == 0 and int(((a == 1) | (b == 2)).sum()) == 64))
    plane = upper.copy()
    plane[:, 2] = (0.2, 0.6, 0.4)
    depth, ident = raster([(plane, tri, 1)], (8, 8), 0.0)
    checks.append(("depth is the plane through the vertices", abs(depth[3, 2] - (0.2 + 0.05 * 2 + 0.025 * 3)) < 1e-12))
    depth, ident = raster([(upper, tri, 1), (upper * np.array([1.0, 1.0, 0.5, 1.0]), tri, 2)], (8, 8))
    checks.append(("the nearer draw wins", bool((ident[ident != 0] == 2).all()) and bool(np.allclose(depth[ident == 2], 0.25))))
    behind = upper.copy()
    behind[2] = (-1.0, -3.0, 0.5, -1.0)
    depth, ident = raster([(behind, tri, 1)], (8, 8))
    checks.append(("a vertex behind the eye is cut", bool((ident == 1).any()) and bool(np.isfinite(depth[ident == 1]).all())))
    checks.append(("the stored depth is reversed", stored_depth(0.25) == 0.75))
    failures = 0
    for name, ok in checks:
        say("  %s: %s" % ("ok  " if ok else "FAIL", name))
        failures += not ok
    say("selftest: %s" % ("ok" if not failures else "%d FAILED" % failures))
    return failures


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("selftest")
    for name in ("fit", "depth-relation", "wind", "pixels", "mask", "occluder"):
        p = sub.add_parser(name)
        p.add_argument("capture")
        p.add_argument("frame", type=int)
        p.add_argument("--cache", help="a folder that keeps each row's replayed vertices between runs (by capture, frame "
                       "and line; a folder filled by another version of the tool or of the table is not noticed: empty it)")
        if name == "pixels":
            p.add_argument("--spot", help="a spot of post-regions.json: counts by its regions too")
        if name == "mask":
            p.add_argument("--out", required=True)
        if name == "occluder":
            p.add_argument("--origin", type=float, default=0.0, help="the one-sample layer's sampling place (not measured)")
            p.add_argument("--out", help="the closed texels, a numpy file")
    p = sub.add_parser("motion")
    p.add_argument("mask")
    p.add_argument("shots", nargs="+")
    p.add_argument("--box", action="append", help="name=x0,y0,x1,y1 as fractions of the picture: D in that box too")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "selftest":
            return 1 if selftest() else 0
        return {"fit": cmd_fit, "depth-relation": cmd_depth_relation, "wind": cmd_wind, "pixels": cmd_pixels, "mask": cmd_mask,
                "motion": cmd_motion, "occluder": cmd_occluder}[args.cmd](args)
    except (Unreadable, OSError) as e:
        print("cannot be read: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
