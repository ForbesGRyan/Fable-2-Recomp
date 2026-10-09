import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import region_compare as rc  # noqa: E402


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def png_bytes(w, h, pixels, channels=3, filters=(0,), bit_depth=8, colour_type=None):
    """A PNG of `pixels` (bytes, w * h * channels), row y filtered with filters[y % len(filters)]."""
    bpp, stride = channels, w * channels
    raw, prev = bytearray(), bytes(stride)
    for y in range(h):
        row = pixels[y * stride:(y + 1) * stride]
        f = filters[y % len(filters)]
        out = bytearray(len(row))  # a short last row (a truncated picture) stays short
        for i in range(len(row)):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            pred = (0, a, b, (a + b) >> 1, _paeth(a, b, c))[f]
            out[i] = (row[i] - pred) & 255
        raw.append(f)
        raw += out
        prev = row

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    if colour_type is None:
        colour_type = {3: 2, 4: 6}[channels]
    data = zlib.compress(bytes(raw), 1)
    half = len(data) // 2  # two IDAT chunks: the reader must join them
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bit_depth, colour_type, 0, 0, 0))
            + chunk(b"tEXt", b"k\x00v") + chunk(b"IDAT", data[:half]) + chunk(b"IDAT", data[half:])
            + chunk(b"IEND", b""))


class Canvas:
    """An RGB picture filled with one colour, with rectangles painted in pixel coordinates."""

    def __init__(self, w, h, colour=(0, 0, 0)):
        self.w, self.h = w, h
        self.rows = [bytearray(bytes(colour) * w) for _ in range(h)]

    def fill(self, x0, y0, x1, y1, colour):
        for y in range(y0, y1):
            self.rows[y][x0 * 3:x1 * 3] = bytes(colour) * (x1 - x0)
        return self

    def image(self):
        return rc.Image(self.w, self.h, b"".join(bytes(r) for r in self.rows))

    def save(self, path):
        Path(path).write_bytes(png_bytes(self.w, self.h, b"".join(bytes(r) for r in self.rows)))
        return str(path)


def grey(v):
    return (v, v, v)


WHOLE = (0, 0, 100, 100)  # the client area of the 100 x 100 test pictures: all of it


class PngTest(unittest.TestCase):
    def test_every_filter_rgb_and_rgba(self):
        for channels in (3, 4):
            w, h, seed, values = 23, 40, 12345, []
            for _ in range(w * h * channels):  # a fixed noise: every branch of the Paeth predictor occurs
                seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
                values.append((seed >> 16) & 255)
            px = bytes(values)
            img = rc.decode_png(png_bytes(w, h, px, channels, filters=(0, 1, 2, 3, 4)))
            self.assertEqual((img.w, img.h), (w, h))
            want = px if channels == 3 else bytes(b for i, b in enumerate(px) if i % 4 != 3)
            self.assertEqual(img.rgb, want)

    def test_other_formats_refused(self):
        px = bytes(4 * 4 * 3)
        with self.assertRaisesRegex(rc.RegionError, "bit depth"):
            rc.decode_png(png_bytes(4, 4, px, bit_depth=16))
        with self.assertRaisesRegex(rc.RegionError, "colour type"):
            rc.decode_png(png_bytes(4, 4, px, colour_type=3))
        with self.assertRaisesRegex(rc.RegionError, "not a PNG"):
            rc.decode_png(b"GIF89a" + bytes(40))
        with self.assertRaisesRegex(rc.RegionError, "pixel data"):
            rc.decode_png(png_bytes(4, 4, px[:-3]))


