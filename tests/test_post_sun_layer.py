"""tools/xdk_sigmatch/post_sun_layer.py: the sun layer rasterised on the CPU for the replay's variant C (plan
Task 11): the rasteriser, the draws' state, the textures, the layer's steps on hand-made draws, and what the
research's sunward capture holds (skipped without it)."""
import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import post_check as pc  # noqa: E402
import post_replay as pr  # noqa: E402
import post_sun_layer as sl  # noqa: E402

SUNWARD = pr.LOGS / "native_discovery_20261009_134312.jsonl"
NAN = float("nan")
IDENTITY = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]
QUAD_VS, CLOUD_VS, CUBE_VS = "0x81BADE63A169E3DC", "0xC304C6A1AF18B46F", "0xD1CC6DC28244C55A"
BEAM, DISC, GLOW, FLARE = "0x8E4DBB9E9A63EF67", "0x3E77B1AD4BC5B900", "0x4B58837D42F08C89", "0xD72FD0903CCE7DD6"
LAYER_DEPTH, OCCLUDER = "0x5D6768847B0602B8", "0x3FF79524FCDB71C5"


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return "0x%08X" % struct.unpack(">I", struct.pack(">f", x))[0]


def pfull(regs):
    words = [0.0] * 1024
    for r, v in regs.items():
        words[4 * r:4 * r + 4] = v
    return struct.pack(">1024f", *words).hex().upper()


def full_screen(z=0.5, u1=1.0, v1=1.0):
    """The corners of a quad over the whole target, in clip space: (x, y, z, u, v), the game's order."""
    return [(-1.0, -1.0, z, 0.0, v1), (-1.0, 1.0, z, 0.0, 0.0), (1.0, -1.0, z, u1, v1), (1.0, 1.0, z, u1, 0.0)]


def draw(ps, corners=None, vs=QUAD_VS, pitch=8, regs=None, lit=None, depth=0x00708762, blend=0x00010001, colour=0x2202,
         alpha_ref=0.5, mask=0xF, bank=None, in_scene=False, slot_fc=None, indices=(0, 1, 2, 1, 3, 2), stride=20, n=1):
    """A LayerDraw from a hand-made "indx" row and its "indxdata" row. colour: the value of register 0x2202
    (4: alpha test off, 0xC: on) when below 0x100, else no entry."""
    corners = corners if corners is not None else full_screen()
    rs = {"2104": "0x%08X" % mask, "210E": bits(alpha_ref), "2200": "0x%08X" % depth, "2201": "0x%08X" % blend,
          "2113": bits(-1.0), "2114": bits(1.0), "bin_mask_lo": "0xFFFFFFFF"}
    if colour < 0x100:
        rs["2202"] = "0x%08X" % colour
    row = {"kind": "indx", "frame": 1, "func": "DrawIndx:82217EE8", "n": n, "in_scene": in_scene, "ps_hash": ps,
           "vs": {"hash": vs}, "surface_info": "0x%08X" % (0x09000000 | pitch), "rs": rs, "pfull": pfull(regs or {}),
           "ps_lit": [{"reg": 508, "v": [0.0] * 12 + list(lit)}] if lit else [], "vs_lit": [],
           "tf": {"13": slot_fc or ["0x82000002", "0x14E60054", "0x001FE0FF", "0x00A80D10", "0x00000103", "0x14E70A18"]},
           "tfc": {}, "bank": list(bank or IDENTITY) + [0.0] * (1024 - 16)}
    flat = [x for c in corners for x in c]
    data = {"kind": "indxdata", "frame": 1, "n": n, "stride": stride, "verts": struct.pack(">%df" % len(flat), *flat).hex().upper(),
            "idx": list(indices)}
    return sl.LayerDraw(row, data)


class Flat:
    """A texture of one texel value."""
    def __init__(self, texel):
        self.texel, self.calls = list(texel), 0

    def sample(self, u, v, lod=0.0, dx=0, dy=0):
        self.calls += 1
        return list(self.texel)


# --- the rasteriser ---------------------------------------------------------------------------------------

