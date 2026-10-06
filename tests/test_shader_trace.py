import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import shader_trace as st  # noqa: E402

PS = """/*    0.0 */       exec
/*   10   */          mad r10._yz_, r0.yyxx, c10.yyxx, c10.wwzz
              +       retain_prev r8._
/*   11   */          mad r0.xy__, r0.yxxx, c8.yxxx, c8.wzzz
              +       retain_prev r8._
/*   12   */          mad r0.__zw, r_abs[9].xxxy, c0.zzzw, c0.xxxy
              +       retain_prev r8._
/*   13   */          tfetch2D r10.x___, r0.zw, tf8
/*   14   */          tfetch2D r0.wxyz, r0.yx, tf0
/*   15   */          tfetch2D r9.xy__, r10.zy, tf2
/*    0.1 */       alloc colors
"""

VS = """/*    0.0 */       exec
/*    5   */          vfetch_full r5.yxw1, r0.x, vf0, DataFormat=FMT_16_16_16_16_FLOAT, Stride=5, Signed=true, NumFormat=integer, PrefetchCount=5
/*    6   */          vfetch_mini r7.xyz_, Offset=2, DataFormat=FMT_10_11_11, Signed=true
/*    7   */          vfetch_mini r6.yx__, Offset=3, DataFormat=FMT_16_16_FLOAT, Signed=true, NumFormat=integer
/*    1.0 */       exec
/*   10   */          dp4 oPos.x___, c0.zxyw, r5.zxyw
/*   16   */          mul r4.xyz_, c19.xyzz, c7.wwww
/*   22   */          dp3 r2.__z_, r7.zxyy, c6.zxyy
              +       log r3._y__, r_abs[3].y
/*   23   */          max o0.xy__, r6.xyyy, r6.xyyy
/*   24   */          dp4 o2.x___, c4.zxyw, r5.zxyw
/*   27   */          mul o1.xyz_, r2.xyzz, c7.wwww
"""


class ParseTest(unittest.TestCase):
    def test_texture_fetches(self):
        tf = st.texture_fetches(st.parse(PS))
        self.assertEqual([(t.slot, t.coord_reg, t.coord_swz, t.mask) for t in tf],
                         [(8, 0, "zw", "x"), (0, 0, "yx", "xyzw"), (2, 10, "zy", "xy")])
        self.assertEqual(tf[0].dim, "2D")

    def test_vfetches(self):
        vf = st.vfetches(st.parse(VS))
        self.assertEqual([(v.ordinal, v.dest, v.dest_swizzle, v.fmt, v.offset) for v in vf],
                         [(0, 5, "yxw1", 32, 0), (1, 7, "xyz_", None, 2), (2, 6, "yx__", 31, 3)])


