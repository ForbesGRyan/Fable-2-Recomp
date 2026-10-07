import json
import math
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import terrain_replay as tr  # noqa: E402
import texture_thumb as tt  # noqa: E402

# The synthetic layer draw. tests/native/test_terrain_patch.cpp feeds the same constants to
# MakeTerrainMaterialInputs and pins the same numbers (LAYER_WANT below, and the weight, fade and
# coordinate values of test_layer_inputs_and_values): the two files are the cross-check of the
# replay tool against the capture's inputs.
LAYER_CONSTS = {
    "c4": [1, 0, 0, 0], "c5": [0, 1, 0, 0], "c6": [0, 0, 1, 0],
    "c8": [0.0625, 0.125, 1, 0.125],
    "c11": [2, 0.5, 0.25, 0],
    "c14": [1 / 384, 1 / 384, 1, 1],
    "c46": [8, 8, 90, 1],
    "c47": [1 / 384, 1 / 384, 16, 32],
    "c113": [80, 144, 0, 0],
    "c114": [1 / 255, 0, 0, 0],
    "c115": [16 / 66, 16 / 33, 33.5 / 66, 0.5 / 33],
}
PCONST = [0.0] * 128
PCONST[19 * 4:19 * 4 + 4] = [100.0, 160.0, 30.0, 1.0]
PCONST[21 * 4:21 * 4 + 4] = [1.5, 1.0, 0.5, 2.0]
PCONST[31 * 4:31 * 4 + 4] = [0.0005, -1.25, 0.0, 0.0]
LAYER_WANT = {
    "detail_uv": [0.125, -0.125, 0.0, 0.0],
    "base_uv": [1 / 384, 1 / 384, -16 / 384, -32 / 384],
    "weight_uv": [2 / 66, 2 / 33, -126.5 / 66, -287.5 / 33],
    "base_pow": 2.0,
    "base_tint": [1.5, 1.0, 0.5],
    "weight_scale": 15 / 14,
    "weight_bias": 1 / 15,
    "fade": [0.0005, -1.25],
    "hole_ref": 1 / 255,
    "eye": [100.0, 160.0, 30.0],
    "world": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0],
}
FAR_CONSTS = {
    "c8": [8, 8, 90, 1],
    "c11": [6, 1 / 6, 1 / 12, 0],
    "c14": [1 / 384, 1 / 384, 1, 1],
    "c46": [1 / 384, 1 / 384, 32, 64],
    "c47": [96, 144, 0, 0],
    "c72": [16 / 17, 0.5 / 17, 0, 0],
    "c113": [3, 0, 0, 0],
    "c114": [0.5, 0.5, 0, 0],
    "c115": [0, 0, 0, 0],
}
FAR_WANT = {
    "base_uv": [1 / 384, 1 / 384, -32 / 384, -64 / 384],
    "far_table": [16 / 17, 0.5 / 17, 0.5, 0.5],
    "far_patch": [0.0, 0.0],
    "base_pow": 2.0,
    "base_tint": [1.5, 1.0, 0.5],
    "fade": [0.0005, -1.25],
    "eye": [100.0, 160.0, 30.0],
}


def solid(rgba, linear=True):
    return tr.Tex(1, 1, [tuple(rgba)], linear=linear)


def fc_for(fmt, w, h, tiled=False, endian=1, clamp=0, linear=True, base=0x1A000000):
    pitch = (w + 31) & ~31
    return [2 | (clamp << 10) | (clamp << 13) | ((pitch >> 5) << 22) | (int(tiled) << 31), fmt | (endian << 6) | base,
            (w - 1) | ((h - 1) << 13), (0x688 << 1) | (int(linear) << 19) | (int(linear) << 21), 0, 1 << 9]


def dump_8888(w, h, texel):
    """Linear k_8_8_8_8 guest bytes (endian 8in32: the stored dword is the swapped RGBA)."""
    pitch = (w + 31) & ~31
    buf = bytearray(pitch * h * 4)
    for y in range(h):
        for x in range(w):
            buf[(y * pitch + x) * 4:(y * pitch + x) * 4 + 4] = tt._swap(bytes(texel(x, y)), 2)
    return bytes(buf)


