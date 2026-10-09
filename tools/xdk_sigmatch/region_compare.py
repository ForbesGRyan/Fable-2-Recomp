"""Compare named regions of a native lit shot with the two emulated shots that bracket it in time.

    region_compare.py <emulated_before.png> <native.png> <emulated_after.png> --regions <file>
                      [--spot <name>] [--calibrate <region>] [--exposure <E>]
                      [--exclude x0,y0,x1,y1]... [--client left,top,width,height]
                      [--edge <name>]...

Lit pictures drift with game time (the sun moves during a run), so pixels are not compared: for each named
rectangle the mean linear colour of each picture is taken, and the native mean is set against the mean of the
two emulated shots, with the change between those two (the drift) beside it as the error bar.

Linear means the inverse of the native renderer's output mapping, display = sqrt(saturate(linear * E)):
linear = (display / 255)^2, for all three pictures alike. A ratio is therefore a factor on the native linear
target, and --calibrate prints the exposure that makes one region's ratio 1: E = current E / ratio. That holds
as long as the region does not clip in the native shot (the share of pixels with a channel at 255 is printed).
The emulated frame's own transfer curve is not known to be a square root: the ratios say how to scale the
native picture to match, not how the two scenes relate in physical light.

Rectangles are [x0, y0, x1, y1] as fractions of the client area (0, 0 top left; 1, 1 bottom right), so that a
regions file serves any window size. The driver's shots are whole-window shots: the title bar and the frame
are cut off by the picture's size (CLIENT); a picture of any other size is taken as client area in whole, and
--client overrides both. The output names the client area used.

Regions file (JSON), either one spot at top level or several under "spots" (then --spot picks one):
    {"spots": {"bridge": {"exclude": [[x0, y0, x1, y1], ...],
                          "regions": [{"name": "dirt patch", "rect": [x0, y0, x1, y1], "kind": "sunlit",
                                       "note": "..."}, ...],
                          "pairs": [{"name": "post", "a": "post sunlit side", "b": "post averted side"}]}}}
"exclude" rectangles (and --exclude) are left out of every region in every picture: a debug window, a text
banner. "kind" and "note" are printed as given (sunlit, shadowed, provisional, ...); the tool does not judge.
For each pair the brighter of its two regions is named for the emulated and for the native picture.

--edge <name> (repeatable; "all" for every edge of the spot) measures where a shadow's edge lies instead of
the regions. The regions file names segments under "edges":
    {"name": "rail post on the planks", "line": [x0, y0, x1, y1], "note": "...",
     "width": 5, "window": 4, "contrast": 1.25}
Along the segment from (x0, y0) to (x1, y1) (fractions of the client area) the linear luminance is read once
a pixel, each sample the mean of "width" pixels across the segment. The step at a position is the mean of the
"window" samples after it less the mean of the "window" samples before it; the edge is placed at the middle
of the run of positions around the largest step whose steps are at least half of it (weighted by the step),
so a hard edge and a soft one (a penumbra) are both placed at their steepest part. For each picture the
position is printed in pixels from the segment's start, then native less the mean of the two emulated shots,
the emulated shots' own difference (after less before: how far the game's edge moved by itself), and both
again in the game's pixels: a window shot shows the game's GAME picture on the client area, so a distance
along the segment is scaled by GAME / client per axis. A segment is refused when it leaves the picture, when
it is shorter than two windows, when in any of the three pictures its largest step is under "contrast" (the
brighter side over the darker, in linear luminance; 1.25 unless the edge says otherwise), or when the step
rises in one picture and falls in another.

Reads 8-bit RGB and RGBA PNG files (not interlaced) with the standard library alone.
"""
import argparse
import json
import math
import struct
import sys
import zlib
from collections import namedtuple
from itertools import accumulate
from pathlib import Path

# The client area of a window shot of tools/drive_game.ps1, by the shot's size: left, top, width, height.
CLIENT = {(1942, 1136): (11, 45, 1920, 1080), (1296, 759): (8, 31, 1280, 720)}

