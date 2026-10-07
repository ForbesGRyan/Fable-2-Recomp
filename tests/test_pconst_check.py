import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import pconst_check as pc  # noqa: E402
import render_state_check as rsc  # noqa: E402

VS = "0xECD66A10092E6562"
PS = "0x8D900846800943C8"
C0 = [0.5, -0.25, 0.0, 92.5]  # vertex constant c0 (the pairing's weak check)
OFFSET = 100  # GPU frame = capture frame + OFFSET in these logs
STATE = {"2104": "0x0000000F", "2200": "0x00708767", "bin_mask_lo": "0xFFFFFFFF", "bin_mask_hi": "0x00000000"}
PRE_PASS, TILE0, TILE1 = 0x80000001, 0x2, 0x8

# Pixel registers of a lit scene draw: the light block's eight and two others.
BASE = {
    0: (0.0, 0.0, 1.0 / 1120, 1.0 / 720),      # screen mask transform
    8: (1.0, 1.0, 0.0, 0.0),
    19: (82.07, 157.34, 50.17, 1.0),            # eye
    20: (0.0074, 0.0716, 0.0926, 16.0),         # additive light, output scale
    21: (1.1168, 1.3836, 1.8652, 1.3),          # tint, exponent
    22: (0.0588, 0.0902, 0.0, 0.0),             # floor
    23: (-0.619, 0.016, 0.785, 4.0),            # axis
    80: (-0.828, 0.022, 0.560, 0.0),            # sun direction
    87: (2.5, 2.25, 1.75, 0.0),                 # sun colour
    100: (0.25, 0.5, 0.75, 1.0),
}


def f32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def reg_hex(v):
    return "".join(f"{f32(x):08X}" for x in v)


def registers(over=None):
    regs = dict(BASE)
    regs.update(over or {})
    return regs


def pfull(regs, lack=()):
    return "".join("-" * 32 if r in lack else reg_hex(regs.get(r, (0.0, 0.0, 0.0, 0.0))) for r in range(256))


def tile(mask, start, values):
    return {"mask": f"0x{mask:08X}", "start": start, "v": "".join(reg_hex(v) for v in values)}


def draw_row(frame, cmd, count=6, over=None, lack=(), tiles=None, bank_diff=None, in_scene=True,
             pbool=0x100, with_pfull=True, vs=VS, ps=PS):
    row = {"kind": "draw", "frame": frame, "func": "D3DDevice_DrawIndexedVertices?",
           "args": ["0x00000004", "0x00000000", "0x00000000", f"0x{count:08X}"], "in_scene": in_scene,
           "rs": dict(STATE), "cmd": f"0x{cmd:08X}", "ps_hash": ps, "vs_hash": vs,
           "bank": list(C0) + [0.0] * 12, "pbool": f"0x{pbool:08X}"}
    if with_pfull:
        row["pfull"] = pfull(registers(over), lack)
    if tiles is not None:
        row["ptiles"] = tiles
    if bank_diff is not None:
        row["pbank_diff"] = {str(r): reg_hex(v) for r, v in bank_diff.items()}
    return row


def gpu_line(frame, d, cmd, regs=None, full=True, n=6, vs=VS, ps=PS, c0=C0, delta=4, bs=0xFFFFFFFF,
             bm=0xFFFFFFFF, mode=4, pitch=1120, pb=0x100):
    """One D line. full: all 256 registers (`regs` over BASE); otherwise only `regs` (the changes)."""
    if full:
        values = registers(regs)
        tokens = ["full=1"] + [f"P{r}={reg_hex(values.get(r, (0.0, 0.0, 0.0, 0.0)))}" for r in range(256)]
    else:
        tokens = [f"P{r}={reg_hex(v)}" for r, v in (regs or {}).items()]
    return (f"D f={frame} d={d} a={rsc.gpu_address(cmd) + delta:08X} pt=4 n={n} bs={bs:016X} "
            f"bm={bm:016X} vs={int(vs, 16):016X} ps={int(ps, 16):016X} pitch={pitch} mode={mode} "
            f"c0={','.join(f'{f32(x):08X}' for x in c0)} "
            f"b=00000000,00000000,00000000,00000000,{pb:08X},00000000,00000000,00000000 " + " ".join(tokens))


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
            code = pc.main([str(cap), str(gpu), *extra])
        return code, out.getvalue()

    def line(self, text, prefix):
        lines = [ln for ln in text.splitlines() if ln.startswith(prefix)]
        self.assertEqual(len(lines), 1, f"{prefix!r} in:\n{text}")
        return lines[0]

    def result(self, text):
        return self.line(text, "RESULT:")


def three_rows():
    return [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100, count=12), draw_row(2, 0xFF600000)]


def three_lines(**second):
    return [gpu_line(1 + OFFSET, 0, 0xFF500000),
            gpu_line(1 + OFFSET, 1, 0xFF500100, n=12, **second),
            gpu_line(2 + OFFSET, 0, 0xFF600000)]


