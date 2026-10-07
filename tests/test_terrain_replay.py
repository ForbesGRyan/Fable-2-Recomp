import contextlib
import io
import json
import math
import os
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


def bc4_block(a0, a1, index):
    """Eight bytes of a BC4 block: the two end values and index(i) (0..7) of texel i, row-major, three
    bits each from the low end."""
    bits = sum((index(i) & 7) << (3 * i) for i in range(16))
    return bytes([a0, a1]) + bits.to_bytes(6, "little")


def dump_dxn(w, h, block):
    """Linear k_DXN guest bytes (8in16). block(bx, by) gives the 16 bytes of a block as the GPU reads
    them: the BC4 block of x, then that of y."""
    wb, hb = (w + 3) // 4, (h + 3) // 4
    pitch = (wb + 31) & ~31
    buf = bytearray(hb * pitch * 16)
    for by in range(hb):
        for bx in range(wb):
            buf[(by * pitch + bx) * 16:(by * pitch + bx) * 16 + 16] = tt._swap(block(bx, by), 1)
    return bytes(buf)


def flat_dxn(x, y):
    """A block whose every texel is (x, y) (both end values equal: every palette entry is the value)."""
    return bc4_block(x, x, lambda i: 0) + bc4_block(y, y, lambda i: 0)