class Raster(unittest.TestCase):
    def cover(self, vertices, indices, w=8, h=6):
        hits = {}

        def pixel(x, y, depth, attrs):
            hits.setdefault((x, y), []).append((depth, attrs))

        n = sl.raster(vertices, indices, w, h, pixel)
        self.assertEqual(n, sum(len(v) for v in hits.values()))
        return hits

    def quad(self, z=0.5):
        return [(x, y, z, 1.0, u, v) for x, y, z, u, v in full_screen(z)]

    def test_a_quad_over_the_target_covers_every_pixel_once(self):
        hits = self.cover(self.quad(), lf.POST_QUAD_INDICES)
        self.assertEqual(len(hits), 48)
        self.assertTrue(all(len(v) == 1 for v in hits.values()))       # the shared diagonal is drawn once

    def test_the_coordinate_at_a_pixel_is_its_position_over_the_size(self):
        # A pixel is sampled at its integer position (the post rectangles' convention: the interpolator is
        # i / N at pixel i).
        self.assertEqual(sl.PIXEL_CENTRE, 0.0)
        hits = self.cover(self.quad(), lf.POST_QUAD_INDICES)
        for (x, y), ((depth, (u, v)),) in hits.items():
            self.assertAlmostEqual(u, x / 8, delta=1e-12)
            self.assertAlmostEqual(v, y / 6, delta=1e-12)
            self.assertAlmostEqual(depth, 0.5, delta=1e-12)

    def test_half_the_target(self):
        # The left half in clip space: x in -1..0 is columns 0..3 (column 4's sample lies on the right edge,
        # which is not the quad's).
        left = [(-1.0, -1.0, 0.25, 1.0), (-1.0, 1.0, 0.25, 1.0), (0.0, -1.0, 0.25, 1.0), (0.0, 1.0, 0.25, 1.0)]
        hits = self.cover(left, lf.POST_QUAD_INDICES)
        self.assertEqual(sorted({x for x, _ in hits}), [0, 1, 2, 3])
        self.assertEqual(len(hits), 24)

    def test_two_quads_that_share_an_edge_share_no_pixel(self):
        a = [(-1.0, -1.0, 0.0, 1.0), (-1.0, 1.0, 0.0, 1.0), (0.3, -1.0, 0.0, 1.0), (0.3, 1.0, 0.0, 1.0)]
        b = [(0.3, -1.0, 0.0, 1.0), (0.3, 1.0, 0.0, 1.0), (1.0, -1.0, 0.0, 1.0), (1.0, 1.0, 0.0, 1.0)]
        ha, hb = self.cover(a, lf.POST_QUAD_INDICES), self.cover(b, lf.POST_QUAD_INDICES)
        self.assertFalse(set(ha) & set(hb))
        self.assertEqual(len(ha) + len(hb), 48)

    def test_either_winding_is_drawn(self):
        self.assertEqual(len(self.cover(self.quad(), (0, 2, 1, 1, 2, 3))), 48)

    def test_the_coordinate_is_correct_in_perspective(self):
        # The right edge twice as far: at the middle column the coordinate is 1 / 3, not 1 / 2.
        quad = [(-1.0, -1.0, 0.0, 1.0, 0.0), (-1.0, 1.0, 0.0, 1.0, 0.0), (2.0, -2.0, 0.0, 2.0, 1.0), (2.0, 2.0, 0.0, 2.0, 1.0)]
        hits = self.cover(quad, lf.POST_QUAD_INDICES)
        self.assertAlmostEqual(hits[(4, 3)][0][1][0], 1.0 / 3.0, delta=1e-12)
        self.assertAlmostEqual(hits[(0, 3)][0][1][0], 0.0, delta=1e-12)

    def test_the_depth_is_linear_on_the_screen(self):
        quad = [(-1.0, -1.0, 0.0, 1.0), (-1.0, 1.0, 0.0, 1.0), (1.0, -1.0, 0.8, 1.0), (1.0, 1.0, 0.8, 1.0)]
        hits = self.cover(quad, lf.POST_QUAD_INDICES)
        self.assertAlmostEqual(hits[(2, 1)][0][0], 0.2, delta=1e-12)
        self.assertAlmostEqual(hits[(6, 4)][0][0], 0.6, delta=1e-12)

    def test_what_lies_behind_the_eye_is_cut_off(self):
        # The right corners behind the eye (w < 0): the quad ends where w reaches 0, and nothing is drawn with
        # a number that is none.
        quad = [(-1.0, -1.0, 0.0, 1.0, 0.0), (-1.0, 1.0, 0.0, 1.0, 0.0), (3.0, -1.0, 0.0, -1.0, 1.0), (3.0, 1.0, 0.0, -1.0, 1.0)]
        hits = self.cover(quad, lf.POST_QUAD_INDICES)
        self.assertEqual(len(hits), 48)
        for (x, y), entries in hits.items():
            self.assertEqual(len(entries), 1)
            self.assertTrue(0.0 <= entries[0][1][0] < 0.5, entries)        # only the near half of the coordinate
        self.assertEqual(self.cover([(0.0, 0.0, 0.0, -1.0)] * 4, lf.POST_QUAD_INDICES), {})

    def test_the_near_clip(self):
        tri = [(0.0, 0.0, 0.0, 1.0, 0.0), (0.0, 0.0, 0.0, -1.0, 1.0), (1.0, 0.0, 0.0, 1.0, 0.5)]
        out = sl.clip_near(tri, 0.5)
        self.assertEqual(len(out), 4)
        self.assertTrue(all(v[3] >= 0.5 - 1e-12 for v in out))
        self.assertAlmostEqual(out[1][4], 0.25, delta=1e-12)                # a quarter of the way to the far vertex
        self.assertEqual(sl.clip_near([(0.0, 0.0, 0.0, -1.0)] * 3), [])

    def test_a_triangle_without_area_draws_nothing(self):
        self.assertEqual(self.cover([(0.0, 0.0, 0.0, 1.0)] * 3, (0, 1, 2)), {})