def dump_dxt3a(w, h, value):
    """Linear k_DXT3A guest bytes (8in16) of the 4-bit image value(x, y)."""
    wb, hb = (w + 3) // 4, (h + 3) // 4
    pitch = (wb + 31) & ~31
    buf = bytearray(hb * pitch * 8)
    for by in range(hb):
        for bx in range(wb):
            post = bytearray()
            for r in range(4):
                v = sum((value(bx * 4 + i, by * 4 + r) if bx * 4 + i < w and by * 4 + r < h else 0) << (4 * i)
                        for i in range(4))
                post += bytes([v & 255, v >> 8])
            buf[(by * pitch + bx) * 8:(by * pitch + bx) * 8 + 8] = tt._swap(bytes(post), 1)
    return bytes(buf)


def dump_k16(w, h, value):
    """Linear k_16 guest bytes (8in16: big-endian words) of value(x, y) in 0..65535."""
    pitch = (w + 31) & ~31
    buf = bytearray(pitch * h * 2)
    for y in range(h):
        for x in range(w):
            struct.pack_into(">H", buf, (y * pitch + x) * 2, value(x, y))
    return bytes(buf)


def cell_value(x, y):
    """The synthetic weight atlas, 66 x 33: two 33 x 33 cells. Cell 0 (x < 33) is the base layer's: 15
    everywhere. Cell 1 is the second layer's: 0 for local x < 8 (the layer is absent there), then 1,
    then rising by one per texel to 15."""
    if x < 33:
        return 15
    lx = x - 33
    return 0 if lx < 8 else min(15, lx - 7)


class TexTest(unittest.TestCase):
    def test_point_and_linear_sampling_with_wrap_and_clamp(self):
        t = tr.Tex(2, 1, [(0.0, 0.0, 0.0, 1.0), (1.0, 0.5, 0.0, 1.0)], clamp=(0, 0), linear=True)
        self.assertEqual(t.sample(0.25, 0.5), (0.0, 0.0, 0.0, 1.0))                 # texel 0 centre
        self.assertEqual(t.sample(0.5, 0.5), (0.5, 0.25, 0.0, 1.0))                 # between the two
        self.assertEqual(t.sample(1.0, 0.5), (0.5, 0.25, 0.0, 1.0))                 # wraps: texel 1 and texel 0
        self.assertEqual(t.sample(-0.75, 0.5), (0.0, 0.0, 0.0, 1.0))                # repeat of 0.25
        c = tr.Tex(2, 1, t.texels, clamp=(2, 2), linear=True)
        self.assertEqual(c.sample(1.0, 0.5), (1.0, 0.5, 0.0, 1.0))                  # clamped to the edge texel
        p = tr.Tex(2, 1, t.texels, clamp=(0, 0), linear=False)
        self.assertEqual(p.sample(0.49, 0.5), (0.0, 0.0, 0.0, 1.0))
        self.assertEqual(p.sample(0.51, 0.5), (1.0, 0.5, 0.0, 1.0))

    def test_decode_formats(self):
        t = tr.decode_texture(dump_8888(3, 2, lambda x, y: (x * 80, y * 255, 7, 255)), fc_for(6, 3, 2, endian=2))
        self.assertEqual((t.w, t.h), (3, 2))
        self.assertEqual(t.texel(2, 1), (160 / 255, 1.0, 7 / 255, 1.0))
        w = tr.decode_texture(dump_dxt3a(66, 33, cell_value), fc_for(58, 66, 33))
        self.assertEqual(w.texel(0, 0), (1.0, 1.0, 1.0, 1.0))                       # every channel holds the value
        self.assertAlmostEqual(w.texel(33 + 9, 5)[3], 2 / 15, places=9)
        k = tr.decode_texture(dump_k16(34, 2, lambda x, y: x * 1000 + y), fc_for(24, 34, 2, linear=False))
        self.assertAlmostEqual(k.texel(33, 1)[0], 33001 / 65535)
        self.assertFalse(k.linear)
        with self.assertRaises(ValueError):
            tr.decode_texture(b"", fc_for(49, 4, 4))


