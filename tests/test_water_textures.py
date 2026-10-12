"""The lake's textures (frame-map section 20, "The water's textures"): the Python twins of render::DecodeK8Level,
render::DecodeDxnLevelRg8 and render::WaterTextureFetch (src/native/render/texture_decode.h), on made inputs and,
where a discovery capture's dumps and the C++ test's outputs are on disk, on the real bytes."""
import contextlib
import importlib.util
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))
SPEC = importlib.util.spec_from_file_location("water_textures", ROOT / "tools" / "xdk_sigmatch" / "water_textures.py")
wt = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(wt)
import texture_thumb as tt  # noqa: E402  (its tiled_offset_2d is the scalar port the vector one is held to)


def bc4(a0, a1, index):
    """Eight bytes of a BC4 block from the format's definition: two end values, then texel i's 3-bit index at bit
    3 * i of the following 48 bits (the packing of tests/native/test_texture_decode.cpp)."""
    bits = 0
    for i in range(16):
        bits |= (index(i) & 7) << (3 * i)
    return bytes([a0, a1]) + bits.to_bytes(6, "little")


MASK_FC = ["0x80804802", "0x1BD03002", "0x0007E03F", "0x00A81400", "0x00000003", "0x00000218"]      # CAP 155547, slot 1
SMALL_FC = ["0x80404802", "0x1B6D0002", "0x0001E007", "0x00A81400", "0x00000003", "0x00000218"]     # the 8 x 16 mask
NORMAL_FC = ["0x82000002", "0x14D40071", "0x001FE0FF", "0x00A80D10", "0x00000203", "0x14D50A18"]    # slot 2
PICTURE_FC = ["0x82000002", "0x13401086", "0x001FE0FF", "0x00A80C14", "0x00000003", "0x00000218"]   # slot 13


class DecodeK8(unittest.TestCase):
    def test_the_five_texels_of_the_native_test(self):
        # tests/native/test_texture_decode.cpp, returns 223 and 224: the same texels at the same tiled places
        src = bytearray(16384)
        at = lambda x, y: tt.tiled_offset_2d(x, y, 64, 0)   # noqa: E731
        self.assertEqual((at(0, 0), at(1, 0)), (0, 1))
        for (x, y), v in (((0, 0), 7), ((1, 0), 255), ((0, 1), 31), ((33, 2), 99), ((63, 63), 200)):
            src[at(x, y)] = v
        out = wt.decode_k8(bytes(src), 64, 64, 64, True)
        self.assertEqual((out.shape, out.dtype), ((64, 64), np.uint8))
        self.assertEqual([int(out[0, 0]), int(out[0, 1]), int(out[1, 0]), int(out[2, 33]), int(out[63, 63]), int(out[0, 2])], [7, 255, 31, 99, 200, 0])

    def test_every_texel_at_the_scalar_ports_place(self):
        for pitch in (64, 128):
            src = bytearray(16384)
            want = np.zeros((64, 64), dtype=np.uint8)
            for y in range(64):
                for x in range(64):
                    want[y, x] = 1 + (x * 7 + y * 13) % 255
                    src[tt.tiled_offset_2d(x, y, pitch, 0)] = want[y, x]
            self.assertTrue(np.array_equal(wt.decode_k8(bytes(src), 64, 64, pitch, True), want))

    def test_a_short_source_raises(self):
        src = bytes(16384)
        with self.assertRaises(ValueError):
            wt.decode_k8(src[:tt.tiled_offset_2d(63, 63, 64, 0)], 64, 64, 64, True)   # the last texel outside
        with self.assertRaises(ValueError):
            wt.decode_k8(src[:4095], 64, 64, 64, True)
        wt.decode_k8(src[:4096], 64, 64, 64, True)
        # at pitch 128 the texels' number of bytes is not enough: the bound is each texel's own
        top = max(tt.tiled_offset_2d(x, y, 128, 0) for y in range(64) for x in range(64))
        self.assertGreater(top, 4096)
        with self.assertRaises(ValueError):
            wt.decode_k8(src[:top], 64, 64, 128, True)
        wt.decode_k8(src[:top + 1], 64, 64, 128, True)
        for args in ((0, 64, 64, True), (64, 0, 64, True), (64, 64, 32, True), (8193, 1, 8224, False), (1, 8193, 32, False)):
            with self.assertRaises(ValueError):
                wt.decode_k8(src, *args)

    def test_linear_rows_of_the_pitch_aligned_to_32(self):
        lin = bytearray(32 * 16)
        lin[32 * 3 + 5] = 77
        lin[32 * 15 + 7] = 5
        out = wt.decode_k8(bytes(lin), 8, 16, 8, False)
        self.assertEqual((out.shape, int(out[3, 5]), int(out[15, 7]), int(out[0, 0])), ((16, 8), 77, 5, 0))
        wt.decode_k8(bytes(lin[:32 * 15 + 8]), 8, 16, 8, False)
        with self.assertRaises(ValueError):
            wt.decode_k8(bytes(lin[:32 * 15 + 7]), 8, 16, 8, False)