# Display level to linear: the inverse of display = sqrt(linear).
LINEAR = [(v / 255.0) ** 2 for v in range(256)]
LUMA = (0.2126, 0.7152, 0.0722)

Image = namedtuple("Image", "w h rgb")  # rgb: bytes, 3 per pixel, rows top to bottom
Stats = namedtuple("Stats", "pixels excluded rgb lum hue clipped")
Row = namedtuple("Row", "name kind note before native after emulated ratio channel_ratio drift hue_diff")
Pair = namedtuple("Pair", "name a b emulated_brighter native_brighter same emulated_ratio native_ratio")

# The game's picture, which a window shot shows scaled to the client area.
GAME = (1120, 720)
# An edge's defaults: pixels averaged across the segment, samples on each side of a step, least contrast.
EDGE_WIDTH = 5
EDGE_WINDOW = 4
EDGE_CONTRAST = 1.25

# position: pixels from the segment's start; low, high: the two sides' linear luminance; contrast: high / low.
Step = namedtuple("Step", "position low high contrast rising")
EdgeRow = namedtuple("EdgeRow", "name note length before native after difference spread game_difference "
                                "game_spread")


class RegionError(Exception):
    """A picture, rectangle or regions file that cannot be used; the message says which and why."""


# ---------------------------------------------------------------------------------------------------- PNG


def _unfilter(kind, row, prev, bpp):
    if kind == 0:
        return bytes(row)
    if kind == 1:  # Sub: a running sum per channel
        out = bytearray(len(row))
        for c in range(bpp):
            out[c::bpp] = bytes(accumulate(row[c::bpp], lambda a, b: (a + b) & 255))
        return bytes(out)
    if kind == 2:  # Up
        return bytes((a + b) & 255 for a, b in zip(row, prev))
    out = bytearray(row)
    if kind == 3:  # Average
        for i in range(len(out)):
            left = out[i - bpp] if i >= bpp else 0
            out[i] = (out[i] + ((left + prev[i]) >> 1)) & 255
        return bytes(out)
    if kind == 4:  # Paeth
        for i in range(len(out)):
            if i >= bpp:
                a, b, c = out[i - bpp], prev[i], prev[i - bpp]
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            else:
                pred = prev[i]
            out[i] = (out[i] + pred) & 255
        return bytes(out)
    raise RegionError("PNG row filter %d is not defined" % kind)


def decode_png(data, name="picture"):
    """An Image from the bytes of an 8-bit RGB or RGBA PNG (alpha dropped)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise RegionError("%s: not a PNG file" % name)
    pos, header, idat = 8, None, []
    while pos + 8 <= len(data):
        size, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + size]
        pos += 12 + size
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", body[:13])
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"IEND":
            break
    if header is None:
        raise RegionError("%s: PNG without a header" % name)
    w, h, depth, colour, _, _, interlace = header
    if depth != 8:
        raise RegionError("%s: bit depth %d (only 8 is read)" % (name, depth))
    if colour not in (2, 6):
        raise RegionError("%s: colour type %d (only RGB and RGBA are read)" % (name, colour))
    if interlace:
        raise RegionError("%s: interlaced PNG is not read" % name)
    bpp = 3 if colour == 2 else 4
    stride = w * bpp
    try:
        raw = zlib.decompress(b"".join(idat))
    except zlib.error as e:
        raise RegionError("%s: pixel data does not decompress (%s)" % (name, e))
    if w == 0 or h == 0 or len(raw) != h * (stride + 1):
        raise RegionError("%s: pixel data is %d bytes, %d x %d needs %d" % (name, len(raw), w, h, h * (stride + 1)))
    rows, prev = [], bytes(stride)
    for y in range(h):
        at = y * (stride + 1)
        prev = _unfilter(raw[at], raw[at + 1:at + 1 + stride], prev, bpp)
        rows.append(prev)
    pixels = b"".join(rows)
    if bpp == 4:
        rgb = bytearray(w * h * 3)
        for c in range(3):
            rgb[c::3] = pixels[c::4]
        pixels = bytes(rgb)
    return Image(w, h, pixels)


def read_png(path):
    try:
        data = Path(path).read_bytes()
    except OSError as e:
        raise RegionError("%s: cannot be read (%s)" % (path, e))
    return decode_png(data, str(path))


# ----------------------------------------------------------------------------------------------- geometry


def client_area(w, h, override=None):
    """(left, top, width, height) of the game's picture inside a shot of w x h pixels."""
    if override is None:
        return CLIENT.get((w, h), (0, 0, w, h))
    try:
        left, top, cw, ch = (int(v) for v in override)
    except (TypeError, ValueError):
        raise RegionError("client area %r: four whole numbers left, top, width, height expected" % (override,))
    if left < 0 or top < 0 or cw <= 0 or ch <= 0 or left + cw > w or top + ch > h:
        raise RegionError("client area %d x %d at %d, %d does not lie in the %d x %d picture"
                          % (cw, ch, left, top, w, h))
    return (left, top, cw, ch)