# --- a draw's state ---------------------------------------------------------------------------------------

class State(unittest.TestCase):
    def test_the_depth_word(self):
        d = draw(BEAM, depth=0x00708762)
        self.assertEqual((d.depth_test, d.depth_write, d.depth_func), (True, False, 6))
        d = draw(BEAM, depth=0x00708766)
        self.assertEqual((d.depth_test, d.depth_write), (True, True))
        self.assertFalse(draw(BEAM, depth=0x00708760).depth_test)
        self.assertEqual((d.zscale, d.zoffset), (-1.0, 1.0))

    def test_the_depth_comparison(self):
        self.assertEqual([sl.depth_passes(f, 0.5, 0.25) for f in range(8)], [False, False, False, False, True, True, True, True])
        self.assertEqual([sl.depth_passes(f, 0.25, 0.5) for f in range(8)], [False, True, False, True, False, True, False, True])
        self.assertEqual([sl.depth_passes(f, 0.5, 0.5) for f in range(8)], [False, False, True, True, False, False, True, True])
        self.assertFalse(sl.depth_passes(6, NAN, 0.0))

    def test_the_alpha_test(self):
        d = draw(DISC, colour=0xC, alpha_ref=0.50196)
        self.assertTrue(d.alpha_test)
        self.assertEqual(d.alpha_ref, f32(0.50196))
        self.assertFalse(draw(BEAM, colour=0x4).alpha_test)
        self.assertFalse(draw(BEAM, colour=0x2202).alpha_test)             # no entry: off
        with self.assertRaises(pr.ReplayError):
            draw(DISC, colour=0xB).alpha_test                              # a function other than "greater"

    def test_the_blend_word(self):
        src, dst = (0.5, 0.25, 1.0, 0.5), (0.2, 0.2, 0.2)
        self.assertEqual(sl.blend(0x00010001, src, dst), [0.5, 0.25, 1.0])
        self.assertEqual(sl.blend(0x01010101, src, dst), [0.7, 0.45, 1.2])
        self.assertEqual(sl.blend(0x07060706, src, dst), [0.35, 0.225, 0.6])
        with self.assertRaises(pr.ReplayError):
            sl.blend(0x0F0E0F0E, src, dst)                                 # the constant factors: pass F's, not the layer's

    def test_registers_literals_and_the_matrix(self):
        d = draw(DISC, regs={31: (0.5, 0.25, 0.125, 0.0), 20: (0.0, 0.0, 0.0, 1.0)}, lit=(0.2, 0.0, 0.0, 0.0),
                 bank=[float(i) for i in range(16)])
        self.assertEqual(d.reg(31), (0.5, 0.25, 0.125, 0.0))
        self.assertEqual(d.literal(255), (f32(0.2), 0.0, 0.0, 0.0))
        self.assertEqual(d.view_proj, [[0.0, 1.0, 2.0, 3.0], [4.0, 5.0, 6.0, 7.0], [8.0, 9.0, 10.0, 11.0], [12.0, 13.0, 14.0, 15.0]])
        self.assertEqual((d.role, d.pitch, d.where), ("sun-disc", 8, "layer2"))
        self.assertEqual(len(d.vertices), 4)
        self.assertEqual(d.vertices[2], (1.0, -1.0, 0.5, 1.0, 1.0))
        self.assertEqual(d.indices, [0, 1, 2, 1, 3, 2])
        with self.assertRaises(pr.ReplayError):
            d.literal(250)

    def test_a_cube_is_known_by_its_vertex_shader(self):
        d = draw("0x0000000000000000", vs=CUBE_VS, corners=[(0.0, 0.0, 0.0, 1.0)] * 8, stride=16, indices=tuple(range(8)))
        self.assertEqual((d.role, d.where), ("cube", "layer-depth"))
        self.assertEqual(d.vertices[0], (0.0, 0.0, 0.0, 1.0))


# --- textures ---------------------------------------------------------------------------------------------

