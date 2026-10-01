import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("summ", ROOT / "tools" / "xdk_sigmatch" / "summarize_census.py")
summ = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(summ)


def row(frame, guest, draws, cpu, pitches, dip_calls, callers, copies=0):
    return {"frame": frame, "guest_ms": guest,
            "gpu": {"draws": draws, "draw_cpu_ms": cpu, "swap_interval_ms": guest,
                    "pitches": pitches, "other": 0, "copies": copies},
            "funcs": {"D3DDevice_DrawIndexedVertices": {"calls": dip_calls, "callers": callers, "args": []}}}


class SummarizeTests(unittest.TestCase):
    def test_medians_and_ratio(self):
        rows = [row(1, 16.0, 100, 4.0, {"1280": 80, "640": 20}, 95, {"0x82000010": 60}, copies=2),
                row(2, 17.0, 110, 5.0, {"1280": 90, "640": 20}, 105, {"0x82000010": 70}, copies=4),
                row(3, 40.0, 120, 6.0, {"1280": 100}, 115, {"0x82000020": 5}, copies=6)]
        s = summ.summarize(rows)
        self.assertEqual(s["frames"], 3)
        self.assertEqual(s["guest_ms_median"], 17.0)
        self.assertEqual(s["draws_median"], 110)
        self.assertEqual(s["copies_median"], 4)
        self.assertEqual(s["pitches"]["1280"], 90)
        self.assertEqual(s["pitches"]["640"], 20)
        self.assertEqual(s["top_callers"]["D3DDevice_DrawIndexedVertices"][0], ("0x82000010", 130))
        self.assertAlmostEqual(s["draw_call_ratio"], 105 / 110, places=3)
        self.assertAlmostEqual(s["draws_mean"], 110.0)
        self.assertEqual(s["draws_p90"], 120)
        self.assertAlmostEqual(s["draw_call_ratio_total"], (95 + 105 + 115) / (100 + 110 + 120), places=6)

    def test_repeated_gpu_frame_counted_once(self):
        a = row(2, 16.0, 100, 4.0, {"1280": 100}, 95, {"0x1": 1})
        b = row(3, 16.0, 999, 4.0, {"1280": 999}, 95, {"0x1": 1})
        c = row(4, 16.0, 200, 4.0, {"1280": 200}, 95, {"0x1": 1})
        a["gpu"]["gpu_frame"] = 7
        b["gpu"]["gpu_frame"] = 7
        c["gpu"]["gpu_frame"] = 8
        s = summ.summarize([a, b, c])
        self.assertEqual(s["frames"], 2)
        self.assertAlmostEqual(s["draws_mean"], 150.0)

    def test_filter_rows(self):
        rows = [{"frame": f} for f in range(1, 11)]
        self.assertEqual([r["frame"] for r in summ.filter_rows(rows, 3, 5)], [3, 4, 5])
        self.assertEqual(len(summ.filter_rows(rows, None, None)), 10)
        self.assertEqual([r["frame"] for r in summ.filter_rows(rows, 9, None)], [9, 10])
        self.assertEqual([r["frame"] for r in summ.filter_rows(rows, None, 2)], [1, 2])

    def test_draws_p90_nearest_rank(self):
        rows = [row(i, 16.0, d, 1.0, {"1280": d}, d, {}) for i, d in enumerate(range(10, 110, 10), 2)]
        self.assertEqual(summ.summarize(rows)["draws_p90"], 90)

    def test_rows_without_gpu_are_tolerated(self):
        s = summ.summarize([{"frame": 1, "guest_ms": 16.0, "funcs": {}}])
        self.assertEqual(s["frames"], 1)
        self.assertIsNone(s["draws_median"])


class TilingAndEmitterTests(unittest.TestCase):
    def test_emitter_hooks_count_as_draws(self):
        r = row(2, 33.0, 100, 1.0, {"1120": 100}, 30, {"0x1": 30})
        r["funcs"]["DrawIndx:8221C9C8"] = {"calls": 10, "callers": {"0x2": 10}, "args": []}
        r["funcs"]["DrawIndx2:821EF988"] = {"calls": 5, "callers": {"0x3": 5}, "args": []}
        r["funcs"]["SetBinSelect:822A6318"] = {"calls": 99, "callers": {"0x4": 99}, "args": []}
        s = summ.summarize([r])
        self.assertAlmostEqual(s["draw_call_ratio_total"], 45 / 100)

    def test_tiling_medians_and_extents(self):
        rows = []
        for i, (tiles, pred) in enumerate([(2, 1500), (2, 1700), (3, 1600)]):
            r = row(2 + i, 33.0, 3000, 6.0, {"1120": 1800}, 0, {})
            r["gpu"].update({"bin_selects": 4, "tiles": tiles, "pred_draws": pred, "pred_skips": 10,
                             "tile_selects": ["0x1", "0x2"] if tiles == 2 else ["0x1", "0x2", "0x4"],
                             "extents": {"1120": [1120, 320 + i, 1120, 630 + i]}})
            rows.append(r)
        s = summ.summarize(rows)
        self.assertEqual(s["tiles_median"], 2)
        self.assertEqual(s["pred_draws_median"], 1600)
        self.assertEqual(s["extents"]["1120"], [1120, 322, 1120, 632])
        self.assertEqual(s["tile_select_sets"][0], (("0x1", "0x2"), 2))
        md = summ.to_markdown(s)
        self.assertIn("| 1120 | 1120 x 322 | 1120 x 632 |", md)

    def test_tiling_absent_in_old_captures(self):
        s = summ.summarize([row(2, 33.0, 10, 1.0, {"1280": 10}, 5, {})])
        self.assertIsNone(s["tiles_median"])
        self.assertEqual(s["extents"], {})
        self.assertNotIn("## Tiling", summ.to_markdown(s))


if __name__ == "__main__":
    unittest.main()