# The steep-ground block of the three layer shaders paired with C30A. tests/native/test_terrain_patch.cpp
# feeds the same constants to MakeTerrainMaterialInputs and pins the same numbers (STEEP_WANT and the
# values of SteepTest): c45 as captured in game session 1, the two literals as the shaders' tables hold
# them.
STEEP_PC = {"c45": [-7.66190386, 10.0019045, 0, 0]}
STEEP_LIT_9C21 = [{"reg": 508, "v": [0, 0, 0, 0, 0, 0, 0, 0, 2, 0.99609375, 8, 0, 0, -1, 1, 9.99999975e-05]}]
STEEP_LIT_4524 = [{"reg": 508, "v": [0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0.99609375, 0, 0, 1, -1, 9.99999975e-05]}]
STEEP_WANT = {
    "side_uv": [0.0625, -0.0625],
    "slope": [-7.66190386, 10.0019045],
    "steep_threshold": 0.99609375,
    "steep_eps": 0.0001,
}


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
            tr.decode_texture(b"", fc_for(2, 4, 4))

    def test_decode_dxn(self):
        # 8 x 4: two blocks. Block 0: x has end values 200 > 60 (eight values: the ends, then six steps
        # of a seventh from 200 towards 60, not rounded), texel i with index i % 8; y has 10 <= 250 (six
        # values: the ends, four steps of a fifth, then 0 and 255). Block 1: x 255 everywhere, y 0.
        def block(bx, by):
            if bx == 0:
                return bc4_block(200, 60, lambda i: i % 8) + bc4_block(10, 250, lambda i: i % 8)
            return flat_dxn(255, 0)

        t = tr.decode_texture(dump_dxn(8, 4, block), fc_for(49, 8, 4, linear=False))
        self.assertEqual((t.w, t.h), (8, 4))
        self.assertFalse(t.linear)
        x_want = [200, 60] + [((7 - i) * 200 + i * 60) / 7 for i in range(1, 7)]
        y_want = [10, 250] + [((5 - i) * 10 + i * 250) / 5 for i in range(1, 5)] + [0, 255]
        self.assertAlmostEqual(x_want[2], 180.0, places=9)        # (6 * 200 + 60) / 7
        self.assertAlmostEqual(x_want[4], 140.0, places=9)        # (4 * 200 + 3 * 60) / 7
        self.assertAlmostEqual(x_want[3], 1120 / 7, places=9)     # (5 * 200 + 2 * 60) / 7 = 160
        self.assertAlmostEqual(y_want[2], 58.0, places=9)         # (4 * 10 + 250) / 5
        for i in range(16):
            got = t.texel(i % 4, i // 4)
            self.assertAlmostEqual(got[0], x_want[i % 8] / 255, places=9, msg=i)   # x: the first eight bytes
            self.assertAlmostEqual(got[1], y_want[i % 8] / 255, places=9, msg=i)   # y: the second
            self.assertEqual(got[2:], (0.0, 1.0))
        self.assertEqual(t.texel(5, 2), (1.0, 0.0, 0.0, 1.0))
        # A value that is no whole number of 255ths is kept as it is: 200 and 61 give 1339 / 7.
        odd = tr.decode_texture(dump_dxn(4, 4, lambda bx, by: bc4_block(200, 61, lambda i: 2) + flat_dxn(0, 0)[8:]),
                                fc_for(49, 4, 4, linear=False))
        self.assertAlmostEqual(odd.texel(0, 0)[0], (6 * 200 + 61) / 7 / 255, places=9)
        # One row of 32 blocks of 16 bytes; the second block ends at byte 32.
        self.assertEqual(tr.base_level_need(fc_for(49, 8, 4)), 32)
        with self.assertRaisesRegex(tr.MissingInput, r"normal map.*has 31 bytes.*needs 32"):
            tr.decode_texture(bytes(31), fc_for(49, 8, 4), "normal map")
        # The byte order is the fetch constant's: the same guest bytes read without the 8in16 swap have
        # the end values exchanged (60 <= 200: the six-value mode, index 0 is 60).
        guest = dump_dxn(4, 4, lambda bx, by: bc4_block(200, 60, lambda i: 0) + flat_dxn(0, 0)[8:])
        self.assertAlmostEqual(tr.decode_texture(guest, fc_for(49, 4, 4)).texel(0, 0)[0], 200 / 255, places=9)
        self.assertAlmostEqual(tr.decode_texture(guest, fc_for(49, 4, 4, endian=0)).texel(0, 0)[0], 60 / 255, places=9)

    def test_real_tf17_dump_matches_cpp_decoder(self):
        """The terrain normal map, once a discovery capture has dumped it (tf17 of the layer draws,
        k_DXN). Needs FABLE2_TF17_BIN and FABLE2_TF17_FC (see tests/native/test_texture_decode.cpp) and
        the C++ test's output FABLE2_TF17_BIN + '.cpp.xy'; SKIPS (never passes silently) when any is
        missing."""
        bin_path, fcs = os.environ.get("FABLE2_TF17_BIN"), os.environ.get("FABLE2_TF17_FC")
        if not bin_path or not fcs or not Path(bin_path).is_file():
            self.skipTest("no tf17 dump: set FABLE2_TF17_BIN and FABLE2_TF17_FC after a discovery capture")
        cpp = Path(bin_path + ".cpp.xy")
        if not cpp.is_file():
            self.skipTest("run tests/run_native_tests.cmd with the same variables first (writes " + str(cpp) + ")")
        fc = [int(x, 16) for x in fcs.split(",")]
        self.assertEqual(fc[1] & 0x3F, 49)
        w, h, xy = tr.decode_dxn(Path(bin_path).read_bytes(), fc)
        got = struct.unpack(f"<{2 * w * h}f", cpp.read_bytes())
        self.assertEqual(len(xy), w * h)
        worst = max(max(abs(p[0] - got[2 * i]), abs(p[1] - got[2 * i + 1])) for i, p in enumerate(xy))
        self.assertLess(worst, 1e-6)       # the C++ decode works in single precision

    def test_short_dump_is_refused_with_the_sizes(self):
        # Linear 34 x 2 k_16, pitch 64: one row of 128 bytes and 34 texels of the second: 196 bytes.
        fc = fc_for(24, 34, 2, linear=False)
        data = dump_k16(34, 2, lambda x, y: 7)
        self.assertEqual(tr.base_level_need(fc), 196)
        tr.decode_texture(data[:196], fc)
        with self.assertRaisesRegex(tr.MissingInput, r"far u table.*0x1A000058.*has 195 bytes.*needs 196"):
            tr.decode_texture(data[:195], fc, "far u table")
        # Tiled 40 x 20 k_16, pitch 64: the last texel read is in the second tile; the first 64 KB of a
        # larger level would not do either.
        tiled = fc_for(24, 40, 20, tiled=True, linear=False)
        need = tr.base_level_need(tiled)
        self.assertEqual(need, max(tt.tiled_offset_2d(x, y, 64, 1) for x in range(40) for y in range(20)) + 2)
        with self.assertRaisesRegex(tr.MissingInput, f"has {need - 1} bytes.*needs {need}"):
            tr.decode_texture(bytes(need - 1), tiled, "heightmap")
        tr.decode_texture(bytes(need), tiled, "heightmap")
        # The block formats: DXT3A 66 x 33 linear (17 x 9 blocks, rows of 32 blocks), 8888 3 x 2.
        w = fc_for(58, 66, 33)
        self.assertEqual(tr.base_level_need(w), 8 * 32 * 8 + 17 * 8)
        with self.assertRaisesRegex(tr.MissingInput, "layer weights"):
            tr.decode_texture(dump_dxt3a(66, 33, cell_value)[:tr.base_level_need(w) - 1], w, "layer weights")
        c = fc_for(6, 3, 2, endian=2)
        with self.assertRaisesRegex(tr.MissingInput, "has 0 bytes"):
            tr.decode_texture(b"", c, "detail colour")


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


class WeightChecksTest(unittest.TestCase):
    def test_cull_edge_pairs(self):
        # 3 x 3 texel values (of 15):  0 1 5 / 0 0 2 / 3 0 0. Pairs of neighbours with a 0 beside a value
        # of 2 or more: across (0, 2) in row 1 and (3, 0) in row 2; down (0, 3) in column 0 and (2, 0) in
        # column 2: 4 of the 12 pairs. (0, 1) and (1, 0) do not count.
        vals = [0, 1, 5, 0, 0, 2, 3, 0, 0]
        t = tr.Tex(3, 3, [(v / 15,) * 4 for v in vals])
        self.assertEqual(tr.cull_edge_pairs(t, 0, 0, 3), (4, 12))
        # A 0 beside a 1 is where the per-pixel and the per-vertex cull agree (weight 0 either way).
        ones = tr.Tex(2, 2, [(0.0,) * 4, (1 / 15,) * 4, (1 / 15,) * 4, (1 / 15,) * 4])
        self.assertEqual(tr.cull_edge_pairs(ones, 0, 0, 2), (0, 4))
        # The window: only the 2 x 2 texels from (1, 1): 0 2 / 0 0: across (0, 2), down (2, 0): 2 of 4.
        self.assertEqual(tr.cull_edge_pairs(t, 1, 1, 2), (2, 4))

    def test_weight_sum_bins(self):
        sums = [0.5, 0.9, 0.97, 0.98, 1.0, 1.02, 1.05, 1.1, 1.5]
        self.assertEqual(tr.weight_sum_bins(sums), [1, 2, 3, 2, 1])
        self.assertEqual(tr.weight_sum_bins([]), [0, 0, 0, 0, 0])

    def test_project(self):
        ident = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        self.assertEqual(tr.project(ident, 0.5, 0.5, 0.0), (840.0, 180.0))       # ndc (0.5, 0.5), y up
        self.assertEqual(tr.project(ident, -1.0, 1.0, 0.0), (0.0, 0.0))          # top left
        self.assertEqual(tr.project(ident, 1.0, -1.0, 0.0, 200, 100), (200.0, 100.0))
        # w from the height: (1, 1, 2) has w 2 and lands on the same pixel; behind the eye there is none.
        persp = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 1, 0]]
        self.assertEqual(tr.project(persp, 1.0, 1.0, 2.0), (840.0, 180.0))
        self.assertIsNone(tr.project(persp, 1.0, 1.0, -1.0))
        self.assertIsNone(tr.project(persp, 1.0, 1.0, 0.0))


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
        # 0.25 to 0.75 over the 17 knots and v 0.6 to 0.9, so reading the v table with the knot along u
        # (0.675 at U = 0.25 instead of 0.75 at V = 0.5) fails the coordinate check below.
        tu = tr.decode_texture(dump_k16(34, 2, lambda x, y: round(65535 * (0.25 + 0.5 * (x - 17) / 16)) if x >= 17 and y == 1 else 0),
                               fc_for(24, 34, 2, linear=False))
        tv = tr.decode_texture(dump_k16(34, 2, lambda x, y: round(65535 * (0.6 + 0.3 * (x - 17) / 16)) if x >= 17 and y == 1 else 0),
                               fc_for(24, 34, 2, linear=False))
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