class GeometryTest(unittest.TestCase):
    def test_client_area_of_the_drivers_window_shots(self):
        self.assertEqual(rc.client_area(1942, 1136), (11, 45, 1920, 1080))
        self.assertEqual(rc.client_area(1296, 759), (8, 31, 1280, 720))
        self.assertEqual(rc.client_area(640, 360), (0, 0, 640, 360))  # not a known window: the whole picture
        self.assertEqual(rc.client_area(1942, 1136, (0, 0, 1942, 1136)), (0, 0, 1942, 1136))
        with self.assertRaisesRegex(rc.RegionError, "client area"):
            rc.client_area(100, 100, (10, 10, 100, 50))
        with self.assertRaisesRegex(rc.RegionError, "client area"):
            rc.client_area(100, 100, (0, 0, 0, 50))

    def test_relative_rectangle_to_pixels(self):
        client = (11, 45, 1920, 1080)
        self.assertEqual(rc.to_pixels((0, 0, 1, 1), client), (11, 45, 1931, 1125))
        self.assertEqual(rc.to_pixels((0.25, 0.5, 0.5, 0.75), client), (491, 585, 971, 855))
        self.assertEqual(rc.to_pixels((0.36, 0.36, 0.84, 0.84), (0, 0, 10, 10)), (4, 4, 8, 8))  # to the nearest pixel

    def test_rectangle_outside_the_picture_refused(self):
        for rect in ((0.5, 0.5, 1.01, 0.9), (-0.01, 0, 0.5, 0.5), (0.2, 0.2, 0.9, 1.5), (0.6, 0.2, 0.4, 0.5),
                     (0.2, 0.5, 0.4, 0.5), (0.2, 0.2, float("nan"), 0.5), (0.2, 0.2, 0.5), "0,0,1,1",
                     (0.2, 0.2, 0.5, True)):
            with self.assertRaises(rc.RegionError, msg=repr(rect)):
                rc.to_pixels(rect, WHOLE)
        with self.assertRaisesRegex(rc.RegionError, "no pixel"):
            rc.to_pixels((0.501, 0.501, 0.504, 0.504), WHOLE)

    def test_title_bar_and_frame_are_not_measured(self):
        # A window shot: white frame and title bar around a client area of one grey.
        w, h = 1942, 1136
        row_frame = bytes([255]) * (w * 3)
        row_client = bytes([255]) * (11 * 3) + bytes([64]) * (1920 * 3) + bytes([255]) * (11 * 3)
        img = rc.Image(w, h, row_frame * 45 + row_client * 1080 + row_frame * 11)
        client = rc.client_area(w, h)
        for rect in ((0, 0, 1, 1), (0, 0, 0.01, 0.01), (0.99, 0.99, 1, 1)):
            s = rc.region_stats(img, rc.to_pixels(rect, client))
            self.assertAlmostEqual(s.lum, (64 / 255) ** 2, places=12, msg=repr(rect))
        # Taken as a whole picture the frame is measured too.
        s = rc.region_stats(img, rc.to_pixels((0, 0, 1, 1), rc.client_area(w, h, (0, 0, w, h))))
        self.assertGreater(s.lum, (64 / 255) ** 2 + 0.01)


class StatsTest(unittest.TestCase):
    def test_linear_is_the_square_of_the_display_value(self):
        self.assertEqual(rc.LINEAR[0], 0.0)
        self.assertEqual(rc.LINEAR[255], 1.0)
        self.assertAlmostEqual(rc.LINEAR[128], (128 / 255) ** 2, places=15)

    def test_mean_is_taken_in_linear_terms(self):
        # Half 0, half 200: the linear mean is half of 200's, not the linear value of 100.
        img = Canvas(100, 100).fill(0, 0, 50, 100, grey(200)).image()
        s = rc.region_stats(img, (0, 0, 100, 100))
        self.assertAlmostEqual(s.lum, 0.5 * (200 / 255) ** 2, places=12)
        self.assertEqual(s.pixels, 10000)

    def test_luminance_weights(self):
        for colour, weight in (((255, 0, 0), 0.2126), ((0, 255, 0), 0.7152), ((0, 0, 255), 0.0722)):
            s = rc.region_stats(Canvas(10, 10, colour).image(), (0, 0, 10, 10))
            self.assertAlmostEqual(s.lum, weight, places=12)

    def test_hue_of_known_colours(self):
        for colour, want in (((255, 0, 0), 0.0), ((255, 255, 0), 60.0), ((0, 255, 0), 120.0),
                             ((0, 255, 255), 180.0), ((0, 0, 255), 240.0), ((255, 0, 255), 300.0),
                             # of the linear means: squares 40000, 10000, 2500 -> 60 * 7500 / 37500
                             ((200, 100, 50), 12.0),
                             # negative before the wrap: squares 40000, 2500, 10000 -> 360 - 12
                             ((200, 50, 100), 348.0)):
            s = rc.region_stats(Canvas(10, 10, colour).image(), (0, 0, 10, 10))
            self.assertAlmostEqual(s.hue, want, places=9, msg=repr(colour))

    def test_grey_has_no_hue(self):
        self.assertIsNone(rc.region_stats(Canvas(10, 10, grey(90)).image(), (0, 0, 10, 10)).hue)
        self.assertIsNone(rc.region_stats(Canvas(10, 10).image(), (0, 0, 10, 10)).hue)
        self.assertIsNone(rc.hue_difference(None, 10.0))

    def test_hue_difference_wraps(self):
        self.assertAlmostEqual(rc.hue_difference(350.0, 10.0), -20.0)
        self.assertAlmostEqual(rc.hue_difference(10.0, 350.0), 20.0)
        self.assertAlmostEqual(rc.hue_difference(100.0, 90.0), 10.0)

    def test_clipped_share(self):
        img = Canvas(10, 10, (10, 20, 30)).fill(0, 0, 10, 2, (255, 20, 30)).fill(0, 2, 10, 3, (10, 20, 255)).image()
        self.assertAlmostEqual(rc.region_stats(img, (0, 0, 10, 10)).clipped, 0.3)

    def test_excluded_rectangles_are_left_out(self):
        img = Canvas(100, 100, grey(100)).fill(40, 40, 70, 70, grey(255)).image()
        s = rc.region_stats(img, (20, 20, 80, 80), [(40, 40, 70, 70)])
        self.assertAlmostEqual(s.lum, (100 / 255) ** 2, places=12)
        self.assertEqual(s.pixels, 60 * 60 - 30 * 30)
        self.assertEqual(s.excluded, 30 * 30)
        # Two rectangles that overlap each other and reach over the region's edge.
        s = rc.region_stats(img, (20, 20, 80, 80), [(40, 40, 60, 70), (50, 30, 90, 70), (0, 0, 10, 10)])
        self.assertAlmostEqual(s.lum, (100 / 255) ** 2, places=12)
        self.assertEqual(s.excluded, 20 * 30 + 30 * 40 - 10 * 30)
        self.assertEqual(s.pixels, 3600 - s.excluded)
        # Without the exclusion the bright square counts.
        self.assertGreater(rc.region_stats(img, (20, 20, 80, 80)).lum, (100 / 255) ** 2 + 0.05)

    def test_region_wholly_excluded_refused(self):
        img = Canvas(100, 100, grey(100)).image()
        with self.assertRaisesRegex(rc.RegionError, "excluded"):
            rc.region_stats(img, (20, 20, 40, 40), [(10, 10, 50, 50)])


