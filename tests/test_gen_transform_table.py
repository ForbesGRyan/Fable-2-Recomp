"""Tests for tools/xdk_sigmatch/gen_transform_table.py (vs-transforms.json -> .inc)."""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gtt", ROOT / "tools" / "xdk_sigmatch" / "gen_transform_table.py")
gtt = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gtt)


class GenTransformTableTest(unittest.TestCase):
    def test_swizzle_text(self):
        self.assertEqual(gtt.swizzle_bits("yxw1"), 0xAC1)
        self.assertEqual(gtt.swizzle_bits("xyzw"), 0x688)
        self.assertEqual(gtt.swizzle_bits("zxwy"), 0x2C2)
        self.assertEqual(gtt.swizzle_bits("xy0_"), 0xF08)
        with self.assertRaises(ValueError):
            gtt.swizzle_bits("xyz")
        with self.assertRaises(ValueError):
            gtt.swizzle_bits("xyzq")

    def test_register_component(self):
        self.assertEqual(gtt.reg_comp("c11.x"), 44)
        self.assertEqual(gtt.reg_comp("c47.zw", pair=True), 190)
        self.assertEqual(gtt.reg_comp("c46.z"), 186)
        with self.assertRaises(ValueError):
            gtt.reg_comp("c47.zx", pair=True)  # not consecutive
        with self.assertRaises(ValueError):
            gtt.reg_comp("c47.w", pair=True)  # a pair needs two components
        with self.assertRaises(ValueError):
            gtt.reg_comp("c256.x")

    def test_extras(self):
        data = {
            "0x79EAC49585797037": {"base": 0, "layout": "dot", "pos_swizzle": "yxw1"},
            "0xA1F7E9885EC466DF": {"base": 0, "layout": "dot", "pos_swizzle": "yxw1",
                                   "skin": {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}},
            "0xC30A97D946FA2BE4": {"base": 0, "layout": "dot",
                                   "terrain": {"grid": "c11.xy", "cell": "c46.xy", "height_scale": "c46.z",
                                               "origin": "c113.xy", "tex_offset": "c47.zw", "tex_scale": "c47.xy",
                                               "patch_offset": None, "height_fetch": 16}},
            "0x5003700B7C9B1C16": {"base": 0, "layout": "dot",
                                   "terrain": {"grid": "c11.xy", "cell": "c8.xy", "height_scale": "c8.z",
                                               "origin": "c47.xy", "tex_offset": "c46.zw", "tex_scale": "c46.xy",
                                               "patch_offset": "c113.x", "height_fetch": 16}},
            "0x1": {"manual": True, "rejected": "no"},
        }
        text = gtt.generate(data)
        self.assertIn("FABLE2_VS_TRANSFORM(0x79EAC49585797037ull, 0, 0, -1, 0)", text)
        self.assertIn("FABLE2_VS_POS_SWIZZLE(0x79EAC49585797037ull, 0xAC1)", text)
        self.assertIn("FABLE2_VS_POS_SWIZZLE(0xA1F7E9885EC466DFull, 0xAC1)", text)
        self.assertIn("FABLE2_VS_SKIN(0xA1F7E9885EC466DFull, 2, 2, 6, 7, 8)", text)
        self.assertIn("FABLE2_VS_TERRAIN(0xC30A97D946FA2BE4ull, 44, 184, 186, 452, 190, 188, -1, 16)", text)
        self.assertIn("FABLE2_VS_TERRAIN(0x5003700B7C9B1C16ull, 44, 32, 34, 188, 186, 184, 452, 16)", text)
        self.assertNotIn("0x1ull", text)
        # Extras only for shaders that have a transform.
        bad = {"0x2": {"rejected": "x", "pos_swizzle": "yxw1"}}
        self.assertNotIn("FABLE2_VS_POS_SWIZZLE", gtt.generate(bad))

    def test_plain_entries_unchanged(self):
        text = gtt.generate({"0x2": {"base": 4, "layout": "combine", "pos_fetch": 1, "deformed": True}})
        self.assertEqual(text.splitlines()[1:], ["FABLE2_VS_TRANSFORM(0x2ull, 4, 1, 1, 1)"])


if __name__ == "__main__":
    unittest.main()
