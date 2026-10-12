"""Captured reflection records and views, bit for bit against independent rows."""
import copy
import sys
import struct
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/xdk_sigmatch"))
import water_check as wc


class ReflectionRecords(unittest.TestCase):
    def independent_row(self):
        constants = {"c%d" % r: [0.0] * 4 for r in (0, 1, 2, 3, 8, 11, 14, 46, 47, 72, 113, 114, 115)}
        constants["c0"] = [1.0, 0, 0, 0]
        constants["c8"] = [8.0, 8.0, 32.0, float("nan")]
        constants["c11"] = [16.0, 0.0625, 0, 0]
        constants["c113"] = [7.0, 0, 0, 0]
        constants["c46"] = [0.125, 0.25, 24.0, 40.0]
        constants["c47"] = [80.0, 96.0, 0, 0]
        constants["c14"] = [0.25, 0.5, float("nan"), float("nan")]
        pixels = [[0.0] * 4 for _ in range(256)]
        pixels[19] = [1, 2, -3, float("nan")]
        pixels[21] = [0.25, 0.5, 0.75, 2.0]
        pixels[31] = [0.125, 0.25, float("nan"), float("nan")]
        row = {"vs": {"hash": "0x5003700B7C9B1C16"}, "ps_hash": "0x" + wc.FAR_TERRAIN_PS,
               "args": ["0x0", "0x0", "0x0", "0x3"], "consts": constants,
               "pfull": b"".join(struct.pack(">4f", *p) for p in pixels).hex(),
               "lin": {"c11x": 0.5}, "rs": {k: "0x1" for k in ("2200", "2201", "2202", "2205", "2104", "2113", "2206")},
               "tf": {str(s): ["0x00800002", "0x10000018", "0x0003E01F", "0x0", "0x0", "0x200"] for s in (16, 17, 18, 19, 3, 13)}}
        return row

    def test_independent_row_inputs(self):
        got = wc.reflection_of_row(self.independent_row(), 9)
        self.assertEqual((got["seq"], got["start"], got["count"]), (9, 0, 3))
        self.assertEqual(got["patch"][:12], [wc._float_bits(x) for x in (7, 16, 0.0625, 8, 8, 32, 80, 96, 24, 40, 0.125, 0.25)])
        self.assertEqual(got["patch"][12:16], [wc._float_bits(x) for x in (0.25, 0.5, -6, -20)])
        self.assertEqual(got["patch"][-3:], [wc._float_bits(x) for x in (1, 2, -3)])
        self.assertEqual(got["map"], [0x10000000, 4032, 32, 32, 64, 0, 0, 0, 0])
        self.assertEqual(got["c11x"], [0x3F000000])
        wrong = copy.deepcopy(got); wrong["patch"][-1] ^= 1
        self.assertEqual(wc.reflection_differences(wrong, got), ["patch"])

    def test_integer_json_zero_keeps_float_uv_sign(self):
        row = self.independent_row()
        # Discovery emits exact zero floats as JSON integers. C++ still negates
        # float operands, so the UV offset retains -0 for each positive scale.
        for zero in (0, 0.0, -0.0):
            with self.subTest(zero=repr(zero)):
                row["consts"]["c46"][2:4] = [zero, zero]
                got = wc.reflection_of_row(row, 0)
                expected_sign = 0 if struct.pack(">f", float(zero))[0] & 0x80 else 0x80000000
                self.assertEqual(got["patch"][14:16], [expected_sign] * 2)
                wrong = copy.deepcopy(got)
                wrong["patch"][15] ^= 0x80000000
                self.assertEqual(wc.reflection_differences(wrong, got), ["patch"])

    def test_equal_and_each_member_mutation(self):
        want = {"rows": [0x3F800000] * 16, "vs_hash": "5003700B7C9B1C16", "ps_hash": wc.FAR_TERRAIN_PS,
                "state": [1, 2, 3], "patch": [4, 5], "fetch": [[6]], "c11x": 0x3EF0D845}
        self.assertEqual(wc.reflection_differences(want, want), [])
        for key in want:
            wrong = copy.deepcopy(want)
            wrong[key] = None
            self.assertEqual(wc.reflection_differences(wrong, want), [key])

    def test_views_count_order_blur_whole(self):
        expected = [{"first": 0, "count": 36, "blurred": 1}, {"first": 36, "count": 4, "blurred": 1},
                    {"first": 40, "count": 12, "blurred": 0}, {"first": 52, "count": 3, "blurred": 0}]
        self.assertEqual(wc.reflection_view_differences(expected, expected, 55), [])
        wrong = copy.deepcopy(expected); wrong[1]["count"] = 3
        self.assertTrue(wc.reflection_view_differences(wrong, expected, 55))
        wrong = copy.deepcopy(expected); wrong[0]["blurred"] = 0
        self.assertTrue(wc.reflection_view_differences(wrong, expected, 55))
        self.assertTrue(wc.reflection_view_differences(expected[::-1], expected, 55))
        self.assertTrue(wc.reflection_view_differences(expected[:-1], expected, 55))

    def test_command_rejects_missing_records(self):
        import water_replay as replay
        row = {"ps_hash": wc.FAR_TERRAIN_PS}
        view = SimpleNamespace(rows=[row], blur=None)
        block = {"views": [{"first": 0, "count": 1, "blurred": 0}], "reflection": 0, "reflection_records": []}
        frames = [{"frame": 1, "water": block}]
        with patch.object(wc, "_captures_of", return_value=[Path("synthetic.jsonl")]), \
             patch.object(wc.cr, "rows", return_value=frames), \
             patch.object(replay, "read_views", return_value=[view]), \
             patch.object(wc, "reflection_of_row", return_value={"seq": 0}):
            lines = []
            self.assertEqual(wc.cmd_reflection(None, lines.append), 1)
            self.assertTrue(any("reflection count" in line for line in lines))

    def test_command_bit_equality_and_state_mutation(self):
        import water_replay as replay
        view = SimpleNamespace(rows=[{"ps_hash": wc.FAR_TERRAIN_PS}], blur=None)
        want = {"seq": 0, "vs_hash": "5003700B7C9B1C16", "ps_hash": wc.FAR_TERRAIN_PS,
                "rows": [1], "state": [2], "patch": [3], "map": [4], "c11x": [5], "fetch": [[6]]}
        got = {k: (["0x%08X" % x for x in v] if isinstance(v, list) and k != "fetch" else v) for k, v in want.items()}
        got["fetch"] = [["0x00000006"]]
        block = {"views": [{"first": 0, "count": 1, "blurred": 0}], "reflection": 1, "reflection_records": [got]}
        with patch.object(wc, "_captures_of", return_value=[Path("synthetic.jsonl")]), \
             patch.object(wc.cr, "rows", return_value=[{"frame": 1, "water": block}]), \
             patch.object(replay, "read_views", return_value=[view]), \
             patch.object(wc, "reflection_of_row", return_value=want):
            self.assertEqual(wc.cmd_reflection(None, lambda line: None), 0)
            got["state"] = ["0x00000003"]
            lines = []
            self.assertEqual(wc.cmd_reflection(None, lines.append), 1)
            self.assertTrue(any("record 0 state" in line for line in lines))


if __name__ == "__main__":
    unittest.main()
