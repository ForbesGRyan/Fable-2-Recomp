"""The replay's variant C: variant B of post_replay.py with the game's half-size "sun layer" rasterised on the
CPU from a capture's quad rows and the oracle's depth (plan Task 11, spec criterion 18).

  post_sun_layer.py replay <capture> <frame> [--bias 0] [--sun-roles research|exchanged] [--png <prefix>]
                                             [--region-file NAME ...] [--spot bridge]
  post_sun_layer.py quads  <capture> <frame>

<capture>: a read-back capture, as post_replay.py takes it. Read besides what variant B reads: the "indx" rows
of the frame whose pixel shader is one of light_formulas.POST_FURNITURE_SHADERS (or whose vertex shader is the
cubes') at the layer's target (pitch 560), each with its "indxdata" row (the corners as the game filled them),
its registers, literals, state words and textures; and the scene depth 0x1A02A000 with the content the layer's
depth draw names.

What is replayed, in the rows' order, at half the scene's size:
  depth    the layer's depth: the scene's at every second texel (5D67: light_formulas.post_layer_depth);
           then the cloud occluders (3FF7: where post_cloud_occluder is above the draw's alpha reference) and
           the three cubes (D1CC: post_cube_position), which write depth only;
  layer 1  the beam source (8E4D), depth-tested, no blend;
  layer 2  flares (D72F, additive), the moon glow (4B58, alpha blend, alpha test), the sun disc (3E77,
           additive, alpha test), each depth-tested;
  then     each picture reduced to a quarter of the scene's size (0A96: post_reduce), layer 1 twice through
           the radial blur (4BE4: post_radial with the frame's registers), and the bright pass (EE92) with
           tf1 = the blurred beams and tf2 = the reduced layer 2. Every write is clamped and rounded to the
           10 bits of a k_2_10_10_10 target.
The scene's own furniture (moon, clouds, glare) is in the oracle's scene colour already and is not drawn.

What is this tool's and not read from the game (each also in the document's "What stands on inference"):
  - which picture is tf1 and which tf2, and that the radial blur's two passes are both of layer 1 (the
    research's reading by the weights; plan Task 10 is to settle it; --sun-roles exchanged gives the other);
  - a pixel is sampled at its integer position (PIXEL_CENTRE: the post rectangles' convention of Task 3), so
    the layer's depth is the scene's texel (2 x, 2 y);
  - the layer's depth is kept between layer 1 and layer 2 (the clears between them are taken as colour only);
  - the tree occluder draws (F1EB: indexed tree geometry, 9,000 to 24,000 indices) are NOT replayed: no row
    holds their vertices. The scene's depth already holds the scene's own trees;
  - the cubes are drawn without culling (their cull word 0x00218002 is not applied: seen from inside, as the
    eye is, five faces of a box face the eye if any does);
  - a texture's levels are made here by means of four (a dump holds the base level only) and a quad reads the
    level of its mean footprint; the cloud occluders read level 0;
  - textures are read as stored (no gamma decode; the fetch constants' sign fields are 0 in every row).

Pure Python: a replay takes one to two minutes.
"""
import argparse
import json
import math
import struct
import sys
from array import array
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fog_tables  # noqa: E402
import light_formulas as lf  # noqa: E402
import post_replay as pr  # noqa: E402
from texture_thumb import decode_rgba, write_png  # noqa: E402

ReplayError = pr.ReplayError
DEPTH_BASE = 0x1A02A000
LAYER_PITCH = pr.SCENE_W // 2
PIXEL_CENTRE = 0.0          # where a pixel is sampled: its integer position (Task 3's inference for the post rectangles)
W_MIN = 1e-6                # the near clip: w at least this
FURNITURE = {s.hash: s.role for s in lf.POST_FURNITURE_SHADERS}
WHERE = {s.kind: s.where for s in lf.POST_QUAD_SHADERS}
WHERE.update({"layer-depth": "layer-depth", "cube": "layer-depth"})
TREE_OCCLUDER = "F1EB2497108F159D"
LAYER_ROLES = ("layer-depth", "cloud-occluder", "cube", "beam-source", "flare", "moon-glow", "sun-disc")
SCENE_ROLES = ("moon", "cloud", "glare")
_BLEND_FACTORS = {0: lambda a: 0.0, 1: lambda a: 1.0, 6: lambda a: a, 7: lambda a: 1.0 - a}


# --- the rasteriser ------------------------------------------------------------------------------------------

def clip_near(poly, w_min=W_MIN):
    """A polygon of clip-space vertices (x, y, z, w, *attributes) cut at w >= w_min (Sutherland-Hodgman)."""
    out = []
    for i, a in enumerate(poly):
        b = poly[(i + 1) % len(poly)]
        a_in, b_in = a[3] >= w_min, b[3] >= w_min
        if a_in:
            out.append(tuple(a))
        if a_in != b_in:
            t = (w_min - a[3]) / (b[3] - a[3])
            out.append(tuple(p + (q - p) * t for p, q in zip(a, b)))
    return out


