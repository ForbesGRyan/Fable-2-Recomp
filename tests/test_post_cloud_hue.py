"""tools/xdk_sigmatch/post_cloud_hue.py: the native clouds against the game's scene by hue (plan Task 18 of the
post sub-project; review of plan Tasks 16 and 17, Important 2): the maps, the smooth part taken off, the search
over shifts, the mirrored control, the rule, and the tool end to end on made pictures."""
import contextlib
import io
import math
import struct
import sys
import tempfile
import unittest
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import post_cloud_hue as ch  # noqa: E402
import region_compare as rc  # noqa: E402
from texture_thumb import write_png  # noqa: E402

W, H = 320, 160          # a made scene: the band is 192 x 19 pixels at the default fractions: 24 x 2 blocks
BAND = (0.25, 0.10, 0.85, 0.60)   # the tests' own band: 192 x 80 pixels, 24 x 10 blocks


def pattern(bx, by):
    """A cloud-like pattern over blocks, with no left-right symmetry: negative where a 'cloud' lowers the blue."""
    return -0.04 * (math.sin(0.9 * bx + 0.31 * by * by) * math.cos(0.5 * by + 0.07 * bx * bx) > 0.35)


def sky(x, y):
    """The sky's smooth blue share: a gradient the detrending must take off."""
    return 0.45 - 0.0004 * y + 0.00005 * x


def colour(share, level=0.5):
    """A linear colour of the given blue share with r = g."""
    return (level * (1 - share) / 2, level * (1 - share) / 2, level * share)


