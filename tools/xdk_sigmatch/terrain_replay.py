"""Replay the terrain shaders' unlit colour from a discovery capture (frame-map section 13, "Terrain").

Usage: terrain_replay.py <capture.jsonl> --frame N --list
       terrain_replay.py <capture.jsonl> --frame N --patch K --out image.png [--res 64] [--no-fade]
                         [--no-tint] [--no-base] [--encode sqrt|linear] [--exposure X]
       terrain_replay.py <capture.jsonl> --frame N --weights image.png

The game draws the ground near the eye in blended layers (builder 8221C9C8: one draw per 8 x 8
square of ground and layer) over an underlay drawn first by the adaptive builder (82207C30).
--list prints the squares of a frame with their layer draws in capture order. --patch K replays
square K: the underlay if a colour draw of the adaptive builder covers the square, then every
layer draw with its captured blend, on a res x res pixel grid, and writes the picture. --weights
writes the layer weights of every square of the frame as one map (16 pixels per square, one
colour per detail texture, magenta where no layer covers a pixel).

What is evaluated (the shaders' colour with the lighting factor 1 and the additive light, sun and
fog terms dropped):
  layer   colour = tf0(detail uv)^2 * base',  base' = pow(tf3(base uv), c21.w) * c21.xyz
          alpha  = (1 - sat(d2 * c31.x + c31.y)) * weight,  weight = 15/14 * (tf18 - 1/15)
          not drawn where tf18 < c114.x (the vertex shader writes a NaN position there)
  far     colour = tf13(u, v)^2 * base', u and v from the tables tf18 and tf19 of the adaptive
          vertex shader; shader 2F8A times sat(d2 * c31.x + c31.y)
Limits, by design: the weight is sampled per pixel with a bilinear filter, which is the game's
per-vertex weight at 16 segments per patch edge (the game tessellates less far away); the three
layer shaders paired with vertex shader C30A blend tf13 into the detail colour on steep ground,
which is not evaluated (--list marks those draws); base levels only, no mip maps; a negative alpha
is clamped to 0.

Needs, per tess row: "consts" with c8, c11, c14, c46, c47, c113, c114, c115 (far: c72 too),
"pconst" (pixel constants c0..c31: c19, c21, c31), "tf" with the slots the shaders sample, and the
"texture" rows and dump files of those textures. --list and --weights need neither "pconst" nor
c14. A capture taken before these were recorded is refused with the list of what it lacks.
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import texture_thumb as tt  # noqa: E402

# c253.w and c253.y of the layer vertex shaders: shader literals, zero in the device bank. Values
# from the GPU's register file ([vtess] log, frame-map section 13 "Terrain"): the weight of the
# 4-bit texel value n is (n - 1) / 14.
WEIGHT_SCALE = 15.0 / 14.0
WEIGHT_BIAS = 1.0 / 15.0
FAR_ROW_BIAS = 0.5  # c254.x of the adaptive vertex shader (same source)

BLEND_OPAQUE = ("one", "zero")
BLEND_REPLACE = ("src_alpha", "zero")
BLEND_ADD = ("src_alpha", "one")
_BLENDS = {0x00010001: BLEND_OPAQUE, 0x00060006: BLEND_REPLACE, 0x01060106: BLEND_ADD}

LAYER_BUILDER = "DrawIndx:8221C9C8"
FAR_BUILDER = "DrawIndx:82207C30"
FADING_FAR = "0x2F8A0341E8C09698"
CLIFF_SHADERS = ("0x9C2144E42E95A76F", "0x45243B758F9272AB", "0x04F23292B2F44077")
COLOUR_BINS = 0x2AAAAAAA
UNCOVERED = (255, 0, 255)
_PALETTE = [(60, 160, 60), (150, 110, 60), (200, 190, 150), (110, 110, 120), (40, 100, 40), (190, 150, 90),
            (90, 70, 50), (160, 200, 120), (220, 220, 220), (70, 130, 160), (130, 60, 60), (200, 120, 40)]


class MissingInput(Exception):
    """The capture does not hold something the replay needs."""


class Tex:
    """A decoded base level: `texels` are row-major tuples of four floats in 0..1."""

    def __init__(self, w, h, texels, clamp=(0, 0), linear=True):
        self.w, self.h, self.texels, self.clamp, self.linear = w, h, texels, clamp, linear

    def _index(self, i, n, mode):
        if mode == 0:
            return i % n
        if mode == 1:  # mirrored repeat
            i %= 2 * n
            return i if i < n else 2 * n - 1 - i
        return min(max(i, 0), n - 1)

    def texel(self, x, y):
        return self.texels[self._index(y, self.h, self.clamp[1]) * self.w + self._index(x, self.w, self.clamp[0])]

    def sample(self, u, v, linear=None):
        linear = self.linear if linear is None else linear
        if not linear:
            return self.texel(math.floor(u * self.w), math.floor(v * self.h))
        x, y = u * self.w - 0.5, v * self.h - 0.5
        x0, y0 = math.floor(x), math.floor(y)
        fx, fy = x - x0, y - y0
        a, b = self.texel(x0, y0), self.texel(x0 + 1, y0)
        c, d = self.texel(x0, y0 + 1), self.texel(x0 + 1, y0 + 1)
        return tuple((a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy for k in range(4))


def decode_texture(data, fc):
    """Dump bytes and six fetch dwords -> Tex. DXT1, DXT2/3, DXT4/5, 8888 as RGBA; k_DXT3A (58) with
    the value in every channel (the SDK's view of that format); k_16 (24) with the value in x.
    Filter and clamp modes come from the fetch constant. Others raise ValueError("format N")."""
    fmt = fc[1] & 0x3F
    clamp = ((fc[0] >> 10) & 7, (fc[0] >> 13) & 7)
    linear = ((fc[3] >> 19) & 3) == 1
    if fmt == 24:
        endian = (fc[1] >> 6) & 3
        w = (fc[2] & 0x1FFF) + 1
        h = ((fc[2] >> 13) & 0x1FFF) + 1
        pitch = max(((fc[0] >> 22) & 0x1FF) << 5, w)
        tiled = (fc[0] >> 31) != 0
        row_pitch = ((pitch + 31) & ~31) * 2
        texels = []
        for y in range(h):
            for x in range(w):
                o = tt.tiled_offset_2d(x, y, pitch, 1) if tiled else y * row_pitch + x * 2
                raw = data[o:o + 2]
                raw = tt._swap(raw + bytes(2 - len(raw)), endian)
                texels.append((struct.unpack("<H", raw)[0] / 65535.0, 0.0, 0.0, 1.0))
        return Tex(w, h, texels, clamp, linear)
    if fmt == 58:
        w, h, r8 = tt.decode_r8(data, fc)
        return Tex(w, h, [(v / 255.0,) * 4 for v in r8], clamp, linear)
    w, h, rgba = tt.decode_rgba(data, fc)
    return Tex(w, h, [tuple(c / 255.0 for c in rgba[i:i + 4]) for i in range(0, len(rgba), 4)], clamp, linear)


_NO_PCONST = 'no "pconst" (pixel constants c0..c31)'


def _need(consts, names, pconst=()):
    """Raises MissingInput naming every vertex constant of `names` the row lacks, and the pixel
    constants when `pconst` is given as None or too short (() = not needed)."""
    missing = ["vertex constant " + n for n in names if not consts or n not in consts]
    if pconst != () and (not pconst or len(pconst) < 128):
        missing.append(_NO_PCONST)
    if missing:
        raise MissingInput("the tess row has " + ", ".join("no " + m if not m.startswith("no ") else m for m in missing)
                           + ": the capture was taken before terrain rows recorded them")


def _pc(pconst, reg):
    return pconst[4 * reg:4 * reg + 4]


def layer_inputs(consts, pconst):
    """The numbers MakeTerrainMaterialInputs (terrain_patch.h) makes for a layer draw, from a tess
    row's vertex constants and pixel constants. Coordinates are u = x * [0] + [2], v = y * [1] + [3]
    of the world position."""
    _need(consts, ("c8", "c14", "c46", "c47", "c113", "c114", "c115"), pconst)
    c8, c14, c46, c47, c113, c115 = (consts[k] for k in ("c8", "c14", "c46", "c47", "c113", "c115"))
    c19, c21, c31 = _pc(pconst, 19), _pc(pconst, 21), _pc(pconst, 31)
    sx, sy = c115[0] / c46[0], c115[1] / c46[1]
    world = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]
    if all(k in consts for k in ("c4", "c5", "c6")):
        world = list(consts["c4"]) + list(consts["c5"]) + list(consts["c6"])
    return {
        "detail_uv": [c8[1], -c8[1], 0.0, 0.0],
        "base_uv": [c14[0], c14[1], -c47[2] * c14[0], -c47[3] * c14[1]],
        "weight_uv": [sx, sy, c115[2] - c113[0] * sx, c115[3] - c113[1] * sy],
        "base_pow": c21[3],
        "base_tint": list(c21[:3]),
        "weight_scale": WEIGHT_SCALE,
        "weight_bias": WEIGHT_BIAS,
        "fade": list(c31[:2]),
        "hole_ref": consts["c114"][0],
        "eye": list(c19[:3]),
        "world": world,
    }


def far_inputs(consts, pconst):
    """The same for a draw of the adaptive builder: the far colour is addressed through two tables."""
    _need(consts, ("c8", "c14", "c46", "c47", "c72", "c114", "c115"), pconst)
    c14, c46, c72, c114, c115 = (consts[k] for k in ("c14", "c46", "c72", "c114", "c115"))
    c19, c21, c31 = _pc(pconst, 19), _pc(pconst, 21), _pc(pconst, 31)
    return {
        "base_uv": [c14[0], c14[1], -c46[2] * c14[0], -c46[3] * c14[1]],
        "far_table": [c72[0], c72[1], c114[0], c114[1]],
        "far_patch": [c115[0], c115[1]],
        "origin": list(consts["c47"][:2]),
        "cell": list(consts["c8"][:2]),
        "base_pow": c21[3],
        "base_tint": list(c21[:3]),
        "fade": list(c31[:2]),
        "eye": list(c19[:3]),
        "world": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0],
    }


def patch_square(consts, patch, far):
    """(x0, y0, width, height) of patch `patch` of a draw: the vertex shaders' row = floor(p / cols),
    col = p - row * cols, corner = origin + (col, row) * cell. Far draws add c113.x to the index."""
    cols, inv = consts["c11"][0], consts["c11"][1]
    if far:
        p = patch + consts["c113"][0]
        origin, cell = consts["c47"], consts["c8"]
    else:
        p = patch
        origin, cell = consts["c113"], consts["c46"]
    row = math.floor(p * inv + 1e-6)
    col = p - row * cols
    return (float(origin[0] + col * cell[0]), float(origin[1] + row * cell[1]), float(cell[0]), float(cell[1]))


def apply_uv(xf, wx, wy):
    return (wx * xf[0] + xf[2], wy * xf[1] + xf[3])


def layer_weight(inp, value):
    return inp["weight_scale"] * (value - inp["weight_bias"])


def layer_culled(inp, value):
    return value < inp["hole_ref"]


def _sat(x):
    return min(max(x, 0.0), 1.0)


def fade(inp, d2):
    """The layers' fade with the squared distance from the eye."""
    return 1.0 - _sat(d2 * inp["fade"][0] + inp["fade"][1])


def eye_distance2(inp, wx, wy, h):
    w = inp["world"]
    p = [w[4 * r] * wx + w[4 * r + 1] * wy + w[4 * r + 2] * h + w[4 * r + 3] for r in range(3)]
    return sum((e - q) ** 2 for e, q in zip(inp["eye"], p))


class Layer:
    def __init__(self, inp, detail, base, weight, blend):
        self.inp, self.detail, self.base, self.weight, self.blend = inp, detail, base, weight, blend


class Far:
    def __init__(self, inp, colour, base, table_u, table_v, fade_in):
        self.inp, self.colour, self.base, self.table_u, self.table_v, self.fade_in = inp, colour, base, table_u, table_v, fade_in


def _base_term(inp, base_tex, wx, wy, tint, base):
    if not base or base_tex is None:
        return (1.0, 1.0, 1.0)
    b = base_tex.sample(*apply_uv(inp["base_uv"], wx, wy))
    return tuple(abs(b[k]) ** inp["base_pow"] * (inp["base_tint"][k] if tint else 1.0) for k in range(3))


def layer_colour(layer, wx, wy, tint=True, base=True):
    d = layer.detail.sample(*apply_uv(layer.inp["detail_uv"], wx, wy))
    bp = _base_term(layer.inp, layer.base, wx, wy, tint, base)
    return tuple(d[k] * d[k] * bp[k] for k in range(3))


def _blend(dst, src, alpha, blend):
    s = alpha if blend[0] == "src_alpha" else 1.0
    k = 1.0 if blend[1] == "one" else 0.0
    return tuple(src[i] * s + dst[i] * k for i in range(3))


def shade_point(layers, wx, wy, h, fade_on=True, background=(0.0, 0.0, 0.0), tint=True, base=True):
    """The layers of one point in draw order over `background`. Returns (rgb, layers drawn)."""
    rgb = tuple(float(c) for c in background)
    drawn = 0
    for layer in layers:
        value = layer.weight.sample(*apply_uv(layer.inp["weight_uv"], wx, wy), linear=True)[3]
        if layer_culled(layer.inp, value):
            continue
        drawn += 1
        alpha = layer_weight(layer.inp, value)
        if fade_on and h is not None:
            alpha *= fade(layer.inp, eye_distance2(layer.inp, wx, wy, h))
        rgb = _blend(rgb, layer_colour(layer, wx, wy, tint, base), _sat(alpha), layer.blend)
    return rgb, drawn


def far_knot_coord(inp, col, row, k):
    """Table coordinate of knot k (0..16) of patch column `col`, row `row` (frame-map: the adaptive
    vertex shader's tf18 / tf19 addressing; the same expression serves both with U or V)."""
    t = inp["far_table"]
    return ((inp["far_patch"][0] + col + (k / 16.0) * t[0] + t[1]) * t[2],
            (inp["far_patch"][1] + row + FAR_ROW_BIAS) * t[3])


def far_uv(far, wx, wy):
    """The far colour coordinate at a world position: the two tables, read at the 17 knots of the
    patch as the vertex shader reads them at its vertices, linear in between."""
    inp = far.inp
    px, py = (wx - inp["origin"][0]) / inp["cell"][0], (wy - inp["origin"][1]) / inp["cell"][1]
    col, row = math.floor(px), math.floor(py)
    out = []
    for table, t in ((far.table_u, px - col), (far.table_v, py - row)):
        k = min(int(t * 16.0), 15)
        f = t * 16.0 - k
        a = table.sample(*far_knot_coord(inp, col, row, k), linear=False)[0]
        b = table.sample(*far_knot_coord(inp, col, row, k + 1), linear=False)[0]
        out.append(a * (1.0 - f) + b * f)
    return tuple(out)


def far_colour(far, wx, wy, h, tint=True, base=True):
    c = far.colour.sample(*far_uv(far, wx, wy))
    bp = _base_term(far.inp, far.base, wx, wy, tint, base)
    k = 1.0
    if far.fade_in and h is not None:
        k = _sat(eye_distance2(far.inp, wx, wy, h) * far.inp["fade"][0] + far.inp["fade"][1])
    return tuple(c[i] * c[i] * bp[i] * k for i in range(3))


def blend_of(word):
    v = int(word, 16) if isinstance(word, str) else int(word)
    if v not in _BLENDS:
        raise ValueError(f"blend word 0x{v:08X} is not one the terrain draws use")
    return _BLENDS[v]


# --- captures ---------------------------------------------------------------------------------

def read_capture(path, frame):
    """(main-scene tess rows of `frame` in capture order, {fetch dwords tuple: dump path})."""
    root = Path(path).parent
    rows, textures = [], {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            head = line[:24]
            if '"texture"' in head:
                r = json.loads(line)
                textures[tuple(int(x, 16) for x in r["fc"])] = root / r["file"]
            elif '"tess"' in head:
                r = json.loads(line)
                if r.get("frame") == frame and r.get("in_scene"):
                    rows.append(r)
    return rows, textures


def squares_of(rows):
    """Layer draws by ground square, in first-appearance order: [((x0, y0, w, h), [rows])]."""
    out = {}
    for r in rows:
        if r.get("func") != LAYER_BUILDER:
            continue
        first, count = int(r["args"][1], 16), int(r["args"][2], 16)
        for p in range(first, first + count):
            out.setdefault(patch_square(r["consts"], p, far=False), []).append(r)
    return list(out.items())


def underlay_of(rows, square):
    """The adaptive draw whose colour pass covers the square, or None."""
    for r in rows:
        if r.get("func") != FAR_BUILDER:
            continue
        if not int(r.get("rs", {}).get("bin_mask_lo", "0xFFFFFFFF"), 16) & COLOUR_BINS:
            continue  # executed in the depth pre-pass only
        for p in range(int(r["args"][3], 16)):
            x0, y0, w, h = patch_square(r["consts"], p, far=True)
            if x0 <= square[0] < x0 + w and y0 <= square[1] < y0 + h:
                return r
    return None


class _Textures:
    def __init__(self, index):
        self.index, self.cache = index, {}

    def get(self, row, slot, what):
        fc = (row.get("tf") or {}).get(str(slot))
        if not fc:
            raise MissingInput(f'the tess row has no fetch constant tf{slot} ({what}): the capture was taken before '
                               "terrain rows listed the textures their shaders sample")
        key = tuple(int(x, 16) for x in fc)
        if key not in self.cache:
            if key not in self.index:
                raise MissingInput(f"no texture dump for tf{slot} ({what}), fetch dword 1 0x{key[1]:08X}")
            self.cache[key] = decode_texture(Path(self.index[key]).read_bytes(), list(key))
        return self.cache[key]


def _height(tex, row, far, wx, wy):
    """The vertex shaders' height: the heightmap point-sampled at (world - offset) * scale."""
    c = row["consts"]
    scale, off, hs = (c["c46"], c["c46"][2:], c["c8"][2]) if far else (c["c47"], c["c47"][2:], c["c46"][2])
    return tex.sample((wx - off[0]) * scale[0], (wy - off[1]) * scale[1], linear=False)[0] * hs


def _encode(rgb, mode, exposure):
    out = []
    for c in rgb:
        c = min(max(c * exposure, 0.0), 1.0)
        out.append(round(255 * (math.sqrt(c) if mode == "sqrt" else c)))
    return bytes(out + [255])


def _replay(a, rows, textures, lines):
    squares = squares_of(rows)
    if not 0 <= a.patch < len(squares):
        raise MissingInput(f"frame {a.frame} has {len(squares)} squares with layer draws; --patch {a.patch} is not one")
    square, draws = squares[a.patch]
    tex = _Textures(textures)
    layers, problems = [], []
    for r in draws:
        try:
            layers.append(Layer(layer_inputs(r["consts"], r.get("pconst")), tex.get(r, 0, "detail colour"),
                                tex.get(r, 3, "base map"), tex.get(r, 18, "layer weights"), blend_of(r["rs"]["2201"])))
        except MissingInput as e:
            problems.append(str(e))
    under = underlay_of(rows, square)
    far = None
    if under is not None:
        try:
            far = Far(far_inputs(under["consts"], under.get("pconst")), tex.get(under, 13, "far colour"),
                      tex.get(under, 3, "base map"), tex.get(under, 18, "far u table"), tex.get(under, 19, "far v table"),
                      under.get("ps_hash") == FADING_FAR)
        except MissingInput as e:
            problems.append(str(e))
    if problems:
        raise MissingInput("; ".join(sorted(set(problems))))
    height = None
    if not a.no_fade:
        height = tex.get(draws[0], 16, "heightmap")
    cliff = sum(r.get("ps_hash") in CLIFF_SHADERS for r in draws)
    lines.append(f"square ({square[0]:g}, {square[1]:g}) {square[2]:g} x {square[3]:g}: {len(draws)} layer draws"
                 f" ({cliff} of them with the tf13 blend on steep ground, not evaluated), underlay "
                 + (f"{under['ps_hash']}" if under is not None else "none"))
    n = a.res
    rgba = bytearray()
    for j in range(n):
        for i in range(n):
            wx = square[0] + square[2] * (i + 0.5) / n
            wy = square[1] + square[3] * (j + 0.5) / n
            h = _height(height, draws[0], False, wx, wy) if height is not None else None
            bg = far_colour(far, wx, wy, h, not a.no_tint, not a.no_base) if far is not None else (0.0, 0.0, 0.0)
            rgb, _ = shade_point(layers, wx, wy, h, not a.no_fade, bg, not a.no_tint, not a.no_base)
            rgba += _encode(rgb, a.encode, a.exposure)
    tt.write_png(a.out, n, n, bytes(rgba), max_edge=n)
    lines.append(f"wrote {a.out} ({n} x {n})")


def _list(rows, lines):
    for k, (square, draws) in enumerate(squares_of(rows)):
        under = underlay_of(rows, square)
        lines.append(f"patch {k}: square ({square[0]:g}, {square[1]:g}) {len(draws)} layers, underlay "
                     + (under["ps_hash"][:8] if under is not None else "none"))
        for r in draws:
            c = r["consts"]
            tf0 = (r.get("tf") or {}).get("0")
            lines.append(f"    ps {r['ps_hash'][:8]}{' (tf13 blend on steep ground)' if r['ps_hash'] in CLIFF_SHADERS else ''}"
                         f" blend {r['rs']['2201']} c114.x {c['c114'][0]:g} detail scale {c['c8'][1]:g}"
                         f" weight cell ({c['c115'][2]:.5f}, {c['c115'][3]:.5f}) detail "
                         + (tf0[1] if tf0 else "not recorded"))


def _weights(a, rows, textures, lines):
    squares = squares_of(rows)
    if not squares:
        raise MissingInput(f"frame {a.frame} has no layer draws")
    tex = _Textures(textures)
    xs = [s[0] for s, _ in squares]
    ys = [s[1] for s, _ in squares]
    cw, ch = squares[0][0][2], squares[0][0][3]
    cols = int(round((max(xs) - min(xs)) / cw)) + 1
    rows_n = int(round((max(ys) - min(ys)) / ch)) + 1
    img = bytearray(bytes([0, 0, 0, 255]) * (cols * 16 * rows_n * 16))
    palette = {}
    for square, draws in squares:
        ox = int(round((square[0] - min(xs)) / cw)) * 16
        oy = int(round((square[1] - min(ys)) / ch)) * 16
        prepared = []
        for r in draws:
            c = r["consts"]
            _need(c, ("c46", "c113", "c114", "c115"))
            sx, sy = c["c115"][0] / c["c46"][0], c["c115"][1] / c["c46"][1]
            xf = [sx, sy, c["c115"][2] - c["c113"][0] * sx, c["c115"][3] - c["c113"][1] * sy]
            tf0 = (r.get("tf") or {}).get("0")
            key = tf0[1] if tf0 else r.get("ps", {}).get("obj", r["ps_hash"])
            colour = palette.setdefault(key, _PALETTE[len(palette) % len(_PALETTE)])
            prepared.append((xf, c["c114"][0], tex.get(r, 18, "layer weights"), colour))
        for j in range(16):
            for i in range(16):
                wx = square[0] + cw * (i + 0.5) / 16
                wy = square[1] + ch * (j + 0.5) / 16
                acc, total = [0.0, 0.0, 0.0], 0.0
                for xf, ref, wt, colour in prepared:
                    value = wt.sample(*apply_uv(xf, wx, wy), linear=True)[3]
                    if value < ref:
                        continue
                    w = _sat(WEIGHT_SCALE * (value - WEIGHT_BIAS))
                    total += w
                    for k in range(3):
                        acc[k] += w * colour[k]
                at = ((oy + j) * cols * 16 + ox + i) * 4
                img[at:at + 3] = bytes(UNCOVERED) if total <= 0.0 else bytes(min(255, round(v)) for v in acc)
    tt.write_png(a.weights, cols * 16, rows_n * 16, bytes(img), max_edge=max(cols, rows_n) * 16)
    lines.append(f"wrote {a.weights}: {len(squares)} squares, {cols * 16} x {rows_n * 16}, x {min(xs):g}..{max(xs) + cw:g}"
                 f" y {min(ys):g}..{max(ys) + ch:g} (y grows downwards in the picture)")
    for key, colour in palette.items():
        lines.append(f"    detail {key}: colour {colour}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--frame", type=int, required=True)
    ap.add_argument("--list", action="store_true", help="print the frame's squares and their layer draws")
    ap.add_argument("--patch", type=int, help="replay this square (number from --list)")
    ap.add_argument("--out", help="picture of the replayed square")
    ap.add_argument("--res", type=int, default=64)
    ap.add_argument("--no-fade", action="store_true", help="alpha is the weight alone (no distance fade)")
    ap.add_argument("--no-tint", action="store_true", help="base' without c21.xyz")
    ap.add_argument("--no-base", action="store_true", help="the detail colour alone")
    ap.add_argument("--encode", choices=("sqrt", "linear"), default="sqrt",
                    help="sqrt (default) undoes the shaders' squaring of the detail colour for viewing")
    ap.add_argument("--exposure", type=float, default=1.0)
    ap.add_argument("--weights", help="write the frame's layer weight map")
    a = ap.parse_args(argv)
    rows, textures = read_capture(a.capture, a.frame)
    lines = []
    if a.list:
        _list(rows, lines)
    if a.weights:
        _weights(a, rows, textures, lines)
    if a.patch is not None:
        if not a.out:
            ap.error("--patch needs --out")
        _replay(a, rows, textures, lines)
    return lines


if __name__ == "__main__":
    try:
        for text in main():
            print(text)
    except MissingInput as e:
        print(f"cannot replay: {e}", file=sys.stderr)
        sys.exit(2)