class FormatTest(unittest.TestCase):
    def test_the_light_block_registers(self):
        # light_block.h: c0 mask transform, c19 eye, c20, c21, c22, c23, c80 sun direction, c87 sun colour,
        # c94 light mix, c109 mask axis, c112 shadow mix.
        self.assertEqual(pc.LIGHT_BLOCK, (0, 19, 20, 21, 22, 23, 80, 87, 94, 109, 112))

    def test_parse_registers_reads_256_and_marks_the_lacking_ones(self):
        regs = pc.parse_registers(pfull(BASE, lack=(87,)))
        self.assertEqual(len(regs), 256)
        self.assertEqual(regs[80], reg_hex(BASE[80]))
        self.assertIsNone(regs[87])
        self.assertEqual(regs[1], "0" * 32)

    def test_parse_registers_refuses_another_length(self):
        self.assertIsNone(pc.parse_registers("00" * 100))
        self.assertIsNone(pc.parse_registers(None))
        self.assertIsNone(pc.parse_registers("zz" * 4096))

    def test_floats_of_a_register(self):
        self.assertEqual(pc.floats(reg_hex((1.0, -0.5, 0.0, 16.0))), (1.0, -0.5, 0.0, 16.0))


class EqualityTest(Base):
    def test_all_equal_passes(self):
        code, text = self.run_check(three_rows(), three_lines())
        self.assertEqual(code, 0, text)
        self.assertIn("RESULT: OK", self.result(text))
        self.assertIn(f"frame offset: GPU frame = capture frame + {OFFSET}", text)
        self.assertIn("paired 3 of 3", self.line(text, "pairing:"))
        for reg in pc.LIGHT_BLOCK:
            ln = self.line(text, f"register c{reg} ")
            self.assertIn("compared 3, equal 3, different 0", ln)
            self.assertIn("[light block]", ln)
        self.assertNotIn("[light block]", self.line(text, "register c100 "))
        self.assertIn("compared 3, equal 3, different 0", self.line(text, "register c100 "))

    def test_one_light_block_register_different_in_one_draw_fails_and_is_named(self):
        changed = (2.5, 2.25, 1.7500001, 0.0)  # one bit of one component
        code, text = self.run_check(three_rows(), three_lines(regs={87: changed}))
        self.assertEqual(code, 1, text)
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "register c87 "))
        self.assertIn("c87", self.result(text))
        self.assertIn("RESULT: FAIL", self.result(text))
        mismatch = self.line(text, "mismatch c87:")
        self.assertIn(f"frame 1 (GPU frame {1 + OFFSET}, draw 1)", mismatch)
        self.assertIn("1.75", mismatch)
        # The other registers are untouched by it.
        self.assertIn("compared 3, equal 3, different 0", self.line(text, "register c80 "))

    def test_another_register_different_is_listed_but_does_not_fail(self):
        code, text = self.run_check(three_rows(), three_lines(regs={100: (9.0, 9.0, 9.0, 9.0)}))
        self.assertEqual(code, 0, text)
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "register c100 "))
        self.assertIn("c100", self.line(text, "registers different in a main-scene colour execution:"))
        self.assertIn("RESULT: OK", self.result(text))

    def test_bits_are_compared_not_values(self):
        # -0.0 against 0.0: equal as floats, different registers.
        code, text = self.run_check(three_rows(), three_lines(regs={22: (0.0588, 0.0902, -0.0, 0.0)}))
        self.assertEqual(code, 1, text)
        self.assertIn("different 1", self.line(text, "register c22 "))

    def test_a_row_outside_the_scene_is_compared_apart_and_never_decides(self):
        rows = three_rows() + [draw_row(2, 0xFF600100, in_scene=False)]
        lines = three_lines() + [gpu_line(2 + OFFSET, 1, 0xFF600100, regs={87: (0.0, 0.0, 0.0, 0.0)}, pitch=1040)]
        code, text = self.run_check(rows, lines)
        self.assertEqual(code, 0, text)
        ln = self.line(text, "register c87 ")
        self.assertIn("compared 3, equal 3, different 0", ln)
        self.assertIn("other passes compared 1, different 1", ln)


