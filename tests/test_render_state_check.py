import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import render_state_check as rsc  # noqa: E402

VS = "0xECD66A10092E6562"
PS = "0xF6D98C7B4D98438B"
# Real words of the bridge scene: an alpha-tested mesh draw (colour mask RGBA, depth test and
# write with GreaterEqual, no blending, alpha test Greater with alpha-to-mask, cull back) under
# the reversed depth range (viewport Z scale -1, offset 1, both enabled in PA_CL_VTE_CNTL).
STATE = {"2104": 0x0000000F, "210E": 0x3F008081, "2200": 0x00708767, "2201": 0x00010001,
         "2202": 0x8700001C, "2205": 0x00218006, "2105": 0x3F800000, "2106": 0x3F800000,
         "2107": 0x3F800000, "2108": 0x3F800000, "2113": 0xBF800000, "2114": 0x3F800000,
         "2206": 0x0000043F,
         # Stencil reference words (reference 0x2A, mask and write mask 0xFF) and the polygon
         # offset registers (scale 2 and offset 0.0005 for front faces, 1.5 and 0.00025 for back).
         "210C": 0x00FFFF00, "210D": 0x00FFFF2A, "2380": 0x40000000, "2381": 0x3A03126F,
         "2382": 0x3FC00000, "2383": 0x3983126F}
# A GPU value for each register that differs from STATE in a high bit only (and, for the
# floats, in one bit), so a comparison of the low half or of some registers cannot pass.
HIGH_BIT_DIFFERENCE = {"2104": 0x8000000F, "210E": 0x3F018081, "2200": 0x80708767, "2201": 0x01010001,
                       "2202": 0xAA00001C, "2205": 0x00018006, "2105": 0x3F810000, "2106": 0x3F800100,
                       "2107": 0x7F800000, "2108": 0xBF800000, "2113": 0x3F800000, "2114": 0x3F000000,
                       "2206": 0x0001043F,
                       "210C": 0x00FEFF00, "210D": 0x01FFFF2A, "2380": 0xC0000000, "2381": 0x3A83126F,
                       "2382": 0x3FC00001, "2383": 0x7983126F}
C0 = [0.5, -0.25, 0.0, 92.5]
OFFSET = 100  # GPU frame = capture frame + OFFSET in these logs
ALL_BINS, PRE_PASS_BINS, COLOUR_BINS = 0xFFFFFFFF, 0x15555555, 0x2AAAAAAA
PRE_PASS = 0x80000001  # the bin select of the depth pre-pass in the real captures


def f32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def rs(bin_mask=ALL_BINS, **over):
    """The row's "rs"; rs(r2201=...) overrides a register, bin_mask=None leaves the mask out."""
    out = {k: f"0x{v:08X}" for k, v in STATE.items()}
    out.update({k[1:]: f"0x{v:08X}" for k, v in over.items()})
    if bin_mask is not None:
        out["bin_mask_lo"] = f"0x{bin_mask:08X}"
        out["bin_mask_hi"] = "0x00000000"
    return out


def draw_row(frame, cmd, count=6, state=None, vs=VS, ps=PS, c0=C0, in_scene=True, with_rs=True):
    row = {"kind": "draw", "frame": frame, "func": "D3DDevice_DrawIndexedVertices?",
           "args": ["0x00000004", "0x00000000", "0x00000000", f"0x{count:08X}"], "in_scene": in_scene,
           "cmd": f"0x{cmd:08X}", "ps_hash": ps, "vs_hash": vs}
    if c0 is not None:
        row["bank"] = list(c0) + [0.0] * 12
    if with_rs:
        row["rs"] = rs() if state is None else state
    return row


def gpu_line(frame, d, cmd, n=6, regs=None, vs=VS, ps=PS, c0=C0, delta=4, bs=0xFFFFFFFF, bm=0xFFFFFFFF,
             mode=4, pitch=1120, **over):
    r = dict(STATE if regs is None else regs)
    r.update({k[1:]: v for k, v in over.items()})
    words = " ".join(f"r{k}={v:08X}" for k, v in r.items())
    return (f"D f={frame} d={d} a={rsc.gpu_address(cmd) + delta:08X} pt=4 n={n} src=0 k=0 e=0 bs={bs:016X} "
            f"bm={bm:016X} vs={int(vs, 16):016X} ps={int(ps, 16):016X} pitch={pitch} mode={mode} "
            f"ioff=00000000 dma=1B000000 c0={','.join(f'{f32(x):08X}' for x in c0)} {words}")


def tiled(frame, d, cmd, pre_pass=True, colour=True, **over):
    """The executions of one main-scene draw: a depth pre-pass with a substituted shader and
    colour mask 0, then two colour tiles. `over` changes the pre-pass line."""
    lines = []
    if pre_pass:
        args = dict(vs="0xC47236E5A9B00356", ps="0x37CAF82F552893EE", bs=PRE_PASS, r2104=0)
        args.update(over)
        lines.append(gpu_line(frame, d, cmd, **args))
    if colour:
        lines += [gpu_line(frame, d + 500, cmd, bs=0x2, bm=0x8000000F),
                  gpu_line(frame, d + 900, cmd, bs=0x8, bm=0x8000000F)]
    return lines


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_check(self, rows, gpu_lines, *extra):
        cap = self.dir / "cap.jsonl"
        cap.write_text("\n".join(json.dumps(r) for r in rows) + "\n")
        gpu = self.dir / "gpu.log"
        gpu.write_text("\n".join(gpu_lines) + "\n")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = rsc.main([str(cap), "--gpu", str(gpu), *extra])
        return code, out.getvalue()

    def register_line(self, text, reg):
        lines = [ln for ln in text.splitlines() if ln.startswith(f"register {reg} ")]
        self.assertEqual(len(lines), 1, text)
        return lines[0]

    def result(self, text):
        lines = [ln for ln in text.splitlines() if ln.startswith("RESULT:")]
        self.assertEqual(len(lines), 1, text)
        return lines[0]


