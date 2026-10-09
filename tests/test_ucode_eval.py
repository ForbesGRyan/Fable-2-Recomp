import math
import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import ucode_eval as ue  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"


def no_sampler(slot, coords, attrs):
    raise AssertionError(f"unexpected texture fetch tf{slot}")


def run(text, consts=None, regs=None, bools=None, sampler=no_sampler, **kw):
    m = ue.Machine(text, consts or {}, sampler, regs=regs, bools=bools, **kw)
    return m.run(), m


def near(test, got, want, tol=1e-12):
    test.assertEqual(len(got), len(want))
    for g, w in zip(got, want):
        test.assertAlmostEqual(g, w, delta=tol * max(1.0, abs(w)))


# Hand-written dumps, one per instruction class; every expected number below is worked out by hand in the
# comment beside it.

VECTOR = """/*    0.0 */       exec
/*    1   */          mul r1.xyz_, r0.zyxx, c1.xxyy
/*    2   */          mad r2, r1.xyzw, c2.wwww, -r0.xyzw
/*    3   */          dp3 r3.x___, r1.zxyy, c1.zxyy
/*    4   */          add_sat r4.xy__, -r0.xyyy, c3.xyyy
/*    0.1 */       exece
/*    5   */          max oC0, r2, r3.xxxx
"""

SCALAR = """/*    0.0 */       exec
/*    1   */          mul r1.x___, r0.xxxx, r0.yyyy
              +       muls r1._y__, r0.zw
/*    2   */          add r0.x___, r0.xxxx, r0.xxxx
              +       adds_prev r2.x___, r0.x
/*    3   */          max r6._, r6, r6
              +       mulsc r2._y__, c1.x, r0.y
/*    4   */          mul r3, r2.xyxy, r2.xyxy
              +       retain_prev r6._
/*    0.1 */       exece
/*    5   */          max oC0, r3, r3
              +       rsq r2.__z_, r_abs[2].w
"""

PREDICATE = """/*    0.0 */       exec    // PredicateClean=false
/*    1   */          max r6._, r6, r6
              +       setp_gt r6._, r0.x
/*    2   */     (p0) add r1, r0, r0
/*    3   */    (!p0) add r1, -r0, -r0
/*    0.1 */ (!p0) jmp L2
/*    1.0 */  (p0) exec
/*    4   */     (p0) mul r1, r1, c1.xxxx
                label L2
/*    2.0 */       exece
/*    5   */          max oC0, r1, r1
"""

CEXEC = """/*    0.0 */       exec
/*    1   */          add r1, r0, c1
/*    0.1 */       cexec b133
/*    2   */          mul r1, r1, c1
/*    3   */          add r1, r1, c1
/*    1.0 */       exec
/*    4   */          add r1, r1, r0
/*    1.1 */       exece
/*    5   */          max oC0, r1, r1
"""

FETCH = """/*    0.0 */       exec
/*    1   */          mul r1.xy__, r0.yxxx, c1.xyyy
/*    2   */          tfetch2D r2.wx_1, r1.yx, tf3, OffsetX=1
/*    3   */          tfetch1D r3.___x, r2.y, tf4
/*    4   */          mul r4, r2, r3.wwww
/*    0.1 */       exece
/*    5   */          max oC0, r4, r4
"""

# The additions of the lighting sub-project: vertex fetches, a0-relative constants, the conditional moves
# on a sign, truncation, sine and cosine, and the jump on a bool constant.

VFETCH = """/*    0.0 */       exec
/*    1   */          vfetch_full r5.yxw1, r0.x, vf0, DataFormat=FMT_16_16_16_16_FLOAT, Stride=5, Signed=true
/*    2   */          vfetch_mini r7.xyz_, Offset=2, DataFormat=FMT_10_11_11, Signed=true
/*    3   */          add r0.x___, r0.xxxx, c1.xxxx
/*    4   */          vfetch_full r3.zyxw, r0.x, vf3, DataFormat=FMT_8_8_8_8, Stride=1
/*    5   */          vfetch_mini r4.x0__, Offset=1, DataFormat=FMT_32_FLOAT
/*    0.1 */       exece
/*    6   */          add o0, r5, r7
"""

# Operand conventions (fix round 1 of the lighting plan's Task 2). The SDK sources cited in the tests are
# thirdparty/rexglue-sdk/include/rex/graphics/format/ucode.h ("ucode.h") and
# thirdparty/rexglue-sdk/src/graphics/pipeline/shader/interpreter.cpp ("interpreter.cpp").
OPERANDS = """/*    0.0 */       exec
/*    1   */          add_sat r1, r0, c1
/*    2   */          max r6._, r6, r6
              +       subs r2.x___, r0.yw
/*    3   */          max r6._, r6, r6
              +       subsc r2._y__, c2.x, r0.y
/*    4   */          max r6._, r6, r6
              +       subs_prev r2.__z_, r0.w
/*    5   */          add r3, -r_abs[0], c2.yyyy
/*    0.1 */       exece
/*    6   */          max oC0, r3, r3
              +       mulsc_sat r2.___w, c2.z, r0.z
"""