def to_pixels(rect, client):
    """A relative rectangle [x0, y0, x1, y1] of the client area as pixel bounds (x0, y0, x1, y1), end open."""
    if isinstance(rect, (str, bytes)) or not hasattr(rect, "__len__") or len(rect) != 4:
        raise RegionError("rectangle %r: four numbers x0, y0, x1, y1 expected" % (rect,))
    for v in rect:
        if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v):
            raise RegionError("rectangle %r: four finite numbers expected" % (rect,))
        if v < 0.0 or v > 1.0:
            raise RegionError("rectangle %r reaches outside the picture (coordinates are 0 to 1)" % (rect,))
    x0, y0, x1, y1 = rect
    if x1 <= x0 or y1 <= y0:
        raise RegionError("rectangle %r is empty or inverted" % (rect,))
    left, top, cw, ch = client
    px = (left + int(x0 * cw + 0.5), top + int(y0 * ch + 0.5), left + int(x1 * cw + 0.5), top + int(y1 * ch + 0.5))
    if px[2] <= px[0] or px[3] <= px[1]:
        raise RegionError("rectangle %r holds no pixel of a %d x %d client area" % (rect, cw, ch))
    return px


# -------------------------------------------------------------------------------------------------- stats


def _spans(x0, x1, cuts):
    """[x0, x1) less the intervals in cuts, as a list of intervals."""
    spans = [(x0, x1)]
    for c0, c1 in cuts:
        nxt = []
        for a, b in spans:
            if c1 <= a or c0 >= b:
                nxt.append((a, b))
                continue
            if c0 > a:
                nxt.append((a, c0))
            if c1 < b:
                nxt.append((c1, b))
        spans = nxt
    return spans


def hue_of(rgb):
    """Hue in degrees (red 0, green 120, blue 240) of a colour; None for a grey."""
    r, g, b = rgb
    hi, lo = max(rgb), min(rgb)
    if hi - lo < 1e-12:
        return None
    if hi == r:
        hue = 60.0 * ((g - b) / (hi - lo))
    elif hi == g:
        hue = 60.0 * ((b - r) / (hi - lo)) + 120.0
    else:
        hue = 60.0 * ((r - g) / (hi - lo)) + 240.0
    return hue % 360.0


def hue_difference(a, b):
    """a - b in degrees, in (-180, 180]; None if either colour is a grey."""
    if a is None or b is None:
        return None
    d = (a - b) % 360.0
    return d - 360.0 if d > 180.0 else d


def _stats(pixels, excluded, rgb, clipped):
    return Stats(pixels, excluded, rgb, sum(w * c for w, c in zip(LUMA, rgb)), hue_of(rgb), clipped)