class AddressTest(unittest.TestCase):
    def test_gpu_address_matches_the_capture_conversion(self):
        # xdk_layout.h GpuAddress: the 0xE0000000 view is offset by 0x1000.
        self.assertEqual(rsc.gpu_address(0xE0000000), 0x00001000)
        self.assertEqual(rsc.gpu_address(0xDFFFF000), 0x1FFFF000)
        self.assertEqual(rsc.gpu_address(0xA0001000), 0x00001000)
        self.assertEqual(rsc.gpu_address(0xFF4FE848), 0x1F4FF848)

    def test_the_registers_are_the_nineteen_of_the_capture(self):
        # The thirteen of the materials proof, then the stencil reference words and the four
        # polygon offset registers (render_state.h kRowStateRegisters without RB_COLOR_INFO).
        self.assertEqual(set(rsc.REGISTERS), set(STATE))
        self.assertEqual(len(rsc.REGISTERS), 19)
        self.assertEqual(rsc.REGISTERS[13:], ("210C", "210D", "2380", "2381", "2382", "2383"))
        self.assertEqual(set(rsc.REGISTER_NAMES), set(rsc.REGISTERS))


class EqualityTest(Base):
    def rows(self):
        return [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100, count=12), draw_row(2, 0xFF600000)]

    def gpu(self):
        return [gpu_line(OFFSET + 1, 0, 0xFF500000), gpu_line(OFFSET + 1, 1, 0xFF500100, n=12),
                gpu_line(OFFSET + 1, 2, 0xFF5F0000, n=3),  # a draw the capture did not sample
                gpu_line(OFFSET + 2, 0, 0xFF600000)]

    def test_all_equal_exits_zero_with_counts(self):
        code, text = self.run_check(self.rows(), self.gpu())
        self.assertEqual(code, 0, text)
        self.assertIn("rows 3, sampled 3, not sampled 0", text)
        self.assertIn("paired 3 of 3 sampled rows (100.00%)", text)
        self.assertIn(f"GPU frame = capture frame + {OFFSET}", text)
        for reg in rsc.REGISTERS:
            line = self.register_line(text, reg)
            self.assertIn("sampled 3, paired 3, equal 3, unequal 0, no colour execution 0;", line)
            self.assertIn("colour executions 3, equal 3, unequal 0;", line)
            self.assertIn("pre-pass executions 0, equal 0, unequal 0", line)
        self.assertEqual(self.result(text),
                         "RESULT: OK: paired 3 of 3 sampled rows (100.00%); every register equal in every "
                         "colour-pass execution (3) and every pre-pass execution (0)")

    def test_a_difference_in_any_register_exits_one_and_is_listed(self):
        for reg in rsc.REGISTERS:
            with self.subTest(register=reg):
                gpu = self.gpu()
                gpu[1] = gpu_line(OFFSET + 1, 1, 0xFF500100, n=12, **{"r" + reg: HIGH_BIT_DIFFERENCE[reg]})
                code, text = self.run_check(self.rows(), gpu)
                self.assertEqual(code, 1, text)
                for other in rsc.REGISTERS:
                    line = self.register_line(text, other)
                    if other == reg:
                        self.assertIn("sampled 3, paired 3, equal 2, unequal 1, no colour execution 0;", line)
                        self.assertIn("colour executions 3, equal 2, unequal 1;", line)
                    else:
                        self.assertIn("sampled 3, paired 3, equal 3, unequal 0, no colour execution 0;", line)
                        self.assertIn("colour executions 3, equal 3, unequal 0;", line)
                # The mismatch names the capture frame, the GPU frame and the GPU draw ordinal.
                self.assertRegex(text, rf"mismatch {reg}: frame 1 \(GPU frame 101, draw 1\).*"
                                       rf"device 0x{STATE[reg]:08X}, GPU 0x{HIGH_BIT_DIFFERENCE[reg]:08X}")
                self.assertEqual(text.count("mismatch "), 1)
                self.assertEqual(self.result(text), "RESULT: FAIL: paired 3 of 3 sampled rows (100.00%); "
                                                    f"unequal in a colour-pass execution: {reg}")

    def test_only_the_first_five_mismatches_of_a_register_are_listed(self):
        rows = [draw_row(1, 0xFF500000 + 0x100 * i) for i in range(8)]
        gpu = [gpu_line(OFFSET + 1, i, 0xFF500000 + 0x100 * i, r2200=0x00708756) for i in range(8)]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 1)
        self.assertIn("equal 0, unequal 8, no colour execution 0;", self.register_line(text, "2200"))
        self.assertEqual(text.count("mismatch 2200:"), 5)

    def test_rows_without_rs_are_not_sampled_and_never_equal(self):
        rows = self.rows()
        rows[2] = draw_row(2, 0xFF600000, with_rs=False)
        code, text = self.run_check(rows, self.gpu())
        self.assertEqual(code, 0, text)
        self.assertIn("rows 3, sampled 2, not sampled 1", text)
        self.assertIn("sampled 2, paired 2, equal 2, unequal 0", self.register_line(text, "2104"))
        self.assertIn("paired 2 of 2 sampled rows (100.00%)", self.result(text))

    def test_a_register_missing_from_rs_is_not_sampled_for_that_register(self):
        rows = self.rows()
        del rows[0]["rs"]["210E"]
        code, text = self.run_check(rows, self.gpu())
        self.assertEqual(code, 0, text)
        self.assertIn("sampled 2, paired 2, equal 2, unequal 0", self.register_line(text, "210E"))
        self.assertIn("sampled 3, paired 3, equal 3, unequal 0", self.register_line(text, "2104"))

    def test_a_register_the_gpu_log_lacks_is_named_in_the_result(self):
        # A GPU log of the first diagnostic has ten registers: the other three are not proven.
        ten = {k: v for k, v in STATE.items() if k not in ("2113", "2114", "2206")}
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000, regs=ten), gpu_line(OFFSET + 1, 1, 0xFF500100, n=12, regs=ten),
               gpu_line(OFFSET + 2, 0, 0xFF600000, regs=ten)]
        code, text = self.run_check(self.rows(), gpu)
        self.assertEqual(code, 0, text)
        self.assertIn("colour executions 0, equal 0, unequal 0;", self.register_line(text, "2113"))
        self.assertIn("colour executions 3, equal 3, unequal 0;", self.register_line(text, "2108"))
        self.assertTrue(self.result(text).endswith("; not compared (absent from the GPU log): 2113, 2114, 2206"),
                        text)

    def test_no_paired_row_is_not_a_proof(self):
        code, text = self.run_check(self.rows(), [gpu_line(OFFSET + 1, 0, 0xFF700000)])
        self.assertEqual(code, 1, text)
        self.assertEqual(self.result(text), "RESULT: FAIL: paired 0 of 3 sampled rows (0.00%); 3 rows not paired")