def scene_file(path, shift=0, sign=1.0, clouds=True):
    x0, y0, cols, rows = ch.band_blocks(W, H, BAND)
    floats = array("f")
    for y in range(H):
        for x in range(W):
            s = sky(x, y)
            if clouds and x0 <= x < x0 + cols * 8 and y0 <= y < y0 + rows * 8:
                s += sign * pattern((x - x0) // 8 - shift, (y - y0) // 8)
            floats.extend(colour(s, 2.0))
    Path(path).write_bytes(b"F2OS" + struct.pack("<II", W, H) + floats.tobytes())


def to_srgb(v):
    v = min(max(v, 0.0), 1.0)
    return int(round(255 * (12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055)))


def shot_file(path, shift=0, clouds=True, sign=1.0, scale=1):
    """A screenshot of scale times the scene's size (no window frame: an unknown size is all picture)."""
    x0, y0, cols, rows = ch.band_blocks(W, H, BAND)
    rgba = bytearray()
    for sy in range(H * scale):
        for sx in range(W * scale):
            x, y = sx // scale, sy // scale
            s = sky(x, y)
            if clouds and x0 <= x < x0 + cols * 8 and y0 <= y < y0 + rows * 8:
                s += sign * pattern((x - x0) // 8 - shift, (y - y0) // 8)
            rgba += bytes(to_srgb(c) for c in colour(s)) + b"\xff"
    write_png(str(path), W * scale, H * scale, bytes(rgba), max_edge=W * scale)


class Pieces(unittest.TestCase):
    def test_blue_share_and_the_curve(self):
        self.assertAlmostEqual(ch.blue_share(1.0, 1.0, 2.0), 0.5)
        self.assertEqual(ch.blue_share(0.0, 0.0, 0.0), 0.0)
        self.assertAlmostEqual(ch.srgb_to_linear(0.04045), 0.04045 / 12.92)
        self.assertAlmostEqual(ch.srgb_to_linear(1.0), 1.0)
        self.assertAlmostEqual(ch.srgb_to_linear(0.5), 0.21404, places=4)

    def test_band_in_whole_blocks(self):
        self.assertEqual(ch.band_blocks(1120, 720), (336, 14, 84, 10))
        self.assertEqual(ch.band_blocks(W, H, BAND), (80, 16, 24, 10))
        with self.assertRaises(ch.HueError):
            ch.band_blocks(64, 64)

    def test_detrend_takes_a_quadratic_surface_off_and_keeps_the_rest(self):
        surface = [[0.3 + 0.01 * x - 0.02 * y + 0.001 * x * x + 0.002 * y * y - 0.0005 * x * y for x in range(20)] for y in range(8)]
        for row in ch.detrend(surface):
            for v in row:
                self.assertAlmostEqual(v, 0.0, places=9)
        bumpy = [[surface[y][x] + pattern(x, y) for x in range(20)] for y in range(8)]
        left = ch.detrend(bumpy)
        want = ch.detrend([[pattern(x, y) for x in range(20)] for y in range(8)])
        for a, b in zip(left, want):
            for u, v in zip(a, b):
                self.assertAlmostEqual(u, v, places=9)

    def test_correlation(self):
        self.assertAlmostEqual(ch.correlation([1, 2, 3, 4], [2, 4, 6, 8]), 1.0)
        self.assertAlmostEqual(ch.correlation([1, 2, 3, 4], [8, 6, 4, 2]), -1.0)
        self.assertEqual(ch.correlation([1, 1, 1, 1], [1, 2, 3, 4]), 0.0)      # (a map that does not vary)
        self.assertEqual(ch.correlation([1, 2], [1, 2]), 0.0)
        self.assertEqual(ch.correlation([1, 2, 3], [1, 2]), 0.0)

    def test_the_search_finds_a_shift_and_its_direction(self):
        game = [[pattern(x, y) for x in range(24)] for y in range(10)]
        for dx, dy in ((0, 0), (3, 0), (-5, 1), (2, -2)):
            native = [[pattern(x - dx, y - dy) for x in range(24)] for y in range(10)]
            r, bx, by = ch.best_shift(native, game)
            self.assertEqual((bx, by), (dx, dy))
            self.assertAlmostEqual(r, 1.0)
        with self.assertRaises(ch.HueError):
            ch.best_shift(game, [row[:-1] for row in game])

    def test_the_rule(self):
        self.assertTrue(ch.confirmed(0.78, 0.10))
        self.assertTrue(ch.confirmed(0.5, 0.25))
        self.assertFalse(ch.confirmed(0.49, 0.0))        # too weak
        self.assertFalse(ch.confirmed(0.78, 0.40))       # the mirrored map does nearly as well: no place shown
        self.assertFalse(ch.confirmed(-0.9, -1.0))       # the wrong sign is never confirmed

    def test_compare_on_maps(self):
        game = [[0.4 + 0.001 * x + pattern(x, y) for x in range(24)] for y in range(10)]
        off = [[0.38 + 0.002 * y for x in range(24)] for y in range(10)]
        on = [[off[y][x] + 0.5 * pattern(x - 2, y) for x in range(24)] for y in range(10)]
        c = ch.compare(game, on, off)
        self.assertTrue(c["confirmed"])
        self.assertEqual(c["best"][1:], (2, 0))
        self.assertGreater(c["best"][0], 0.98)      # (not 1: each map loses its own smooth part)
        self.assertAlmostEqual(c["slope"], 0.5, delta=0.03)
        self.assertLess(c["control"][0], 0.5)
        self.assertLess(c["mean"], 0.0)
        # the switch moving the hue the other way: a strong NEGATIVE correlation, not confirmed
        wrong = [[off[y][x] - 0.5 * pattern(x - 2, y) for x in range(24)] for y in range(10)]
        self.assertFalse(ch.compare(game, wrong, off)["confirmed"])
        # the switch changing nothing: not confirmed
        self.assertFalse(ch.compare(game, off, off)["confirmed"])
        # a native pattern that is not the game's: not confirmed
        other = [[off[y][x] - 0.02 * ((x * 7 + y * 13) % 5 == 0) for x in range(24)] for y in range(10)]
        self.assertFalse(ch.compare(game, other, off)["confirmed"])


class EndToEnd(unittest.TestCase):
    def run_tool(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = ch.main(argv)
        return code, out.getvalue(), err.getvalue()

    def test_made_pictures(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            scene_file(tmp / "g.scene")
            shot_file(tmp / "on.png", shift=3, scale=2)       # the clouds have drifted three blocks; a larger shot
            shot_file(tmp / "off.png", clouds=False, scale=2)
            shot_file(tmp / "inverse.png", shift=3, sign=-1.0, scale=2)
            band = ["--band"] + [str(v) for v in BAND]
            code, text, _ = self.run_tool([str(tmp / "g.scene"), "--pair", str(tmp / "on.png"), str(tmp / "off.png")] + band)
            self.assertEqual(code, 0, text)
            self.assertIn("at (+3, +0) blocks", text)
            self.assertIn("CONFIRMED", text)
            self.assertNotIn("NOT CONFIRMED", text)
            # the shot's block means are the made values (the mapping of a block onto a larger shot)
            share, _ = ch.shot_map(rc.read_png(str(tmp / "off.png")), W, H, BAND)
            self.assertAlmostEqual(share[4][7], sky(80 + 7 * 8 + 3.5, 16 + 4 * 8 + 3.5), places=3)
            # the hue moved the other way, and no clouds at all: exit code 1
            code, text, _ = self.run_tool([str(tmp / "g.scene"), "--pair", str(tmp / "inverse.png"), str(tmp / "off.png")] + band)
            self.assertEqual(code, 1)
            self.assertIn("NOT CONFIRMED", text)
            code, text, _ = self.run_tool([str(tmp / "g.scene"), "--pair", str(tmp / "off.png"), str(tmp / "off.png")] + band)
            self.assertEqual(code, 1)
            # one bad pair among good ones fails the run
            code, _, _ = self.run_tool([str(tmp / "g.scene"), "--pair", str(tmp / "on.png"), str(tmp / "off.png"),
                                        "--pair", str(tmp / "off.png"), str(tmp / "off.png")] + band)
            self.assertEqual(code, 1)
            # a file that is no scene: exit code 2, a message, no traceback
            (tmp / "bad.scene").write_bytes(b"nope")
            code, _, err = self.run_tool([str(tmp / "bad.scene"), "--pair", str(tmp / "on.png"), str(tmp / "off.png")])
            self.assertEqual(code, 2)
            self.assertIn("not a scene file", err)

    def test_scene_file_is_read_whole_or_not_at_all(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "short.scene"
            p.write_bytes(b"F2OS" + struct.pack("<II", 4, 4) + b"\0" * 100)
            with self.assertRaises(ch.HueError):
                ch.read_scene(p)
            p.write_bytes(b"F2OS" + struct.pack("<II", 2, 1) + struct.pack("<6f", 1, 2, 3, 4, 5, 6))
            w, h, floats = ch.read_scene(p)
            self.assertEqual((w, h, list(floats)), (2, 1, [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]))


if __name__ == "__main__":
    unittest.main()