def region_stats(img, rect, excludes=()):
    """Mean linear colour of the pixel rectangle `rect` of img, less the pixel rectangles in `excludes`."""
    x0, y0, x1, y1 = rect
    if x0 < 0 or y0 < 0 or x1 > img.w or y1 > img.h or x1 <= x0 or y1 <= y0:
        raise RegionError("pixel rectangle %r does not lie in the %d x %d picture" % (rect, img.w, img.h))
    parts = []
    for y in range(y0, y1):
        cuts = [(e[0], e[2]) for e in excludes if e[1] <= y < e[3]]
        for a, b in (_spans(x0, x1, cuts) if cuts else ((x0, x1),)):
            parts.append(img.rgb[(y * img.w + a) * 3:(y * img.w + b) * 3])
    data = b"".join(parts)
    n = len(data) // 3
    total = (x1 - x0) * (y1 - y0)
    if n == 0:
        raise RegionError("the region is wholly covered by excluded rectangles")
    channels = [data[c::3] for c in range(3)]
    # Exact counts per level, then one careful sum: the same pixels give the same mean in any order.
    rgb = tuple(math.fsum(ch.count(v) * LINEAR[v] for v in range(256)) / n for ch in channels)
    clipped = bytes(map(max, *channels)).count(255) / n
    return _stats(n, total - n, rgb, clipped)


def _ratio(a, b):
    return a / b if b > 0.0 else None


def compare(before, native, after, regions, excludes=(), client=None):
    """One Row per region: the three pictures' statistics, native over the emulated mean, and the drift."""
    for img, what in ((native, "native"), (after, "second emulated")):
        if (img.w, img.h) != (before.w, before.h):
            raise RegionError("the %s picture's size %d x %d differs from the first emulated picture's %d x %d"
                              % (what, img.w, img.h, before.w, before.h))
    area = client_area(before.w, before.h, client)
    cuts = []
    for e in excludes:
        try:
            cuts.append(to_pixels(e, area))
        except RegionError as err:
            raise RegionError("excluded %s" % err)
    rows, seen = [], set()
    for region in regions:
        name = region.get("name") if isinstance(region, dict) else None
        if not isinstance(name, str) or not name:
            raise RegionError("a region without a name: %r" % (region,))
        if name in seen:
            raise RegionError("region '%s' is named twice" % name)
        seen.add(name)
        if "rect" not in region:
            raise RegionError("region '%s' has no rect" % name)
        try:
            px = to_pixels(region["rect"], area)
            b, n, a = (region_stats(img, px, cuts) for img in (before, native, after))
        except RegionError as err:
            raise RegionError("region '%s': %s" % (name, err))
        emulated = _stats(b.pixels, b.excluded, tuple((p + q) / 2.0 for p, q in zip(b.rgb, a.rgb)),
                          max(b.clipped, a.clipped))
        drift = _ratio(a.lum, b.lum)
        rows.append(Row(name, str(region.get("kind", "")), str(region.get("note", "")), b, n, a, emulated,
                        _ratio(n.lum, emulated.lum), tuple(_ratio(p, q) for p, q in zip(n.rgb, emulated.rgb)),
                        None if drift is None else drift - 1.0, hue_difference(n.hue, emulated.hue)))
    return rows


def compare_pairs(rows, pairs):
    """One Pair per entry {"name", "a", "b"}: which of the two regions is brighter, emulated and native."""
    by_name = {r.name: r for r in rows}
    out = []
    for pair in pairs:
        if not isinstance(pair, dict) or not all(isinstance(pair.get(k), str) for k in ("name", "a", "b")):
            raise RegionError("a pair needs \"name\", \"a\" and \"b\": %r" % (pair,))
        for k in ("a", "b"):
            if pair[k] not in by_name:
                raise RegionError("pair '%s': no region named '%s'" % (pair["name"], pair[k]))
        a, b = by_name[pair["a"]], by_name[pair["b"]]
        emu = a.name if a.emulated.lum >= b.emulated.lum else b.name
        nat = a.name if a.native.lum >= b.native.lum else b.name
        out.append(Pair(pair["name"], a.name, b.name, emu, nat, emu == nat,
                        _ratio(a.emulated.lum, b.emulated.lum), _ratio(a.native.lum, b.native.lum)))
    return out


# -------------------------------------------------------------------------------------------------- edges