def three(emu_before, native, emu_after, x0=20, y0=20, x1=60, y1=60, back=grey(30)):
    return [Canvas(100, 100, back).fill(x0, y0, x1, y1, c).image() for c in (emu_before, native, emu_after)]


REGION = {"name": "patch", "rect": [0.2, 0.2, 0.6, 0.6]}


class CompareTest(unittest.TestCase):
    def test_twice_as_bright_in_linear_terms_is_ratio_two(self):
        # Display value times sqrt(2) is linear value times 2 (141 is the rounded value).
        before, native, after = three(grey(100), grey(141), grey(100))
        row = rc.compare(before, native, after, [REGION])[0]
        self.assertAlmostEqual(row.ratio, (141 / 100) ** 2, places=12)
        self.assertAlmostEqual(row.ratio, 2.0, delta=0.02)
        self.assertEqual(row.name, "patch")

    def test_ratio_is_against_the_mean_of_the_two_emulated_shots(self):
        before, native, after = three(grey(100), grey(120), grey(140))
        row = rc.compare(before, native, after, [REGION])[0]
        emulated = (100 ** 2 + 140 ** 2) / 2
        self.assertAlmostEqual(row.ratio, 120 ** 2 / emulated, places=12)
        self.assertAlmostEqual(row.emulated.lum, emulated / 255 ** 2, places=12)

    def test_drift_between_the_emulated_shots(self):
        before, native, after = three(grey(100), grey(100), grey(110))
        row = rc.compare(before, native, after, [REGION])[0]
        self.assertAlmostEqual(row.drift, (110 / 100) ** 2 - 1, places=12)  # +21%
        before, native, after = three(grey(110), grey(100), grey(100))
        self.assertAlmostEqual(rc.compare(before, native, after, [REGION])[0].drift, (100 / 110) ** 2 - 1, places=12)

    def test_channel_ratios_and_hue_difference(self):
        before, native, after = three((200, 100, 50), (100, 100, 50), (200, 100, 50))
        row = rc.compare(before, native, after, [REGION])[0]
        self.assertAlmostEqual(row.channel_ratio[0], 0.25, places=12)
        self.assertAlmostEqual(row.channel_ratio[1], 1.0, places=12)
        self.assertAlmostEqual(row.channel_ratio[2], 1.0, places=12)
        self.assertAlmostEqual(row.emulated.hue, 12.0, places=9)
        self.assertAlmostEqual(row.native.hue, 60.0, places=9)  # red = green: yellow
        self.assertAlmostEqual(row.hue_diff, 48.0, places=9)

    def test_black_emulated_region_has_no_ratio(self):
        before, native, after = three(grey(0), grey(100), grey(0))
        row = rc.compare(before, native, after, [REGION])[0]
        self.assertIsNone(row.ratio)
        self.assertIsNone(row.drift)
        self.assertEqual(row.channel_ratio, (None, None, None))

    def test_pictures_of_different_sizes_refused(self):
        a, b = Canvas(100, 100).image(), Canvas(100, 99).image()
        for trio in ((a, b, a), (a, a, b), (b, a, a)):
            with self.assertRaisesRegex(rc.RegionError, "size"):
                rc.compare(*trio, [REGION])

    def test_rectangle_outside_refused_with_its_name(self):
        a = Canvas(100, 100).image()
        with self.assertRaisesRegex(rc.RegionError, "patch"):
            rc.compare(a, a, a, [{"name": "patch", "rect": [0.2, 0.2, 1.2, 0.6]}])

    def test_bad_region_lists_refused(self):
        a = Canvas(100, 100).image()
        with self.assertRaisesRegex(rc.RegionError, "twice"):
            rc.compare(a, a, a, [REGION, REGION])
        with self.assertRaisesRegex(rc.RegionError, "name"):
            rc.compare(a, a, a, [{"rect": [0, 0, 1, 1]}])
        with self.assertRaisesRegex(rc.RegionError, "rect"):
            rc.compare(a, a, a, [{"name": "x"}])

    def test_exclusion_in_relative_coordinates(self):
        # A "debug window" over the top left of the region in the native shot only.
        before, native, after = three(grey(100), grey(100), grey(100))
        native = Canvas(100, 100, grey(30)).fill(20, 20, 60, 60, grey(100)).fill(0, 0, 40, 40, (0, 255, 0)).image()
        row = rc.compare(before, native, after, [REGION], excludes=[(0.0, 0.0, 0.4, 0.4)])[0]
        self.assertAlmostEqual(row.ratio, 1.0, places=12)
        self.assertEqual(row.native.excluded, 400)
        self.assertEqual(row.emulated.pixels, 1200)  # the same pixels are left out of every picture
        self.assertNotAlmostEqual(rc.compare(before, native, after, [REGION])[0].ratio, 1.0, places=2)
        with self.assertRaises(rc.RegionError):
            rc.compare(before, native, after, [REGION], excludes=[(0.0, 0.0, 1.4, 0.4)])

    def test_calibration(self):
        self.assertAlmostEqual(rc.calibrated_exposure(0.5, 1.0), 2.0)
        self.assertAlmostEqual(rc.calibrated_exposure(0.5, 1.5), 3.0)
        self.assertAlmostEqual(rc.calibrated_exposure(1.25, 1.0), 0.8)
        for bad in (None, 0.0, -1.0, float("nan"), float("inf")):
            with self.assertRaises(rc.RegionError, msg=repr(bad)):
                rc.calibrated_exposure(bad, 1.0)
        for bad in (0.0, -2.0, float("nan"), float("inf")):
            with self.assertRaises(rc.RegionError, msg=repr(bad)):
                rc.calibrated_exposure(0.5, bad)

    def test_calibrated_exposure_makes_the_ratio_one(self):
        # The renderer's mapping: display = sqrt(saturate(linear * E)).
        def display(linear, e):
            return grey(round(255 * min(1.0, max(0.0, linear * e)) ** 0.5))

        linear, e0 = 0.09, 1.5
        before, native, after = three(grey(140), display(linear, e0), grey(140))
        row = rc.compare(before, native, after, [REGION])[0]
        e1 = rc.calibrated_exposure(row.ratio, e0)
        before, native, after = three(grey(140), display(linear, e1), grey(140))
        self.assertAlmostEqual(rc.compare(before, native, after, [REGION])[0].ratio, 1.0, delta=0.01)

    def test_pairs(self):
        regions = [{"name": "sun", "rect": [0.0, 0.0, 0.5, 1.0]}, {"name": "away", "rect": [0.5, 0.0, 1.0, 1.0]}]

        def pic(left, right):
            return Canvas(100, 100, grey(left)).fill(50, 0, 100, 100, grey(right)).image()

        rows = rc.compare(pic(200, 100), pic(150, 90), pic(200, 100), regions)
        p = rc.compare_pairs(rows, [{"name": "post", "a": "sun", "b": "away"}])[0]
        self.assertEqual((p.name, p.emulated_brighter, p.native_brighter, p.same), ("post", "sun", "sun", True))
        self.assertAlmostEqual(p.emulated_ratio, 4.0, places=12)
        self.assertAlmostEqual(p.native_ratio, (150 / 90) ** 2, places=12)
        rows = rc.compare(pic(200, 100), pic(90, 150), pic(200, 100), regions)
        p = rc.compare_pairs(rows, [{"name": "post", "a": "sun", "b": "away"}])[0]
        self.assertEqual((p.emulated_brighter, p.native_brighter, p.same), ("sun", "away", False))
        with self.assertRaisesRegex(rc.RegionError, "nowhere"):
            rc.compare_pairs(rows, [{"name": "post", "a": "sun", "b": "nowhere"}])
        with self.assertRaisesRegex(rc.RegionError, "pair"):
            rc.compare_pairs(rows, [{"name": "post", "a": "sun"}])


class MainTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def shots(self, before, native, after, size=(100, 100)):
        out = []
        for name, colours in (("before", before), ("native", native), ("after", after)):
            c = Canvas(size[0], size[1], grey(30))
            c.fill(0, 0, 50, 100, grey(colours[0])).fill(50, 0, 100, 100, grey(colours[1]))
            out.append(c.save(self.dir / (name + ".png")))
        return out

    def regions_file(self, doc, name="regions.json"):
        path = self.dir / name
        path.write_text(json.dumps(doc), encoding="utf-8")
        return str(path)

    def run_main(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = rc.main(argv)
        return code, out.getvalue(), err.getvalue()

    DOC = {"regions": [{"name": "dirt", "rect": [0.1, 0.1, 0.4, 0.9], "kind": "sunlit"},
                       {"name": "under tree", "rect": [0.6, 0.1, 0.9, 0.9], "kind": "shadowed", "note": "no shadows"}],
           "pairs": [{"name": "left and right", "a": "dirt", "b": "under tree"}]}

    def test_report(self):
        shots = self.shots((100, 50), (141, 100), (100, 50))
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(self.DOC)])
        self.assertEqual((code, err), (0, ""))
        dirt = [l for l in out.splitlines() if l.startswith("dirt")]
        self.assertEqual(len(dirt), 1)
        self.assertIn("sunlit", dirt[0])
        self.assertIn("1.988", dirt[0])  # (141 / 100)^2
        tree = [l for l in out.splitlines() if l.startswith("under tree")]
        self.assertIn("shadowed", tree[0])
        self.assertIn("4.000", tree[0])
        self.assertIn("client area: the whole picture", out)
        pair = [l for l in out.splitlines() if l.startswith("left and right")]
        self.assertIn("same", pair[0])
        self.assertNotIn("calibrate", out)

    def test_calibrate(self):
        shots = self.shots((100, 50), (50, 50), (100, 50))
        code, out, _ = self.run_main(shots + ["--regions", self.regions_file(self.DOC), "--calibrate", "dirt"])
        self.assertEqual(code, 0)
        line = [l for l in out.splitlines() if l.startswith("calibrate")]
        self.assertEqual(len(line), 1)
        self.assertIn("E = 4.0000", line[0])  # ratio 0.25 at E = 1
        code, out, _ = self.run_main(shots + ["--regions", self.regions_file(self.DOC), "--calibrate", "dirt",
                                              "--exposure", "1.5"])
        self.assertIn("E = 6.0000", out)
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(self.DOC), "--calibrate", "grass"])
        self.assertEqual(code, 2)
        self.assertIn("grass", err)

    def test_spots(self):
        doc = {"spots": {"bridge": dict(self.DOC, exclude=[[0.0, 0.0, 0.2, 1.0]]),
                         "trees": {"regions": [{"name": "trunk", "rect": [0.6, 0.1, 0.9, 0.9]}]}}}
        shots = self.shots((100, 50), (100, 50), (100, 50))
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(doc)])
        self.assertEqual(code, 2)
        self.assertIn("--spot", err)
        self.assertIn("bridge", err)
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(doc), "--spot", "trees"])
        self.assertEqual(code, 0)
        self.assertTrue(any(l.startswith("trunk") for l in out.splitlines()))
        self.assertFalse(any(l.startswith("dirt") for l in out.splitlines()))
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(doc), "--spot", "bridge"])
        self.assertEqual(code, 0)
        self.assertIn("excluded rectangles: 1", out)
        dirt = [l for l in out.splitlines() if l.startswith("dirt")][0]
        self.assertIn(" 1600 ", dirt)  # 30 x 80 pixels less the 10 x 80 under the excluded rectangle
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(doc), "--spot", "town"])
        self.assertEqual(code, 2)

    def test_exclude_option_and_client_option(self):
        shots = self.shots((100, 50), (100, 50), (100, 50))
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(self.DOC),
                                                "--exclude", "0,0,0.2,1", "--exclude", "0.8,0,1,1"])
        self.assertEqual(code, 0)
        self.assertIn("excluded rectangles: 2", out)
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(self.DOC), "--exclude", "0,0,2,1"])
        self.assertEqual(code, 2)
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(self.DOC), "--client", "0,0,50,100"])
        self.assertEqual(code, 0)
        self.assertIn("client area: 50 x 100 at 0, 0", out)
        # Both regions now lie in the left half: the right one reads the left half's values.
        tree = [l for l in out.splitlines() if l.startswith("under tree")][0]
        self.assertIn("1.000", tree)

    def test_refusals_exit_with_a_message(self):
        shots = self.shots((100, 50), (100, 50), (100, 50))
        small = Canvas(100, 90, grey(30)).save(self.dir / "small.png")
        code, out, err = self.run_main([shots[0], small, shots[2], "--regions", self.regions_file(self.DOC)])
        self.assertEqual(code, 2)
        self.assertIn("size", err)
        bad = dict(self.DOC, regions=[{"name": "off", "rect": [0.5, 0.5, 1.5, 0.9]}], pairs=[])
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(bad, "bad.json")])
        self.assertEqual(code, 2)
        self.assertIn("off", err)
        code, out, err = self.run_main(shots + ["--regions", str(self.dir / "missing.json")])
        self.assertEqual(code, 2)
        (self.dir / "broken.json").write_text("{", encoding="utf-8")
        code, out, err = self.run_main(shots + ["--regions", str(self.dir / "broken.json")])
        self.assertEqual(code, 2)

    def test_window_shot_is_reported_by_its_client_area(self):
        shots = self.shots((100, 100), (100, 100), (100, 100), size=(1296, 759))
        code, out, err = self.run_main(shots + ["--regions", self.regions_file(self.DOC)])
        self.assertEqual(code, 0)
        self.assertIn("client area: 1280 x 720 at 8, 31", out)


