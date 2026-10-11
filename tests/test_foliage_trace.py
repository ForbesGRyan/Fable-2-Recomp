"""tools/xdk_sigmatch/foliage_trace.py (foliage plan Task 17; spec criteria 15 to 17, ruling 18): the trace's
arithmetic on made arrays (the classes' parts sum to the region's ratio less 1, values that are no numbers are
counted out and said, the verdict's bound), the stored native scene's file on made bytes, the holes count, the
classes of a made depth copy, the redraw's kept set, the half-pixel fit on a made shift, the model of
criterion 17 on made numbers."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import foliage_trace as ft  # noqa: E402


class Trace(unittest.TestCase):
    def test_shares_reproduce_a_made_shortfall(self):
        game = np.ones((4, 4, 3))
        native = np.ones((4, 4, 3))
        cls = np.zeros((4, 4), dtype=np.uint8)
        cls[:2] = 2                      # the upper half is leaf
        native[:2] *= 0.5                # and half as bright natively
        rows = ft.trace_arrays(game, native, cls, {"all": (0, 0, 4, 4)})["all"]
        self.assertAlmostEqual(rows["region"]["ratio"], 0.75)
        self.assertAlmostEqual(rows[2]["ratio"], 0.5)
        self.assertAlmostEqual(rows[0]["ratio"], 1.0)
        self.assertAlmostEqual(rows[2]["delta"], -0.25)
        self.assertAlmostEqual(sum(r["delta"] for k, r in rows.items() if k != "region"), 0.75 - 1.0)
        self.assertEqual(ft.verdict(rows), "assigned")
        cls[:2] = 5                      # the same pixels unclassified: the shortfall is in no named class
        rows = ft.trace_arrays(game, native, cls, {"all": (0, 0, 4, 4)})["all"]
        self.assertEqual(ft.verdict(rows), "not assigned")

    def test_a_class_without_pixels_and_values_that_are_no_numbers(self):
        game = np.ones((2, 2, 3))
        native = np.ones((2, 2, 3))
        native[0, 0] = np.nan
        rows = ft.trace_arrays(game, native, np.zeros((2, 2), dtype=np.uint8), {"all": (0, 0, 2, 2)})["all"]
        self.assertEqual(rows[0]["pixels"], 3)            # the NaN pixel is counted out, and said
        self.assertEqual(rows["region"]["bad"], 1)
        self.assertNotIn(2, rows)

    def test_holes(self):
        scene = np.full((2, 2, 3), 0.3)
        depth = np.full((2, 2), 16777215, dtype=np.uint32)
        clear = (0.02, 0.02, 0.03)
        scene[0, 0] = clear                               # clear colour at the clear depth: sky, not a hole
        scene[1, 1] = clear
        depth[1, 1] = 1000                                # clear colour under a nearer depth: a hole
        self.assertEqual(ft.holes_arrays(scene, depth, clear), 1)

    def test_the_parts_sum_exactly_with_unequal_classes_and_a_region_inside_the_picture(self):
        rng = np.random.default_rng(7)
        game = rng.uniform(0.1, 2.0, (6, 8, 3))
        native = game * rng.uniform(0.3, 1.5, (6, 8, 1))
        cls = rng.integers(0, 8, (6, 8)).astype(np.uint8)
        out = ft.trace_arrays(game, native, cls, {"box": (2, 1, 7, 5), "all": (0, 0, 8, 6)})
        for name, (x0, y0, x1, y1) in (("box", (2, 1, 7, 5)), ("all", (0, 0, 8, 6))):
            rows = out[name]
            g = (game[y0:y1, x0:x1] * ft.LUMA).sum(-1)
            n = (native[y0:y1, x0:x1] * ft.LUMA).sum(-1)
            self.assertAlmostEqual(rows["region"]["ratio"], n.sum() / g.sum(), places=12)
            self.assertEqual(rows["region"]["pixels"], g.size)
            self.assertAlmostEqual(sum(r["delta"] for k, r in rows.items() if k != "region"), rows["region"]["ratio"] - 1.0, places=12)
            self.assertEqual(sum(r["pixels"] for k, r in rows.items() if k != "region"), g.size)
            for k, r in rows.items():
                if k != "region":
                    at = cls[y0:y1, x0:x1] == k
                    self.assertAlmostEqual(r["ratio"], n[at].sum() / g[at].sum(), places=12)
                    self.assertAlmostEqual(r["share"], g[at].sum() / g.sum(), places=12)

    def test_luminance_is_the_plans(self):
        self.assertEqual(tuple(ft.LUMA), (0.2125, 0.7154, 0.0721))
        rows = ft.trace_arrays(np.array([[[1.0, 0.0, 0.0]]]), np.array([[[0.0, 1.0, 0.0]]]), np.zeros((1, 1), np.uint8), {"p": (0, 0, 1, 1)})["p"]
        self.assertAlmostEqual(rows["region"]["ratio"], 0.7154 / 0.2125)

    def test_the_verdicts_bound(self):
        """Assigned iff what class 5 holds moves the region's ratio by at most 0.05 in logarithm and fewer than 1%
        of the pixels are counted out."""
        def rows(ratio, delta5, pixels=1000, bad=0):
            return {"region": {"ratio": ratio, "pixels": pixels, "bad": bad}, 5: {"delta": delta5, "pixels": 10, "ratio": 1.0, "share": 0.0},
                    2: {"delta": ratio - 1.0 - delta5, "pixels": pixels - 10, "ratio": 1.0, "share": 1.0}}
        self.assertEqual(ft.verdict(rows(0.5, -0.02)), "assigned")          # ln 0.5 - ln 0.52 = -0.039
        self.assertEqual(ft.verdict(rows(0.5, -0.03)), "not assigned")      # ln 0.5 - ln 0.53 = -0.058
        self.assertEqual(ft.verdict(rows(0.5, 0.02)), "assigned")
        self.assertEqual(ft.verdict(rows(0.5, 0.03)), "not assigned")
        self.assertEqual(ft.verdict(rows(0.5, 0.0, pixels=1000, bad=9)), "assigned")
        self.assertEqual(ft.verdict(rows(0.5, 0.0, pixels=1000, bad=11)), "not assigned")     # 1% of the region's pixels
        self.assertEqual(ft.verdict(rows(0.5, 0.6)), "not assigned")        # without class 5 nothing is left: no logarithm
        self.assertEqual(ft.verdict({"region": {"ratio": float("nan"), "pixels": 0, "bad": 0}}), "not assigned")

    def test_classes_outside_the_band_are_named(self):
        rows = {"region": {"ratio": 0.8, "pixels": 100, "bad": 0},
                0: {"ratio": 1.0, "delta": 0.0, "pixels": 10, "share": 0.1}, 2: {"ratio": 0.5, "delta": -0.2, "pixels": 50, "share": 0.4},
                3: {"ratio": 1.19, "delta": 0.0, "pixels": 20, "share": 0.2}, 4: {"ratio": 0.85, "delta": 0.0, "pixels": 20, "share": 0.3}}
        self.assertEqual(ft.outside_band(rows), [2, 3])
        self.assertEqual(ft.BAND, (0.85, 1.18))


class Dump(unittest.TestCase):
    def file(self, w, h, flags, scene, depth, layers=()):
        head = struct.pack("<4sIIIQQIfII", b"F2NS", 1, w, h, 77, 76, flags, 7.5, 2, 0)
        return head + scene.astype("<f4").tobytes() + depth.astype("<u4").tobytes() + b"".join(p.astype("<u4").tobytes() for p in layers)

    def test_a_file_with_and_without_the_layers_planes(self):
        scene = np.arange(4 * 2 * 3, dtype=np.float32).reshape(2, 4, 3)
        depth = np.arange(8, dtype=np.uint32).reshape(2, 4)
        before, after = np.array([[5, 6]], dtype=np.uint32), np.array([[5, 3]], dtype=np.uint32)
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "a.f2ns"
            p.write_bytes(self.file(4, 2, 7, scene, depth, (before, after)))
            head, s, z, layers = ft.read_dump(p)
            self.assertEqual((head["width"], head["height"], head["scene_frame"], head["asked_frame"], head["flags"], head["bad_values"]), (4, 2, 77, 76, 7, 2))
            self.assertAlmostEqual(head["exposure"], 7.5)
            self.assertTrue(np.array_equal(s, scene) and np.array_equal(z, depth))
            self.assertTrue(np.array_equal(layers[0], before) and np.array_equal(layers[1], after))
            p.write_bytes(self.file(4, 2, 5, scene, depth))
            self.assertIsNone(ft.read_dump(p)[3])
            # A file that is not whole or holds more than its head asks for, another magic, a flag the format does
            # not know: refused.
            for bad in (self.file(4, 2, 7, scene, depth, (before,)), self.file(4, 2, 5, scene, depth) + b"\0\0\0\0",
                        self.file(4, 2, 7, scene, depth, (before, after, after)),
                        b"F2NX" + self.file(4, 2, 5, scene, depth)[4:], self.file(4, 2, 8, scene, depth)):

                p.write_bytes(bad)
                with self.assertRaises(ValueError):
                    ft.read_dump(p)


class Classes(unittest.TestCase):
    def test_the_classes_of_a_made_depth_copy(self):
        """stored 0: nothing drawn before the copy (open sky, or a late tree over it); a pixel whose stored depth a
        replayed fragment of a kind lies at takes that kind's class (leaf before plant before wood); one that no
        replayed fragment explains is ground (terrain is not replayed), unless a fragment lies close by: other."""
        stored = np.array([[0.0, 0.0, 0.5, 0.5], [0.5, 0.5, 0.5, 0.5]])
        f = np.zeros((2, 4), dtype=bool)
        leaf, plant, wood, near, late = f.copy(), f.copy(), f.copy(), f.copy(), f.copy()
        late[0, 1] = late[0, 2] = True
        leaf[0, 3] = plant[0, 3] = True          # both match: the leaf
        plant[1, 0] = wood[1, 0] = True
        wood[1, 1] = True
        near[1, 2] = near[1, 1] = True           # (a match is no near miss)
        cls = ft.classify(stored, {ft.LEAF: leaf, ft.PLANT: plant, ft.WOOD: wood}, late, near)
        self.assertEqual(cls.tolist(), [[ft.SKY, ft.LATE_SKY, ft.LATE_OTHER, ft.LEAF], [ft.PLANT, ft.WOOD, ft.OTHER, ft.GROUND]])
        self.assertEqual((ft.SKY, ft.LATE_SKY, ft.LEAF, ft.WOOD, ft.GROUND, ft.OTHER), (0, 1, 2, 3, 4, 5))
        self.assertEqual(len(set(ft.CLASS_NAMES)), len(ft.CLASS_NAMES))

    def test_fragments_at_the_stored_depth(self):
        """depth_matches: the pixels where a triangle's replayed depth is the stored one (within the tolerance),
        and those where it only comes near."""
        clip = np.array([[-1.0, 1.0, 0.25, 1.0], [1.0, 1.0, 0.25, 1.0], [-1.0, -1.0, 0.25, 1.0], [1.0, -1.0, 0.25, 1.0]])
        tris = np.array([[0, 1, 2], [1, 3, 2]])
        stored = np.full((4, 4), 0.75)             # 1 - z / w
        stored[0] = 0.75 + 5e-6                     # a little off: near, no match
        stored[1] = 0.9                            # something else in front
        match, near = ft.depth_matches(stored, [(clip, tris)], (4, 4), 0.0, tolerance=1e-6, near=1e-5)
        self.assertTrue(match[2:].all() and not match[:2].any())
        self.assertTrue(near[0].all() and not near[1:].any())


class KeptSet(unittest.TestCase):
    def test_criterion_15s_shares(self):
        before = np.full((2, 5), 100, dtype=np.uint32)
        after = before.copy()
        after[0, :4] = 50                          # the native redraw closes four texels
        replay = np.zeros((2, 5), dtype=bool)
        replay[0, 1:5] = True                      # the replay closes four, three of them the same
        k = ft.kept_set(before, after, replay)
        self.assertEqual((k["replay"], k["native"], k["both"], k["native_alone"], k["replay_alone"]), (4, 4, 3, 1, 1))
        self.assertAlmostEqual(k["closed_share"], 0.75)
        self.assertAlmostEqual(k["alone_share"], 0.25)
        self.assertEqual(ft.kept_verdict(k), "not met")
        after[0, 4] = 50
        k = ft.kept_set(before, after, replay)
        self.assertEqual(ft.kept_verdict(k), "not met")                 # 100% closed, but 25% alone: above 15%
        replay[0, 0] = True
        self.assertEqual(ft.kept_verdict(ft.kept_set(before, after, replay)), "met")
        self.assertEqual(ft.kept_verdict(ft.kept_set(before, before, np.zeros((2, 5), dtype=bool))), "not decidable")
        # A texel the redraw moved AWAY from the eye would be a fault of the depth test: counted, never "closed".
        after[1, 0] = 200
        self.assertEqual(ft.kept_set(before, after, replay)["farther"], 1)
        self.assertEqual(ft.kept_set(before, after, replay)["native"], 5)

    def test_the_two_bounds_each_alone(self):
        before = np.full((1, 100), 100, dtype=np.uint32)
        replay = np.ones((1, 100), dtype=bool)
        after = before.copy()
        after[0, :89] = 1
        self.assertEqual(ft.kept_verdict(ft.kept_set(before, after, replay)), "not met")      # 89% closed, none alone
        after[0, :90] = 1
        self.assertEqual(ft.kept_verdict(ft.kept_set(before, after, replay)), "met")          # 90%
        replay[0, :13] = False                                                              # 87 in the replay, 77 closed
        after[0, :] = 100
        after[0, :90] = 1                                                                   # native 90: 13 alone = 14.9%
        k = ft.kept_set(before, after, replay)
        self.assertEqual((k["replay"], k["both"], k["native_alone"]), (87, 77, 13))
        self.assertEqual(ft.kept_verdict(k), "not met")                                     # 77 of 87 = 88.5% closed
        after[0, 90:92] = 1                                                                 # 79 of 87 = 90.8%; 13 alone = 14.9%
        self.assertEqual(ft.kept_verdict(ft.kept_set(before, after, replay)), "met")
        replay[0, 13] = False                                                               # 86 in the replay, 14 alone = 16.3%
        self.assertEqual(ft.kept_verdict(ft.kept_set(before, after, replay)), "not met")


class HalfPixel(unittest.TestCase):
    def test_a_made_shift_is_found(self):
        y, x = np.mgrid[0:40, 0:60].astype(np.float64)

        def picture(dx, dy):
            return 1.0 + np.sin((x + dx) * 0.37) * np.cos((y + dy) * 0.23) * 0.5

        game = picture(0.0, 0.0)
        for dx, dy in ((0.5, 0.0), (-0.25, 0.5), (0.0, 0.0)):
            # native(x) = game(x + d): the native picture is the game's sampled d pixels to the right and below
            shift = ft.halfpixel_arrays(game, picture(dx, dy), np.ones_like(game, dtype=bool))
            self.assertAlmostEqual(shift[0], dx, places=6)
            self.assertAlmostEqual(shift[1], dy, places=6)
        self.assertIsNone(ft.halfpixel_arrays(game, game, np.zeros_like(game, dtype=bool)))


class Predict(unittest.TestCase):
    def test_the_screen_model(self):
        """A region's display value is its scene's screened with the layer's factor B: d = d0 + B (1 - d0). The
        scene's linear factor k and the layer's factor m change d0 and B; the figure is the measured ratio times
        the change of the linear value."""
        self.assertAlmostEqual(ft.predict_region(0.7, d0=0.4, layer=0.2, scene_factor=1.0, layer_factor=1.0), 0.7)
        # no layer: the scene's factor alone
        self.assertAlmostEqual(ft.predict_region(0.5, d0=0.4, layer=0.0, scene_factor=1.3, layer_factor=1.0), 0.65)
        # no scene change, the layer halved
        d, d2 = 0.4 + 0.2 * 0.6, 0.4 + 0.1 * 0.6
        self.assertAlmostEqual(ft.predict_region(1.2, d0=0.4, layer=0.2, scene_factor=1.0, layer_factor=0.5), 1.2 * (d2 / d) ** ft.DISPLAY_POWER)
        # a brighter scene takes less of the same layer
        up = ft.predict_region(1.0, d0=0.4, layer=0.2, scene_factor=1.5, layer_factor=1.0)
        self.assertTrue(1.0 < up < 1.5)
        for bad in ({"d0": 1.2}, {"layer": -0.1}, {"scene_factor": float("nan")}, {"layer_factor": -1.0}):
            args = {"d0": 0.4, "layer": 0.2, "scene_factor": 1.0, "layer_factor": 1.0}
            args.update(bad)
            with self.assertRaises(ValueError):
                ft.predict_region(1.0, **args)


if __name__ == "__main__":
    unittest.main()