SELECT = """/*    0.0 */       exec    // PredicateClean=false
/*    1   */          cndeq r1, r0, c1, c2
/*    2   */          dp2add r2.x___, r0.wzzz, c1.xyyy, c2.zwww
/*    3   */          max r6._, r6, r6
              +       exp r3.x___, r0.w
/*    4   */          max r6._, r6, r6
              +       log r3._y__, c1.w
/*    5   */          max r6._, r6, r6
              +       setp_ge r3.__z_, r0.y
/*    0.1 */       exece
/*    6   */          maxa o0, c3, r0
              +       setp_eq r3.___w, r0.x
"""

ADDRESS = """/*    0.0 */       exec
/*    1   */          max r6._, r6, r6
              +       maxasf r6._, r0.xx
/*    0.1 */       exece
/*    2   */          add o0, c[10+a0], r0.yyyy
"""

RELATIVE = """/*    0.0 */       exec
/*    1   */          mul r1.x___, r0.xxxx, c1.xxxx
              +       maxas r0._, r0.yy
/*    2   */          add r2, c[28+a0].xyzz, r0.zzzz
/*    3   */          cndge r3, r0.xyzw, c1.xxxx, c1.yyyy
/*    4   */          cndgt r4, r0.xyzw, c1.xxxx, c1.yyyy
/*    0.1 */       exece
/*    5   */          trunc o0, r0.wwzz
              +       truncs r5.x___, r0.w
"""

TRIG = """/*    0.0 */       exec
/*    1   */          max r6._, r6, r6
              +       sin r1.x___, r0.x
/*    2   */          max r6._, r6, r6
              +       cos r1._y__, r0.x
/*    0.1 */       cjmp !b129, L2
/*    1.0 */       exec
/*    3   */          add r1, r1, r1
                label L2
/*    1.1 */       cjmp b132, L3
/*    2.0 */       exec
/*    4   */          add r1.__z_, r1.zzzz, c1.xxxx
                label L3
/*    2.1 */       exece
/*    5   */          max o0, r1, r1
"""


LOOP = """/*    0.0 */       exec    // PredicateClean=false
/*    1   */          max r6._, r6, r6
              +       setp_gt r6._, r0.x
/*    0.1 */       loop i0, L3
                label L1
/*    1.0 */       exec
/*    2   */          add r1, r1, c1
/*    3   */          max r6._, r6, r6
              +       maxas r6._, r1.xx
/*    4   */          add r2, r2, c[10+a0]
/*    1.1 */  (p0) endloop i0, L1
                label L3
/*    2.0 */       exece
/*    5   */          add o0, r1, r2
"""

# The additions of the shadows sub-project (plan Task 2): getWeights2D, fetches with texel offsets, and the
# predicated branch as the mask shader 0F99810F072BF0FC has it (a jump on the cleared predicate over a block
# whose instructions all carry (p0), with a `serialize` line whose instruction is printed without a number).
WEIGHTS = """/*    0.0 */       exec
/*    1   */          getWeights2D r2.__xy, r0.yz, tf7, MagFilter=linear, MinFilter=linear, OffsetX=-0.5, OffsetY=-0.5
/*    2   */          getWeights2D r3.xy__, r0.yz, tf7, MagFilter=linear, MinFilter=linear
/*    3   */          getWeights2D r4.yx__, r0.yz, tf5, UnnormalizedTextureCoords=true, OffsetX=0.5
/*    0.1 */       exece
/*    4   */          max oC0, r2, r2
"""

TEXEL_FETCH = """/*    0.0 */       exec
/*    1   */          tfetch2D r1.x___, r0.yz, tf7, OffsetX=-1, OffsetY=-1
/*    2   */          tfetch2D r1._x__, r0.yz, tf7, OffsetY=-1
/*    3   */          tfetch2D r1.__x_, r0.yz, tf7, OffsetX=-1
/*    4   */          tfetch2D r1.___x, r0.yz, tf7
/*    0.1 */       exece
/*    5   */          max oC0, r1, r1
"""