class Textures(unittest.TestCase):
    def texture(self, wrap=True):
        # 2 x 2: red 0, 1 / 1, 0 (a checker), alpha 1
        rgba = bytes([0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 0, 255])
        return sl.MipTexture(2, 2, rgba, wrap=wrap)

    def test_a_texels_centre_reads_the_texel(self):
        t = self.texture()
        self.assertEqual(t.sample(0.25, 0.25), [0.0, 0.0, 0.0, 1.0])
        self.assertEqual(t.sample(0.75, 0.25), [1.0, 0.0, 0.0, 1.0])

    def test_between_texels_and_across_the_edge(self):
        t = self.texture()
        self.assertAlmostEqual(t.sample(0.5, 0.25)[0], 0.5, delta=1e-12)
        self.assertAlmostEqual(t.sample(0.0, 0.25)[0], 0.5, delta=1e-12)           # wrapped: half the other side's
        self.assertAlmostEqual(self.texture(wrap=False).sample(0.0, 0.25)[0], 0.0, delta=1e-12)
        self.assertEqual(t.sample(1.25, 2.25), t.sample(0.25, 0.25))

    def test_an_offset_in_texels(self):
        t = self.texture()
        self.assertEqual(t.sample(0.25, 0.25, 0.0, 1, 0), t.sample(0.75, 0.25))
        self.assertEqual(t.sample(0.25, 0.25, 0.0, 0, -1), t.sample(0.25, 0.75))

    def test_the_levels_are_means_of_four(self):
        t = self.texture()
        self.assertEqual(t.sample(0.3, 0.9, 1.0), [0.5, 0.0, 0.0, 1.0])
        self.assertAlmostEqual(t.sample(0.25, 0.25, 0.5)[0], 0.25, delta=1e-12)     # half way between the levels
        self.assertEqual(t.sample(0.3, 0.9, 7.0), [0.5, 0.0, 0.0, 1.0])             # no level beyond the last
        self.assertEqual(t.sample(0.25, 0.25, -3.0), [0.0, 0.0, 0.0, 1.0])
        self.assertEqual(t.sample(NAN, 0.25), [0.0, 0.0, 0.0, 0.0])

    def test_the_level_of_a_quad(self):
        # 64 texels over 8 pixels along each axis: three levels down; a magnified texture: level 0.
        quad = [(x, y, 0.0, 1.0, u, v) for x, y, _, u, v in full_screen()]
        self.assertAlmostEqual(sl.quad_lod(quad, lf.POST_QUAD_INDICES, 64, 64, 8, 8), 3.0, delta=1e-12)
        self.assertEqual(sl.quad_lod(quad, lf.POST_QUAD_INDICES, 4, 4, 8, 8), 0.0)
        self.assertEqual(sl.quad_lod([(0.0, 0.0, 0.0, 1.0, 0.0, 0.0)] * 4, lf.POST_QUAD_INDICES, 64, 64, 8, 8), 0.0)


# --- the layer --------------------------------------------------------------------------------------------

W, H = 16, 12           # the scene of the hand-made frames; the layer is 8 x 6


def scene_depth(value=0.0, near=None):
    """A scene depth of w * h floats (reversed: 1 near, 0 nothing drawn); near: (x0, x1) columns of the layer
    that hold something near."""
    out = [value] * (W * H)
    if near:
        for y in range(H):
            for x in range(2 * near[0], 2 * near[1]):
                out[y * W + x] = 0.9
    return out


def run(draws, depth=None, textures=None):
    tex = textures or {}
    return sl.sun_layer(draws, depth if depth is not None else scene_depth(), W, H,
                        lambda d, slot: tex.get(d.role, Flat((0.6, 0.6, 0.6, 1.0))))


SUN = {31: (0.5, 0.25, 1.0, 0.0), 20: (0.0, 0.0, 0.0, 1.0)}


