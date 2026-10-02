import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import gen_albedo_table  # noqa: E402
import uv_refs  # noqa: E402


class UvRefsTest(unittest.TestCase):
    def test_encode(self):
        self.assertEqual(uv_refs.encode_ref("c8.y", 1), (1 << 10) | (8 * 4 + 1))
        self.assertEqual(uv_refs.encode_ref("-c10.w", 0), (1 << 11) | (10 * 4 + 3))
        self.assertEqual(uv_refs.encode_ref(None, 0), -1)
        for bad in ("c256.x", "r1.x", "c1.xy", "c1"):
            with self.assertRaises(ValueError):
                uv_refs.encode_ref(bad, 0)

    def test_stages(self):
        self.assertEqual(uv_refs.stages([], 1), [-1, -1, -1, -1])
        self.assertEqual(uv_refs.stages([{"scale": "c8.y", "offset": None}], 1), [(1 << 10) | 33, -1, -1, -1])
        with self.assertRaises(ValueError):
            uv_refs.stages([{}, {}, {}], 0)

    def test_component(self):
        self.assertEqual(uv_refs.input_comp("r0.y"), (0, 1))
        self.assertEqual(uv_refs.input_comp("o3.w"), (3, 3))
        with self.assertRaises(ValueError):
            uv_refs.input_comp("r0.xy")


class GenAlbedoTest(unittest.TestCase):
    def test_generate(self):
        data = {
            "0x00E09D1BC5295D52": {"slot": 0, "manual": True, "evidence": "e",
                                   "u": {"input": "r0.y", "stages": [{"scale": "c8.y", "offset": "c8.w"}]},
                                   "v": {"input": "r0.x", "stages": [{"scale": "c8.x", "offset": "c8.z"}]}},
            "0x0000000000000002": {"no_albedo": "fog only", "manual": True, "evidence": "e"},
        }
        out = gen_albedo_table.generate(data).splitlines()
        self.assertTrue(out[0].startswith("// Generated"))
        self.assertEqual(out[1], "FABLE2_PS_NO_ALBEDO(0x0000000000000002ull)")
        s = (1 << 10)
        self.assertEqual(out[2], f"FABLE2_PS_ALBEDO(0x00E09D1BC5295D52ull, 0, 0, 1, {s | 33}, {s | 35}, -1, -1, "
                                 f"0, 0, {s | 32}, {s | 34}, -1, -1)")

    def test_rejects_bad_slot(self):
        with self.assertRaises(ValueError):
            gen_albedo_table.generate({"0x1": {"slot": 32, "u": {"input": "r0.x", "stages": []},
                                                "v": {"input": "r0.y", "stages": []}}})


if __name__ == "__main__":
    unittest.main()