class InputsTest(unittest.TestCase):
    def test_layer_inputs_and_values(self):
        inp = tr.layer_inputs(LAYER_CONSTS, PCONST)
        for key, want in LAYER_WANT.items():
            got = inp[key]
            if isinstance(want, list):
                self.assertEqual(len(got), len(want), key)
                for g, w in zip(got, want):
                    self.assertAlmostEqual(g, w, places=6, msg=key)
            else:
                self.assertAlmostEqual(got, want, places=6, msg=key)
        # Sub-patch 3 of the tile: row 1, column 1, so its corner is the origin plus one cell each way.
        self.assertEqual(tr.patch_square(LAYER_CONSTS, 3, far=False), (88.0, 152.0, 8.0, 8.0))
        # At that corner: the weight coordinate is texel 16 of cell 1 (33 + 16 + 0.5 = 49.5 of 66) and row 16.
        u, v = tr.apply_uv(inp["weight_uv"], 88.0, 152.0)
        self.assertAlmostEqual(u, 0.75, places=6)
        self.assertAlmostEqual(v, 0.5, places=6)
        u, v = tr.apply_uv(inp["base_uv"], 88.0, 152.0)
        self.assertAlmostEqual(u, 0.1875, places=6)
        self.assertAlmostEqual(v, 0.3125, places=6)
        self.assertEqual(tr.apply_uv(inp["detail_uv"], 88.0, 152.0), (11.0, -19.0))
        # Weight (n - 1) / 14 for the 4-bit texel value n; n = 0 is below the cull reference.
        self.assertAlmostEqual(tr.layer_weight(inp, 8 / 15), 0.5, places=6)
        self.assertAlmostEqual(tr.layer_weight(inp, 1.0), 1.0, places=6)
        self.assertAlmostEqual(tr.layer_weight(inp, 1 / 15), 0.0, places=6)
        self.assertTrue(tr.layer_culled(inp, 0.0))
        self.assertFalse(tr.layer_culled(inp, 1 / 15))
        # Fade: 1 - sat(d2 * 0.0005 - 1.25): 1 up to d2 = 2500, 0.75 at 3000, 0 from 4500.
        self.assertEqual(tr.fade(inp, 2500.0), 1.0)
        self.assertAlmostEqual(tr.fade(inp, 3000.0), 0.75, places=6)
        self.assertEqual(tr.fade(inp, 4500.0), 0.0)
        self.assertAlmostEqual(tr.eye_distance2(inp, 88.0, 152.0, 20.0), 308.0, places=4)

    def test_base_layer_reference_never_culls(self):
        consts = dict(LAYER_CONSTS, c114=[-1.0, 0, 0, 0])
        inp = tr.layer_inputs(consts, PCONST)
        self.assertFalse(tr.layer_culled(inp, 0.0))
        self.assertAlmostEqual(tr.layer_weight(inp, 0.0), -1 / 14, places=6)

    def test_far_inputs_and_table_coordinates(self):
        inp = tr.far_inputs(FAR_CONSTS, PCONST)
        for key, want in FAR_WANT.items():
            got = inp[key]
            if isinstance(want, list):
                for g, w in zip(got, want):
                    self.assertAlmostEqual(g, w, places=6, msg=key)
            else:
                self.assertAlmostEqual(got, want, places=6, msg=key)
        # Patch 4 of the draw is patch 7 of the block (c113.x = 3): row 1, column 1.
        self.assertEqual(tr.patch_square(FAR_CONSTS, 4, far=True), (104.0, 152.0, 8.0, 8.0))
        # Knot k of column 1, row 1 is texel 17 + k of table row 1.
        for k, texel in ((0, 17), (16, 33)):
            x, y = tr.far_knot_coord(inp, 1, 1, k)
            self.assertEqual((math.floor(x * 34), math.floor(y * 2)), (texel, 1))
        # The knot is the centre of its texel: (1 + 0.5 / 17) / 2 across, (1 + 0.5) / 2 down.
        x, y = tr.far_knot_coord(inp, 1, 1, 0)
        self.assertAlmostEqual(x, (1 + 0.5 / 17) / 2, places=9)
        self.assertAlmostEqual(y, 0.75, places=9)
        u, v = tr.apply_uv(inp["base_uv"], 104.0, 152.0)
        self.assertAlmostEqual(u, 0.1875, places=6)
        self.assertAlmostEqual(v, 88 / 384, places=6)

    def test_missing_constants_are_named(self):
        with self.assertRaisesRegex(tr.MissingInput, "c14"):
            tr.layer_inputs({k: v for k, v in LAYER_CONSTS.items() if k != "c14"}, PCONST)
        with self.assertRaisesRegex(tr.MissingInput, "pconst"):
            tr.layer_inputs(LAYER_CONSTS, None)