class SteepTest(unittest.TestCase):
    """The steep-ground block, hand-computed. Detail colour tf0 = (0.8, 0.4, 0.2), so tf0^2 = (0.64, 0.16,
    0.04). The side texture is 2 x 2, point sampled: row 1 holds P = (0.3, 0.6, 0.9) and Q = (0.5, 0.25,
    1.0). At the world point (92, 148, height 20) with c8.x = 0.0625:
      o0 = (c8.x * y, -c8.x * h) = (9.25, -1.25): texel (0, 1) = P   (the projection for faces along x)
      o1 = (c8.x * x, -c8.x * h) = (5.75, -1.25): texel (1, 1) = Q   (for faces along y)
    c45 = (-7.6619, 10.0019): s = sat(|n.z| * 10.0019 - 7.6619)."""
    TF0 = (0.8, 0.4, 0.2, 1.0)
    P = (0.3, 0.6, 0.9, 1.0)
    Q = (0.5, 0.25, 1.0, 1.0)

    def setUp(self):
        self.inp = dict(tr.layer_inputs(LAYER_CONSTS, PCONST),
                        **tr.steep_inputs(LAYER_CONSTS, STEEP_PC, STEEP_LIT_9C21, "0x9C2144E42E95A76F"))
        self.side = tr.Tex(2, 2, [(0.0,) * 4, (0.0,) * 4, self.P, self.Q], linear=False)

    def near(self, got, want, places=6):
        self.assertEqual(len(got), len(want))
        for g, w in zip(got, want):
            self.assertAlmostEqual(g, w, places=places)

    def test_steep_inputs(self):
        for key, want in STEEP_WANT.items():
            got = self.inp[key]
            if isinstance(want, list):
                self.near(got, want)
            else:
                self.assertAlmostEqual(got, want, places=9, msg=key)
        # The literals are found by each shader's own register: 4524 keeps the threshold in c254.z.
        other = tr.steep_inputs(LAYER_CONSTS, STEEP_PC, STEEP_LIT_4524, "0x45243B758F9272AB")
        self.assertEqual(other["steep_threshold"], 0.99609375)
        self.assertAlmostEqual(other["steep_eps"], 0.0001, places=9)
        # Read with the other shader's register the threshold would be 2: refused, not used.
        with self.assertRaisesRegex(tr.MissingInput, "threshold"):
            tr.steep_inputs(LAYER_CONSTS, STEEP_PC, STEEP_LIT_9C21, "0x45243B758F9272AB")
        with self.assertRaisesRegex(tr.MissingInput, "c45"):
            tr.steep_inputs(LAYER_CONSTS, {}, STEEP_LIT_9C21, "0x9C2144E42E95A76F")
        with self.assertRaisesRegex(tr.MissingInput, "ps_lit"):
            tr.steep_inputs(LAYER_CONSTS, STEEP_PC, None, "0x9C2144E42E95A76F")
        with self.assertRaisesRegex(tr.MissingInput, "no steep-ground block"):
            tr.steep_inputs(LAYER_CONSTS, STEEP_PC, STEEP_LIT_9C21, "0x4755A2E4A05BA678")

    def test_normal_from_the_map_texel(self):
        self.near(tr.steep_normal(0.5, 0.5), (0.0, 0.0, 1.0))
        self.near(tr.steep_normal(0.8, 0.2), (0.6, -0.6, math.sqrt(0.28)))
        self.near(tr.steep_normal(1.0, 0.5), (1.0, 0.0, 0.0))
        # x^2 + y^2 above 1 (the map's compression gives such texels): the root of the absolute value.
        self.near(tr.steep_normal(1.0, 1.0), (1.0, 1.0, 1.0))

    def test_slope(self):
        self.assertEqual(tr.steep_slope(self.inp, 1.0), 1.0)                                    # flat
        self.assertAlmostEqual(tr.steep_slope(self.inp, math.cos(math.radians(30))), 1.0, places=6)
        self.assertAlmostEqual(tr.steep_slope(self.inp, math.cos(math.radians(35))), 0.5311767, places=6)
        self.assertEqual(tr.steep_slope(self.inp, math.cos(math.radians(40))), 0.0)
        self.assertEqual(tr.steep_slope(self.inp, 0.0), 0.0)                                    # vertical
        self.assertAlmostEqual(tr.steep_slope(self.inp, -math.cos(math.radians(35))), 0.5311767, places=6)   # |n.z|

    def test_weights_and_side_coordinates(self):
        self.near(tr.steep_weights(self.inp, 1.0, 0.0), (1 / 1.0001, 0.0))
        self.near(tr.steep_weights(self.inp, 0.0, -1.0), (0.0, 1 / 1.0001))
        self.near(tr.steep_weights(self.inp, 0.6, -0.6), (0.6 / 1.2001, 0.6 / 1.2001))
        self.near(tr.steep_weights(self.inp, 0.0, 0.0), (0.0, 0.0))                             # the small term
        o0, o1 = tr.side_uvs(self.inp, 92.0, 148.0, 20.0)
        self.assertEqual(o0, (9.25, -1.25))
        self.assertEqual(o1, (5.75, -1.25))
        self.assertEqual(self.side.sample(*o0), self.P)
        self.assertEqual(self.side.sample(*o1), self.Q)

    def test_mix_flat_35_degrees_and_vertical(self):
        mix = lambda n: tr.steep_mix(self.inp, n, self.TF0, self.P, self.Q)
        # Flat: s = 1, the block is skipped: tf0^2, shown as tf0.
        self.near(mix((0.0, 0.0, 1.0)), (0.64, 0.16, 0.04))
        # 35 degrees, facing x: s = 0.5311767, wx = 0.573576 / 0.573676 = 0.9998257, wy = 0:
        # A = s * tf0^2 + (1 - s) * wx * P^2.
        n35 = (math.sin(math.radians(35)), 0.0, math.cos(math.radians(35)))
        self.near(mix(n35), (0.3821398, 0.2537352, 0.4009278))
        self.near([math.sqrt(c) for c in mix(n35)], (0.6181746, 0.5037214, 0.6331886))
        # Vertical, facing x: s = 0: P^2 / 1.0001. Facing y (either way): Q^2 / 1.0001.
        self.near(mix((1.0, 0.0, 0.0)), (0.0899910, 0.3599640, 0.8099190))
        self.near(mix((0.0, -1.0, 0.0)), (0.2499750, 0.0624938, 0.9999000))
        self.near([math.sqrt(c) for c in mix((0.0, 1.0, 0.0))], (0.4999750, 0.2499875, 0.9999500))
        # Between the two: equal weights 0.6 / 1.2001 of P^2 and Q^2 (s = 0 at n.z = 0.529).
        self.near(mix((0.6, -0.6, math.sqrt(0.28))),
                  [0.6 / 1.2001 * (p * p + q * q) for p, q in zip(self.P[:3], self.Q[:3])])

    def test_the_block_is_skipped_from_the_threshold_on(self):
        mix = lambda nz: tr.steep_mix(self.inp, (math.sqrt(1 - nz * nz), 0.0, nz), self.TF0, self.P, self.Q)
        at = lambda s: (s + 7.66190386) / 10.0019045          # the n.z that gives the slope value s
        self.assertEqual(mix(at(0.99609375 + 1e-9)), (0.8 * 0.8, 0.4 * 0.4, 0.2 * 0.2))
        # Just below it the side colour enters with (1 - s): continuous, no step at the threshold.
        below = mix(at(0.996))
        self.assertNotEqual(below, (0.8 * 0.8, 0.4 * 0.4, 0.2 * 0.2))
        self.near(below, (0.64, 0.16, 0.04), places=2)
        s, nx = 0.996, math.sqrt(1 - at(0.996) ** 2)
        self.assertAlmostEqual(below[2], s * 0.04 + (1 - s) * nx / (nx + 0.0001) * 0.81, places=7)
        # At the threshold itself the block is skipped (the game jumps when not threshold > s): with
        # the slope constants (0, 1), s is |n.z| exactly.
        edge = dict(self.inp, slope=[0.0, 1.0])
        self.assertEqual(tr.steep_mix(edge, (0.0, 0.0, 0.99609375), self.TF0, self.P, self.Q), (0.8 * 0.8, 0.4 * 0.4, 0.2 * 0.2))
        self.assertNotEqual(tr.steep_mix(edge, (0.0, 1.0, 0.996), self.TF0, self.P, self.Q)[2], 0.2 * 0.2)

    def test_layer_colour_with_and_without_the_block(self):
        layer = tr.Layer(self.inp, solid(self.TF0), None, solid((1.0,) * 4), tr.BLEND_REPLACE, side=self.side)
        plain = tr.Layer(self.inp, solid(self.TF0), None, solid((1.0,) * 4), tr.BLEND_REPLACE)
        steep = ((1.0, 0.0, 0.0), 20.0)       # the normal and the height of the pixel
        self.near(tr.layer_colour(layer, 92.0, 148.0, base=False, steep=steep), (0.0899910, 0.3599640, 0.8099190))
        # y picks the texel of o0 only: at y = 156 its u is 9.75, texel (1, 1) = Q.
        self.near(tr.layer_colour(layer, 92.0, 156.0, base=False, steep=steep), [q * q / 1.0001 for q in self.Q[:3]])
        # The height moves both: at height 28 v is -1.75, row 0 of the side texture (black).
        self.near(tr.layer_colour(layer, 92.0, 148.0, base=False, steep=((1.0, 0.0, 0.0), 28.0)), (0.0, 0.0, 0.0))
        # A layer without a side texture (the shaders paired with FB68), or a replay without the block.
        self.near(tr.layer_colour(plain, 92.0, 148.0, base=False, steep=steep), (0.64, 0.16, 0.04))
        self.near(tr.layer_colour(layer, 92.0, 148.0, base=False), (0.64, 0.16, 0.04))
        rgb, _ = tr.shade_point([layer], 92.0, 148.0, None, fade_on=False, base=False, steep=steep)
        self.near(rgb, (0.0899910, 0.3599640, 0.8099190))

    def test_grid_interpolation_follows_the_pass_triangles(self):
        # Corner values a (0, 0), b (1, 0), c (1, 1), d (0, 1); the grid's quads are cut from a to c.
        a, b, c, d = (0.0,), (1.0,), (5.0,), (2.0,)
        self.assertEqual(tr.grid_interp(a, b, c, d, 0.0, 0.0), (0.0,))
        self.assertEqual(tr.grid_interp(a, b, c, d, 1.0, 1.0), (5.0,))
        self.assertEqual(tr.grid_interp(a, b, c, d, 0.5, 0.5), (2.5,))            # on the diagonal: (a + c) / 2
        self.assertEqual(tr.grid_interp(a, b, c, d, 0.75, 0.25), (1.75,))         # triangle a b c: 0.75 * 1 + 0.25 * 4
        self.assertEqual(tr.grid_interp(a, b, c, d, 0.25, 0.75), (2.25,))         # triangle a c d: 0.25 * 3 + 0.75 * 2
        # (bilinear would give 1.6875 and 1.9375)


