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
        self.assertIn("FABLE2_VS_SKIN(0xA1F7E9885EC466DFull, 2, -1, 6, 7, 8, 0x0, 0x0, 0x0, 1, 2, 0, 0, 0, 0, 0, 0, 0)", text)
        self.assertIn("FABLE2_VS_TERRAIN(0xC30A97D946FA2BE4ull, 44, 184, 186, 452, 190, 188, -1, 16)", text)
        self.assertIn("FABLE2_VS_TERRAIN(0x5003700B7C9B1C16ull, 44, 32, 34, 188, 186, 184, 452, 16)", text)
        self.assertNotIn("0x1ull", text)
        # Extras only for shaders that have a transform.
        bad = {"0x2": {"rejected": "x", "pos_swizzle": "yxw1"}}
        self.assertNotIn("FABLE2_VS_POS_SWIZZLE", gtt.generate(bad))

    def test_skin_rigid_form(self):
        data = {"0xA1F7E9885EC466DF": {"base": 0, "layout": "dot", "pos_fetch": -1,
                                       "skin": {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}}}
        out = gtt.generate(data).splitlines()
        self.assertIn("FABLE2_VS_SKIN(0xA1F7E9885EC466DFull, 2, -1, 6, 7, 8, 0x0, 0x0, 0x0, 1, 2, 0, 0, 0, 0, 0, 0, 0)", out)

    def test_skin_weighted_form(self):
        data = {"0xD4D558DA6A82BDC8": {"base": 0, "layout": "dot", "pos_fetch": -1,
                                       "skin": {"index_fetch": 1, "weight_fetch": 2, "row_fetches": [3, 4, 5],
                                                "row_swizzles": ["xyzw", "xzyw", "yxzw"],
                                                "pairs": [["x", "z"], ["y", "y"], ["z", "x"], ["w", "w"]]}}}
        out = gtt.generate(data).splitlines()
        self.assertIn("FABLE2_VS_SKIN(0xD4D558DA6A82BDC8ull, 1, 2, 3, 4, 5, 0x688, 0x650, 0x681, 4, "
                      "0, 2, 1, 1, 2, 0, 3, 3)", out)

    def test_skin_rejects_bad_pairs(self):
        for pairs in ([], [["x", "x"]] * 5, [["q", "x"]]):
            with self.assertRaises(ValueError):
                gtt.generate({"0x1": {"base": 0, "layout": "dot",
                                      "skin": {"index_fetch": 1, "weight_fetch": 2,
                                               "row_fetches": [3, 4, 5], "pairs": pairs}}})

    def test_instance(self):
        data = {"0x8123C16DBF583F92": {"base": 0, "layout": "dot",
                                       "instance": {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "inv_count": "c12.x",
                                                    "count": "c12.y", "first": "c12.z", "bias": 0.5,
                                                    "offset": "c7.xyz"}}}
        out = gtt.generate(data).splitlines()
        self.assertIn("FABLE2_VS_TRANSFORM(0x8123C16DBF583F92ull, 0, 0, 4, 0)", out)
        self.assertIn("FABLE2_VS_INSTANCE(0x8123C16DBF583F92ull, 0, 1, 2, 0x0, 0x0, 0x0, 48, 49, 50, 0.5f, 28, -1, -1)",
                      out)

    def test_instance_cut(self):
        inst = {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "inv_count": "c12.x", "count": "c12.y", "first": "c12.z",
                "bias": 0.5, "offset": "c7.xyz"}

        def lines(cut):
            return gtt.generate({"0x1": {"base": 0, "layout": "dot", "instance": dict(inst, cut=cut)}}).splitlines()

        # eye c9.xyz -> 9 * 4, squared distance c13.z -> 13 * 4 + 2
        self.assertIn("FABLE2_VS_INSTANCE(0x1ull, 0, 1, 2, 0x0, 0x0, 0x0, 48, 49, 50, 0.5f, 28, 36, 54)",
                      lines({"eye": "c9.xyz", "dist2": "c13.z"}))
        for bad in ({"eye": "c9.xyz"}, {"dist2": "c13.z"}, {"eye": "c9.xy", "dist2": "c13.z"},
                    {"eye": "c9.yzw", "dist2": "c13.z"}, {"eye": "c9.xyz", "dist2": "c13.zw"},
                    {"eye": "c9.xyz", "dist2": "r13.z"}, {"eye": "c9.xyz", "dist2": 1062.4},
                    {"eye": "c256.xyz", "dist2": "c13.z"}, {"eye": "c9.xyz", "dist2": "c13.z", "near": "c13.w"},
                    "c9.xyz", []):
            with self.assertRaises(ValueError, msg=repr(bad)):
                lines(bad)

    def test_instance_rejects_conflicts(self):
        base = {"base": 0, "layout": "dot",
                "instance": {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "inv_count": "c12.x", "count": "c12.y",
                             "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}}
        with self.assertRaises(ValueError):
            gtt.generate({"0x1": dict(base, pos_fetch=2)})       # disagrees with mesh_fetch
        bad = dict(base); bad["instance"] = dict(base["instance"], offset="c7.yzw")
        with self.assertRaises(ValueError):
            gtt.generate({"0x1": bad})                           # offset must be .xyz
        bad = dict(base); bad["instance"] = dict(base["instance"], row_fetches=[0, 1])
        with self.assertRaises(ValueError):
            gtt.generate({"0x1": bad})

    # --- Contradictory entries raise, naming the shader ---

    INSTANCE = {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "inv_count": "c12.x", "count": "c12.y",
                "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}

    def test_entry_with_both_skin_and_instance_is_rejected(self):
        entry = {"base": 0, "layout": "dot", "instance": dict(self.INSTANCE),
                 "skin": {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}}
        with self.assertRaisesRegex(ValueError, "0xABC1.*skin.*instance"):
            gtt.generate({"0xABC1": entry})

    def test_skin_with_pairs_but_no_weight_fetch_is_rejected(self):
        # Without the check the entry silently became a rigid skin (or a bare KeyError without index_component).
        skin = {"index_fetch": 1, "row_fetches": [3, 4, 5], "pairs": [["x", "z"], ["y", "y"]]}
        for extra in ({}, {"index_component": "x"}):
            with self.assertRaisesRegex(ValueError, "0xABC2.*pairs.*weight_fetch"):
                gtt.generate({"0xABC2": {"base": 0, "layout": "dot", "skin": dict(skin, **extra)}})

    def test_instance_entry_without_base_is_rejected(self):
        # Without the check the entry was dropped from the table without a word.
        with self.assertRaisesRegex(ValueError, "0xABC3.*instance.*base"):
            gtt.generate({"0xABC3": {"layout": "dot", "instance": dict(self.INSTANCE)}})

    def test_skin_entry_without_base_is_rejected(self):
        # Without the check the entry was dropped from the table without a word, like the instance one.
        skin = {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}
        with self.assertRaisesRegex(ValueError, "0xABC4.*skin.*base"):
            gtt.generate({"0xABC4": {"layout": "dot", "skin": skin}})

    def test_two_window_product_entries_are_left_out(self):
        # "base2" is what matrix_finder.py --products writes for a shader whose transform is the
        # product of two constant windows. The table has one base per shader, so such an entry is a
        # finder result to read, not a table line; it is left out on purpose and does not raise.
        text = gtt.generate({"0xABC5": {"base": 8, "base2": 20, "layout": "dot", "score": 1.0, "samples": 4},
                             "0xABC6": {"base": 0, "layout": "dot"}})
        self.assertNotIn("0xABC5", text)
        self.assertIn("FABLE2_VS_TRANSFORM(0xABC6ull, 0, 0, -1, 0)", text)

    def test_plain_entries_unchanged(self):
        text = gtt.generate({"0x2": {"base": 4, "layout": "combine", "pos_fetch": 1, "deformed": True}})
        self.assertEqual(text.splitlines()[1:], ["FABLE2_VS_TRANSFORM(0x2ull, 4, 1, 1, 1)"])

    def test_uv_entries(self):
        data = {"0xECD66A10092E6562": {"base": 0, "layout": "dot", "pos_fetch": -1,
                                       "uv": {"o0.x": {"fetch": 2, "src": "y", "format": 31, "offset": 3, "stages": []},
                                              "o0.y": {"fetch": 2, "src": "x", "format": 31, "offset": 3,
                                                       "stages": [{"scale": "c10.y", "offset": "-c10.w"}]}}}}
        out = gtt.generate(data).splitlines()
        self.assertIn("FABLE2_VS_UV(0xECD66A10092E6562ull, 0, 0, 2, 1, 31, 3, -1, -1, -1, -1)", out)
        self.assertIn(f"FABLE2_VS_UV(0xECD66A10092E6562ull, 0, 1, 2, 0, 31, 3, {41}, {(1 << 11) | 43}, -1, -1)", out)


if __name__ == "__main__":
    unittest.main()