class TraceTest(unittest.TestCase):
    def setUp(self):
        self.ps = st.parse(PS)
        self.tf = st.texture_fetches(self.ps)

    def test_albedo_affine(self):
        # tf0 at r0.yx: r0.x = r0.y*c8.y + c8.w ; r0.y = r0.x*c8.x + c8.z (instr 11)
        t = st.trace_ps_albedo(self.ps, self.tf[1])
        self.assertEqual(t["u"], {"input": "r0.x", "stages": [{"scale": "c8.x", "offset": "c8.z"}]})
        self.assertEqual(t["v"], {"input": "r0.y", "stages": [{"scale": "c8.y", "offset": "c8.w"}]})

    def test_abs_unsupported(self):
        t = st.trace_ps_albedo(self.ps, self.tf[0])  # r0.zw from r_abs[9]
        self.assertIsInstance(t, st.Unsupported)
        self.assertIn("abs", t.reason)

    def test_vs_export(self):
        vs = st.parse(VS)
        # o0.x = r6.x = source y of fetch 2 ; o0.y = r6.y = source x.
        self.assertEqual(st.trace_vs_export(vs, 0, 0),
                         {"fetch": 2, "src": "y", "format": 31, "offset": 3, "stages": []})
        self.assertEqual(st.trace_vs_export(vs, 0, 1)["src"], "x")
        # o2.x comes from a dp4: unsupported.
        self.assertIsInstance(st.trace_vs_export(vs, 2, 0), st.Unsupported)
        # Not exported at all.
        self.assertIsInstance(st.trace_vs_export(vs, 9, 0), st.Unsupported)

    def test_mul_and_add_stages(self):
        text = """/*    0.0 */       exec
/*    1   */          mul r1.xy__, r0.xyyy, c3.xyyy
/*    2   */          add r1.xy__, r1.xyyy, -c4.zwww
/*    3   */          tfetch2D r2, r1.xy, tf1
"""
        ins = st.parse(text)
        t = st.trace_ps_albedo(ins, st.texture_fetches(ins)[0])
        self.assertEqual(t["u"], {"input": "r0.x", "stages": [{"scale": "c3.x", "offset": None},
                                                              {"scale": None, "offset": "-c4.z"}]})

    def test_vs_export_in_conditional_region_unsupported(self):
        # The fetch and the export share one cexec region: the walk meets no control-flow line
        # between them, but the export is conditional.
        text = """/*    0.0 */       cexec b135
/*    5   */          vfetch_mini r6.yx__, Offset=3, DataFormat=FMT_16_16_FLOAT, Signed=true
/*   23   */          max o0.xy__, r6.xyyy, r6.xyyy
/*    1.0 */       alloc interpolators
"""
        r = st.trace_vs_export(st.parse(text), 0, 0)
        self.assertIsInstance(r, st.Unsupported)
        self.assertIn("cexec", r.reason)
        # A conditional region anywhere before the export (here before the fetch, outside the
        # backward walk) also rejects it.
        text = """/*    0.0 */       cexec b134
/*    2   */          mul r4.xy__, r4.xyyy, c3.xyyy
/*    1.0 */       exec
/*    5   */          vfetch_mini r6.yx__, Offset=3, DataFormat=FMT_16_16_FLOAT, Signed=true
/*   23   */          max o0.xy__, r6.xyyy, r6.xyyy
"""
        self.assertIsInstance(st.trace_vs_export(st.parse(text), 0, 0), st.Unsupported)

    def test_vs_export_two_writers_unsupported(self):
        text = """/*    0.0 */       exec
/*    5   */          vfetch_mini r6.yx__, Offset=3, DataFormat=FMT_16_16_FLOAT, Signed=true
/*   23   */          max o0.xy__, r6.xyyy, r6.xyyy
/*   24   */          mov o0.x___, r6.yyyy
"""
        ins = st.parse(text)
        r = st.trace_vs_export(ins, 0, 0)  # o0.x written twice
        self.assertIsInstance(r, st.Unsupported)
        self.assertIn("writ", r.reason)
        self.assertEqual(st.trace_vs_export(ins, 0, 1)["src"], "x")  # o0.y has one writer

    def test_fetch_coordinate_attributes_unsupported(self):
        for attrs in ("UnnormalizedTextureCoords=true", "OffsetX=0.5", "OffsetY=-1"):
            text = ("/*    0.0 */       exec\n/*    1   */          mul r1.xy__, r0.xyyy, c3.xyyy\n"
                    f"/*    3   */          tfetch2D r2, r1.xy, tf1, {attrs}\n")
            ins = st.parse(text)
            r = st.trace_ps_albedo(ins, st.texture_fetches(ins)[0])
            self.assertIsInstance(r, st.Unsupported, attrs)
        # Filter and validity overrides do not move the coordinate.
        text = ("/*    0.0 */       exec\n/*    1   */          mul r1.xy__, r0.xyyy, c3.xyyy\n"
                "/*    3   */          tfetch2D r2, r1.xy, tf1, FetchValidOnly=false, MagFilter=linear\n")
        ins = st.parse(text)
        self.assertNotIsInstance(st.trace_ps_albedo(ins, st.texture_fetches(ins)[0]), st.Unsupported)

    def test_predicated_and_sat_unsupported(self):
        for line in ("mul_sat r1.xy__, r0.xyyy, c3.xyyy", "(p0) mul r1.xy__, r0.xyyy, c3.xyyy"):
            text = f"/*    0.0 */       exec\n/*    1   */          {line}\n/*    3   */          tfetch2D r2, r1.xy, tf1\n"
            ins = st.parse(text)
            self.assertIsInstance(st.trace_ps_albedo(ins, st.texture_fetches(ins)[0]), st.Unsupported, line)