def _segment(line, client):
    """(x0, y0, x1, y1) in pixels of the picture (fractions of a pixel kept) of a relative segment."""
    if isinstance(line, (str, bytes)) or not hasattr(line, "__len__") or len(line) != 4:
        raise RegionError("segment %r: four numbers x0, y0, x1, y1 expected" % (line,))
    for v in line:
        if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v):
            raise RegionError("segment %r: four finite numbers expected" % (line,))
        if v < 0.0 or v > 1.0:
            raise RegionError("segment %r reaches outside the picture (coordinates are 0 to 1)" % (line,))
    left, top, cw, ch = client
    return (left + line[0] * cw, top + line[1] * ch, left + line[2] * cw, top + line[3] * ch)


def edge_profile(img, client, line, width=EDGE_WIDTH):
    """Linear luminance along a relative segment of the client area: one sample a pixel from its start, each
    the mean of `width` pixels across the segment (the pixel each point falls in)."""
    if isinstance(width, bool) or not isinstance(width, int) or width < 1:
        raise RegionError("width %r: a whole number of pixels, at least 1, expected" % (width,))
    x0, y0, x1, y1 = _segment(line, client)
    length = math.hypot(x1 - x0, y1 - y0)
    if length < 1.0:
        raise RegionError("segment %r is shorter than a pixel" % (line,))
    ux, uy = (x1 - x0) / length, (y1 - y0) / length
    left, top, cw, ch = client
    profile = []
    for k in range(int(length) + 1):
        total = 0.0
        for j in range(width):
            off = j - (width - 1) / 2.0
            x = int(math.floor(x0 + k * ux - off * uy))
            y = int(math.floor(y0 + k * uy + off * ux))
            if x < left or y < top or x >= left + cw or y >= top + ch or x >= img.w or y >= img.h:
                raise RegionError("segment %r: its band of %d pixels leaves the picture" % (line, width))
            at = (y * img.w + x) * 3
            total += sum(w * LINEAR[v] for w, v in zip(LUMA, img.rgb[at:at + 3]))
        profile.append(total / width)
    return profile


def steepest_step(profile, window=EDGE_WINDOW, contrast=EDGE_CONTRAST):
    """The Step of a luminance profile: where its largest step lies, and the two sides."""
    if isinstance(window, bool) or not isinstance(window, int) or window < 1:
        raise RegionError("window %r: a whole number of samples, at least 1, expected" % (window,))
    if isinstance(contrast, bool) or not isinstance(contrast, (int, float)) or not contrast > 1.0:
        raise RegionError("contrast %r: a number above 1 expected" % (contrast,))
    n = len(profile)
    if n < 2 * window + 1:
        raise RegionError("the segment has %d samples: at least %d are needed for a window of %d"
                          % (n, 2 * window + 1, window))
    sides = [(math.fsum(profile[i - window:i]) / window, math.fsum(profile[i:i + window]) / window)
             for i in range(window, n - window + 1)]
    steps = [b - a for a, b in sides]
    best = max(range(len(steps)), key=lambda i: (abs(steps[i]), -i))
    peak = steps[best]
    low, high = min(sides[best]), max(sides[best])
    ratio = high / low if low > 0.0 else (math.inf if high > 0.0 else 1.0)
    if peak == 0.0 or ratio < contrast:
        raise RegionError("no step above contrast %.2f (the largest is %.2f)" % (contrast, ratio))
    # The run around the largest step whose steps have its direction and at least half its size.
    first = last = best
    while first > 0 and steps[first - 1] * peak > 0.0 and abs(steps[first - 1]) >= 0.5 * abs(peak):
        first -= 1
    while last + 1 < len(steps) and steps[last + 1] * peak > 0.0 and abs(steps[last + 1]) >= 0.5 * abs(peak):
        last += 1
    weight = math.fsum(abs(steps[i]) for i in range(first, last + 1))
    middle = math.fsum(abs(steps[i]) * i for i in range(first, last + 1)) / weight
    # steps[i] lies between the samples i + window - 1 and i + window.
    return Step(middle + window - 0.5, low, high, ratio, peak > 0.0)


def _game_pixels(distance, line, client):
    """A distance in picture pixels along a segment, in the game's pixels."""
    x0, y0, x1, y1 = _segment(line, client)
    length = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / length, (y1 - y0) / length
    return distance * math.hypot(ux * GAME[0] / client[2], uy * GAME[1] / client[3])