class PairingTest(Base):
    def test_a_packet_with_another_index_count_is_not_paired(self):
        # Same address, same registers, other index count: not the capture's draw.
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100, count=12)]
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000), gpu_line(OFFSET + 1, 1, 0xFF500100, n=13)]
        code, text = self.run_check(rows, gpu)
        self.assertIn("paired 1 of 2 sampled rows (50.00%)", text)
        self.assertIn("packet at the address not confirmed 1", text)
        self.assertIn("sampled 2, paired 1, equal 1, unequal 0", self.register_line(text, "2201"))
        # Any row that is not paired fails the run, whatever the paired ones say.
        self.assertEqual(code, 1, text)
        self.assertEqual(self.result(text), "RESULT: FAIL: paired 1 of 2 sampled rows (50.00%); 1 rows not paired")

    def test_a_packet_with_another_shader_and_other_constants_is_not_paired(self):
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100)]
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000),
               gpu_line(OFFSET + 1, 1, 0xFF500100, vs="0x1111111111111111", c0=[1.0, 2.0, 3.0, 4.0])]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 1, text)
        self.assertIn("paired 1 of 2 sampled rows (50.00%)", text)
        self.assertIn("packet at the address not confirmed 1", text)

    def test_the_vertex_shader_alone_confirms(self):
        # Other constants (or a row without them), the row's shader: confirmed.
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100, c0=None)]
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000, c0=[1.0, 2.0, 3.0, 4.0]), gpu_line(OFFSET + 1, 1, 0xFF500100)]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 0, text)
        self.assertIn("paired 2 of 2 sampled rows (100.00%)", text)
        self.assertIn("confirmed by index count and: vertex shader 2", text)

    def test_c0_alone_confirms(self):
        rows = [draw_row(1, 0xFF500000)]
        code, text = self.run_check(rows, [gpu_line(OFFSET + 1, 0, 0xFF500000, vs="0x1111111111111111")])
        self.assertEqual(code, 0, text)
        self.assertIn("confirmed by index count and: c0 1", text)

    def test_no_packet_and_skipped_packets_are_reported_and_fail(self):
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100), draw_row(1, 0xFF500200),
                draw_row(1, 0xFF500300)]
        del rows[3]["cmd"]
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000),
               f"S f={OFFSET + 1} a={rsc.gpu_address(0xFF500100) + 4:08X}"]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 1, text)
        self.assertIn("paired 1 of 4 sampled rows (25.00%)", text)
        self.assertIn("skipped by the bin predicate 1", text)
        self.assertIn("no packet at the address 1", text)
        self.assertIn("no command pointer 1", text)
        self.assertEqual(self.result(text), "RESULT: FAIL: paired 1 of 4 sampled rows (25.00%); 3 rows not paired")

    def test_the_packet_may_end_24_bytes_before_the_write_pointer(self):
        # Per-draw bin masks: seven more dwords follow the draw packet.
        rows = [draw_row(1, 0xFF500000)]
        code, text = self.run_check(rows, [gpu_line(OFFSET + 1, 0, 0xFF500000, delta=-24)])
        self.assertEqual(code, 0, text)
        self.assertIn("paired 1 of 1 sampled rows", text)

    def test_the_frame_offset_is_the_one_with_the_most_confirmed_packets(self):
        rows = [draw_row(f, 0xFF500000 + 0x40 * f, count=6 + f) for f in range(1, 6)]
        gpu = [gpu_line(OFFSET + f, 0, 0xFF500000 + 0x40 * f, n=6 + f) for f in range(1, 6)]
        # The command buffer is reused: frame 1's address holds another draw 3 frames later.
        gpu.append(gpu_line(OFFSET + 4, 1, 0xFF500000 + 0x40, n=99, r2201=0x07060706))
        gpu.append(gpu_line(OFFSET + 5, 1, 0xFF500000 + 0x80, n=8, r2201=0x07060706))
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 0, text)
        self.assertIn(f"GPU frame = capture frame + {OFFSET}", text)
        self.assertIn("paired 5 of 5 sampled rows", text)

    def test_the_control_counts_rows_whose_neighbour_would_also_be_confirmed(self):
        # Index count and c0 are shared by neighbouring draws (terrain patches): the address is
        # what identifies a pair. The control says how often the confirmation alone would not.
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100), draw_row(1, 0xFF500200, count=12),
                draw_row(2, 0xFF600000)]
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000), gpu_line(OFFSET + 1, 1, 0xFF500100, r2201=0x01060106),
               gpu_line(OFFSET + 1, 2, 0xFF500200, n=12, r2201=0x01060106), gpu_line(OFFSET + 2, 0, 0xFF600000)]
        rows[1]["rs"]["2201"] = rows[2]["rs"]["2201"] = "0x01060106"
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 0, text)
        # Rows 1 and 2 have a next row in their frame; row 1's neighbour passes the test, row 2's
        # (other index count) does not; row 1 differs from its neighbour in one register, row 2
        # holds the same words as its neighbour.
        self.assertIn("control: the next sampled row's packet also passes the confirmation test for 1 of 2 rows",
                      text)
        self.assertIn("control: compared with the next sampled row's colour-pass executions instead of their "
                      "own, 1 of 2 rows differ in a register", text)

    def test_tessellated_builders_have_their_own_index_count(self):
        consts = {"c0": C0}
        tess_a = {"kind": "tess", "frame": 1, "func": "DrawIndx:8221C9C8",
                  "args": ["0x00000012", "0x00000007", "0x00000004", "0x00000000"], "in_scene": True,
                  "rs": rs(), "cmd": "0xFF500000", "vs": {"hash": VS}, "ps_hash": PS, "consts": consts}
        tess_b = {"kind": "tess", "frame": 1, "func": "DrawIndx:82207C30",
                  "args": ["0x00000012", "0x00000000", "0x00000100", "0x00000003"], "in_scene": True,
                  "rs": rs(), "cmd": "0xFF500100", "vs": {"hash": VS}, "ps_hash": PS, "consts": consts}
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000, n=4), gpu_line(OFFSET + 1, 1, 0xFF500100, n=12)]
        code, text = self.run_check([tess_a, tess_b], gpu)
        self.assertEqual(code, 0, text)
        self.assertIn("paired 2 of 2 sampled rows", text)
        # 8221C9C8 draws r6 patches, 82207C30 r7 * 4 tessellation factors.
        code, text = self.run_check([tess_a, tess_b],
                                    [gpu_line(OFFSET + 1, 0, 0xFF500000, n=16),
                                     gpu_line(OFFSET + 1, 1, 0xFF500100, n=3)])
        self.assertIn("paired 0 of 2 sampled rows", text)


