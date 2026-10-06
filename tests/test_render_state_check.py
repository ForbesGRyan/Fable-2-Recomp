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
# An opaque draw as the bridge scene has it: colour mask RGBA, depth test and write with
# GreaterEqual, no blending, no alpha test, cull back.
OPAQUE = {"2104": 0x0000000F, "210E": 0x00000000, "2200": 0x00708766, "2201": 0x00010001,
          "2202": 0x00000004, "2205": 0x00218006, "2105": 0x3F800000, "2106": 0x3F800000,
          "2107": 0x3F800000, "2108": 0x3F800000}
C0 = [0.5, -0.25, 0.0, 92.5]
OFFSET = 100  # GPU frame = capture frame + OFFSET in these logs


def f32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def rs(regs=OPAQUE, **over):
    out = {k: f"0x{v:08X}" for k, v in regs.items()}
    out.update({k[1:]: f"0x{v:08X}" for k, v in over.items()})  # rs(r2201=...) overrides
    return out


def draw_row(frame, cmd, count=6, state=None, vs=VS, ps=PS, c0=C0, in_scene=True, with_rs=True):
    row = {"kind": "draw", "frame": frame, "func": "D3DDevice_DrawIndexedVertices?",
           "args": ["0x00000004", "0x00000000", "0x00000000", f"0x{count:08X}"], "in_scene": in_scene,
           "cmd": f"0x{cmd:08X}", "ps_hash": ps, "vs_hash": vs, "bank": list(c0) + [0.0] * 12}
    if with_rs:
        row["rs"] = rs() if state is None else state
    return row


def gpu_line(frame, d, cmd, n=6, regs=OPAQUE, vs=VS, ps=PS, c0=C0, delta=4, bs=0xFFFFFFFF, bm=0xFFFFFFFF,
             mode=4, pitch=1120, **over):
    r = dict(regs)
    r.update({k[1:]: v for k, v in over.items()})
    words = " ".join(f"r{k}={v:08X}" for k, v in r.items())
    return (f"D f={frame} d={d} a={rsc.gpu_address(cmd) + delta:08X} pt=4 n={n} src=0 k=0 e=0 bs={bs:016X} "
            f"bm={bm:016X} vs={int(vs, 16):016X} ps={int(ps, 16):016X} pitch={pitch} mode={mode} "
            f"ioff=00000000 dma=1B000000 c0={','.join(f'{f32(x):08X}' for x in c0)} {words}")


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


class AddressTest(unittest.TestCase):
    def test_gpu_address_matches_the_capture_conversion(self):
        # xdk_layout.h GpuAddress: the 0xE0000000 view is offset by 0x1000.
        self.assertEqual(rsc.gpu_address(0xE0000000), 0x00001000)
        self.assertEqual(rsc.gpu_address(0xDFFFF000), 0x1FFFF000)
        self.assertEqual(rsc.gpu_address(0xA0001000), 0x00001000)
        self.assertEqual(rsc.gpu_address(0xFF4FE848), 0x1F4FF848)


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
            self.assertIn("sampled 3, paired 3, equal 3, unequal 0", line)
        self.assertIn("RESULT: every register equal in every paired draw", text)

    def test_mismatch_in_blend_control_exits_one_and_is_listed(self):
        gpu = self.gpu()
        gpu[1] = gpu_line(OFFSET + 1, 1, 0xFF500100, n=12, r2201=0x07060706)
        code, text = self.run_check(self.rows(), gpu)
        self.assertEqual(code, 1, text)
        self.assertIn("sampled 3, paired 3, equal 2, unequal 1", self.register_line(text, "2201"))
        self.assertIn("sampled 3, paired 3, equal 3, unequal 0", self.register_line(text, "2200"))
        # The mismatch names the capture frame, the GPU frame and the GPU draw ordinal.
        self.assertRegex(text, r"mismatch 2201: frame 1 \(GPU frame 101, draw 1\).*device 0x00010001.*"
                               r"GPU 0x07060706")
        self.assertIn("RESULT: UNEQUAL registers: 2201", text)

    def test_only_the_first_five_mismatches_of_a_register_are_listed(self):
        rows = [draw_row(1, 0xFF500000 + 0x100 * i) for i in range(8)]
        gpu = [gpu_line(OFFSET + 1, i, 0xFF500000 + 0x100 * i, r2200=0x00708756) for i in range(8)]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 1)
        self.assertIn("equal 0, unequal 8", self.register_line(text, "2200"))
        self.assertEqual(text.count("mismatch 2200:"), 5)

    def test_rows_without_rs_are_not_sampled_and_never_equal(self):
        rows = self.rows()
        rows[2] = draw_row(2, 0xFF600000, with_rs=False)
        code, text = self.run_check(rows, self.gpu())
        self.assertEqual(code, 0, text)
        self.assertIn("rows 3, sampled 2, not sampled 1", text)
        self.assertIn("sampled 2, paired 2, equal 2, unequal 0", self.register_line(text, "2104"))

    def test_a_register_missing_from_rs_is_not_sampled_for_that_register(self):
        rows = self.rows()
        del rows[0]["rs"]["210E"]
        code, text = self.run_check(rows, self.gpu())
        self.assertEqual(code, 0, text)
        self.assertIn("sampled 2, paired 2, equal 2, unequal 0", self.register_line(text, "210E"))
        self.assertIn("sampled 3, paired 3, equal 3, unequal 0", self.register_line(text, "2104"))

    def test_no_paired_row_is_not_a_proof(self):
        code, text = self.run_check(self.rows(), [gpu_line(OFFSET + 1, 0, 0xFF700000)])
        self.assertEqual(code, 1, text)
        self.assertIn("RESULT: no draw was paired", text)