class Layer(unittest.TestCase):
    def test_the_layers_depth_is_every_second_texel_of_the_scenes(self):
        depth = [float(y * W + x) / 1000.0 for y in range(H) for x in range(W)]
        got = sl.layer_depth(depth, W, H)
        self.assertEqual(len(got), 48)
        self.assertEqual(got[0], 0.0)
        self.assertEqual(got[1], 0.002)                         # texel (2, 0)
        self.assertEqual(got[8 + 3], (2 * W + 6) / 1000.0)      # texel (6, 2)
        self.assertEqual(sl.layer_depth([2.0, NAN, -1.0, 0.5] * 48, W, H)[0], 1.0)

    def test_the_beam_source_is_drawn_where_the_sky_is(self):
        # A sixth of the texture times the sun's colour; hidden where the scene has something nearer.
        draws = [draw(LAYER_DEPTH, depth=0x00708766, mask=0), draw(BEAM, regs=SUN, lit=(1.0 / 6.0, 0, 0, 0))]
        one, two, lines = run(draws, scene_depth(near=(0, 3)))
        self.assertEqual(one[2][2], [0.0, 0.0, 0.0])
        for got, want in zip(one[2][5], [0.05, 0.025, 0.1]):
            self.assertAlmostEqual(got, pr.q10(want), delta=1e-12)
        self.assertEqual(sum(1 for row in one for t in row if t[0] > 0), 6 * 5)
        self.assertTrue(all(t == [0.0, 0.0, 0.0] for row in two for t in row))
        self.assertIn("30 pixels drawn", lines[1])
        self.assertIn("18 behind the depth", lines[1])

    def test_a_quad_nearer_than_the_scene_is_drawn(self):
        # The quad at clip z 0.05: the layer's depth value 1 - 0.05 = 0.95, in front of the 0.9.
        draws = [draw(LAYER_DEPTH, depth=0x00708766, mask=0), draw(BEAM, full_screen(z=0.05), regs=SUN, lit=(1.0 / 6.0, 0, 0, 0))]
        one, _, _ = run(draws, scene_depth(near=(0, 8)))
        self.assertEqual(sum(1 for row in one for t in row if t[0] > 0), 48)

    def test_no_depth_before_the_layers_depth_draw(self):
        with self.assertRaises(pr.ReplayError):
            run([draw(BEAM, regs=SUN, lit=(1.0 / 6.0, 0, 0, 0))])

    def test_the_second_picture_adds_blends_and_tests_alpha(self):
        first = [draw(LAYER_DEPTH, depth=0x00708766, mask=0)]
        flare = draw(FLARE, regs={31: (0.25, 0.125, 0.5, 0.0)}, blend=0x01010101)
        disc = draw(DISC, regs=SUN, blend=0x01010101, colour=0xC, alpha_ref=0.50196)
        glow = draw(GLOW, lit=(0.2, 0, 0, 0), blend=0x07060706, colour=0xC, alpha_ref=0.0392)
        tex = {"flare": Flat((0.0, 0.0, 0.0, 0.8)), "sun-disc": Flat((1.0, 1.0, 0.5, 0.6)), "moon-glow": Flat((1.0, 0.5, 0.25, 0.5))}
        # two flares: 2 * 0.8 * c31
        _, two, _ = run(first + [flare, flare], textures=tex)
        for got, want in zip(two[3][3], [0.4, 0.2, 0.8]):
            self.assertAlmostEqual(got, pr.q10(pr.q10(want / 2) * 2), delta=1e-12)
        # a flare, then the glow over it (alpha 0.5: half of each), then the disc added
        _, two, lines = run(first + [flare, glow, disc], textures=tex)
        after_flare = [pr.q10(v) for v in (0.2, 0.1, 0.4)]
        after_glow = [pr.q10(0.5 * s + 0.5 * d) for s, d in zip((0.1, 0.05, 0.025), after_flare)]
        want = [pr.q10(d + s) for s, d in zip((0.3, 0.15, 0.3), after_glow)]
        for got, w in zip(two[3][3], want):
            self.assertAlmostEqual(got, w, delta=1e-12)
        # a disc whose alpha is not above the reference is not drawn at all
        tex["sun-disc"] = Flat((1.0, 1.0, 0.5, 0.5))
        _, two, lines = run(first + [disc], textures=tex)
        self.assertTrue(all(t == [0.0, 0.0, 0.0] for row in two for t in row))
        self.assertIn("48 below the alpha reference", lines[1])

    def test_the_pictures_are_clamped_at_one(self):
        first = [draw(LAYER_DEPTH, depth=0x00708766, mask=0)]
        flare = draw(FLARE, regs={31: (0.75, 0.125, 0.5, 0.0)}, blend=0x01010101)
        _, two, _ = run(first + [flare, flare], textures={"flare": Flat((0.0, 0.0, 0.0, 1.0))})
        self.assertEqual(two[0][0], [1.0, pr.q10(pr.q10(0.125) * 2), 1.0])

    def test_a_cloud_occluder_hides_the_sun_where_its_alpha_is_above_the_reference(self):
        # The occluder's quad nearer than the sun's; its alpha 0.8 * c77.x * fade(1) = 0.4 against 0.149.
        cloud = draw(OCCLUDER, full_screen(z=0.2), vs=CLOUD_VS, depth=0x00708766, blend=0x07060706, colour=0xC, alpha_ref=0.149,
                     mask=0, regs={31: (5.0, 5.0, 0, 0), 46: (0.25, 0.5, 0, 0), 47: (0.0, 0.0, 0.0, 1.0), 77: (0.5, 0, 0, 0)},
                     lit=(1000.0, 0.0, 0.001, 1.0))
        draws = [draw(LAYER_DEPTH, depth=0x00708766, mask=0), cloud, draw(BEAM, full_screen(z=0.5), regs=SUN, lit=(1.0 / 6.0, 0, 0, 0))]
        one, _, lines = run(draws, textures={"cloud-occluder": Flat((0.0, 0.0, 0.0, 0.8))})
        self.assertTrue(all(t == [0.0, 0.0, 0.0] for row in one for t in row))
        self.assertIn("48 pixels drawn", lines[1])
        # A thin cloud (alpha 0.2 * 0.5 = 0.1) hides nothing.
        one, _, lines = run(draws, textures={"cloud-occluder": Flat((0.0, 0.0, 0.0, 0.2))})
        self.assertEqual(sum(1 for row in one for t in row if t[0] > 0), 48)
        self.assertIn("48 below the alpha reference", lines[1])

    def test_a_cube_writes_depth_only(self):
        # The cube's vertex shader with an identity matrix: its two faces at the heights c119.x and c9.z, both
        # wider than the target here; drawn without culling.
        bank = list(IDENTITY) + [0.0] * (1024 - 16)
        bank[0], bank[5] = 2.0 / 2850.0, 2.0 / 2850.0                       # x and y to -2..2: beyond the target
        bank[36:40] = [0.0, 0.0, 0.25, 1.0]                                # c9: the eye
        bank[476:480] = [0.25, 0.0, 0.0, 0.0]                              # c119.x
        verts = [(float(i & 1), float((i >> 1) & 1), float(i >> 2), 1.0) for i in range(8)]
        cube = draw("0x0000000000000000", verts, vs=CUBE_VS, depth=0x00708766, mask=0, bank=bank, stride=16,
                    indices=(0, 1, 2, 3, 2, 1))
        cube.vs_literals = {254: (2.0, 0.0, 2850.0, -1.0), 255: (1.0, 0.0, 0.0, 0.0)}
        draws = [draw(LAYER_DEPTH, depth=0x00708766, mask=0), cube, draw(BEAM, full_screen(z=0.5), regs=SUN, lit=(1.0 / 6.0, 0, 0, 0))]
        one, two, lines = run(draws)
        self.assertIn("cube", lines[1])
        self.assertIn("48 pixels drawn", lines[1])
        self.assertTrue(all(t == [0.0, 0.0, 0.0] for row in one for t in row))     # 1 - 0.25 is nearer than 1 - 0.5