class VertsRowTest(Base):
    """A "verts" row is the decoded row of a BeginVertices draw (the shadow-mask rectangles): it
    is paired and compared like the others; its "cmd" is the builder's own write pointer."""

    def verts(self, cmd=0xFF500000, count=3, **over):
        return {"kind": "verts", "frame": 1, "func": "D3DDevice_BeginVertices?", "prim": 8, "count": count,
                "args": ["0x00000008", f"0x{count:08X}", "0x00000010", "0x00000008"], "in_scene": True,
                "rs": rs(**over), "cmd": f"0x{cmd:08X}", "vs": {"hash": VS}, "ps_hash": PS,
                "consts": {"c0": C0}}

    def test_the_index_count_is_the_vertex_count(self):
        self.assertEqual(rsc.expected_count("D3DDevice_BeginVertices?", [8, 3, 16, 8]), 3)
        self.assertEqual(rsc.expected_count("D3DDevice_BeginVertices?", [13, 224, 4, 0x110]), 224)

    def test_a_verts_row_is_paired_and_compared(self):
        code, text = self.run_check([self.verts()], [gpu_line(OFFSET + 1, 0, 0xFF500000, n=3, bs=0x2, bm=0x2)])
        self.assertEqual(code, 0, text)
        self.assertIn("rows 1, sampled 1, not sampled 0", text)
        self.assertIn("paired 1 of 1 sampled rows", text)
        self.assertIn("sampled 1, paired 1, equal 1, unequal 0", self.register_line(text, "210D"))
        # With per-draw bin masks seven more dwords follow the packet, as for the other builders.
        code, text = self.run_check([self.verts()],
                                    [gpu_line(OFFSET + 1, 0, 0xFF500000, n=3, bs=0x2, bm=0x2, delta=-24)])
        self.assertEqual(code, 0, text)

    def test_a_verts_row_with_another_stencil_reference_fails(self):
        code, text = self.run_check([self.verts()],
                                    [gpu_line(OFFSET + 1, 0, 0xFF500000, n=3, bs=0x2, bm=0x2, r210D=0x00FFFF2B)])
        self.assertEqual(code, 1, text)
        self.assertRegex(text, r"mismatch 210D: frame 1 .*D3DDevice_BeginVertices\?.*device 0x00FFFF2A, "
                               r"GPU 0x00FFFF2B")

    def test_a_verts_row_with_another_vertex_count_is_not_paired(self):
        code, text = self.run_check([self.verts(count=4)], [gpu_line(OFFSET + 1, 0, 0xFF500000, n=3)])
        self.assertEqual(code, 1, text)
        self.assertIn("paired 0 of 1 sampled rows", text)