BRANCH = """/*    0.0 */       exec    // PredicateClean=false
/*    1   */          sgt r1.x___, c1.xxxx, r0.wwww
/*    2   */          sgt r3.xy__, -r_abs[0].xxxx, c2.xxxx
              +       setp_ne r0._, r1.x
/*    0.1 */ (!p0) jmp L15
/*    1.0 */  (p0) exec
/*    3   */     (p0) add r4.xy__, r_abs[0].xyyy, c1.yyyy
/*    4   */          serialize
                 (p0) mul r4.xy__, r4.xyyy, c1.zzzz
              +  (p0) maxs r5.___w, c2.yy
/*    5   */     (p0) mulsc r3._y__, c2.z, r4.x
                label L15
/*    1.1 */       exec    // PredicateClean=false
/*    6   */          sgt r0.x___, r0.wwww, c1.wwww
/*    7   */          setp_ne r0._, r0.x
/*    2.0 */ (!p0) jmp L20
/*    2.1 */  (p0) exec
/*    8   */     (p0) add r3.x___, r0.wwww, c2.wwww
                label L20
/*    3.0 */       exece
/*    9   */          mad oC0.xyz1, r3.xxxx, c2.yyyy, r3.yyyy
"""


class HandWrittenDumps(unittest.TestCase):
    def test_vector_alu(self):
        out, m = run(VECTOR, consts={1: [0.5, -1, 2, 0], 2: [0, 0, 0, 3], 3: [1.25, 2.5, 0, 0]},
                     regs={0: [1, 2, 3, 4]})
        # 1: r1 = (r0.z * c1.x, r0.y * c1.x, r0.x * c1.y) = (1.5, 1, -1); w stays 0.
        near(self, m.r[1], [1.5, 1.0, -1.0, 0.0])
        # 2: r2 = r1 * 3 - r0 = (3.5, 1, -6, -4).
        near(self, m.r[2], [3.5, 1.0, -6.0, -4.0])
        # 3: the first three of (r1.z, r1.x, r1.y) . (c1.z, c1.x, c1.y) = -2 + 0.75 - 1.
        self.assertEqual(m.r[3][0], -2.25)
        # 4: sat(-1 + 1.25), sat(-2 + 2.5); z and w are not written.
        near(self, m.r[4], [0.25, 0.5, 0.0, 0.0])
        # 5: max(r2, -2.25) per component.
        near(self, out["oC0"], [3.5, 1.0, -2.25, -2.25])
        self.assertEqual(m.used_consts, {1, 2, 3})

    def test_scalar_alu_and_co_issue(self):
        out, m = run(SCALAR, consts={1: [0.5, 0, 0, 0]}, regs={0: [2, 3, 4, 5], 2: [0, 0, 0, -4]})
        # 1: vector r1.x = 2 * 3; scalar r1.y = r0.z * r0.w = 20 (ps = 20).
        near(self, m.r[1][:2], [6.0, 20.0])
        # 2: the vector half doubles r0.x, the scalar half reads r0.x as it was before the instruction:
        #    r2.x = 2 + ps = 22 (24 if the scalar read the vector's result).
        self.assertEqual(m.r[0][0], 4.0)
        self.assertEqual(m.r[2][0], 22.0)
        # 3: a constant and a register: r2.y = c1.x * r0.y = 1.5; `max r6._` writes nothing.
        self.assertEqual(m.r[2][1], 1.5)
        self.assertEqual(m.r[6], [0.0] * 4)
        # 4: r3 = (22, 1.5, 22, 1.5)^2; retain_prev with an empty mask keeps ps.
        near(self, out["oC0"], [484.0, 2.25, 484.0, 2.25])
        # 5: rsq of |r2.w| = 1 / sqrt(4).
        self.assertEqual(m.r[2][2], 0.5)
        self.assertEqual(m.ps, 0.5)

    def test_predicate_and_jump(self):
        out, m = run(PREDICATE, consts={1: [10, 0, 0, 0]}, regs={0: [1, 2, 3, 4]})
        self.assertTrue(m.p0)                                   # r0.x > 0
        near(self, out["oC0"], [20.0, 40.0, 60.0, 80.0])        # (r0 + r0) * 10
        out, m = run(PREDICATE, consts={1: [10, 0, 0, 0]}, regs={0: [-1, 2, 3, 4]})
        self.assertFalse(m.p0)
        near(self, out["oC0"], [2.0, -4.0, -6.0, -8.0])         # -r0 - r0, the multiply jumped over
        self.assertEqual(m.used_consts, set())

    def test_cexec_on_and_off(self):
        consts = {1: [2, 2, 2, 2]}
        regs = {0: [1, 1, 1, 1]}
        out, _ = run(CEXEC, consts=consts, regs=regs, bools={133})
        near(self, out["oC0"], [9.0] * 4)                       # ((1 + 2) * 2 + 2) + 1
        out, _ = run(CEXEC, consts=consts, regs=regs)
        near(self, out["oC0"], [4.0] * 4)                       # (1 + 2) + 1: the block did not run
        out, _ = run(CEXEC, consts=consts, regs=regs, bools={134})
        near(self, out["oC0"], [4.0] * 4)

    def test_fetch_with_a_swizzle(self):
        calls = []

        def sampler(slot, coords, attrs):
            calls.append((slot, coords, dict(attrs)))
            return {3: [0.1, 0.2, 0.3, 0.4], 4: [0.5, 0.6, 0.7, 0.8]}[slot]

        out, m = run(FETCH, consts={1: [2, 3, 0, 0]}, regs={0: [0.25, 0.5, 0, 0], 2: [9, 9, 7, 9]}, sampler=sampler)
        # 1: r1 = (r0.y * 2, r0.x * 3) = (1, 0.75); the fetch reads (r1.y, r1.x).
        self.assertEqual(calls[0], (3, (0.75, 1.0), {"OffsetX": "1"}))
        # 2: destination `wx_1`: x takes the sample's w, y its x, z is kept, w is the constant 1.
        near(self, m.r[2], [0.4, 0.1, 7.0, 1.0])
        # 3: a 1D fetch has one coordinate (r2.y); `___x` puts the sample's x in w.
        self.assertEqual(calls[1][:2], (4, (0.1,)))
        self.assertEqual(m.r[3], [0.0, 0.0, 0.0, 0.5])
        near(self, out["oC0"], [0.2, 0.05, 3.5, 0.5])
        self.assertEqual([(i, s) for i, s, _ in m.fetches], [(2, 3), (3, 4)])

    def test_operand_conventions(self):
        out, m = run(OPERANDS, consts={1: [0.25, -2.5, 0.5, 2.5], 2: [10, 1, -4, 0]}, regs={0: [-0.5, 3, 0.75, -2]})
        # 1: saturation clamps on both sides: sat(-0.25, 0.5, 1.25, 0.5) (interpreter.cpp 831 to 834).
        self.assertEqual(m.r[1], [0.0, 0.5, 1.0, 0.5])
        # 2: subs is first component minus second: r0.y - r0.w = 3 - (-2) (ucode.h 1132).
        # 3: subsc is the constant minus the register: c2.x - r0.y = 10 - 3 (interpreter.cpp 601 to 613: operand 0
        #    is the constant, 1 the register; 756 to 759: operand 0 minus operand 1).
        # 4: subs_prev is the operand minus the previous scalar: r0.w - 7 (ucode.h 1137; interpreter.cpp 761 to 763).
        self.assertEqual(m.r[2][:3], [5.0, 7.0, -9.0])
        # 5: `-r_abs[0]` takes the absolute value first, then negates: -|r0| + 1 (interpreter.cpp 289 to 298).
        self.assertEqual(out["oC0"], [0.5, -2.0, 0.25, -1.0])
        # 6: a saturated scalar writes the clamped value and leaves the previous scalar as computed:
        #    sat(-4 * 0.75) = 0 is written, ps stays -3 (interpreter.cpp 836, 837).
        self.assertEqual(m.r[2][3], 0.0)
        self.assertEqual(m.ps, -3.0)

    def test_selects_exponents_and_predicate_results(self):
        consts = {1: [10, 20, 30, 8], 2: [-1, -2, 5, 7], 3: [0.5, -3, 2.5, 300.4]}
        out, m = run(SELECT, consts=consts, regs={0: [0, -1, 2, 3], 3: [9, 9, 9, 9]})
        # 1: cndeq takes the second operand where the first is 0, else the third (ucode.h 1437 to 1442).
        self.assertEqual(m.r[1], [10.0, -2.0, 5.0, 7.0])
        # 2: dp2add is two products plus the third operand's FIRST component: 3 * 10 + 2 * 20 + c2.z
        #    (ucode.h 1470 to 1482; the printed swizzle `zwww` puts c2.z first).
        self.assertEqual(m.r[2][0], 75.0)
        # 3, 4: exp and log are base 2: 2^3 and log2(8) (ucode.h 1059, 1070).
        self.assertEqual(m.r[3][:2], [8.0, 3.0])
        # 5: setp_ge with -1: the predicate is false and the result is 1 (ucode.h 1175 to 1182; interpreter.cpp
        #    the result is float(!predicate)).
        self.assertEqual(m.r[3][2], 1.0)
        # 6: setp_eq with 0: the predicate is true and the result is 0 (ucode.h 1142 to 1149; interpreter.cpp 764
        #    to 767). maxa is max and sets a0 from the FIRST operand's w, floor(300.4 + 0.5) clamped to 255
        #    (ucode.h 1670 to 1684; interpreter.cpp 549 to 551).
        self.assertTrue(m.p0)
        self.assertEqual(m.r[3][3], 0.0)
        self.assertEqual(out["o0"], [0.5, -1.0, 2.5, 300.4])
        self.assertEqual(m.a0, 255)
        # The half is added before the floor: w = 2.6 gives 3, not 2 (ucode.h 1670; interpreter.cpp 551).
        _, m = run(SELECT, consts={**consts, 3: [0.5, -3, 2.5, 2.6]}, regs={0: [0, -1, 2, 3]})
        self.assertEqual(m.a0, 3)

    def test_address_register_clamp_and_floor(self):
        consts = {12: [100, 0, 0, 0], 13: [1000, 0, 0, 0]}
        # maxasf takes floor(a) where maxas takes floor(a + 0.5): 2.7 gives 2 (ucode.h 1126; interpreter.cpp 751).
        out, m = run(ADDRESS, consts=consts, regs={0: [2.7, 1, 0, 0]})
        self.assertEqual(m.a0, 2)
        self.assertEqual(out["o0"][0], 101.0)
        # Both clamp to [-256, 255] (ucode.h 1119, 1126; interpreter.cpp 744, 751).
        for text, low in ((ADDRESS, [-300.2, 0, 0, 0]), (RELATIVE, [0, -300.2, 0, 0])):
            m = ue.Machine(text, {1: [3, -7, 0, 0]}, no_sampler, regs={0: low})
            with self.assertRaises(KeyError):               # c[10 - 256] / c[28 - 256] is not supplied
                m.run()
            self.assertEqual(m.a0, -256)
        m = ue.Machine(RELATIVE, {1: [3, -7, 0, 0]}, no_sampler, regs={0: [0, 254.6, 0, 0]})
        with self.assertRaises(KeyError):
            m.run()
        self.assertEqual(m.a0, 255)                         # floor(254.6 + 0.5)

    def test_vertex_fetch(self):
        calls = []

        def vfetch(slot, offset, attrs, index):
            calls.append((slot, offset, attrs.get("DataFormat"), index))
            return {(0, 0): [1, 2, 3, 4], (0, 2): [0.1, 0.2, 0.3, 9], (3, 0): [5, 6, 7, 8], (3, 1): [0.5, 9, 9, 9]}[(slot, offset)]

        out, m = run(VFETCH, consts={1: [100, 0, 0, 0]}, regs={0: [7, 0, 0, 0]}, vfetch=vfetch)
        # A mini fetch reads the stream and the index of the full fetch before it, at its own offset; the
        # callback is told the index, as the shader computed it (7, then 7 + c1.x).
        self.assertEqual(calls, [(0, 0, "FMT_16_16_16_16_FLOAT", 7.0), (0, 2, "FMT_10_11_11", 7.0),
                                 (3, 0, "FMT_8_8_8_8", 107.0), (3, 1, "FMT_32_FLOAT", 107.0)])
        self.assertEqual(m.vfetches, [(1, 0, 0, 7.0), (2, 0, 2, 7.0), (4, 3, 0, 107.0), (5, 3, 1, 107.0)])
        near(self, m.r[5], [2.0, 1.0, 4.0, 1.0])                # `yxw1`
        near(self, m.r[7], [0.1, 0.2, 0.3, 0.0])                # `xyz_`
        near(self, m.r[3], [7.0, 6.0, 5.0, 8.0])                # `zyxw`
        near(self, m.r[4], [0.5, 0.0, 0.0, 0.0])                # `x0__`
        near(self, out["o0"], [2.1, 1.2, 4.3, 1.0])

    def test_vertex_fetch_needs_a_callback(self):
        with self.assertRaises(ValueError):
            run(VFETCH, consts={1: [100, 0, 0, 0]})

    def test_relative_constant_and_conditional_moves(self):
        consts = {1: [3, -7, 0, 0], 28: [1, 1, 1, 1], 30: [10, 20, 30, 40], 31: [50, 60, 70, 80]}
        out, m = run(RELATIVE, consts=consts, regs={0: [-0.0, 2.4, 0.5, -2.75]})
        # 1: maxas sets a0 = floor(2.4 + 0.5) = 2; with an empty mask it writes no register.
        self.assertEqual(m.a0, 2)
        self.assertEqual(m.r[0], [-0.0, 2.4, 0.5, -2.75])
        # 2: c[28 + 2] = c30, swizzle xyzz, plus r0.z.
        near(self, m.r[2], [10.5, 20.5, 30.5, 30.5])
        self.assertIn(30, m.used_consts)
        # 3, 4: cndge takes the second operand where the first is >= 0, cndgt where it is > 0 (-0.0 is
        #       >= 0 and not > 0).
        self.assertEqual(m.r[3], [3.0, 3.0, 3.0, -7.0])
        self.assertEqual(m.r[4], [-7.0, 3.0, 3.0, -7.0])
        # 5: truncation is toward zero.
        self.assertEqual(out["o0"], [-2.0, -2.0, 0.0, 0.0])
        self.assertEqual(m.r[5][0], -2.0)
        out, m = run(RELATIVE, consts=consts, regs={0: [1, 2.5, 0.5, 1.5]})
        self.assertEqual(m.a0, 3)                               # floor(2.5 + 0.5)
        near(self, m.r[2], [50.5, 60.5, 70.5, 70.5])
        with self.assertRaises(KeyError):                       # c[28 + 5] was not supplied
            run(RELATIVE, consts=consts, regs={0: [1, 5.0, 0.5, 1.5]})

    def test_sine_cosine_and_bool_jumps(self):
        x = 0.75
        consts = {1: [100, 0, 0, 0]}
        # b129 clear: `cjmp !b129` jumps over instruction 3; b132 clear: `cjmp b132` does not jump.
        out, _ = run(TRIG, consts=consts, regs={0: [x, 0, 0, 0]})
        near(self, out["o0"], [math.sin(x), math.cos(x), 100.0, 0.0])
        out, _ = run(TRIG, consts=consts, regs={0: [x, 0, 0, 0]}, bools={129})
        near(self, out["o0"], [2 * math.sin(x), 2 * math.cos(x), 100.0, 0.0])
        out, _ = run(TRIG, consts=consts, regs={0: [x, 0, 0, 0]}, bools={129, 132})
        near(self, out["o0"], [2 * math.sin(x), 2 * math.cos(x), 0.0, 0.0])

    def test_loop(self):
        consts = {1: [1, 1, 1, 1], 11: [10, 0, 0, 0], 12: [100, 0, 0, 0], 13: [1000, 0, 0, 0]}
        # r0.x <= 0: the predicate is false, so `(p0) endloop` never leaves early. Each of the three
        # iterations adds 1 to r1 and c[10 + r1.x] to r2: 10 + 100 + 1000.
        out, _ = run(LOOP, consts=consts, regs={0: [-1, 0, 0, 0]}, loops={0: 3})
        self.assertEqual(out["o0"], [1113.0, 3.0, 3.0, 3.0])
        # A count of 0 skips the body.
        out, _ = run(LOOP, consts=consts, regs={0: [-1, 0, 0, 0]}, loops={0: 0})
        self.assertEqual(out["o0"], [0.0] * 4)
        # r0.x > 0: the predicated endloop leaves after the first iteration.
        out, _ = run(LOOP, consts=consts, regs={0: [1, 0, 0, 0]}, loops={0: 3})
        self.assertEqual(out["o0"], [11.0, 1.0, 1.0, 1.0])
        with self.assertRaises(NotImplementedError):            # no count given for i0
            run(LOOP, consts=consts, regs={0: [-1, 0, 0, 0]})

    def test_a_constant_that_was_not_supplied_is_an_error(self):
        with self.assertRaises(KeyError):
            run(VECTOR, consts={1: [0.5, -1, 2, 0], 2: [0, 0, 0, 3]}, regs={0: [1, 2, 3, 4]})

    def test_an_unknown_op_is_an_error(self):
        text = "/*    0.0 */       exece\n/*    1   */          cube r1, r0.zzxy, r0.yxzz\n"
        with self.assertRaises(NotImplementedError):
            run(text, regs={0: [1, 2, 3, 4]})