class PairingTest(Base):
    def test_a_packet_with_another_index_count_is_not_paired(self):
        # Same address, same registers, other index count: not the capture's draw.
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100, count=12)]
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000), gpu_line(OFFSET + 1, 1, 0xFF500100, n=13)]
        code, text = self.run_check(rows, gpu)
        self.assertIn("paired 1 of 2 sampled rows (50.00%)", text)
        self.assertIn("packet at the address not confirmed 1", text)
        self.assertIn("sampled 2, paired 1, equal 1, unequal 0", self.register_line(text, "2201"))
        self.assertEqual(code, 0, text)

    def test_a_packet_with_another_shader_and_other_constants_is_not_paired(self):
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100)]
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000),
               gpu_line(OFFSET + 1, 1, 0xFF500100, vs="0x1111111111111111", c0=[1.0, 2.0, 3.0, 4.0])]
        code, text = self.run_check(rows, gpu)
        self.assertIn("paired 1 of 2 sampled rows (50.00%)", text)
        self.assertIn("packet at the address not confirmed 1", text)

    def test_no_packet_and_skipped_packets_are_reported(self):
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100), draw_row(1, 0xFF500200),
                draw_row(1, 0xFF500300)]
        del rows[3]["cmd"]
        gpu = [gpu_line(OFFSET + 1, 0, 0xFF500000),
               f"S f={OFFSET + 1} a={rsc.gpu_address(0xFF500100) + 4:08X}"]
        code, text = self.run_check(rows, gpu)
        self.assertIn("paired 1 of 4 sampled rows (25.00%)", text)
        self.assertIn("skipped by the bin predicate 1", text)
        self.assertIn("no packet at the address 1", text)
        self.assertIn("no command pointer 1", text)

    def test_the_packet_may_end_28_bytes_before_the_write_pointer(self):
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

    def test_every_execution_of_a_tiled_draw_is_compared(self):
        # One guest draw, three executions: a depth pre-pass with a substituted shader and no
        # colour writes, then two colour tiles. Confirmed by index count and c0.
        rows = [draw_row(1, 0xFF500000)]
        gpu = [gpu_line(OFFSET + 1, 10, 0xFF500000, vs="0xC47236E5A9B00356", ps="0x37CAF82F552893EE",
                        bs=0x80000001, r2104=0),
               gpu_line(OFFSET + 1, 500, 0xFF500000, bs=0x2, bm=0x8000000F),
               gpu_line(OFFSET + 1, 900, 0xFF500000, bs=0x8, bm=0x8000000F)]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 1, text)
        self.assertIn("executions 3", text)
        line = self.register_line(text, "2104")
        self.assertIn("sampled 1, paired 1, equal 0, unequal 1", line)
        self.assertIn("executions 3, equal 2, unequal 1", line)
        self.assertIn("executions 3, equal 3, unequal 0", self.register_line(text, "2200"))
        self.assertRegex(text, r"mismatch 2104: frame 1 \(GPU frame 101, draw 10\)")
        # The per-pass table separates the pre-pass bin from the colour bins.
        self.assertRegex(text, r"pitch 1120 bin select 80000001 mode 4 shader other +executions 1 +"
                               r"unequal 2104: 1")
        self.assertRegex(text, r"pitch 1120 bin select 00000002 mode 4 shader own +executions 1 +unequal -")

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


class CensusTest(Base):
    def census(self, rows):
        gpu = [gpu_line(OFFSET + r["frame"], i, int(r["cmd"], 16), regs={k: int(v, 16) for k, v in r["rs"].items()})
               for i, r in enumerate(rows)]
        code, text = self.run_check(rows, gpu)
        self.assertEqual(code, 0, text)
        return text

    def test_alpha_test_function(self):
        rows = [draw_row(1, 0xFF500000, state=rs(r2202=0x0000000C, r210E=f32(0.5))),
                draw_row(1, 0xFF500100), draw_row(1, 0xFF500200)]
        text = self.census(rows)
        self.assertRegex(text, r"alpha test on, function 4 \(greater\) +1\b")
        self.assertRegex(text, r"alpha test off +2\b")
        self.assertRegex(text, r"alpha reference 0\.5 +1\b")

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

    def test_blend_constant_use_is_counted(self):
        # Colour source factor 12 (constant colour).
        text = self.census([draw_row(1, 0xFF500000, state=rs(r2201=0x0001000C)), draw_row(1, 0xFF500100)])
        self.assertIn("blend constant used by 1 of 2 rows", text)

    def test_unknown_blend_factor_is_shown_by_number(self):
        text = self.census([draw_row(1, 0xFF500000, state=rs(r2201=0x00010011))])
        self.assertRegex(text, r"blend colour factor17 add zero, alpha one add zero +1\b")

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
        self.assertRegex(text, r"2200=0x00708766 2201=0x00010001 2202=0x00000004 2205=0x00218006 "
                               r"2104=0x0000000F +2\b")
        self.assertRegex(text, r"2200=0x00708766 2201=0x00010001 2202=0x0000000C 2205=0x00218006 "
                               r"2104=0x0000000F +1\b")


if __name__ == "__main__":
    unittest.main()