class PrePassTest(Base):
    """A bin select without a colour bin (0x2AAAAAAA) is a depth pre-pass: there the D3D library
    forces RB_COLOR_MASK to 0, which is the one accepted difference."""

    def test_the_pre_pass_colour_mask_of_zero_is_reported_apart_and_passes(self):
        rows = [draw_row(1, 0xFF500000)]
        code, text = self.run_check(rows, tiled(OFFSET + 1, 10, 0xFF500000))
        self.assertEqual(code, 0, text)
        line = self.register_line(text, "2104")
        self.assertIn("sampled 1, paired 1, equal 1, unequal 0, no colour execution 0;", line)
        self.assertIn("colour executions 2, equal 2, unequal 0;", line)
        self.assertIn("pre-pass executions 1, equal 0, unequal 0, GPU 0 against another device value 1", line)
        line = self.register_line(text, "2200")
        self.assertIn("colour executions 2, equal 2, unequal 0;", line)
        self.assertTrue(line.endswith("pre-pass executions 1, equal 1, unequal 0"), line)
        self.assertNotIn("mismatch", text)
        self.assertEqual(self.result(text),
                         "RESULT: OK: paired 1 of 1 sampled rows (100.00%); every register equal in every "
                         "colour-pass execution (2) and every pre-pass execution (1) but RB_COLOR_MASK, "
                         "which the pre-pass has at 0 against another device value in 1")
        # The per-pass table separates the pre-pass bin from the colour bins.
        self.assertRegex(text, r"pre-pass +pitch 1120 bin select 80000001 mode 4 shader other +executions 1 +"
                               r"differing 2104: 1")
        self.assertRegex(text, r"colour +pitch 1120 bin select 00000002 mode 4 shader own +executions 1 +differing -")

    def test_an_untiled_pass_is_a_colour_pass(self):
        self.assertFalse(rsc.is_pre_pass(0xFFFFFFFF))
        self.assertTrue(rsc.is_pre_pass(0x80000001))
        for colour in (0x80000003, 0x0000000C, 0x00000002, 0x00000008):
            self.assertFalse(rsc.is_pre_pass(colour))

    def test_a_colour_pass_colour_mask_of_zero_fails(self):
        # The exemption is the pre-pass's alone.
        rows = [draw_row(1, 0xFF500000)]
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000, bs=0x80000003, r2104=0)]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 1, text)
        self.assertIn("equal 0, unequal 1, no colour execution 0;", self.register_line(text, "2104"))
        self.assertIn("unequal in a colour-pass execution: 2104", self.result(text))

    def test_another_register_unequal_in_the_pre_pass_fails(self):
        for reg in rsc.REGISTERS:
            if reg == "2104":
                continue
            with self.subTest(register=reg):
                rows = [draw_row(1, 0xFF500000)]
                gpu = tiled(OFFSET + 1, 10, 0xFF500000, **{"r" + reg: HIGH_BIT_DIFFERENCE[reg]})
                code, text = self.run_check(rows, gpu)
                self.assertEqual(code, 1, text)
                line = self.register_line(text, reg)
                self.assertIn("equal 1, unequal 0, no colour execution 0;", line)  # the row's colour passes
                self.assertIn("pre-pass executions 1, equal 0, unequal 1", line)
                self.assertRegex(text, rf"mismatch {reg}: frame 1 \(GPU frame 101, draw 10\)")
                self.assertEqual(self.result(text), "RESULT: FAIL: paired 1 of 1 sampled rows (100.00%); "
                                                    f"unequal in a pre-pass execution: {reg}")

    def test_a_pre_pass_colour_mask_other_than_zero_fails(self):
        # The pre-pass writes no colour: any other GPU value there is a finding, also the
        # device's own.
        for gpu_mask in (0x00000007, 0x0000000F, 0x00010000):
            with self.subTest(gpu_mask=gpu_mask):
                rows = [draw_row(1, 0xFF500000)]
                code, text = self.run_check(rows, tiled(OFFSET + 1, 10, 0xFF500000, r2104=gpu_mask))
                self.assertEqual(code, 1, text)
                self.assertIn("pre-pass executions 1, equal 0, unequal 1, GPU 0 against another device value 0",
                              self.register_line(text, "2104"))
                self.assertRegex(text, r"mismatch 2104: frame 1 \(GPU frame 101, draw 10\).*"
                                       rf"device 0x0000000F, GPU 0x{gpu_mask:08X}")
                self.assertIn("pre-pass RB_COLOR_MASK not 0 in 1 executions", self.result(text))

    def test_a_device_mask_of_zero_is_equal_in_the_pre_pass(self):
        rows = [draw_row(1, 0xFF500000, state=rs(r2104=0))]
        gpu = [gpu_line(OFFSET + 1, 10, 0xFF500000, bs=PRE_PASS, r2104=0),
               gpu_line(OFFSET + 1, 500, 0xFF500000, bs=0x2, r2104=0)]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 0, text)
        self.assertIn("pre-pass executions 1, equal 1, unequal 0, GPU 0 against another device value 0",
                      self.register_line(text, "2104"))

    def test_a_row_with_no_colour_execution_is_not_equal(self):
        rows = [draw_row(1, 0xFF500000, state=rs(bin_mask=PRE_PASS_BINS)), draw_row(1, 0xFF500100)]
        gpu = [gpu_line(OFFSET + 1, 10, 0xFF500000, bs=PRE_PASS, bm=PRE_PASS_BINS, mode=5, r2104=0)]
        gpu += tiled(OFFSET + 1, 20, 0xFF500100)
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 0, text)
        for reg in rsc.REGISTERS:
            self.assertIn("sampled 2, paired 2, equal 1, unequal 0, no colour execution 1;",
                          self.register_line(text, reg))
        self.assertIn("rows with no colour-pass execution: 1", text)
        # By builder and shader.
        self.assertRegex(text, rf"no colour execution: D3DDevice_DrawIndexedVertices\? vs {VS} ps {PS} +1\b")