class ShadeTest(unittest.TestCase):
    """Hand-computed pixels. Detail A (0.8, 0.4, 0.2), detail B (0.5, 0.5, 1.0), base map (0.5, 1.0, 0.25):
    base' = base^2 * (1.5, 1.0, 0.5) = (0.375, 1.0, 0.03125);
    layer A colour = A^2 * base' = (0.64, 0.16, 0.04) * base' = (0.24, 0.16, 0.00125);
    layer B colour = (0.25, 0.25, 1.0) * base' = (0.09375, 0.25, 0.03125)."""
    BASE = (0.5, 1.0, 0.25, 1.0)
    A = (0.8, 0.4, 0.2, 1.0)
    B = (0.5, 0.5, 1.0, 1.0)

    def setUp(self):
        self.inp = tr.layer_inputs(LAYER_CONSTS, PCONST)
        self.weights = tr.decode_texture(dump_dxt3a(66, 33, cell_value), fc_for(58, 66, 33))
        base_layer = dict(self.inp, hole_ref=-1.0)
        # The base layer's cell is cell 0: its weight coordinates are 33 texels left of cell 1's.
        base_layer["weight_uv"] = self.inp["weight_uv"][:2] + [self.inp["weight_uv"][2] - 33 / 66, self.inp["weight_uv"][3]]
        self.layers = [
            tr.Layer(base_layer, solid(self.A), solid(self.BASE), self.weights, tr.BLEND_REPLACE),
            tr.Layer(self.inp, solid(self.B), solid(self.BASE), self.weights, tr.BLEND_ADD),
        ]

    def test_layer_colour(self):
        rgb = tr.layer_colour(self.layers[0], 88.0, 152.0)
        for g, w in zip(rgb, (0.24, 0.16, 0.00125)):
            self.assertAlmostEqual(g, w, places=6)
        no_tint = tr.layer_colour(self.layers[0], 88.0, 152.0, tint=False)
        for g, w in zip(no_tint, (0.16, 0.16, 0.0025)):
            self.assertAlmostEqual(g, w, places=6)
        no_base = tr.layer_colour(self.layers[0], 88.0, 152.0, base=False)
        for g, w in zip(no_base, (0.64, 0.16, 0.04)):
            self.assertAlmostEqual(g, w, places=6)

    def test_two_layers_blend_in_draw_order(self):
        # World x = 88 + 8 * (lx - 16) / 16 puts the weight coordinate on texel lx of the second layer's cell.
        def at(lx):
            return tr.shade_point(self.layers, 88.0 + (lx - 16) * 0.5, 152.0, None, fade_on=False)

        # lx = 15: the second layer's value is 8, weight 0.5: A + 0.5 * B.
        rgb, drawn = at(15)
        for g, w in zip(rgb, (0.24 + 0.046875, 0.16 + 0.125, 0.00125 + 0.015625)):
            self.assertAlmostEqual(g, w, places=6)
        self.assertEqual(drawn, 2)
        # lx = 8: value 1, weight 0: the base layer alone, the second layer drawn with alpha 0.
        rgb, drawn = at(8)
        for g, w in zip(rgb, (0.24, 0.16, 0.00125)):
            self.assertAlmostEqual(g, w, places=6)
        self.assertEqual(drawn, 2)
        # lx = 22: value 15, weight 1: A + B.
        rgb, _ = at(22)
        for g, w in zip(rgb, (0.33375, 0.41, 0.0325)):
            self.assertAlmostEqual(g, w, places=6)
        # Half way between texels 14 (value 7) and 15 (value 8): the bilinear weight is 6.5 / 14.
        rgb, _ = at(14.5)
        self.assertAlmostEqual(rgb[1], 0.16 + 0.25 * 6.5 / 14, places=6)

    def test_culled_layer_is_not_drawn(self):
        # lx = 4: the second layer's value is 0, below c114.x: the game writes a NaN position there.
        rgb, drawn = tr.shade_point(self.layers, 88.0 + (4 - 16) * 0.5, 152.0, None, fade_on=False)
        self.assertEqual(drawn, 1)
        for g, w in zip(rgb, (0.24, 0.16, 0.00125)):
            self.assertAlmostEqual(g, w, places=6)
        # The base layer's reference is -1: it is drawn where its own value is 0 too, with the weight
        # -1/14 clamped to 0 as a fixed-point colour target clamps the alpha.
        zero = tr.Tex(1, 1, [(0.0, 0.0, 0.0, 0.0)])
        lone = [tr.Layer(self.layers[0].inp, solid(self.A), solid(self.BASE), zero, tr.BLEND_REPLACE)]
        rgb, drawn = tr.shade_point(lone, 88.0, 152.0, None, fade_on=False)
        self.assertEqual((rgb, drawn), ((0.0, 0.0, 0.0), 1))

    def test_fade_scales_the_alpha(self):
        # The eye is (100, 160, 30): at height 30 and y = 152, x is chosen so d2 = 3000 (fade 0.75).
        x = 100.0 - math.sqrt(3000.0 - 64.0)
        lone = [tr.Layer(self.layers[0].inp, solid(self.A), solid(self.BASE), solid((1.0, 1.0, 1.0, 1.0)), tr.BLEND_REPLACE)]
        rgb, _ = tr.shade_point(lone, x, 152.0, 30.0, fade_on=True)
        for g, w in zip(rgb, (0.24 * 0.75, 0.16 * 0.75, 0.00125 * 0.75)):
            self.assertAlmostEqual(g, w, places=5)

    def test_additive_layer_keeps_the_background(self):
        rgb, _ = tr.shade_point(self.layers[1:], 88.0 + (22 - 16) * 0.5, 152.0, None, fade_on=False,
                                background=(0.1, 0.2, 0.3))
        for g, w in zip(rgb, (0.1 + 0.09375, 0.2 + 0.25, 0.3 + 0.03125)):
            self.assertAlmostEqual(g, w, places=6)

    def test_far_patch(self):
        inp = tr.far_inputs(FAR_CONSTS, PCONST)
        # Tables 34 x 2 (two patch columns, two rows of patches), point sampled. Column 1, row 1: u runs
        # 0.25 to 0.75 over the 17 knots, v is 0.75 at every knot.
        tu = tr.decode_texture(dump_k16(34, 2, lambda x, y: round(65535 * (0.25 + 0.5 * (x - 17) / 16)) if x >= 17 and y == 1 else 0),
                               fc_for(24, 34, 2, linear=False))
        tv = tr.decode_texture(dump_k16(34, 2, lambda x, y: round(65535 * 0.75)), fc_for(24, 34, 2, linear=False))
        # Far colour 2 x 2, point sampled: (u < 0.5, v >= 0.5) is texel (0, 1), (u >= 0.5, v >= 0.5) texel (1, 1).
        far = tr.Tex(2, 2, [(0.1, 0.1, 0.1, 1), (0.2, 0.2, 0.2, 1), (0.6, 0.3, 0.9, 1), (0.9, 0.6, 0.3, 1)], linear=False)
        f = tr.Far(inp, far, solid(self.BASE), tu, tv, fade_in=False)
        x0, y0, cw, ch = tr.patch_square(FAR_CONSTS, 4, far=True)
        self.assertAlmostEqual(tr.far_uv(f, x0 + 0.25 * cw, y0 + 0.5 * ch)[1], 0.75, places=4)
        self.assertAlmostEqual(tr.far_uv(f, x0 + 0.25 * cw, y0 + 0.5 * ch)[0], 0.375, places=4)
        # U = 0.25: u = 0.375, texel (0, 1): (0.6, 0.3, 0.9)^2 * base' = (0.36 * 0.375, 0.09, 0.81 * 0.03125).
        rgb = tr.far_colour(f, x0 + 0.25 * cw, y0 + 0.5 * ch, None)
        for g, w in zip(rgb, (0.135, 0.09, 0.0253125)):
            self.assertAlmostEqual(g, w, places=6)
        # U = 0.75: u = 0.625, texel (1, 1): (0.81 * 0.375, 0.36, 0.09 * 0.03125).
        rgb = tr.far_colour(f, x0 + 0.75 * cw, y0 + 0.5 * ch, None)
        for g, w in zip(rgb, (0.30375, 0.36, 0.0028125)):
            self.assertAlmostEqual(g, w, places=6)
        # The fading far shader (2F8A) multiplies by sat(d2 * c31.x + c31.y): 0.25 at d2 = 3000.
        wx, wy = x0 + 0.25 * cw, y0 + 0.5 * ch
        g = tr.Far(dict(inp, eye=[wx + math.sqrt(3000.0), wy, 30.0]), far, solid(self.BASE), tu, tv, fade_in=True)
        rgb = tr.far_colour(g, wx, wy, 30.0)
        for got, w in zip(rgb, (0.135 * 0.25, 0.09 * 0.25, 0.0253125 * 0.25)):
            self.assertAlmostEqual(got, w, places=5)

    def test_blend_words(self):
        self.assertEqual(tr.blend_of("0x00060006"), tr.BLEND_REPLACE)
        self.assertEqual(tr.blend_of("0x01060106"), tr.BLEND_ADD)
        self.assertEqual(tr.blend_of("0x00010001"), tr.BLEND_OPAQUE)
        with self.assertRaises(ValueError):
            tr.blend_of("0x07060706")