def png_size_and_pixels(path):
    raw = Path(path).read_bytes()
    w, h = struct.unpack(">II", raw[16:24])
    at = raw.index(b"IDAT")
    size = struct.unpack(">I", raw[at - 4:at])[0]
    data = zlib.decompress(raw[at + 4:at + 4 + size])
    rows = [data[y * (w * 4 + 1) + 1:(y + 1) * (w * 4 + 1)] for y in range(h)]
    return w, h, rows


class LegendTest(unittest.TestCase):
    def test_first_sixteen_legend_colours_are_visibly_distinct(self):
        pal = tr._PALETTE
        self.assertGreaterEqual(len(pal), 16)
        self.assertEqual(len(set(pal)), len(pal))
        others = [tr.UNCOVERED, (0, 0, 0)]    # the map's own marks: uncovered and the black background
        for i in range(16):
            for j in list(range(i)) + others:
                other = pal[j] if isinstance(j, int) else j
                d = math.dist(pal[i], other)
                self.assertGreaterEqual(d, 60, (i, j, pal[i], other, d))


class CaptureTest(unittest.TestCase):
    """The tool on a capture: tess rows, texture rows and dump files as discovery writes them."""

    def _capture(self, root, pconst=True, with_tf0=True, height_bytes=None, detail_format=6, other_sampler=False,
                 steep=None):
        """Two layer draws of one square. Without `steep` they have the shader pair without the
        steep-ground block (FB68 with 4755). With steep = (x, y) (byte values of every texel of a 4 x 4
        normal map) they have C30A with 9C21 and what the block reads: tf13 (the side texture), tf17
        (the normal map), pixel constant c45 and the shader's literals. steep = "rows": a 16 x 16
        normal map whose texel rows 1, 5, 9 and 13 are (230, 128) and whose other rows are (128, 128)."""
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
        # Heightmap 4 x 4 k_16, point sampled: every texel 0.2, so the height is 0.2 * 90 = 18.
        hdata = dump_k16(4, 4, lambda x, y: 13107)
        hgt = add("h.bin", hdata if height_bytes is None else hdata[:height_bytes],
                  fc_for(24, 4, 4, linear=False, clamp=2, base=0x1A500000))
        if detail_format != 6:   # a texture row of another format under detail B
            b = add("other.bin", bytes(64), fc_for(detail_format, 4, 4, base=0x1A600000))
        if other_sampler:        # the tess rows name detail A with other clamp and filter bits than its dump row
            a = [f"0x{x:08X}" for x in fc_for(6, 2, 2, endian=2, base=0x1A100000, clamp=2, linear=False)]
        side = nrm = None
        if steep is not None:
            # Side texture 4 x 2, point sampled: row 1 is black, black, P = (77, 153, 230), Q = (128, 64, 255).
            texels = ((0, 0, 0, 255), (0, 0, 0, 255), (77, 153, 230, 255), (128, 64, 255, 255))
            side = add("side.bin", dump_8888(4, 2, lambda x, y: texels[x] if y else (0, 0, 0, 255)),
                       fc_for(6, 4, 2, endian=2, linear=False, base=0x1A700000))
            if steep == "rows":   # x: end values 230 and 128, the texels of a block's row 1 with index 0
                block = bc4_block(230, 128, lambda i: 0 if i // 4 == 1 else 1) + flat_dxn(128, 128)[8:]
                nrm = add("n.bin", dump_dxn(16, 16, lambda bx, by: block), fc_for(49, 16, 16, linear=False, clamp=2, base=0x1A800000))
            else:
                nrm = add("n.bin", dump_dxn(4, 4, lambda bx, by: flat_dxn(*steep)), fc_for(49, 4, 4, linear=False, clamp=2, base=0x1A800000))
        rows = [{"kind": "frame", "frame": 1}]
        for name, fc in files.items():
            rows.append({"kind": "texture", "file": f"native_tex_x/{name}", "bytes": 1, "fc": [f"0x{x:08X}" for x in fc]})

        def tess(consts, blend, detail):
            r = {"kind": "tess", "frame": 1, "func": "DrawIndx:8221C9C8",
                 "args": ["0x00000012", "0x00000003", "0x00000001", "0x00000000"], "in_scene": True,
                 "rs": {"2201": blend, "bin_mask_lo": "0x0003000A"},
                 "vs": {"hash": "0xFB68A7F2301210E1"}, "ps_hash": "0x4755A2E4A05BA678",
                 "consts": dict(consts, c0=[0.01, 0, 0, -1], c1=[0, 0.01, 0, -1.5], c2=[0, 0, 1, 0], c3=[0, 0, 0, 1]),
                 "tf": {"3": base, "16": hgt, "18": wgt}}
            if with_tf0:
                r["tf"]["0"] = detail
            if pconst:
                r["pconst"] = PCONST
            if steep is not None:
                r["vs"], r["ps_hash"] = {"hash": "0xC30A97D946FA2BE4"}, "0x9C2144E42E95A76F"
                r["tf"].update({"13": side, "17": nrm})
                r["pc"], r["ps_lit"] = STEEP_PC, STEEP_LIT_9C21
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

    def test_default_replay_reads_the_heightmap_and_refuses_a_cut_dump(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root)
            out = root / "patch.png"
            tr.main([str(cap), "--frame", "1", "--patch", "0", "--out", str(out), "--res", "16", "--encode", "linear"])
            _, _, rows = png_size_and_pixels(out)
            # Height 18 under the eye at (100, 160, 30): d2 is far below 2500, the fade is 1, and the pixel
            # is the one of the replay without the fade.
            a2 = [(c / 255) ** 2 for c in (204, 102, 51)]
            b2 = [(c / 255) ** 2 for c in (128, 128, 255)]
            bp = [(c / 255) ** 2 * t for c, t in zip((128, 255, 64), (1.5, 1.0, 0.5))]
            self.assertEqual(list(rows[8][0:3]), [round(255 * min(1.0, (a2[k] + b2[k] * 8.5 / 14) * bp[k])) for k in range(3)])
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, height_bytes=100)    # the level needs 3 * 64 + 8 = 200 bytes
            with self.assertRaisesRegex(tr.MissingInput, r"heightmap.*has 100 bytes.*needs 200"):
                tr.main([str(cap), "--frame", "1", "--patch", "0", "--out", str(root / "p.png")])
            # Without the fade the heightmap is not read and the replay goes through.
            tr.main([str(cap), "--frame", "1", "--patch", "0", "--out", str(root / "p.png"), "--no-fade"])

    def test_check_dumps_lists_every_texture_the_rows_name(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False)
            lines = tr.main([str(cap), "--frame", "1", "--check-dumps"])
            # Detail A and B, the base map, the heightmap and the weight map: five textures, all whole.
            self.assertEqual(sum(" ok" in line for line in lines), 5, lines)
            self.assertIn("0 of 5 textures short, missing or of unknown size", lines[-1])
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, height_bytes=100, detail_format=49)
            (root / "native_tex_x" / "w.bin").unlink()
            lines = tr.main([str(cap), "--frame", "1", "--check-dumps"])
            text = chr(10).join(lines)
            self.assertRegex(text, r"tf16 0x1A500058 format 24 4 x 4: SHORT, 100 of 200 bytes")
            self.assertRegex(text, r"tf18 0x1A40007A format 58 66 x 33: NO DUMP FILE")
            # k_DXN: 4 x 4 is one 16-byte block; the file has 64.
            self.assertRegex(text, r"tf0 0x1A600071 format 49 4 x 4: ok, 64 bytes \(needs 16\)")
            self.assertIn("2 of 5 textures short, missing or of unknown size", lines[-1])

    def _edit_rows(self, cap, edit):
        rows = [json.loads(line) for line in cap.read_text().splitlines()]
        edit(rows)
        cap.write_text("".join(json.dumps(r) + chr(10) for r in rows))

    def test_check_dumps_counts_only_textures_the_replay_reads(self):
        fog = fc_for(29, 64, 1, base=0x1B000000)      # like the session-1 fog tables: no texture row
        fog2 = fc_for(6, 256, 1, base=0x1B100000)
        sun = fc_for(2, 1120, 720, base=0x1B200000)   # a format whose size this tool does not know; has a row
        hx = lambda fc: [f"0x{x:08X}" for x in fc]

        def unneeded(rows):
            rows.insert(1, {"kind": "texture", "file": "native_tex_x/sun.bin", "bytes": 1, "fc": hx(sun)})
            last = [r for r in rows if r.get("kind") == "tess"][-1]
            last["tf"].update({"4": hx(fog), "5": hx(fog2), "8": hx(sun)})

        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False)
            (root / "native_tex_x" / "sun.bin").write_bytes(bytes(100))
            self._edit_rows(cap, unneeded)
            lines = tr.main([str(cap), "--frame", "1", "--check-dumps"])
            text = chr(10).join(lines)
            self.assertEqual(sum("not needed by the replay" in line for line in lines), 3, lines)
            self.assertRegex(text, r"tf4 0x1B00005D format 29 64 x 1: .*not needed by the replay")
            self.assertRegex(text, r"tf8 0x1B200042 format 2 1120 x 720: .*not needed by the replay")
            self.assertEqual(lines[-1], "0 of 5 textures short, missing or of unknown size")

        # The same texture named by a slot the replay reads is needed: missing, it counts.
        def needed(rows):
            last = [r for r in rows if r.get("kind") == "tess"][-1]
            last["tf"].update({"4": hx(fog), "0": hx(fog)})

        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False)
            self._edit_rows(cap, needed)
            lines = tr.main([str(cap), "--frame", "1", "--check-dumps"])
            self.assertTrue(any("tf0 0x1B00005D" in line and line.endswith("NO TEXTURE ROW") for line in lines), lines)
            self.assertEqual(lines[-1], "1 of 5 textures short, missing or of unknown size", lines)

    def test_check_dumps_far_rows_are_read_only_in_the_colour_pass(self):
        miss = fc_for(6, 4, 4, base=0x1C000000)
        hx = [f"0x{x:08X}" for x in miss]

        def far(mask):
            def edit(rows):
                rows.append({"kind": "tess", "frame": 1, "func": "DrawIndx:82207C30", "in_scene": True,
                             "args": ["0x0", "0x0", "0x0", "0x1"], "rs": {"bin_mask_lo": mask}, "consts": {},
                             "tf": {"13": hx}})
            return edit

        for mask, want in (("0x00000001", "0 of 5"), ("0x2AAAAAAA", "1 of 6")):
            with tempfile.TemporaryDirectory() as d:
                root = Path(d)
                cap = self._capture(root, pconst=False)
                self._edit_rows(cap, far(mask))
                lines = tr.main([str(cap), "--frame", "1", "--check-dumps"])
                self.assertTrue(lines[-1].startswith(want + " textures"), (mask, lines))

    def test_texture_is_found_by_address_format_and_size(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, other_sampler=True)
            out = root / "patch.png"
            lines = tr.main([str(cap), "--frame", "1", "--patch", "0", "--out", str(out), "--res", "16", "--no-fade"])
            self.assertTrue(any("2 layer draws" in line for line in lines), lines)

    def test_unsupported_format_is_refused_by_name_and_exit_code(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, detail_format=2)
            argv = [str(cap), "--frame", "1", "--patch", "0", "--out", str(root / "p.png"), "--no-fade"]
            with self.assertRaisesRegex(tr.MissingInput, r"tf0 \(detail colour\).*format 2"):
                tr.main(argv)
            err = io.StringIO()
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(tr.cli(argv), 2)
            self.assertIn("cannot replay", err.getvalue())
            self.assertNotIn("Traceback", err.getvalue())
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(tr.cli([str(self._capture(Path(tempfile.mkdtemp()))), "--frame", "1", "--list"]), 0)

    def test_weight_sums_cull_edges_and_locate(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False)
            lines = tr.main([str(cap), "--frame", "1", "--weight-sums"])
            # One square whose first layer replaces: at its 17 x 17 grid points the base layer is 1 and the
            # second layer (texels 16 to 32 of its cell: values 9 to 15) between 8/14 and 1: all above 1.1.
            text = " ".join(lines)
            self.assertIn("1 squares whose first layer replaces", text)
            self.assertIn("below 0.9: 0, 0.9 to 0.98: 0, 0.98 to 1.02: 0, 1.02 to 1.1: 0, above 1.1: 289", text)
            self.assertIn("0 squares whose first layer adds", text)
            # Cull edges (printed with the weight map) over the two 17 x 17 blocks the frame uses: the
            # texels of this sub-patch (16 to 32 of each cell) hold no 0: 0 of 2 * 2 * 17 * 16 pairs.
            lines = tr.main([str(cap), "--frame", "1", "--weights", str(root / "w.png")])
            self.assertTrue(any("cull edges: 0 of 1088 neighbouring texel pairs in the 2 17 x 17" in line for line in lines), lines)
            lines = tr.main([str(cap), "--frame", "1", "--patch", "0", "--locate"])
            # c0 = (0.01, 0, 0, -1), c1 = (0, 0.01, 0, -1.5), w = 1: corner (88, 152) is ndc (-0.12, 0.02):
            # pixel (492.8, 352.8); corner (96, 160) is ndc (-0.04, 0.10): pixel (537.6, 324.0).
            text = " ".join(lines)
            self.assertIn("(88, 152) -> (492.8, 352.8)", text)
            self.assertIn("(96, 160) -> (537.6, 324.0)", text)

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

    def _steep_pixel(self, nxy, wx, wy, h):
        """The first layer's linear colour at a world point, by the formulas of the frame map (not by the
        tool's functions): normal from the map's byte pair, c45 and the literals of STEEP_*."""
        n = [2 * v / 255 - 1 for v in nxy]
        nz = math.sqrt(abs(1 - n[0] ** 2 - n[1] ** 2))
        s = min(max(nz * 10.0019045 - 7.66190386, 0.0), 1.0)
        a2 = [(c / 255) ** 2 for c in (204, 102, 51)]
        if s >= 0.99609375:
            return a2
        den = abs(n[0]) + abs(n[1]) + 9.99999975e-05
        side = lambda u: [(c / 255) ** 2 for c in ((0, 0, 0), (0, 0, 0), (77, 153, 230), (128, 64, 255))[math.floor((u % 1.0) * 4)]]
        self.assertGreaterEqual((-0.0625 * h) % 1.0, 0.5)        # row 1 of the side texture
        p0, p1 = side(0.0625 * wy), side(0.0625 * wx)
        return [s * a2[k] + (1 - s) * (abs(n[1]) / den * p1[k] + abs(n[0]) / den * p0[k]) for k in range(3)]

    def test_replay_evaluates_the_steep_block(self):
        # The square is (88, 152) 8 x 8, the height 18 everywhere (-c8.x * h = -1.125: row 1 of the side
        # texture). Pixel (i, j) of a 16 x 16 picture is the world point (88.25 + i / 2, 152.25 + j / 2):
        # o0's u = y / 16 is on texel 2 (P) for j < 8 and on texel 3 (Q) from j = 8; o1's u = x / 16 the
        # same by i. Normal map texels (230, 128): n = (0.804, 0.004, 0.595): s = 0, nearly all weight
        # on o0 (wx = 0.995, wy = 0.005).
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, steep=(230, 128))
            out = root / "patch.png"
            argv = [str(cap), "--frame", "1", "--patch", "0", "--out", str(out), "--res", "16", "--no-fade",
                    "--no-base", "--encode", "linear"]
            lines = tr.main(argv)
            self.assertTrue(any("2 of them with the tf13 blend on steep ground, evaluated" in line for line in lines), lines)
            _, _, rows = png_size_and_pixels(out)
            a2 = [(c / 255) ** 2 for c in (204, 102, 51)]
            b2 = [(c / 255) ** 2 for c in (128, 128, 255)]
            seen = set()
            for i, j in ((0, 8), (15, 8), (6, 2), (9, 13), (3, 0), (12, 5)):
                wx, wy = 88.25 + i / 2, 152.25 + j / 2
                first = self._steep_pixel((230, 128), wx, wy, 18.0)
                # The second layer (weight texels 16 + i and 17 + i: (8.5 + i) / 14, 1 from column 6 on)
                # has the block too, with its own detail colour B: s is 0, so both are the side colour.
                self.assertEqual(self._steep_pixel((230, 128), wx, wy, 18.0), first)
                w2 = min(1.0, (8.5 + i) / 14)
                want = [round(255 * min(1.0, first[k] * (1 + w2))) for k in range(3)]
                self.assertEqual(list(rows[j][i * 4:i * 4 + 3]), want, (i, j))
                seen.add(tuple(want))
            self.assertGreater(len(seen), 3)
            # o0 depends on y: rows 2 and 13 of column 0 fall on different texels of the side texture.
            self.assertNotEqual(rows[2][0:3], rows[13][0:3])
            # --no-steep: the old output, every pixel the detail colours alone.
            lines = tr.main(argv + ["--no-steep"])
            self.assertTrue(any("2 of them with the tf13 blend on steep ground, not evaluated" in line for line in lines), lines)
            _, _, rows = png_size_and_pixels(out)
            self.assertEqual(list(rows[8][0:3]), [round(255 * min(1.0, a2[k] + b2[k] * 8.5 / 14)) for k in range(3)])
            self.assertEqual(rows[2][0:3], rows[13][0:3])

    def test_replay_mixes_detail_and_side_colour_on_a_35_degree_slope(self):
        # Texels (201, 128): n = (0.5765, 0.0039, 0.8171): s = sat(8.1724 - 7.6619) = 0.5105.
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, steep=(201, 128))
            out = root / "patch.png"
            tr.main([str(cap), "--frame", "1", "--patch", "0", "--out", str(out), "--res", "16", "--no-fade",
                     "--no-base", "--encode", "linear"])
            _, _, rows = png_size_and_pixels(out)
            n = [2 * 201 / 255 - 1, 2 * 128 / 255 - 1]
            s = math.sqrt(1 - n[0] ** 2 - n[1] ** 2) * 10.0019045 - 7.66190386
            self.assertAlmostEqual(s, 0.5105, places=3)
            a2 = [(c / 255) ** 2 for c in (204, 102, 51)]
            b2 = [(c / 255) ** 2 for c in (128, 128, 255)]
            for i, j in ((0, 8), (15, 3)):
                wx, wy = 88.25 + i / 2, 152.25 + j / 2
                first = self._steep_pixel((201, 128), wx, wy, 18.0)
                second = [f + s * (b - a) for f, a, b in zip(first, a2, b2)]      # the same side part, detail B
                w2 = min(1.0, (8.5 + i) / 14)
                want = [round(255 * min(1.0, first[k] + w2 * second[k])) for k in range(3)]
                self.assertEqual(list(rows[j][i * 4:i * 4 + 3]), want, (i, j))

    def test_flat_ground_is_the_same_with_and_without_the_block(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, steep=(128, 128))     # n = (0.004, 0.004, 1): s = 1, the block is skipped
            a, b = root / "a.png", root / "b.png"
            argv = [str(cap), "--frame", "1", "--patch", "0", "--res", "16", "--encode", "linear"]
            tr.main(argv + ["--out", str(a)])
            tr.main(argv + ["--out", str(b), "--no-steep"])
            self.assertEqual(png_size_and_pixels(a), png_size_and_pixels(b))

    def test_steep_replay_names_what_a_capture_lacks(self):
        def drop(key, sub=None):
            def edit(rows):
                for r in rows:
                    if r.get("kind") == "tess":
                        (r[key].pop(sub) if sub else r.pop(key))
            return edit

        for edit, text in ((drop("tf", "13"), r"tf13 \(side colour\)"), (drop("tf", "17"), r"tf17 \(normal map\)"),
                           (drop("pc"), "c45"), (drop("ps_lit"), "ps_lit")):
            with tempfile.TemporaryDirectory() as d:
                root = Path(d)
                cap = self._capture(root, steep=(230, 128))
                self._edit_rows(cap, edit)
                argv = [str(cap), "--frame", "1", "--patch", "0", "--out", str(root / "p.png"), "--no-fade"]
                with self.assertRaisesRegex(tr.MissingInput, text + r".*--no-steep"):
                    tr.main(argv)
                tr.main(argv + ["--no-steep"])      # the old replay does not need it
        # The block needs the height even without the fade: a cut heightmap is refused, not read as zero.
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, steep=(230, 128), height_bytes=100)
            argv = [str(cap), "--frame", "1", "--patch", "0", "--out", str(root / "p.png"), "--no-fade"]
            with self.assertRaisesRegex(tr.MissingInput, r"heightmap.*has 100 bytes"):
                tr.main(argv)
            tr.main(argv + ["--no-steep"])

    def test_check_dumps_counts_the_block_textures_of_steep_rows(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False, steep=(230, 128))
            (root / "native_tex_x" / "n.bin").write_bytes(bytes(8))
            lines = tr.main([str(cap), "--frame", "1", "--check-dumps"])
            text = chr(10).join(lines)
            self.assertRegex(text, r"tf17 0x1A800071 format 49 4 x 4: SHORT, 8 of 16 bytes")
            self.assertRegex(text, r"tf13 0x1A700086 format 6 4 x 2: ok")
            self.assertEqual(lines[-1], "1 of 7 textures short, missing or of unknown size")

    def test_steep_share(self):
        # Every texel of the synthetic normal map is the same, so all 289 grid points of the square agree.
        # (201, 128): n = (0.576, 0.004, 0.817): s = 0.51. (230, 128): n.z = 0.595: s = 0.
        for texel, below, zero in (((128, 128), 0, 0), ((230, 128), 289, 289), ((201, 128), 289, 0)):
            with tempfile.TemporaryDirectory() as d:
                root = Path(d)
                cap = self._capture(root, pconst=False, steep=texel)
                lines = tr.main([str(cap), "--frame", "1", "--steep-share"])
                text = " ".join(lines)
                self.assertIn("1 of 1 squares have a layer with the steep-ground block", text)
                self.assertIn(f"s below 0.99609375 at {below} of 289 grid points", text)
                self.assertIn(f"s = 0 at {zero} ", text)
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False)          # no shader of the frame has the block
            with self.assertRaisesRegex(tr.MissingInput, "steep-ground block"):
                tr.main([str(cap), "--frame", "1", "--steep-share"])
        # The normal is read at the height coordinate, (world - c47.zw) * c47.xy: the square's grid points
        # (x 88 to 96, y 152 to 160, offset (16, 32), scale 1 / 384) all fall on texel (3, 5) of a 16 x 16
        # map, in a steep row; at the unshifted position they would fall on row 6, which is flat.
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False, steep="rows")
            text = " ".join(tr.main([str(cap), "--frame", "1", "--steep-share"]))
            self.assertIn("s below 0.99609375 at 289 of 289 grid points", text)
            self.assertIn("s = 0 at 289 ", text)

    def test_replay_names_what_an_old_capture_lacks(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            cap = self._capture(root, pconst=False, with_tf0=False)
            with self.assertRaisesRegex(tr.MissingInput, "pconst"):
                tr.main([str(cap), "--frame", "1", "--patch", "0", "--out", str(root / "p.png")])


if __name__ == "__main__":
    unittest.main()