class PresenceTest(Base):
    def test_a_row_without_a_gpu_draw_is_reported_and_not_counted_as_equal(self):
        rows = three_rows() + [draw_row(1, 0xFF500200)]
        code, text = self.run_check(rows, three_lines())
        self.assertEqual(code, 1, text)
        self.assertIn("paired 3 of 4", self.line(text, "pairing:"))
        self.assertIn("no packet at the address 1", text)
        self.assertIn("compared 3, equal 3, different 0", self.line(text, "register c80 "))
        self.assertIn("1 rows not paired", self.result(text))

    def test_a_gpu_draw_without_a_row_is_reported(self):
        lines = three_lines() + [gpu_line(1 + OFFSET, 2, 0xFF500300, regs={87: (0.0, 0.0, 0.0, 0.0)})]
        code, text = self.run_check(three_rows(), lines)
        self.assertEqual(code, 0, text)
        self.assertIn("GPU draws in the paired frames: 4; at a sampled row's address 3; without a row 1", text)
        self.assertIn("compared 3, equal 3, different 0", self.line(text, "register c87 "))

    def test_a_packet_of_another_draw_at_the_address_is_not_paired(self):
        lines = three_lines()
        lines[1] = gpu_line(1 + OFFSET, 1, 0xFF500100, n=13)  # another index count
        code, text = self.run_check(three_rows(), lines)
        self.assertEqual(code, 1, text)
        self.assertIn("packet at the address not confirmed 1", text)
        self.assertIn("compared 2, equal 2, different 0", self.line(text, "register c80 "))

    def test_a_register_the_row_lacks_is_not_compared(self):
        rows = three_rows()
        rows[1] = draw_row(1, 0xFF500100, count=12, lack=(100,))
        code, text = self.run_check(rows, three_lines())
        self.assertEqual(code, 0, text)
        self.assertIn("compared 2, equal 2, different 0, not compared 1", self.line(text, "register c100 "))

    def test_a_light_block_register_lacking_in_a_row_fails(self):
        rows = three_rows()
        rows[1] = draw_row(1, 0xFF500100, count=12, lack=(80,))
        code, text = self.run_check(rows, three_lines())
        self.assertEqual(code, 1, text)
        self.assertIn("compared 2, equal 2, different 0, not compared 1", self.line(text, "register c80 "))
        self.assertIn("not read in a main-scene row: c80", self.result(text))

    def test_rows_without_the_key_are_not_sampled(self):
        rows = three_rows() + [draw_row(2, 0xFF600200, with_pfull=False)]
        code, text = self.run_check(rows, three_lines())
        self.assertEqual(code, 0, text)
        self.assertIn("rows 4, sampled 3, not sampled 1", self.line(text, "capture:"))

    def test_no_main_scene_row_compared_is_a_failure(self):
        rows = [draw_row(1, 0xFF500000, in_scene=False)]
        code, text = self.run_check(rows, [gpu_line(1 + OFFSET, 0, 0xFF500000)])
        self.assertEqual(code, 1, text)
        self.assertIn("no main-scene colour execution compared", self.result(text))

    def test_frames_outside_the_gpu_log_are_reported_apart(self):
        rows = three_rows() + [draw_row(3, 0xFF700000)]
        code, text = self.run_check(rows, three_lines())
        self.assertEqual(code, 0, text)
        self.assertIn("frame outside the GPU log 1", text)
        self.assertIn("1 rows in frames outside the GPU log were not compared", self.result(text))

    def test_frame_option_keeps_one_capture_frame(self):
        code, text = self.run_check(three_rows(), three_lines(regs={87: (0.0, 0.0, 0.0, 0.0)}), "--frame", "2")
        self.assertEqual(code, 0, text)
        self.assertIn("compared 1, equal 1, different 0", self.line(text, "register c87 "))


class DeltaTest(Base):
    def test_a_line_carries_only_the_registers_that_changed(self):
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100, over={8: (2.0, 2.0, 0.0, 0.0)}),
                draw_row(1, 0xFF500200, over={8: (2.0, 2.0, 0.0, 0.0)})]
        lines = [gpu_line(1 + OFFSET, 0, 0xFF500000),
                 gpu_line(1 + OFFSET, 1, 0xFF500100, regs={8: (2.0, 2.0, 0.0, 0.0)}, full=False),
                 gpu_line(1 + OFFSET, 2, 0xFF500200, full=False)]  # nothing changed
        code, text = self.run_check(rows, lines)
        self.assertEqual(code, 0, text)
        self.assertIn("compared 3, equal 3, different 0", self.line(text, "register c8 "))
        self.assertIn("compared 3, equal 3, different 0", self.line(text, "register c87 "))

    def test_a_change_persists_until_the_next_change(self):
        rows = [draw_row(1, 0xFF500000), draw_row(1, 0xFF500100), draw_row(1, 0xFF500200)]
        lines = [gpu_line(1 + OFFSET, 0, 0xFF500000),
                 gpu_line(1 + OFFSET, 1, 0xFF500100, regs={87: (1.0, 1.0, 1.0, 0.0)}, full=False),
                 gpu_line(1 + OFFSET, 2, 0xFF500200, full=False)]
        code, text = self.run_check(rows, lines)
        self.assertEqual(code, 1, text)
        self.assertIn("compared 3, equal 1, different 2", self.line(text, "register c87 "))

    def test_lines_before_a_frames_first_full_line_are_not_used(self):
        # The log starts in the middle of GPU frame 101: its lines cannot be reconstructed.
        rows = [draw_row(1, 0xFF500000), draw_row(2, 0xFF600000)]
        lines = [gpu_line(1 + OFFSET, 7, 0xFF500000, regs={87: (1.0, 1.0, 1.0, 0.0)}, full=False),
                 gpu_line(2 + OFFSET, 0, 0xFF600000)]
        code, text = self.run_check(rows, lines)
        self.assertIn("1 lines before their frame's first full line were not used", text)
        self.assertIn("compared 1, equal 1, different 0", self.line(text, "register c87 "))
        self.assertEqual(code, 1, text)  # the row of frame 1 is left without a packet

    def test_a_new_frame_needs_its_own_full_line(self):
        rows = [draw_row(1, 0xFF500000), draw_row(2, 0xFF600000)]
        lines = [gpu_line(1 + OFFSET, 0, 0xFF500000),
                 gpu_line(2 + OFFSET, 0, 0xFF600000, full=False)]
        code, text = self.run_check(rows, lines)
        self.assertIn("1 lines before their frame's first full line were not used", text)
        self.assertEqual(code, 1, text)


