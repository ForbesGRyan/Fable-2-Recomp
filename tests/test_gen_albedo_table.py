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


def _albedo(**extra):
    e = {"slot": 3, "manual": True, "evidence": "e",
         "u": {"input": "r0.x", "stages": []}, "v": {"input": "r0.y", "stages": []}}
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
        data = {"0x3": _terrain(), "0x2": _albedo(alpha={"source": "one"}), "0x1": _albedo(), "0x4": _terrain()}
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


if __name__ == "__main__":
    unittest.main()
