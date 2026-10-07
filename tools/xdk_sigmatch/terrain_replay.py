"""Replay the terrain shaders' unlit colour from a discovery capture (frame-map section 13, "Terrain").

Usage: terrain_replay.py <capture.jsonl> --frame N --list
       terrain_replay.py <capture.jsonl> --frame N --patch K --out image.png [--res 64] [--no-fade]
                         [--no-tint] [--no-base] [--no-steep] [--encode sqrt|linear] [--exposure X]
       terrain_replay.py <capture.jsonl> --frame N --patch K --locate
       terrain_replay.py <capture.jsonl> --frame N --weights image.png
       terrain_replay.py <capture.jsonl> --frame N --weight-sums
       terrain_replay.py <capture.jsonl> --frame N --check-dumps
       terrain_replay.py <capture.jsonl> --frame N --steep-share

The game draws the ground near the eye in blended layers (builder 8221C9C8: one draw per 8 x 8
square of ground and layer) over an underlay drawn first by the adaptive builder (82207C30).
--list prints the squares of a frame with their layer draws in capture order. --patch K replays
square K: the underlay if a colour draw of the adaptive builder covers the square, then every
layer draw with its captured blend, on a res x res pixel grid, and writes the picture. --weights
writes the layer weights of every square of the frame as one map (16 pixels per square, one
colour per detail texture, magenta where no layer covers a pixel) and counts the texel pairs where
a 0 lies beside a value of 2 or more. --weight-sums prints how the layer weights of a grid point
add up over the frame's squares. --patch K --locate prints where the square's corners are in the
1120 x 720 frame (the row's c0..c3). --check-dumps lists every texture the frame's tess rows name
with the size of its dump against the size of its base level (a dump cut short by the capture
shows as SHORT). Only textures the replay reads count; the others are listed as
not needed by the replay. --steep-share prints how much of the frame's layered ground is steep
enough for the steep-ground block: the slope value s at the 17 x 17 grid points of every square,
from the normal map.

What is evaluated (the shaders' colour with the lighting factor 1 and the additive light, sun and
fog terms dropped):
  layer   colour = A * base',  A = tf0(detail uv)^2,  base' = pow(tf3(base uv), c21.w) * c21.xyz
          the three layer shaders paired with vertex shader C30A (CLIFF_SHADERS), on steep ground:
          A = s * tf0^2 + (1 - s) * (wy * tf13(o1)^2 + wx * tf13(o0)^2) where s < threshold, with
          s = sat(|n.z| * c45.y + c45.x), wx, wy = |n.x|, |n.y| / (|n.x| + |n.y| + small term),
          o0 = (c8.x * y, -c8.x * height), o1 = (c8.x * x, -c8.x * height), n the vertex shader's
          normal: tf17 at the height coordinate, * 2 - 1, z = sqrt(|1 - x^2 - y^2|)
          alpha  = (1 - sat(d2 * c31.x + c31.y)) * weight,  weight = 15/14 * (tf18 - 1/15)
          not drawn where tf18 < c114.x (the vertex shader writes a NaN position there)
  far     colour = tf13(u, v)^2 * base', u and v from the tables tf18 and tf19 of the adaptive
          vertex shader; shader 2F8A times sat(d2 * c31.x + c31.y)
Limits, by design: the weight is sampled per pixel with a bilinear filter, which approximates the
game's per-vertex weight (it is that weight at 16 segments per patch edge; the game's tessellator
tops out at 15 and uses fewer far away); a layer is left out per pixel where the bilinear value is
below c114.x, where the game removes every triangle that touches such a vertex (the two differ
where a 0 texel lies beside a texel of 2 or more: --weights counts those); the normal and the
height of the steep-ground block are read at the square's 17 x 17 grid points, as the vertex shader
reads them at its vertices (point sampled), and vary linearly over the two triangles of each grid
quad that the clay pass draws (grid_interp), where the game's tessellator has at most 15 segments
and its own triangles; --no-steep leaves the block out (A = tf0^2 everywhere, the replay before
the block was read); base levels only, no mip maps; a negative alpha is clamped to 0.

Needs, per tess row: "consts" with c8, c11, c14, c46, c47, c113, c114, c115 (far: c72 too),
"pconst" (pixel constants c0..c31: c19, c21, c31), "tf" with the slots the shaders sample, and the
"texture" rows and dump files of those textures (found by fetch dwords 1 and 2: base address,
format and size; the row's own dwords give the sampler state). A row of a shader with the
steep-ground block also needs tf13 (the layer's side texture), tf17 (the normal map, k_DXN), tf16
(the heightmap), "pc" with c45 and "ps_lit" (the shader's literals: the threshold and the small
term, each in the register its shader keeps it in). --list, --weights, --weight-sums
and --locate need neither "pconst" nor c14. A capture that lacks an input, a dump shorter than
its base level and a texture format the tool does not decode are refused by name (exit code 2);
nothing is read as zero.
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import texture_thumb as tt  # noqa: E402
from gen_albedo_table import STEEP_EPSILON_MAX  # noqa: E402  (the table's bound on the small term)

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
# The layer pixel shaders with the steep-ground block (the three paired with vertex shader C30A), and
# where each keeps the block's two literals, as (pixel register, component): the slope threshold below
# which the block runs and the small term of the weights (frame-map section 13, "Terrain", item 1;
# 9C21 instr 13 and 22, 4524 instr 16 and 25, 04F2 instr 17 and 26).
STEEP_LITERALS = {
    "0x9C2144E42E95A76F": ((254, 1), (255, 3)),
    "0x45243B758F9272AB": ((254, 2), (255, 3)),
    "0x04F23292B2F44077": ((254, 1), (255, 3)),
}
CLIFF_SHADERS = tuple(STEEP_LITERALS)
COLOUR_BINS = 0x2AAAAAAA
UNCOVERED = (255, 0, 255)
# Legend colours of --weights, one per detail texture in order of appearance: a fixed palette, the
# first 16 at least 60 apart from each other, from magenta (UNCOVERED) and from black.
_PALETTE = [(230, 25, 75), (60, 180, 75), (255, 225, 25), (0, 130, 200), (245, 130, 48), (145, 30, 180),
            (70, 240, 240), (250, 190, 212), (210, 245, 60), (0, 128, 128), (110, 110, 255), (170, 110, 40),
            (128, 0, 0), (170, 255, 195), (128, 128, 0), (0, 0, 160), (128, 128, 128), (255, 255, 255)]

# The textures the replay reads, by fetch-constant slot (what _replay asks _Textures for). A layer draw
# (builder 8221C9C8) is read through these; the heightmap only for the distance fade (draws[0]).
LAYER_TEXTURES = {0: "detail colour", 3: "base map", 18: "layer weights", 16: "heightmap"}
# More for a layer draw of a shader with the steep-ground block.
STEEP_TEXTURES = {13: "side colour", 17: "normal map"}
# An underlay draw: the adaptive builder's colour pass (underlay_of).
FAR_TEXTURES = {13: "far colour", 3: "base map", 18: "far u table", 19: "far v table"}


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


_BLOCKS = {6: (1, 4, 2), 18: (4, 8, 3), 19: (4, 16, 4), 20: (4, 16, 4), 24: (1, 2, 1), 49: (4, 16, 4),
           58: (4, 8, 3)}


def base_level_need(fc):
    """Bytes of a dump the decode of this fetch constant's base level reads: the end of its last
    block (tiled) or of its last row (linear, rows by the SDK's base-level rule). ValueError
    ("format N") for a format the tool does not decode."""
    fmt = fc[1] & 0x3F
    if fmt not in _BLOCKS:
        raise ValueError(f"format {fmt}")
    block, bpb, log2 = _BLOCKS[fmt]
    w = (fc[2] & 0x1FFF) + 1
    h = ((fc[2] >> 13) & 0x1FFF) + 1
    wb, hb = (w + block - 1) // block, (h + block - 1) // block
    pitch_blocks = max((((fc[0] >> 22) & 0x1FF) << 5) // block, wb)
    if (fc[0] >> 31) != 0:
        return max(tt.tiled_offset_2d(x, y, pitch_blocks, log2) for y in range(hb) for x in range(wb)) + bpb
    return (hb - 1) * ((pitch_blocks + 31) & ~31) * bpb + wb * bpb


def bc4_values(blk):
    """The 16 values (0..255, row-major, not rounded) of an 8-byte BC4 block: two end values a0, a1
    and 16 three-bit indices from the low end of the following 48 bits. a0 > a1: indices 2 to 7 are
    the six steps of a seventh from a0 to a1; else indices 2 to 5 are the four steps of a fifth, 6
    is 0 and 7 is 255 (the BC4 / DXT5 alpha block)."""
    a0, a1 = blk[0], blk[1]
    bits = int.from_bytes(blk[2:8], "little")
    if a0 > a1:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) / 7 for i in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - i) * a0 + i * a1) / 5 for i in range(1, 5)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode_dxn(data, fc):
    """Base level of a k_DXN (49) fetch constant -> (width, height, [(x, y)] in 0..1, row-major).
    A 4 x 4 block is 16 bytes: after the fetch constant's endian swap, the BC4 block of x and then
    that of y. Source: the SDK presents k_DXN as BC5 through its plain 128-bits-per-block loader,
    which only swaps the bytes, with the host swizzle RGGG, so the shader's x is the first block
    and its y the second (thirdparty/rexglue-sdk/src/graphics/d3d12/texture_cache.cpp, the k_DXN
    row of the host format table); checked on the dump of the terrain's normal map against the
    heightmap's slopes (docs/native-renderer/frame-map.md, section 13 "Terrain"). The values are
    the block's palette entries as fractions, not rounded to 8 bits. Blocks sit like DXT5's: tiled
    at tiled_offset_2d(bx, by, pitch_blocks, 4), or linear in rows of the pitch aligned to 32
    blocks. The caller has checked the dump's size (base_level_need)."""
    endian = (fc[1] >> 6) & 3
    w = (fc[2] & 0x1FFF) + 1
    h = ((fc[2] >> 13) & 0x1FFF) + 1
    pitch = ((fc[0] >> 22) & 0x1FF) << 5
    tiled = (fc[0] >> 31) != 0
    wb, hb = (w + 3) // 4, (h + 3) // 4
    pitch_blocks = max(pitch // 4, wb)
    row_pitch = ((pitch_blocks + 31) & ~31) * 16
    out = [None] * (w * h)
    for by in range(hb):
        for bx in range(wb):
            o = tt.tiled_offset_2d(bx, by, pitch_blocks, 4) if tiled else by * row_pitch + bx * 16
            raw = tt._swap(data[o:o + 16], endian)
            xs, ys = bc4_values(raw[0:8]), bc4_values(raw[8:16])
            for i in range(16):
                x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                if x < w and y < h:
                    out[y * w + x] = (xs[i] / 255.0, ys[i] / 255.0)
    return w, h, out


def decode_texture(data, fc, what="texture"):
    """Dump bytes and six fetch dwords -> Tex. DXT1, DXT2/3, DXT4/5, 8888 as RGBA; k_DXT3A (58) with
    the value in every channel (the SDK's view of that format); k_16 (24) with the value in x;
    k_DXN (49) with its two values in x and y (decode_dxn).
    Filter and clamp modes come from the fetch constant. Others raise ValueError("format N"). A
    dump shorter than the base level raises MissingInput with both sizes: nothing is padded."""
    fmt = fc[1] & 0x3F
    need = base_level_need(fc)
    if len(data) < need:
        raise MissingInput(f"the dump of the {what} (fetch dword 1 0x{fc[1]:08X}, format {fmt}, {(fc[2] & 0x1FFF) + 1} x "
                           f"{((fc[2] >> 13) & 0x1FFF) + 1}) has {len(data)} bytes, its base level needs {need}: "
                           "the capture cut the dump short")
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
                raw = tt._swap(data[o:o + 2], endian)
                texels.append((struct.unpack("<H", raw)[0] / 65535.0, 0.0, 0.0, 1.0))
        return Tex(w, h, texels, clamp, linear)
    if fmt == 58:
        w, h, r8 = tt.decode_r8(data, fc)
        return Tex(w, h, [(v / 255.0,) * 4 for v in r8], clamp, linear)
    if fmt == 49:
        w, h, xy = decode_dxn(data, fc)
        return Tex(w, h, [(x, y, 0.0, 1.0) for x, y in xy], clamp, linear)
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


def _literal(ps_lit, reg, comp):
    """Component `comp` of pixel literal register `reg` from a tess row's "ps_lit" (runs of floats
    from a first register; pixel registers are numbered from 256 there), or None."""
    for run in ps_lit or []:
        first = run["reg"] - 256 if run["reg"] >= 256 else run["reg"]
        i = (reg - first) * 4 + comp
        if reg >= first and i < len(run["v"]):
            return run["v"][i]
    return None


def steep_inputs(consts, pc, ps_lit, ps_hash):
    """The numbers of the steep-ground block that MakeTerrainMaterialInputs (terrain_patch.h) makes
    for a layer draw whose pixel shader has it: from the vertex constant c8, the pixel constant
    c45 (the row's "pc") and the shader's two literals (the row's "ps_lit")."""
    if ps_hash not in STEEP_LITERALS:
        raise MissingInput(f"pixel shader {ps_hash} has no steep-ground block")
    _need(consts, ("c8",))
    c45 = (pc or {}).get("c45")
    if not c45 or len(c45) < 2:
        raise MissingInput('the tess row has no pixel constant c45 (in "pc"): the capture was taken before terrain '
                           "rows recorded it")
    (treg, tcomp), (ereg, ecomp) = STEEP_LITERALS[ps_hash]
    threshold, eps = _literal(ps_lit, treg, tcomp), _literal(ps_lit, ereg, ecomp)
    if threshold is None or eps is None:
        raise MissingInput('the tess row has no "ps_lit" with the pixel shader\'s literals c254 and c255: the capture '
                           "was taken before terrain rows recorded them")
    if not (0.0 < threshold <= 1.0 and 0.0 < eps < STEEP_EPSILON_MAX):
        raise MissingInput(f"the literals of {ps_hash} do not look like the block's: slope threshold {threshold!r}, "
                           f"small term {eps!r}")
    return {
        "side_uv": [consts["c8"][0], -consts["c8"][0]],
        "slope": list(c45[:2]),
        "steep_threshold": threshold,
        "steep_eps": eps,
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


def steep_normal(x, y):
    """The layer vertex shader's normal from the normal map's two values (0..1): (x, y) * 2 - 1 and
    z = sqrt(|1 - (x^2 + y^2)|) (C30A instr 25 to 31, o3)."""
    nx, ny = 2.0 * x - 1.0, 2.0 * y - 1.0
    return (nx, ny, math.sqrt(abs(1.0 - (nx * nx + ny * ny))))


def steep_slope(inp, nz):
    """s = sat(|n.z| * c45.y + c45.x): 1 on flat ground, 0 on steep (9C21 instr 12)."""
    return _sat(abs(nz) * inp["slope"][1] + inp["slope"][0])


def steep_weights(inp, nx, ny):
    """(wx, wy) = (|n.x|, |n.y|) / (|n.x| + small term + |n.y|) (9C21 instr 22 to 28, 36)."""
    inv = 1.0 / (abs(nx) + inp["steep_eps"] + abs(ny))
    return (abs(nx) * inv, abs(ny) * inv)


def side_uvs(inp, wx, wy, h):
    """(o0, o1) of a world point: o0 = (c8.x * y, -c8.x * height) is the projection for faces along
    x, o1 = (c8.x * x, -c8.x * height) for faces along y (C30A instr 52, 53, 63, 64, 74 to 76)."""
    s = inp["side_uv"]
    return (wy * s[0], h * s[1]), (wx * s[0], h * s[1])


def steep_mix(inp, n, tf0, side0, side1):
    """A of a layer shader with the steep-ground block, in the game's linear space: tf0^2 where the
    slope value reaches the threshold, else s * tf0^2 + (1 - s) * (wy * side1^2 + wx * side0^2),
    with side0 = tf13(o0) and side1 = tf13(o1) (9C21 instr 13 to 37). The clay pass shows its
    square root."""
    s = steep_slope(inp, n[2])
    if s >= inp["steep_threshold"]:
        return tuple(tf0[k] * tf0[k] for k in range(3))
    wx, wy = steep_weights(inp, n[0], n[1])
    return tuple(s * tf0[k] * tf0[k] + (1.0 - s) * (wy * side1[k] * side1[k] + wx * side0[k] * side0[k])
                 for k in range(3))


def grid_interp(a, b, c, d, fx, fy):
    """The value at (fx, fy) in 0..1 of a grid quad with the corner values a at (0, 0), b at (1, 0),
    c at (1, 1) and d at (0, 1), linear over each of the two triangles the clay pass draws the quad
    with: (a, b, c) and (a, c, d), cut from a to c (BuildGridIndices in terrain_patch.h)."""
    if fx >= fy:
        return tuple(a[k] + fx * (b[k] - a[k]) + fy * (c[k] - b[k]) for k in range(len(a)))
    return tuple(a[k] + fx * (c[k] - d[k]) + fy * (d[k] - a[k]) for k in range(len(a)))


class Layer:
    def __init__(self, inp, detail, base, weight, blend, side=None):
        """`side`: the layer's side texture (tf13) when its shader has the steep-ground block and the
        block is replayed; `inp` then holds steep_inputs too."""
        self.inp, self.detail, self.base, self.weight, self.blend, self.side = inp, detail, base, weight, blend, side


class Far:
    def __init__(self, inp, colour, base, table_u, table_v, fade_in):
        self.inp, self.colour, self.base, self.table_u, self.table_v, self.fade_in = inp, colour, base, table_u, table_v, fade_in


def _base_term(inp, base_tex, wx, wy, tint, base):
    if not base or base_tex is None:
        return (1.0, 1.0, 1.0)
    b = base_tex.sample(*apply_uv(inp["base_uv"], wx, wy))
    return tuple(abs(b[k]) ** inp["base_pow"] * (inp["base_tint"][k] if tint else 1.0) for k in range(3))


def layer_colour(layer, wx, wy, tint=True, base=True, steep=None):
    """`steep`: (normal, height) of the point for the steep-ground block, or None to leave it out."""
    d = layer.detail.sample(*apply_uv(layer.inp["detail_uv"], wx, wy))
    bp = _base_term(layer.inp, layer.base, wx, wy, tint, base)
    if layer.side is None or steep is None:
        return tuple(d[k] * d[k] * bp[k] for k in range(3))
    o0, o1 = side_uvs(layer.inp, wx, wy, steep[1])
    a = steep_mix(layer.inp, steep[0], d, layer.side.sample(*o0), layer.side.sample(*o1))
    return tuple(a[k] * bp[k] for k in range(3))


def _blend(dst, src, alpha, blend):
    s = alpha if blend[0] == "src_alpha" else 1.0
    k = 1.0 if blend[1] == "one" else 0.0
    return tuple(src[i] * s + dst[i] * k for i in range(3))


def shade_point(layers, wx, wy, h, fade_on=True, background=(0.0, 0.0, 0.0), tint=True, base=True, steep=None):
    """The layers of one point in draw order over `background`. Returns (rgb, layers drawn).
    `steep`: (normal, height) of the point for the layers with the steep-ground block, or None."""
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
        rgb = _blend(rgb, layer_colour(layer, wx, wy, tint, base, steep), _sat(alpha), layer.blend)
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


def cull_edge_pairs(tex, x0, y0, n=17):
    """(pairs, all pairs) of horizontally or vertically neighbouring texels of the n x n block at
    (x0, y0) of a weight map where one is 0 and the other 2 or more (of 15): the places where a
    cull per pixel on the bilinear value and the game's cull of whole triangles at a vertex
    differ. A 0 beside a 1 is weight 0 either way."""
    def v(x, y):
        return round(tex.texel(x0 + x, y0 + y)[3] * 15)

    bad = total = 0
    for y in range(n):
        for x in range(n):
            for dx, dy in ((1, 0), (0, 1)):
                if x + dx < n and y + dy < n:
                    a, b = v(x, y), v(x + dx, y + dy)
                    total += 1
                    bad += min(a, b) == 0 and max(a, b) >= 2
    return bad, total


_SUM_EDGES = (0.9, 0.98, 1.02, 1.1)
_SUM_NAMES = ("below 0.9", "0.9 to 0.98", "0.98 to 1.02", "1.02 to 1.1", "above 1.1")


def weight_sum_bins(sums):
    """Counts of the sums below 0.9, from 0.9 below 0.98, from 0.98 to 1.02, above 1.02 to 1.1, above 1.1."""
    bins = [0] * 5
    for v in sums:
        if v < _SUM_EDGES[0]:
            bins[0] += 1
        elif v < _SUM_EDGES[1]:
            bins[1] += 1
        elif v <= _SUM_EDGES[2]:
            bins[2] += 1
        elif v <= _SUM_EDGES[3]:
            bins[3] += 1
        else:
            bins[4] += 1
    return bins


def project(c, x, y, h, width=1120, height=720):
    """Pixel position of the world point (x, y, h) under the rows c0..c3 of a tess row (the vertex
    shaders' oPos = c0..c3 . (x, y, h, 1)), y down, for a width x height target; None at or
    behind the eye plane."""
    p = (x, y, h, 1.0)
    cx, cy, _, cw = (sum(c[r][k] * p[k] for k in range(4)) for r in range(4))
    if cw <= 0:
        return None
    return ((cx / cw * 0.5 + 0.5) * width, (0.5 - cy / cw * 0.5) * height)


def blend_of(word):
    v = int(word, 16) if isinstance(word, str) else int(word)
    if v not in _BLENDS:
        raise ValueError(f"blend word 0x{v:08X} is not one the terrain draws use")
    return _BLENDS[v]


# --- captures ---------------------------------------------------------------------------------

def read_capture(path, frame):
    """(main-scene tess rows of `frame` in capture order, {(fetch dword 1, fetch dword 2): dump
    path}). Dwords 1 and 2 hold the base address, format, endian and size; no pair of them occurs
    with two pitch or tiling fields in the captures, and the sampler bits of the other dwords
    differ between draws of one texture."""
    root = Path(path).parent
    rows, textures = [], {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            head = line[:24]
            if '"texture"' in head:
                r = json.loads(line)
                fc = [int(x, 16) for x in r["fc"]]
                textures[(fc[1], fc[2])] = root / r["file"]
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
        self.index, self.cache, self.decoded = index, {}, {}

    def get(self, row, slot, what):
        fc = (row.get("tf") or {}).get(str(slot))
        if not fc:
            raise MissingInput(f'the tess row has no fetch constant tf{slot} ({what}): the capture was taken before '
                               "terrain rows listed the textures their shaders sample")
        key = tuple(int(x, 16) for x in fc)
        if key not in self.cache:
            if key[1:3] not in self.index:
                raise MissingInput(f"no texture dump for tf{slot} ({what}), fetch dword 1 0x{key[1]:08X}")
            layout = (key[1], key[2], key[0] >> 22)   # the decode of the bytes; the rest is sampler state
            if layout in self.decoded:
                t = self.decoded[layout]
                t = Tex(t.w, t.h, t.texels, ((key[0] >> 10) & 7, (key[0] >> 13) & 7), ((key[3] >> 19) & 3) == 1)
            else:
                try:
                    t = decode_texture(Path(self.index[key[1:3]]).read_bytes(), list(key), f"tf{slot} ({what})")
                except ValueError as e:
                    raise MissingInput(f"tf{slot} ({what}), fetch dword 1 0x{key[1]:08X}, is {e}, which this tool "
                                       "does not decode") from None
                self.decoded[layout] = t
            self.cache[key] = t
        return self.cache[key]


def _height(tex, row, far, wx, wy):
    """The vertex shaders' height: the heightmap point-sampled at (world - offset) * scale."""
    c = row["consts"]
    scale, off, hs = (c["c46"], c["c46"][2:], c["c8"][2]) if far else (c["c47"], c["c47"][2:], c["c46"][2])
    return tex.sample((wx - off[0]) * scale[0], (wy - off[1]) * scale[1], linear=False)[0] * hs


class SquareGrid:
    """The normal and the height of a square at its 17 x 17 grid points, read as the layer vertex
    shader reads them at a vertex (both maps point sampled at the height coordinate), and linear
    over the grid's triangles in between (grid_interp)."""
    N = 16

    def __init__(self, square, row, height_tex, normal_tex):
        self.square = square
        c = row["consts"]
        scale, off = c["c47"], c["c47"][2:]
        self.points = []
        for j in range(self.N + 1):
            for i in range(self.N + 1):
                wx = square[0] + square[2] * i / self.N
                wy = square[1] + square[3] * j / self.N
                t = normal_tex.sample((wx - off[0]) * scale[0], (wy - off[1]) * scale[1], linear=False)
                self.points.append(steep_normal(t[0], t[1]) + (_height(height_tex, row, False, wx, wy),))

    def at(self, wx, wy):
        """((n.x, n.y, n.z), height) at a world point of the square."""
        n = self.N
        gx = min(max((wx - self.square[0]) / self.square[2] * n, 0.0), float(n))
        gy = min(max((wy - self.square[1]) / self.square[3] * n, 0.0), float(n))
        i, j = min(int(gx), n - 1), min(int(gy), n - 1)
        p = self.points
        v = grid_interp(p[j * (n + 1) + i], p[j * (n + 1) + i + 1], p[(j + 1) * (n + 1) + i + 1],
                        p[(j + 1) * (n + 1) + i], gx - i, gy - j)
        return v[:3], v[3]


_NO_STEEP_HINT = " (--no-steep replays without the steep-ground block)"


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
    steep_on = not a.no_steep
    for r in draws:
        try:
            layers.append(Layer(layer_inputs(r["consts"], r.get("pconst")), tex.get(r, 0, LAYER_TEXTURES[0]),
                                tex.get(r, 3, LAYER_TEXTURES[3]), tex.get(r, 18, LAYER_TEXTURES[18]), blend_of(r["rs"]["2201"])))
        except MissingInput as e:
            problems.append(str(e))
            continue
        if steep_on and r.get("ps_hash") in STEEP_LITERALS:
            try:
                layers[-1].inp.update(steep_inputs(r["consts"], r.get("pc"), r.get("ps_lit"), r["ps_hash"]))
                layers[-1].side = tex.get(r, 13, STEEP_TEXTURES[13])
            except MissingInput as e:
                problems.append(str(e) + _NO_STEEP_HINT)
    under = underlay_of(rows, square)
    far = None
    if under is not None:
        try:
            far = Far(far_inputs(under["consts"], under.get("pconst")), tex.get(under, 13, FAR_TEXTURES[13]),
                      tex.get(under, 3, FAR_TEXTURES[3]), tex.get(under, 18, FAR_TEXTURES[18]),
                      tex.get(under, 19, FAR_TEXTURES[19]),
                      under.get("ps_hash") == FADING_FAR)
        except MissingInput as e:
            problems.append(str(e))
    cliff = [r for r in draws if r.get("ps_hash") in CLIFF_SHADERS]
    grid = None
    if steep_on and cliff and not problems:
        # The vertex shader's normal and height at the square's grid points (every layer of a square
        # has the same patch and the same two maps).
        try:
            grid = SquareGrid(square, cliff[0], tex.get(cliff[0], 16, LAYER_TEXTURES[16]),
                              tex.get(cliff[0], 17, STEEP_TEXTURES[17]))
        except MissingInput as e:
            problems.append(str(e) + _NO_STEEP_HINT)
    if problems:
        raise MissingInput("; ".join(sorted(set(problems))))
    height = None
    if not a.no_fade:
        height = tex.get(draws[0], 16, LAYER_TEXTURES[16])
    lines.append(f"square ({square[0]:g}, {square[1]:g}) {square[2]:g} x {square[3]:g}: {len(draws)} layer draws"
                 f" ({len(cliff)} of them with the tf13 blend on steep ground, {'evaluated' if steep_on else 'not evaluated'}),"
                 " underlay " + (f"{under['ps_hash']}" if under is not None else "none"))
    n = a.res
    rgba = bytearray()
    for j in range(n):
        for i in range(n):
            wx = square[0] + square[2] * (i + 0.5) / n
            wy = square[1] + square[3] * (j + 0.5) / n
            h = _height(height, draws[0], False, wx, wy) if height is not None else None
            bg = far_colour(far, wx, wy, h, not a.no_tint, not a.no_base) if far is not None else (0.0, 0.0, 0.0)
            steep = grid.at(wx, wy) if grid is not None else None
            rgb, _ = shade_point(layers, wx, wy, h, not a.no_fade, bg, not a.no_tint, not a.no_base, steep)
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
    _cull_edges(squares, tex, lines)
    lines.append(f"wrote {a.weights}: {len(squares)} squares, {cols * 16} x {rows_n * 16}, x {min(xs):g}..{max(xs) + cw:g}"
                 f" y {min(ys):g}..{max(ys) + ch:g} (y grows downwards in the picture)")
    for key, colour in palette.items():
        lines.append(f"    detail {key}: colour {colour}")


def _read_slots(row):
    """The fetch-constant slots of a tess row that the replay reads (see LAYER_TEXTURES and FAR_TEXTURES)."""
    if row.get("func") == LAYER_BUILDER:
        return {**LAYER_TEXTURES, **STEEP_TEXTURES} if row.get("ps_hash") in STEEP_LITERALS else LAYER_TEXTURES
    if row.get("func") == FAR_BUILDER and int(row.get("rs", {}).get("bin_mask_lo", "0xFFFFFFFF"), 16) & COLOUR_BINS:
        return FAR_TEXTURES
    return {}


def _check_dumps(rows, textures, lines):
    """One line per texture the frame's tess rows name: whether its dump holds the whole base level. Only
    the textures the replay reads count; one it does not read (the fog tables, the sun mask) that is short,
    missing or of unknown size is listed as not needed."""
    seen, needed = {}, set()
    for r in rows:
        read = _read_slots(r)
        for slot, fc in sorted((r.get("tf") or {}).items(), key=lambda kv: int(kv[0])):
            if fc:
                key = tuple(int(x, 16) for x in fc)
                seen.setdefault(key[1:3], (int(slot), key))
                if int(slot) in read:
                    needed.add(key[1:3])
    bad = 0
    for (slot, fc) in sorted(seen.values()):
        head = f"tf{slot} 0x{fc[1]:08X} format {fc[1] & 0x3F} {(fc[2] & 0x1FFF) + 1} x {((fc[2] >> 13) & 0x1FFF) + 1}: "
        path = textures.get(fc[1:3])
        problem = None
        if path is None or not Path(path).is_file():
            problem = "NO TEXTURE ROW" if path is None else "NO DUMP FILE"
        else:
            have = Path(path).stat().st_size
            try:
                need = base_level_need(list(fc))
            except ValueError:
                problem = f"size not known to this tool, {have} bytes"
            else:
                if have < need:
                    problem = f"SHORT, {have} of {need} bytes"
        if problem is None:
            lines.append(head + f"ok, {have} bytes (needs {need})")
        elif fc[1:3] in needed:
            lines.append(head + problem)
            bad += 1
        else:
            lines.append(head + f"not needed by the replay ({problem})")
    lines.append(f"{bad} of {len(needed)} textures short, missing or of unknown size")


def _cell_texel(row, square, wt):
    """Texel of the weight map at the square's first grid point."""
    c = row["consts"]
    sx, sy = c["c115"][0] / c["c46"][0], c["c115"][1] / c["c46"][1]
    u = square[0] * sx + c["c115"][2] - c["c113"][0] * sx
    v = square[1] * sy + c["c115"][3] - c["c113"][1] * sy
    return math.floor(u * wt.w), math.floor(v * wt.h)


def _cull_edges(squares, tex, lines):
    bad = total = 0
    seen = set()
    for square, draws in squares:
        for r in draws:
            _need(r["consts"], ("c46", "c113", "c114", "c115"))
            if r["consts"]["c114"][0] <= 0.0:
                continue  # never culled (a square's first layer has c114.x = -1): no cull to differ
            wt = tex.get(r, 18, "layer weights")
            x0, y0 = _cell_texel(r, square, wt)
            if (id(wt.texels), x0, y0) in seen:
                continue
            seen.add((id(wt.texels), x0, y0))
            b, t = cull_edge_pairs(wt, x0, y0)
            bad += b
            total += t
    lines.append(f"cull edges: {bad} of {total} neighbouring texel pairs in the {len(seen)} 17 x 17 weight blocks of "
                 "the frame's layer draws that can be culled have a 0 beside a value of 2 or more (where a "
                 "per-pixel cull and the game's per-vertex cull differ)")


def _weight_sums(a, rows, textures, lines):
    squares = squares_of(rows)
    tex = _Textures(textures)
    groups = {"replaces": [], "adds": []}
    counts = {"replaces": 0, "adds": 0}
    for square, draws in squares:
        kind = "replaces" if blend_of(draws[0]["rs"]["2201"]) == BLEND_REPLACE else "adds"
        counts[kind] += 1
        prepared = []
        for r in draws:
            _need(r["consts"], ("c46", "c113", "c114", "c115"))
            wt = tex.get(r, 18, "layer weights")
            prepared.append((wt, _cell_texel(r, square, wt)))
        for j in range(17):
            for i in range(17):
                total = 0.0
                for wt, (x0, y0) in prepared:   # a culled texel (0) has weight 0 after the clamp anyway
                    total += _sat(WEIGHT_SCALE * (wt.texel(x0 + i, y0 + j)[3] - WEIGHT_BIAS))
                groups[kind].append(total)
    for kind, what in (("replaces", "no underlay"), ("adds", "over the underlay")):
        sums = groups[kind]
        bins = weight_sum_bins(sums)
        lines.append(f"weight sums at the 17 x 17 grid points of {counts[kind]} squares whose first layer {kind} ({what}): "
                     + ", ".join(f"{n}: {b}" for n, b in zip(_SUM_NAMES, bins))
                     + (f"; min {min(sums):.3f}, max {max(sums):.3f}" if sums else ""))


def _steep_share(a, rows, textures, lines):
    """How much of the frame's layered ground the steep-ground block changes: the slope value s at
    the 17 x 17 grid points of every square (the vertex shader's normal there), counted over all
    squares and over the squares that have a layer of a shader with the block."""
    squares = squares_of(rows)
    tex = _Textures(textures)
    ref = next((r for _, draws in squares for r in draws if r.get("ps_hash") in STEEP_LITERALS), None)
    if ref is None:
        raise MissingInput(f"frame {a.frame} has no layer draw of a shader with the steep-ground block")
    inp = steep_inputs(ref["consts"], ref.get("pc"), ref.get("ps_lit"), ref["ps_hash"])
    thr = inp["steep_threshold"]
    totals = {False: [0, 0, 0, 0, 0], True: [0, 0, 0, 0, 0]}   # squares, squares with a point below, points, below, zero
    draws_all = draws_block = 0
    for square, draws in squares:
        has_block = any(r.get("ps_hash") in STEEP_LITERALS for r in draws)
        draws_all += len(draws)
        draws_block += sum(r.get("ps_hash") in STEEP_LITERALS for r in draws)
        row = draws[0]
        _need(row["consts"], ("c46", "c47"))
        grid = SquareGrid(square, row, tex.get(row, 16, LAYER_TEXTURES[16]), tex.get(row, 17, STEEP_TEXTURES[17]))
        s = [steep_slope(inp, p[2]) for p in grid.points]
        below, zero = sum(v < thr for v in s), sum(v == 0.0 for v in s)
        for key in ((False, True) if has_block else (False,)):
            t = totals[key]
            t[0] += 1
            t[1] += below > 0
            t[2] += len(s)
            t[3] += below
            t[4] += zero
    lines.append(f"{totals[True][0]} of {totals[False][0]} squares have a layer with the steep-ground block "
                 f"({draws_block} of {draws_all} layer draws); c45 = ({inp['slope'][0]:g}, {inp['slope'][1]:g})")
    for key, what in ((False, "all squares"), (True, "squares with such a layer")):
        n, some, points, below, zero = totals[key]
        pct = lambda v: f"{100.0 * v / points:.1f}%" if points else "0.0%"
        lines.append(f"{what}: s below {thr!r} at {below} of {points} grid points ({pct(below)}), s = 0 at {zero} "
                     f"({pct(zero)}); {some} of {n} squares have a point below")


def _locate(a, rows, textures, lines):
    squares = squares_of(rows)
    if not 0 <= a.patch < len(squares):
        raise MissingInput(f"frame {a.frame} has {len(squares)} squares with layer draws; --patch {a.patch} is not one")
    square, draws = squares[a.patch]
    row = draws[0]
    _need(row["consts"], ("c0", "c1", "c2", "c3"))
    c = [row["consts"][k] for k in ("c0", "c1", "c2", "c3")]
    # The corners are projected at the ground's height: a heightmap that is missing or cut is
    # refused by name (MissingInput), as the replay refuses it; nothing is placed at height 0.
    height = _Textures(textures).get(row, 16, "heightmap")
    parts = []
    for dx, dy in ((0, 0), (1, 0), (1, 1), (0, 1)):
        x, y = square[0] + dx * square[2], square[1] + dy * square[3]
        at = project(c, x, y, _height(height, row, False, x, y))
        parts.append(f"({x:g}, {y:g}) -> " + (f"({at[0]:.1f}, {at[1]:.1f})" if at else "behind the eye"))
    lines.append(f"square {a.patch} corners in a 1120 x 720 frame (x right, y down; scale to the screenshot's size)"
                 ": " + ", ".join(parts))


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
    ap.add_argument("--no-steep", action="store_true",
                    help="leave the steep-ground block out (the detail colour on steep ground too)")
    ap.add_argument("--encode", choices=("sqrt", "linear"), default="sqrt",
                    help="sqrt (default) undoes the shaders' squaring of the detail colour for viewing")
    ap.add_argument("--exposure", type=float, default=1.0)
    ap.add_argument("--weights", help="write the frame's layer weight map and count the cull edges")
    ap.add_argument("--weight-sums", action="store_true",
                    help="print how the layer weights add up at the grid points of the frame's squares")
    ap.add_argument("--locate", action="store_true", help="with --patch: where the square is in the frame")
    ap.add_argument("--check-dumps", action="store_true",
                    help="list every texture the frame's tess rows name with its dump's size against the base level's")
    ap.add_argument("--steep-share", action="store_true",
                    help="print how much of the frame's layered ground is steep enough for the steep-ground block")
    a = ap.parse_args(argv)
    rows, textures = read_capture(a.capture, a.frame)
    lines = []
    if a.list:
        _list(rows, lines)
    if a.weights:
        _weights(a, rows, textures, lines)
    if a.check_dumps:
        _check_dumps(rows, textures, lines)
    if a.weight_sums:
        _weight_sums(a, rows, textures, lines)
    if a.steep_share:
        _steep_share(a, rows, textures, lines)
    if a.patch is not None:
        if not a.out and not a.locate:
            ap.error("--patch needs --out or --locate")
        if a.locate:
            _locate(a, rows, textures, lines)
        if a.out:
            _replay(a, rows, textures, lines)
    return lines


def cli(argv=None):
    """main() for the command line: prints the lines; 2 with one line on stderr when the capture
    lacks something."""
    try:
        for text in main(argv):
            print(text)
    except MissingInput as e:
        print(f"cannot replay: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(cli())