class TileTest(Base):
    """c0..c3 are loaded once per colour tile under that tile's bin mask, c4..c7 once for the
    pre-pass bins and once for the colour bins: the row carries those loads ("ptiles")."""

    C0_T0 = (0.0, 0.0, 1.0 / 1120, 1.0 / 720)
    C0_T1 = (0.0, 0.5, 1.0 / 1120, 1.0 / 720)
    C4_PRE = (1.0, 0.0, 0.0, 0.0)
    C4_COL = (0.0, 1.0, 0.0, 0.0)

    def tiles(self):
        return [tile(0x2, 0, [self.C0_T0]), tile(0x8, 0, [self.C0_T1]),
                tile(0x15555555, 4, [self.C4_PRE]), tile(0x2AAAAAAA, 4, [self.C4_COL])]

    def lines(self, cmd, d=0, frame=1 + OFFSET):
        return [gpu_line(frame, d, cmd, regs={0: self.C0_T1, 4: self.C4_PRE}, bs=PRE_PASS,
                         vs="0xC47236E5A9B00356", ps="0x37CAF82F552893EE"),
                gpu_line(frame, d + 500, cmd, regs={0: self.C0_T0, 4: self.C4_COL}, bs=TILE0, bm=0x8000000F),
                gpu_line(frame, d + 900, cmd, regs={0: self.C0_T1, 4: self.C4_COL}, bs=TILE1, bm=0x8000000F)]

    def test_each_execution_is_compared_with_its_tiles_load(self):
        # "pfull" holds the first colour tile's c0 and the colour bins' c4.
        rows = [draw_row(1, 0xFF500000, over={0: self.C0_T0, 4: self.C4_COL}, tiles=self.tiles())]
        code, text = self.run_check(rows, self.lines(0xFF500000))
        self.assertEqual(code, 0, text)
        ln = self.line(text, "register c0 ")
        self.assertIn("compared 2, equal 2, different 0", ln)
        self.assertIn("pre-pass compared 1, different 1", ln)   # no load for the pre-pass bins: stale
        ln = self.line(text, "register c4 ")
        self.assertIn("compared 2, equal 2, different 0", ln)
        self.assertIn("pre-pass compared 1, different 0", ln)

    def test_without_the_tile_loads_the_second_tile_differs(self):
        rows = [draw_row(1, 0xFF500000, over={0: self.C0_T0, 4: self.C4_COL})]
        code, text = self.run_check(rows, self.lines(0xFF500000))
        self.assertEqual(code, 1, text)
        self.assertIn("compared 2, equal 1, different 1", self.line(text, "register c0 "))
        self.assertIn("bin select 00000008", self.line(text, "mismatch c0:"))

    def test_a_later_load_for_the_same_bins_wins(self):
        tiles = self.tiles() + [tile(0x2, 0, [self.C0_T1])]
        rows = [draw_row(1, 0xFF500000, over={0: self.C0_T0, 4: self.C4_COL}, tiles=tiles)]
        code, text = self.run_check(rows, self.lines(0xFF500000))
        self.assertEqual(code, 1, text)
        self.assertIn("bin select 00000002", self.line(text, "mismatch c0:"))

    def test_the_pre_pass_never_decides(self):
        rows = [draw_row(1, 0xFF500000, over={0: self.C0_T0, 4: self.C4_COL}, tiles=self.tiles())]
        lines = self.lines(0xFF500000)
        lines[0] = gpu_line(1 + OFFSET, 0, 0xFF500000, regs={0: self.C0_T1, 4: self.C4_PRE, 87: (0.0, 0.0, 0.0, 0.0)},
                            bs=PRE_PASS, vs="0xC47236E5A9B00356", ps="0x37CAF82F552893EE")
        code, text = self.run_check(rows, lines)
        self.assertEqual(code, 0, text)
        self.assertIn("pre-pass compared 1, different 1", self.line(text, "register c87 "))


class BankTest(Base):
    def test_key_pbank_compares_the_device_bank(self):
        zero = (0.0, 0.0, 0.0, 0.0)
        rows = [draw_row(1, 0xFF500000, bank_diff={80: zero, 87: zero}), draw_row(2, 0xFF600000, bank_diff={})]
        lines = [gpu_line(1 + OFFSET, 0, 0xFF500000), gpu_line(2 + OFFSET, 0, 0xFF600000)]
        code, text = self.run_check(rows, lines)
        self.assertEqual(code, 0, text)  # the proven path is equal
        code, text = self.run_check(rows, lines, "--key", "pbank")
        self.assertEqual(code, 1, text)
        self.assertIn("compared 2, equal 1, different 1", self.line(text, "register c80 "))
        self.assertIn("compared 2, equal 1, different 1", self.line(text, "register c87 "))
        self.assertIn("compared 2, equal 2, different 0", self.line(text, "register c21 "))
        self.assertIn("c80 c87", self.line(text, "registers different in a main-scene colour execution:"))
        self.assertIn("pbank", self.result(text))

    def test_key_pbank_needs_the_banks_difference_in_the_row(self):
        # A row whose bank could not be read has no "pbank_diff": there is no bank to compare.
        rows = [draw_row(1, 0xFF500000, bank_diff={}), draw_row(2, 0xFF600000)]
        lines = [gpu_line(1 + OFFSET, 0, 0xFF500000), gpu_line(2 + OFFSET, 0, 0xFF600000)]
        code, text = self.run_check(rows, lines, "--key", "pbank")
        self.assertIn("rows 2, sampled 1, not sampled 1", self.line(text, "capture:"))
        self.assertIn("compared 1, equal 1, different 0", self.line(text, "register c80 "))

    def test_key_pbank_ignores_the_tile_loads(self):
        t = TileTest()
        rows = [draw_row(1, 0xFF500000, over={0: t.C0_T0, 4: t.C4_COL}, tiles=t.tiles(),
                         bank_diff={0: (0.0, 0.0, 0.0, 0.0)})]
        code, text = self.run_check(rows, t.lines(0xFF500000), "--key", "pbank")
        self.assertEqual(code, 1, text)
        self.assertIn("compared 2, equal 0, different 2", self.line(text, "register c0 "))


