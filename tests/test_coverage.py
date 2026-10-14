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


class SwitchOff(unittest.TestCase):
    """Plan Task 13, Review Focus 5: no test compiles clay_pass.cpp, so the text is held. Everything the pass does
    for the coverage is behind the Render's `coverage` word; with it off the sky step's place is SkyPlace's."""

    def setUp(self):
        self.source = (ROOT / "src" / "native" / "render" / "clay_pass.cpp").read_text(encoding="utf-8")
        self.front = (ROOT / "src" / "native" / "fable2_native_render.cpp").read_text(encoding="utf-8")

    def test_the_switch_is_off_by_default_and_read_by_the_pass_alone(self):
        self.assertRegex(self.front, r'REXCVAR_DEFINE_STRING\(fable2_native_coverage, "off", "Fable2",')
        self.assertEqual(self.front.count("CurrentCoverage()"), 3)      # its definition, the Render's argument, the start line
        self.assertIn("CurrentFog(), CurrentWind(),\n                 CurrentLocalLights(), CurrentCoverage(), impostors, water, post, st, tst);", self.front)
        self.assertIn('if (st.coverage_switch) REXLOG_INFO("[native] clay: {}", render::FormatCoverageText(st));', self.front)
        capture = (ROOT / "src" / "native" / "capture" / "capture.cpp").read_text(encoding="utf-8")
        for word in ("fable2_native_coverage", "CoverageOf", "CoveredSamples", "kCoveragePs"):   # the capture reads nothing for it
            self.assertNotIn(word, capture)

    def test_nothing_of_the_coverage_without_the_switch(self):
        s = self.source
        # the programs are asked for in one place, inside the switch's block of the prepare loop, and a record has
        # a plan (Prepared::parts: the parts it is drawn by) only there; without a plan TakeRecordPipelines asks for its own key alone
        # (executed: test_clay_logic.cpp, case 768)
        self.assertEqual(s.count("EnsureCoverageShader(dev)"), 1)
        a = s.index("    if (coverage) {\n      ClayCoverageInputs cin;")
        b = s.index("    if (d.alpha_tested) ++st.alpha_tested;", a)
        block = s[a:b]
        self.assertIn("      if (verdict == ClayCoverage::kDrawn) {\n        p.coverage_wanted = true;\n"
                      "        if (EnsureCoverageShader(dev)) p.parts = CoveragePlanOf(cin.kind, cin.has_full);\n      }\n", block)
        self.assertEqual(s.count("p.parts = "), 2)                      # there, and TakePipelines' reset to none
        self.assertEqual(s.count("p.coverage_wanted = "), 2)
        # the pass names no coverage key itself: the keys of a record's parts are clay_logic.h's RecordPipelineKeys
        for word in ("CoverageHalfKey(", "CoverageFullKey(", "CoverageHalfDepthKey("):
            self.assertEqual(s.count(word), 0, word)
        logic_text = (ROOT / "src" / "native" / "render" / "clay_logic.h").read_text(encoding="utf-8")
        self.assertIn("  keys.place = plan == CoveragePlan::kLate ? CoverageHalfKey(own) : plan == CoveragePlan::kEarlyFull ? CoverageFullKey(own) : own;\n"
                      "  if (plan != CoveragePlan::kNone) {\n    keys.depth = CoverageHalfDepthKey(own);\n    keys.has_depth = true;\n  }\n", logic_text)
        # a record's parts are set in one place, from what was granted: the late record's half part, an early
        # record's (plan Task 14), and the half part's depth step (ledger ruling 60) with every plan; a record
        # without the step's pipeline has no plan at all (all or nothing: case 768)
        take = s[s.index("bool ClayPass::TakePipelines("):s.index("nrhi::Pipeline* ClayPass::PipelineFor(")]
        self.assertIn("  p.pipeline = got.place;\n  p.coverage_tail = got.tail;\n  p.coverage_depth = got.depth;\n"
                      "  p.coverage_half = got.plan == CoveragePlan::kLate;\n  p.coverage_place = got.plan != CoveragePlan::kEarlyHalf;\n", take)
        for field in ("p.coverage_tail = ", "p.coverage_depth = ", "p.coverage_half = ", "p.coverage_place = "):
            self.assertEqual(s.count(field), 1, field)
        self.assertIn("  if (p.coverage_wanted && got.plan == CoveragePlan::kNone) {\n    ++st.coverage_rest[size_t(ClayCoverage::kPipeline)];\n", take)
        # the depth step is drawn before each of the three places a half part's colour is drawn, and nowhere else
        self.assertEqual(s.count("half_depth_step("), 3)
        self.assertEqual(s.count("auto half_depth_step = [&](const Prepared& q) {\n    if (!q.coverage_depth) return;\n"), 1)
        self.assertIn("      if (!q.coverage_tail) continue;\n      half_depth_step(q);\n      cmd->SetPipeline(q.coverage_tail);\n", s)
        self.assertIn("    if (p.coverage_half) half_depth_step(p);\n", s)
        self.assertIn("    if (half_here) {\n      half_depth_step(p);\n      cmd->SetPipeline(p.coverage_tail);\n", s)
        # the colour step writes no depth and is tested less-or-equal (the RHI has no "equal"); the depth step no colour
        logic = (ROOT / "src" / "native" / "render" / "clay_logic.h").read_text(encoding="utf-8")
        self.assertIn("constexpr PipelineKey CoverageHalfKey(PipelineKey k) {\n  k.coverage = 1;\n  k.depth_test = 1;\n  k.depth_write = 0;\n"
                      "  k.depth_func = uint8_t(capture::Compare::kLessEqual);\n  k.blend = 1;\n", logic)
        self.assertIn("constexpr PipelineKey CoverageHalfDepthKey(PipelineKey k) {\n  k.coverage = 1;\n  k.blend = 0;\n  k.color_mask = 0;\n  return k;\n}\n", logic)
        # a coverage pixel shader goes into a pipeline only for a key with the member
        self.assertIn("  if (key.coverage) {\n"
                      "    nrhi::Shader* const part = key.coverage == 1 ? coverage_half_ps_ : key.coverage == 2 ? coverage_full_ps_ : nullptr;\n"
                      "    if (kind != 1 || depth_only || !part) return nullptr;\n    pipe.ps = part;\n  }\n", s)
        self.assertEqual(s.count("pipe.ps = part;"), 1)
        # the sky step's place: SkyPlace's unless the switch's block moves it; PlanCoverageOrder is asked nowhere else
        self.assertIn("  size_t sky_at = sky_order.place;\n", s)
        self.assertEqual(s.count("PlanCoverageOrder("), 1)
        c = s.index("  size_t sky_at = sky_order.place;\n")
        d = s.index("  for (size_t index = 0; index < prepared_.size(); ++index) {", c)
        self.assertIn("  if (coverage) {\n    coverage_items_.clear();", s[c:d])
        self.assertEqual(s[c:d].count("sky_at = "), 3)              # (the third: the order handed to ColourStepsDue)
        # the tail: no tail unless the switch's block names one; drawn in two places (at its index, after the last record)
        self.assertIn("  size_t tail_at = kNoCoverageTail;\n", s[c:d])
        self.assertEqual(s[c:d].count("tail_at = "), 3)
        self.assertEqual(s.count("draw_tail("), 2)
        # What is due before each record and after the last is clay_logic.h's ColourStepsDue and ColourStepsAfter
        # (executed: test_clay_logic.cpp, case 769, which walks this loop's statements over both index spaces and
        # every arrangement of up to five records); held here: the loop hands the function the prepared place and
        # the record's place in FrameScene::draws, each where it belongs, and does what it says in its order.
        self.assertIn("  colour_order.sky_drawn = sky_drawn;\n  colour_order.sky_at = sky_at;\n  colour_order.tail_at = tail_at;\n"
                      "  colour_order.depth_early = sun_depth_early;\n  colour_order.depth = sun_place;\n", s[c:d])
        # (distant trees, plan Task 10: the impostor step is due first; with its switch off `due.impostors` is never set)
        self.assertIn("    const ColourDue due = ColourStepsDue(colour_order, index, size_t(p.r - scene.draws.data()), sky_done, sun_depth_taken);\n"
                      "    // The impostor step, first of this record's steps (it is opaque and writes depth).\n"
                      "    if (due.impostors) draw_impostors();\n"
                      "    // The early tail, before the layer's depth is taken; the sky step first when it is due here.\n"
                      "    if (due.tail) {\n      if (due.sky_first) {\n        draw_sky();\n        sky_done = true;\n      }\n"
                      "      draw_tail(index);\n    }\n", s)
        # (the water, sub-project 13, plan Task 12: the layer's depth is taken here, or after the water step when both
        # are due at one record; the water is the last of a record's steps; with its switch off `due.water` and
        # `due.depth_after_water` are never set and the order is the one of before)
        self.assertIn("    if (due.layer_depth && !due.depth_after_water) layer_depth();\n    if (due.sky) draw_sky();\n", s)
        self.assertIn("    if (due.water) draw_water();\n    if (due.depth_after_water) layer_depth();\n    if (!p.pipeline) continue;", s)
        self.assertEqual(s.count("layer_depth();"), 2)
        self.assertEqual(s.count("ColourStepsDue("), 1)
        self.assertEqual(s.count("sun_depth_taken = true;"), 1)
        self.assertEqual(s.count("draw_sky();"), 3)
        self.assertIn("    if (due.sky) draw_sky();\n    // The water step, last of this record's steps: after the tail and after the sky, the moon and the clouds.\n", s)
        self.assertIn("  const ColourDue after = ColourStepsAfter(colour_order, prepared_.size());\n"
                      "  // (the impostor step of a frame whose place lies after the last record: before the sky step)\n"
                      "  if (!left_out && after.impostors) draw_impostors();\n  if (after.sky) draw_sky();\n", s)
        self.assertIn("  if (!left_out && after.tail) draw_tail(prepared_.size());\n", s)
        self.assertEqual(s.count("SunDepthBefore("), 0)             # (the loop no longer asks it itself)
        # a record without coverage parts is drawn as before: one in-place draw, behind a flag that is true for it
        self.assertIn("    if (p.coverage_place) {\n      if (p.pipeline != bound) {\n        cmd->SetPipeline(p.pipeline);\n        bound = p.pipeline;\n      }\n"
                      "      DrawPrepared(cmd, p, color, false, mask_bound, &light_mode, &draw_fog, nullptr);\n    }\n", s)
        self.assertIn("const bool half_here = p.coverage_tail && !CoverageHalfInTail(CoverageKind::kEarly, index, tail_at);", s)
        # the log's line only with the switch
        self.assertIn("  if (coverage) {\n    const uint32_t form = ", s)

    def test_a_half_record_is_in_no_prepass_and_no_prepass_count(self):
        s = self.source
        # (the rule is clay_logic.h's CoverageInPrepass, asked of the record's kind in both loops: review of Tasks 12 to 14, M3)
        self.assertIn("      if (!key || !InPrepass(*key)) continue;\n"
                      "      // (a late coverage record is in no pre-pass; an early one as it is today: CoverageInPrepass)\n"
                      "      if (!CoverageInPrepass(coverage_kind(p))) continue;\n", s)
        self.assertIn("        if (p.prepass_culled || !CoverageInPrepass(coverage_kind(p))) continue;\n", s)
        self.assertEqual(s.count("CoverageInPrepass(coverage_kind(p))"), 2)
        self.assertIn("  auto coverage_kind = [](const Prepared& p) {\n"
                      "    return p.coverage_half ? CoverageKind::kLate : p.coverage_tail ? CoverageKind::kEarly : CoverageKind::kNone;\n  };\n", s)
        # the line's form does not hold the sky's move (it flips frame to frame in a held view and would spend the lines)
        self.assertNotIn("(st.coverage_sky_moved ? 8u : 0u)", s)
        self.assertIn("    const bool in_prepass = prepass && !p.coverage_half && key && InPrepass(*key);\n", s)
        # ... and on the Sky line it is a record that writes depth (its depth step does: final review, M2)
        self.assertIn("      item.depth_write = (key && InPrepass(*key)) || p.coverage_half;\n", s)
        # for SkyPlace a half record is the opaque record it is in the game's state
        self.assertIn("      item.blend = key && key->blend != 0 && !p.coverage_half;\n", s)

    def test_the_retry_and_the_release_know_the_two_programs(self):
        s = self.source
        self.assertEqual(s.count("coverage_half_failed_ = false;"), 2)      # another device, and the F6 retry
        self.assertEqual(s.count("coverage_half_ps_ = coverage_full_ps_ = nullptr;"), 2)       # the release, and another device
        self.assertEqual(s.count("device_->DestroyDeferred(coverage_half_ps_);"), 1)
        self.assertEqual(s.count("device_->DestroyDeferred(coverage_full_ps_);"), 1)

    def test_an_early_record_keeps_todays_prepass(self):
        # (the design check of plan Task 14: the pre-pass feeds the mask alone and holds the first sample's pixels, as
        # the game's depth copy does) nothing in the pre-pass's two loops asks for an early record's parts
        s = self.source
        a = s.index("    // The pre-pass's pipelines, one a depth-writing record")
        b = s.index("  st.mask = mask;", a)
        self.assertNotIn("coverage_tail", s[a:b])
        self.assertNotIn("coverage_place", s[a:b])
        c = s.index("    // 1. The depth pre-pass: every record that writes depth")
        d = s.index("    // 2. The mask:", c)
        self.assertNotIn("coverage_tail", s[c:d])
        self.assertNotIn("coverage_place", s[c:d])
        header = (ROOT / "src" / "native" / "fable2_native_shaders.h").read_text(encoding="utf-8")
        self.assertNotIn("kClayTexturedFullDepthPs", header)
        self.assertNotIn("kClayTexturedFullDepthPs", s)


if __name__ == "__main__":
    unittest.main()