class BinMaskRuleTest(Base):
    """A draw in the tiled main scene is drawn in colour when its device bin mask has a colour
    bin (0x2AAAAAAA); outside the main scene the bin select is all ones and it always is."""

    def rule(self, text):
        lines = [ln.strip() for ln in text.splitlines() if ln.strip().startswith("colour-pass rule:")]
        self.assertEqual(len(lines), 1, text)
        return lines[0]

    def scene(self, wrong=None):
        rows = [draw_row(1, 0xFF500000),                                     # pre-pass and colour
                draw_row(1, 0xFF500100, state=rs(bin_mask=PRE_PASS_BINS)),   # pre-pass only
                draw_row(1, 0xFF500200, state=rs(bin_mask=COLOUR_BINS)),     # colour only
                draw_row(1, 0xFF500300, state=rs(bin_mask=0x00030002)),      # one colour tile
                draw_row(1, 0xFF500400, state=rs(bin_mask=PRE_PASS_BINS), in_scene=False)]  # untiled pass
        gpu = tiled(OFFSET + 1, 10, 0xFF500000)
        gpu += tiled(OFFSET + 1, 11, 0xFF500100, colour=(wrong == "pre-pass mask drawn in colour"),
                     vs=VS, ps=PS, bm=PRE_PASS_BINS, mode=5)
        gpu += tiled(OFFSET + 1, 12, 0xFF500200, pre_pass=(wrong == "colour mask not drawn in colour"),
                     colour=(wrong != "colour mask not drawn in colour"))
        gpu += [gpu_line(OFFSET + 1, 13, 0xFF500300, bs=0x80000003, bm=0x00030002)]
        gpu += [gpu_line(OFFSET + 1, 14, 0xFF500400, pitch=280, bm=PRE_PASS_BINS)]
        return rows, gpu

    def test_the_rule_classifies_every_row(self):
        code, text = self.run_check(*self.scene())
        self.assertEqual(code, 0, text)
        self.assertEqual(self.rule(text),
                         "colour-pass rule: predicted colour and has a colour execution 4, predicted colour and "
                         "has none 0, predicted pre-pass only and has a colour execution 0, predicted pre-pass "
                         "only and has none 1, rows without a bin mask 0")
        self.assertIn("rows with no colour-pass execution: 1", text)
        # The table behind it: main scene or not, the device's mask, the bin selects executed.
        self.assertRegex(text, r"main scene +bin mask 15555555 +bin selects 80000001 +1\b")
        self.assertRegex(text, r"main scene +bin mask 2AAAAAAA +bin selects 00000002 00000008 +1\b")
        self.assertRegex(text, r"main scene +bin mask FFFFFFFF +bin selects 80000001 00000002 00000008 +1\b")
        self.assertRegex(text, r"main scene +bin mask 00030002 +bin selects 80000003 +1\b")
        self.assertRegex(text, r"other passes +bin mask 15555555 +bin selects FFFFFFFF +1\b")

    def test_a_pre_pass_mask_drawn_in_colour_breaks_the_rule(self):
        code, text = self.run_check(*self.scene("pre-pass mask drawn in colour"))
        self.assertEqual(code, 1, text)
        self.assertIn("predicted pre-pass only and has a colour execution 1, predicted pre-pass only and has "
                      "none 0", self.rule(text))
        self.assertIn("colour-pass rule wrong for 1 rows", self.result(text))

    def test_a_colour_mask_not_drawn_in_colour_breaks_the_rule(self):
        code, text = self.run_check(*self.scene("colour mask not drawn in colour"))
        self.assertEqual(code, 1, text)
        self.assertIn("predicted colour and has a colour execution 3, predicted colour and has none 1",
                      self.rule(text))
        self.assertIn("colour-pass rule wrong for 1 rows", self.result(text))

    def test_rows_without_a_bin_mask_are_not_classified(self):
        rows = [draw_row(1, 0xFF500000, state=rs(bin_mask=None))]
        code, text = self.run_check(rows, tiled(OFFSET + 1, 10, 0xFF500000, colour=False))
        self.assertEqual(code, 0, text)
        self.assertTrue(self.rule(text).endswith("rows without a bin mask 1"), text)
        self.assertIn("predicted colour and has none 0", self.rule(text))

    def test_the_high_word_of_the_mask_is_shown_when_set(self):
        state = rs(bin_mask=COLOUR_BINS)
        state["bin_mask_hi"] = "0x00000001"
        rows = [draw_row(1, 0xFF500000, state=state)]
        code, text = self.run_check(rows, tiled(OFFSET + 1, 10, 0xFF500000, pre_pass=False))
        self.assertIn("rows with a non-zero high word of the bin mask: 1", text)