class DecodeDxnRg8(unittest.TestCase):
    def test_the_native_tests_block(self):
        blk = bc4(255, 0, lambda i: 0) + bc4(0, 128, lambda i: 1)
        self.assertEqual(blk, bytes([255, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24]))   # the plan's literal bytes
        out = wt.decode_dxn_rg8(blk, 4, 4, 1, False, 0)
        self.assertEqual((out.shape, out.dtype), ((4, 4, 2), np.uint8))
        self.assertTrue((out[..., 0] == 255).all() and (out[..., 1] == 128).all())
        with self.assertRaises(ValueError):
            wt.decode_dxn_rg8(blk[:15], 4, 4, 1, False, 0)
        with self.assertRaises(ValueError):
            wt.decode_dxn_rg8(blk, 0, 4, 1, False, 0)

    def test_both_palettes_rounded_to_nearest(self):
        # whole-number arithmetic that shares nothing with the decoder's: n / d rounded to nearest is (2 n + d) // (2 d)
        blk = bc4(200, 61, lambda i: i % 8) + bc4(10, 251, lambda i: i % 8)
        out = wt.decode_dxn_rg8(blk, 4, 4, 1, False, 0)
        for i in range(16):
            k = i % 8
            nx = 200 * 7 if k == 0 else 61 * 7 if k == 1 else (8 - k) * 200 + (k - 1) * 61
            ny = 10 * 5 if k == 0 else 251 * 5 if k == 1 else (6 - k) * 10 + (k - 1) * 251 if k < 6 else 0 if k == 6 else 255 * 5
            self.assertEqual((int(out[i // 4, i % 4, 0]), int(out[i // 4, i % 4, 1])), ((2 * nx + 7) // 14, (2 * ny + 5) // 10), i)
        # against the terrain replay's independent transcription of the block (floats, not rounded)
        import terrain_replay as tr
        xs, ys = tr.bc4_values(blk[0:8]), tr.bc4_values(blk[8:16])
        for i in range(16):
            self.assertEqual(int(out[i // 4, i % 4, 0]), int(xs[i] + 0.5))
            self.assertEqual(int(out[i // 4, i % 4, 1]), int(ys[i] + 0.5))

    def test_blocks_tiled_linear_and_swapped(self):
        # an 8 x 8 level of four blocks, block (bx, by) flat at (10 + 20 bx + 40 by, 200 - 3 bx - 5 by), stored 8-in-16
        def blocks(place, size):
            src = bytearray(size)
            for by in range(2):
                for bx in range(2):
                    vx, vy = 10 + 20 * bx + 40 * by, 200 - 3 * bx - 5 * by
                    raw = bytes([vx, vx]) + bytes(6) + bytes([vy, vy]) + bytes(6)
                    swapped = bytes(raw[i ^ 1] for i in range(16))
                    src[place(bx, by):place(bx, by) + 16] = swapped
            return bytes(src)
        want = np.zeros((8, 8, 2), dtype=np.uint8)
        for y in range(8):
            for x in range(8):
                want[y, x] = (10 + 20 * (x // 4) + 40 * (y // 4), 200 - 3 * (x // 4) - 5 * (y // 4))
        lin = blocks(lambda bx, by: by * 32 * 16 + bx * 16, 32 * 16 + 32)
        self.assertTrue(np.array_equal(wt.decode_dxn_rg8(lin, 8, 8, 2, False, 1), want))
        tiled = blocks(lambda bx, by: tt.tiled_offset_2d(bx, by, 2, 4), 4096)
        self.assertTrue(np.array_equal(wt.decode_dxn_rg8(tiled, 8, 8, 2, True, 1), want))
        # a level at a block origin inside a packed tail (the stored levels 16 texels and below)
        tail = blocks(lambda bx, by: tt.tiled_offset_2d(4 + bx, 1 + by, 32, 4), 16384)
        self.assertTrue(np.array_equal(wt.decode_dxn_rg8(tail, 8, 8, 32, True, 1, origin=(4, 1)), want))
        # read without the swap the two end values change places: with equal ends nothing shows, so one block's differ
        one = bytes([200, 60]) + bytes(6) + bytes(8)
        guest = bytes(one[i ^ 1] for i in range(16))
        self.assertEqual(int(wt.decode_dxn_rg8(guest, 4, 4, 1, False, 1)[0, 0, 0]), 200)
        self.assertEqual(int(wt.decode_dxn_rg8(guest, 4, 4, 1, False, 0)[0, 0, 0]), 60)


class Fetch(unittest.TestCase):
    def test_the_captured_constants(self):
        m = wt.fetch(MASK_FC, "k8")
        self.assertEqual((m["width"], m["height"], m["pitch"], m["tiled"], m["endian"], m["base"], m["clamp"], m["swizzle"], m["levels"]),
                         (64, 64, 64, True, 0, 0x1BD03000, (2, 2), (0, 0, 0, 5), 1))
        s = wt.fetch(SMALL_FC, "k8")
        self.assertEqual((s["width"], s["height"], s["pitch"], s["tiled"]), (8, 16, 32, True))
        n = wt.fetch(NORMAL_FC, "dxn")
        self.assertEqual((n["width"], n["height"], n["pitch"], n["tiled"], n["endian"], n["base"], n["mip"], n["mip_max"], n["packed"], n["levels"]),
                         (256, 256, 256, True, 1, 0x14D40000, 0x14D50000, 8, True, 9))
        self.assertEqual((n["swizzle"], n["clamp"], n["filter"], n["sign"]), ((0, 1, 2, 3), (0, 0), ("linear", "linear", "linear"), (0, 0, 0, 0)))

    def test_refusals_as_the_cpp_reader(self):
        for fc, kind in ((MASK_FC, "dxn"), (NORMAL_FC, "k8"), (PICTURE_FC, "k8"), (PICTURE_FC, "dxn")):
            with self.assertRaises(ValueError):
                wt.fetch(fc, kind)
        big = list(MASK_FC)
        big[2] = "0x%08X" % (511 | (511 << 13))
        big[0] = "0x%08X" % (2 | (16 << 22) | (1 << 31))
        self.assertEqual(wt.fetch(big, "k8")["width"], 512)       # (ledger ruling 42: no size a 2D constant can name is too large)
        short = list(MASK_FC)
        short[0] = "0x%08X" % (2 | (1 << 22) | (1 << 31))         # a pitch of 32 texels under a width of 64
        with self.assertRaises(ValueError):
            wt.fetch(short, "k8")
        cube = list(NORMAL_FC)
        cube[5] = "0x14D50E18"
        with self.assertRaises(ValueError):
            wt.fetch(cube, "dxn")
        with self.assertRaises(ValueError):
            wt.fetch(NORMAL_FC, "other")

    def test_the_unrounded_values_round_to_the_bytes(self):
        """Ledger ruling 32: the game's fetch reads BC5 palette entries that are not rounded to 8 bits; the replay
        samples those (rounded=False) and the 8-bit decode stays the reference of the tests."""
        block = bytes([200, 13, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA, 13, 200, 0x53, 0x97, 0x2E, 0x53, 0x97, 0x2E])
        whole = wt.decode_dxn_rg8(block, 4, 4, 1, False, 0)
        exact = wt.decode_dxn_rg8(block, 4, 4, 1, False, 0, rounded=False)
        self.assertEqual(whole.dtype, np.uint8)
        self.assertEqual(exact.shape, (4, 4, 2))
        self.assertTrue(np.issubdtype(exact.dtype, np.floating))
        self.assertTrue((np.floor(exact + 0.5).astype(np.uint8) == whole).all())
        self.assertGreater(int((exact != whole).sum()), 8)            # most palette entries are not whole steps
        self.assertLessEqual(float(np.abs(exact - whole).max()), 0.5)
        # the two end values are whole in both
        self.assertIn(200.0, exact[..., 0].reshape(-1).tolist())
        self.assertIn(13.0, exact[..., 0].reshape(-1).tolist())

    def test_a_signed_biased_or_exponent_adjusted_constant_is_refused(self):
        """Ledger ruling 33 (the review of Tasks 1 to 5, I1): the dead draw's proof needs the mask's sample to be a
        number in [0, 1]; a fetch constant that delivers it signed, biased, as an integer or with an exponent adjust
        is not the water's."""
        def changed(fc, dword, clear, bits):
            words = [int(v, 16) if isinstance(v, str) else int(v) for v in fc]
            words[dword] = (words[dword] & ~clear) | bits
            return words
        for kind, fc, delivered in (("k8", MASK_FC, 1), ("dxn", NORMAL_FC, 2)):
            wt.fetch(fc, kind)                                    # the captured constant is inside the rule
            for component in range(delivered):
                for sign in (1, 2, 3):                            # signed, unsigned biased, gamma
                    with self.assertRaises(ValueError) as e:
                        wt.fetch(changed(fc, 0, 3 << (2 + 2 * component), sign << (2 + 2 * component)), kind)
                    self.assertIn("sign", str(e.exception))
            with self.assertRaises(ValueError) as e:
                wt.fetch(changed(fc, 3, 0x3F << 13, 1 << 13), kind)       # an exponent adjust of 1
            self.assertIn("exponent", str(e.exception))
            with self.assertRaises(ValueError) as e:
                wt.fetch(changed(fc, 3, 0x3F << 13, 0x3F << 13), kind)    # of -1
            self.assertIn("exponent", str(e.exception))
            with self.assertRaises(ValueError) as e:
                wt.fetch(changed(fc, 3, 1, 1), kind)                      # the integer number format
            self.assertIn("number format", str(e.exception))
            # a sign of a component the shader is not given (the mask's w is the constant 1) is not judged
            wt.fetch(changed(fc, 0, 3 << 8, 1 << 8), kind)
        # Ledger ruling 41 (the review of Tasks 4 to 15, I1; the reviewer's probe): the sign is the source channel's
        # that the swizzle names. The mask's x from channel 1 with channel 1 biased, the map's x from channel 2 with
        # channel 2 biased: refused. The same swizzle with the sign on channel 0, which no delivered component reads:
        # the water's.
        def moved(fc, component, channel, sign_channel, sign):
            words = changed(fc, 3, 7 << (1 + 3 * component), channel << (1 + 3 * component))
            return changed(words, 0, 3 << (2 + 2 * sign_channel), sign << (2 + 2 * sign_channel))
        for sign in (1, 2, 3):
            for kind, bad, good in (("k8", moved(MASK_FC, 0, 1, 1, sign), moved(MASK_FC, 0, 1, 0, sign)),
                                    ("dxn", moved(NORMAL_FC, 0, 2, 2, sign), moved(NORMAL_FC, 0, 2, 0, sign)),
                                    ("dxn", moved(NORMAL_FC, 1, 3, 3, sign), moved(NORMAL_FC, 1, 3, 2, sign))):
                with self.assertRaises(ValueError) as e:
                    wt.fetch(bad, kind)
                self.assertIn("sign", str(e.exception))
                wt.fetch(good, kind)
            # a constant selector has no sign: x the constant 1 with every channel's sign set
            words = changed(MASK_FC, 3, 7 << 1, 5 << 1)
            wt.fetch(changed(words, 0, 0xFF << 2, (sign | sign << 2 | sign << 4 | sign << 6) << 2), "k8")
        self.assertTrue(wt.components_unsigned(PICTURE_FC, 3))
        self.assertFalse(wt.components_unsigned(changed(PICTURE_FC, 0, 3 << 6, 3 << 6), 3))       # gamma on channel 2, which red reads
        self.assertTrue(wt.components_unsigned(changed(PICTURE_FC, 0, 3 << 8, 3 << 8), 3))        # on alpha's channel
        # any constant, whatever its format: the fields a census prints
        p = wt.describe(PICTURE_FC)
        self.assertEqual((p["format"], p["width"], p["height"], p["endian"], p["swizzle"], p["base"]), (6, 256, 256, 2, (2, 1, 0, 3), 0x13401000))

    def test_a_normal_map_with_a_lod_bias_is_refused_by_its_own_name(self):
        """The review of Tasks 11 and 12, Minor 3 (ledger ruling 46): the native surface hands the GPU the coordinates'
        derivatives as they are; a constant with a LOD bias (dword 4 bits 12 to 21) would be drawn a level off, so it is
        not the water's. The mask is sampled at level 0: its bias is not judged. The same cases as
        tests/native/test_texture_decode.cpp, 260."""
        def with_bits(fc, dword, bits):
            words = [int(v, 16) if isinstance(v, str) else int(v) for v in fc]
            words[dword] |= bits
            return words
        self.assertEqual(wt.describe(NORMAL_FC)["lod_bias"], 0.0)          # the captured constant has none
        for bias in (1, 32, 0x1FF, 0x200, 0x3FF):                           # 1/32, +1, the largest, -16, -1/32
            with self.assertRaises(ValueError) as e:
                wt.fetch(with_bits(NORMAL_FC, 4, bias << 12), "dxn")
            self.assertIn("lod-bias", str(e.exception))
            wt.fetch(with_bits(MASK_FC, 4, bias << 12), "k8")
        wt.fetch(with_bits(NORMAL_FC, 4, 0xFFF), "dxn")                     # the bits below the field
        wt.fetch(with_bits(NORMAL_FC, 4, 0xFFC00000), "dxn")                # and above it
        with self.assertRaises(ValueError) as e:                           # a sign is said first
            wt.fetch(with_bits(with_bits(NORMAL_FC, 0, 1 << 2), 4, 32 << 12), "dxn")
        self.assertIn("sign", str(e.exception))
        text = (ROOT / "src" / "native" / "render" / "texture_decode.h").read_text(encoding="utf-8")
        self.assertIn("constexpr uint32_t WaterFetchLodBias(const uint32_t fc[6]) { return (fc[4] >> 12) & 0x3FFu; }", text)
        self.assertIn("  if (kind == WaterDecode::kDxn && WaterFetchLodBias(fc) != 0) return FetchError::kLodBias;\n", text)
        self.assertIn('{"none", "not-texture", "not-2d", "format", "size", "sign", "lod-bias"};', text)

    def test_the_limits_are_the_cpp_headers(self):
        text = (ROOT / "src" / "native" / "render" / "texture_decode.h").read_text(encoding="utf-8")
        self.assertIn("inline constexpr uint32_t kXenosK8 = 2, kXenosDxn = 49;", text)
        # (ledger ruling 42: the size's bound is what a 2D constant can name, for both kinds)
        self.assertIn("inline constexpr uint32_t kWaterFetchMaxSide = 8192;", text)
        self.assertIn("if (t.width > kWaterFetchMaxSide || t.height > kWaterFetchMaxSide) return FetchError::kSize;", text)
        self.assertNotIn("? 256u : 1024u", text)
        self.assertEqual((wt.XENOS_K8, wt.XENOS_DXN, wt.FETCH_LIMIT, wt.MAX_EDGE), (2, 49, {"k8": 8192, "dxn": 8192}, 8192))
        larger = [int(v, 16) if isinstance(v, str) else int(v) for v in MASK_FC]
        larger[2] = 511 | (511 << 13)
        larger[0] = (larger[0] & ~(0x1FF << 22)) | (16 << 22)
        self.assertEqual((wt.fetch(larger, "k8")["width"], wt.fetch(larger, "k8")["height"]), (512, 512))
        # (and the sign rule's text is the channel's that the swizzle names)
        self.assertIn("const uint32_t channel = (fc[3] >> (1 + 3 * i)) & 7u;", text)
        self.assertIn("if (channel <= 3 && ((fc[0] >> (2 + 2 * channel)) & 3u) != 0) return false;", text)
        self.assertIn("for (size_t i = 0; i < xy.size(); ++i) out[i] = uint8_t(xy[i] * 255.0f + 0.5f);", text)


class MeanLevels(unittest.TestCase):
    def test_a_flat_base_and_a_split_one(self):
        flat = np.full((4, 4), 93, dtype=np.uint8)
        levels = wt.mean_levels(flat)
        self.assertEqual([lv.shape for lv in levels], [(2, 2), (1, 1)])
        self.assertTrue(all((lv == 93).all() for lv in levels))
        split = np.zeros((4, 4), dtype=np.uint8)
        split[:, 2:] = 255
        levels = wt.mean_levels(split)
        self.assertEqual(levels[0].tolist(), [[0, 255], [0, 255]])
        self.assertEqual(levels[1].tolist(), [[128]])                # 127.5: a half rounds up
        # two channels a texel, and a base that is not square
        rg = np.zeros((2, 8, 2), dtype=np.uint8)
        rg[..., 0] = 10
        rg[0, :, 1] = 100
        levels = wt.mean_levels(rg)
        self.assertEqual([lv.shape for lv in levels], [(1, 4, 2), (1, 2, 2), (1, 1, 2)])
        self.assertTrue((levels[0][..., 0] == 10).all() and (levels[0][..., 1] == 50).all())
        self.assertEqual(wt.mean_levels(np.zeros((1, 1), dtype=np.uint8)), [])
        # the sums are carried whole: three blocks of mean 0.5 and one of 0 are 1, 1, 1, 0 at the 2 x 2 level and
        # 6 / 16, 0, at the 1 x 1 (rounding each level by itself would make it 3 / 4, 1)
        half = np.zeros((4, 4), dtype=np.uint8)
        half[0, 0:2] = 1
        half[0, 2:4] = 1
        half[2, 0:2] = 1
        levels = wt.mean_levels(half)
        self.assertEqual((levels[0].tolist(), levels[1].tolist()), ([[1, 1], [1, 0]], [[0]]))
        odd = np.full((3, 5), 40, dtype=np.uint8)                    # sides that are no powers of two: a level's side is the half rounded down
        self.assertEqual([lv.shape for lv in wt.mean_levels(odd)], [(1, 2), (1, 1)])
        self.assertTrue(all((lv == 40).all() for lv in wt.mean_levels(odd)))


class Capture(unittest.TestCase):
    def _capture(self, d):
        """A made capture: one water row, a mask of 8 x 16, a normal map of 8 x 8 with one stored level."""
        tex = d / "native_tex_x"
        tex.mkdir()
        mask = bytearray(4096)
        for y in range(16):
            for x in range(8):
                mask[tt.tiled_offset_2d(x, y, 32, 0)] = 16 * y + x
        (tex / "1B6D0000_AA.bin").write_bytes(bytes(mask))
        nfc = ["0x80400002", "0x14D40071", "0x0000E007", "0x00A80D10", "0x00000043", "0x14D50A18"]   # 8 x 8, tiled, pitch 32, levels 0 to 1
        base = bytearray(4096)
        for by in range(2):
            for bx in range(2):
                raw = bytes([50 + bx, 50 + bx]) + bytes(6) + bytes([90 + by, 90 + by]) + bytes(6)
                o = tt.tiled_offset_2d(bx, by, 8, 4)
                base[o:o + 16] = bytes(raw[i ^ 1] for i in range(16))
        (tex / "14D40000_BB.bin").write_bytes(bytes(base))
        tail = bytearray(4096)
        raw = bytes([77, 77]) + bytes(6) + bytes([33, 33]) + bytes(6)
        o = tt.tiled_offset_2d(4, 0, 32, 4)
        tail[o:o + 16] = bytes(raw[i ^ 1] for i in range(16))
        (tex / "14D40000_BB_mips.bin").write_bytes(bytes(tail))
        rows = [
            {"kind": "texture", "file": "native_tex_x/1B6D0000_AA.bin", "bytes": 4096, "fc": SMALL_FC},
            {"kind": "texture", "file": "native_tex_x/14D40000_BB.bin", "bytes": 4096, "fc": nfc,
             "mips": {"file": "native_tex_x/14D40000_BB_mips.bin", "phys": "0x14D50000", "bytes": 4096, "tiled": True,
                      "levels": [{"level": 1, "offset": 0, "pitch": 512, "w": 4, "h": 4, "x": 4, "y": 0}]}},
            {"kind": "draw", "frame": 1, "in_scene": True, "vs_hash": "0xBEAD84BD72072E0E", "ps_hash": "0xA17D8AEC3A817D45",
             "tf": {"1": SMALL_FC, "2": nfc, "13": PICTURE_FC, "16": SMALL_FC},
             "pfull": "-" * (141 * 32) + "40003127400000003AC49BA63D70F0F1" + "-" * (114 * 32)},
            {"kind": "draw", "frame": 1, "in_scene": True, "vs_hash": "0x00000000000000AB", "tf": {"1": MASK_FC}},
        ]
        (d / "x.jsonl").write_text("\n".join(json.dumps(r) for r in rows) + "\n")
        return rows

    def test_mask_normal_and_its_stored_level(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            rows = self._capture(d)
            m = wt.mask(d, rows[0])
            self.assertEqual((m.shape, int(m[3, 5]), int(m[15, 7])), ((16, 8), 16 * 3 + 5, 247))
            n0 = wt.normal(d, rows[1], 0)
            self.assertEqual((n0.shape, n0[0, 0].tolist(), n0[7, 7].tolist()), ((8, 8, 2), [50, 90], [51, 91]))
            n1 = wt.normal(d, rows[1], 1)
            self.assertEqual((n1.shape, n1[0, 0].tolist(), n1[3, 3].tolist()), ((4, 4, 2), [77, 33], [77, 33]))
            with self.assertRaises(ValueError):
                wt.normal(d, rows[1], 2)                               # a level the row does not name
            with self.assertRaises(ValueError):
                wt.normal(d, {k: v for k, v in rows[1].items() if k != "mips"}, 1)   # no stored levels in the capture
            with self.assertRaises(ValueError):
                wt.mask(d, rows[1])                                    # not a mask

    def test_fetches_census_sheet_cli(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            self._capture(d)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(wt.main(["fetches", str(d / "x.jsonl")]), 0)
            text = out.getvalue()
            self.assertIn("1 water rows", text)                        # the other shader's row is not one
            self.assertIn("slot  1", text)
            self.assertIn("8 x 16", text)
            self.assertIn("swizzle (2, 1, 0, 3)", text)                # slot 13: criterion 4's candidate
            self.assertNotIn("1BD03000", text)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(wt.main(["census", str(d / "x.jsonl")]), 0)
            self.assertIn("1B6D0000  8x16 pitch  32 tiled 1", out.getvalue())
            self.assertIn("c141 = (2.003, 2)", out.getvalue())
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(wt.main(["sheet", str(d / "x.jsonl"), "--out", str(d / "s.png")]), 0)
            self.assertEqual((d / "s.png").read_bytes()[:8], b"\x89PNG\r\n\x1a\n")

    def test_compare_against_the_cpp_outputs(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            rows = self._capture(d)
            b = d / "native_tex_x" / "1B6D0000_AA.bin"
            fcs = ",".join(v[2:] for v in SMALL_FC)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(wt.main(["compare", str(b), fcs]), 2)             # no C++ output yet
                Path(str(b) + ".cpp.r8").write_bytes(wt.mask(d, rows[0]).tobytes())
                self.assertEqual(wt.main(["compare", str(b), fcs]), 0)
                Path(str(b) + ".cpp.r8").write_bytes(bytes(128))
                self.assertEqual(wt.main(["compare", str(b), fcs]), 1)


@unittest.skipUnless(os.environ.get("FABLE2_K8_BIN") and os.environ.get("FABLE2_K8_FC"),
                     "real shore mask dump (set FABLE2_K8_BIN and FABLE2_K8_FC, and run the C++ test with them first)")
class RealMask(unittest.TestCase):
    def test_equal_to_the_cpp_decoder(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(wt.compare(os.environ["FABLE2_K8_BIN"], os.environ["FABLE2_K8_FC"]), 0)


@unittest.skipUnless(os.environ.get("FABLE2_WDXN_BIN") and os.environ.get("FABLE2_WDXN_FC"),
                     "real water normal map dump (set FABLE2_WDXN_BIN and FABLE2_WDXN_FC, and run the C++ test with them first)")
class RealNormal(unittest.TestCase):
    def test_equal_to_the_cpp_decoder(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(wt.compare(os.environ["FABLE2_WDXN_BIN"], os.environ["FABLE2_WDXN_FC"]), 0)


if __name__ == "__main__":
    unittest.main()