class SunInputs(unittest.TestCase):
    def test_the_reduction_is_the_mean_of_four(self):
        rows = [[[float(x + y)] * 3 for x in range(4)] for y in range(4)]
        got = sl.reduce_rows(rows)
        self.assertEqual((len(got), len(got[0])), (2, 2))
        self.assertEqual(got[0][0], [1.0] * 3)                              # (0 + 1 + 1 + 2) / 4, clamped and in 10 bits
        self.assertEqual(sl.reduce_rows([[[0.1] * 3] * 4] * 4)[1][1], [pr.q10(0.1)] * 3)

    def test_the_beams_are_blurred_twice_about_the_centre_and_the_flares_only_reduced(self):
        one = [[[0.25, 0.125, 0.0625]] * 8 for _ in range(4)]
        two = [[[0.5, 0.25, 0.125]] * 8 for _ in range(4)]
        tf1, tf2 = sl.sun_inputs(one, two, (0.5, 0.5), 0.87, 0.935)
        self.assertEqual((len(tf1), len(tf1[0]), len(tf2), len(tf2[0])), (2, 4, 2, 4))
        self.assertEqual(tf2[0][0], [pr.q10(v) for v in (0.5, 0.25, 0.125)])
        # A flat picture through one pass is its value times the weights' sum; twice, each in 10 bits.
        first = [pr.q10(lf.post_radial(lambda u, v: [pr.q10(c) for c in (0.25, 0.125, 0.0625)], 0.0, 0.0, (0.5, 0.5), 0.87, 0.935)[i])
                 for i in range(3)]
        second = [pr.q10(v) for v in lf.post_radial(lambda u, v: first, 0.0, 0.0, (0.5, 0.5), 0.87, 0.935)]
        self.assertEqual(tf1[1][2], second)
        self.assertGreater(tf1[1][2][0], 0.25)
        ex1, ex2 = sl.sun_inputs(one, two, (0.5, 0.5), 0.87, 0.935, exchanged=True)
        self.assertEqual((ex1, ex2), (tf2, tf1))

    def test_the_bloom_with_the_suns_two_inputs(self):
        fr = pr.Frame("made.jsonl", 1)
        fr.regs = {"bright": {31: (1.5, 0, 0, 0), 46: (0.2, 0, 0, 0), 47: (2.0, 0, 0, 0), 77: (0.05, 0.125, 0, 0)},
                   "blur-x": {46: (1.0, 0.0, 0.0, 0.0), 47: (0.0, 0.0, 0.0, 0.0)},
                   "blur-y": {46: (1.0, 0.0, 0.0, 0.0), 47: (0.0, 0.0, 0.0, 0.0)}}
        from array import array
        scene = array("d", [0.0] * (8 * 8 * 3))
        zero = [[[0.0] * 3] * 2] * 2
        tf1 = [[[0.25, 0.0, 0.0], [0.0] * 3], [[0.0] * 3, [0.0] * 3]]
        tf2 = [[[0.0] * 3, [0.0, 0.5, 0.0]], [[0.0] * 3, [0.0] * 3]]
        self.assertEqual(sl.bloom_picture(scene, 8, 8, fr, sun=(zero, zero)), pr.bloom_picture(scene, 8, 8, fr))
        bw, bh, rows = sl.bloom_picture(scene, 8, 8, fr, sun=(tf1, tf2))
        # (tf1 * 6 * c46.x + tf2) / c47.x, through blurs of one tap
        self.assertEqual(rows[0][0], [pr.q10(0.25 * 6 * f32(0.2) / 2.0), 0.0, 0.0])
        self.assertEqual(rows[0][1], [0.0, pr.q10(0.25), 0.0])
        with self.assertRaises(pr.ReplayError):
            sl.bloom_picture(scene, 8, 8, fr, sun=(tf1, [[[0.0] * 3] * 3] * 2))


