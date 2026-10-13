"""lantern_regions.py: the lantern disc measure on paired scenes (local lights, plan Task 9)."""
import contextlib
import io
import math
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import lantern_regions as lr  # noqa: E402

W, H = 1120, 720
F = 1.0  # 1 / tan(fov / 2)


def view(f=F):
    """c0..c3 of a camera at the origin looking down -z, y up: clip = (f/a x, f y, -z, -z)."""
    a = W / H
    return [[f / a, 0.0, 0.0, 0.0], [0.0, f, 0.0, 0.0], [0.0, 0.0, -1.0, -0.1], [0.0, 0.0, -1.0, 0.0]]


def scenes(native_gain=1.0, at=(0.0, 0.0, -10.0), radius=2.0):
    """A dim flat game and native scene, each with a bright disc around the lantern's ground point."""
    game = np.full((H, W, 3), 0.1, dtype=np.float32)
    native = game.copy()
    px, py, r = lr.disc_of(view(), lr.ground_point(at), radius, W, H)
    mask = lr.disc_mask((H, W), px, py, r)
    game[mask] = (1.0, 0.5, 0.2)
    native[mask] = np.array((1.0, 0.5, 0.2), dtype=np.float32) * native_gain
    return native, game


class DiscGeometry(unittest.TestCase):
    def test_ground_point_lowers_z(self):
        self.assertEqual(lr.ground_point((1.0, 2.0, 3.0)), (1.0, 2.0, 2.5))

    def test_disc_centre_and_radius(self):
        px, py, r = lr.disc_of(view(), (0.0, 0.0, -10.5), 2.0, W, H)
        self.assertAlmostEqual(px, W / 2)
        self.assertAlmostEqual(py, H / 2)
        # radius * (H / 2) / (w * tan(fov / 2)), w = 10.5
        self.assertAlmostEqual(r, 2.0 * (H / 2) * F / 10.5)

    def test_radius_equals_a_point_two_units_aside(self):
        px, py, r = lr.disc_of(view(), (0.0, 0.0, -10.5), 2.0, W, H)
        sx, _ = lr.tr.project(view(), 2.0, 0.0, -10.5, W, H)
        self.assertAlmostEqual(sx - px, r, places=6)

    def test_behind_the_eye_is_none(self):
        self.assertIsNone(lr.disc_of(view(), (0.0, 0.0, 5.0), 2.0, W, H))

    def test_mask_area(self):
        m = lr.disc_mask((H, W), 500.0, 300.0, 20.0)
        self.assertLess(abs(int(m.sum()) - math.pi * 400.0), 0.05 * math.pi * 400.0)


