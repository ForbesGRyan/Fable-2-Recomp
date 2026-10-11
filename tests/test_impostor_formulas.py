"""The far woods' impostors as closed formulas (light_formulas.impostor_position) and their machine check against
the microcode of vertex shader 129F7F2793DA9084 (impostor_check.py): the distant-trees sub-project, plan Task 4,
spec criterion 1's first half (made inputs; the captured rows are plan Task 6's)."""
import math
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import impostor_check  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"
ROWS = {72: [1.0, 0.0, 0.0, 0.0], 73: [0.0, 1.0, 0.0, 0.0], 74: [0.0, 0.0, 1.0, 0.0], 75: [0.0, 0.0, 0.0, 1.0]}


def consts(over=None):
    c = {r: [0.0, 0.0, 0.0, 0.0] for r in lf.IMPOSTOR_SHADER["reg"]}
    c.update(ROWS)
    c[8] = [0.0, 0.0, 0.5, 0.0]
    c[46] = [0.6, 0.8, 1.0, 0.0]
    c[47] = [10.0, 20.0, 5.0, 1.0]
    c[113] = [1.0, 0.0, 0.0, 1.0]
    c[114] = [1.0, 1.0, 0.0, 0.0]
    c[115] = [1.0, 0.0, 0.0, 3.0]
    c[116] = [0.0, 1.0, 0.0, 4.0]
    c[117] = [0.0, 0.0, 1.0, 5.0]
    c.update(over or {})
    return c


# A camera whose view rows are world x, world z (up) and world y; the picture's lying axis c46 along world y.
LEVEL = {115: [1.0, 0.0, 0.0, 0.0], 116: [0.0, 0.0, 1.0, 0.0], 117: [0.0, 1.0, 0.0, 0.0], 46: [0.0, 1.0, 0.0, 0.0]}
# The same camera looking straight down: its second row is world y.
DOWN = {115: [1.0, 0.0, 0.0, 0.0], 116: [0.0, 1.0, 0.0, 0.0], 117: [0.0, 0.0, 1.0, 0.0], 46: [0.0, 1.0, 0.0, 0.0]}


