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

    def test_predicated_and_sat_unsupported(self):
        for line in ("mul_sat r1.xy__, r0.xyyy, c3.xyyy", "(p0) mul r1.xy__, r0.xyyy, c3.xyyy"):
            text = f"/*    0.0 */       exec\n/*    1   */          {line}\n/*    3   */          tfetch2D r2, r1.xy, tf1\n"
            ins = st.parse(text)
            self.assertIsInstance(st.trace_ps_albedo(ins, st.texture_fetches(ins)[0]), st.Unsupported, line)


if __name__ == "__main__":
    unittest.main()