def _screen(v, w, h):
    iw = 1.0 / v[3]
    return (v[0] * iw * 0.5 + 0.5) * w, (0.5 - v[1] * iw * 0.5) * h, v[2] * iw, iw, [x * iw for x in v[4:]]


def _top_left(ax, ay, bx, by):
    """Whether the edge a -> b of a triangle that runs clockwise on the screen (y down) is a top or a left edge."""
    return (ay == by and bx > ax) or by < ay


def raster_triangle(a, b, c, w, h, pixel, centre=PIXEL_CENTRE):
    """Calls pixel(x, y, depth, attributes) for every pixel of a w x h target whose sample point
    (x + centre, y + centre) the triangle covers, an edge's pixels by the top-left rule (two triangles that
    share an edge share no pixel). Vertices: clip space (x, y, z, w, *attributes) with w > 0. depth: z / w,
    linear on the screen; the attributes are correct in perspective. Returns the number of pixels."""
    p0, p1, p2 = _screen(a, w, h), _screen(b, w, h), _screen(c, w, h)
    area = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0])
    if not math.isfinite(area) or area == 0.0:
        return 0
    if area < 0.0:
        p1, p2, area = p2, p1, -area
    pts = (p0, p1, p2)
    edges = []      # the edge opposite vertex i: E(p) = A x + B y + C, positive inside
    for i in range(3):
        (ax, ay), (bx, by) = pts[(i + 1) % 3][:2], pts[(i + 2) % 3][:2]
        edges.append((-(by - ay), bx - ax, (by - ay) * ax - (bx - ax) * ay, _top_left(ax, ay, bx, by)))
    y0 = max(int(math.ceil(min(p[1] for p in pts) - centre)), 0)
    y1 = min(int(math.floor(max(p[1] for p in pts) - centre)), h - 1)
    x_lo = max(int(math.ceil(min(p[0] for p in pts) - centre)), 0)
    x_hi = min(int(math.floor(max(p[0] for p in pts) - centre)), w - 1)
    count = 0
    n_attr = len(p0[4])
    for y in range(y0, y1 + 1):
        sy = y + centre
        lo, hi = x_lo, x_hi
        for A, B, C, _ in edges:        # the row's span, a pixel wider than the exact one; the test below decides
            k = B * sy + C
            if A > 0.0:
                lo = max(lo, int(math.floor(-k / A - centre)) - 1)
            elif A < 0.0:
                hi = min(hi, int(math.ceil(-k / A - centre)) + 1)
            elif k < 0.0:
                lo, hi = 1, 0
        for x in range(max(lo, x_lo), min(hi, x_hi) + 1):
            sx = x + centre
            e = [A * sx + B * sy + C for A, B, C, _ in edges]
            if any(v < 0.0 or (v == 0.0 and not edges[i][3]) for i, v in enumerate(e)):
                continue
            b0, b1, b2 = e[0] / area, e[1] / area, e[2] / area
            iw = b0 * p0[3] + b1 * p1[3] + b2 * p2[3]
            attrs = [(b0 * p0[4][k] + b1 * p1[4][k] + b2 * p2[4][k]) / iw for k in range(n_attr)]
            pixel(x, y, b0 * p0[2] + b1 * p1[2] + b2 * p2[2], attrs)
            count += 1
    return count


def raster(vertices, indices, w, h, pixel):
    """The triangles of an index list, each cut at the near clip and drawn (no culling). Returns the pixels."""
    count = 0
    for i in range(0, len(indices) - 2, 3):
        poly = clip_near([vertices[k] for k in indices[i:i + 3]])
        for k in range(1, len(poly) - 1):
            count += raster_triangle(poly[0], poly[k], poly[k + 1], w, h, pixel)
    return count


def quad_lod(vertices, indices, tex_w, tex_h, w, h):
    """The texture level of a quad's mean footprint: half the base-2 logarithm of its texels over its pixels
    (both as areas, after the near clip), at least 0. vertices: (x, y, z, w, u, v)."""
    texels = pixels = 0.0
    for i in range(0, len(indices) - 2, 3):
        poly = clip_near([vertices[k] for k in indices[i:i + 3]])
        pts = [_screen(v, w, h) for v in poly]
        for k in range(1, len(poly) - 1):
            a, b, c = pts[0], pts[k], pts[k + 1]
            pixels += abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / 2.0
            ua, ub, uc = poly[0][4:6], poly[k][4:6], poly[k + 1][4:6]
            texels += abs((ub[0] - ua[0]) * (uc[1] - ua[1]) - (ub[1] - ua[1]) * (uc[0] - ua[0])) / 2.0 * tex_w * tex_h
    if not (pixels > 0.0 and texels > 0.0 and math.isfinite(pixels) and math.isfinite(texels)):
        return 0.0
    return max(0.0, 0.5 * math.log2(texels / pixels))


