"""The darkness trace and the redraw's kept set (foliage sub-project, plan Tasks 16 and 17; spec Design 13,
criteria 15 to 17, ruling 18): a stored native scene before post (render/scene_dump.h, "F2NS") beside the game's
scene of the same frame (a read-back capture's scene colour, as post_replay.py reads it), by pixel class.

  foliage_trace.py head      <dump>
  foliage_trace.py holes     <dump> [...]
  foliage_trace.py classes   <capture> <frame> --out <npy> [--cache DIR]
  foliage_trace.py trace     <dump> <capture> <frame> [--spot sunward] [--classes <npy>] [--cache DIR] [--scale S]
  foliage_trace.py halfpixel <dump> <capture> <frame> [--classes <npy>] [--cache DIR]
  foliage_trace.py occluder  <dump> <capture> <frame> [--levels 0,1,2] [--origin 0.0] [--cache DIR]
  foliage_trace.py predict   <inputs.json>

<capture> is a path or the end of a capture's name in the logs folder. `classes`, `trace` and `halfpixel` need a
capture made with --readback_resolve=full: the game's scene before post and its depth copy exist nowhere else.
`occluder` needs only the capture's rows and index files (no read-back) and a dump with the layer's two planes.

The pairing. A dump's head names the scene frame it holds ("scene_frame") and the one asked for; a capture's
frame row names its "scene_frame". `trace` prints both and says when they differ: a native scene one frame late
is still the same view when the camera stands, and NOT when it moves. Units: the native linear target holds a
lit colour times the frame's exposure (the game's c20.w of that frame in a post frame), the game's scene colour
holds the game's own scale: the same frame gives the same units. `trace` prints the dump's exposure and the
capture's c20.w and, when they differ, multiplies the native scene by c20.w / exposure (said; --scale overrides).

The classes (`classify`), one a pixel, from the capture alone:
  0 open sky              the game's depth copy holds nothing (stored 0) and no late tree covers the pixel
  1 late tree over sky    a late tree draw (family A under offsets AA, issued after the depth copy) covers it, sky behind
  2 leaf                  a replayed tree-leaf fragment lies at the stored depth
  3 trunk and wood        a replayed trunk fragment, or one of a draw that does not sway (props, rails, the hero)
  4 ground                the stored depth is explained by no replayed fragment: terrain and immediate rows, which
                          the replay does not draw
  5 other                 no replayed fragment AT the stored depth but one close to it (a miss by precision or by
                          an edge): not assigned to a class
  6 late tree over closed a late tree draw over something the depth copy holds
  7 plant                 a replayed instanced-plant fragment (bushes, grass, ferns)
Classes 6 and 7 are this tool's additions to the plan's six: a late tree over ground and a bush are neither
"leaf" nor "other". A fragment is "at" the stored depth within MATCH_STEPS steps of 2^-24 and "close" within
NEAR_STEPS, at the depth copy's sampling place (foliage_replay.SAMPLE_ORIGIN); no alpha is evaluated (a card's
fragment the alpha test dropped does not lie at the stored depth unless the surface behind it happens to).
Limits: what the game drew AFTER its depth copy other than the late trees (particles, glows) keeps the class of
what the copy holds; the classes are the game's cover, so a native pixel is judged by what the GAME shows there.

The trace (`trace_arrays`). Per region and class k over the pixels where both pictures are numbers: pixels, the
game's and the native mean linear luminance (0.2125 r + 0.7154 g + 0.0721 b), their ratio r_k, the class's share
s_k of the game's luminance in the region, and its part of the region's ratio, delta_k = s_k (r_k - 1): the
parts sum to the region's ratio less 1, exactly. `verdict` (ruling 18): "assigned" when what class 5 holds
moves the region's ratio by at most 0.05 in logarithm (|ln R - ln(R - delta_5)| <= 0.05) and fewer than 1% of
the region's pixels were counted out; else "not assigned". `outside_band`: the classes outside 0.85 to 1.18,
each of which the report names with its cause or as open.

`occluder` (criterion 15): the native redraw's kept set, the texels the layer's second plane holds nearer than
its first, against the replay's (foliage_replay.occluder_replay: the twin's triangles with the wind, the alpha
rule on one sample, tested against the NATIVE layer's first plane so that only the redraw is compared), at each
texture level asked for. Met: the native set holds at least 90% of the replay's and the texels the native set
alone holds are at most 15% of the replay's count.

`predict` (criterion 17): per region, the figure of sub-project 10's run T16-5 moved by the measured change of
the linear scene (coverage) and of the layer (the redraw), through the screen formula (`predict_region`).

Needs numpy. Exit code: 0; 1 when a verdict is "not assigned" / "not met" / not decidable or holes are above
the bound; 2 when a file cannot be read.
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture_replay as cr  # noqa: E402
import foliage_replay as fp  # noqa: E402

LUMA = (0.2125, 0.7154, 0.0721)
BAND = (0.85, 1.18)                  # ruling 18: a class outside it is named with its cause or as open
ASSIGNED_LOG = 0.05                  # ruling 18
BAD_SHARE = 0.01
SKY, LATE_SKY, LEAF, WOOD, GROUND, OTHER, LATE_OTHER, PLANT = range(8)
CLASS_NAMES = ("open sky", "late tree over sky", "leaf", "trunk and wood", "ground", "other", "late tree over closed", "plant")
MATCH_STEPS, NEAR_STEPS = 256, 4096  # of 2^-24: "at" and "close to" the stored depth
FAR = 16777215
CLEAR = (101.0 / 16384.0, 101.0 / 16384.0, 39.0 / 4096.0)     # render::kClayClearLinear, exact in the float target
HOLES_BOUND = 1e-4
KEPT_CLOSED_MIN, KEPT_ALONE_MAX = 0.90, 0.15                  # criterion 15
DISPLAY_POWER = 2.2                  # the regions' measure: linear = display ** 2.2 (run_regions.py)
SHIFTS = [i / 4.0 for i in range(-4, 5)]
REGIONS = fp.REGIONS

Unreadable = cr.Unreadable


# --- the stored native scene ---------------------------------------------------------------------------------

def read_dump(path):
    """(head, scene [h, w, 3] float32, depth [h, w] uint32 levels, layers): a stored native scene. layers: None, or
    the sun layer's depth before and after the tree occluders, two planes [h / 2, w / 2] of levels (0 the near
    plane, 16777215 the far plane and the cleared depth). ValueError for anything that is not a whole file."""
    data = Path(path).read_bytes()
    if len(data) < 48:
        raise ValueError("%s: not a stored native scene" % path)
    magic, version, w, h, scene_frame, asked, flags, exposure, bad, reserved = struct.unpack_from("<4sIIIQQIfII", data, 0)
    if magic != b"F2NS" or version != 1 or reserved != 0 or flags & ~7 or not (0 < w <= 8192 and 0 < h <= 8192):
        raise ValueError("%s: not a stored native scene" % path)
    n = w * h
    layer = bool(flags & 2)
    want = 48 + n * 16 + (2 * (w // 2) * (h // 2) * 4 if layer else 0)
    if len(data) != want or (layer and (w | h) & 1):
        raise ValueError("%s: %d bytes, the head asks for %d" % (path, len(data), want))
    scene = np.frombuffer(data, dtype="<f4", count=n * 3, offset=48).reshape(h, w, 3)
    depth = np.frombuffer(data, dtype="<u4", count=n, offset=48 + n * 12).reshape(h, w)
    layers = None
    if layer:
        m = (w // 2) * (h // 2)
        layers = [np.frombuffer(data, dtype="<u4", count=m, offset=48 + n * 16 + i * m * 4).reshape(h // 2, w // 2) for i in range(2)]
    head = {"width": w, "height": h, "scene_frame": scene_frame, "asked_frame": asked, "flags": flags, "exposure": exposure,
            "bad_values": bad}
    return head, scene, depth, layers


def holes_arrays(scene, depth, clear=CLEAR):
    """The pixels with the clear colour under a written depth (criterion 7's first reading)."""
    clear = np.asarray(clear, dtype=np.asarray(scene).dtype)
    return int((np.all(np.asarray(scene) == clear, axis=-1) & (np.asarray(depth) < FAR)).sum())


def holes(path):
    head, scene, depth, _ = read_dump(path)
    return holes_arrays(scene, depth, np.array(CLEAR, dtype=np.float32)), scene.shape[0] * scene.shape[1]


# --- the trace -----------------------------------------------------------------------------------------------

def luminance(picture):
    return (np.asarray(picture, dtype=np.float64) * LUMA).sum(-1)


def trace_arrays(game, native, cls, regions):
    """{region name: rows}; rows: {class id: {"pixels", "game", "native", "ratio", "share", "delta"}} for the
    classes with a pixel in the region, and "region": {"pixels", "bad", "game", "native", "ratio"}. game, native
    [h, w, 3] linear; cls [h, w] class ids; regions: name -> (x0, y0, x1, y1) in pixels. A pixel where either
    picture is no number is counted out ("bad")."""
    g_all, n_all = luminance(game), luminance(native)
    out = {}
    for name, (x0, y0, x1, y1) in regions.items():
        g, n, c = g_all[y0:y1, x0:x1], n_all[y0:y1, x0:x1], np.asarray(cls)[y0:y1, x0:x1]
        good = np.isfinite(g) & np.isfinite(n)
        total_g, total_n = float(g[good].sum()), float(n[good].sum())
        rows = {"region": {"pixels": int(good.sum()), "bad": int((~good).sum()),
                           "game": total_g / max(int(good.sum()), 1), "native": total_n / max(int(good.sum()), 1),
                           "ratio": total_n / total_g if total_g > 0 else math.nan}}
        for k in np.unique(c[good]):
            at = good & (c == k)
            sg, sn = float(g[at].sum()), float(n[at].sum())
            ratio = sn / sg if sg > 0 else math.nan
            share = sg / total_g if total_g > 0 else math.nan
            rows[int(k)] = {"pixels": int(at.sum()), "game": sg / int(at.sum()), "native": sn / int(at.sum()), "ratio": ratio,
                            "share": share, "delta": (sn - sg) / total_g if total_g > 0 else math.nan}
        out[name] = rows
    return out


def verdict(rows):
    """"assigned" or "not assigned" (ruling 18; the module text)."""
    region = rows["region"]
    ratio, pixels, bad = region.get("ratio", math.nan), region.get("pixels", 0), region.get("bad", 0)
    if not (ratio > 0 and math.isfinite(ratio)) or pixels + bad == 0 or bad >= BAD_SHARE * (pixels + bad):
        return "not assigned"
    rest = ratio - rows.get(OTHER, {}).get("delta", 0.0)
    if not (rest > 0 and math.isfinite(rest)):
        return "not assigned"
    return "assigned" if abs(math.log(ratio) - math.log(rest)) <= ASSIGNED_LOG else "not assigned"


def outside_band(rows):
    """The classes of a region whose ratio lies outside BAND (or is no number), in order."""
    return [k for k in sorted(k for k in rows if k != "region") if not (BAND[0] <= rows[k]["ratio"] <= BAND[1])]


# --- the classes ---------------------------------------------------------------------------------------------

def classify(stored, matches, late, near=None):
    """Class ids [h, w] (the module text). stored: the game's depth copy (0: nothing drawn before it); matches:
    {LEAF / PLANT / WOOD: bool [h, w]}: a replayed fragment of that kind lies at the stored depth; late: a late
    tree draw covers the pixel; near: a replayed fragment lies close to the stored depth."""
    stored = np.asarray(stored)
    drawn = stored > 0
    cls = np.full(stored.shape, GROUND, dtype=np.uint8)
    if near is not None:
        cls[np.asarray(near, dtype=bool)] = OTHER
    for k in (WOOD, PLANT, LEAF):                         # the last written wins: leaf before plant before wood
        if k in matches:
            cls[np.asarray(matches[k], dtype=bool)] = k
    cls[~drawn] = SKY
    late = np.asarray(late, dtype=bool)
    cls[late & drawn] = LATE_OTHER
    cls[late & ~drawn] = LATE_SKY
    return cls


def depth_matches(stored, draws, size, origin, tolerance, near, zscale=-1.0, zoffset=1.0):
    """(match, close), bool [rows, columns]: the pixels where a fragment of `draws` ((clip, tris) pairs) lies
    within `tolerance` of the stored depth, and those where one lies within `near` but none within `tolerance`."""
    stored = np.asarray(stored, dtype=np.float64)
    match = np.zeros(stored.shape, dtype=bool)
    close = np.zeros(stored.shape, dtype=bool)
    for clip, tris in draws:
        for _, ys, xs, d, _ in fp.fragments(clip, tris, size, origin):
            miss = np.abs(fp.stored_depth(d, zscale, zoffset) - stored[ys, xs])
            hit = miss <= tolerance
            match[ys[hit], xs[hit]] = True
            almost = miss <= near
            close[ys[almost], xs[almost]] = True
    return match, close & ~match


def _is_late_tree(row, copy_line):
    word = fp._state_word(row, "2202")
    return (row.get("kind") == "draw" and copy_line is not None and row["line"] > copy_line and cr.ps_hash(row) in fp.FAMILY_A
            and word & 0x10 and (word >> 24) == 0xAA)


def classes(capture, frame, cache=None, say=print):
    """(class ids [720, 1120], the foliage_replay Frame) of a read-back capture's frame."""
    fr = fp.load_frame(cr.find_capture(capture), frame)
    if fr.depth is None or fr.copy_line is None:
        raise Unreadable("no stored depth to class by (%s); the capture must be made with --readback_resolve=full" % fr.depth_note)
    if not np.any(fr.depth > 0):
        raise Unreadable("the stored depth is empty (a capture without read-back)")
    table = fp.load_table()
    origin = (fp.SAMPLE_ORIGIN, fp.SAMPLE_ORIGIN)
    kinds = {LEAF: [], PLANT: [], WOOD: []}
    left = 0
    for row in fr.before_copy:
        if row["kind"] != "draw":
            continue
        g = fp._cached_geometry(row, fr, True, table, (), cache)
        if g.reason:
            left += 1
            continue
        kind = fp.kind_of(row, table)
        kinds[LEAF if kind == "leaf" else PLANT if kind == "plant" else WOOD].append((g.clip, g.tris))
    matches, near = {}, np.zeros(fr.depth.shape, dtype=bool)
    step = fp.DEPTH_STEP
    for k, draws in kinds.items():
        matches[k], close = depth_matches(fr.depth, draws, fp.SIZE, origin, MATCH_STEPS * step, NEAR_STEPS * step, fr.zscale, fr.zoffset)
        near |= close
    near &= ~(matches[LEAF] | matches[PLANT] | matches[WOOD])
    late = np.zeros(fr.depth.shape, dtype=bool)
    late_rows = [r for r in fr.scene if _is_late_tree(r, fr.copy_line)]
    for row in late_rows:
        try:
            count, depth, _ = fp.covered(fr, row, table, origin=origin, cache=cache)
        except Unreadable as e:
            say("  late tree row line %d not made: %s" % (row["line"], e))
            continue
        with np.errstate(invalid="ignore"):
            late |= (count > 0) & (fp.stored_depth(depth, fr.zscale, fr.zoffset) >= fr.depth)
    say("classes: %d leaf, %d plant, %d other draws replayed before the depth copy (%d not replayable), %d late tree rows" % (
        len(kinds[LEAF]), len(kinds[PLANT]), len(kinds[WOOD]), left, len(late_rows)))
    return classify(fr.depth, matches, late, near), fr


def game_scene(capture, frame):
    """(scene [720, 1120, 3] in the game's scale, c20.w) of a read-back capture's frame (post_replay's reader)."""
    import post_replay as pr
    try:
        f = pr.load_frame(cr.find_capture(capture), frame)
        scene = pr.load_scene(f)
    except pr.ReplayError as e:
        raise Unreadable(str(e))
    return np.array(scene, dtype=np.float64).reshape(pr.SCENE_H, pr.SCENE_W, 3), f.c20w


def scene_frame_of(capture, frame):
    """The "scene_frame" of a capture's frame row; None when the row has none."""
    for r in cr.rows(cr.find_capture(capture), ("frame",), frame=frame):
        if r.get("scene_frame") is not None:
            return int(r["scene_frame"])
    return None


def spot_regions(spot, width, height, path=REGIONS):
    """name -> (x0, y0, x1, y1) in pixels for a spot of post-regions.json."""
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    regions = doc.get("spots", {}).get(spot, {}).get("regions")
    if not regions:
        raise Unreadable("no spot %r in %s" % (spot, Path(path).name))
    out = {}
    for r in regions:
        x0, y0, x1, y1 = r["rect"]
        out[r["name"]] = (int(x0 * width + 0.5), int(y0 * height + 0.5), max(int(x1 * width + 0.5), int(x0 * width + 0.5) + 1),
                          max(int(y1 * height + 0.5), int(y0 * height + 0.5) + 1))
    return out


# --- the half pixel ------------------------------------------------------------------------------------------

def _shifted(picture, dx, dy):
    """picture sampled at (x + dx, y + dy), bilinear, the edge repeated."""
    h, w = picture.shape
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    x, y = np.clip(x + dx, 0, w - 1), np.clip(y + dy, 0, h - 1)
    x0, y0 = np.minimum(np.floor(x).astype(int), w - 2), np.minimum(np.floor(y).astype(int), h - 2)
    fx, fy = x - x0, y - y0
    return (picture[y0, x0] * (1 - fx) * (1 - fy) + picture[y0, x0 + 1] * fx * (1 - fy) +
            picture[y0 + 1, x0] * (1 - fx) * fy + picture[y0 + 1, x0 + 1] * fx * fy)


def halfpixel_arrays(game, native, mask, shifts=SHIFTS):
    """(dx, dy) in pixels, of `shifts`, at which the native picture is best the game's sampled at (x + dx, y + dy):
    the largest correlation of the two pictures' logarithms over the masked pixels (two pixels from the edge
    left out). None when fewer than 16 pixels carry it. Pictures [h, w] of luminance."""
    game, native = np.asarray(game, dtype=np.float64), np.asarray(native, dtype=np.float64)
    inner = np.zeros(game.shape, dtype=bool)
    inner[2:-2, 2:-2] = True
    at = np.asarray(mask, dtype=bool) & inner & np.isfinite(game) & np.isfinite(native) & (native > 0)
    if at.sum() < 16:
        return None
    b = np.log(native[at])
    b = b - b.mean()
    best = None
    for dy in shifts:
        for dx in shifts:
            a = np.log(np.maximum(_shifted(game, dx, dy)[at], 1e-12))
            a = a - a.mean()
            score = float((a * b).sum() / max(math.sqrt(float((a * a).sum() * (b * b).sum())), 1e-300))
            if best is None or score > best[0]:
                best = (score, dx, dy)
    return best[1], best[2], best[0]


# --- the redraw's kept set (criterion 15) --------------------------------------------------------------------

def kept_set(before, after, replay_closed):
    """The native redraw's kept set (the texels of the layer's second plane nearer than its first) against the
    replay's: counts and the criterion's two shares. `farther`: texels the second plane holds FARTHER (none can
    be: the redraw only tests and writes nearer or equal)."""
    before, after = np.asarray(before, dtype=np.int64), np.asarray(after, dtype=np.int64)
    native = after < before
    replay = np.asarray(replay_closed, dtype=bool)
    k = {"replay": int(replay.sum()), "native": int(native.sum()), "both": int((native & replay).sum()),
         "native_alone": int((native & ~replay).sum()), "replay_alone": int((replay & ~native).sum()),
         "farther": int((after > before).sum())}
    k["closed_share"] = k["both"] / k["replay"] if k["replay"] else math.nan
    k["alone_share"] = k["native_alone"] / k["replay"] if k["replay"] else math.nan
    return k


def kept_verdict(k):
    if not k["replay"]:
        return "not decidable"
    return "met" if k["closed_share"] >= KEPT_CLOSED_MIN and k["alone_share"] <= KEPT_ALONE_MAX else "not met"


# --- criterion 17's model ------------------------------------------------------------------------------------

def predict_region(measured, d0, layer, scene_factor, layer_factor):
    """A region's predicted native-over-game figure. measured: the figure of the run the model starts from (sun
    on); d0: the region's native display value WITHOUT the layer in that run (0..1); layer: the layer's screen
    factor B there (d = d0 + B (1 - d0)); scene_factor: the measured change of the region's linear scene (the
    coverage: on over off); layer_factor: what is left of B (the redraw: the open share of the beam source).
    The display value's linear measure is display ** DISPLAY_POWER on both sides."""
    for name, v, lo, hi in (("measured", measured, 0.0, math.inf), ("d0", d0, 0.0, 1.0), ("layer", layer, 0.0, 1.0),
                            ("scene_factor", scene_factor, 0.0, math.inf), ("layer_factor", layer_factor, 0.0, math.inf)):
        if not (lo <= v <= hi) or not math.isfinite(v):
            raise ValueError("%s = %r" % (name, v))
    was = d0 + layer * (1.0 - d0)
    new0 = min(1.0, (scene_factor * d0 ** DISPLAY_POWER) ** (1.0 / DISPLAY_POWER))
    now = new0 + min(1.0, layer * layer_factor) * (1.0 - new0)
    if was <= 0.0:
        raise ValueError("a region without light")
    return measured * (now / was) ** DISPLAY_POWER


# --- commands ------------------------------------------------------------------------------------------------

def _load_classes(args, say):
    if getattr(args, "classes", None) and Path(args.classes).is_file():
        say("classes: read from %s" % args.classes)
        return np.load(args.classes)
    cls, _ = classes(args.capture, args.frame, cache=args.cache, say=say)
    if getattr(args, "classes", None):
        np.save(args.classes, cls)
        say("classes: written to %s" % args.classes)
    return cls


def _pair(args, say):
    head, native, depth, layers = read_dump(args.dump)
    game, c20w = game_scene(args.capture, args.frame)
    if native.shape != game.shape:
        raise Unreadable("the dump is %d x %d, the game's scene %d x %d" % (native.shape[1], native.shape[0], game.shape[1], game.shape[0]))
    frame = scene_frame_of(args.capture, args.frame)
    say("dump %s: scene frame %d (asked %d), flags %d, exposure %.5f, bad values %d" % (
        Path(args.dump).name, head["scene_frame"], head["asked_frame"], head["flags"], head["exposure"], head["bad_values"]))
    say("capture %s frame %d: scene frame %s, c20.w %.5f%s" % (
        Path(cr.find_capture(args.capture)).name, args.frame, frame, c20w,
        "" if frame == head["scene_frame"] else "  ** NOT the dump's scene frame: the pair is of two frames **"))
    scale = getattr(args, "scale", None)
    if scale is None:
        scale = c20w / head["exposure"] if head["exposure"] > 0 and abs(c20w / head["exposure"] - 1.0) > 1e-4 else 1.0
    if scale != 1.0:
        say("the native scene is multiplied by %.5f (the capture's c20.w over the dump's exposure, or --scale)" % scale)
    return head, native.astype(np.float64) * scale, depth, game


def cmd_head(args, say=print):
    head, scene, depth, layers = read_dump(args.dump)
    say("%s: %d x %d, scene frame %d (asked %d), flags %d, exposure %.5f, bad values %d; mean linear luminance %.5f; depth written on "
        "%.2f%% of the picture; layer planes %s" % (
            Path(args.dump).name, head["width"], head["height"], head["scene_frame"], head["asked_frame"], head["flags"], head["exposure"],
            head["bad_values"], float(luminance(scene).mean()), 100.0 * float((depth < FAR).mean()),
            "none" if layers is None else "2 (the redraw closed %d texels)" % int((layers[1] < layers[0]).sum())))
    return 0


def cmd_holes(args, say=print):
    worst = 0.0
    for path in args.dump:
        n, pixels = holes(path)
        worst = max(worst, n / pixels)
        say("%s: holes %d of %d pixels = %.4f%% (bound 0.01%%: %s)" % (Path(path).name, n, pixels, 100.0 * n / pixels,
                                                                     "within" if n / pixels <= HOLES_BOUND else "ABOVE"))
    return 0 if worst <= HOLES_BOUND else 1


def cmd_classes(args, say=print):
    cls, fr = classes(args.capture, args.frame, cache=args.cache, say=say)
    np.save(args.out, cls)
    for k, name in enumerate(CLASS_NAMES):
        say("  %d %-22s %7d pixels  %6.2f%%" % (k, name, int((cls == k).sum()), 100.0 * float((cls == k).mean())))
    say("written: %s" % args.out)
    return 0


def report(traced, say=print):
    """The table of a trace; returns whether every region is assigned."""
    every = True
    for name, rows in traced.items():
        region = rows["region"]
        v = verdict(rows)
        every = every and v == "assigned"
        say("%s: %d pixels (%d counted out), game %.5f, native %.5f, native over game %.4f: %s" % (
            name, region["pixels"], region["bad"], region["game"], region["native"], region["ratio"], v))
        say("    %-24s %8s %7s %9s %9s %7s %8s" % ("class", "pixels", "share", "game", "native", "ratio", "part"))
        for k in sorted(k for k in rows if k != "region"):
            r = rows[k]
            say("    %d %-22s %8d %6.1f%% %9.5f %9.5f %7.4f %+8.4f%s" % (
                k, CLASS_NAMES[k] if k < len(CLASS_NAMES) else "?", r["pixels"], 100.0 * r["share"], r["game"], r["native"], r["ratio"],
                r["delta"], "" if BAND[0] <= r["ratio"] <= BAND[1] else "   outside %.2f to %.2f" % BAND))
    return every


def cmd_trace(args, say=print):
    head, native, depth, game = _pair(args, say)
    cls = _load_classes(args, say)
    regions = spot_regions(args.spot, game.shape[1], game.shape[0])
    regions["whole picture"] = (0, 0, game.shape[1], game.shape[0])
    return 0 if report(trace_arrays(game, native, cls, regions), say) else 1


def cmd_halfpixel(args, say=print):
    head, native, depth, game = _pair(args, say)
    cls = _load_classes(args, say)
    for k in (WOOD, GROUND):
        fit = halfpixel_arrays(luminance(game), luminance(native), cls == k)
        say("class %d %s: %s" % (k, CLASS_NAMES[k], "no fit" if fit is None else
                                 "the native picture is the game's sampled at (%+.2f, %+.2f) pixels (correlation %.4f; steps of 0.25)" % fit))
    return 0


def cmd_occluder(args, say=print):
    head, _, _, layers = read_dump(args.dump)
    if layers is None:
        raise Unreadable("the dump has no layer planes (the sun layer was not drawn)")
    fr = fp.load_frame(cr.find_capture(args.capture), args.frame, depth=False)
    frame = scene_frame_of(args.capture, args.frame)
    say("dump %s: scene frame %d; capture frame %d: scene frame %s%s" % (
        Path(args.dump).name, head["scene_frame"], args.frame, frame,
        "" if frame == head["scene_frame"] else "  ** NOT the same frame: with the wind or a moving camera the sets differ for that alone **"))
    table = fp.load_table()
    size = (layers[0].shape[1], layers[0].shape[0])
    before = layers[0].astype(np.float64) / FAR                   # the native layer's depth: z / w
    pairs = fp.occluder_rows(fr)
    say("tree occluder rows: %d; the native redraw closed %d of %d texels" % (len(pairs), int((layers[1] < layers[0]).sum()), layers[0].size))
    worst = 0
    for level in args.levels:
        closed = np.zeros(layers[0].shape, dtype=bool)
        made = 0
        for r, twin in pairs:
            if twin is None:
                say("  line %d: no twin" % r["line"])
                continue
            try:
                shut, _, _, g = fp.occluder_replay(fr, r, twin, table, size, args.origin, level, before, 1.0, 0.0, cache=args.cache)
            except Unreadable as e:
                say("  level %d, line %d: not made: %s" % (level, r["line"], e))
                continue
            made += g.state == "whole"
            closed |= shut
        k = kept_set(layers[0], layers[1], closed)
        v = kept_verdict(k) if made == len(pairs) and pairs else "not decidable"
        worst = max(worst, 0 if v == "met" else 1)
        say("level %d (sampled at %g): replay %d, native %d, both %d = %.2f%% of the replay's (at least 90%%), native alone %d = %.2f%% "
            "(at most 15%%), replay alone %d, moved farther %d: %s" % (
                level, args.origin, k["replay"], k["native"], k["both"], 100.0 * k["closed_share"], k["native_alone"],
                100.0 * k["alone_share"], k["replay_alone"], k["farther"], v))
    return worst


def cmd_predict(args, say=print):
    doc = json.loads(Path(args.inputs).read_text(encoding="utf-8"))
    for name, r in doc["regions"].items():
        figures = [predict_region(m, r["d0"], r["layer"], r["scene_factor"], r["layer_factor"]) for m in r["measured"]]
        say("%s: measured %s -> predicted %s (scene x %.3f, layer x %.3f)" % (
            name, ", ".join("%.3f" % m for m in r["measured"]), ", ".join("%.3f" % f for f in figures), r["scene_factor"], r["layer_factor"]))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("head").add_argument("dump")
    sub.add_parser("holes").add_argument("dump", nargs="+")
    p = sub.add_parser("classes")
    p.add_argument("capture")
    p.add_argument("frame", type=int)
    p.add_argument("--out", required=True)
    p.add_argument("--cache")
    for name in ("trace", "halfpixel", "occluder"):
        p = sub.add_parser(name)
        p.add_argument("dump")
        p.add_argument("capture")
        p.add_argument("frame", type=int)
        p.add_argument("--cache")
        if name == "occluder":
            p.add_argument("--levels", type=lambda s: [int(x) for x in s.split(",")], default=[0])
            p.add_argument("--origin", type=float, default=0.0)
        else:
            p.add_argument("--classes", help="a numpy file of class ids: read when it is there, else written")
        if name == "trace":
            p.add_argument("--spot", default="sunward")
            p.add_argument("--scale", type=float, help="multiply the native scene by this instead of c20.w / exposure")
    sub.add_parser("predict").add_argument("inputs")
    args = ap.parse_args(argv)
    try:
        return {"head": cmd_head, "holes": cmd_holes, "classes": cmd_classes, "trace": cmd_trace, "halfpixel": cmd_halfpixel,
                "occluder": cmd_occluder, "predict": cmd_predict}[args.cmd](args)
    except (Unreadable, OSError, ValueError) as e:
        print("cannot be read: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
