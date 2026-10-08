"""Tests for tools/xdk_sigmatch/gen_transform_table.py (vs-transforms.json -> .inc)."""
import importlib.util
import json
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
            "0x0000000000000001": {"manual": True, "rejected": "no"},
        }
        text = gtt.generate(data)
        self.assertIn("FABLE2_VS_TRANSFORM(0x79EAC49585797037ull, 0, 0, -1, 0)", text)
        self.assertIn("FABLE2_VS_POS_SWIZZLE(0x79EAC49585797037ull, 0xAC1)", text)
        self.assertIn("FABLE2_VS_POS_SWIZZLE(0xA1F7E9885EC466DFull, 0xAC1)", text)
        self.assertIn("FABLE2_VS_SKIN(0xA1F7E9885EC466DFull, 2, -1, 6, 7, 8, 0x0, 0x0, 0x0, 1, 2, 0, 0, 0, 0, 0, 0, 0)", text)
        self.assertIn("FABLE2_VS_TERRAIN(0xC30A97D946FA2BE4ull, 44, 184, 186, 452, 190, 188, -1, 16)", text)
        self.assertIn("FABLE2_VS_TERRAIN(0x5003700B7C9B1C16ull, 44, 32, 34, 188, 186, 184, 452, 16)", text)
        self.assertNotIn("0x0000000000000001ull", text)
        # Extras only for shaders that have a transform.
        bad = {"0x0000000000000002": {"rejected": "x", "pos_swizzle": "yxw1"}}
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
                gtt.generate({"0x0000000000000001": {"base": 0, "layout": "dot",
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
            return gtt.generate({"0x0000000000000001": {"base": 0, "layout": "dot", "instance": dict(inst, cut=cut)}}).splitlines()

        # eye c9.xyz -> 9 * 4, squared distance c13.z -> 13 * 4 + 2
        self.assertIn("FABLE2_VS_INSTANCE(0x0000000000000001ull, 0, 1, 2, 0x0, 0x0, 0x0, 48, 49, 50, 0.5f, 28, 36, 54)",
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
            gtt.generate({"0x0000000000000001": dict(base, pos_fetch=2)})       # disagrees with mesh_fetch
        bad = dict(base); bad["instance"] = dict(base["instance"], offset="c7.yzw")
        with self.assertRaises(ValueError):
            gtt.generate({"0x0000000000000001": bad})                           # offset must be .xyz
        bad = dict(base); bad["instance"] = dict(base["instance"], row_fetches=[0, 1])
        with self.assertRaises(ValueError):
            gtt.generate({"0x0000000000000001": bad})

    # --- Contradictory entries raise, naming the shader ---

    INSTANCE = {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "inv_count": "c12.x", "count": "c12.y",
                "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}

    def test_entry_with_both_skin_and_instance_is_rejected(self):
        entry = {"base": 0, "layout": "dot", "instance": dict(self.INSTANCE),
                 "skin": {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}}
        with self.assertRaisesRegex(ValueError, "0x000000000000ABC1.*skin.*instance"):
            gtt.generate({"0x000000000000ABC1": entry})

    def test_skin_with_pairs_but_no_weight_fetch_is_rejected(self):
        # Without the check the entry silently became a rigid skin (or a bare KeyError without index_component).
        skin = {"index_fetch": 1, "row_fetches": [3, 4, 5], "pairs": [["x", "z"], ["y", "y"]]}
        for extra in ({}, {"index_component": "x"}):
            with self.assertRaisesRegex(ValueError, "0x000000000000ABC2.*pairs.*weight_fetch"):
                gtt.generate({"0x000000000000ABC2": {"base": 0, "layout": "dot", "skin": dict(skin, **extra)}})

    def test_instance_entry_without_base_is_rejected(self):
        # Without the check the entry was dropped from the table without a word.
        with self.assertRaisesRegex(ValueError, "0x000000000000ABC3.*instance.*base"):
            gtt.generate({"0x000000000000ABC3": {"layout": "dot", "instance": dict(self.INSTANCE)}})

    def test_skin_entry_without_base_is_rejected(self):
        # Without the check the entry was dropped from the table without a word, like the instance one.
        skin = {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}
        with self.assertRaisesRegex(ValueError, "0x000000000000ABC4.*skin.*base"):
            gtt.generate({"0x000000000000ABC4": {"layout": "dot", "skin": skin}})

    def test_two_window_product_entries_are_left_out(self):
        # "base2" is what matrix_finder.py --products writes for a shader whose transform is the
        # product of two constant windows. The table has one base per shader, so such an entry is a
        # finder result to read, not a table line; it is left out on purpose and does not raise.
        text = gtt.generate({"0x000000000000ABC5": {"base": 8, "base2": 20, "layout": "dot", "score": 1.0, "samples": 4},
                             "0x000000000000ABC6": {"base": 0, "layout": "dot"}})
        self.assertNotIn("0x000000000000ABC5", text)
        self.assertIn("FABLE2_VS_TRANSFORM(0x000000000000ABC6ull, 0, 0, -1, 0)", text)

    def test_plain_entries_unchanged(self):
        text = gtt.generate({"0x0000000000000002": {"base": 4, "layout": "combine", "pos_fetch": 1, "deformed": True}})
        self.assertEqual(text.splitlines()[1:], ["FABLE2_VS_TRANSFORM(0x0000000000000002ull, 4, 1, 1, 1)"])

    def test_uv_entries(self):
        data = {"0xECD66A10092E6562": {"base": 0, "layout": "dot", "pos_fetch": -1,
                                       "uv": {"o0.x": {"fetch": 2, "src": "y", "format": 31, "offset": 3, "stages": []},
                                              "o0.y": {"fetch": 2, "src": "x", "format": 31, "offset": 3,
                                                       "stages": [{"scale": "c10.y", "offset": "-c10.w"}]}}}}
        out = gtt.generate(data).splitlines()
        self.assertIn("FABLE2_VS_UV(0xECD66A10092E6562ull, 0, 0, 2, 1, 31, 3, -1, -1, -1, -1)", out)
        self.assertIn(f"FABLE2_VS_UV(0xECD66A10092E6562ull, 0, 1, 2, 0, 31, 3, {41}, {(1 << 11) | 43}, -1, -1)", out)

    # --- Malformed entries raise, naming the shader, instead of being read loosely ---

    SKIN = {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}
    TERRAIN = {"grid": "c11.xy", "cell": "c46.xy", "height_scale": "c46.z", "origin": "c113.xy",
               "tex_offset": "c47.zw", "tex_scale": "c47.xy", "patch_offset": None, "height_fetch": 16}
    UV = {"fetch": 2, "src": "y", "format": 31, "offset": 3, "stages": []}

    def test_unknown_keys_are_rejected(self):
        # A mistyped key used to be dropped without a word (a "deformd" entry drew as undeformed, a
        # "weight_fetchh" skin became rigid by KeyError or silently, a "patch_ofset" lost its offset).
        base = {"base": 0, "layout": "dot"}
        bad = [
            (dict(base, deformd=True), "unknown key deformd"),
            (dict(base, pos_fetchh=0), "unknown key pos_fetchh"),
            ({"rejeted": "no"}, "unknown key rejeted"),
            (dict(base, skin=dict(self.SKIN, index_componentt="z")), "unknown skin key index_componentt"),
            (dict(base, instance=dict(self.INSTANCE, biass=0.5)), "unknown instance key biass"),
            (dict(base, terrain=dict(self.TERRAIN, patch_ofset="c113.x")), "unknown terrain key patch_ofset"),
            (dict(base, uv={"o0.x": dict(self.UV, stage=[])}), "unknown uv key stage"),
            (dict(base, skin=[2, 6, 7, 8]), "skin is not an object"),
            (dict(base, instance="c12"), "instance is not an object"),
            (dict(base, terrain=16), "terrain is not an object"),
            (dict(base, uv=[self.UV]), "uv is not an object"),
            (dict(base, uv={"o0.x": 2}), "uv is not an object"),
            ("rejected", "not an object"),
        ]
        for entry, why in bad:
            with self.assertRaises(ValueError, msg=repr(entry)) as cm:
                gtt.generate({"0x000000000000ABC8": entry})
            self.assertIn("0x000000000000ABC8", str(cm.exception))
            self.assertIn(why, str(cm.exception), repr(entry))
        # What the finder writes for a shader (matrix_finder.py) is still read.
        self.assertIn("FABLE2_VS_TRANSFORM(0x000000000000ABC8ull, 8, 1, -1, 0)",
                      gtt.generate({"0x000000000000ABC8": {"base": 8, "layout": "combine", "score": 0.97, "samples": 12,
                                               "pos_fetch": -1}}))

    def test_unknown_layout_is_rejected(self):
        # Anything but "dot" used to mean "combine".
        for layout in ("dott", "Dot", "", None, 0, 1, ["dot"]):
            with self.assertRaisesRegex(ValueError, "0x000000000000ABC9.*layout", msg=repr(layout)):
                gtt.generate({"0x000000000000ABC9": {"base": 0, "layout": layout}})
        with self.assertRaisesRegex(ValueError, "0x000000000000ABC9.*layout"):
            gtt.generate({"0x000000000000ABC9": {"base": 0}})
        self.assertIn("FABLE2_VS_TRANSFORM(0x000000000000ABC9ull, 0, 0, -1, 0)", gtt.generate({"0x000000000000ABC9": {"base": 0, "layout": "dot"}}))
        self.assertIn("FABLE2_VS_TRANSFORM(0x000000000000ABC9ull, 0, 1, -1, 0)",
                      gtt.generate({"0x000000000000ABC9": {"base": 0, "layout": "combine"}}))

    def test_base_is_an_integer_in_range(self):
        # Four constant registers from `base`: 0 to 252, a JSON integer.
        for base in (3.5, 4.0, "4", True, None, -1, 253, 1024, [4]):
            with self.assertRaisesRegex(ValueError, "0x000000000000ABCA.*base", msg=repr(base)):
                gtt.generate({"0x000000000000ABCA": {"base": base, "layout": "dot"}})
        self.assertIn("FABLE2_VS_TRANSFORM(0x000000000000ABCAull, 252, 0, -1, 0)",
                      gtt.generate({"0x000000000000ABCA": {"base": 252, "layout": "dot"}}))

    def test_one_shader_has_one_entry(self):
        # The same hash in two spellings used to give two table lines.
        entry = {"base": 0, "layout": "dot"}
        with self.assertRaises(ValueError) as cm:
            gtt.generate({"0x000000000000ABCB": entry, "0x000000000000abcb": dict(entry, base=4)})
        self.assertIn("0x000000000000ABCB", str(cm.exception))
        self.assertIn("0x000000000000abcb", str(cm.exception))
        self.assertIn("twice", str(cm.exception))

    def test_key_is_0x_and_sixteen_hex_digits(self):
        # The key is written into the table as a C++ literal: a key in another spelling used to be
        # emitted as it stood ("ABCBull", " 0xAB ull", "-0x1ull"). The albedo table's rule.
        entry = {"base": 0, "layout": "dot"}
        for key in ("ABCB", "0xABCB", " 0x000000000000ABCB ", " 0xAB ", "-0x1", "0x1", "0X000000000000ABCB",
                    "0x000000000000ABCG", "0x0000000000000ABCB", "000000000000ABCB", "0x_00000000000ABCB", ""):
            with self.assertRaises(ValueError, msg=key) as cm:
                gtt.generate({key: entry})
            self.assertIn(repr(key), str(cm.exception))
            self.assertIn("16 hex digits", str(cm.exception))
        # Lower case is one spelling of the same key: written in upper case, on every line of the entry.
        out = gtt.generate({"0x000000000000abcb": dict(entry, pos_swizzle="yxw1", skin=dict(self.SKIN),
                                                       uv={"o0.x": dict(self.UV)})})
        self.assertEqual(out.count("(0x000000000000ABCBull,"), 4)
        self.assertNotIn("abcb", out)

    def test_fetch_fields_are_json_integers_in_range(self):
        # int() used to read 2.5 as 2, true as 1 and "2" as 2. Fetch ordinals 0 to 127 (the runtime keeps
        # them in a signed byte; -1 only for pos_fetch, the default), a vertex format 0 to 63, an offset
        # of 0 to 255 dwords, the heightmap's texture fetch slot 0 to 31.
        base = {"base": 0, "layout": "dot"}
        skin_w = {"index_fetch": 1, "weight_fetch": 2, "row_fetches": [3, 4, 5], "pairs": [["x", "z"]]}
        bad = [
            (dict(base, pos_fetch=1.5), "pos_fetch"), (dict(base, pos_fetch="0"), "pos_fetch"),
            (dict(base, pos_fetch=True), "pos_fetch"), (dict(base, pos_fetch=-2), "pos_fetch"),
            (dict(base, pos_fetch=128), "pos_fetch"), (dict(base, pos_fetch=None), "pos_fetch"),
            (dict(base, uv={"o0.x": dict(self.UV, fetch=2.5)}), "uv fetch"),
            (dict(base, uv={"o0.x": dict(self.UV, fetch=True)}), "uv fetch"),
            (dict(base, uv={"o0.x": dict(self.UV, fetch="2")}), "uv fetch"),
            (dict(base, uv={"o0.x": dict(self.UV, fetch=-1)}), "uv fetch"),
            (dict(base, uv={"o0.x": dict(self.UV, fetch=128)}), "uv fetch"),
            (dict(base, uv={"o0.x": dict(self.UV, format="31")}), "uv format"),
            (dict(base, uv={"o0.x": dict(self.UV, format=31.0)}), "uv format"),
            (dict(base, uv={"o0.x": dict(self.UV, format=64)}), "uv format"),
            (dict(base, uv={"o0.x": dict(self.UV, offset=3.5)}), "uv offset"),
            (dict(base, uv={"o0.x": dict(self.UV, offset=-1)}), "uv offset"),
            (dict(base, uv={"o0.x": dict(self.UV, offset=256)}), "uv offset"),
            (dict(base, uv={"o0.x": dict(self.UV, src="xy")}), "uv src"),
            (dict(base, uv={"o0.x": dict(self.UV, src=1)}), "uv src"),
            (dict(base, uv={"o0.x": {k: v for k, v in self.UV.items() if k != "fetch"}}), "uv fetch"),
            (dict(base, uv={"o0.x": dict(self.UV, stages="c10.y")}), "uv stages"),
            (dict(base, uv={"o0.x": dict(self.UV, stages=[{"scal": "c10.y"}])}), "uv stage"),
            (dict(base, uv={"o0.x": dict(self.UV, stages=["c10.y"])}), "uv stage"),
            (dict(base, uv={"o9.q": dict(self.UV)}), "interpolator"),
            (dict(base, skin=dict(self.SKIN, index_fetch="2")), "skin index_fetch"),
            (dict(base, skin=dict(self.SKIN, index_fetch=2.0)), "skin index_fetch"),
            (dict(base, skin=dict(self.SKIN, row_fetches=[6, "7", 8])), "skin row_fetches"),
            (dict(base, skin=dict(self.SKIN, row_fetches=[6, 7.5, 8])), "skin row_fetches"),
            (dict(base, skin=dict(self.SKIN, row_fetches=6)), "skin row_fetches"),
            (dict(base, skin=dict(self.SKIN, row_fetches=[6, 7, True])), "skin row_fetches"),
            (dict(base, skin=dict(self.SKIN, index_component="q")), "skin index_component"),
            (dict(base, skin=dict(skin_w, weight_fetch=True)), "skin weight_fetch"),
            (dict(base, skin=dict(skin_w, weight_fetch=2.0)), "skin weight_fetch"),
            (dict(base, skin=dict(skin_w, pairs=[["x"]])), "skin pairs"),
            (dict(base, skin=dict(skin_w, pairs="xz")), "skin pairs"),
            (dict(base, instance=dict(self.INSTANCE, mesh_fetch=4.0)), "instance mesh_fetch"),
            (dict(base, instance=dict(self.INSTANCE, mesh_fetch="4")), "instance mesh_fetch"),
            (dict(base, instance=dict(self.INSTANCE, row_fetches=[0, 1, 2.0])), "instance row_fetches"),
            (dict(base, instance=dict(self.INSTANCE, bias="0.5")), "instance bias"),
            (dict(base, instance=dict(self.INSTANCE, bias=True)), "instance bias"),
            (dict(base, instance=dict(self.INSTANCE, bias=float("nan"))), "instance bias"),
            (dict(base, instance=dict(self.INSTANCE, offset=7)), "instance offset"),
            (dict(base, terrain=dict(self.TERRAIN, height_fetch=16.0)), "terrain height_fetch"),
            (dict(base, terrain=dict(self.TERRAIN, height_fetch="16")), "terrain height_fetch"),
            (dict(base, terrain=dict(self.TERRAIN, height_fetch=32)), "terrain height_fetch"),
            (dict(base, terrain=dict(self.TERRAIN, grid=44)), "bad register 44"),
            (dict(base, terrain={k: v for k, v in self.TERRAIN.items() if k != "cell"}), "terrain"),
        ]
        for entry, why in bad:
            with self.assertRaises(ValueError, msg=repr(entry)) as cm:
                gtt.generate({"0x000000000000ABCD": entry})
            self.assertTrue(str(cm.exception).startswith("0x000000000000ABCD: "), str(cm.exception))
            self.assertIn(why, str(cm.exception), repr(entry))
        # The bounds themselves are taken.
        out = gtt.generate({"0x000000000000ABCD": dict(base, pos_fetch=127, uv={
            "o0.x": dict(self.UV, fetch=127, format=63, offset=255), "o0.y": dict(self.UV, fetch=0, format=0, offset=0)})})
        self.assertIn("FABLE2_VS_TRANSFORM(0x000000000000ABCDull, 0, 0, 127, 0)", out)
        self.assertIn("FABLE2_VS_UV(0x000000000000ABCDull, 0, 0, 127, 1, 63, 255, -1, -1, -1, -1)", out)
        self.assertIn("FABLE2_VS_UV(0x000000000000ABCDull, 0, 1, 0, 1, 0, 0, -1, -1, -1, -1)", out)

    def test_deformed_is_a_json_boolean(self):
        # Any truthy value used to mean deformed: "false" drew the entry as deformed.
        base = {"base": 0, "layout": "dot"}
        for value in ("false", "true", 1, 0, None, [], 1.0):
            with self.assertRaisesRegex(ValueError, "^0x000000000000ABCE: deformed", msg=repr(value)):
                gtt.generate({"0x000000000000ABCE": dict(base, deformed=value)})
        self.assertIn("FABLE2_VS_TRANSFORM(0x000000000000ABCEull, 0, 0, -1, 1)",
                      gtt.generate({"0x000000000000ABCE": dict(base, deformed=True)}))
        self.assertIn("FABLE2_VS_TRANSFORM(0x000000000000ABCEull, 0, 0, -1, 0)",
                      gtt.generate({"0x000000000000ABCE": dict(base, deformed=False)}))

    def test_load_refuses_a_key_written_twice(self):
        # json.loads keeps the last of two equal keys without a word.
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "t.json"
            path.write_text('{"0x000000000000ABCC": {"base": 0, "layout": "dot"}, "0x000000000000ABCC": {"base": 4, "layout": "dot"}}')
            with self.assertRaisesRegex(ValueError, "0x000000000000ABCC.*twice"):
                gtt.load(path)
            with self.assertRaisesRegex(ValueError, "0x000000000000ABCC.*twice"):
                gtt.main(["--json", str(path), "--out", str(Path(tmp) / "t.inc")])
            self.assertFalse((Path(tmp) / "t.inc").exists())
            path.write_text('{"0x000000000000ABCC": {"base": 0, "base": 4, "layout": "dot"}}')
            with self.assertRaisesRegex(ValueError, "base.*twice"):
                gtt.load(path)
            path.write_text('{"0x000000000000ABCC": {"base": 0, "layout": "dot"}}')
            self.assertEqual(gtt.load(path), {"0x000000000000ABCC": {"base": 0, "layout": "dot"}})

    # --- "light": what the light of a draw reads on the vertex side ---

    NORMAL = {"fetch": 1, "format": 16, "offset": 2}
    COLOUR = {"fetch": 4, "format": 6, "offset": 0}
    WEIGHTS = {"fetch": 4, "format": 38, "offset": 0}
    L = "0x000000000000ABCD"

    def _light(self, light, **entry):
        e = {"base": 0, "layout": "dot"}
        e.update(entry)
        e["light"] = light
        return gtt.generate({self.L: e}).splitlines()

    def _stream(self, **extra):
        return dict({"kind": "stream", "normal": self.NORMAL, "colour": self.COLOUR, "power": 2.2, "evidence": "e"},
                    **extra)

    def _probe(self, **extra):
        return dict({"kind": "probe", "normal": self.NORMAL, "weights": self.WEIGHTS, "a0": "z", "evidence": "e"},
                    **extra)

    def _instanced(self, **extra):
        return dict({"kind": "instance", "normal": {"fetch": 5, "format": 32, "offset": 2},
                     "colour": {"fetch": 3, "format": 6, "offset": 6}, "evidence": "e"}, **extra)

    def test_light_lines(self):
        # FABLE2_VS_LIGHT(hash, kind, normal fetch, format, offset, second fetch, format, offset, a0,
        # scale reference, power): kind 1 terrain, 2 stream, 3 probe, 4 instance; the second fetch is the
        # colour stream's, the probe weights' or the instance colour's; a0 the source component of the
        # position fetch that holds the probe set's index (-1 without); the scale is the constant the
        # shader multiplies its world normal by (register * 4 + component, -1 without); the power is the
        # colour stream's literal exponent (0.0f without). After every other line of the shader.
        p = "FABLE2_VS_LIGHT(0x000000000000ABCDull, "
        self.assertEqual(self._light(self._stream())[2:], [p + "2, 1, 16, 2, 4, 6, 0, -1, -1, 2.2f)"])
        self.assertEqual(self._light(self._stream(scale="c7.w", power=2))[2], p + "2, 1, 16, 2, 4, 6, 0, -1, 31, 2.0f)")
        self.assertEqual(self._light(self._probe())[2], p + "3, 1, 16, 2, 4, 38, 0, 2, -1, 0.0f)")
        self.assertEqual(self._light(self._probe(a0="x", scale="c7.w", weights={"fetch": 6, "format": 38, "offset": 0}))[2],
                         p + "3, 1, 16, 2, 6, 38, 0, 0, 31, 0.0f)")
        out = self._light(self._instanced(), instance=self.INSTANCE)
        self.assertEqual(out[-1], p + "4, 5, 32, 2, 3, 6, 6, -1, -1, 0.0f)")
        self.assertEqual([l.split("(")[0] for l in out[1:]], ["FABLE2_VS_TRANSFORM", "FABLE2_VS_INSTANCE", "FABLE2_VS_LIGHT"])
        out = self._light({"kind": "terrain", "evidence": "e"}, terrain=self.TERRAIN)
        self.assertEqual(out[-1], p + "1, -1, 0, 0, -1, 0, 0, -1, -1, 0.0f)")
        # a skinned shader may have a probe entry
        out = self._light(self._probe(), skin=self.SKIN)
        self.assertEqual([l.split("(")[0] for l in out[1:]], ["FABLE2_VS_TRANSFORM", "FABLE2_VS_SKIN", "FABLE2_VS_LIGHT"])

    def test_light_is_refused_rather_than_read_loosely(self):
        terrain = {"kind": "terrain", "evidence": "e"}
        bad = [
            # (light, the rest of the entry, the message)
            ("stream", {}, "light is not an object"),
            ([self.NORMAL], {}, "light is not an object"),
            (self._stream(kind="colour"), {}, "unknown light kind 'colour'"),
            (self._stream(kind=2), {}, "unknown light kind 2"),
            ({"normal": self.NORMAL, "evidence": "e"}, {}, "unknown light kind None"),
            (self._stream(normall=self.NORMAL), {}, "unknown light key normall"),
            (self._stream(manual=True), {}, "unknown light key manual"),
            # the evidence
            ({k: v for k, v in self._stream().items() if k != "evidence"}, {}, "light without evidence"),
            (self._stream(evidence=" "), {}, "light without evidence"),
            (self._probe(evidence=["dump"]), {}, "light without evidence"),
            # each kind has its keys and no other
            (dict(terrain, normal=self.NORMAL), {"terrain": self.TERRAIN}, "a terrain light takes no normal"),
            (dict(terrain, power=2.2), {"terrain": self.TERRAIN}, "a terrain light takes no power"),
            (self._stream(weights=self.WEIGHTS), {}, "a stream light takes no weights"),
            (self._stream(a0="z"), {}, "a stream light takes no a0"),
            (self._probe(colour=self.COLOUR), {}, "a probe light takes no colour"),
            (self._probe(power=2.2), {}, "a probe light takes no power"),
            (self._instanced(weights=self.WEIGHTS), {"instance": self.INSTANCE}, "an instance light takes no weights"),
            (self._instanced(scale="c7.w"), {"instance": self.INSTANCE}, "an instance light takes no scale"),
            (self._instanced(power=2.2), {"instance": self.INSTANCE}, "an instance light takes no power"),
            ({k: v for k, v in self._stream().items() if k != "normal"}, {}, "a stream light needs normal"),
            ({k: v for k, v in self._stream().items() if k != "colour"}, {}, "a stream light needs colour"),
            ({k: v for k, v in self._stream().items() if k != "power"}, {}, "a stream light needs power"),
            ({k: v for k, v in self._probe().items() if k != "weights"}, {}, "a probe light needs weights"),
            ({k: v for k, v in self._probe().items() if k != "a0"}, {}, "a probe light needs a0"),
            ({k: v for k, v in self._instanced().items() if k != "colour"}, {"instance": self.INSTANCE},
             "an instance light needs colour"),
            # the kind and the entry's own form
            (terrain, {}, "a terrain light belongs to an entry with terrain"),
            (self._instanced(), {}, "an instance light belongs to an entry with instance"),
            (self._stream(), {"instance": self.INSTANCE}, "a stream light does not belong to an entry with instance"),
            (self._probe(), {"instance": self.INSTANCE}, "a probe light does not belong to an entry with instance"),
            (self._stream(), {"terrain": self.TERRAIN}, "a stream light does not belong to an entry with terrain"),
            (self._instanced(), {"instance": self.INSTANCE, "terrain": self.TERRAIN},
             "an instance light does not belong to an entry with terrain"),
            # the fetches: an object with fetch, format, offset; the format is the one the reader decodes
            (self._stream(normal=1), {}, "light normal is not an object"),
            (self._stream(normal=dict(self.NORMAL, slot=95)), {}, "unknown light normal key slot"),
            (self._stream(normal={"fetch": 1, "format": 16}), {}, "light normal offset None"),
            (self._stream(normal=dict(self.NORMAL, fetch=-1)), {}, "light normal fetch -1"),
            (self._stream(normal=dict(self.NORMAL, fetch=128)), {}, "light normal fetch 128"),
            (self._stream(normal=dict(self.NORMAL, fetch=1.0)), {}, "light normal fetch 1.0"),
            (self._stream(normal=dict(self.NORMAL, fetch=True)), {}, "light normal fetch True"),
            (self._stream(normal=dict(self.NORMAL, offset=256)), {}, "light normal offset 256"),
            (self._stream(normal=dict(self.NORMAL, offset="2")), {}, "light normal offset '2'"),
            (self._stream(normal=dict(self.NORMAL, format=7)), {}, "light normal format 7 is not 16 (10_11_11) or 32"),
            (self._stream(normal=dict(self.NORMAL, format=6)), {}, "light normal format 6 is not"),
            (self._stream(colour=dict(self.COLOUR, format=7)), {}, "light colour format 7 is not 6 (8_8_8_8)"),
            (self._probe(weights=dict(self.WEIGHTS, format=32)), {}, "light weights format 32 is not 38 (32_32_32_32_FLOAT)"),
            (self._instanced(colour={"fetch": 3, "format": 38, "offset": 6}), {"instance": self.INSTANCE},
             "light colour format 38 is not 6"),
            # a0, scale, power
            (self._probe(a0="zw"), {}, "light a0 'zw' is not one of x, y, z, w"),
            (self._probe(a0=2), {}, "light a0 2 is not one of x, y, z, w"),
            (self._probe(scale="c7"), {}, "bad register 'c7'"),
            (self._probe(scale="c7.xy"), {}, "names more than one component"),
            (self._stream(scale="c256.w"), {}, "bad register 'c256.w'"),
            (self._stream(scale=31), {}, "bad register 31"),
            (self._stream(power=0), {}, "light power 0 is not a number above 0"),
            (self._stream(power=-2.2), {}, "light power -2.2 is not a number above 0"),
            (self._stream(power="2.2"), {}, "light power '2.2' is not a number above 0"),
            (self._stream(power=True), {}, "light power True is not a number above 0"),
            (self._stream(power=float("nan")), {}, "light power nan is not a number above 0"),
            (self._stream(power=float("inf")), {}, "light power inf is not a number above 0"),
        ]
        for light, rest, why in bad:
            with self.assertRaises(ValueError, msg=repr(light)) as cm:
                self._light(light, **rest)
            self.assertTrue(str(cm.exception).startswith(self.L + ": "), str(cm.exception))
            self.assertIn(why, str(cm.exception), repr(light))
        # without a transform the entry has no line, and a light with it is a mistake
        with self.assertRaisesRegex(ValueError, f"{self.L}: light entry without base"):
            gtt.generate({self.L: {"rejected": "no", "light": self._stream()}})

    def test_the_committed_light_entries(self):
        # One entry per vertex shader of light_check.py's FAMILIES (the leaves' apart: no family yet),
        # of the kind that shader's colour comes from; the fetches are the dumps' in DecodeVertexFetches
        # order (out/native-evidence/lighting/task4_table_facts.txt).
        import sys
        sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))
        import light_check
        table = gtt.load(ROOT / "docs" / "native-renderer" / "vs-transforms.json")
        kinds = {"0x" + vs: e["light"]["kind"] for vs, e in ((k[2:], v) for k, v in table.items()) if "light" in e}
        want = {}
        for family, pairs in light_check.FAMILIES.items():
            for _, vs in pairs:
                if family.startswith("terrain"):
                    kind = "terrain"
                elif family.startswith("plant"):
                    kind = "instance"
                elif family.startswith("tree-leaf"):
                    continue
                else:
                    kind = "stream" if vs == "ECD66A10092E6562" else "probe"
                self.assertEqual(want.setdefault("0x" + vs, kind), kind, vs)
        self.assertEqual(kinds, want)
        self.assertEqual(len(kinds), 15)
        light = {vs: table[vs]["light"] for vs in kinds}
        normal = {"fetch": 1, "format": 16, "offset": 2}
        self.assertEqual({k: v for k, v in light["0xECD66A10092E6562"].items() if k != "evidence"},
                         {"kind": "stream", "normal": normal, "colour": {"fetch": 4, "format": 6, "offset": 0},
                          "scale": "c7.w", "power": 2.2})
        probes = {"0x79EAC49585797037": (4, "c7.w"), "0xD4D558DA6A82BDC8": (6, None),
                  "0xA1F7E9885EC466DF": (5, None), "0x475EC9F795E5EDBB": (5, "c7.w")}
        for vs, (fetch, scale) in probes.items():
            want = {"kind": "probe", "normal": normal, "weights": {"fetch": fetch, "format": 38, "offset": 0}, "a0": "z"}
            if scale:
                want["scale"] = scale
            self.assertEqual({k: v for k, v in light[vs].items() if k != "evidence"}, want, vs)
        for vs, kind in kinds.items():
            if kind == "instance":
                self.assertEqual({k: v for k, v in light[vs].items() if k != "evidence"},
                                 {"kind": "instance", "normal": {"fetch": 5, "format": 32, "offset": 2},
                                  "colour": {"fetch": 3, "format": 6, "offset": 6}}, vs)
            if kind == "terrain":
                self.assertEqual(sorted(light[vs]), ["evidence", "kind"], vs)
        self.assertNotIn("light", table["0xA5846836C90E1192"])

    def test_the_committed_table_uses_the_allowed_keys(self):
        # The lists are the committed file's keys plus the three the finder writes.
        table = gtt.load(ROOT / "docs" / "native-renderer" / "vs-transforms.json")
        used = {"entry": set(), "skin": set(), "instance": set(), "terrain": set(), "uv": set()}
        self.assertEqual({k for e in table.values() for k in e.get("light", {})}, set(gtt.LIGHT_KEYS))
        self.assertEqual({k for e in table.values() for part in ("normal", "colour", "weights")
                          for k in e.get("light", {}).get(part, {})}, set(gtt.LIGHT_FETCH_KEYS))
        for e in table.values():
            used["entry"] |= set(e)
            for part in ("skin", "instance", "terrain"):
                used[part] |= set(e.get(part, {}))
            for u in e.get("uv", {}).values():
                used["uv"] |= set(u)
        self.assertEqual(used["entry"] | {"base2", "score", "samples"}, set(gtt.ENTRY_KEYS))
        self.assertEqual(used["skin"], set(gtt.SKIN_KEYS))
        self.assertEqual(used["instance"], set(gtt.INSTANCE_KEYS))
        self.assertEqual(used["terrain"], set(gtt.TERRAIN_KEYS))
        self.assertEqual(used["uv"], set(gtt.UV_KEYS))


class CommittedLeafEntriesTest(unittest.TestCase):
    """The table as committed. The leaf vertex shaders 0xA584..., 0xD7DD... and 0x2D40... are one
    shader with one, two and four lights (frame-map section 13): the same position and the
    same UV export, so the three entries carry one layout, and the .inc is a fresh generator run."""

    LEAVES = ("0xA5846836C90E1192", "0xD7DD31B9BF194DE8", "0x2D40B53C926109BE")
    LAYOUT_KEYS = ("base", "layout", "pos_fetch", "pos_swizzle", "deformed", "uv")

    @staticmethod
    def lines(vs):
        return [f"FABLE2_VS_TRANSFORM({vs}ull, 0, 0, 0, 1)",
                f"FABLE2_VS_POS_SWIZZLE({vs}ull, 0xAC1)",
                f"FABLE2_VS_UV({vs}ull, 1, 3, 2, 1, 31, 3, -1, -1, -1, -1)",
                f"FABLE2_VS_UV({vs}ull, 2, 3, 2, 0, 31, 3, -1, -1, -1, -1)"]

    def setUp(self):
        self.table = json.loads((ROOT / "docs" / "native-renderer" / "vs-transforms.json").read_text())

    def test_leaf_form_lines(self):
        entry = {"base": 0, "layout": "dot", "pos_fetch": 0, "manual": True, "deformed": True, "pos_swizzle": "yxw1",
                 "uv": {"o1.w": {"fetch": 2, "format": 31, "offset": 3, "stages": [], "src": "y"},
                        "o2.w": {"fetch": 2, "format": 31, "offset": 3, "stages": [], "src": "x"}}}
        self.assertEqual(gtt.generate({"0x000000000000ABC7": entry}).splitlines()[1:], self.lines("0x000000000000ABC7"))

    def test_d7dd_and_2d40_carry_the_layout_of_a584(self):
        ref = self.table[self.LEAVES[0]]
        for vs in self.LEAVES[1:]:
            entry = self.table.get(vs)
            self.assertIsNotNone(entry, vs)
            self.assertNotIn("rejected", entry, vs)
            self.assertIs(entry.get("manual"), True, vs)
            self.assertIs(entry.get("uv_manual"), True, vs)
            self.assertTrue(entry.get("evidence"), vs)
            self.assertTrue(entry.get("uv_evidence"), vs)
            self.assertEqual({k: entry.get(k) for k in self.LAYOUT_KEYS}, {k: ref[k] for k in self.LAYOUT_KEYS}, vs)

    def test_committed_inc_is_a_fresh_generator_run_with_the_leaf_lines(self):
        inc = (ROOT / "src" / "native" / "capture" / "vs_transform_table.inc").read_text()
        self.assertEqual(inc, gtt.generate(self.table))
        have = set(inc.splitlines())
        for vs in self.LEAVES:
            for line in self.lines(vs):
                self.assertTrue(line in have, line)


if __name__ == "__main__":
    unittest.main()