HEAD = "/*    0.0 */       exec\n"


def _alpha(body):
    return st.trace_ps_alpha(st.parse(HEAD + body))


class AlphaTraceTest(unittest.TestCase):
    def test_literal_one(self):
        r = _alpha("/*    5   */          mul oC0.xyz1, r0.xzww, c20.wwww\n")
        self.assertEqual(r, {"source": "one", "slot": None, "component": None, "scale": None})

    def test_fetch_component_unscaled(self):
        r = _alpha("/*    1   */          tfetch2D r0.___x, r6.zw, tf8\n"
                   "/*    5   */          max oC0.___w, r0.wwww, r0.wwww\n")
        self.assertEqual(r, {"source": "fetch", "slot": 8, "component": "x", "scale": None})

    def test_fetch_component_scaled_vector_mul(self):
        r = _alpha("/*    1   */          tfetch2D r2.wxyz, r6.yx, tf0\n"
                   "/*    5   */          mul oC0.___w, r2.xxxx, c15.wwww\n")
        self.assertEqual(r, {"source": "fetch", "slot": 0, "component": "w", "scale": "c15.w"})

    def test_fetch_component_scaled_scalar_mul(self):
        r = _alpha("/*    1   */          tfetch2D r0.x___, r6.zw, tf8\n"
                   "/*    5   */          mul oC0.xyz_, r1.xyzz, r2.xyzz\n"
                   "              +       mulsc oC0.___w, c15.w, r0.x\n")
        self.assertEqual(r, {"source": "fetch", "slot": 8, "component": "x", "scale": "c15.w"})

    def test_join_label_before_unconditional_write_is_fine(self):
        r = _alpha("/*    1.0 */       cjmp !b3, L9\n/*    2   */          mul r1.x___, r1.xxxx, r1.xxxx\n"
                   "                label L9\n/*    5   */          mul oC0.xyz1, r0.xzww, c20.wwww\n")
        self.assertEqual(r["source"], "one")

    def test_refused_shapes(self):
        fetch = "/*    1   */          tfetch2D r0.___x, r6.zw, tf8\n"
        cases = {
            "no writer": "/*    5   */          mul oC0.xyz_, r0.xzww, c20.wwww\n",
            "predicated": "/*    4   */          setp_eq r1._, r1.x\n/*    5   */    (p0) mul oC0.xyz1, r0.xzww, c20.wwww\n",
            "two writers": "/*    5   */          mul oC0.xyz1, r0.xzww, c20.wwww\n/*    6   */          mul oC0.___w, r0.wwww, c1.xxxx\n",
            "literal zero": "/*    5   */          mul oC0.xyz0, r0.xzww, c20.wwww\n",
            "sat": fetch + "/*    5   */          mul_sat oC0.___w, r0.wwww, c1.xxxx\n",
            "add": fetch + "/*    5   */          add oC0.___w, r0.wwww, c1.xxxx\n",
            "two temps": fetch + "/*    5   */          mul oC0.___w, r0.wwww, r1.wwww\n",
            "negated const": fetch + "/*    5   */          mul oC0.___w, r0.wwww, -c1.xxxx\n",
            "abs temp": fetch + "/*    5   */          mul oC0.___w, r_abs[0].wwww, c1.xxxx\n",
            "1D fetch": "/*    1   */          tfetch1D r0.___x, r6.x, tf8\n/*    5   */          mul oC0.___w, r0.wwww, c1.xxxx\n",
            "cube fetch": "/*    1   */          tfetchCube r0.___x, r6.xyz, tf8\n/*    5   */          mul oC0.___w, r0.wwww, c1.xxxx\n",
            "predicated fetch": "/*    1   */    (p0) tfetch2D r0.___x, r6.zw, tf8\n/*    5   */          mul oC0.___w, r0.wwww, c1.xxxx\n",
            "constant fetch component": "/*    1   */          tfetch2D r0.___1, r6.zw, tf8\n/*    5   */          mul oC0.___w, r0.wwww, c1.xxxx\n",
            "no fetch writer": "/*    5   */          mul oC0.___w, r0.wwww, c1.xxxx\n",
            "other writer": fetch + "/*    2   */          mul r0.___w, r1.wwww, r1.wwww\n/*    5   */          mul oC0.___w, r0.wwww, c1.xxxx\n",
            "write in skipped region": "/*    1.0 */       cjmp !b3, L9\n/*    5   */          mul oC0.xyz1, r0.xzww, c20.wwww\n"
                                       "                label L9\n",
            "fetch overwritten in branch": fetch + "/*    1.1 */       cjmp !b3, L9\n/*    2   */          mul r0.___w, r1.wwww, r1.wwww\n"
                                           "                label L9\n/*    5   */          mul oC0.___w, r0.wwww, c1.xxxx\n",
            "loop": "/*    1.0 */       loop i0, L9\n/*    5   */          mul oC0.xyz1, r0.xzww, c20.wwww\n",
        }
        for name, body in cases.items():
            self.assertIsInstance(_alpha(body), st.Unsupported, name)

    def test_fetch_coordinate_offset_unsupported(self):
        r = _alpha("/*    1   */          tfetch2D r0.___x, r6.zw, tf8, OffsetX=0.5\n"
                   "/*    5   */          mul oC0.___w, r0.wwww, c1.xxxx\n")
        self.assertIsInstance(r, st.Unsupported)

    def test_same_instruction_register_write_is_not_the_producer(self):
        # r0.w is read by the alpha write before this instruction's own write lands, so the
        # producer is the tfetch above: the other write at the same index must not hide it.
        r = _alpha("/*    1   */          tfetch2D r0.x___, r6.zw, tf8\n"
                   "/*    5   */          mul r0.x___, r1.xxxx, r1.xxxx\n"
                   "              +       mulsc oC0.___w, c15.w, r0.x\n")
        self.assertEqual(r, {"source": "fetch", "slot": 8, "component": "x", "scale": "c15.w"})