class DiscMeasure(unittest.TestCase):
    def test_ratio_one_where_both_have_the_disc(self):
        native, game = scenes(1.0)
        (row,) = lr.measure(native, game, view(), [(0.0, 0.0, -10.0)], 2.0)
        np.testing.assert_allclose(row["ratio"], (1.0, 1.0, 1.0), rtol=1e-6)
        np.testing.assert_allclose(row["game"], (1.0, 0.5, 0.2), rtol=1e-6)
        self.assertGreater(row["pixels"], 1000)

    def test_ratio_half_when_native_disc_is_half(self):
        native, game = scenes(0.5)
        (row,) = lr.measure(native, game, view(), [(0.0, 0.0, -10.0)], 2.0)
        np.testing.assert_allclose(row["ratio"], (0.5, 0.5, 0.5), rtol=1e-6)

    def test_scale_applies_to_native(self):
        native, game = scenes(0.5)
        (row,) = lr.measure(native, game, view(), [(0.0, 0.0, -10.0)], 2.0, scale=2.0)
        np.testing.assert_allclose(row["ratio"], (1.0, 1.0, 1.0), rtol=1e-6)

    def test_non_finite_pixels_are_left_out(self):
        native, game = scenes(1.0)
        native[H // 2, W // 2] = np.nan
        (row,) = lr.measure(native, game, view(), [(0.0, 0.0, -10.0)], 2.0)
        self.assertEqual(row["bad"], 1)
        np.testing.assert_allclose(row["ratio"], (1.0, 1.0, 1.0), rtol=1e-6)

    def test_off_screen_lantern_is_reported_not_measured(self):
        native, game = scenes(1.0)
        (row,) = lr.measure(native, game, view(), [(0.0, 0.0, 10.0)], 2.0)
        self.assertEqual(row["pixels"], 0)
        self.assertIsNone(row["ratio"])

    def test_outside_regions_leave_the_disc_out(self):
        native, game = scenes(0.5)
        union = lr.discs_union((H, W), view(), [(0.0, 0.0, -10.0)], 2.0)
        rows = lr.regions_outside(native, game, {"whole": (0, 0, W, H)}, union)
        np.testing.assert_allclose(rows["whole"]["ratio"], (1.0, 1.0, 1.0), rtol=1e-6)
        self.assertEqual(rows["whole"]["pixels"], W * H - int(union.sum()))


class Inputs(unittest.TestCase):
    def test_parse_lanterns(self):
        self.assertEqual(lr.parse_lanterns("1,2,3; 4.5,-6,7"), [(1.0, 2.0, 3.0), (4.5, -6.0, 7.0)])
        with self.assertRaises(ValueError):
            lr.parse_lanterns("1,2")

    def test_lanterns_of_rows_unique(self):
        l1 = [93.75, 161.32, 42.38, 1.0, 1.6, 0.75, 0.09, 0, 0.25, 0, 0, 0, 0, 0, 1]
        l2 = [115.88, 158.23, 41.46, 1.0, 1.5, 0.7, 0.08, 0, 0.25, 0, 0, 0, 0, 0, 1]
        rows = [{"lin": {"lset": {"reason": "ok", "count": 2, "lanterns": [l1, l2]}}},
                {"lin": {"lset": {"reason": "ok", "count": 1, "lanterns": [l1]}}},
                {"lin": {"lset": {"reason": "no-upload", "count": 0}}},
                {"lin": {"reason": "light-unknown"}}, {}]
        got = lr.lanterns_of_rows(rows)
        self.assertEqual([g["pos"] for g in got], [(93.75, 161.32, 42.38), (115.88, 158.23, 41.46)])
        self.assertEqual(got[0]["rows"], 2)
        self.assertEqual(got[1]["col"], (1.5, 0.7, 0.08))

    def test_view_of_rows_takes_a_tess_row(self):
        c = view()
        rows = [{"kind": "draw", "consts": {"c0": [9, 9, 9, 9]}},
                {"kind": "tess", "consts": {"c0": c[0], "c1": c[1], "c2": c[2], "c3": c[3]}}]
        self.assertEqual(lr.view_of_rows(rows), c)

    def test_camera_of(self):
        np.testing.assert_allclose(lr.camera_of(moved_view((3.0, -2.0, 7.0))), (3.0, -2.0, 7.0), atol=1e-9)

    def test_view_of_rows_picks_the_view_at_the_eye(self):
        main, mirror = moved_view((0.0, 0.0, 50.0)), moved_view((0.0, 0.0, -10.0))
        tess = lambda c: {"kind": "tess", "consts": {"c0": c[0], "c1": c[1], "c2": c[2], "c3": c[3]}}
        rows = [tess(mirror)] * 5 + [tess(main)] * 2
        self.assertEqual(lr.view_of_rows(rows), mirror)  # most rows without an eye
        self.assertEqual(lr.view_of_rows(rows, eye=(0.0, 0.0, 50.0)), main)


def moved_view(eye):
    """view() with its camera moved to `eye`: c . (p - eye)."""
    out = []
    for r in view():
        out.append(r[:3] + [r[3] - sum(r[k] * eye[k] for k in range(3))])
    return out


class SelfTest(unittest.TestCase):
    def test_self_test_passes(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(lr.main(["--self-test"]), 0)
        self.assertIn("self-test: PASS", out.getvalue())


if __name__ == "__main__":
    unittest.main()
