"""Coverage (sub-project 11, phase 11b; plan Task 12): the covered samples of a pixel under alpha-to-mask on the
main scene's two samples.

light_formulas.covered_samples is held to a transcription of the emulator's own instruction sequence (SDK
src/graphics/pipeline/shader/dxbc_translator_om.cpp, CompletePixelShader_AlphaToMask and
CompletePixelShader_AlphaToMaskSample, lines 1674 to 1790, the two-sample branch; the constant as
src/graphics/d3d12/command_processor.cpp line 4041 makes it) for all 256 offset bytes, the four parities and the
alphas 0 to 1.25 in steps of 1 / 1024 (criterion 10 of the design). The C++ mirror, CoveredSamples in
src/native/render/clay_logic.h, is held to the same closed form on the same grid by tests/native/test_clay_logic.cpp
(cases 700 to 703); here its text is read and its two thresholds are compared with the reference's.

What this cannot see: whether the console's hardware does what the emulator does (the emulator is the oracle of
every picture this renderer is compared with), and which of the two samples the game's depth copy holds (measured
on captures: the first, plan Task 4)."""
import math
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402

ALPHAS = [i / 1024.0 for i in range(1281)]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def emulator_coverage(color_control, position, alpha, sample_count_log2=(0, 1)):
    """The output coverage mask the emulator's pixel shader ends with, bit 0 the first (top) guest sample and bit 1
    the second: the instructions of CompletePixelShader_AlphaToMask in order, in the "ROV" numbering of the
    samples (guest sample indices; the host's own numbering of a 2x target exchanges them and is no part of the
    rule). `color_control`: RB_COLORCONTROL. `position`: SV_Position.xy. `sample_count_log2`: (x, y) as the
    emulator's constant holds it, (0, 1) for two samples, (1, 1) for four, (0, 0) for one."""
    enabled = (color_control >> 4) & 1
    constant = ((color_control >> 24) | (1 << 8)) if enabled else 0       # command_processor.cpp 4041
    if not constant:
        return 0xFFFFFFFF                                                 # OpMov(OMask, UINT32_MAX)
    tx, ty = int(position[0]), int(position[1])                           # OpFToU
    ty &= 1                                                               # OpAnd(temp.y, temp.y, 1)
    tx = (ty & ~(1 << 1)) | ((tx & 1) << 1)                               # OpBFI(width 1, offset 1, src x, dst y)
    tx <<= 1                                                              # OpIShL
    tx = (constant >> tx) & 3                                             # OpUBFE(width 2, offset temp.x, constant)
    offset = float(tx)                                                    # OpUToF

    def sample(index, base, scale):
        threshold = f32(f32(offset * f32(-scale)) + f32(base))            # OpMAd(offset, -scale, base)
        covered = (not math.isnan(alpha)) and f32(alpha) >= threshold     # OpGE: a NaN is not covered
        return (1 << index) if covered else 0

    if sample_count_log2[1]:                                              # "Check if MSAA is enabled."
        if sample_count_log2[0]:                                          # 4x
            return sample(0, 0.75, 1.0 / 16.0) | sample(1, 0.25, 1.0 / 16.0) | sample(2, 0.5, 1.0 / 16.0) | sample(3, 1.0, 1.0 / 16.0)
        return sample(0, 0.5, 1.0 / 8.0) | sample(1, 1.0, 1.0 / 8.0)      # 2x: the first sample 0.5, the second 1
    return sample(0, 1.0, 1.0 / 4.0)


