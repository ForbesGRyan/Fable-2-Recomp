"""The lantern disc measure on paired scenes (local lights, plan Task 9; spec "5. Validation" 3, criterion 2).

  lantern_regions.py --native <scene.f2ns> --game <capture> [--frame 1] [--view <row.json>]
                     [--lanterns "x,y,z;x,y,z"] [--radius 2.0] [--drop 0.5] [--spot bridge] [--scale S]
  lantern_regions.py --self-test

A stored native scene before post (render/scene_dump.h, "F2NS", read by foliage_trace.read_dump) beside the
game's stored linear scene of the same frame (a read-back capture's scene colour, foliage_trace.game_scene, as
water_replay.py pairs them: the dump's head names its scene frame, the capture's frame row names its own; a pair
whose frames differ is refused). The native colour is brought to the game's scale by c20.w / exposure (said;
--scale overrides).

Per lantern: its ground point (its position with z lowered by --drop) is projected through the view-projection
c0..c3 of a terrain row of the frame (terrain_replay.project: oPos = c0..c3 . (x, y, z, 1)); the disc of --radius
world units around it is radius * (height / 2) / (w * tan(fov / 2)) pixels, w the clip w of the point and
1 / tan(fov / 2) the length of c1's xyz (the projection's y scale under an orthonormal view). The mean linear
colour over the disc in both scenes and native / game per channel are printed; non-finite pixels are left out
(counted). --view: a JSON file holding a capture row with "consts" c0..c3, or {"c0": [...], ...}; default the
tess view of the frame whose camera lies at the frame row's
"light" "eye" (the reflection views are tess rows too). --lanterns: default the distinct lanterns of the frame's rows' "lin" ->
"lset" -> "lanterns" (capture.cpp AppendLightInputs; position, colour and range printed).

Frame-wide, outside the discs: the regions of the spot (fog-regions.json and lighting-regions.json, fractions of
the 1120 x 720 frame) and the whole frame, each with every disc's pixels left out, native / game per channel and
in luminance.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import terrain_replay as tr  # noqa: E402  (project)

REPO = Path(__file__).resolve().parents[2]
REGION_FILES = [REPO / "docs" / "native-renderer" / "fog-regions.json",
                REPO / "docs" / "native-renderer" / "lighting-regions.json"]
LUMA = (0.2125, 0.7154, 0.0721)


# --- geometry ---------------------------------------------------------------------------------------------

def ground_point(pos, drop=0.5):
    return (float(pos[0]), float(pos[1]), float(pos[2]) - drop)


def disc_of(c, point, radius, width, height):
    """(px, py, r_px) of the disc of `radius` world units around `point` under c0..c3, y down; None at or
    behind the eye plane."""
    at = tr.project(c, point[0], point[1], point[2], width, height)
    if at is None:
        return None
    p = (point[0], point[1], point[2], 1.0)
    w = sum(c[3][k] * p[k] for k in range(4))
    focal = math.sqrt(sum(c[1][k] ** 2 for k in range(3)))  # 1 / tan(fov / 2)
    return at[0], at[1], radius * (height / 2.0) * focal / w


def disc_mask(shape, px, py, r):
    """The pixels whose centres lie within r of (px, py)."""
    h, w = shape
    ys, xs = np.ogrid[0:h, 0:w]
    return (xs + 0.5 - px) ** 2 + (ys + 0.5 - py) ** 2 <= r * r


def discs_union(shape, c, lanterns, radius, drop=0.5):
    out = np.zeros(shape, dtype=bool)
    for pos in lanterns:
        d = disc_of(c, ground_point(pos, drop), radius, shape[1], shape[0])
        if d is not None:
            out |= disc_mask(shape, *d)
    return out


# --- the measure ------------------------------------------------------------------------------------------

def _means(native, game, sel):
    good = sel & np.all(np.isfinite(native), -1) & np.all(np.isfinite(game), -1)
    n = int(good.sum())
    row = {"pixels": n, "bad": int(sel.sum()) - n, "native": None, "game": None, "ratio": None, "luma": None}
    if n:
        a = native[good].astype(np.float64).mean(0)
        g = game[good].astype(np.float64).mean(0)
        row["native"], row["game"] = tuple(a), tuple(g)
        row["ratio"] = tuple(float(a[k] / g[k]) if g[k] > 0 else math.nan for k in range(3))
        la, lg = float(np.dot(a, LUMA)), float(np.dot(g, LUMA))
        row["luma"] = la / lg if lg > 0 else math.nan
    return row


def measure(native, game, c, lanterns, radius, scale=1.0, drop=0.5):
    """[{pos, ground, disc (px, py, r) or None, pixels, bad, native, game, ratio, luma}] a lantern."""
    native = np.asarray(native, dtype=np.float64) * scale
    game = np.asarray(game, dtype=np.float64)
    h, w = game.shape[:2]
    out = []
    for pos in lanterns:
        gp = ground_point(pos, drop)
        d = disc_of(c, gp, radius, w, h)
        sel = disc_mask((h, w), *d) if d is not None else np.zeros((h, w), dtype=bool)
        row = _means(native, game, sel)
        row.update(pos=tuple(pos), ground=gp, disc=d)
        out.append(row)
    return out


def regions_outside(native, game, regions, union, scale=1.0):
    """{name: means} over each pixel box (x0, y0, x1, y1) less the union of the discs."""
    native = np.asarray(native, dtype=np.float64) * scale
    game = np.asarray(game, dtype=np.float64)
    out = {}
    for name, (x0, y0, x1, y1) in regions.items():
        sel = np.zeros(game.shape[:2], dtype=bool)
        sel[y0:y1, x0:x1] = True
        out[name] = _means(native, game, sel & ~union)
    return out


# --- inputs -----------------------------------------------------------------------------------------------

def parse_lanterns(text):
    out = []
    for part in text.split(";"):
        v = [float(x) for x in part.split(",") if x.strip()]
        if len(v) != 3:
            raise ValueError("a lantern is x,y,z: %r" % part)
        out.append(tuple(v))
    return out


def lanterns_of_rows(rows):
    """The distinct lanterns of the rows' "lin" -> "lset" -> "lanterns", in first-seen order:
    [{pos, col, range, rows}] (rows: how many rows name it; distinct by position to 1e-3)."""
    seen = {}
    for r in rows:
        ls = (r.get("lin") or {}).get("lset") or {}
        for v in ls.get("lanterns") or []:
            key = tuple(round(float(x), 3) for x in v[0:3])
            if key not in seen:
                seen[key] = {"pos": tuple(float(x) for x in v[0:3]), "col": tuple(float(x) for x in v[4:7]),
                             "range": float(v[8]), "rows": 0}
            seen[key]["rows"] += 1
    return list(seen.values())


def camera_of(c):
    """The eye of c0..c3: the point where clip x, y and w are zero."""
    a = np.array([c[0][:3], c[1][:3], c[3][:3]], dtype=np.float64)
    b = -np.array([c[0][3], c[1][3], c[3][3]], dtype=np.float64)
    return np.linalg.solve(a, b)


def view_of_rows(rows, eye=None):
    """c0..c3 of the frame's main view from its tess rows, or None. A frame's tess rows hold more than one view
    (the water's reflection views are tess rows too): with `eye` (the frame row's "light" "eye") the view whose
    camera lies nearest it, otherwise the view of the most rows."""
    views = {}
    for r in rows:
        k = r.get("consts") or {}
        if r.get("kind") == "tess" and all(n in k for n in ("c0", "c1", "c2", "c3")):
            c = [list(map(float, k[n])) for n in ("c0", "c1", "c2", "c3")]
            key = tuple(tuple(v) for v in c)
            views.setdefault(key, [c, 0])[1] += 1
    if not views:
        return None
    if eye is not None:
        e = np.asarray(eye, dtype=np.float64)
        return min(views.values(), key=lambda v: float(np.linalg.norm(camera_of(v[0]) - e)))[0]
    return max(views.values(), key=lambda v: v[1])[0]


def view_of_file(path):
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    k = doc.get("consts", doc)
    return [list(map(float, k[n])) for n in ("c0", "c1", "c2", "c3")]


def spot_boxes(spot, width, height, files=REGION_FILES):
    out = {"whole frame": (0, 0, width, height)}
    for f in files:
        doc = json.loads(Path(f).read_text(encoding="utf-8"))
        for r in doc.get("spots", {}).get(spot, {}).get("regions", []):
            x0, y0, x1, y1 = r["rect"]
            box = (int(x0 * width + 0.5), int(y0 * height + 0.5), int(x1 * width + 0.5), int(y1 * height + 0.5))
            out["%s (%s)" % (r["name"], Path(f).stem.split("-")[0])] = (box[0], box[1], max(box[2], box[0] + 1),
                                                                        max(box[3], box[1] + 1))
    return out


# --- output -----------------------------------------------------------------------------------------------

def _rgb(v, spec="%.5f"):
    return "-" if v is None else " ".join(spec % x for x in v)


def report(rows, say=print):
    for i, r in enumerate(rows, 1):
        d = r["disc"]
        say("lantern %d at (%.2f, %.2f, %.2f), ground (%.2f, %.2f, %.2f): %s" % (
            i, *r["pos"], *r["ground"],
            "behind the eye" if d is None else "disc at pixel (%.1f, %.1f), radius %.1f px, %d pixels (%d not finite)" % (
                d[0], d[1], d[2], r["pixels"], r["bad"])))
        if r["ratio"] is not None:
            say("  native %s | game %s | native / game %s | luminance %.4f" % (
                _rgb(r["native"]), _rgb(r["game"]), _rgb(r["ratio"], "%.4f"), r["luma"]))


def report_regions(rows, say=print):
    say("frame-wide, outside the discs (native / game: red green blue | luminance | pixels):")
    for name, r in rows.items():
        if r["ratio"] is None:
            say("  %-40s no pixels" % name)
        else:
            say("  %-40s %s | %.4f | %d" % (name, _rgb(r["ratio"], "%.4f"), r["luma"], r["pixels"]))


# --- self-test --------------------------------------------------------------------------------------------

def self_test(say=print):
    W, H = 1120, 720
    c = [[H / W, 0, 0, 0], [0, 1.0, 0, 0], [0, 0, -1.0, -0.1], [0, 0, -1.0, 0]]
    lantern = (0.0, 0.0, -10.0)
    game = np.full((H, W, 3), 0.1)
    d = disc_of(c, ground_point(lantern), 2.0, W, H)
    m = disc_mask((H, W), *d)
    game[m] = (1.0, 0.5, 0.2)
    ok = True
    for gain, want in ((1.0, 1.0), (0.5, 0.5)):
        native = game.copy()
        native[m] *= gain
        (r,) = measure(native, game, c, [lantern], 2.0)
        good = all(abs(x - want) < 1e-9 for x in r["ratio"])
        say("self-test: native disc x%.1f -> ratio %s (want %.1f): %s" % (gain, _rgb(r["ratio"], "%.4f"), want,
                                                                         "ok" if good else "WRONG"))
        ok &= good
    say("self-test: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


# --- main -------------------------------------------------------------------------------------------------

def main(argv=None, say=print):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--native")
    ap.add_argument("--game", help="the read-back capture (path or end of its name)")
    ap.add_argument("--frame", type=int, default=1)
    ap.add_argument("--view")
    ap.add_argument("--lanterns")
    ap.add_argument("--radius", type=float, default=2.0)
    ap.add_argument("--drop", type=float, default=0.5)
    ap.add_argument("--spot", default="bridge")
    ap.add_argument("--scale", type=float)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test(say)
    if not a.native or not a.game:
        ap.error("--native and --game are needed (or --self-test)")
    import capture_replay as cr
    import foliage_trace as ft
    path = cr.find_capture(a.game)
    head, nscene, _, _ = ft.read_dump(a.native)
    want = ft.scene_frame_of(path, a.frame)
    say("native %s: scene frame %s, %d x %d, exposure %g; game %s frame %d: scene frame %s" % (
        Path(a.native).name, head["scene_frame"], head["width"], head["height"], head["exposure"], path.name, a.frame, want))
    if want is not None and int(head["scene_frame"]) != int(want):
        say("NOT COMPARED: the stored native scene is not the capture's frame %d" % a.frame)
        return 1
    game, c20w = ft.game_scene(path, a.frame)
    scale = a.scale if a.scale is not None else (c20w / head["exposure"] if head["exposure"] else 1.0)
    say("the native colour times %.6g (%s)" % (scale, "--scale" if a.scale is not None else "c20.w %g / exposure" % c20w))
    H, W = game.shape[:2]
    nscene = np.asarray(nscene)[..., :3]
    if nscene.shape[:2] != (H, W):
        yi, xi = np.arange(H) * nscene.shape[0] // H, np.arange(W) * nscene.shape[1] // W
        nscene = nscene[yi][:, xi]
        say("the native scene resampled to %d x %d (nearest)" % (W, H))
    rows = cr.rows(path, ("draw", "tess"), frame=a.frame)
    eye = None
    for fr in cr.rows(path, ("frame",), frame=a.frame):
        eye = (fr.get("light") or {}).get("eye") or eye
    c = view_of_file(a.view) if a.view else view_of_rows(rows, eye)
    if c is None:
        say("NOT COMPARED: no tess row of frame %d has c0..c3 (give --view)" % a.frame)
        return 1
    say("view-projection c0..c3: %s; its camera (%.3f, %.3f, %.3f), the frame's eye %s" % (
        "; ".join(" ".join("%.9g" % x for x in r) for r in c), *camera_of(c),
        "(%.3f, %.3f, %.3f)" % tuple(eye) if eye else "not in the frame row"))
    found = lanterns_of_rows(rows)
    for i, l in enumerate(found, 1):
        say("the frame's lantern %d (lset): position (%.3f, %.3f, %.3f), colour (%.4f, %.4f, %.4f), range %.4g, in %d rows" % (
            i, *l["pos"], *l["col"], l["range"], l["rows"]))
    lanterns = parse_lanterns(a.lanterns) if a.lanterns else [l["pos"] for l in found]
    if not lanterns:
        say("NOT COMPARED: no lantern given and none in the frame's rows")
        return 1
    rows_m = measure(nscene, game, c, lanterns, a.radius, scale, a.drop)
    say("radius %.3g units, ground point z - %.3g" % (a.radius, a.drop))
    report(rows_m, say)
    union = discs_union((H, W), c, lanterns, a.radius, a.drop)
    report_regions(regions_outside(nscene, game, spot_boxes(a.spot, W, H), union, scale), say)
    return 0


if __name__ == "__main__":
    sys.exit(main())