# --- textures ------------------------------------------------------------------------------------------------

class MipTexture:
    """A decoded texture (RGBA bytes) with its smaller levels made here, each texel the mean of four."""

    def __init__(self, w, h, rgba, wrap=True):
        if len(rgba) != w * h * 4:
            raise ReplayError("%d bytes for a texture of %d x %d" % (len(rgba), w, h))
        self.wrap = wrap
        self.levels = [(w, h, array("d", [b / 255.0 for b in rgba]))]

    def _level(self, k):
        while len(self.levels) <= k:
            w, h, src = self.levels[-1]
            if w == 1 and h == 1:
                break
            nw, nh = max(w // 2, 1), max(h // 2, 1)
            out = array("d", bytes(8 * nw * nh * 4))
            for y in range(nh):
                ya, yb = min(2 * y, h - 1), min(2 * y + 1, h - 1)
                for x in range(nw):
                    xa, xb = min(2 * x, w - 1), min(2 * x + 1, w - 1)
                    o = (y * nw + x) * 4
                    for c in range(4):
                        out[o + c] = (src[(ya * w + xa) * 4 + c] + src[(ya * w + xb) * 4 + c]
                                      + src[(yb * w + xa) * 4 + c] + src[(yb * w + xb) * 4 + c]) / 4.0
            self.levels.append((nw, nh, out))
        return self.levels[min(k, len(self.levels) - 1)]

    def _linear(self, k, u, v, dx, dy):
        w, h, data = self._level(k)
        x, y = u * w - 0.5 + dx, v * h - 0.5 + dy
        x0, y0 = math.floor(x), math.floor(y)
        fx, fy = x - x0, y - y0
        if self.wrap:
            xa, xb, ya, yb = int(x0) % w, int(x0 + 1) % w, int(y0) % h, int(y0 + 1) % h
        else:
            xa, xb = (int(min(max(p, 0), w - 1)) for p in (x0, x0 + 1))
            ya, yb = (int(min(max(p, 0), h - 1)) for p in (y0, y0 + 1))
        out = []
        for c in range(4):
            top = data[(ya * w + xa) * 4 + c] * (1.0 - fx) + data[(ya * w + xb) * 4 + c] * fx
            bottom = data[(yb * w + xa) * 4 + c] * (1.0 - fx) + data[(yb * w + xb) * 4 + c] * fx
            out.append(top * (1.0 - fy) + bottom * fy)
        return out

    def sample(self, u, v, lod=0.0, dx=0, dy=0):
        """Four channels at the normalised coordinate, linear within a level and between two levels; (dx, dy):
        an offset in texels of the lower level read. A coordinate that is not finite reads zeros."""
        if not (math.isfinite(u) and math.isfinite(v)) or abs(u) > 1e6 or abs(v) > 1e6:
            return [0.0, 0.0, 0.0, 0.0]
        lod = lod if lod > 0.0 else 0.0
        self._level(int(lod) + 1)
        top = len(self.levels) - 1
        k = min(int(lod), top)
        a = self._linear(k, u, v, dx, dy)
        f = lod - k
        if f <= 0.0 or k >= top:
            return a
        b = self._linear(k + 1, u, v, dx, dy)
        return [p + (q - p) * f for p, q in zip(a, b)]


# --- a draw of the layer -------------------------------------------------------------------------------------

def depth_passes(func, new, old):
    """The depth comparison `func` (0 never, 1 less, 2 equal, 3 less or equal, 4 greater, 5 not equal,
    6 greater or equal, 7 always) of a new value against the stored one. The game's depth is reversed."""
    if func == 0 or new != new or old != old:
        return False
    return (func == 7, new < old, new == old, new <= old, new > old, new != old, new >= old)[func % 7]


def blend(word, src, dst):
    """The colour a blend word (register 0x2201) leaves: src.rgb * F_src + dst * F_dst with the factors of its
    low two bytes (0 zero, 1 one, 6 source alpha, 7 one less source alpha). src: four floats; dst: three."""
    fs, fd = word & 0x1F, (word >> 8) & 0x1F
    if fs not in _BLEND_FACTORS or fd not in _BLEND_FACTORS or (word >> 5) & 7:
        raise ReplayError("blend word 0x%08X is none of the layer's" % word)
    ks, kd = _BLEND_FACTORS[fs](src[3]), _BLEND_FACTORS[fd](src[3])
    return [src[i] * ks + dst[i] * kd for i in range(3)]


def _float(word):
    return struct.unpack(">f", struct.pack(">I", word & 0xFFFFFFFF))[0]


class LayerDraw:
    """An "indx" row of a furniture shader (or of the cubes' vertex shader) with its "indxdata" row."""

    def __init__(self, row, data):
        self.row, self.data = row, data
        self.ps = row.get("ps_hash", "0x")[2:].upper()
        self.vs = ((row.get("vs") or {}).get("hash") or row.get("vs_hash") or "0x")[2:].upper()
        self.role = FURNITURE.get(self.ps) or FURNITURE.get(self.vs)
        self.where = WHERE.get(self.role)
        self.pitch = int(row.get("surface_info", "0x0"), 16) & 0x3FFF
        self.in_scene = bool(row.get("in_scene"))
        self.rs = {}
        for k, v in (row.get("rs") or {}).items():
            try:
                self.rs[int(k, 16)] = int(v, 16)
            except ValueError:
                pass
        self.literals = self._literals(row.get("ps_lit"))
        self.vs_literals = self._literals(row.get("vs_lit"))
        self.fc = {int(s): [int(x, 16) for x in v] for s, v in (row.get("tf") or {}).items()}
        self.tfc = {int(s): int(v, 16) for s, v in (row.get("tfc") or {}).items()}
        stride = data["stride"]
        raw = bytes.fromhex(data["verts"])
        if stride % 4 or stride == 0 or len(raw) % stride:
            raise ReplayError("%d vertex bytes of stride %d" % (len(raw), stride))
        self.vertices = [struct.unpack(">%df" % (stride // 4), raw[k:k + stride]) for k in range(0, len(raw), stride)]
        self.indices = list(data["idx"])
        if any(not 0 <= i < len(self.vertices) for i in self.indices):
            raise ReplayError("an index beyond the %d vertices" % len(self.vertices))

    @staticmethod
    def _literals(entries):
        out = {}
        for e in entries or []:
            for k in range(len(e["v"]) // 4):
                out[e["reg"] % 256 + k] = tuple(struct.unpack("<4f", struct.pack("<4f", *e["v"][4 * k:4 * k + 4])))
        return out

    def reg(self, n):
        v = fog_tables.pfull_reg(self.row.get("pfull", ""), n)
        if v is None:
            raise ReplayError("%s: no pixel register c%d in the row" % (self.role, n))
        return v

    def literal(self, n):
        for where in (self.literals, self.vs_literals):
            if n in where:
                return where[n]
        raise ReplayError("%s: no literal c%d in the row" % (self.role, n))

    def vreg(self, n):
        bank = self.row.get("bank") or []
        if len(bank) < 4 * n + 4:
            raise ReplayError("%s: no vertex register c%d in the row" % (self.role, n))
        return tuple(bank[4 * n:4 * n + 4])

    @property
    def view_proj(self):
        return [list(self.vreg(i)) for i in range(4)]

    depth_word = property(lambda self: self.rs.get(0x2200, 0))
    depth_test = property(lambda self: bool(self.depth_word & 2))
    depth_write = property(lambda self: bool(self.depth_word & 4))
    depth_func = property(lambda self: (self.depth_word >> 4) & 7)
    blend_word = property(lambda self: self.rs.get(0x2201, 0x00010001))
    colour_mask = property(lambda self: self.rs.get(0x2104, 0xF) & 0xF)
    alpha_ref = property(lambda self: _float(self.rs.get(0x210E, 0)))
    zscale = property(lambda self: _float(self.rs[0x2113]) if 0x2113 in self.rs else 1.0)
    zoffset = property(lambda self: _float(self.rs[0x2114]) if 0x2114 in self.rs else 0.0)

    @property
    def alpha_test(self):
        """Register 0x2202 bit 3; the function (bits 0 to 2) must then be 4, "greater"."""
        word = self.rs.get(0x2202, 0)
        if not word & 8:
            return False
        if word & 7 != 4:
            raise ReplayError("%s: alpha function %d, not 4 (greater)" % (self.role, word & 7))
        return True


def _furniture_rows(path, number):
    """(the "indx" rows of furniture shaders of a frame with their "indxdata" rows, as LayerDraws in the rows'
    order; the frame's other rows of furniture pixel shaders)."""
    draws, pending, others = [], {}, []
    marks = ['"0x%s"' % h for h in FURNITURE]
    with open(path, encoding="utf-8") as f:
        for line in f:
            head = line[:48]
            if not head.startswith('{"kind":"'):
                continue
            kind = head[9:head.find('"', 9)]
            if kind not in ("indx", "indxdata", "draw") or ('"frame":%d,' % number) not in head:
                continue
            if kind == "indxdata":
                n = json.loads(line[:line.index(',"verts"')] + "}")["n"] if ',"verts"' in line else None
                if n in pending:
                    draws[pending.pop(n)][1] = json.loads(line)
                continue
            if not any(m in line for m in marks):
                continue
            row = json.loads(line)
            if kind == "indx":
                pending[row.get("n")] = len(draws)
                draws.append([row, None])
            else:
                others.append(row)
    for row, data in draws:
        if data is None:
            raise ReplayError("%s frame %d: the draw n = %s has no \"indxdata\" row" % (Path(path).name, number, row.get("n")))
    return [LayerDraw(row, data) for row, data in draws], others


def load_layer_draws(path, number, pitch=LAYER_PITCH):
    """The frame's draws at the sun layer's target (pitch 560, outside the scene), in the rows' order, and a
    count of what is seen and not replayed."""
    draws, others = _furniture_rows(path, number)
    out, other = [], {}

    def count(word):
        other[word] = other.get(word, 0) + 1

    for d in draws:
        if d.in_scene or d.role not in LAYER_ROLES:
            continue
        if d.pitch == pitch:
            out.append(d)
        elif d.role == "flare":
            count("flare quads at another target (pitch %d)" % d.pitch)
    for row in others:
        if row.get("ps_hash", "")[2:].upper() == TREE_OCCLUDER and int(row.get("surface_info", "0x0"), 16) & 0x3FFF == pitch:
            count("tree-occluder draws at the layer (not replayed)")
    return out, other


def scene_quads(path, number):
    """The frame's furniture quads in the main scene (moon, clouds, glare), in the rows' order."""
    return [d for d in _furniture_rows(path, number)[0] if d.in_scene and d.role in SCENE_ROLES]


# --- the layer -----------------------------------------------------------------------------------------------

def layer_depth(depth, w, h):
    """The layer's depth before its occluders: the scene's (w * h floats) at texel (2 x, 2 y), clamped."""
    return [lf.post_layer_depth((depth[2 * y * w + 2 * x],))[1] for y in range(h // 2) for x in range(w // 2)]


def _clip_vertices(d):
    """A draw's vertices in clip space with their attributes: a quad's (u, v) and, for the cloud shaders'
    vertex shader, the world position; a cube's none."""
    vp = d.view_proj
    if d.role == "cube":
        c254, c255x = d.literal(254), d.literal(255)[0]
        return [tuple(lf.post_cube_position(v[:3], *vp, d.vreg(9), d.vreg(119)[0], c254, c255x)) for v in d.vertices]
    out = []
    for v in d.vertices:
        if len(v) != 5:
            raise ReplayError("%s: a vertex of %d floats, not 5" % (d.role, len(v)))
        out.append(tuple(lf.post_quad_position(v[:3], *vp)) + (v[3], v[4]) + tuple(v[:3]))
    return out


def _shader(d, tex, lod):
    """attributes -> (colour of four floats or None, alpha) for a draw's pixel shader."""
    if d.role == "cube":
        return lambda a: (None, 1.0)
    if d.role == "cloud-occluder":
        c31, c46, c47, c77x, c255 = d.reg(31), d.reg(46), d.reg(47), d.reg(77)[0], d.literal(255)
        fetch = lambda u, v, dx, dy: tex.sample(u, v, 0.0, dx, dy)      # noqa: E731
        return lambda a: (None, lf.post_cloud_occluder(fetch, a[:2], a[2:5], c31, c46, c47, c77x, c255))
    if d.role == "beam-source":
        c31, k = d.reg(31), d.literal(255)[0]
        form = lambda t: lf.post_beam_source(t, c31, k)                 # noqa: E731
    elif d.role == "flare":
        c31 = d.reg(31)
        form = lambda t: lf.post_flare(t, c31)                          # noqa: E731
    elif d.role == "moon-glow":
        k = d.literal(255)[0]
        form = lambda t: lf.post_moon_glow(t, k)                        # noqa: E731
    elif d.role == "sun-disc":
        c31, c20w = d.reg(31), d.reg(20)[3]
        form = lambda t: lf.post_sun_disc(t, c31, c20w)                 # noqa: E731
    else:
        raise ReplayError("no formula for a %s draw" % d.role)

    def shade(a):
        out = form(tex.sample(a[0], a[1], lod))
        return out, out[3]

    return shade


def sun_layer(draws, depth, w, h, texture):
    """The layer's two pictures at (w / 2) x (h / 2) from the frame's layer draws (load_layer_draws) and the
    scene depth (w * h floats). texture(draw, slot): the draw's texture (an object with `sample`, as
    MipTexture). Returns (layer 1, layer 2, a line for each draw): rows of [r, g, b] in 0..1, in 10 bits."""
    lw, lh = w // 2, h // 2
    pictures = {"layer1": [[[0.0, 0.0, 0.0] for _ in range(lw)] for _ in range(lh)],
                "layer2": [[[0.0, 0.0, 0.0] for _ in range(lw)] for _ in range(lh)]}
    zbuf, lines = None, []
    for d in draws:
        if d.role == "layer-depth":
            zbuf = layer_depth(depth, w, h)
            lines.append("layer-depth: the scene's depth at every second texel; %d of %d texels hold something" % (
                sum(1 for z in zbuf if z > 0.0), len(zbuf)))
            continue
        if zbuf is None:
            raise ReplayError("a %s draw before the layer's depth draw" % d.role)
        if d.role not in LAYER_ROLES:
            raise ReplayError("a %s draw is not the layer's" % d.role)
        vertices = _clip_vertices(d)
        slot = min(d.fc) if d.fc else None
        tex = texture(d, slot) if d.role != "cube" else None
        lod = 0.0
        if d.role not in ("cube", "cloud-occluder") and hasattr(tex, "levels"):
            tw, th, _ = tex.levels[0]
            lod = quad_lod(vertices, d.indices, tw, th, lw, lh)
        shade = _shader(d, tex, lod)
        test, ref = d.alpha_test, d.alpha_ref
        z_test, z_write, z_func, zs, zo = d.depth_test, d.depth_write, d.depth_func, d.zscale, d.zoffset
        target = pictures.get(d.where) if d.colour_mask else None
        word = d.blend_word
        tally = {"drawn": 0, "alpha": 0, "depth": 0, "max": 0.0}

        def pixel(x, y, z, attrs):
            colour, alpha = shade(attrs)
            if test and not alpha > ref:
                tally["alpha"] += 1
                return
            i = y * lw + x
            zq = z * zs + zo
            if z_test and not depth_passes(z_func, zq, zbuf[i]):
                tally["depth"] += 1
                return
            if z_write:
                zbuf[i] = zq
            if target is not None and colour is not None:
                out = [pr.q10(v) for v in blend(word, colour, target[y][x])]
                target[y][x] = out
                tally["max"] = max(tally["max"], *out)
            tally["drawn"] += 1

        raster(vertices, d.indices, lw, lh, pixel)
        lines.append("%-14s -> %-11s: %d pixels drawn, %d behind the depth, %d below the alpha reference%s%s" % (
            d.role, d.where if target is not None else "depth only", tally["drawn"], tally["depth"], tally["alpha"],
            " (%.4f)" % ref if test else "", "; texture level %.2f, largest value written %.4f" % (lod, tally["max"])
            if target is not None else ""))
    return pictures["layer1"], pictures["layer2"], lines


def reduce_rows(rows):
    """A picture at half its size: light_formulas.post_reduce (the mean of four), clamped, in 10 bits."""
    h, w = len(rows) // 2, len(rows[0]) // 2
    tex = lf.post_texture(rows, linear=True)
    return [[[pr.q10(v) for v in lf.post_reduce(tex, x, y)] for x in range(w)] for y in range(h)]


def sun_inputs(layer1, layer2, c31, c46x, c47x, exchanged=False):
    """The bright pass's (tf1, tf2) from the layer's two pictures: layer 1 reduced and twice through the radial
    blur about c31.xy; layer 2 reduced. exchanged: the other assignment of the two."""
    beams, flares = reduce_rows(layer1), reduce_rows(layer2)
    bh, bw = len(beams), len(beams[0])
    for _ in range(2):
        src = lf.post_texture(beams)
        tex = lambda u, v: src(u * bw + 0.5, v * bh + 0.5)             # noqa: E731  (the fetch's half texel)
        beams = [[[pr.q10(c) for c in lf.post_radial(tex, x / bw, y / bh, c31, c46x, c47x)] for x in range(bw)] for y in range(bh)]
    return (flares, beams) if exchanged else (beams, flares)


def bloom_picture(scene, w, h, fr, bias=0.0, sun=None):
    """post_replay.bloom_picture with the bright pass's two other inputs: sun = (tf1 rows, tf2 rows) at a
    quarter of the scene's size, or None for none (variant B's bloom)."""
    if sun is None:
        return pr.bloom_picture(scene, w, h, fr, bias)
    if w % 4 or h % 4:
        raise ReplayError("the scene's size %d x %d is no multiple of 4" % (w, h))
    bw, bh = w // 4, h // 4
    tf1, tf2 = sun
    for rows in (tf1, tf2):
        if len(rows) != bh or any(len(r) != bw for r in rows):
            raise ReplayError("a sun input is not %d x %d" % (bw, bh))
    tex = pr.flat_texture(scene, w, h, True)
    bright = fr.bright()
    stage = [[[pr.q10(v) for v in lf.post_bright(lf.post_downsample(tex, x, y, bias), tf1[y][x], tf2[y][x], *bright)]
              for x in range(bw)] for y in range(bh)]
    for role, vertical in (("blur-x", False), ("blur-y", True)):
        src, weights = lf.post_texture(stage), fr.weights(role)
        stage = [[[pr.q10(v) for v in lf.post_blur13(src, x, y, weights, vertical)] for x in range(bw)] for y in range(bh)]
    return bw, bh, stage


# --- the commands --------------------------------------------------------------------------------------------

def _texture_of(fr):
    cache = {}

    def texture(d, slot):
        fc = d.fc[slot]
        t = fr.texture(fc[1] & 0xFFFFF000, d.tfc.get(slot))
        if t.path not in cache:
            w, h, rgba = decode_rgba(t.read(), fc)
            cache[t.path] = MipTexture(w, h, rgba, wrap=((fc[0] >> 10) & 7) == 0)
        return cache[t.path]

    return texture


def _scene_depth(fr, draws):
    first = next((d for d in draws if d.role == "layer-depth"), None)
    if first is None:
        raise ReplayError("frame %d: no layer depth draw (5D67 at pitch %d)" % (fr.number, LAYER_PITCH))
    slot = min(first.fc)
    if first.tfc.get(slot) is None:
        raise ReplayError("frame %d: the layer's depth draw names no depth content (not a read-back capture)" % fr.number)
    t = fr.texture(first.fc[slot][1] & 0xFFFFF000, first.tfc[slot])
    w, h, values = fog_tables.decode_depth(t.read(), first.fc[slot])
    if (w, h) != (pr.SCENE_W, pr.SCENE_H):
        raise ReplayError("the scene depth is %d x %d" % (w, h))
    return values


def _rows_png(path, rows, gain=1.0):
    h, w = len(rows), len(rows[0])
    rgba = bytearray(b"\xff" * (w * h * 4))
    at = 0
    for row in rows:
        for t in row:
            rgba[at:at + 3] = bytes(lf.post_ramp_index(lf.sat(v * gain)) for v in t)
            at += 4
    write_png(path, w, h, bytes(rgba), max_edge=w)


def _stats(rows):
    flat = [v for row in rows for t in row for v in t]
    lit = sum(1 for row in rows for t in row if max(t) > 0.0)
    return "mean %.5f, largest %.4f, %d of %d texels above 0" % (sum(flat) / len(flat), max(flat), lit, len(flat) // 3)


def describe(d):
    regs = {s.kind: s.registers for s in lf.POST_QUAD_SHADERS}.get(d.role, ())
    return "%-14s ps %s vs %s pitch %4d %s | depth 0x%08X blend 0x%08X mask %X alpha test %s | %d vertices, indices %s | %s" % (
        d.role, d.ps[:4], d.vs[:4], d.pitch, "scene" if d.in_scene else d.where, d.depth_word, d.blend_word, d.colour_mask,
        "above %.4f" % d.alpha_ref if d.alpha_test else "off", len(d.vertices), "".join(str(i) for i in d.indices[:12]),
        " ".join("c%d=(%s)" % (r, ", ".join("%.4g" % x for x in d.reg(r))) for r in regs))


def cmd_quads(args):
    path = pr.find_capture(args.capture)
    draws, other = load_layer_draws(path, args.frame)
    print("%s frame %d: the furniture's quads" % (path.name, args.frame))
    for d in scene_quads(path, args.frame) + draws:
        print(describe(d))
        print("      " + " ".join("(" + ", ".join("%.5g" % x for x in v) + ")" for v in d.vertices[:4]))
    for word, n in sorted(other.items()):
        print("%d %s" % (n, word))
    return 0


def cmd_replay(args):
    path = pr.find_capture(args.capture)
    fr = pr.load_frame(path, args.frame)
    if "radial" not in fr.regs or any(r not in fr.regs["radial"] for r in (31, 46, 47)):
        raise ReplayError("frame %d: no radial blur row with its registers" % args.frame)
    pr._head(fr)
    c31, c46x, c47x = fr.regs["radial"][31][:2], fr.reg("radial", 46), fr.reg("radial", 47)
    print("radial blur c31.xy (%.4f, %.4f) c46.x %.4f c47.x %.4f; sun roles: %s" % (
        c31[0], c31[1], c46x, c47x, "tf1 = the blurred beams, tf2 = layer 2 (the research's reading by the weights: inference)"
        if args.sun_roles == "research" else "EXCHANGED: tf1 = layer 2, tf2 = the blurred beams"))
    draws, other = load_layer_draws(path, args.frame)
    one, two, lines = sun_layer(draws, _scene_depth(fr, draws), pr.SCENE_W, pr.SCENE_H, _texture_of(fr))
    print()
    print("the sun layer, %d x %d, %d draws:" % (pr.SCENE_W // 2, pr.SCENE_H // 2, len(draws)))
    for line in lines:
        print("  " + line)
    for word, n in sorted(other.items()):
        print("  %d %s" % (n, word))
    tf1, tf2 = sun_inputs(one, two, c31, c46x, c47x, exchanged=args.sun_roles != "research")
    print("layer 1: %s" % _stats(one))
    print("layer 2: %s" % _stats(two))
    print("tf1    : %s" % _stats(tf1))
    print("tf2    : %s" % _stats(tf2))
    scene = pr.load_scene(fr)
    front = pr.load_front(fr)
    spans = pr.outside_hud(pr.SCENE_W, pr.SCENE_H)
    pictures, stats = {}, {}
    for name, sun in (("B", None), ("C", (tf1, tf2))):
        bw, bh, rows = bloom_picture(scene, pr.SCENE_W, pr.SCENE_H, fr, args.bias, sun)
        sampler = pr.bloom_sampler(bw, bh, rows, pr.SCENE_W, pr.SCENE_H)
        pictures[name] = pr.variant_picture("B", scene, pr.SCENE_W, pr.SCENE_H, fr, sampler)
        s = stats[name] = pr.difference_stats(pictures[name], front, pr.SCENE_W, pr.SCENE_H, spans)
        print()
        print("variant %s against the front buffer 0x%08X, whole picture outside the HUD: |d| in 8-bit levels: median %.2f, "
              "mean %.2f, 90%% %.2f, 99%% %.2f; share within 4 levels %.1f%%, within 8 %.1f%%" % (
                  name, pr.FRONT_BASE, s["median"], s["mean"], s["p90"], s["p99"], 100 * s["within"][4], 100 * s["within"][8]))
    gain = 100 * (stats["C"]["within"][8] - stats["B"]["within"][8])
    print()
    print("criterion 18, first part: the share within 8 levels is %+.1f points with the sun layer (%.1f%% against %.1f%%); "
          "asked: at least +10: %s" % (gain, 100 * stats["C"]["within"][8], 100 * stats["B"]["within"][8],
                                       "held" if gain >= 10.0 else "NOT held"))
    regions = [("the quarter about the blur's centre (this tool's box)",
                [max(c31[0] - 0.125, 0.0), max(c31[1] - 0.125, 0.0), min(c31[0] + 0.125, 1.0), min(c31[1] + 0.125, 1.0)], "tool")]
    for name in args.region_file or []:
        doc = json.loads((pr.REPO / "docs" / "native-renderer" / (name + ".json")).read_text(encoding="utf-8"))
        regions += [(r["name"], r["rect"], r.get("kind", "")) for r in doc.get("spots", {}).get(args.spot, {}).get("regions", [])]
    print()
    print("%-52s | front lum | B ratio  C ratio" % "region (mean luminance of the display values)")
    for name, rect, kind in regions:
        f = lf.post_lum(pr.region_mean(front, pr.SCENE_W, pr.SCENE_H, rect))
        ratios = [lf.post_lum(pr.region_mean(pictures[v], pr.SCENE_W, pr.SCENE_H, rect)) / f if f else float("nan") for v in "BC"]
        print("%-52s | %9.3f | %7.3f  %7.3f" % (name[:52], f, ratios[0], ratios[1]))
    if not args.region_file:
        print("criterion 18, second part (the regions \"sun\" and \"beams\" within 15%): docs/native-renderer/post-regions.json "
              "is plan Task 10's and is not given here (--region-file post-regions --spot <its spot>)")
    if args.png:
        for name in ("B", "C"):
            write_png("%s_%s.png" % (args.png, name), pr.SCENE_W, pr.SCENE_H, pr.to_bytes(pictures[name]), max_edge=pr.SCENE_W)
        write_png("%s_final.png" % args.png, pr.SCENE_W, pr.SCENE_H, pr.to_bytes(front), max_edge=pr.SCENE_W)
        _rows_png("%s_layer1_x4.png" % args.png, one, 4.0)
        _rows_png("%s_layer2.png" % args.png, two)
        _rows_png("%s_tf1.png" % args.png, tf1)
        _rows_png("%s_tf2.png" % args.png, tf2)
        print("wrote %s_{B,C,final,layer1_x4,layer2,tf1,tf2}.png (no ramp; layer 1 times 4)" % args.png)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("replay")
    p.add_argument("capture")
    p.add_argument("frame", type=int)
    p.add_argument("--bias", type=float, default=0.0, help="the downsample's pixel position: 0 integral, 0.5 the centre")
    p.add_argument("--sun-roles", choices=("research", "exchanged"), default="research")
    p.add_argument("--region-file", action="append", metavar="NAME", help="a region file of docs/native-renderer, without .json")
    p.add_argument("--spot", default="sunward")
    p.add_argument("--png", metavar="PREFIX")
    p.set_defaults(run=cmd_replay)
    p = sub.add_parser("quads")
    p.add_argument("capture")
    p.add_argument("frame", type=int)
    p.set_defaults(run=cmd_quads)
    args = ap.parse_args(argv)
    try:
        return args.run(args)
    except (ReplayError, OSError, ValueError, KeyError) as e:
        print("post_sun_layer: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