class MaskShaderInstructions(unittest.TestCase):
    """Plan Task 2 of the shadows sub-project: what the two mask shaders need beyond the lit families."""

    def test_get_weights_by_hand(self):
        # The emulator's rule (thirdparty/rexglue-sdk/src/graphics/pipeline/shader/dxbc_translator_fetch.cpp 724
        # to 730, 949 to 970): weight = frac(coordinate * size + Offset - 0.5), the 0.5 being the translator's
        # own ("for coordinate lerp factors"); the size is not applied to unnormalised coordinates. Its
        # rounding epsilon of 1.5 / 1024 texel is not modelled.
        sizes = {7: (8, 4), 5: (64, 64)}
        out, m = run(WEIGHTS, regs={0: [9, 0.3125, 0.40625, 9], 2: [5, 6, 7, 8]}, tex_size=sizes)
        # 1: u * 8 = 2.5, v * 4 = 1.625; with Offset -0.5: frac(2.5 - 1) = 0.5, frac(1.625 - 1) = 0.625.
        #    Destination `__xy`: z takes the first weight, w the second; x and y are kept.
        self.assertEqual(m.r[2], [5.0, 6.0, 0.5, 0.625])
        # 2: without offsets: frac(2.5 - 0.5) = 0, frac(1.625 - 0.5) = 0.125.
        self.assertEqual(m.r[3][:2], [0.0, 0.125])
        # 3: unnormalised: frac(0.3125 + 0.5 - 0.5) = 0.3125 and frac(0.40625 - 0.5) = 0.90625; `yx__` swaps.
        self.assertEqual(m.r[4][:2], [0.90625, 0.3125])
        self.assertEqual(out["oC0"], [5.0, 6.0, 0.5, 0.625])
        self.assertEqual(m.weights, [(1, 7, (0.3125, 0.40625), (0.5, 0.625)), (2, 7, (0.3125, 0.40625), (0.0, 0.125)),
                                     (3, 5, (0.3125, 0.40625), (0.3125, 0.90625))])
        # A negative coordinate: frac is x - floor(x): u * 8 = -0.75, frac(-0.75 - 1) = 0.25.
        _, m = run(WEIGHTS, regs={0: [9, -0.09375, 0.40625, 9]}, tex_size=sizes)
        self.assertEqual(m.r[2][2], 0.25)

    def test_get_weights_needs_the_texture_size(self):
        with self.assertRaises(KeyError):
            run(WEIGHTS, regs={0: [9, 0.3125, 0.40625, 9]}, tex_size={5: (64, 64)})
        with self.assertRaises(KeyError):
            run(WEIGHTS, regs={0: [9, 0.3125, 0.40625, 9]})

    def test_fetch_with_texel_offsets(self):
        # A point-sampled 8 x 4 map whose texel (ix, iy) reads 10 * iy + ix. ue.point_texel is the texel a
        # point fetch reads: floor(coordinate * size + Offset) (the emulator adds the offset in texels before
        # it samples, dxbc_translator_fetch.cpp 724 to 726).
        seen = []

        def sampler(slot, coords, attrs):
            ix, iy = ue.point_texel(coords, (8, 4), attrs)
            seen.append((ix, iy))
            return [10.0 * iy + ix, -1.0, -1.0, -1.0]

        # u * 8 = 2.5, v * 4 = 1.625: the fetch without an offset reads texel (2, 1), the others its left,
        # upper and upper-left neighbours.
        out, m = run(TEXEL_FETCH, regs={0: [9, 0.3125, 0.40625, 9]}, sampler=sampler)
        self.assertEqual(seen, [(1, 0), (2, 0), (1, 1), (2, 1)])
        self.assertEqual(out["oC0"], [1.0, 2.0, 11.0, 12.0])
        self.assertEqual(ue.fetch_offsets({"OffsetX": "-1", "OffsetY": "-0.5"}), (-1.0, -0.5, 0.0))
        self.assertEqual(ue.fetch_offsets({}), (0.0, 0.0, 0.0))
        # Just left of a texel edge the offset fetch reads one texel further: u * 8 = 1.999 -> texels 0 and 1.
        self.assertEqual(ue.point_texel((1.999 / 8, 0.5), (8, 4), {"OffsetX": "-1"}), (0, 2))
        self.assertEqual(ue.point_texel((-0.01, 0.5), (8, 4), {}), (-1, 2))        # floor, not truncation

    def test_predicated_branch_pair(self):
        consts = {1: [20, 0.5, 2, 30], 2: [0, 1, 0.25, 100]}
        # r0.w = 10: 20 > 10, the first block runs: r4.xy = (|r0.x| + 0.5) * 2, (|r0.y| + 0.5) * 2 = (4, 7);
        # the `serialize` line's instruction and its scalar half run under (p0): r5.w = 1; r3.y = 0.25 * 4.
        # 10 > 30 is false: the second block is jumped over and r3.x stays the 0 of instruction 2.
        out, m = run(BRANCH, consts=consts, regs={0: [-1.5, 3, 0, 10], 3: [9, 9, 9, 9]})
        self.assertEqual(m.r[4][:2], [4.0, 7.0])
        self.assertEqual(m.r[5][3], 1.0)
        self.assertEqual(out["oC0"], [1.0, 1.0, 1.0, 1.0])              # 0 * 1 + 1; alpha the constant 1
        # r0.w = 25: neither block runs; instruction 2 has zeroed r3.xy (-|x| > 0 is never true).
        out, m = run(BRANCH, consts=consts, regs={0: [-1.5, 3, 0, 25], 3: [9, 9, 9, 9], 4: [7, 7, 7, 7]})
        self.assertEqual(m.r[4], [7.0] * 4)
        self.assertEqual(m.r[5][3], 0.0)
        self.assertEqual(out["oC0"], [0.0, 0.0, 0.0, 1.0])
        # r0.w = 40: only the second block: r3.x = 40 + 100.
        out, m = run(BRANCH, consts=consts, regs={0: [-1.5, 3, 0, 40], 3: [9, 9, 9, 9]})
        self.assertEqual(out["oC0"], [140.0, 140.0, 140.0, 1.0])
        self.assertEqual(m.r[5][3], 0.0)


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class RealMaskDumps(unittest.TestCase):
    def test_both_mask_shaders_run_and_take_their_branches(self):
        """The interpreter runs both dumps to the end. With every constant 0 and the literals where the
        shaders read them, 1B4B's depth 1 - 0 is not beyond c46.x = 2 (the near side: c12.w and alpha 0) and
        0F99's distance 0 is inside neither cascade's range (both blocks jumped over, then the fade is 0)."""
        zero = {k: [0.0] * 4 for k in (12, 31, 36, 37, 38, 39, 40, 41, 42, 43, 46, 47, 77, 116, 117, 118, 119)}
        fetched = []

        def sampler(slot, coords, attrs):
            fetched.append(slot)
            return [0.0] * 4

        text = ue.load(DUMPS, "1B4B5AE12EF270CA", "frag")
        consts = {**zero, 12: [0, 0, 0, 0.5], 46: [2.0, 0, 0, 0], 254: [0.5, 1.0, 0.0, -1.0], 255: [2.0, 0, 0, 0]}
        out, m = run(text, consts=consts, regs={1: [10, 20, 0, 0]}, sampler=sampler, tex_size={7: (1024, 1024)})
        self.assertEqual(out["oC0"], [0.5, 0.5, 0.5, 0.0])
        self.assertEqual(fetched, [13])
        self.assertEqual(m.weights, [])
        del fetched[:]
        text = ue.load(DUMPS, "0F99810F072BF0FC", "frag")
        consts = {**zero, 12: [0, 0, 0, 0.5], 119: [0, 0, 0, 1.0], 248: [10.0, 1 / 12, -1.0, 2.0],
                  249: [0.0, 1.0, 0.5, 0.1]}
        out, m = run(text, consts=consts, regs={0: [10, 20, 0, 0]}, sampler=sampler, tex_size={7: (1024, 1024)})
        self.assertEqual(fetched, [13])
        self.assertEqual(out["oC0"], [0.0, 0.0, 0.0, 1.0])