# --- reading a capture ------------------------------------------------------------------------------------

class Reader(unittest.TestCase):
    def test_the_layers_draws_of_a_frame_in_their_order(self):
        def rows(d, n, **over):
            r = dict(d.row, n=n, **over)
            return [r, dict(d.data, n=n, frame=r["frame"])]

        beam, disc = draw(BEAM, regs=SUN, lit=(1.0 / 6.0, 0, 0, 0)), draw(DISC, regs=SUN)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "native_discovery_test.jsonl"
            lines = rows(draw(LAYER_DEPTH), 1) + rows(beam, 2) + rows(disc, 3, frame=2) + rows(draw(FLARE), 4, surface_info="0x04020118") \
                + rows(disc, 5) + rows(draw("0x04985A0F7296E130"), 6, in_scene=True) \
                + [{"kind": "draw", "frame": 1, "ps_hash": "0xF1EB2497108F159D", "surface_info": "0x09000008", "in_scene": False},
                   {"kind": "draw", "frame": 1, "ps_hash": "0xF1EB2497108F159D", "surface_info": "0x09000410", "in_scene": False}]
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                for r in lines:
                    f.write(json.dumps(r, separators=(",", ":")) + "\n")
            draws, other = sl.load_layer_draws(path, 1, pitch=8)
        self.assertEqual([d.role for d in draws], ["layer-depth", "beam-source", "sun-disc"])
        self.assertEqual(other, {"tree-occluder draws at the layer (not replayed)": 1, "flare quads at another target (pitch 280)": 1})
        self.assertEqual(draws[1].literal(255)[0], f32(1.0 / 6.0))

    def test_a_draw_without_its_vertices_is_said(self):
        d = draw(BEAM)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "native_discovery_test.jsonl"
            path.write_text(json.dumps(d.row, separators=(",", ":")) + "\n", encoding="utf-8")
            with self.assertRaises(pr.ReplayError) as e:
                sl.load_layer_draws(path, 1, pitch=8)
        self.assertIn("indxdata", str(e.exception))

    def test_the_command_line_says_what_is_wrong(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(sl.main(["replay", str(ROOT / "out" / "no_such_capture.jsonl"), "1"]), 2)
        self.assertIn("post_sun_layer:", err.getvalue())


class VariantC(unittest.TestCase):
    def test_post_replays_variant_c_is_this_tools_replay(self):
        # `post_replay.py replay <capture> <frame> --variant C` (the plan's command) hands over to this tool.
        seen = []
        kept = sl.cmd_replay
        sl.cmd_replay = lambda args: seen.append(args) or 0
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                code = pr.main(["replay", "some.jsonl", "3", "--variant", "c", "--bias", "0.5", "--png", "out/x"])
        finally:
            sl.cmd_replay = kept
        self.assertEqual(code, 0)
        self.assertEqual(len(seen), 1)
        a = seen[0]
        self.assertEqual((a.capture, a.frame, a.bias, a.png, a.sun_roles, a.region_file), ("some.jsonl", 3, 0.5, "out/x", "research", None))

    def test_a_missing_capture_is_said_for_variant_c_too(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(pr.main(["replay", str(ROOT / "out" / "no_such_capture.jsonl"), "1", "--variant", "C"]), 2)
        self.assertIn("post_replay:", err.getvalue())


@unittest.skipUnless(SUNWARD.is_file(), f"no capture {SUNWARD}")
class RealCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.draws, cls.other = sl.load_layer_draws(SUNWARD, 1)

    def test_the_layers_draws_towards_the_sun(self):
        # Research section 1 and furniture_134312_f1.txt: the depth, three cloud occluders, three cubes, the
        # beam source, the moon glow, the sun disc; no flare in this capture; one tree occluder draw.
        self.assertEqual([d.role for d in self.draws], ["layer-depth"] + ["cloud-occluder"] * 3 + ["cube"] * 3
                         + ["beam-source", "moon-glow", "sun-disc"])
        self.assertEqual(self.other, {"tree-occluder draws at the layer (not replayed)": 1})

    def test_every_quad_has_the_six_indices_and_four_corners(self):
        for d in self.draws:
            if d.role != "cube":
                self.assertEqual(tuple(d.indices), lf.POST_QUAD_INDICES, d.role)
                self.assertEqual((len(d.vertices), len(d.vertices[0])), (4, 5), d.role)
            else:
                self.assertEqual((len(d.vertices), len(d.indices)), (8, 30))

    def test_the_states_are_the_quad_lists(self):
        by = {s.kind: s for s in lf.POST_QUAD_SHADERS}
        words = {"alpha": 0x07060706, "add": 0x01010101, "none": 0x00010001}
        for d in self.draws:
            if d.role not in by:
                continue
            s = by[d.role]
            self.assertEqual(d.alpha_test, s.test, d.role)
            self.assertEqual(d.where, s.where, d.role)
            self.assertEqual(d.vs, s.vs, d.role)
            if s.blend == "depth":
                self.assertEqual(d.colour_mask, 0, d.role)
            else:
                self.assertEqual(d.blend_word, words[s.blend], d.role)
                self.assertEqual(d.colour_mask, 0xF, d.role)
        refs = {d.role: round(d.alpha_ref * 255) for d in self.draws if d.alpha_test}
        self.assertEqual(refs, {"cloud-occluder": 38, "moon-glow": 10, "sun-disc": 128})

    def test_the_checks_literals_are_the_rows(self):
        # post_check.FURNITURE_LITERALS and light_formulas' defaults are typed in: here they are the rows'.
        seen = set()
        for d in self.draws:
            shader = d.ps if d.role != "cube" else d.vs
            want = pc.FURNITURE_LITERALS.get(shader)
            if want is None:
                self.assertEqual(d.literals, {}, d.role)
                continue
            seen.add(shader)
            for reg, values in want.items():
                self.assertEqual(list(d.literal(reg)), [f32(v) for v in values], (d.role, reg))
        self.assertEqual(seen, {"8E4DBB9E9A63EF67", "4B58837D42F08C89", "3FF79524FCDB71C5", "D1CC6DC28244C55A"})

    def test_the_clouds_literals_are_the_scenes_rows(self):
        rows = sl.scene_quads(SUNWARD, 1)
        self.assertEqual([d.role for d in rows], ["moon", "cloud", "cloud", "cloud", "glare", "glare"])
        for d in rows:
            if d.role == "cloud":
                for reg, values in pc.FURNITURE_LITERALS[d.ps].items():
                    self.assertEqual(list(d.literal(reg)), [f32(v) for v in values], reg)
            self.assertEqual(tuple(d.indices), lf.POST_QUAD_INDICES)
        by = {s.kind: s for s in lf.POST_QUAD_SHADERS}
        self.assertEqual([(d.role, d.alpha_test) for d in rows], [(d.role, by[d.role].test) for d in rows])

    def test_the_cubes_are_slabs_under_the_eye(self):
        cubes = [d for d in self.draws if d.role == "cube"]
        self.assertEqual([round(d.vreg(119)[0], 2) for d in cubes], [18.47, 20.0, 20.0])
        eye = cubes[0].vreg(9)
        top = lf.post_cube_point((1.0, 1.0, 1.0), eye, cubes[0].vreg(119)[0], cubes[0].vs_literals[254], cubes[0].vs_literals[255][0])
        self.assertEqual(top, (2850.0 + eye[0], 2850.0 + eye[1], eye[2]))


if __name__ == "__main__":
    unittest.main()
