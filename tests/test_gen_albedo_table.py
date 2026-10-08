import json
import sys
import tempfile
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


# A vertex table (vs-transforms.json) as far as the albedo generator reads it: a vertex shader of
# each light kind, one without a "light", one whose "light" names no kind the generator knows.
VS, PROBE, INSTANCE, TERRAIN = ("0xECD66A10092E6562", "0x79EAC49585797037", "0x8123C16DBF583F92",
                                "0xC30A97D946FA2BE4")
NO_LIGHT, ODD_KIND = "0x5F4416192E87005F", "0x00000000000000DD"
VERTEX = {VS: {"layout": "plain", "light": {"kind": "stream"}}, PROBE: {"light": {"kind": "probe"}},
          INSTANCE: {"light": {"kind": "instance"}}, TERRAIN: {"light": {"kind": "terrain"}},
          NO_LIGHT: {"layout": "plain"}, ODD_KIND: {"light": {"kind": "leaf"}}}
# The vertex shader kind each family is checked with here (gen_albedo_table._LIGHT_VERTEX_KINDS).
FITS = {"terrain-layer": TERRAIN, "terrain-far": TERRAIN, "object": VS, "tree-trunk": PROBE, "plant": INSTANCE,
        "plant-lit": INSTANCE}


class GenAlphaKindTest(unittest.TestCase):
    H = "0x00000000000000AA"

    def _gen(self, entry, vertex=VERTEX):
        return gen_albedo_table.generate({self.H: entry}, vertex).splitlines()

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
        vertex = json.loads((root / "docs" / "native-renderer" / "vs-transforms.json").read_text())
        lines = gen_albedo_table.generate(data, vertex).splitlines()
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

    # --- "light": the light family of the shader's (pixel, vertex) pairs ---

    VS = VS

    def _light(self, **extra):
        """A light object; its vertex shader, unless given, is one of the kind the family stands on."""
        light = {"family": "object", "evidence": "light_check.py"}
        light.update(extra)
        family = light["family"]
        base = family[:-len("-multi")] if isinstance(family, str) and family.endswith("-multi") else family
        light.setdefault("vertex", [FITS.get(base, VS) if isinstance(base, str) else VS])
        return light

    def test_light_lines(self):
        # One FABLE2_PS_LIGHT line per shader: the family's code (1 terrain-layer, 2 terrain-far, 3 object,
        # 4 tree-trunk, 5 plant, 6 plant-lit), whether it is the family's two- or four-light form
        # ("-multi"), and the literal of the terrain layers' facing factor (0.0f where the family has
        # none); then one FABLE2_PS_LIGHT_VS line per vertex shader the pair was checked with. After the
        # terrain lines.
        p = "FABLE2_PS_LIGHT(0x00000000000000AAull, "
        v = "FABLE2_PS_LIGHT_VS(0x00000000000000AAull, "
        out = self._gen(_albedo(light=self._light()))
        self.assertEqual(out[2:], [p + "3, 0, 0.0f)", v + "0xECD66A10092E6562ull)"])
        two = ["0x79EAC49585797037", "0xecd66a10092e6562"]
        out = self._gen(_albedo(light=self._light(family="object-multi", vertex=two)))
        self.assertEqual(out[2:], [p + "3, 1, 0.0f)", v + "0x79EAC49585797037ull)", v + "0xECD66A10092E6562ull)"])
        for family, code, multi in (("tree-trunk", 4, 0), ("tree-trunk-multi", 4, 1), ("plant", 5, 0),
                                    ("plant-multi", 5, 1), ("plant-lit", 6, 0), ("plant-lit-multi", 6, 1)):
            self.assertEqual(self._gen(_albedo(light=self._light(family=family)))[2], p + f"{code}, {multi}, 0.0f)")
        # a recipe's alpha line comes before it
        out = self._gen(_albedo(alpha={"source": "one"}, light=self._light()))
        self.assertEqual([l.split("(")[0] for l in out[1:]],
                         ["FABLE2_PS_ALBEDO", "FABLE2_PS_ALPHA", "FABLE2_PS_LIGHT", "FABLE2_PS_LIGHT_VS"])
        # terrain entries: the layers carry the facing factor's literal
        layer = self._light(family="terrain-layer", facing_scale=8)
        out = self._gen(_terrain(light=layer))
        self.assertEqual(out[2:], [p + "1, 0, 8.0f)", v + "0xC30A97D946FA2BE4ull)"])
        out = self._gen(_terrain(light=dict(layer, family="terrain-layer-multi", facing_scale=7.5)))
        self.assertEqual(out[2], p + "1, 1, 7.5f)")
        far = self._light(family="terrain-far")
        self.assertEqual(self._gen(_terrain(kind="terrain-far", detail_slot=13, light=far))[2], p + "2, 0, 0.0f)")
        self.assertEqual(self._gen(_terrain(kind="terrain-far-fade", detail_slot=13, light=far))[2], p + "2, 0, 0.0f)")

    def test_light_lines_follow_every_other_line(self):
        data = {"0x0000000000000003": _terrain(light=self._light(family="terrain-layer", facing_scale=8)),
                "0x0000000000000002": _albedo(alpha={"source": "one"}, light=self._light()),
                "0x0000000000000001": _albedo()}
        out = gen_albedo_table.generate(data, VERTEX).splitlines()
        self.assertEqual([l.split("(")[0] for l in out[1:]],
                         ["FABLE2_PS_ALBEDO", "FABLE2_PS_ALBEDO", "FABLE2_PS_ALPHA", "FABLE2_PS_TERRAIN",
                          "FABLE2_PS_LIGHT", "FABLE2_PS_LIGHT_VS", "FABLE2_PS_LIGHT", "FABLE2_PS_LIGHT_VS"])
        self.assertIn("FABLE2_PS_LIGHT(0x0000000000000002ull, 3, 0, 0.0f)", out)
        self.assertIn("FABLE2_PS_LIGHT(0x0000000000000003ull, 1, 0, 8.0f)", out)

    def test_light_is_refused_rather_than_read_loosely(self):
        fog = {"no_albedo": "fog", "manual": True, "evidence": "e"}
        layer = self._light(family="terrain-layer", facing_scale=8)
        far = self._light(family="terrain-far")
        bad = [
            # the entry's kind cannot have the family
            (_albedo(light=layer), "light family terrain-layer belongs to a terrain-layer entry"),
            (_albedo(light=far), "light family terrain-far belongs to a terrain-far"),
            (_terrain(light=self._light()), "light family object belongs to a plain entry"),
            (_terrain(light=self._light(family="plant-lit-multi")), "light family plant-lit-multi belongs to a plain"),
            (_terrain(light=far), "light family terrain-far belongs to a terrain-far"),
            (_terrain(kind="terrain-far", detail_slot=13, light=layer), "light family terrain-layer belongs to"),
            (_terrain(kind="terrain-far-fade", detail_slot=13, light=dict(layer, family="terrain-layer-multi")),
             "light family terrain-layer-multi belongs to"),
            (dict(fog, light=self._light()), "unknown key light in a no_albedo entry"),
            # the family
            (_albedo(light=self._light(family="tree-leaf")), "unknown light family 'tree-leaf'"),
            (_terrain(kind="terrain-far", detail_slot=13, light=dict(far, family="terrain-far-multi")),
             "unknown light family 'terrain-far-multi'"),   # the far shaders have one light
            (_albedo(light=self._light(family="object-multi-multi")), "unknown light family 'object-multi-multi'"),
            (_albedo(light=self._light(family="-multi")), "unknown light family '-multi'"),
            (_albedo(light=self._light(family="tree")), "unknown light family 'tree'"),
            (_albedo(light=self._light(family="Object")), "unknown light family 'Object'"),
            (_albedo(light=self._light(family=3)), "unknown light family 3"),
            (_albedo(light={"vertex": [self.VS], "evidence": "e"}), "unknown light family None"),
            # a family whose vertex shader cannot serve it (the vertex table's "light" kind): the layers
            # and the far shader stand on a terrain vertex shader, objects on the colour stream's or a
            # probe one, trunks on a probe one, plants on an instancing one
            (_terrain(light=dict(layer, vertex=[VS])),
             "light family terrain-layer needs a vertex shader of kind terrain, and 0xECD66A10092E6562 is of kind stream"),
            (_terrain(kind="terrain-far", detail_slot=13, light=dict(far, vertex=[INSTANCE])),
             "light family terrain-far needs a vertex shader of kind terrain, and 0x8123C16DBF583F92 is of kind instance"),
            (_albedo(light=self._light(vertex=[INSTANCE])),
             "light family object needs a vertex shader of kind stream or probe, and 0x8123C16DBF583F92 is of kind instance"),
            (_albedo(light=self._light(vertex=[TERRAIN])), "needs a vertex shader of kind stream or probe, and 0xC30A97D946FA2BE4 is of kind terrain"),
            (_albedo(light=self._light(family="object-multi", vertex=[VS, PROBE, TERRAIN])), "light family object-multi needs"),
            (_albedo(light=self._light(family="tree-trunk", vertex=[VS])),
             "light family tree-trunk needs a vertex shader of kind probe, and 0xECD66A10092E6562 is of kind stream"),
            (_albedo(light=self._light(family="tree-trunk-multi", vertex=[INSTANCE])), "needs a vertex shader of kind probe"),
            (_albedo(light=self._light(family="plant", vertex=[PROBE])),
             "light family plant needs a vertex shader of kind instance, and 0x79EAC49585797037 is of kind probe"),
            (_albedo(light=self._light(family="plant-lit", vertex=[VS])), "light family plant-lit needs a vertex shader of kind instance"),
            (_albedo(light=self._light(family="plant-lit-multi", vertex=[INSTANCE, TERRAIN])),
             "light family plant-lit-multi needs a vertex shader of kind instance, and 0xC30A97D946FA2BE4 is of kind terrain"),
            # a vertex shader the vertex table has no light for: an entry without "light", no entry
            (_albedo(light=self._light(vertex=[NO_LIGHT])), "light vertex 0x5F4416192E87005F has no light in the vertex table"),
            (_albedo(light=self._light(vertex=[VS, NO_LIGHT])), "light vertex 0x5F4416192E87005F has no light in the vertex table"),
            (_albedo(light=self._light(vertex=["0x0000000000000001"])), "light vertex 0x0000000000000001 has no light in the vertex table"),
            # or a light of a kind this generator does not know (the two generators disagree)
            (_albedo(light=self._light(vertex=[ODD_KIND])), "needs a vertex shader of kind stream or probe, and 0x00000000000000DD is of kind 'leaf'"),
            # not an object; keys it does not have
            (_albedo(light="object"), "light is not an object"),
            (_albedo(light=["object"]), "light is not an object"),
            (_albedo(light=None), "light is not an object"),
            (_albedo(light=self._light(familly="object")), "unknown light key familly"),
            (_albedo(light=self._light(multi=True)), "unknown light key multi"),
            (_albedo(light=self._light(manual=True)), "unknown light key manual"),
            # its evidence
            (_albedo(light={"family": "object", "vertex": [self.VS]}), "light without evidence"),
            (_albedo(light=self._light(evidence="")), "light without evidence"),
            (_albedo(light=self._light(evidence="  ")), "light without evidence"),
            (_albedo(light=self._light(evidence=["seen"])), "light without evidence"),
            # the vertex shaders the family was checked with
            (_albedo(light={"family": "object", "evidence": "e"}), "light vertex is not a list of vertex shaders"),
            (_albedo(light=self._light(vertex=[])), "light vertex is not a list of vertex shaders"),
            (_albedo(light=self._light(vertex=self.VS)), "light vertex is not a list of vertex shaders"),
            (_albedo(light=self._light(vertex=["ECD66A10092E6562"])), "light vertex 'ECD66A10092E6562' is not 0x and"),
            (_albedo(light=self._light(vertex=["0xECD6"])), "light vertex '0xECD6' is not 0x and"),
            (_albedo(light=self._light(vertex=[0xECD66A10092E6562])), "is not 0x and 16 hex digits"),
            (_albedo(light=self._light(vertex=[self.VS, self.VS])), "light vertex 0xECD66A10092E6562 twice"),
            (_albedo(light=self._light(vertex=[self.VS, self.VS.lower()])), "twice"),
            # the facing factor's literal: the terrain layers', a positive finite JSON number
            (_terrain(light={k: v for k, v in layer.items() if k != "facing_scale"}), "needs facing_scale"),
            (_terrain(light=dict(layer, facing_scale=0)), "facing_scale 0 is not a number above 0"),
            (_terrain(light=dict(layer, facing_scale=-8)), "facing_scale -8 is not a number above 0"),
            (_terrain(light=dict(layer, facing_scale="8")), "facing_scale '8' is not a number above 0"),
            (_terrain(light=dict(layer, facing_scale=True)), "facing_scale True is not a number above 0"),
            (_terrain(light=dict(layer, facing_scale=float("inf"))), "facing_scale inf is not a number above 0"),
            (_terrain(light=dict(layer, facing_scale=float("nan"))), "facing_scale nan is not a number above 0"),
            (_albedo(light=self._light(facing_scale=8)), "facing_scale belongs to a terrain-layer family"),
            (_terrain(kind="terrain-far", detail_slot=13, light=dict(far, facing_scale=8)),
             "facing_scale belongs to a terrain-layer family"),
        ]
        for entry, why in bad:
            with self.assertRaises(ValueError, msg=repr(entry)) as cm:
                self._gen(entry)
            self.assertTrue(str(cm.exception).startswith(self.H + ": "), str(cm.exception))
            self.assertIn(why, str(cm.exception), repr(entry))

    def test_light_needs_the_vertex_table(self):
        # A table with a "light" is not generated without the vertex table: the pair's check cannot
        # be left out by leaving the table out. A table without any generates as before.
        with self.assertRaisesRegex(ValueError, "0x00000000000000AA: light needs the vertex table"):
            gen_albedo_table.generate({self.H: _albedo(light=self._light())})
        with self.assertRaisesRegex(ValueError, "0x00000000000000AA: light needs the vertex table"):
            gen_albedo_table.generate({self.H: _terrain(light=self._light(family="terrain-layer", facing_scale=8))}, None)
        self.assertEqual(gen_albedo_table.generate({self.H: _albedo()}), gen_albedo_table.generate({self.H: _albedo()}, VERTEX))
        # The vertex table's keys are shaders by value, whatever the letters' case, as the entry's are.
        p = "FABLE2_PS_LIGHT_VS(0x00000000000000AAull, 0xECD66A10092E6562ull)"
        lower = {VS.lower(): VERTEX[VS]}
        self.assertIn(p, self._gen(_albedo(light=self._light()), lower))
        self.assertIn(p, self._gen(_albedo(light=self._light(vertex=[VS.lower()]))))
        # A vertex table that is not one: refused, never read as "no light anywhere".
        for vertex, why in (([], "the vertex table is not an object"), ("x", "the vertex table is not an object"),
                            ({"ECD6": {}}, "the vertex table's key 'ECD6' is not 0x and 16 hex digits"),
                            ({VS: {"light": {"kind": "stream"}}, VS.lower(): {}}, "the vertex table names 0x"),
                            ({VS: {"light": "stream"}}, "light vertex 0xECD66A10092E6562 has no light in the vertex table"),
                            ({VS: ["light"]}, "light vertex 0xECD66A10092E6562 has no light in the vertex table"),
                            ({VS: {"light": {"kind": ["stream"]}}}, "is of kind ['stream']"),
                            ({VS: {"light": {}}}, "is of kind None")):
            with self.assertRaises(ValueError, msg=repr(vertex)) as cm:
                self._gen(_albedo(light=self._light()), vertex)
            self.assertIn(why, str(cm.exception), repr(vertex))

    def test_main_reads_the_vertex_table_beside_the_albedo_table(self):
        # main() checks against vs-transforms.json in the folder of --json, or the file --vs-json
        # names; a pair that fails writes nothing.
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "ps.json").write_text(json.dumps({self.H: _albedo(light=self._light())}))
            out = tmp / "t.inc"
            with self.assertRaises(FileNotFoundError):          # no vertex table there
                gen_albedo_table.main(["--json", str(tmp / "ps.json"), "--out", str(out)])
            self.assertFalse(out.exists())
            (tmp / "vs-transforms.json").write_text(json.dumps(VERTEX))
            gen_albedo_table.main(["--json", str(tmp / "ps.json"), "--out", str(out)])
            self.assertIn("FABLE2_PS_LIGHT_VS(0x00000000000000AAull, 0xECD66A10092E6562ull)", out.read_text())
            out.unlink()
            (tmp / "other.json").write_text(json.dumps({VS: {"light": {"kind": "instance"}}}))
            with self.assertRaisesRegex(ValueError, "0x00000000000000AA: light family object needs a vertex shader"):
                gen_albedo_table.main(["--json", str(tmp / "ps.json"), "--vs-json", str(tmp / "other.json"), "--out", str(out)])
            self.assertFalse(out.exists())
            (tmp / "other.json").write_text(json.dumps({VS: {"layout": "plain"}}))
            with self.assertRaisesRegex(ValueError, "has no light in the vertex table"):
                gen_albedo_table.main(["--json", str(tmp / "ps.json"), "--vs-json", str(tmp / "other.json"), "--out", str(out)])
            self.assertFalse(out.exists())
            # a vertex table with a key twice is refused as the albedo table is
            (tmp / "other.json").write_text('{"%s": {"light": {"kind": "stream"}}, "%s": {}}' % (VS, VS))
            with self.assertRaisesRegex(ValueError, "twice"):
                gen_albedo_table.main(["--json", str(tmp / "ps.json"), "--vs-json", str(tmp / "other.json"), "--out", str(out)])
            self.assertFalse(out.exists())

    def test_the_committed_tables_fail_together_when_one_loses_its_side(self):
        # The two committed files as generation holds them against each other: every pair's vertex
        # shader has its "light" and is of a kind the family stands on; take one vertex shader's
        # "light" away, or make it another kind, and the albedo table is not generated.
        root = Path(__file__).resolve().parent.parent
        data = gen_albedo_table.load(root / "docs" / "native-renderer" / "ps-albedo.json")
        vertex = json.loads((root / "docs" / "native-renderer" / "vs-transforms.json").read_text())
        gen_albedo_table.generate(data, vertex)
        named = sorted({vs.upper() for e in data.values() if "light" in e for vs in e["light"]["vertex"]})
        self.assertEqual(len(named), 15)
        other = {"terrain": "instance", "stream": "instance", "probe": "terrain", "instance": "probe"}
        for vs in named:
            key = next(k for k in vertex if k.upper() == vs)
            without = {k: ({f: v for f, v in e.items() if f != "light"} if k == key else e) for k, e in vertex.items()}
            with self.assertRaisesRegex(ValueError, "has no light in the vertex table", msg=vs):
                gen_albedo_table.generate(data, without)
            gone = {k: e for k, e in vertex.items() if k != key}
            with self.assertRaisesRegex(ValueError, "has no light in the vertex table", msg=vs):
                gen_albedo_table.generate(data, gone)
            kind = vertex[key]["light"]["kind"]
            wrong = {k: (dict(e, light=dict(e["light"], kind=other[kind])) if k == key else e) for k, e in vertex.items()}
            with self.assertRaisesRegex(ValueError, "needs a vertex shader of kind", msg=vs):
                gen_albedo_table.generate(data, wrong)

    def test_generation_does_not_tell_a_family_from_its_multi_form(self):
        # What generation cannot check (the tool's docstring says so): whether a pair's family is the
        # one-light form or the "-multi" one. 0x648B... written "object" for "object-multi" generates,
        # with the same vertex shaders, and would light a lantern's draws as the sun's; it is the test
        # below, against light_check.FAMILIES, that holds the committed table to the checked label.
        root = Path(__file__).resolve().parent.parent
        data = gen_albedo_table.load(root / "docs" / "native-renderer" / "ps-albedo.json")
        vertex = json.loads((root / "docs" / "native-renderer" / "vs-transforms.json").read_text())
        ps = "0x648B965C200A5113"
        self.assertEqual(data[ps]["light"]["family"], "object-multi")
        data[ps] = dict(data[ps], light=dict(data[ps]["light"], family="object"))
        out = gen_albedo_table.generate(data, vertex)
        self.assertIn("FABLE2_PS_LIGHT(0x648B965C200A5113ull, 3, 0, 0.0f)", out)
        import light_check
        self.assertNotIn((ps[2:], "ECD66A10092E6562"), light_check.FAMILIES["object"])
        self.assertIn((ps[2:], "ECD66A10092E6562"), light_check.FAMILIES["object-multi"])

    def test_the_committed_light_pairs_are_the_checked_pairs(self):
        # The table's (family, pixel shader, vertex shader) triples are exactly the pairs
        # light_check.py runs (FAMILIES), all but the tree leaves, which stay without a family:
        # their formula is not closed (docs/native-renderer/lighting-formulas.md, "Tree leaves").
        import light_check
        root = Path(__file__).resolve().parent.parent
        data = gen_albedo_table.load(root / "docs" / "native-renderer" / "ps-albedo.json")
        table = {(e["light"]["family"], ps[2:].upper(), vs[2:].upper())
                 for ps, e in data.items() if "light" in e for vs in e["light"]["vertex"]}
        checked = {(family, ps, vs) for family, pairs in light_check.FAMILIES.items() if family != "tree-leaf"
                   for ps, vs in pairs}
        self.assertEqual(table, checked)
        self.assertEqual(len(table), 36)
        self.assertNotIn("light", data["0x7CD57B81550F19E3"])
        # every terrain layer's facing literal is the 8 of its own table (frame-map section 14, "The
        # shaders' literals"), and each family's evidence names the checker
        for e in data.values():
            if "light" in e:
                self.assertEqual(e["light"].get("facing_scale"), 8 if e["light"]["family"].startswith("terrain-layer") else None)
                self.assertIn("light_check", e["light"]["evidence"])
        # every vertex shader named has the vertex side's entry (vs-transforms.json "light")
        vs_table = json.loads((root / "docs" / "native-renderer" / "vs-transforms.json").read_text())
        for _, _, vs in table:
            self.assertIn("light", vs_table["0x" + vs], vs)

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
                                            "evidence", "light"]),
                          "no_albedo": sorted(["no_albedo", "manual", "evidence"]),
                          "terrain": sorted(["kind", "base_slot", "detail_slot", "steep", "manual", "evidence",
                                             "light"])})
        for form, keys in used.items():
            self.assertEqual(keys | {"kind"}, set(gen_albedo_table.ENTRY_KEYS[form]), form)
        # ("slot" is the fetch source's: no committed recipe uses that source)
        self.assertEqual(alpha_used | {"slot"}, set(gen_albedo_table.ALPHA_KEYS))
        self.assertEqual({k for e in data.values() for k in e.get("light", {})}, set(gen_albedo_table.LIGHT_KEYS))
        # byte for byte (text mode on both sides: main() writes the platform's line ends), held
        # against the committed vertex table as main() holds it
        inc = root / "src" / "native" / "capture" / "ps_albedo_table.inc"
        vertex = gen_albedo_table.load(root / "docs" / "native-renderer" / "vs-transforms.json")
        self.assertEqual(inc.read_text(), gen_albedo_table.generate(data, vertex))


if __name__ == "__main__":
    unittest.main()