class Corner(unittest.TestCase):
    def test_four_vertices_share_a_quad(self):
        for i in range(48):
            self.assertEqual(lf.impostor_corner(i), (i // 4, i % 4))


class Position(unittest.TestCase):
    WORD = (100.0, 200.0, 30.0, 8.0)

    def test_the_place_is_raised_and_the_same_at_four_corners(self):       # instr 17, 29, 88
        for k in range(4):
            place = lf.impostor_position(k, self.WORD, consts())["place"]
            self.assertEqual(tuple(place), (100.0, 200.0, 30.0 + 8.0 * 0.5))

    def test_the_four_corners_take_the_four_picture_corners(self):         # instr 23 to 25, 33, 43, 68
        uv = [tuple(lf.impostor_position(k, self.WORD, consts())["uv"]) for k in range(4)]
        self.assertEqual(uv, [(0.0, 1.0), (0.0, 0.0), (1.0, 0.0), (1.0, 1.0)])
        scaled = lf.impostor_position(2, self.WORD, consts({113: [0.5, 0.25, 0.125, 2.0]}))["uv"]
        self.assertEqual(tuple(scaled), (1.0 * 0.5 + 0.25, 0.0 * 2.0 + 0.125))

    def test_w_is_one_under_unit_rows_and_the_rows_are_dotted_in_order(self):   # instr 71 to 76
        base = lf.impostor_position(1, self.WORD, consts())["pos"]
        self.assertEqual(base[3], 1.0)
        self.assertTrue(all(math.isfinite(v) for v in base))
        doubled = lf.impostor_position(1, self.WORD, consts({73: [0.0, 2.0, 0.0, 0.0]}))["pos"]
        self.assertEqual(doubled[1], 2.0 * base[1])
        self.assertEqual((doubled[0], doubled[2], doubled[3]), (base[0], base[2], base[3]))

    def test_the_four_corners_are_four_places(self):
        pos = {tuple(lf.impostor_position(k, self.WORD, consts())["pos"]) for k in range(4)}
        self.assertEqual(len(pos), 4)

    def test_no_number_in_is_no_number_out(self):
        pos = lf.impostor_position(0, (100.0, 200.0, 30.0, math.nan), consts())["pos"]
        self.assertTrue(any(math.isnan(v) for v in pos))

    def test_the_register_list_is_what_the_dump_reads(self):
        self.assertEqual(len(lf.IMPOSTOR_SHADER["reg"]), 14)
        self.assertEqual(lf.IMPOSTOR_SHADER["reg"], tuple(sorted(lf.IMPOSTOR_SHADER["reg"])))

    # --- the block the research did not read (instr 13, 14, 28 to 66), written out by hand ---------------------

    def corners(self, over):
        return [tuple(lf.impostor_position(k, self.WORD, consts(over))["pos"]) for k in range(4)]

    def test_a_level_camera_sees_an_upright_quad(self):
        # The lying axis has no part of the view's second row: its weight is 2^(-inf) = 0 (instr 42 to 50). The
        # quad is the raised place, the size up and down in the world, the size left and right in the view.
        self.assertEqual(self.corners(LEVEL), [(92.0, 26.0, 200.0, 1.0), (92.0, 42.0, 200.0, 1.0),
                                               (108.0, 42.0, 200.0, 1.0), (108.0, 26.0, 200.0, 1.0)])

    def test_a_camera_looking_down_sees_the_quad_lying_along_c46(self):
        # The vertical has no part of the view's second row: the picture's top goes to the place minus the size
        # along c46 where the raised place is above the eye (34 > 5), its bottom to the place plus it.
        self.assertEqual(self.corners(DOWN), [(92.0, 208.0, 34.0, 1.0), (92.0, 192.0, 34.0, 1.0),
                                              (108.0, 192.0, 34.0, 1.0), (108.0, 208.0, 34.0, 1.0)])

    def test_below_the_eye_the_lying_quad_is_turned_end_for_end(self):     # instr 31, 53 to 57
        over = dict(DOWN)
        over[47] = [10.0, 20.0, 34.5, 1.0]
        self.assertEqual(self.corners(over), [(92.0, 192.0, 34.0, 1.0), (92.0, 208.0, 34.0, 1.0),
                                              (108.0, 208.0, 34.0, 1.0), (108.0, 192.0, 34.0, 1.0)])
        over[47] = [10.0, 20.0, 34.0, 1.0]                                # "above" is strictly above (sgt)
        self.assertEqual(self.corners(over)[0], (92.0, 192.0, 34.0, 1.0))

    def test_between_the_two_the_weights_are_the_powers_of_the_two_extents(self):
        # The view's second row (0, 0.6, 0.8): the vertical's extent in it is 2 * 8 * 0.8 = 12.8, the lying
        # axis's 2 * 8 * 0.6 = 9.6. Weights: 12.8^y and 9.6^y over their sum (c114.x scales the first).
        for c114, wv in (((1.0, 1.0), 12.8 / 22.4), ((1.0, 2.0), 163.84 / 256.0), ((0.75, 1.0), 9.6 / 19.2)):
            over = dict(DOWN)
            over[116] = [0.0, 0.6, 0.8, 0.0]
            over[114] = [c114[0], c114[1], 0.0, 0.0]
            wh = 1.0 - wv
            got = self.corners(over)
            top = (wv * (0.8 * 42.0 + 0.6 * 200.0) + wh * (0.8 * 34.0 + 0.6 * 192.0), wv * 42.0 + wh * 34.0)
            bottom = (wv * (0.8 * 26.0 + 0.6 * 200.0) + wh * (0.8 * 34.0 + 0.6 * 208.0), wv * 26.0 + wh * 34.0)
            for k, (x, (y, z)) in enumerate(((92.0, bottom), (92.0, top), (108.0, top), (108.0, bottom))):
                for g, w in zip(got[k], (x, y, z, 1.0)):
                    self.assertAlmostEqual(g, w, delta=1e-9, msg=f"c114 {c114} corner {k}")

    def test_the_size_goes_sideways_in_the_view_not_in_the_world(self):    # instr 67, 69
        over = dict(LEVEL)
        over[115] = [0.0, 1.0, 0.0, 7.0]                                  # the view's first row is world y, offset 7
        over[46] = [1.0, 0.0, 0.0, 0.0]
        self.assertEqual([p[0] for p in self.corners(over)], [207.0 - 8.0, 207.0 - 8.0, 207.0 + 8.0, 207.0 + 8.0])

    def test_a_quad_of_no_size_is_the_views_origin_by_the_consoles_rule(self):
        # Both extents are 0: both powers 2^(-inf) = 0, their sum's reciprocal is infinite, and 0 times it is +0
        # by the console's rule for a product with a zero factor (IEEE: no number). All four corners fall on the
        # view's origin: the fourth column of c72..c75. Nothing is drawn of it either way; a mirror must say which.
        over = dict(LEVEL)
        over.update({72: [1.0, 0.0, 0.0, 11.0], 73: [0.0, 1.0, 0.0, 12.0], 74: [0.0, 0.0, 1.0, 13.0], 75: [0.0, 0.0, 0.0, 14.0]})
        for k in range(4):
            got = lf.impostor_position(k, (100.0, 200.0, 30.0, 0.0), consts(over))
            self.assertEqual(tuple(got["pos"]), (11.0, 12.0, 13.0, 14.0))
            self.assertEqual(tuple(got["place"]), (100.0, 200.0, 30.0))

    def test_single_precision_is_near_the_double_reference(self):
        for k in range(4):
            a = lf.impostor_position(k, self.WORD, consts(LEVEL))["pos"]
            b = lf.impostor_position(k, self.WORD, consts(LEVEL), single=True)["pos"]
            self.assertEqual(tuple(a), tuple(b))                           # these numbers are exact in both
        over = dict(DOWN)
        over[116] = [0.0, 0.6, 0.8, 0.0]
        a = lf.impostor_position(1, self.WORD, consts(over))["pos"]
        b = lf.impostor_position(1, self.WORD, consts(over), single=True)["pos"]
        self.assertNotEqual(tuple(a), tuple(b))
        for x, y in zip(a, b):
            self.assertAlmostEqual(x, y, delta=1e-4)
            self.assertEqual(y, struct.unpack("<f", struct.pack("<f", y))[0])


class Words(unittest.TestCase):
    def test_eight_bytes_a_quad_by_the_given_order_and_endian(self):
        halves = bytes.fromhex("3C00" "4000" "4200" "4400")          # 1, 2, 3, 4 as big-endian halves
        verts = {"count": 8, "storage_dwords": 8}
        data = {"read": True, "bytes": (halves + halves).hex() + "00" * 16}
        self.assertEqual(impostor_check.quad_words(verts, data, source=(0, 1, 2, 3), endian=1),
                         [(1.0, 2.0, 3.0, 4.0), (1.0, 2.0, 3.0, 4.0)])
        self.assertEqual(impostor_check.quad_words(verts, data, source=(1, 0, 3, 2), endian=1)[0], (2.0, 1.0, 4.0, 3.0))
        with self.assertRaises(ValueError):
            impostor_check.quad_words(verts, {"read": False})
        with self.assertRaises(ValueError):
            impostor_check.quad_words({"count": 20, "storage_dwords": 20}, data)      # 5 quads want 40 bytes; the row has 32

    def test_the_shaders_order_is_the_memorys_order_of_halves(self):
        # Endian 2 (the bytes of a dword reversed) and the fetch's swizzle yxwz undo each other: register r6 is
        # the four big-endian halves in the order they lie in guest memory. (PROVISIONAL until plan Task 3.)
        halves = bytes.fromhex("3C00" "4000" "4200" "4400")
        self.assertEqual(impostor_check.quad_words({"count": 4, "storage_dwords": 4}, {"read": True, "bytes": (halves + halves).hex()}),
                         [(1.0, 2.0, 3.0, 4.0)])
        self.assertEqual((lf.IMPOSTOR_SHADER["word_source"], lf.IMPOSTOR_SHADER["word_endian"]), ((1, 0, 3, 2), 2))

    def test_the_four_endians(self):
        row = {"count": 4, "storage_dwords": 2}
        data = {"read": True, "bytes": "3C00400042004400"}
        self.assertEqual(impostor_check.quad_words(row, data, source=(0, 1, 2, 3), endian=2)[0], (2.0, 1.0, 4.0, 3.0))
        swapped = {"read": True, "bytes": "003C004000420044"}                             # little-endian halves
        self.assertEqual(impostor_check.quad_words(row, swapped, source=(0, 1, 2, 3), endian=0)[0], (1.0, 2.0, 3.0, 4.0))
        self.assertEqual(impostor_check.quad_words(row, swapped, source=(0, 1, 2, 3), endian=3)[0], (2.0, 1.0, 4.0, 3.0))
        for source, endian in (((0, 1, 2, 4), 2), ((0, 1, 2), 2), ((0, 1, 2, 3), 4)):
            with self.assertRaises(ValueError):
                impostor_check.quad_words(row, data, source=source, endian=endian)

    def test_a_count_that_is_no_whole_number_of_quads_is_refused(self):
        with self.assertRaises(ValueError):
            impostor_check.quad_words({"count": 6, "storage_dwords": 8}, {"read": True, "bytes": "00" * 32})
        with self.assertRaises(ValueError):
            impostor_check.quad_words({"count": 4, "storage_dwords": 8}, {"read": True, "bytes": "zz"})


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class AgainstMicrocode(unittest.TestCase):
    def test_the_position_on_made_inputs(self):
        r = impostor_check.check_position(trials=200, seed=11, dumps=DUMPS)
        self.assertEqual(r.failed, 0, r.first_failure)
        for corner in r.corners:
            self.assertGreaterEqual(corner, r.trials // 5)          # a check on one corner proves a quarter
        self.assertGreaterEqual(r.above, r.trials // 4)             # both arrangements of instr 53 to 57
        self.assertGreaterEqual(r.below, r.trials // 4)
        self.assertGreaterEqual(r.powered, r.trials // 2)           # the log / exp block on numbers
        self.assertLess(r.worst, 1e-9)                              # the same arithmetic, not merely inside the bound

    def test_mutations_fail(self):
        for name in impostor_check.POSITION_MUTATIONS:
            with self.subTest(mutation=name):
                r = impostor_check.check_position(trials=200, seed=11, mutation=name, dumps=DUMPS)
                self.assertGreater(r.failed, 0, f"{name} was not caught")

    def test_an_unknown_mutation_is_refused(self):
        with self.assertRaises(ValueError):
            impostor_check.check_position(trials=1, mutation="nothing", dumps=DUMPS)

    def test_the_made_trials_reach_the_degenerate_points(self):
        # The points where the console's product rule (a zero factor gives +0) and IEEE part: a quad of no size
        # and a camera with no extent of either axis; and those where one power is 2^(-inf).
        r = impostor_check.check_position(trials=400, seed=3, dumps=DUMPS)
        self.assertEqual(r.failed, 0, r.first_failure)
        for kind in ("size0", "level", "down", "flat", "nan"):
            self.assertGreaterEqual(r.kinds.get(kind, 0), 2, kind)
        self.assertLess(r.powered, r.trials)                        # some trials are not on numbers, and agree
        self.assertGreaterEqual(r.tilted, r.trials // 4)            # both weights strictly inside (0, 1)

    def test_without_the_consoles_product_rule_the_degenerate_points_disagree(self):
        # The check is of the rule too: an IEEE interpreter makes no number where the formula says the origin.
        r = impostor_check.check_position(trials=400, seed=3, dumps=DUMPS, zero_product=False)
        self.assertGreater(r.failed, 0)
        self.assertLessEqual(r.failed, r.kinds["size0"] + r.kinds["flat"] + r.kinds["nan"])

    def test_every_literal_is_where_the_table_says(self):
        # 1% in the formula alone fails; 1% in the formula and at the table's place in the microcode's constants
        # agrees again: name, place and use are one.
        for name in ("two", "one", "minus_one", "four"):
            with self.subTest(literal=name):
                alone = impostor_check.check_position(trials=120, seed=11, mutation=("literal", name), dumps=DUMPS)
                self.assertGreater(alone.failed, 0, f"{name} changed in the formula alone was not caught")
                both = impostor_check.check_position(trials=120, seed=11, shift=(name, 1.01), dumps=DUMPS)
                self.assertEqual(both.failed, 0, both.first_failure)

    def test_the_literal_table_is_the_frame_maps(self):
        # Frame-map section 12, "Literals" (the row of 0x129F...): c254 and c255 as the GPU held them.
        self.assertEqual(lf.IMPOSTOR_SHADER["vs_lit"], {254: (0.5, 0.25, 2.0, 0.0), 255: (0.0, 1.0, -1.0, 4.0)})
        for name, (reg, comp) in lf.IMPOSTOR_SHADER["vs_lit_at"].items():
            self.assertEqual(lf.impostor_literals("vs")[name], lf.IMPOSTOR_SHADER["vs_lit"][reg][comp])

    def test_the_registers_are_the_interpreters(self):
        # What the microcode reads on a made trial with the fog begun, by the interpreter's own list.
        used = impostor_check.used_registers(dumps=DUMPS)
        self.assertEqual(tuple(sorted(r for r in used if r < 248)), lf.IMPOSTOR_SHADER["reg"])
        self.assertEqual(sorted(r for r in used if r >= 248), sorted(lf.IMPOSTOR_SHADER["vs_lit"]))

    def test_trace_gives_the_register_after_an_instruction(self):
        case = impostor_check.made_case(seed=11, trial=5, dumps=DUMPS)
        word = impostor_check.case_word(case)
        r6 = impostor_check.trace(case, 12)[6]
        self.assertEqual(tuple(r6), word)                               # the fetch's result, before the raise
        raised = impostor_check.trace(case, 29)[6]
        self.assertEqual(raised[2], word[2] + word[3] * case.consts[8][2])
        self.assertEqual(impostor_check.trace(case, 21)[1][2], float(lf.impostor_corner(case.index)[1]))

    def test_the_check_compares_the_machines_outputs_with_the_formulas(self):
        # Not a formula against itself: one side is what ucode_eval.Machine exports for the vertex (the fetch
        # made by the machine), the other light_formulas alone on the word quad_words decoded.
        case = impostor_check.made_case(seed=11, trial=5, dumps=DUMPS)
        out, machine = impostor_check.run_microcode(case)
        self.assertEqual([f[1:] for f in machine.vfetches], [(0, 0, (case.index + 0.5) * 0.25)])
        t = impostor_check.evaluate_position(case)
        self.assertEqual(t.got, list(out["oPos"]) + list(out["o0"][:3]) + list(out["o1"][:2]))
        quad, k = lf.impostor_corner(case.index)
        c = {reg: case.consts[reg] for reg in lf.IMPOSTOR_SHADER["reg"]}
        own = lf.impostor_position(k, impostor_check.case_words(case)[quad], c)
        self.assertEqual(t.want, list(own["pos"]) + list(own["place"]) + list(own["uv"]))
        self.assertNotEqual(t.got, t.want)                              # two computations, equal to rounding
        self.assertTrue(t.ok)

    def test_a_corner_that_is_no_number_has_no_fourth_factor(self):
        # instr 71: the fourth factor of the rows c72..c75 is `|Q.x| >= 0`: 0 where Q.x is no number. Under unit
        # rows oPos.w is then +0 (the console's product rule on the three other terms) and oPos.x no number.
        case = impostor_check.made_case(seed=3, trial=27, dumps=DUMPS)
        self.assertEqual(case.kind, "nan")
        self.assertTrue(math.isnan(impostor_check.case_word(case)[3]))
        case.consts.update({reg: list(row) for reg, row in ROWS.items()})
        t = impostor_check.evaluate_position(case)
        self.assertTrue(t.ok, (t.got, t.want))
        self.assertTrue(math.isnan(t.got[0]) and math.isnan(t.want[0]))
        self.assertEqual((t.got[3], t.want[3]), (0.0, 0.0))

    def test_single_precision_is_measured_on_the_made_draws(self):
        n, median, p99, largest = impostor_check.single_gap(trials=200, seed=11, dumps=DUMPS)
        self.assertGreaterEqual(n, 150)
        self.assertTrue(0.0 < median <= p99 <= largest)
        self.assertLess(median, 1e-5)                                   # single precision is near, and not equal

    def test_the_command_line(self):
        said = []
        self.assertEqual(impostor_check.main(["--dumps", str(DUMPS), "formulas", "--trials", "60", "--seed", "11"], say=said.append), 0)
        self.assertEqual(said[-1], "all agree")
        self.assertTrue(any(line.startswith("position 60 / 60") for line in said), said)
        said = []
        self.assertEqual(impostor_check.main(["--dumps", str(DUMPS), "trace", "--instr", "29"], say=said.append), 0)
        self.assertTrue(any(line.strip().startswith("r6") for line in said))
        self.assertEqual(impostor_check.main(["--dumps", str(ROOT / "no-such-folder"), "formulas"], say=said.append), 1)


if __name__ == "__main__":
    unittest.main()