class BoolTest(Base):
    def test_the_pixel_bool_word_is_compared(self):
        rows = three_rows()
        lines = three_lines()
        lines[2] = gpu_line(2 + OFFSET, 0, 0xFF600000, pb=0x1A0)  # b133 and b135 set on the GPU
        code, text = self.run_check(rows, lines)
        self.assertEqual(code, 0, text)  # reported, not part of the verdict
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "pixel bool word (register 0x4904"))
        self.assertIn("GPU set 1, device set 0, device differs 1", self.line(text, "  b133:"))
        self.assertIn("GPU set 1, device set 0, device differs 1", self.line(text, "  b135:"))
        self.assertIn("GPU set 3, device set 3, device differs 0", self.line(text, "  b136:"))
        self.assertIn("GPU set 0, device set 0, device differs 0", self.line(text, "  b128:"))


class UnreadTest(Base):
    """With --dumps, a difference in a register the pixel shader the GPU ran does not read is
    counted apart and does not decide: a replayed tile keeps, for its first draws, the light
    registers the replay before it ended with, where the engine loaded none for a draw whose
    shader reads none."""

    OTHER_PS = "0xF6D98C7B4D98438B"

    def dumps(self, files):
        d = self.dir / "dumps"
        d.mkdir(exist_ok=True)
        for ps, text in files.items():
            (d / f"shader_{int(ps, 16):016X}.ucode.frag").write_text(text)
        return str(d)

    def case(self, text, ps=OTHER_PS):
        rows = three_rows()
        rows[1] = draw_row(1, 0xFF500100, count=12, ps=ps)
        lines = three_lines()
        lines[1] = gpu_line(1 + OFFSET, 1, 0xFF500100, n=12, ps=ps, regs={87: (9.0, 9.0, 9.0, 0.0), 100: (5.0, 5.0, 5.0, 5.0)})
        files = {PS: "/* 1 */ mul r0, r1.xyzz, c80.xyzz\n/* 2 */ mul r0, r0, c87\n"}
        if text is not None:
            files[ps] = text
        return rows, lines, self.dumps(files)

    def test_a_difference_the_shader_does_not_read_is_counted_apart(self):
        rows, lines, dumps = self.case("/* 1 */ mad r0, r1, c19.xyzz, c100.wwww\n         cexec b136\n")
        code, text = self.run_check(rows, lines, "--dumps", dumps)
        self.assertEqual(code, 0, text)
        ln = self.line(text, "register c87 ")
        self.assertIn("compared 3, equal 2, different 0, different where the pixel shader does not read it 1", ln)
        # c100 is read by that shader: a difference as before.
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "register c100 "))
        self.assertNotIn("does not read", self.line(text, "register c100 "))
        self.assertIn("c87", self.line(text, "registers different only where the pixel shader does not read them:"))
        self.assertIn("RESULT: OK", self.result(text))

    def test_a_difference_the_shader_reads_still_fails(self):
        rows, lines, dumps = self.case("/* 1 */ mul r0, r1, c87.xyzz\n")
        code, text = self.run_check(rows, lines, "--dumps", dumps)
        self.assertEqual(code, 1, text)
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "register c87 "))

    def test_c8_is_not_c87(self):
        rows, lines, dumps = self.case("/* 1 */ mul r0, r1, c8.xyzz\n/* 2 */ mul r0, r1, c870\n")
        code, text = self.run_check(rows, lines, "--dumps", dumps)
        self.assertEqual(code, 0, text)

    def test_without_the_shaders_dump_every_difference_counts(self):
        rows, lines, dumps = self.case(None)
        code, text = self.run_check(rows, lines, "--dumps", dumps)
        self.assertEqual(code, 1, text)
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "register c87 "))
        self.assertIn("pixel shaders without a readable dump: 1", text)

    def test_a_shader_that_indexes_its_constants_is_taken_to_read_them_all(self):
        rows, lines, dumps = self.case("/* 1 */ mul r0, r1, c[8+aL].xyzz\n")
        code, text = self.run_check(rows, lines, "--dumps", dumps)
        self.assertEqual(code, 1, text)

    def test_without_the_option_every_difference_counts(self):
        rows, lines, _ = self.case("/* 1 */ mul r0, r1, c19.xyzz\n")
        code, text = self.run_check(rows, lines)
        self.assertEqual(code, 1, text)
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "register c87 "))

    def test_the_shader_the_gpu_ran_decides(self):
        # The row names a shader that reads c87; the GPU ran another one that does not.
        rows, lines, dumps = self.case("/* 1 */ mul r0, r1, c19.xyzz\n")
        rows[1] = draw_row(1, 0xFF500100, count=12, ps=PS)
        code, text = self.run_check(rows, lines, "--dumps", dumps)
        self.assertEqual(code, 0, text)
        self.assertIn("different where the pixel shader does not read it 1", self.line(text, "register c87 "))

    def test_bool_bits_the_shader_does_not_test_are_told_apart(self):
        rows, lines, dumps = self.case("/* 1 */ mad r0, r1, c19.xyzz, c100.wwww\n         cexec b136\n")
        lines[1] = gpu_line(1 + OFFSET, 1, 0xFF500100, n=12, ps=self.OTHER_PS, pb=0x010)  # b132 set, b136 clear
        for row in rows:
            row["pbool_load"] = "0x00000100"
        code, text = self.run_check(rows, lines, "--dumps", dumps)
        ln = self.line(text, "  bits of \"pbool_load\" that differ")
        self.assertIn("b132: 1 (tested by the pixel shader in 0)", ln)
        self.assertIn("b136: 1 (tested by the pixel shader in 1)", ln)