class FindKillsTest(unittest.TestCase):
    def test_synthetic(self):
        ins = st.parse(HEAD + "/*    3   */          mul r1.x___, r0.xxxx, r0.xxxx\n"
                       "/*    4   */    (p0) kill_gt r0._, c254.xxxx, c255.yyyy\n"
                       "/*    5   */          kill_ge r0._, r1.x, c1.x\n")
        self.assertEqual(st.find_kills(ins), [{"index": 4, "op": "kill_gt", "pred": True},
                                              {"index": 5, "op": "kill_ge", "pred": False}])
        self.assertEqual(st.find_kills(st.parse(HEAD + "/*    3   */          mul r1.x___, r0.xxxx, r0.xxxx\n")), [])


DUMP_DIR = Path(__file__).resolve().parent.parent / "out" / "shader_dump"


def _dump(hash_):
    path = DUMP_DIR / f"shader_{hash_}.ucode.frag"
    if not path.exists():
        raise unittest.SkipTest(f"real shader dump {path} is absent (out/shader_dump is git-ignored); "
                                f"run the game with --dump_shaders to produce it")
    return st.parse(path.read_text(errors="replace"))


class RealDumpAlphaTest(unittest.TestCase):
    def test_7DDAD5C29B3F56EE_alpha_one(self):
        self.assertEqual(st.trace_ps_alpha(_dump("7DDAD5C29B3F56EE")),
                         {"source": "one", "slot": None, "component": None, "scale": None})

    def test_7CD57B81550F19E3_unsupported(self):
        # oC0.w = c15.w x r0.x, and r0.x = c26.w x r6.x (instruction 68, scalar mulsc): not a fetch.
        r = st.trace_ps_alpha(_dump("7CD57B81550F19E3"))
        self.assertIsInstance(r, st.Unsupported)
        self.assertIn("not a texture fetch", r.reason)

    def test_00E09D1BC5295D52_unsupported(self):
        # Same shape: oC0.w = c15.w x r8.x, and r8.x = c26.w x r0.x (instruction 88).
        r = st.trace_ps_alpha(_dump("00E09D1BC5295D52"))
        self.assertIsInstance(r, st.Unsupported)
        self.assertIn("not a texture fetch", r.reason)

    def test_A17D8AEC3A817D45_kill(self):
        self.assertEqual(st.find_kills(_dump("A17D8AEC3A817D45")), [{"index": 16, "op": "kill_gt", "pred": True}])


if __name__ == "__main__":
    unittest.main()
