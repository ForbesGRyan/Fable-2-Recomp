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


class GenAlphaKindTest(unittest.TestCase):
    H = "0x00000000000000AA"

    def _gen(self, entry):
        return gen_albedo_table.generate({self.H: entry}).splitlines()

    def test_no_alpha_or_kind_adds_nothing(self):
        self.assertEqual(len(self._gen(_albedo())), 2)

    def test_alpha_forms(self):
        s = (1 << 10)  # unused; scale_ref is register * 4 + component, no bank bit
        del s
        out = self._gen(_albedo(alpha={"source": "one"}))
        self.assertEqual(out[2], "FABLE2_PS_ALPHA(0x00000000000000AAull, 0, 0, 3, -1)")
        out = self._gen(_albedo(alpha={"source": "albedo", "component": "w", "scale": "c15.w"}))
        self.assertEqual(out[2], "FABLE2_PS_ALPHA(0x00000000000000AAull, 1, 3, 3, 63)")
        out = self._gen(_albedo(alpha={"source": "albedo", "component": "x"}))
        self.assertEqual(out[2], "FABLE2_PS_ALPHA(0x00000000000000AAull, 1, 3, 0, -1)")
        out = self._gen(_albedo(alpha={"source": "fetch", "slot": 8, "component": "y", "scale": "c2.z"}))
        self.assertEqual(out[2], "FABLE2_PS_ALPHA(0x00000000000000AAull, 2, 8, 1, 10)")

    def test_kind(self):
        self.assertEqual(len(self._gen(_albedo(kind="plain"))), 2)
        self.assertEqual(self._gen(_albedo(kind="terrain-layer"))[2], "FABLE2_PS_KIND(0x00000000000000AAull, 1)")
        self.assertEqual(self._gen(_albedo(kind="terrain-far"))[2], "FABLE2_PS_KIND(0x00000000000000AAull, 2)")

    def test_order_alpha_then_kind_after_albedo_lines(self):
        data = {"0x2": _albedo(alpha={"source": "one"}, kind="terrain-far"), "0x1": _albedo()}
        out = gen_albedo_table.generate(data).splitlines()
        self.assertEqual([l.split("(")[0] for l in out[1:]],
                         ["FABLE2_PS_ALBEDO", "FABLE2_PS_ALBEDO", "FABLE2_PS_ALPHA", "FABLE2_PS_KIND"])

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
            _albedo(kind="water"),
            {"no_albedo": "fog", "manual": True, "evidence": "e", "alpha": {"source": "one"}},
        ]
        for entry in bad:
            with self.assertRaises(ValueError, msg=repr(entry)) as cm:
                self._gen(entry)
            self.assertIn(self.H, str(cm.exception))


if __name__ == "__main__":
    unittest.main()