class BoolLoadTest(Base):
    def rows(self, loads):
        rows = three_rows()
        for row, word in zip(rows, loads):
            if word is not None:
                row["pbool_load"] = f"0x{word:08X}"
        return rows

    def test_the_engine_word_at_the_last_shader_load_is_compared_too(self):
        lines = three_lines()
        lines[2] = gpu_line(2 + OFFSET, 0, 0xFF600000, pb=0x1A0)
        code, text = self.run_check(self.rows([0x100, 0x100, 0x1A0]), lines)
        self.assertEqual(code, 0, text)
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "pixel bool word (register 0x4904"))
        self.assertIn("compared 3, equal 3, different 0", self.line(text, "pixel bool word as its last writer left it"))

    def test_every_differing_bit_is_listed(self):
        lines = three_lines()
        lines[1] = gpu_line(1 + OFFSET, 1, 0xFF500100, n=12, pb=0x80000102)  # b129 and b159 set on the GPU
        lines[2] = gpu_line(2 + OFFSET, 0, 0xFF600000, pb=0x102)
        code, text = self.run_check(self.rows([0x100, 0x80000102, 0x100]), lines)
        self.assertIn("b129: 2, b159: 1", self.line(text, "  bits of \"pbool\" that differ"))
        self.assertIn("b129: 1", self.line(text, "  bits of \"pbool_load\" that differ"))
        self.assertNotIn("b159", self.line(text, "  bits of \"pbool_load\" that differ"))

    def test_rows_without_the_engine_word_are_not_compared_for_it(self):
        code, text = self.run_check(self.rows([0x100, None, 0x180]), three_lines())
        self.assertIn("compared 2, equal 1, different 1", self.line(text, "pixel bool word as its last writer left it"))
        self.assertIn("load set 1 of 2, load differs 1", self.line(text, "  b135:"))
        self.assertIn("load set 2 of 2, load differs 0", self.line(text, "  b136:"))


def load_row(frame, cmd, bank=1, start=80, count=36, data=0xBF001000, first=0, pred=0, mask=0xFFFFFFFF):
    return {"kind": "aluload", "frame": frame, "bank": bank, "start": start, "count": count,
            "obj": "0x4A000000", "first": first, "data": f"0x{data:08X}", "pred": pred,
            "bin_mask": f"0x{mask:08X}", "cmd": f"0x{cmd:08X}", "v": "00" * 16 * count}


def load_line(frame, cmd, bank=1, start=80, count=36, data=0xBF001000, first=0, executed=True, bs=0xFFFFFFFF,
              bm=0xFFFFFFFF):
    head = f"L f={frame} a={rsc.gpu_address(cmd) + 4:08X} x={int(executed)} bs={bs:016X} bm={bm:016X}"
    if not executed:
        return head
    return head + (f" src={rsc.gpu_address(data + 16 * first):08X} idx={((bank << 8) + start) * 4:04X} t=0 "
                   f"n={4 * count}")