@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class RealTerrainDumps(unittest.TestCase):
    """The Task 9 checkers on the real terrain dumps, with fewer trials than their command-line run (600 and
    400, which take about a minute; `ps_check.txt` and `vs_check.txt` of Task 9 are those runs)."""

    def test_vs_check(self):
        import vs_check
        lines = vs_check.run(trials=60)
        self.assertEqual(len(lines), 3)
        for line, hash_ in zip(lines, ("C30A97D946FA2BE4", "FB68A7F2301210E1", "5003700B7C9B1C16")):
            self.assertTrue(line.startswith(f"{hash_}: 60 of 60 random points agree with the reading"), line)
        self.assertIn("constants read [0, 1, 2, 3, 4, 5, 6, 8, 11, 14, 19, 46, 47, 113, 114, 115, 253, 254]", lines[0])
        self.assertIn("constants read [0, 1, 2, 3, 8, 11, 14, 46, 47, 72, 113, 114, 115, 254, 255]", lines[2])

    def test_ps_check(self):
        import ps_check
        results = ps_check.run(trials=int(os.environ.get("FABLE2_PS_CHECK_TRIALS", "24")))
        self.assertEqual(sorted(results), sorted(list(ps_check.LAYERS) + ["7DDAD5C29B3F56EE", "2F8A0341E8C09698"]))
        informative = "(a light factor below 0, from the random cone and range constants)"
        for hash_, (n, tally, slots) in results.items():
            self.assertGreater(n, 0, hash_)
            for name, count in tally.items():
                if name != informative:
                    self.assertEqual(count, n, f"{hash_}: {name}")
        # Which textures a layer shader fetches with the steep block and bool b135 on and off.
        _, _, slots = results["45243B758F9272AB"]
        self.assertEqual(slots, {(False, False): [(0, 2, 3, 8)], (False, True): [(0, 2, 3, 8, 9)],
                                 (True, False): [(0, 2, 3, 8, 13, 14)], (True, True): [(0, 2, 3, 8, 9, 13, 14)]})
        _, _, slots = results["4755A2E4A05BA678"]
        self.assertEqual(slots, {(False, False): [(0, 2, 3, 8)]})


if __name__ == "__main__":
    unittest.main()