def png_size_and_pixels(path):
    raw = Path(path).read_bytes()
    w, h = struct.unpack(">II", raw[16:24])
    at = raw.index(b"IDAT")
    size = struct.unpack(">I", raw[at - 4:at])[0]
    data = zlib.decompress(raw[at + 4:at + 4 + size])
    rows = [data[y * (w * 4 + 1) + 1:(y + 1) * (w * 4 + 1)] for y in range(h)]
    return w, h, rows


class CaptureTest(unittest.TestCase):
    """The tool on a capture: tess rows, texture rows and dump files as discovery writes them."""

    def _capture(self, root, pconst=True, with_tf0=True):
        tex = root / "native_tex_x"
        tex.mkdir()
        files = {}

        def add(name, data, fc):
            (tex / name).write_bytes(data)
            files[name] = fc
            return [f"0x{x:08X}" for x in fc]

        a = add("a.bin", dump_8888(2, 2, lambda x, y: (204, 102, 51, 255)), fc_for(6, 2, 2, endian=2, base=0x1A100000))
        b = add("b.bin", dump_8888(2, 2, lambda x, y: (128, 128, 255, 255)), fc_for(6, 2, 2, endian=2, base=0x1A200000))
        base = add("base.bin", dump_8888(2, 2, lambda x, y: (128, 255, 64, 255)), fc_for(6, 2, 2, endian=2, base=0x1A300000))
        wgt = add("w.bin", dump_dxt3a(66, 33, cell_value), fc_for(58, 66, 33, base=0x1A400000))
        rows = [{"kind": "frame", "frame": 1}]
        for name, fc in files.items():
            rows.append({"kind": "texture", "file": f"native_tex_x/{name}", "bytes": 1, "fc": [f"0x{x:08X}" for x in fc]})

        def tess(consts, blend, detail):
            r = {"kind": "tess", "frame": 1, "func": "DrawIndx:8221C9C8",
                 "args": ["0x00000012", "0x00000003", "0x00000001", "0x00000000"], "in_scene": True,
                 "rs": {"2201": blend, "bin_mask_lo": "0x0003000A"},
                 "vs": {"hash": "0xC30A97D946FA2BE4"}, "ps_hash": "0x9C2144E42E95A76F",
                 "consts": consts, "tf": {"3": base, "18": wgt}}
            if with_tf0:
                r["tf"]["0"] = detail
            if pconst:
                r["pconst"] = PCONST
            return r

        base_consts = dict(LAYER_CONSTS, c114=[-1.0, 0, 0, 0],
                           c115=[16 / 66, 16 / 33, 0.5 / 66, 0.5 / 33])
        rows.append(tess(base_consts, "0x00060006", a))
        rows.append(tess(LAYER_CONSTS, "0x01060106", b))
        path = root / "cap.jsonl"
        path.write_text("".join(json.dumps(r) + chr(10) for r in rows))
        return path

    def test_replay_writes_the_patch(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root)
            out = root / "patch.png"
            lines = tr.main([str(cap), "--frame", "1", "--patch", "0", "--out", str(out), "--res", "16",
                             "--no-fade", "--encode", "linear"])
            self.assertTrue(any("2 layer draws" in line for line in lines), lines)
            w, h, rows = png_size_and_pixels(out)
            self.assertEqual((w, h), (16, 16))
            # Pixel column i is U = (i + 0.5) / 16: weight texel coordinate 16 + i + 0.5, half way between
            # texels 16 + i and 17 + i. Column 0: values 9 and 10, weight 8.5 / 14. Colours as in ShadeTest
            # with the 8-bit texture values: A = (204, 102, 51) / 255, B = (128, 128, 255) / 255,
            # base = (128, 255, 64) / 255.
            a2 = [(c / 255) ** 2 for c in (204, 102, 51)]
            b2 = [(c / 255) ** 2 for c in (128, 128, 255)]
            bp = [(c / 255) ** 2 * t for c, t in zip((128, 255, 64), (1.5, 1.0, 0.5))]
            want = [round(255 * min(1.0, (a2[k] + b2[k] * 8.5 / 14) * bp[k])) for k in range(3)]
            self.assertEqual(list(rows[8][0:3]), want)
            # Column 15: texels 31 and 32, both 15: weight 1.
            want = [round(255 * min(1.0, (a2[k] + b2[k]) * bp[k])) for k in range(3)]
            self.assertEqual(list(rows[8][15 * 4:15 * 4 + 3]), want)

    def test_list_and_weights_need_no_pixel_constants(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False)
            lines = tr.main([str(cap), "--frame", "1", "--list"])
            self.assertTrue(any("patch 0" in line and "(88, 152)" in line and "2 layers" in line for line in lines), lines)
            out = root / "weights.png"
            lines = tr.main([str(cap), "--frame", "1", "--weights", str(out)])
            w, h, rows = png_size_and_pixels(out)
            self.assertEqual((w, h), (16, 16))
            self.assertTrue(any("1 squares" in line for line in lines), lines)
            # Every pixel has the base layer at weight 1 plus the second layer: none is marked uncovered.
            self.assertNotIn(bytes(tr.UNCOVERED), [rows[8][i * 4:i * 4 + 3] for i in range(16)])

    def test_underlay_is_the_adaptive_draw_with_a_colour_pass_over_the_square(self):
        def far_row(bins, origin, name):
            return {"func": "DrawIndx:82207C30", "args": ["0x12", "0x0", "0x0", "0x00000006"],
                    "rs": {"bin_mask_lo": bins}, "ps_hash": name,
                    "consts": dict(FAR_CONSTS, c47=[origin[0], origin[1], 0, 0])}

        square = (104.0, 152.0, 8.0, 8.0)   # patch 7 of a block at (96, 144): c113.x = 3, so draw patch 4
        pre_pass = far_row("0x15555555", (96, 144), "pre")
        elsewhere = far_row("0x2AAAAAAA", (192, 144), "other")
        colour = far_row("0x2AAAAAAA", (96, 144), "colour")
        self.assertIsNone(tr.underlay_of([pre_pass, elsewhere], square))
        self.assertEqual(tr.underlay_of([pre_pass, elsewhere, colour], square)["ps_hash"], "colour")
        self.assertEqual(tr.underlay_of([far_row("0xFFFFFFFF", (96, 144), "all")], square)["ps_hash"], "all")
        # The draw's six patches are 3..8 of the block: (120, 144) is patch 3, (96, 144) is patch 0, not drawn.
        self.assertIsNotNone(tr.underlay_of([colour], (120.0, 144.0, 8.0, 8.0)))
        self.assertIsNone(tr.underlay_of([colour], (96.0, 144.0, 8.0, 8.0)))

    def test_replay_names_what_an_old_capture_lacks(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False, with_tf0=False)
            with self.assertRaisesRegex(tr.MissingInput, "pconst"):
                tr.main([str(cap), "--frame", "1", "--patch", "0", "--out", str(root / "p.png")])


if __name__ == "__main__":
    unittest.main()