class LoadTest(Base):
    """The "aluload" rows (one per LoadAluConstants call) against the GPU's LOAD_ALU_CONSTANT packets."""

    def test_a_load_is_paired_by_its_packet_address_and_its_fields_compared(self):
        rows = three_rows() + [load_row(1, 0xFF4FFF00)]
        lines = three_lines() + [load_line(1 + OFFSET, 0xFF4FFF00)]
        code, text = self.run_check(rows, lines)
        self.assertEqual(code, 0, text)
        self.assertIn("rows 1, paired 1, packet as the row says 1, another packet 0", self.line(text, "constant loads:"))
        ln = self.line(text, "  load pixel c80..c115 ")
        self.assertIn("rows 1", ln)
        self.assertIn("executed 1", ln)
        self.assertIn("skipped 0", ln)

    def test_another_source_address_is_another_packet(self):
        rows = three_rows() + [load_row(1, 0xFF4FFF00)]
        lines = three_lines() + [load_line(1 + OFFSET, 0xFF4FFF00, data=0xBF002000)]
        code, text = self.run_check(rows, lines)
        self.assertIn("rows 1, paired 1, packet as the row says 0, another packet 1", self.line(text, "constant loads:"))
        self.assertIn("load differs:", text)

    def test_another_register_or_size_is_another_packet(self):
        for change in ({"start": 84}, {"count": 32}, {"bank": 0}, {"first": 4}):
            rows = three_rows() + [load_row(1, 0xFF4FFF00)]
            lines = three_lines() + [load_line(1 + OFFSET, 0xFF4FFF00, **change)]
            code, text = self.run_check(rows, lines)
            self.assertIn("packet as the row says 0, another packet 1", self.line(text, "constant loads:"), change)

    def test_the_buffers_first_register_moves_the_source(self):
        # c4..c7 for the colour bins come from the buffer's registers 4..7.
        rows = three_rows() + [load_row(1, 0xFF4FFF00, start=4, count=4, first=4)]
        lines = three_lines() + [load_line(1 + OFFSET, 0xFF4FFF00, start=4, count=4, first=4)]
        code, text = self.run_check(rows, lines)
        self.assertIn("packet as the row says 1, another packet 0", self.line(text, "constant loads:"))

    def test_a_load_that_no_pass_executes_is_counted_apart(self):
        rows = three_rows() + [load_row(1, 0xFF4FFF00, pred=1, mask=0x2)]
        lines = three_lines() + [load_line(1 + OFFSET, 0xFF4FFF00, executed=False, bs=PRE_PASS, bm=0x2)]
        code, text = self.run_check(rows, lines)
        self.assertIn("packet as the row says 0, another packet 0, never executed 1", self.line(text, "constant loads:"))

    def test_a_load_without_a_packet_and_a_packet_without_a_load(self):
        rows = three_rows() + [load_row(1, 0xFF4FFF00), load_row(2, 0xFF5FFF00)]
        lines = three_lines() + [load_line(1 + OFFSET, 0xFF4FFF00),
                                 load_line(1 + OFFSET, 0xFF4FFE00, start=252, count=4)]  # a shader's literals
        code, text = self.run_check(rows, lines)
        self.assertIn("rows 2, paired 1", self.line(text, "constant loads:"))
        self.assertIn("without a packet 1", self.line(text, "constant loads:"))
        self.assertIn("GPU loads without a row: 1", text)
        self.assertIn("pixel c252..c255 (4): 1", text)

    def test_a_tile_load_executes_only_under_its_bins(self):
        rows = three_rows() + [load_row(1, 0xFF4FFF00, start=0, count=4, pred=1, mask=0x2)]
        lines = three_lines() + [load_line(1 + OFFSET, 0xFF4FFF00, executed=False, bs=PRE_PASS, bm=0x2),
                                 load_line(1 + OFFSET, 0xFF4FFF00, start=0, count=4, bs=TILE0, bm=0x2),
                                 load_line(1 + OFFSET, 0xFF4FFF00, executed=False, bs=TILE1, bm=0x2)]
        code, text = self.run_check(rows, lines)
        self.assertIn("rows 1, paired 1, packet as the row says 1, another packet 0", self.line(text, "constant loads:"))
        ln = self.line(text, "  load pixel c0..c3 ")
        self.assertIn("predicated under 00000002", ln)
        self.assertIn("executed 1 (bin selects 00000002)", ln)
        self.assertIn("skipped 2 (bin selects 00000008 80000001)", ln)

    def test_a_vertex_load_is_named_by_its_bank(self):
        rows = three_rows() + [load_row(1, 0xFF4FFF00, bank=0, start=76, count=32)]
        lines = three_lines() + [load_line(1 + OFFSET, 0xFF4FFF00, bank=0, start=76, count=32)]
        code, text = self.run_check(rows, lines)
        self.assertIn("rows 1", self.line(text, "  load vertex c76..c107 "))

    def test_without_load_rows_the_section_says_so(self):
        code, text = self.run_check(three_rows(), three_lines())
        self.assertIn("rows 0", self.line(text, "constant loads:"))