class CensusTest(Base):
    def census(self, rows):
        gpu = [gpu_line(OFFSET + r["frame"], i, int(r["cmd"], 16),
                        regs={k: int(v, 16) for k, v in r["rs"].items() if k in rsc.REGISTERS})
               for i, r in enumerate(rows)]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 0, text)
        return text

    def test_alpha_test_function(self):
        rows = [draw_row(1, 0xFF500000, state=rs(r2202=0x0000000C, r210E=f32(0.5))),
                draw_row(1, 0xFF500100, state=rs(r2202=0x00000004)),
                draw_row(1, 0xFF500200, state=rs(r2202=0xAA000004))]
        text = self.census(rows)
        self.assertRegex(text, r"alpha test on, function 4 \(greater\) +1\b")
        self.assertRegex(text, r"alpha test off +2\b")
        self.assertRegex(text, r"alpha reference 0\.5 +1\b")

    def test_polygon_offset_by_shader(self):
        # Bits 11 and 12 of PA_SU_SC_MODE_CNTL enable the offset for front and back faces.
        both = rs(r2205=0x00219806, r2104=0)
        front = rs(r2205=0x00218806, r2380=f32(4.0), r2381=f32(-0.001))
        back = rs(r2205=0x00219006)
        rows = [draw_row(1, 0xFF500000, state=both, vs="0xAAAAAAAAAAAAAAAA", ps="0xBBBBBBBBBBBBBBBB",
                         in_scene=False),
                draw_row(1, 0xFF500100, state=front, in_scene=False),
                draw_row(1, 0xFF500200, state=back, in_scene=False),
                draw_row(1, 0xFF500300)]
        text = self.census(rows)
        self.assertRegex(text, r"polygon offset front scale 2 offset 0\.0005, back scale 1\.5 offset 0\.00025 +1\n"
                               r" +vs 0xAAAAAAAAAAAAAAAA ps 0xBBBBBBBBBBBBBBBB +1\b")
        self.assertRegex(text, r"polygon offset front scale 4 offset -0\.001 +1\b")
        self.assertRegex(text, r"polygon offset back scale 1\.5 offset 0\.00025 +1\b")
        # Off: the registers are shown all the same (they are flushed whatever the enables say).
        self.assertRegex(text, r"polygon offset off \(registers: front 2 0\.0005, back 1\.5 0\.00025\) +1\b")
        self.assertEqual(rsc.decode_polygon_offset(0x00218006, 0, 0, 0, 0), "polygon offset off (registers: "
                                                                            "front 0 0, back 0 0)")

    def test_stencil_state_and_reference(self):
        # 0x00708765: stencil on, always, pass replace, depth test off; 0x00700565: not-equal, keep.
        rows = [draw_row(1, 0xFF500000, state=rs(r2200=0x00708765)),
                draw_row(1, 0xFF500100, state=rs(r2200=0x00700565, r210D=0x00FF0101)),
                draw_row(1, 0xFF500200, state=rs(r2200=0x00708766))]
        text = self.census(rows)
        self.assertRegex(text, r"stencil on: function 7 \(always\), fail keep, pass replace, depth fail keep; "
                               r"reference 42, mask 0xFF, write mask 0xFF +1\b")
        self.assertRegex(text, r"stencil on: function 5 \(notequal\), fail keep, pass keep, depth fail keep; "
                               r"reference 1, mask 0x01, write mask 0xFF +1\b")
        self.assertRegex(text, r"stencil off \(reference word 0x00FFFF2A\) +1\b")
        # Two-sided stencil (bit 7): the back faces' functions and their own reference word.
        self.assertEqual(rsc.decode_stencil(0x287087E5, 0x00FFFF2A, 0x000F0703),
                         "stencil on: function 7 (always), fail keep, pass replace, depth fail keep; reference 42, "
                         "mask 0xFF, write mask 0xFF; back faces: function 7 (always), fail keep, pass replace, "
                         "depth fail zero; reference 3, mask 0x07, write mask 0x0F")

    def test_alpha_to_mask_is_its_own_state(self):
        text = self.census([draw_row(1, 0xFF500000, state=rs(r2202=0x00000014))])
        self.assertRegex(text, r"alpha test off, alpha to mask +1\b")

    def test_blend_depth_cull_and_mask(self):
        blended = rs(r2201=0x07060706, r2200=0x00708762, r2205=0x00018005, r2104=0x00000007)
        rows = [draw_row(1, 0xFF500000, state=blended, vs="0xAAAAAAAAAAAAAAAA", ps="0xBBBBBBBBBBBBBBBB"),
                draw_row(1, 0xFF500100)]
        text = self.census(rows)
        self.assertRegex(text, r"blend colour src_alpha add inv_src_alpha, alpha src_alpha add inv_src_alpha +1\b")
        self.assertRegex(text, r"blend off \(one add zero\) +1\b")
        self.assertRegex(text, r"depth test on, write off, function 6 \(gequal\) +1\b")
        self.assertRegex(text, r"depth test on, write on, function 6 \(gequal\) +1\b")
        self.assertRegex(text, r"cull front, front face cw +1\b")
        self.assertRegex(text, r"cull back, front face cw +1\b")
        self.assertRegex(text, r"colour mask RGB- +1\b")
        self.assertRegex(text, r"colour mask RGBA +1\b")
        # Counts by vertex and pixel shader.
        self.assertRegex(text, r"blend colour src_alpha add inv_src_alpha.*\n +vs 0xAAAAAAAAAAAAAAAA "
                               r"ps 0xBBBBBBBBBBBBBBBB +1\b")
        self.assertIn("blend constant used by 0 of 2 rows", text)

    def test_blend_constant_use_is_counted_for_each_of_the_four_factors(self):
        # Constant colour, inverse constant colour, constant alpha, inverse constant alpha as the
        # colour source, colour destination, alpha source and alpha destination factor in turn.
        for word in (0x0001000C, 0x00010D01, 0x000E0001, 0x0F010001):
            with self.subTest(word=f"0x{word:08X}"):
                text = self.census([draw_row(1, 0xFF500000, state=rs(r2201=word)), draw_row(1, 0xFF500100)])
                self.assertIn("blend constant used by 1 of 2 rows", text)

    def test_unknown_blend_factor_is_shown_by_number(self):
        text = self.census([draw_row(1, 0xFF500000, state=rs(r2201=0x00010011))])
        self.assertRegex(text, r"blend colour factor17 add zero, alpha one add zero +1\b")

    def test_depth_range(self):
        rows = [draw_row(1, 0xFF500000),                                               # scale -1, offset 1
                draw_row(1, 0xFF500100, state=rs(r2113=f32(1.0), r2114=f32(0.0))),      # scale 1, offset 0
                draw_row(1, 0xFF500200, state=rs(r2206=0x0000040F)),                    # Z transform off
                draw_row(1, 0xFF500300, state=rs(r2113=f32(-0.5), r2114=f32(0.5))),
                draw_row(1, 0xFF500400, state=rs(r2113=f32(0.5), r2114=f32(0.5))),       # offset alone: not reversed
                draw_row(1, 0xFF500500, state=rs(r2113=f32(-1.0), r2114=f32(0.0))),      # scale alone: reversed
                draw_row(1, 0xFF500600, state=rs(r2206=0x0000042F)),                     # scale -1 not enabled
                draw_row(1, 0xFF500700, state=rs(r2206=0x0000041F))]                     # offset 1 not enabled
        text = self.census(rows)
        self.assertRegex(text, r"depth range not reversed: z scale 0\.5, offset 0\.5 +1\b")
        self.assertRegex(text, r"depth range reversed: z scale -1, offset 0 +2\b")
        self.assertRegex(text, r"depth range not reversed: z scale 1, offset 1 +1\b")
        self.assertRegex(text, r"depth range reversed: z scale -1, offset 1 +1\b")
        self.assertRegex(text, r"depth range reversed: z scale -0\.5, offset 0\.5 +1\b")
        self.assertRegex(text, r"depth range not reversed: z scale 1, offset 0 +1\b")
        self.assertRegex(text, r"depth range not reversed: z scale and offset disabled +1\b")

    def test_rows_outside_the_main_scene_are_counted_apart(self):
        rows = [draw_row(1, 0xFF500000, state=rs(r2104=0), in_scene=False), draw_row(1, 0xFF500100)]
        text = self.census(rows)
        main, other = text.split("census, other passes")
        self.assertRegex(main, r"colour mask RGBA +1\b")
        self.assertNotIn("colour mask ----", main)
        self.assertRegex(other, r"colour mask ---- +1\b")

    def test_state_combinations_list_the_raw_words(self):
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100),
                draw_row(1, 0xFF500200, state=rs(r2202=0x0000000C))]
        text = self.census(rows)
        self.assertRegex(text, r"2200=0x00708767 2201=0x00010001 2202=0x8700001C 2205=0x00218006 "
                               r"2104=0x0000000F +2\b")
        self.assertRegex(text, r"2200=0x00708767 2201=0x00010001 2202=0x0000000C 2205=0x00218006 "
                               r"2104=0x0000000F +1\b")


if __name__ == "__main__":
    unittest.main()
