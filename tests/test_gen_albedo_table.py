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
        with self.assertRaisesRegex(ValueError, "0x0000000000000001.*slot"):
            gen_albedo_table.generate({"0x0000000000000001": _albedo(slot=32)})


def _albedo(**extra):
    e = {"slot": 3, "manual": True, "evidence": "e",
         "u": {"input": "r0.x", "stages": []}, "v": {"input": "r0.y", "stages": []}}
    if "alpha" in extra:
        # an alpha recipe has its own evidence (the committed entries also carry "alpha_manual")
        e.update(alpha_manual=True, alpha_evidence="e")
    e.update(extra)
    return e


def _terrain(**extra):
    e = {"kind": "terrain-layer", "base_slot": 3, "detail_slot": 0, "manual": True, "evidence": "e"}
    e.update(extra)
    return e


class GenAlphaKindTest(unittest.TestCase):
    H = "0x00000000000000AA"

    def _gen(self, entry):
        return gen_albedo_table.generate({self.H: entry}).splitlines()

    def test_no_alpha_or_kind_adds_nothing(self):
        self.assertEqual(len(self._gen(_albedo())), 2)

    def test_alpha_forms(self):
        # scale refs are register * 4 + component (no bank bit: always pixel constants)
        a = "FABLE2_PS_ALPHA(0x00000000000000AAull, "
        out = self._gen(_albedo(alpha={"source": "one"}))
        self.assertEqual(out[2], a + "0, 0, 3, -1, -1, 0)")
        out = self._gen(_albedo(alpha={"source": "albedo", "component": "w", "scale": "c15.w"}))
        self.assertEqual(out[2], a + "1, 3, 3, 63, -1, 0)")
        out = self._gen(_albedo(alpha={"source": "albedo", "component": "x"}))
        self.assertEqual(out[2], a + "1, 3, 0, -1, -1, 0)")
        out = self._gen(_albedo(alpha={"source": "fetch", "slot": 8, "component": "y", "scale": "c2.z"}))
        self.assertEqual(out[2], a + "2, 8, 1, 10, -1, 0)")
        out = self._gen(_albedo(alpha={"source": "fetch", "slot": 0, "component": "w", "scale": ["c26.w"]}))
        self.assertEqual(out[2], a + "2, 0, 3, 107, -1, 0)")
        out = self._gen(_albedo(alpha={"source": "albedo", "component": "w", "scale": ["c26.w", "c15.w"]}))
        self.assertEqual(out[2], a + "1, 3, 3, 107, 63, 0)")
        out = self._gen(_albedo(alpha={"source": "albedo", "component": "w", "scale": ["c26.w", "c15.w"],
                                       "divide_by_alpha": True}))
        self.assertEqual(out[2], a + "1, 3, 3, 107, 63, 1)")
        out = self._gen(_albedo(alpha={"source": "fetch", "slot": 2, "component": "x", "divide_by_alpha": False}))
        self.assertEqual(out[2], a + "2, 2, 0, -1, -1, 0)")

    def test_plain_kind_adds_nothing(self):
        self.assertEqual(len(self._gen(_albedo(kind="plain"))), 2)

    def test_terrain_kinds(self):
        # A terrain entry is one line, kind 1 layer / 2 far / 3 far with the fade, then the texture fetch
        # slots of the base map and of the detail (far: the far colour) texture, then the steep-ground
        # block: the side texture's slot and the two literals, or -1 and zeros without one. It has no
        # albedo line: the runtime's albedo lookup does not know the shader.
        t = "FABLE2_PS_TERRAIN(0x00000000000000AAull, "
        none = ", -1, 0.0f, 0.0f)"
        self.assertEqual(self._gen(_terrain()), [self._gen(_terrain())[0], t + "1, 3, 0" + none])
        self.assertEqual(self._gen(_terrain(kind="terrain-far", detail_slot=13))[1], t + "2, 3, 13" + none)
        self.assertEqual(self._gen(_terrain(kind="terrain-far-fade", detail_slot=13))[1], t + "3, 3, 13" + none)
        self.assertEqual(self._gen(_terrain(base_slot=31, detail_slot=30))[1], t + "1, 31, 30" + none)

    def test_terrain_steep_block(self):
        # "steep": the layer shader blends a side texture in on steep ground. The two literals are written
        # as float literals with the digits of the JSON number.
        t = "FABLE2_PS_TERRAIN(0x00000000000000AAull, "
        steep = {"threshold": 0.99609375, "epsilon": 0.0001, "side_slot": 13}
        self.assertEqual(self._gen(_terrain(steep=steep))[1], t + "1, 3, 0, 13, 0.99609375f, 0.0001f)")
        self.assertEqual(self._gen(_terrain(steep={"threshold": 1, "epsilon": 1e-05, "side_slot": 31}))[1],
                         t + "1, 3, 0, 31, 1.0f, 1e-05f)")
        bad = [
            _terrain(kind="terrain-far", detail_slot=14, steep=steep),          # the block is the layers'
            _terrain(kind="terrain-far-fade", detail_slot=14, steep=steep),
            _albedo(steep=steep),                                                # not on a plain entry
            _terrain(steep={}),
            _terrain(steep=13),
            _terrain(steep={"threshold": 0.99609375, "epsilon": 0.0001}),        # no side texture
            _terrain(steep={"threshold": 0.99609375, "side_slot": 13}),
            _terrain(steep={"epsilon": 0.0001, "side_slot": 13}),
            _terrain(steep=dict(steep, slot=13)),                                # an unknown key
            _terrain(steep=dict(steep, side_slot=32)),
            _terrain(steep=dict(steep, side_slot=-1)),
            _terrain(steep=dict(steep, side_slot=True)),
            _terrain(steep=dict(steep, side_slot="13")),
            _terrain(steep=dict(steep, side_slot=0)),                            # the detail texture's slot
            _terrain(steep=dict(steep, side_slot=3)),                            # the base map's slot
            _terrain(steep=dict(steep, threshold=0)),                            # the block would never run
            _terrain(steep=dict(steep, threshold=1.5)),
            _terrain(steep=dict(steep, threshold=-0.5)),
            _terrain(steep=dict(steep, threshold="0.99609375")),
            _terrain(steep=dict(steep, threshold=True)),
            _terrain(steep=dict(steep, threshold=float("nan"))),
            _terrain(steep=dict(steep, epsilon=0)),                              # a division by zero on flat ground
            _terrain(steep=dict(steep, epsilon=-0.0001)),
            _terrain(steep=dict(steep, epsilon=1)),
            _terrain(steep=dict(steep, epsilon=None)),
            _terrain(steep=dict(steep, epsilon=float("inf"))),
            _terrain(steep=dict(steep, epsilon=float("nan"))),
            _terrain(steep=dict(steep, threshold=float("inf"))),
        ]
        for entry in bad:
            with self.assertRaises(ValueError, msg=repr(entry)) as cm:
                self._gen(entry)
            self.assertIn(self.H, str(cm.exception))

    def test_the_table_has_the_three_steep_shaders(self):
        # The three layer shaders paired with vertex shader C30A, with the literals of game session 1.
        import json
        root = Path(__file__).resolve().parent.parent
        data = json.loads((root / "docs" / "native-renderer" / "ps-albedo.json").read_text())
        steep = {k: v["steep"] for k, v in data.items() if "steep" in v}
        self.assertEqual(sorted(steep), ["0x04F23292B2F44077", "0x45243B758F9272AB", "0x9C2144E42E95A76F"])
        for v in steep.values():
            self.assertEqual(v, {"threshold": 0.99609375, "epsilon": 0.0001, "side_slot": 13})
        lines = gen_albedo_table.generate(data).splitlines()
        self.assertEqual(sum(", 13, 0.99609375f, 0.0001f)" in line for line in lines), 3)
        self.assertEqual(sum(", -1, 0.0f, 0.0f)" in line for line in lines), 5)
        # The generated table in the tree is this table's.
        inc = (root / "src" / "native" / "capture" / "ps_albedo_table.inc").read_text().splitlines()
        self.assertEqual(inc, lines)

    def test_order_alpha_then_terrain_after_albedo_lines(self):
        data = {"0x0000000000000003": _terrain(), "0x0000000000000002": _albedo(alpha={"source": "one"}),
                "0x0000000000000001": _albedo(), "0x0000000000000004": _terrain()}
        out = gen_albedo_table.generate(data).splitlines()
        self.assertEqual([l.split("(")[0] for l in out[1:]],
                         ["FABLE2_PS_ALBEDO", "FABLE2_PS_ALBEDO", "FABLE2_PS_ALPHA", "FABLE2_PS_TERRAIN",
                          "FABLE2_PS_TERRAIN"])
        self.assertNotIn("FABLE2_PS_KIND", "".join(out))

    def test_rejects_invalid(self):
        bad = [
            _albedo(alpha={"source": "bogus"}),
            _albedo(alpha={}),
            _albedo(alpha={"source": "albedo", "component": "q"}),
            _albedo(alpha={"source": "albedo"}),
            _albedo(alpha={"source": "albedo", "component": "x", "scale": "r1.x"}),
            _albedo(alpha={"source": "albedo", "component": "x", "scale": "-c1.x"}),
            _albedo(alpha={"source": "albedo", "component": "x", "scale": "c256.x"}),
            _albedo(alpha={"source": "fetch", "component": "x"}),
            _albedo(alpha={"source": "fetch", "slot": 32, "component": "x"}),
            _albedo(alpha={"source": "one", "scale": "c1.x"}),
            _albedo(alpha={"source": "albedo", "component": "x", "scale": []}),
            _albedo(alpha={"source": "albedo", "component": "x", "scale": ["c1.x", "c2.x", "c3.x"]}),
            _albedo(alpha={"source": "albedo", "component": "x", "scale": ["c1.x", "c1.x"]}),
            _albedo(alpha={"source": "albedo", "component": "x", "scale": ["c1.x", "r2.x"]}),
            _albedo(alpha={"source": "albedo", "component": "x", "scale": ["c1.x", None]}),
            _albedo(alpha={"source": "albedo", "component": "x", "scale": 5}),
            _albedo(alpha={"source": "albedo", "component": "x", "divide_by_alpha": 1}),
            _albedo(alpha={"source": "albedo", "component": "x", "divide_by_alpha": "true"}),
            _albedo(alpha={"source": "one", "divide_by_alpha": True}),
            _albedo(kind="water"),
            _albedo(kind="terrain-layer"),                       # a terrain kind on an albedo entry
            _albedo(base_slot=3, detail_slot=0),                 # terrain slots on a plain entry
            _albedo(kind="plain", detail_slot=0),
            _terrain(slot=0),
            _terrain(u={"input": "r0.x", "stages": []}),
            _terrain(alpha={"source": "one"}),
            _terrain(no_albedo="terrain"),
            _terrain(base_slot=None),
            _terrain(detail_slot=32),
            _terrain(detail_slot=-1),
            _terrain(base_slot=True),
            _terrain(base_slot="3"),
            _terrain(base_slot=0, detail_slot=0),                # one slot named twice
            {"kind": "terrain-far", "base_slot": 3, "manual": True, "evidence": "e"},
            {"kind": "terrain-layer", "detail_slot": 0, "manual": True, "evidence": "e"},
            {"no_albedo": "fog", "manual": True, "evidence": "e", "kind": "terrain-layer"},
            {"no_albedo": "fog", "manual": True, "evidence": "e", "alpha": {"source": "one"}},
        ]
        for entry in bad:
            with self.assertRaises(ValueError, msg=repr(entry)) as cm:
                self._gen(entry)
            self.assertIn(self.H, str(cm.exception))

    def test_rejects_malformed_entries(self):
        # An entry the generator used to accept and that changed a draw without a word (or kept a
        # typing mistake out of sight): each is refused with the shader's hash and the fault named.
        fog = {"no_albedo": "fog", "manual": True, "evidence": "e"}
        steep = {"threshold": 0.99609375, "epsilon": 0.0001, "side_slot": 13}
        bad = [
            # keys no form has, per form (ENTRY_KEYS)
            (_albedo(sloot=1), "unknown key sloot"),
            (_albedo(alpha_evidense="e"), "unknown key alpha_evidense"),
            (_albedo(uv={"o0.x": {}}), "unknown key uv"),
            (_terrain(side_slot=13), "unknown key side_slot"),
            (_terrain(alpha_evidence="e"), "unknown key alpha_evidence"),
            (dict(fog, rejected="no"), "unknown key rejected"),
            (dict(fog, alpha_manual=True), "unknown key alpha_manual"),
            # a no_albedo entry is the whole entry
            (dict(fog, slot=0), "a no_albedo entry takes no slot"),
            (dict(fog, u={"input": "r0.x", "stages": []}), "a no_albedo entry takes no u"),
            # keys "alpha" does not have; "slot" is the fetch source's
            (_albedo(alpha={"source": "albedo", "component": "w", "scales": "c26.w"}), "unknown alpha key scales"),
            (_albedo(alpha={"source": "fetch", "slot": 2, "component": "w", "divide": True}),
             "unknown alpha key divide"),
            (_albedo(alpha={"source": "one", "evidence": "e"}), "unknown alpha key evidence"),
            (_albedo(alpha={"source": "albedo", "component": "w", "slot": 3}), "slot only with source fetch"),
            # the albedo slot is a JSON integer 0 to 31
            (_albedo(slot=3.9), "bad texture fetch slot 3.9"),
            (_albedo(slot=3.0), "bad texture fetch slot 3.0"),
            (_albedo(slot="3"), "bad texture fetch slot '3'"),
            (_albedo(slot=True), "bad texture fetch slot True"),
            (_albedo(slot=None), "bad texture fetch slot None"),
            (_albedo(slot=-1), "bad texture fetch slot -1"),
            (_albedo(slot=32), "bad texture fetch slot 32"),
            ({k: v for k, v in _albedo().items() if k != "slot"}, "bad texture fetch slot None"),
            # hand-confirmed and with its evidence, in every form
            ({k: v for k, v in _albedo().items() if k != "manual"}, "manual is not true"),
            (_albedo(manual=False), "manual is not true"),
            (_albedo(manual="true"), "manual is not true"),
            (_albedo(manual=1), "manual is not true"),
            (_terrain(manual=False), "manual is not true"),
            ({"no_albedo": "fog", "evidence": "e"}, "manual is not true"),
            ({k: v for k, v in _albedo().items() if k != "evidence"}, "no evidence"),
            (_albedo(evidence=""), "no evidence"),
            (_albedo(evidence="  "), "no evidence"),
            (_albedo(evidence=["seen"]), "no evidence"),
            (_terrain(evidence=""), "no evidence"),
            ({"no_albedo": "fog", "manual": True}, "no evidence"),
            ({k: v for k, v in _albedo(alpha={"source": "one"}).items() if k != "alpha_evidence"},
             "alpha without alpha_evidence"),
            (_albedo(alpha={"source": "one"}, alpha_evidence=""), "alpha without alpha_evidence"),
            (_albedo(alpha={"source": "one"}, alpha_evidence=None), "alpha without alpha_evidence"),
            # the small term of the steep weights: the bound terrain_replay.py and terrain_patch.h use
            (_terrain(steep=dict(steep, epsilon=0.01)), "steep epsilon"),
            (_terrain(steep=dict(steep, epsilon=0.5)), "steep epsilon"),
            # not an entry at all
            ("fog", "not an object"),
            ([], "not an object"),
            (None, "not an object"),
        ]
        for entry, why in bad:
            with self.assertRaises(ValueError, msg=repr(entry)) as cm:
                self._gen(entry)
            self.assertIn(self.H, str(cm.exception))
            self.assertIn(why, str(cm.exception), repr(entry))
        # just inside the bounds
        self.assertEqual(len(self._gen(_terrain(steep=dict(steep, epsilon=0.0099)))), 2)
        self.assertEqual(len(self._gen(_albedo(slot=0))), 2)
        self.assertEqual(len(self._gen(_albedo(slot=31))), 2)
        self.assertEqual(len(self._gen(dict(fog, kind="plain"))), 2)

    def test_rejects_loose_coordinates_and_flags(self):
        # What still got through after the first pass: u and v were indexed without a look (a missing
        # one was a KeyError without the hash, a mistyped key inside was dropped), "alpha_manual":
        # false passed, and "no_albedo": false still made a no-albedo line.
        u = {"input": "r0.x", "stages": [{"scale": "c8.x", "offset": "c8.z"}]}
        fog = {"no_albedo": "fog", "manual": True, "evidence": "e"}
        bad = [
            ({k: v for k, v in _albedo().items() if k != "u"}, "u is not an object"),
            ({k: v for k, v in _albedo().items() if k != "v"}, "v is not an object"),
            (_albedo(u="r0.x"), "u is not an object"),
            (_albedo(v=["r0.y"]), "v is not an object"),
            (_albedo(u=None), "u is not an object"),
            (_albedo(u=dict(u, stage=[])), "unknown key stage in u"),
            (_albedo(v=dict(u, inputs="r0.y")), "unknown key inputs in v"),
            (_albedo(u={"stages": []}), "u has no input"),
            (_albedo(u={"input": "r0.x"}), "stages of u"),
            (_albedo(u=dict(u, input="r0.xy")), "bad interpolator component"),
            (_albedo(u=dict(u, input=3)), "bad interpolator component"),
            (_albedo(u=dict(u, stages={"scale": "c8.x"})), "stages of u"),
            (_albedo(u=dict(u, stages="c8.x")), "stages of u"),
            (_albedo(v=dict(u, stages=[{"scal": "c8.x"}])), "unknown key scal in a stage of v"),
            (_albedo(u=dict(u, stages=["c8.x"])), "a stage of u is not an object"),
            (_albedo(u=dict(u, stages=[{"scale": "r8.x"}])), "bad constant ref"),
            (_albedo(u=dict(u, stages=[{"scale": 8}])), "bad constant ref"),
            (_albedo(u=dict(u, stages=[{}, {}, {}])), "more than two UV stages"),
            (_albedo(alpha={"source": "one"}, alpha_manual=False), "alpha_manual is not true"),
            (_albedo(alpha={"source": "one"}, alpha_manual="true"), "alpha_manual is not true"),
            ({k: v for k, v in _albedo(alpha={"source": "one"}).items() if k != "alpha_manual"}, "alpha_manual is not true"),
            (dict(fog, no_albedo=False), "no_albedo is not a reason"),
            (dict(fog, no_albedo=True), "no_albedo is not a reason"),
            (dict(fog, no_albedo=None), "no_albedo is not a reason"),
            (dict(fog, no_albedo=""), "no_albedo is not a reason"),
            (dict(fog, no_albedo=0), "no_albedo is not a reason"),
        ]
        for entry, why in bad:
            with self.assertRaises(ValueError, msg=repr(entry)) as cm:
                self._gen(entry)
            self.assertTrue(str(cm.exception).startswith(self.H + ": "), str(cm.exception))
            self.assertIn(why, str(cm.exception), repr(entry))
        self.assertEqual(len(self._gen(_albedo(u=dict(u, stages=[{"scale": None}, {"offset": "-c8.w"}])))), 2)
        # The committed entries carry both alpha flags and a reason: nothing new is asked of them.
        import json
        data = json.loads((Path(__file__).resolve().parent.parent / "docs" / "native-renderer" / "ps-albedo.json").read_text())
        self.assertTrue(all(e.get("alpha_manual") is True for e in data.values() if "alpha" in e))
        self.assertTrue(all(isinstance(e["no_albedo"], str) and e["no_albedo"].strip() for e in data.values() if "no_albedo" in e))

    def test_the_steep_epsilon_bound_is_one_number(self):
        # Generator, replay and the C++ inputs refuse the same small terms (0.01 or more).
        import terrain_replay
        self.assertEqual(gen_albedo_table.STEEP_EPSILON_MAX, 0.01)
        self.assertIs(terrain_replay.STEEP_EPSILON_MAX, gen_albedo_table.STEEP_EPSILON_MAX)
        header = (Path(__file__).resolve().parent.parent / "src" / "native" / "capture" / "terrain_patch.h").read_text()
        self.assertIn("inline constexpr float kTerrainSteepEpsMax = 0.01f;", header)
        self.assertIn("m.steep_eps < kTerrainSteepEpsMax", header)

    def test_rejects_bad_and_repeated_hashes(self):
        # The key is 0x and sixteen hex digits; one shader has one entry, whatever the letters' case.
        for key in ("0xAA", "00000000000000AA", "0x00000000000000AG", "0X00000000000000AA", "0x000000000000000AA",
                    "0x00000000000000AA ", "0x_0000000000000AA", ""):
            with self.assertRaises(ValueError, msg=key) as cm:
                gen_albedo_table.generate({key: _albedo()})
            self.assertIn(repr(key), str(cm.exception))
            self.assertIn("16 hex digits", str(cm.exception))
        with self.assertRaises(ValueError) as cm:
            gen_albedo_table.generate({"0x00000000000000AB": _albedo(), "0x00000000000000ab": _albedo(slot=4)})
        self.assertIn("0x00000000000000AB", str(cm.exception))
        self.assertIn("0x00000000000000ab", str(cm.exception))
        self.assertIn("twice", str(cm.exception))
        # lower case alone is fine and is written in upper case
        self.assertIn("(0x00000000000000ABull,", gen_albedo_table.generate({"0x00000000000000ab": _albedo()}))

    def test_load_refuses_a_key_written_twice(self):
        # json.loads keeps the last of two equal keys without a word: the loader refuses them, at any
        # depth (two entries of one shader, or one field given twice).
        import json
        import tempfile
        entry = json.dumps(_albedo())
        twice = '{"0x00000000000000AA": %s, "0x00000000000000AA": %s}' % (entry, entry)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "t.json"
            path.write_text(twice)
            with self.assertRaisesRegex(ValueError, "0x00000000000000AA.*twice"):
                gen_albedo_table.load(path)
            with self.assertRaisesRegex(ValueError, "0x00000000000000AA.*twice"):
                gen_albedo_table.main(["--json", str(path), "--out", str(Path(tmp) / "t.inc")])
            self.assertFalse((Path(tmp) / "t.inc").exists())
            path.write_text('{"0x00000000000000AA": {"slot": 3, "slot": 4}}')
            with self.assertRaisesRegex(ValueError, "slot.*twice"):
                gen_albedo_table.load(path)
            path.write_text('{"0x00000000000000AA": %s}' % entry)
            self.assertEqual(gen_albedo_table.load(path), {"0x00000000000000AA": _albedo()})

    def test_the_committed_table_uses_the_allowed_keys_and_generates_its_inc(self):
        # The per-form lists are the committed file's: every key of every entry is in its form's list
        # and every listed key but "kind" on the forms it only selects is used by some entry (a list
        # wider than the file would hide a typing mistake).
        root = Path(__file__).resolve().parent.parent
        data = gen_albedo_table.load(root / "docs" / "native-renderer" / "ps-albedo.json")
        used = {form: set() for form in gen_albedo_table.ENTRY_KEYS}
        alpha_used = set()
        for e in data.values():
            form = "terrain" if e.get("kind", "plain") != "plain" else "no_albedo" if "no_albedo" in e else "albedo"
            used[form] |= set(e)
            alpha_used |= set(e.get("alpha", {}))
        self.assertEqual({f: sorted(k) for f, k in used.items()},
                         {"albedo": sorted(["slot", "u", "v", "alpha", "alpha_manual", "alpha_evidence", "manual",
                                            "evidence"]),
                          "no_albedo": sorted(["no_albedo", "manual", "evidence"]),
                          "terrain": sorted(["kind", "base_slot", "detail_slot", "steep", "manual", "evidence"])})
        for form, keys in used.items():
            self.assertEqual(keys | {"kind"}, set(gen_albedo_table.ENTRY_KEYS[form]), form)
        # ("slot" is the fetch source's: no committed recipe uses that source)
        self.assertEqual(alpha_used | {"slot"}, set(gen_albedo_table.ALPHA_KEYS))
        # byte for byte (text mode on both sides: main() writes the platform's line ends)
        inc = root / "src" / "native" / "capture" / "ps_albedo_table.inc"
        self.assertEqual(inc.read_text(), gen_albedo_table.generate(data))


if __name__ == "__main__":
    unittest.main()