class VertexTest(Base):
    """--key vfull / vbank: the vertex bank's rows ("vfull", "vtiles", "vbank_diff") against the V
    tokens of the log (register file 0x4000 + 4 r). Every register decides: there is no light
    block on the vertex side."""

    SUN = (-0.63, 0.03, 0.776, 0.0)

    def row(self, frame, cmd, count=6, over=None, lack=(), tiles=None, bank_diff=None, vs=VS):
        row = draw_row(frame, cmd, count=count, vs=vs)
        regs = {18: (1.12, 1.39, 1.88, 2.86), 76: self.SUN}
        regs.update(over or {})
        row["vfull"] = "".join("-" * 32 if r in lack else reg_hex(regs.get(r, (0.0, 0.0, 0.0, 0.0))) for r in range(256))
        if tiles is not None:
            row["vtiles"] = tiles
        if bank_diff is not None:
            row["vbank_diff"] = {str(r): reg_hex(v) for r, v in bank_diff.items()}
        return row

    def gpu(self, frame, d, cmd, over=None, n=6, vs=VS, bs=0xFFFFFFFF):
        regs = {18: (1.12, 1.39, 1.88, 2.86), 76: self.SUN}
        regs.update(over or {})
        vertex = " ".join(f"V{r}={reg_hex(regs.get(r, (0.0, 0.0, 0.0, 0.0)))}" for r in range(256))
        return gpu_line(frame, d, cmd, n=n, vs=vs, bs=bs) + " " + vertex

    def rows(self, **second):
        return [self.row(1, 0xFF500000), self.row(1, 0xFF500100, count=12, **second), self.row(2, 0xFF600000)]

    def lines(self, **second):
        return [self.gpu(1 + OFFSET, 0, 0xFF500000), self.gpu(1 + OFFSET, 1, 0xFF500100, n=12, **second),
                self.gpu(2 + OFFSET, 0, 0xFF600000)]

    def test_all_equal_passes(self):
        code, text = self.run_check(self.rows(), self.lines(), "--key", "vfull")
        self.assertEqual(code, 0, text)
        self.assertIn("compared 3, equal 3, different 0", self.line(text, "register c76 "))
        self.assertIn("RESULT: OK (vfull)", self.result(text))
        self.assertIn("every vertex register", self.result(text))
        self.assertNotIn("[light block]", text)
        self.assertNotIn("pixel bool word", text)

    def test_any_register_different_fails(self):
        code, text = self.run_check(self.rows(), self.lines(over={200: (1.0, 0.0, 0.0, 0.0)}), "--key", "vfull")
        self.assertEqual(code, 1, text)
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "register c200 "))
        self.assertIn("c200", self.result(text))

    def test_the_pixel_registers_of_the_line_are_not_the_vertex_ones(self):
        # The pixel bank of the log holds another c76 than the vertex bank: only V tokens count.
        lines = self.lines()
        lines[0] = lines[0].replace(f"P76={'0' * 32}", f"P76={reg_hex((9.0, 9.0, 9.0, 9.0))}")
        code, text = self.run_check(self.rows(), lines, "--key", "vfull")
        self.assertEqual(code, 0, text)

    def test_a_log_without_vertex_registers_pairs_nothing(self):
        code, text = self.run_check(self.rows(), three_lines(), "--key", "vfull")
        self.assertEqual(code, 1, text)
        self.assertIn("3 lines before their frame's first full line were not used", text)

    def test_key_vbank_compares_the_device_bank(self):
        zero = (0.0, 0.0, 0.0, 0.0)
        rows = self.rows(bank_diff={76: zero})
        rows[0]["vbank_diff"] = {}
        rows[2]["vbank_diff"] = {}
        code, text = self.run_check(rows, self.lines(), "--key", "vbank")
        self.assertEqual(code, 1, text)
        self.assertIn("compared 3, equal 2, different 1", self.line(text, "register c76 "))
        self.assertIn("compared 3, equal 3, different 0", self.line(text, "register c18 "))

    def test_tile_loads_of_the_vertex_bank(self):
        pre, colour = (1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0)
        tiles = [tile(0x15555555, 40, [pre]), tile(0x2AAAAAAA, 40, [colour])]
        rows = [self.row(1, 0xFF500000, over={40: colour}, tiles=tiles)]
        lines = [self.gpu(1 + OFFSET, 0, 0xFF500000, over={40: pre}, bs=PRE_PASS),
                 self.gpu(1 + OFFSET, 500, 0xFF500000, over={40: colour}, bs=TILE0)]
        code, text = self.run_check(rows, lines, "--key", "vfull")
        self.assertEqual(code, 0, text)
        ln = self.line(text, "register c40 ")
        self.assertIn("compared 1, equal 1, different 0", ln)
        self.assertIn("pre-pass compared 1, different 0", ln)

    def test_dumps_are_the_vertex_shaders(self):
        d = self.dir / "dumps"
        d.mkdir()
        (d / f"shader_{int(VS, 16):016X}.ucode.vert").write_text("/* 1 */ mul r0, r1, c18.xyzz\n")
        (d / f"shader_{int(PS, 16):016X}.ucode.frag").write_text("/* 1 */ mul r0, r1, c200.xyzz\n")
        code, text = self.run_check(self.rows(), self.lines(over={200: (1.0, 0.0, 0.0, 0.0)}), "--key", "vfull",
                                    "--dumps", str(d))
        self.assertEqual(code, 0, text)
        self.assertIn("different where the vertex shader does not read it 1", self.line(text, "register c200 "))

    def test_an_indexed_constant_reads_every_register_from_its_base(self):
        # A skin palette: c[28+a0] can be any register from 28 on.
        d = self.dir / "dumps"
        d.mkdir()
        (d / f"shader_{int(VS, 16):016X}.ucode.vert").write_text("/* 1 */ mul r0, r1, c[28+a0].xyzz\n/* 2 */ add r0, r0, c4\n")
        for reg, code_expected in ((200, 1), (28, 1), (27, 0), (4, 1)):
            code, text = self.run_check(self.rows(), self.lines(over={reg: (1.0, 0.0, 0.0, 0.0)}), "--key", "vfull",
                                        "--dumps", str(d))
            self.assertEqual(code, code_expected, (reg, text))

    def test_a_register_the_row_lacks_is_not_compared(self):
        code, text = self.run_check(self.rows(lack=(255,)), self.lines(over={255: (2.0, -1.0, 0.0, 1.0)}),
                                    "--key", "vfull")
        self.assertEqual(code, 0, text)
        self.assertIn("compared 2, equal 2, different 0, not compared 1", self.line(text, "register c255 "))


class ValuesTest(Base):
    def test_values_lists_the_gpus_values_of_the_registers_asked_for(self):
        code, text = self.run_check(three_rows(), three_lines(regs={87: (1.0, 2.0, 3.0, 0.0)}), "--values", "80,87")
        ln = self.line(text, "value c80:")
        self.assertIn("1 distinct in 3", ln)
        self.assertIn("(-0.828, 0.022, 0.56, 0) x3", ln)
        ln = self.line(text, "value c87:")
        self.assertIn("2 distinct in 3", ln)
        self.assertIn("(2.5, 2.25, 1.75, 0) x2", ln)
        self.assertIn("(1, 2, 3, 0) x1", ln)

    def test_values_are_those_of_the_main_scenes_colour_executions(self):
        other = (9.0, 9.0, 9.0, 0.0)
        rows = three_rows() + [draw_row(2, 0xFF600100, in_scene=False), draw_row(2, 0xFF600200)]
        lines = three_lines() + [gpu_line(2 + OFFSET, 1, 0xFF600100, regs={87: other}, pitch=1040),
                                 gpu_line(2 + OFFSET, 2, 0xFF600200, regs={87: other}, bs=PRE_PASS)]
        code, text = self.run_check(rows, lines, "--values", "87")
        self.assertIn("1 distinct in 3", self.line(text, "value c87:"))

    def test_values_takes_ranges(self):
        self.assertEqual(pc.parse_register_list("0,19-21,87"), [0, 19, 20, 21, 87])
        with self.assertRaises(ValueError):
            pc.parse_register_list("256")


if __name__ == "__main__":
    unittest.main()