class CoveredSamples(unittest.TestCase):
    def test_equals_the_emulators_rule_on_the_whole_grid(self):
        wrong = 0
        for offsets in range(256):
            word = (offsets << 24) | 0x1C
            for parity in range(4):
                x, y = parity >> 1, parity & 1
                for a in ALPHAS:
                    mask = emulator_coverage(word, (x + 0.5, y + 0.5), a)
                    wrong += lf.covered_samples(offsets, x, y, a) != bin(mask & 3).count("1")
        self.assertEqual(wrong, 0)

    def test_the_first_sample_is_never_open_where_the_second_is_covered(self):
        # (so "covered == 1" is "the first sample alone", the set the game's depth copy holds beyond the full one)
        for offsets in (0x87, 0xAA, 0x00, 0xFF, 0x1B, 0xE4):
            for parity in range(4):
                for a in ALPHAS:
                    mask = emulator_coverage((offsets << 24) | 0x10, (parity >> 1, parity & 1), a) & 3
                    self.assertIn(mask, (0, 1, 3))

    def test_partition_of_the_first_sample_rule(self):
        # the fully covered and the half-covered sets are disjoint, and their union is what the one-sample target keeps
        # today (AlphaToMaskCovers under two samples: alpha >= 0.5 - offset / 8)
        for offsets in range(256):
            for parity in range(4):
                o = (offsets >> (2 * parity)) & 3
                for a in ALPHAS:
                    n = lf.covered_samples(offsets, parity >> 1, parity & 1, a)
                    self.assertIn(n, (0, 1, 2))
                    self.assertEqual(n >= 1, a >= 0.5 - o / 8.0)

    def test_the_captured_forms(self):
        # 0x87: offsets 3, 1, 0, 2 for the indices 0 to 3; 0xAA: 2 everywhere (research B6)
        first = [min(a for a in ALPHAS if lf.covered_samples(0x87, i >> 1, i & 1, a) >= 1) for i in range(4)]
        second = [min(a for a in ALPHAS if lf.covered_samples(0x87, i >> 1, i & 1, a) == 2) for i in range(4)]
        self.assertEqual(first, [0.125, 0.375, 0.5, 0.25])
        self.assertEqual(second, [0.625, 0.875, 1.0, 0.75])
        for i in range(4):
            self.assertEqual(lf.covered_samples(0xAA, i >> 1, i & 1, 0.5), 1)       # the late tree at fade 0.5: never both
            self.assertEqual(lf.covered_samples(0xAA, i >> 1, i & 1, 0.25), 1)
            self.assertEqual(lf.covered_samples(0xAA, i >> 1, i & 1, 0.2499), 0)
            self.assertEqual(lf.covered_samples(0xAA, i >> 1, i & 1, 0.75), 2)
        # any pixel of a parity reads that parity's bits
        self.assertEqual(lf.covered_samples(0x87, 641, 360, 0.6), lf.covered_samples(0x87, 1, 0, 0.6))

    def test_no_number_covers_nothing(self):
        for offsets in (0x87, 0xAA, 0xFF):
            self.assertEqual(lf.covered_samples(offsets, 0, 0, float("nan")), 0)
            self.assertEqual(emulator_coverage((offsets << 24) | 0x10, (0, 0), float("nan")) & 3, 0)
        self.assertEqual(lf.covered_samples(0x87, 0, 0, float("inf")), 2)
        self.assertEqual(lf.covered_samples(0x87, 0, 0, -1.0), 0)

    def test_has_full(self):
        self.assertFalse(lf.coverage_has_full(0xAA, 0.5))
        self.assertTrue(lf.coverage_has_full(0xAA, 0.75))
        self.assertFalse(lf.coverage_has_full(0x87, 0.6))
        self.assertTrue(lf.coverage_has_full(0x87, 0.625))
        for offsets in range(256):
            for i in range(0, 1025, 8):
                scale = i / 1024.0
                want = any(lf.covered_samples(offsets, p >> 1, p & 1, scale) == 2 for p in range(4))
                self.assertEqual(lf.coverage_has_full(offsets, scale), want)

    def test_a_wrong_rule_fails(self):
        # the comparison can fail: the thresholds exchanged, the parity's bits exchanged, a "greater" for the "at least"
        def count(rule):
            return sum(rule(o, p >> 1, p & 1, a) != bin(emulator_coverage((o << 24) | 0x10, (p >> 1, p & 1), a) & 3).count("1")
                       for o in (0x87, 0xAA, 0x1B) for p in range(4) for a in ALPHAS)

        self.assertEqual(count(lf.covered_samples), 0)
        self.assertGreater(count(lambda o, x, y, a: lf.covered_samples(o, y, x, a)), 0)                 # x and y exchanged
        self.assertGreater(count(lambda o, x, y, a: lf.covered_samples(o, x, y, a - 1e-6)), 0)          # ">" for ">="
        self.assertGreater(count(lambda o, x, y, a: lf.covered_samples(o, x, y, a * 2.0) // 2 * 2), 0)  # one threshold for both
        self.assertGreater(count(lambda o, x, y, a: lf.covered_samples(((o << 2) | (o >> 6)) & 0xFF, x, y, a)), 0)  # the bits shifted


class Mirror(unittest.TestCase):
    """The C++ mirror's text holds the same two thresholds and the same parity index."""

    def setUp(self):
        text = (ROOT / "src" / "native" / "render" / "clay_logic.h").read_text(encoding="utf-8")
        m = re.search(r"constexpr uint32_t CoveredSamples\(uint8_t offsets, uint32_t x, uint32_t y, float alpha\) \{(.*?)\n\}", text, re.S)
        self.assertIsNotNone(m)
        self.body = m.group(1)

    def test_thresholds_and_index(self):
        self.assertIn("((x & 1u) << 1) | (y & 1u)", self.body)
        self.assertIn(">> (2 * index)) & 3u", self.body)
        self.assertIn("alpha >= 0.5f - 0.125f * offset", self.body)
        self.assertIn("alpha >= 1.0f - 0.125f * offset", self.body)

    def test_family_a_is_the_replays(self):
        import foliage_replay as fr
        text = (ROOT / "src" / "native" / "render" / "clay_logic.h").read_text(encoding="utf-8")
        m = re.search(r"kCoveragePs\[\] = \{(.*?)\};", text, re.S)
        self.assertIsNotNone(m)
        self.assertEqual(sorted(h.upper() for h in re.findall(r"0x([0-9A-Fa-f]{16})ull", m.group(1))), sorted(fr.FAMILY_A))
        # and every one of them takes its alpha from the albedo times pixel c26.w and c15.w (the record's scale)
        table = (ROOT / "src" / "native" / "capture" / "ps_albedo_table.inc").read_text(encoding="utf-8")
        for h in fr.FAMILY_A:
            self.assertRegex(table, r"FABLE2_PS_ALPHA\(0x%sull, 1, 0, 3, 107, 63, " % h)


if __name__ == "__main__":
    unittest.main()
