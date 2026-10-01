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

    def test_rows_without_gpu_are_tolerated(self):
        s = summ.summarize([{"frame": 1, "guest_ms": 16.0, "funcs": {}}])
        self.assertEqual(s["frames"], 1)
        self.assertIsNone(s["draws_median"])


if __name__ == "__main__":
    unittest.main()