def step_picture(at, dark=60, bright=160, size=(100, 100), vertical=False, ramp=0):
    """A picture dark before `at` and bright from it on (along x; along y if vertical); `ramp` pixels of a
    linear rise in display levels start at `at` instead of a hard step."""
    c = Canvas(size[0], size[1], grey(dark))
    for k in range(at, size[1] if vertical else size[0]):
        v = bright if k >= at + ramp else dark + (bright - dark) * (k - at + 1) // (ramp + 1)
        if vertical:
            c.fill(0, k, size[0], k + 1, grey(v))
        else:
            c.fill(k, 0, k + 1, size[1], grey(v))
    return c.image()


class EdgeTest(unittest.TestCase):
    """--edge: where the steepest luminance step lies along a named segment, picture by picture."""

    LINE = {"name": "post shadow", "line": [0.1, 0.5, 0.9, 0.5]}

    def test_a_step_is_found_where_it_is(self):
        profile = rc.edge_profile(step_picture(40), WHOLE, self.LINE["line"], 3)
        self.assertEqual(len(profile), 81)  # x 10 to 90, one sample a pixel
        self.assertAlmostEqual(profile[0], rc.LINEAR[60], places=12)
        self.assertAlmostEqual(profile[-1], rc.LINEAR[160], places=12)
        step = rc.steepest_step(profile)
        self.assertAlmostEqual(step.position, 29.5, places=9)  # between x 39 and x 40, 10 from the start
        self.assertTrue(step.rising)
        self.assertAlmostEqual(step.contrast, rc.LINEAR[160] / rc.LINEAR[60], places=9)

    def test_a_shifted_edge_gives_the_shift(self):
        rows = rc.compare_edges(step_picture(40), step_picture(45), step_picture(40), [self.LINE])
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row.name, "post shadow")
        self.assertAlmostEqual(row.difference, 5.0, places=9)
        self.assertAlmostEqual(row.spread, 0.0, places=9)
        rows = rc.compare_edges(step_picture(40), step_picture(38), step_picture(44), [self.LINE])
        self.assertAlmostEqual(rows[0].difference, -4.0, places=9)  # 38 against the mean of 40 and 44
        self.assertAlmostEqual(rows[0].spread, 4.0, places=9)

    def test_a_falling_step_and_a_vertical_segment(self):
        line = {"name": "down", "line": [0.5, 0.2, 0.5, 0.8]}
        imgs = [step_picture(at, dark=160, bright=60, vertical=True) for at in (50, 53, 50)]
        row = rc.compare_edges(*imgs, [line])[0]
        self.assertAlmostEqual(row.difference, 3.0, places=9)
        self.assertFalse(row.native.rising)

    def test_a_soft_edge_is_placed_at_its_middle(self):
        hard = rc.steepest_step(rc.edge_profile(step_picture(50), WHOLE, self.LINE["line"], 3)).position
        soft = rc.steepest_step(rc.edge_profile(step_picture(40, ramp=20), WHOLE, self.LINE["line"], 3)).position
        self.assertLess(abs(soft - hard), 3.0)  # the ramp runs from x 40 to x 60: its steepest part is past 50
        a, b = (step_picture(at, ramp=20) for at in (30, 37))
        self.assertAlmostEqual(rc.compare_edges(a, b, a, [self.LINE])[0].difference, 7.0, places=6)

    def test_the_shift_in_the_games_pixels(self):
        # A 1296 x 759 window shot shows the game's 1120 x 720 picture on 1280 x 720: 8 pixels across are 7.
        size = (1296, 759)
        across = {"name": "across", "line": [0.1, 0.5, 0.9, 0.5]}
        imgs = [step_picture(at, size=size) for at in (600, 608, 600)]
        row = rc.compare_edges(*imgs, [across])[0]
        self.assertAlmostEqual(row.difference, 8.0, places=9)
        self.assertAlmostEqual(row.game_difference, 7.0, places=9)
        down = {"name": "down", "line": [0.5, 0.1, 0.5, 0.9]}
        imgs = [step_picture(at, size=size, vertical=True) for at in (300, 308, 300)]
        row = rc.compare_edges(*imgs, [down])[0]
        self.assertAlmostEqual(row.difference, 8.0, places=9)
        self.assertAlmostEqual(row.game_difference, 8.0, places=9)

    def test_no_edge_is_refused(self):
        flat = Canvas(100, 100, grey(90)).image()
        with self.assertRaisesRegex(rc.RegionError, "post shadow.*no step"):
            rc.compare_edges(flat, flat, flat, [self.LINE])
        faint = step_picture(40, dark=100, bright=105)  # 1.10 in linear light: under the stated 1.25
        with self.assertRaisesRegex(rc.RegionError, "no step"):
            rc.compare_edges(faint, faint, faint, [self.LINE])
        self.assertEqual(len(rc.compare_edges(faint, faint, faint, [dict(self.LINE, contrast=1.05)])), 1)
        # One picture without the step is enough to refuse, and the message names it.
        with self.assertRaisesRegex(rc.RegionError, "native"):
            rc.compare_edges(step_picture(40), flat, step_picture(40), [self.LINE])

    def test_steps_of_opposite_direction_are_refused(self):
        up, down = step_picture(40), step_picture(40, dark=160, bright=60)
        with self.assertRaisesRegex(rc.RegionError, "direction"):
            rc.compare_edges(up, down, up, [self.LINE])

    def test_a_segment_outside_the_picture_is_refused(self):
        img = step_picture(40)
        for line in ([0.1, 0.5, 1.2, 0.5], [-0.1, 0.5, 0.9, 0.5], [0.5, 0.5, 0.5, 0.5], [0.1, 0.5, 0.9],
                     [0.1, 0.5, 0.9, float("nan")], "0.1,0.5,0.9,0.5", [0.50, 0.5, 0.53, 0.5]):
            with self.assertRaises(rc.RegionError, msg=repr(line)):
                rc.compare_edges(img, img, img, [{"name": "bad", "line": line}])
        # A band that leaves the picture at the border is refused too, not read from the other side.
        with self.assertRaisesRegex(rc.RegionError, "edge"):
            rc.compare_edges(img, img, img, [{"name": "edge", "line": [0.1, 0.0, 0.9, 0.0], "width": 5}])
        for bad in ({"line": [0.1, 0.5, 0.9, 0.5]}, {"name": "x"}, {"name": "x", "line": [0.1, 0.5, 0.9, 0.5], "width": 0},
                    {"name": "x", "line": [0.1, 0.5, 0.9, 0.5], "window": 0},
                    {"name": "x", "line": [0.1, 0.5, 0.9, 0.5], "contrast": 1.0}):
            with self.assertRaises(rc.RegionError, msg=repr(bad)):
                rc.compare_edges(img, img, img, [bad])

    def test_pictures_of_different_sizes_are_refused(self):
        with self.assertRaisesRegex(rc.RegionError, "size"):
            rc.compare_edges(step_picture(40), step_picture(40, size=(100, 90)), step_picture(40), [self.LINE])


class EdgeMainTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def shots(self, positions):
        out = []
        for name, at in zip(("before", "native", "after"), positions):
            c = Canvas(100, 100, grey(60)).fill(at, 0, 100, 100, grey(160))
            out.append(c.save(self.dir / (name + ".png")))
        return out

    def regions_file(self, doc):
        path = self.dir / "regions.json"
        path.write_text(json.dumps(doc), encoding="utf-8")
        return str(path)

    def run_main(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = rc.main(argv)
        return code, out.getvalue(), err.getvalue()

    DOC = {"regions": [{"name": "dirt", "rect": [0.1, 0.1, 0.3, 0.9]}],
           "edges": [{"name": "post shadow", "line": [0.1, 0.5, 0.9, 0.5], "note": "the post is static"},
                     {"name": "nothing there", "line": [0.6, 0.5, 0.9, 0.5]}]}

    def test_edge_report(self):
        code, out, err = self.run_main(self.shots((40, 46, 42)) + ["--regions", self.regions_file(self.DOC),
                                                                   "--edge", "post shadow"])
        self.assertEqual((code, err), (0, ""))
        line = [l for l in out.splitlines() if l.startswith("post shadow")]
        self.assertEqual(len(line), 1)
        self.assertIn("+5.0", line[0])  # 46 against the mean of 40 and 42
        self.assertIn("+2.0", line[0])  # the emulated shots' own spread
        self.assertFalse(any(l.startswith("dirt") for l in out.splitlines()))  # an edge run measures edges only

    def test_edge_refusals(self):
        shots = self.shots((40, 46, 42))
        regions = self.regions_file(self.DOC)
        code, out, err = self.run_main(shots + ["--regions", regions, "--edge", "nothing there"])
        self.assertEqual(code, 2)
        self.assertIn("no step", err)
        code, out, err = self.run_main(shots + ["--regions", regions, "--edge", "rail"])
        self.assertEqual(code, 2)
        self.assertIn("rail", err)
        no_edges = self.regions_file({"regions": self.DOC["regions"]})
        code, out, err = self.run_main(shots + ["--regions", no_edges, "--edge", "post shadow"])
        self.assertEqual(code, 2)


class ShadowRegionsFileTest(unittest.TestCase):
    """docs/native-renderer/shadow-regions.json, the regions and edges of the shadows validation."""

    PATH = Path(__file__).resolve().parent.parent / "docs" / "native-renderer" / "shadow-regions.json"

    def test_every_spot_loads_and_every_rectangle_and_segment_lies_in_the_picture(self):
        doc = json.loads(self.PATH.read_text(encoding="utf-8"))
        self.assertEqual(sorted(doc["spots"]), ["bridge", "third", "trees"])
        kinds = set(doc["kinds"])
        img = rc.Image(1296, 759, bytes([90, 120, 60]) * (1296 * 759))
        counted_edges = 0
        for spot in sorted(doc["spots"]):
            regions, pairs, excludes = rc.load_regions(self.PATH, spot)
            self.assertEqual(len({r["name"] for r in regions}), len(regions), spot)
            for r in regions:
                self.assertIn(r["kind"], kinds, "%s: '%s'" % (spot, r["name"]))
                self.assertTrue(r.get("note"), "%s: '%s' says nothing about its caster" % (spot, r["name"]))
            self.assertEqual(len(rc.compare(img, img, img, regions, excludes)), len(regions))
            edges = rc.load_edges(self.PATH, spot)
            self.assertEqual(len({e["name"] for e in edges}), len(edges), spot)
            for e in edges:
                self.assertTrue(e.get("note"), "%s: edge '%s' says nothing about its caster" % (spot, e["name"]))
                self.assertGreater(len(rc.edge_profile(img, rc.client_area(img.w, img.h), e["line"],
                                                       e.get("width", rc.EDGE_WIDTH))), 2 * rc.EDGE_WINDOW)
            counted_edges += len(edges)
        self.assertGreaterEqual(counted_edges, 3)  # spec criterion 5: three named static shadow edges


class RegionsFileTest(unittest.TestCase):
    """docs/native-renderer/lighting-regions.json, the regions of the lighting validation."""

    PATH = Path(__file__).resolve().parent.parent / "docs" / "native-renderer" / "lighting-regions.json"
    SPEC_REGIONS = ("dirt patch", "grass beside it", "bridge planks", "rock face", "trunk", "hero's shirt")

    def spots(self):
        return sorted(json.loads(self.PATH.read_text(encoding="utf-8"))["spots"])

    def test_the_three_spots_load_and_every_rectangle_holds_pixels(self):
        self.assertEqual(self.spots(), ["bridge", "third", "trees"])
        kinds = set(json.loads(self.PATH.read_text(encoding="utf-8"))["kinds"])
        for spot in self.spots():
            regions, pairs, excludes = rc.load_regions(self.PATH, spot)
            for client in ((11, 45, 1920, 1080), (8, 31, 1280, 720)):
                for r in list(regions) + [{"rect": e} for e in excludes]:
                    rc.to_pixels(r["rect"], client)
            self.assertEqual(len({r["name"] for r in regions}), len(regions), spot)
            for r in regions:
                self.assertIn(r["kind"], kinds, "%s: '%s'" % (spot, r["name"]))
            names = {r["name"] for r in regions}
            for p in pairs:
                self.assertIn(p["a"], names, spot)
                self.assertIn(p["b"], names, spot)

    def test_the_specs_six_regions_and_five_pairs(self):
        regions, pairs, _ = rc.load_regions(self.PATH, "bridge")
        by_name = {r["name"]: r for r in regions}
        for name in self.SPEC_REGIONS:
            self.assertEqual(by_name[name]["kind"], "sunlit", name)
        total = sum(len(rc.load_regions(self.PATH, s)[1]) for s in self.spots())
        self.assertGreaterEqual(total, 5)

    def test_runs_on_a_picture_of_the_shots_size(self):
        img = rc.Image(1942, 1136, bytes([90, 120, 60]) * (1942 * 1136))
        for spot in self.spots():
            regions, pairs, excludes = rc.load_regions(self.PATH, spot)
            rows = rc.compare(img, img, img, regions, excludes)
            self.assertEqual(len(rows), len(regions))
            for row in rows:
                self.assertAlmostEqual(row.ratio, 1.0, places=12, msg=row.name)
                self.assertEqual(row.native.excluded, 0, "%s: '%s'" % (spot, row.name))
            self.assertEqual(len(rc.compare_pairs(rows, pairs)), len(pairs))


if __name__ == "__main__":
    unittest.main()