def compare_edges(before, native, after, edges, client=None):
    """One EdgeRow per edge {"name", "line"[, "note", "width", "window", "contrast"]}."""
    for img, what in ((native, "native"), (after, "second emulated")):
        if (img.w, img.h) != (before.w, before.h):
            raise RegionError("the %s picture's size %d x %d differs from the first emulated picture's %d x %d"
                              % (what, img.w, img.h, before.w, before.h))
    area = client_area(before.w, before.h, client)
    rows, seen = [], set()
    for edge in edges:
        name = edge.get("name") if isinstance(edge, dict) else None
        if not isinstance(name, str) or not name:
            raise RegionError("an edge without a name: %r" % (edge,))
        if name in seen:
            raise RegionError("edge '%s' is named twice" % name)
        seen.add(name)
        if "line" not in edge:
            raise RegionError("edge '%s' has no line" % name)
        steps = []
        for img, what in ((before, "first emulated"), (native, "native"), (after, "second emulated")):
            try:
                profile = edge_profile(img, area, edge["line"], edge.get("width", EDGE_WIDTH))
                steps.append(steepest_step(profile, edge.get("window", EDGE_WINDOW),
                                           edge.get("contrast", EDGE_CONTRAST)))
            except RegionError as err:
                raise RegionError("edge '%s', %s picture: %s" % (name, what, err))
        b, n, a = steps
        if not b.rising == n.rising == a.rising:
            raise RegionError("edge '%s': the step is not of one direction in the three pictures (%s)"
                              % (name, ", ".join("rising" if s.rising else "falling" for s in steps)))
        difference = n.position - (b.position + a.position) / 2.0
        spread = a.position - b.position
        rows.append(EdgeRow(name, str(edge.get("note", "")), len(profile) - 1, b, n, a, difference, spread,
                            _game_pixels(difference, edge["line"], area),
                            _game_pixels(spread, edge["line"], area)))
    return rows


def calibrated_exposure(ratio, current):
    """The exposure that makes a region's native / emulated ratio 1, given the one the shot was made with."""
    for v, what in ((current, "the current exposure"), (ratio, "the region's ratio")):
        if v is None or isinstance(v, bool) or not math.isfinite(v) or v <= 0.0:
            raise RegionError("%s is %r: a positive finite number is needed to calibrate" % (what, v))
    return current / ratio


# --------------------------------------------------------------------------------------------------- main


def _spot_doc(path, spot=None):
    """The object of a regions file that holds one spot's lists."""
    try:
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
    except OSError as e:
        raise RegionError("%s: cannot be read (%s)" % (path, e))
    except ValueError as e:
        raise RegionError("%s: not JSON (%s)" % (path, e))
    if not isinstance(doc, dict):
        raise RegionError("%s: an object is expected at top level" % path)
    if "spots" in doc:
        spots = doc["spots"]
        if not isinstance(spots, dict) or not spots:
            raise RegionError("%s: \"spots\" must be an object of spots" % path)
        names = ", ".join(sorted(spots))
        if spot is None:
            raise RegionError("%s holds several spots: pick one with --spot (%s)" % (path, names))
        if spot not in spots:
            raise RegionError("%s: no spot '%s' (it has: %s)" % (path, spot, names))
        doc = spots[spot]
    elif spot is not None:
        raise RegionError("%s has no \"spots\": --spot %s does not apply" % (path, spot))
    return doc


def load_edges(path, spot=None):
    """The "edges" list of a regions file, for the named spot if the file holds several (may be empty)."""
    doc = _spot_doc(path, spot)
    edges = doc.get("edges", []) if isinstance(doc, dict) else None
    if not isinstance(edges, list):
        raise RegionError("%s: \"edges\" must be a list" % path)
    return edges


def load_regions(path, spot=None):
    """(regions, pairs, excludes) of a regions file, for the named spot if the file holds several."""
    doc = _spot_doc(path, spot)
    regions = doc.get("regions") if isinstance(doc, dict) else None
    if not isinstance(regions, list) or not regions:
        raise RegionError("%s: a non-empty \"regions\" list is expected" % path)
    pairs, excludes = doc.get("pairs", []), doc.get("exclude", [])
    if not isinstance(pairs, list) or not isinstance(excludes, list):
        raise RegionError("%s: \"pairs\" and \"exclude\" must be lists" % path)
    return regions, pairs, excludes


def _numbers(text, kind, what):
    try:
        values = [kind(v) for v in text.split(",")]
    except ValueError:
        values = []
    if len(values) != 4:
        raise RegionError("%s '%s': four numbers separated by commas expected" % (what, text))
    return values


def _fmt(v, spec, none="-"):
    return none if v is None else format(v, spec)


def report(rows, pairs, out):
    width = max([len(r.name) for r in rows] + [len(p.name) for p in pairs] + [6])
    kind = max([len(r.kind) for r in rows] + [4])
    out.write("%-*s  %-*s  %7s  %8s %8s %8s  %6s  %6s %6s %6s  %7s  %6s %6s %6s  %7s\n"
              % (width, "region", kind, "kind", "pixels", "emu bef", "native", "emu aft", "ratio",
                 "R", "G", "B", "drift", "hue e", "hue n", "diff", "clipped"))
    for r in rows:
        out.write("%-*s  %-*s  %7d  %8.5f %8.5f %8.5f  %6s  %6s %6s %6s  %7s  %6s %6s %6s  %6.1f%%\n"
                  % (width, r.name, kind, r.kind, r.native.pixels, r.before.lum, r.native.lum, r.after.lum,
                     _fmt(r.ratio, ".3f"), _fmt(r.channel_ratio[0], ".3f"), _fmt(r.channel_ratio[1], ".3f"),
                     _fmt(r.channel_ratio[2], ".3f"), _fmt(None if r.drift is None else 100.0 * r.drift, "+.1f"),
                     _fmt(r.emulated.hue, ".1f"), _fmt(r.native.hue, ".1f"), _fmt(r.hue_diff, "+.1f"),
                     100.0 * r.native.clipped))
    if pairs:
        out.write("\n%-*s  brighter in the emulated shots / in the native shot (a over b: emulated, native)\n"
                  % (width, "pair"))
        for p in pairs:
            out.write("%-*s  %s / %s: %s (%s, %s)\n"
                      % (width, p.name, p.emulated_brighter, p.native_brighter, "same" if p.same else "DIFFERENT",
                         _fmt(p.emulated_ratio, ".3f"), _fmt(p.native_ratio, ".3f")))


def report_edges(rows, out):
    width = max([len(r.name) for r in rows] + [4])
    out.write("%-*s  %6s  %8s %8s %8s  %10s %8s  %10s %8s  %s\n"
              % (width, "edge", "length", "emu bef", "native", "emu aft", "difference", "spread", "game diff",
                 "spread", "contrast (emulated before, native, emulated after), direction"))
    for r in rows:
        out.write("%-*s  %6d  %8.1f %8.1f %8.1f  %10s %8s  %10s %8s  %.2f, %.2f, %.2f, %s\n"
                  % (width, r.name, r.length, r.before.position, r.native.position, r.after.position,
                     format(r.difference, "+.1f"), format(r.spread, "+.1f"), format(r.game_difference, "+.1f"),
                     format(r.game_spread, "+.1f"), r.before.contrast, r.native.contrast, r.after.contrast,
                     "rising" if r.native.rising else "falling"))


def _main_edges(a):
    try:
        edges = load_edges(a.regions, a.spot)
        if not edges:
            raise RegionError("%s: no \"edges\" to measure" % a.regions)
        if "all" not in a.edge:
            by_name = {e.get("name"): e for e in edges if isinstance(e, dict)}
            for name in a.edge:
                if name not in by_name:
                    raise RegionError("--edge: no edge named '%s' (it has: %s)"
                                      % (name, ", ".join(sorted(str(n) for n in by_name))))
            edges = [by_name[name] for name in a.edge]
        client = _numbers(a.client, int, "--client") if a.client else None
        before, native, after = (read_png(p) for p in (a.emulated_before, a.native, a.emulated_after))
        rows = compare_edges(before, native, after, edges, client)
    except RegionError as e:
        sys.stderr.write("region_compare: %s\n" % e)
        return 2
    out = sys.stdout
    out.write("emulated before: %s\nnative:          %s\nemulated after:  %s\n"
              % (a.emulated_before, a.native, a.emulated_after))
    left, top, cw, ch = client_area(before.w, before.h, client)
    out.write("picture %d x %d; client area: %d x %d at %d, %d\n" % (before.w, before.h, cw, ch, left, top))
    out.write("position = pixels from the segment's start to the steepest luminance step; difference = native - "
              "mean of the two emulated; spread = emulated after - before; game = the same in pixels of the "
              "game's %d x %d picture (x %.4f across, x %.4f down)\n\n"
              % (GAME[0], GAME[1], GAME[0] / cw, GAME[1] / ch))
    report_edges(rows, out)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("emulated_before")
    ap.add_argument("native")
    ap.add_argument("emulated_after")
    ap.add_argument("--regions", required=True, help="regions file (JSON)")
    ap.add_argument("--spot", help="the spot to take from a regions file that holds several")
    ap.add_argument("--calibrate", metavar="REGION", help="print the exposure that makes this region's ratio 1")
    ap.add_argument("--exposure", type=float, default=1.0, help="the exposure the native shot was made with")
    ap.add_argument("--exclude", action="append", default=[], metavar="x0,y0,x1,y1",
                    help="a relative rectangle left out of every region (repeatable)")
    ap.add_argument("--client", metavar="left,top,width,height", help="the client area in pixels")
    ap.add_argument("--edge", action="append", default=[], metavar="NAME",
                    help="measure this edge of the regions file instead of the regions (repeatable; 'all')")
    a = ap.parse_args(argv)
    if a.edge:
        return _main_edges(a)
    try:
        regions, pairs, excludes = load_regions(a.regions, a.spot)
        excludes = list(excludes) + [_numbers(e, float, "--exclude") for e in a.exclude]
        client = _numbers(a.client, int, "--client") if a.client else None
        before, native, after = (read_png(p) for p in (a.emulated_before, a.native, a.emulated_after))
        rows = compare(before, native, after, regions, excludes, client)
        pair_rows = compare_pairs(rows, pairs)
        exposure = None
        if a.calibrate is not None:
            target = [r for r in rows if r.name == a.calibrate]
            if not target:
                raise RegionError("--calibrate: no region named '%s'" % a.calibrate)
            exposure = calibrated_exposure(target[0].ratio, a.exposure)
    except RegionError as e:
        sys.stderr.write("region_compare: %s\n" % e)
        return 2
    out = sys.stdout
    out.write("emulated before: %s\nnative:          %s\nemulated after:  %s\n"
              % (a.emulated_before, a.native, a.emulated_after))
    left, top, cw, ch = client_area(before.w, before.h, client)
    if (left, top, cw, ch) == (0, 0, before.w, before.h):
        out.write("picture %d x %d; client area: the whole picture\n" % (before.w, before.h))
    else:
        out.write("picture %d x %d; client area: %d x %d at %d, %d\n" % (before.w, before.h, cw, ch, left, top))
    out.write("linear = (display / 255)^2; luminance = 0.2126 R + 0.7152 G + 0.0722 B; ratio = native / mean of "
              "the two emulated; drift = emulated after / before - 1; hue in degrees of the mean linear colour\n")
    out.write("excluded rectangles: %d\n\n" % len(excludes))
    report(rows, pair_rows, out)
    if exposure is not None:
        r = target[0]
        by_channel = ", ".join("%s %s" % (c, _fmt(None if v is None else a.exposure / v, ".4f"))
                               for c, v in zip("RGB", r.channel_ratio))
        out.write("\ncalibrate on '%s': ratio %.4f at E = %.4f -> E = %.4f (by channel: %s)%s\n"
                  % (r.name, r.ratio, a.exposure, exposure, by_channel,
                     "; %.1f%% of the region clips in the native shot" % (100.0 * r.native.clipped)
                     if r.native.clipped > 0.0 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
