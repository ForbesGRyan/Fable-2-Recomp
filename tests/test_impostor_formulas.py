"""The far woods' impostors as closed formulas (light_formulas.impostor_position, impostor_pixel, impostor_out,
impostor_fog) and their machine check against the microcode of vertex shader 129F7F2793DA9084 and pixel shader
4B1D3E6E8E58D3AC (impostor_check.py): the distant-trees sub-project, plan Tasks 4 and 5, spec criterion 1's first
half (made inputs; the captured rows are plan Task 6's)."""
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


C12, C29, C31X = (20.0, 35.0, 2.0, 0.46779), (48.0, 0.125, 998.4, 25.6), 0.366827     # the bridge, R 3.2
TEXEL = (0.5, 0.25, 0.125, 1.0)


class Pixel(unittest.TestCase):
    def test_the_formula_written_out(self):
        colour, alpha = lf.impostor_pixel(TEXEL, 40.0, C12, C29, C31X)
        light = 1.0 + 0.5 * (0.46779 - 1.0)                 # sat(0.1 * (40 - 35)) = 0.5
        for got, t in zip(colour, TEXEL):
            self.assertAlmostEqual(got, t * C31X * light, delta=1e-7)
        self.assertEqual(alpha, 0.0)                         # 40 units: nearer than c29.x

    def test_the_light_is_one_near_and_the_mean_term_far(self):
        near = lf.impostor_pixel(TEXEL, 30.0, C12, C29, C31X)[0]
        self.assertAlmostEqual(near[0], 0.5 * C31X, delta=1e-7)
        for d in (45.0, 300.0):
            self.assertAlmostEqual(lf.impostor_pixel(TEXEL, d, C12, C29, C31X)[0][0], 0.5 * C31X * 0.46779, delta=1e-7)

    def test_the_dissolve_is_three_steps_a_tree(self):       # spec, "Read for this design": no dither under offsets AA
        for d, want in ((49.9, 0), (50.0, 1), (53.9, 1), (54.0, 2), (200.0, 2)):
            alpha = lf.impostor_pixel((1.0, 1.0, 1.0, 1.0), d, C12, C29, C31X)[1]
            for x, y in ((0, 0), (1, 0), (0, 1), (3, 5)):
                self.assertEqual(lf.covered_samples(0xAA, x, y, alpha), want)

    def test_the_alpha_is_the_pictures_times_the_dissolve(self):
        self.assertAlmostEqual(lf.impostor_pixel((1.0, 1.0, 1.0, 0.5), 52.0, C12, C29, C31X)[1], 0.25)
        self.assertEqual(lf.impostor_pixel((1.0, 1.0, 1.0, 0.0), 500.0, C12, C29, C31X)[1], 0.0)

    def test_the_output(self):
        out = lf.impostor_out((0.25, 0.5, 1.0), (0.5, 0.5, 0.5), (0.125, 0.0, 0.25), 8.0)
        self.assertEqual(tuple(out), (2.0, 2.0, 6.0))

    def test_the_distance_is_from_the_eye_to_the_trees_place(self):
        self.assertEqual(lf.impostor_distance((1.0, 2.0, 3.0), (4.0, 6.0, 15.0)), 13.0)
        self.assertEqual(lf.impostor_distance((7.0, 7.0, 7.0), (7.0, 7.0, 7.0)), 0.0)
        self.assertTrue(math.isnan(lf.impostor_distance((math.nan, 0.0, 0.0), (0.0, 0.0, 0.0))))

    def test_the_alpha_test_is_greater(self):               # colour control 0xAA00001C: function 4, the test on
        self.assertEqual(0xAA00001C & 0xF, 0x8 | 4)
        self.assertTrue(lf.impostor_alpha_test(0.26, 0.25))
        self.assertFalse(lf.impostor_alpha_test(0.25, 0.25))
        self.assertFalse(lf.impostor_alpha_test(math.nan, 0.0))
        self.assertTrue(lf.impostor_alpha_test(1e-6, 0.0))
        self.assertFalse(lf.impostor_alpha_test(0.0, 0.0))  # an absent tree fails at any reference of [0, 1]

    def test_the_consoles_product_rule_at_the_pixels_degenerate_points(self):
        # A zero factor gives +0 whatever the other factor is: an absent tree's alpha is 0 for a picture alpha
        # that is no number; a near tree's light is 1 under an infinite c12.w; no exposure is no colour.
        self.assertEqual(lf.impostor_pixel((1.0, 1.0, 1.0, math.nan), 40.0, C12, C29, C31X)[1], 0.0)
        colour = lf.impostor_pixel(TEXEL, 30.0, (20.0, 35.0, 2.0, math.inf), C29, 2.0)[0]
        self.assertEqual(tuple(colour), (1.0, 0.5, 0.25))
        self.assertEqual(lf.impostor_pixel((math.inf, 1.0, 1.0, 1.0), 60.0, C12, C29, 0.0)[0][0], 0.0)
        self.assertEqual(tuple(lf.impostor_out((math.inf, 1.0, 1.0), (0.0, 0.5, 0.5), (0.25, 0.0, 0.0), 2.0)), (0.5, 1.0, 1.0))

    def test_b129_is_bit_one_of_the_pixel_word(self):
        # Bool constant N is bit N & 31 of register 0x4900 + (N >> 5) (the SDK's interpreter); the pixel word is
        # the fifth (b128..b159). The captured word has b136 alone.
        self.assertEqual(lf.IMPOSTOR_B129, 1 << (129 & 31))
        self.assertEqual(129 >> 5, 4)
        self.assertEqual(impostor_check.CAPTURED_PBOOL, 1 << (136 & 31))
        self.assertEqual(impostor_check.CAPTURED_PBOOL & lf.IMPOSTOR_B129, 0)

    def test_the_literals_are_the_captured_table(self):
        # c255 of the pixel shader in every row of out/native-evidence/distant-trees/impostor_consts.txt.
        self.assertEqual(lf.IMPOSTOR_SHADER["ps_lit"], {255: (0.0, 1.0, 0.1, 0.5)})
        self.assertEqual(lf.impostor_literals("ps"), {"one": 1.0, "tenth": 0.1})
        self.assertEqual(lf.IMPOSTOR_SHADER["ps_reg"], (12, 19, 20, 29, 31))


class Fog(unittest.TestCase):
    def tables(self):
        return (lambda s: [0.5, 0.25, 0.125, 0.0]), (lambda a, t: [0.2, 0.4, 0.8, 0.0])

    def test_before_the_start_nothing(self):
        tf16, tf17 = self.tables()
        got = lf.impostor_fog((0.0, 0.0, 5.0), (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (10.0, 0.0, 0.1, 0.01), tf16, tf17)
        self.assertEqual(got, ([1.0, 1.0, 1.0], [0.0, 0.0, 0.0]))

    def test_it_is_the_trees_form_without_the_scale(self):
        tf16, tf17 = self.tables()
        args = ((300.0, 40.0, 5.0), (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (10.0, 0.0, 0.1, 0.01), tf16, tf17)
        o2, o3 = lf.impostor_fog(*args)
        self.assertEqual((o2, o3), lf.vertex_fog(args[0], args[1], 1.0, *args[2:]))
        self.assertEqual(o2, [0.5, 0.25, 0.125])
        self.assertEqual(o3, [(1.0 - 0.5) * 0.2, (1.0 - 0.25) * 0.4, (1.0 - 0.125) * 0.8])

    def test_the_tables_are_read_at_the_distance_past_the_start_and_the_angle(self):
        seen = {}

        def tf16(s):
            seen["s"] = s
            return [0.5, 0.5, 0.5]

        def tf17(a, t):
            seen["a"], seen["t"] = a, t
            return [1.0, 1.0, 1.0]

        # The place 50 units from the eye along x; the start 10; the axis along the way from the eye to the place.
        lf.impostor_fog((50.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (10.0, 0.0, 0.005, 0.01), tf16, tf17)
        self.assertEqual(seen, {"s": 0.01 * 40.0, "a": 1.0, "t": 0.005 * 40.0})

    def test_a_place_that_is_no_number_is_before_the_start(self):
        tf16, tf17 = self.tables()
        got = lf.impostor_fog((math.nan, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (10.0, 0.0, 0.1, 0.01), tf16, tf17)
        self.assertEqual(got, ([1.0, 1.0, 1.0], [0.0, 0.0, 0.0]))


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class PixelAgainstMicrocode(unittest.TestCase):
    def test_pixel_and_fog_on_made_inputs(self):
        for check in (impostor_check.check_pixel, impostor_check.check_fog):
            r = check(trials=200, seed=11, dumps=DUMPS)
            self.assertEqual(r.failed, 0, r.first_failure)
            self.assertGreaterEqual(r.fogged, r.trials // 2)        # a fog check before the start proves nothing
            self.assertLess(r.worst, 1e-9)                          # the same arithmetic, not merely inside the bound

    def test_pixel_mutations_fail(self):
        for name in impostor_check.PIXEL_MUTATIONS:
            with self.subTest(mutation=name):
                check = impostor_check.check_fog if name in ("one_minus", "c8x") else impostor_check.check_pixel
                self.assertGreater(check(trials=200, seed=11, mutation=name, dumps=DUMPS).failed, 0, f"{name} was not caught")

    def test_the_mutations_are_the_seven_and_each_check_refuses_the_others(self):
        self.assertEqual(impostor_check.PIXEL_MUTATIONS, ("start", "tenth", "exposure", "sign", "picture", "one_minus", "c8x"))
        self.assertEqual(impostor_check.FOG_MUTATIONS, ("one_minus", "c8x"))
        with self.assertRaises(ValueError):
            impostor_check.check_pixel(trials=1, mutation="c8x", dumps=DUMPS)
        with self.assertRaises(ValueError):
            impostor_check.check_fog(trials=1, mutation="start", dumps=DUMPS)

    def test_the_made_pixels_reach_every_part_of_the_formula(self):
        r = impostor_check.check_pixel(trials=400, seed=3, dumps=DUMPS)
        self.assertEqual(r.failed, 0, r.first_failure)
        self.assertEqual(impostor_check.pixel_floors(r), [])
        self.assertGreaterEqual(r.absent, r.trials // 10)           # nearer than the dissolve's start: alpha 0
        self.assertGreaterEqual(r.dissolving, r.trials // 5)
        self.assertGreaterEqual(r.whole, r.trials // 5)
        self.assertGreaterEqual(r.falling, r.trials // 10)          # the light factor strictly between 1 and c12.w
        for kind in ("texel_nan", "light_inf", "colour_inf", "place_nan"):
            self.assertGreaterEqual(r.kinds.get(kind, 0), 2, kind)

    def test_without_the_consoles_product_rule_the_pixels_degenerate_points_disagree(self):
        r = impostor_check.check_pixel(trials=400, seed=3, dumps=DUMPS, zero_product=False)
        self.assertEqual(r.failed, sum(n for kind, n in r.kinds.items() if kind != "place_nan"))

    def test_a_place_that_is_no_number_is_an_absent_tree_in_the_pictures_colour(self):
        # Both saturates give 0 for no number (instr 12): light 1, alpha 0. The machine says the same.
        case = impostor_check.made_pixel(seed=3, trial=37, dumps=DUMPS)
        self.assertEqual(case.kind, "place_nan")
        got = impostor_check.run_pixel(case)[0]["oC0"]
        self.assertEqual(got[3], 0.0)
        self.assertTrue(all(math.isfinite(x) for x in got))
        self.assertEqual(list(got), impostor_check.formula_pixel(case))
        colour, alpha = lf.impostor_pixel(TEXEL, math.nan, C12, C29, C31X)
        self.assertEqual((tuple(colour), alpha), (tuple(t * C31X for t in TEXEL[:3]), 0.0))

    def test_the_made_draws_reach_both_sides_of_the_fogs_start(self):
        r = impostor_check.check_fog(trials=400, seed=3, dumps=DUMPS)
        self.assertEqual(r.failed, 0, r.first_failure)
        self.assertEqual(impostor_check.fog_floors(r), [])
        self.assertEqual(r.coords, r.fogged)                        # both fetches at the formula's coordinates
        self.assertGreaterEqual(r.near, r.trials // 10)             # before the start: exactly 1 and 0, no fetch
        self.assertEqual(r.fogged + r.near + r.unnumbered, r.trials)
        self.assertLessEqual(r.unnumbered, r.trials // 20)          # a place that is no number is not compared

    def test_every_pixel_literal_is_where_the_table_says(self):
        for name in ("one", "tenth"):
            with self.subTest(literal=name):
                alone = impostor_check.check_pixel(trials=120, seed=11, mutation=("literal", name), dumps=DUMPS)
                self.assertGreater(alone.failed, 0, f"{name} changed in the formula alone was not caught")
                both = impostor_check.check_pixel(trials=120, seed=11, shift=(name, 1.01), dumps=DUMPS)
                self.assertEqual(both.failed, 0, both.first_failure)

    def test_the_three_captured_views_constants(self):
        self.assertEqual(sorted(impostor_check.CAPTURED_VIEWS), ["022921", "054648", "074119"])
        for view, v in impostor_check.CAPTURED_VIEWS.items():
            with self.subTest(view=view):
                self.assertEqual(tuple(v["c29"]), (48.0, 0.125, 998.4, 25.6))
                self.assertEqual(tuple(v["c12"][:3]), (20.0, 35.0, 2.0))
                r = impostor_check.check_pixel(trials=120, seed=11, dumps=DUMPS, view=view)
                self.assertEqual(r.failed, 0, r.first_failure)
                self.assertGreater(min(r.absent, r.dissolving, r.whole, r.falling), 0)

    def test_the_check_runs_the_pixel_shader_on_the_picture_at_o1(self):
        case = impostor_check.made_pixel(seed=11, trial=4, dumps=DUMPS)
        out, machine = impostor_check.run_pixel(case)
        self.assertEqual([(f[1], list(f[2])) for f in machine.fetches], [(0, case.regs[1][:2])])
        want = impostor_check.formula_pixel(case)
        self.assertNotEqual(list(out["oC0"]), [0.0] * 4)
        for g, w in zip(out["oC0"], want):
            self.assertAlmostEqual(g, w, delta=1e-12 * max(1.0, abs(g)))
        self.assertEqual(machine.used_consts - set(range(248, 256)), set(lf.IMPOSTOR_SHADER["ps_reg"]))
        self.assertEqual(machine.used_consts & set(range(248, 256)), set(lf.IMPOSTOR_SHADER["ps_lit"]))

    def test_with_b129_set_the_microcode_reads_the_ground_fog(self):
        # The block the formula does not model (instr 16 to 57): with the bool set the shader goes on to fetch tf12.
        self.assertTrue(impostor_check.b129_reads_the_ground_fog(DUMPS))
        case = impostor_check.made_pixel(seed=11, trial=4, dumps=DUMPS)
        clear = impostor_check.run_pixel(case)[0]["oC0"]
        self.assertEqual(impostor_check.run_pixel(case, bools={136})[0]["oC0"], clear)      # the captured word's bit
        with self.assertRaises(KeyError):
            impostor_check.run_pixel(case, bools={129})

    def test_the_pixel_command(self):
        said = []
        self.assertEqual(impostor_check.main(["--dumps", str(DUMPS), "pixel", "--trials", "80", "--seed", "11"], say=said.append), 0)
        self.assertEqual(said[-1], "all agree")
        self.assertTrue(any(line.startswith("pixel 80 / 80") for line in said), said)
        self.assertTrue(any(line.startswith("fog ") and line.endswith(": OK") for line in said), said)
        self.assertEqual(sum(line.startswith("pixel with the constants of capture") for line in said), 3)
        self.assertTrue(any(line.startswith("bool b129 is bit 1") for line in said), said)


def made_capture(folder, read=True, size=14.9765625, shadow_names=False):
    """A capture of one frame with one in-scene 129F row of two quads, its storage and the frame's fog tables."""
    import json
    words = [(128.875, 121.625, 31.09375, size), (183.75, 322.75, 45.03125, 12.96875)]     # two quads of capture D1
    bank = [0.0] * 1024
    c = consts({8: [0.01, 0.02, 0.9, 0.0], 46: [0.8, 0.6, 0.0, 0.0], 47: [80.0, 150.0, 50.0, 0.0],
                72: [0.977, 0.0, 0.0, 0.0], 73: [0.0, 1.737, 0.0, 0.0], 74: [0.0, 0.0, 1.00002, -0.100002], 75: [0.0, 0.0, 1.0, 0.0],
                110: [-0.8, 0.1, 0.59, 0.0], 111: [19.0, 1900.0, 1.0, 0.0005], 113: [1.0, 0.02, 0.01, 0.98], 114: [1.5, 2.0, 0.0, 0.0],
                115: [0.6, -0.8, 0.0, 70.0], 116: [0.14, 0.1, 0.98, -76.0], 117: [0.79, 0.59, -0.18, -140.0]})
    for reg, v in c.items():
        bank[4 * reg:4 * reg + 4] = v
    pix = {12: [20.0, 35.0, 2.0, 0.47], 19: [80.0, 150.0, 50.0, 1.0], 20: [0.005, 0.07, 0.09, 9.0], 29: [48.0, 0.125, 998.4, 25.6],
           31: [0.365, 0.0, 0.0, 0.0]}
    pfull = "".join(struct.pack(">4f", *pix.get(reg, [0.0] * 4)).hex().upper() for reg in range(256))
    storage = b"".join(struct.pack(">4e", *w) for w in words) + bytes(16)     # as the game fills it (big-endian halves x, y, z, s); the second half unused
    address = 0xFF762700
    names = ["0x%08X" % (impostor_check.gpu_address(address) | 3), "0x%08X" % (0x10000000 | 8 << 2 | 2)]
    other = ["0x1B14FC03", "0x10000A02"]
    lit = lf.IMPOSTOR_SHADER["vs_lit"]
    plit = lf.IMPOSTOR_SHADER["ps_lit"]
    verts = {"kind": "verts", "frame": 1, "prim": 13, "count": 8, "in_scene": True, "pbool": "0x00000100", "pbool_load": "0x00000110",
             "storage": "0x%08X" % address, "storage_dwords": 8, "n": 5,
             "vfetch": {"shadow": ["0x1", "0x0", "0x1", "0x0"] + (names if shadow_names else other),
                        "packet": ["0x1", "0x0", "0x1", "0x0"] + (other if shadow_names else names)},
             "ps_hash": "0x" + lf.IMPOSTOR_SHADER["ps"], "pfull": pfull,
             "ps_lit": [{"reg": 508, "v": [0.0] * 12 + list(plit[255])}],
             "vs": {"hash": "0x" + lf.IMPOSTOR_SHADER["vs"]}, "vs_lit": [{"reg": 252, "v": [0.0] * 8 + list(lit[254]) + list(lit[255])}],
             "bank": bank}
    data = {"kind": "vertsdata", "frame": 1, "n": 5, "storage": verts["storage"], "want": 32, "read": read}
    if read:
        data["bytes"] = storage.hex().upper()
    frame = {"kind": "frame", "frame": 1, "fog": {"transmit": [0.9 - 0.002 * (i // 3) for i in range(768)],
                                                  "scatter": [0.3 + 0.001 * (i // 3) for i in range(192)]}}
    path = Path(folder) / "native_discovery_made.jsonl"
    path.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in (verts, data, frame)), encoding="utf-8")
    return path


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class CapturedRows(unittest.TestCase):
    """Plan Tasks 3 and 6: the readers of a capture's 129F rows and the formulas on a row's own registers."""

    def run_rows(self, **made):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            path = made_capture(folder, **made)
            return impostor_check.check_rows(path, impostor_check.ROW_MUTATIONS, DUMPS), path

    def test_four_corners_a_quad_are_counted_and_agree(self):
        (r, caught), _ = self.run_rows()
        self.assertEqual((r.rows, r.quads, r.corners, r.failed, r.unread), (1, 2, 8, 0, 0))
        self.assertEqual((r.fog, r.fog_failed, r.fogged), (8, 0, 8))
        self.assertEqual((r.pixels, r.pixel_failed), (2, 0))
        self.assertLess(r.worst, 1e-9)
        self.assertLess(r.single, impostor_check.SINGLE_TOLERANCE)
        self.assertEqual(r.above + r.below, 8)
        self.assertEqual(set(r.vs_used) - set(range(248, 256)), set(lf.IMPOSTOR_SHADER["reg"]))
        self.assertEqual(set(r.ps_used) - set(range(248, 256)), set(lf.IMPOSTOR_SHADER["ps_reg"]))
        self.assertEqual(impostor_check.rows_problems(r, caught), [])
        for name in impostor_check.ROW_MUTATIONS:
            self.assertGreater(caught[name], 0, name)

    def test_an_unread_storage_is_counted_and_fails_the_run(self):
        (r, caught), _ = self.run_rows(read=False)
        self.assertEqual((r.rows, r.unread, r.corners), (1, 1, 0))
        problems = impostor_check.rows_problems(r, caught)
        self.assertTrue(any("without their storage" in p for p in problems), problems)
        self.assertTrue(any("no corner was compared" in p for p in problems), problems)

    def test_a_mutation_nobody_catches_fails_the_run(self):
        (r, _), _ = self.run_rows()
        self.assertEqual(impostor_check.rows_problems(r, {"raise": 0}), ["mutation raise is NOT CAUGHT"])

    def test_the_rows_command(self):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            said = []
            self.assertEqual(impostor_check.main(["--dumps", str(DUMPS), "rows", str(made_capture(folder))], say=said.append), 0)
            self.assertEqual(said[-1], "all agree")
            self.assertIn("8 corners: 8 agree", said[0])
            said = []
            self.assertEqual(impostor_check.main(["--dumps", str(DUMPS), "rows", str(made_capture(folder, read=False))], say=said.append), 1)
            self.assertEqual(said[-1], "1 captures FAILED")

    def test_the_gpu_address_is_the_layouts(self):
        # src/native/capture/xdk_layout.h, GpuAddress: the 0xE0000000 view lies 0x1000 above its low 29 bits.
        self.assertEqual(impostor_check.gpu_address(0xFF762700), 0x1F763700)
        self.assertEqual(impostor_check.gpu_address(0xE0000000), 0x00001000)
        self.assertEqual(impostor_check.gpu_address(0xA1234560), 0x01234560)
        self.assertEqual(impostor_check.gpu_address(0xDFFFFFFC), 0x1FFFFFFC)

    def test_which_fetch_constant_names_the_storage(self):
        import json
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            row = json.loads(made_capture(folder).read_text().splitlines()[0])
            self.assertEqual(impostor_check.storage_fetch(row), {"shadow": False, "packet": True, "endian": 2, "pad": 4})
            row = json.loads(made_capture(folder, shadow_names=True).read_text().splitlines()[0])
            self.assertEqual(impostor_check.storage_fetch(row), {"shadow": True, "packet": False, "endian": 2, "pad": 4})
            row["storage_dwords"] = 12                     # another size: neither names it
            self.assertEqual(impostor_check.storage_fetch(row), {"shadow": False, "packet": False, "endian": None, "pad": None})
            row["vfetch"] = {"shadow": None, "packet": None}
            self.assertEqual(impostor_check.storage_fetch(row), {"shadow": None, "packet": None, "endian": None, "pad": None})

    def test_the_builders_pair_is_the_captured_one(self):
        # Review of phase 12a, I1: D1's first 129F row (storage 0xFF762700, 224 dwords) has the packet pair
        # 0x1F763703, 0x10000382: the size dword is (dwords << 2) | 0x10000002, not "| 2".
        self.assertEqual(impostor_check.storage_pair(0xFF762700, 224), (0x1F763703, 0x10000382))
        self.assertNotEqual(impostor_check.storage_pair(0xFF762700, 224)[1], (224 << 2) | 2)
        self.assertEqual(impostor_check.storage_pair(0x4A120000, 12, endian=1), (0x0A120003, 0x10000031))
        row = {"storage": "0xFF762700", "storage_dwords": 224,
               "vfetch": {"shadow": None, "packet": ["0x00000001", "0x00000000", "0x00000001", "0x00000000", "0x1F763703", "0x10000382"]}}
        self.assertEqual(impostor_check.storage_fetch(row), {"shadow": None, "packet": True, "endian": 2, "pad": 4})

    def test_the_census_counts_every_endian_and_order(self):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            pairs = impostor_check.impostor_rows(made_capture(folder))
            quads, held = impostor_check.word_census(pairs)
            self.assertEqual((quads, len(held)), (2, 96))
            self.assertEqual(held[(2, (1, 0, 3, 2))], 2)
            self.assertEqual(held[(0, (1, 0, 3, 2))], 0)           # bytes not swapped: halves that are no plausible quad
            # the rule alone does not single the order out (every component is a plausible place and size here):
            self.assertGreater(sum(1 for n in held.values() if n == 2), 1)
            # a size of 2,000 units is not plausible where the table puts the size
            pairs = impostor_check.impostor_rows(made_capture(folder, size=2000.0))
            self.assertEqual(impostor_check.word_census(pairs)[1][(2, (1, 0, 3, 2))], 1)

    def test_the_words_command_names_the_packet(self):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            said = []
            self.assertEqual(impostor_check.main(["words", str(made_capture(folder)), "--quiet"], say=said.append), 0)
            self.assertTrue(any("shadow False, packet True, endian 2, padding bits 0x04: 1 rows" in line for line in said), said)
            said = []
            self.assertEqual(impostor_check.main(["words", str(made_capture(folder, size=2000.0)), "--quiet"], say=said.append), 1)

    def test_identities_and_bounds_of_a_made_row(self):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            path = made_capture(folder)
            found = {name.split(" ")[0] + " " + name.split(" ")[1]: (held, total) for name, held, total, _, _ in impostor_check.identities(path)}
            self.assertEqual(found["vertex c47.xyz"], (1, 1))
            self.assertEqual(found["pixel c31.x"], (0, 1))                 # 0.365 * 9 is not 1 (two names share the key: the last)
            spans, size, rows, quads = impostor_check.bounds([path])
            self.assertEqual((rows, quads, size), (1, 2, (12.96875, 14.9765625)))
            self.assertEqual(spans[("vs", 8)][2], [impostor_check.f32(0.9)] * 2)
            self.assertEqual(spans[("ps", 29)][0], [48.0, 48.0])


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class Pins(unittest.TestCase):
    """tools/xdk_sigmatch/impostor_pins.py (plan Task 6): the mirrors' pins come from the reference alone and are
    held here to the microcode."""

    @classmethod
    def setUpClass(cls):
        import impostor_pins
        cls.pins = impostor_pins

    def test_three_sections_and_their_lines(self):
        lines = self.pins.lines()
        heads = [i for i, line in enumerate(lines) if line.startswith("// ")]
        self.assertEqual([lines[i][3:lines[i].index(":")] for i in heads], ["impostor position pins", "impostor pixel pins", "impostor fog pins"])
        self.assertEqual((heads[1] - heads[0] - 1, heads[2] - heads[1] - 1, len(lines) - heads[2] - 1),
                         (len(self.pins.CAPTURED) + len(self.pins.MADE), len(self.pins.PIXEL), len(self.pins.FOG)))
        self.assertEqual(len(self.pins.CAPTURED), 10)
        for line in lines:
            if not line.startswith("// "):
                self.assertRegex(line, r"^    \{[0-9A-Fa-fxu{}, ]+\},(  // (captured|made))?$")
        inc = self.pins.inc_lines()
        self.assertEqual([line for line in inc if line.startswith("#ifdef")],
                         ["#ifdef FABLE2_IMPOSTOR_PINS_POSITION", "#ifdef FABLE2_IMPOSTOR_PINS_PIXEL", "#ifdef FABLE2_IMPOSTOR_PINS_FOG"])
        self.assertEqual(sum(line == "#endif" for line in inc), 3)
        self.assertEqual([line for line in inc if not line.startswith(("#", "// Generated", "// The pins", "// FABLE2"))], lines)

    def test_every_position_pin_is_the_microcodes_within_the_bound(self):
        text = impostor_check._text(DUMPS)
        corners, sides, worst = set(), set(), 0.0
        for row in self.pins.CAPTURED + self.pins.MADE:
            (corner, word, regs), (pos, place, uv) = self.pins.position_pin(row)
            consts = {reg: [0.0] * 4 for reg in range(256)}
            consts.update({reg: list(regs[i]) for i, reg in enumerate(lf.IMPOSTOR_SHADER["reg"])})
            consts.update(impostor_check._literal_registers("vs"))
            case = impostor_check.Case(text, consts, struct.pack(">4e", *word), corner, lambda s: [1.0] * 3, lambda a, t: [0.0] * 3)
            out, m = impostor_check.run_microcode(case)
            got = [float(x) for x in out["oPos"]] + [float(x) for x in out["o0"][:3]] + [float(x) for x in out["o1"][:2]]
            ok, error = impostor_check._compare(got, list(pos) + list(place) + list(uv), impostor_check.SINGLE_TOLERANCE)
            self.assertTrue(ok, (row[0], error))
            worst = max(worst, error)
            corners.add(corner)
            sides.add(m.r[2][3] == 1.0)
            for v in pos + place + uv:                                  # a single: its own rounding
                self.assertEqual(v, impostor_check.f32(v))
        self.assertEqual((corners, sides), ({0, 1, 2, 3}, {True, False}))
        # the pins are the single-precision reference (ruling 26), not the double one rounded: they differ somewhere
        differ = 0
        for row in self.pins.CAPTURED + self.pins.MADE:
            (corner, word, regs), (pos, _, _) = self.pins.position_pin(row)
            c = {reg: list(regs[i]) for i, reg in enumerate(lf.IMPOSTOR_SHADER["reg"])}
            differ += tuple(impostor_check.f32(v) for v in lf.impostor_position(corner, word, c)["pos"]) != tuple(pos)
        self.assertGreater(differ, 0)

    def test_the_pixel_pins_cross_the_test_and_the_mask(self):
        ptext = impostor_check._text(DUMPS, impostor_check.PS, "frag")
        alphas = []
        for row in self.pins.PIXEL:
            (texel, place, eye, c12, c29, c31x, o2, o3, c20w), (d, rgb, alpha) = self.pins.pixel_pin(row)
            consts = {reg: [0.0] * 4 for reg in range(256)}
            consts.update({12: list(c12), 19: list(eye) + [1.0], 20: [0.0, 0.0, 0.0, c20w], 29: list(c29), 31: [c31x, 0.0, 0.0, 0.0]})
            consts.update(impostor_check._literal_registers("ps"))
            regs = {0: list(place) + [0.0], 1: [0.25, 0.75, 0.0, 0.0], 2: list(o2) + [0.0], 3: list(o3) + [0.0]}
            case = impostor_check.PixelCase(ptext, consts, regs, lambda u, v, texel=texel: list(texel), d, True)
            out, _ = impostor_check.run_pixel(case, bools={132, 136})
            ok, _ = impostor_check._compare([float(x) for x in out["oC0"]], list(rgb) + [alpha], impostor_check.PIXEL_TOLERANCE)
            self.assertTrue(ok, row)
            alphas.append(alpha)
        ref = 128.0 / 255.0
        self.assertEqual([a > ref for a in alphas], [False, False, False, False, True, True, True, True, False])
        self.assertEqual([a >= 0.75 for a in alphas], [False, False, False, False, False, True, True, True, False])
        self.assertAlmostEqual(alphas[3], 0.5, delta=1e-5)             # 52 units: a half is not above the reference
        self.assertLess(alphas[3], ref)

    def test_the_fog_pins_begin_at_the_start(self):
        runs = []
        for row in self.pins.FOG:
            (place, eye, c110, c111, transmit, scatter), (ran, coords, o2, o3) = self.pins.fog_pin(row)
            runs.append(ran)
            if ran:
                self.assertEqual(tuple(o2), tuple(transmit))
                for i in range(3):
                    self.assertAlmostEqual(o3[i], (1.0 - transmit[i]) * scatter[i], delta=1e-12)
                self.assertTrue(0.0 <= coords[0] <= 1.0 and coords[1] > 0.0 and 0.0 <= coords[2] <= 1.0)
            else:
                self.assertEqual((tuple(o2), tuple(o3), tuple(coords)), ((1.0, 1.0, 1.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)))
        self.assertEqual(runs, [False, False, True, True, True, True])

    def test_the_captured_pins_are_the_captures_rows(self):
        import capture_replay
        seen = 0
        for (stamp, frame, place, quad), (corner, word, regs) in zip(self.pins.CAPTURED_ROWS, self.pins.CAPTURED):
            path = capture_replay.LOGS / ("native_discovery_20261011_%s.jsonl" % stamp)
            if not path.is_file():
                continue
            row, data = [(r, d) for r, d in impostor_check.impostor_rows(path) if r["frame"] == frame][place]
            self.assertEqual(tuple(self.pins.bits(v) for v in impostor_check.quad_words(row, data)[quad]), tuple(word))
            c = impostor_check.row_registers(row)
            self.assertEqual(tuple(tuple(self.pins.bits(v) for v in c[reg]) for reg in lf.IMPOSTOR_SHADER["reg"]), tuple(regs))
            seen += 1
        if not seen:
            self.skipTest("none of the four discovery captures is in the logs folder")


def made_record_capture(folder, change=None):
    """made_capture's frame with what a product run adds (plan Task 8): the row's fetch constants and state words,
    and the frame row's "impostors" as capture.cpp writes it for that row. `change(block)`: applied to the block."""
    import json
    path = made_capture(folder)
    verts, data, frame = [json.loads(line) for line in path.read_text().splitlines()]
    verts["tf"] = {"0": ["0x8200D802", "0x12704043", "0x001FE0FF", "0x00A80C14", "0x000000C3", "0x12724A18"],
                   "16": ["0x02004802", "0x1FAB4086", "0x000000FF", "0x00A80D10", "0x00000003", "0x00000018"],
                   "17": ["0x00804802", "0x1FAB509A", "0x0000003F", "0x00A80C14", "0x00000003", "0x00000018"]}
    verts["rs"] = {"2104": "0x0000000F", "210E": "0x3F008081", "2200": "0x00708766", "2201": "0x00010001", "2202": "0xAA00001C"}
    verts["surface_info"] = "0x11810460"
    want = impostor_check.record_of_row(verts, data)
    record = {"place": 1059, "ps": "0x%016X" % want["ps"], "vs": "0x%016X" % want["vs"], "quads": want["quads"], "first": 0,
              "pbool": "0x%08X" % want["pbool"], "vs_regs": "".join(want["vs_regs"]), "ps_regs": "".join(want["ps_regs"]),
              "fetch": ["0x%08X" % x for x in want["fetch"]], "fog_fetch": [["0x%08X" % x for x in six] for six in want["fog_fetch"]],
              "vfetch": ["0x%08X" % x for x in want["vfetch"]], "depth": "0x%08X" % want["depth"], "blend": "0x%08X" % want["blend"],
              "colour_control": "0x%08X" % want["colour_control"], "mask": want["mask"], "surface": "0x%08X" % want["surface"],
              "alpha_ref": "0x%08X" % want["alpha_ref"], "words": "".join(want["words"])}
    frame["impostors"] = {"mode": "flat", "seen": 1, "taken": 1, "quads": 2, "reason": {}, "records": [record]}
    frame["in_bracket"] = 1060          # the scene's hooked calls: this one typed in-scene row and 1,059 without a row
    if change:
        change(frame["impostors"])
    path.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in (verts, data, frame)), encoding="utf-8")
    return path


def _flip(text, digit):
    """`text` (hex) with one bit of hex digit `digit` changed."""
    return text[:digit] + "%X" % (int(text[digit], 16) ^ 1) + text[digit + 1:]


class Records(unittest.TestCase):
    """Plan Task 8, criterion 2: a frame row's impostor records against the frame's rows, bit for bit."""

    def problems(self, change=None):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            return impostor_check.check_records(made_record_capture(folder, change))

    def test_a_record_that_is_its_row_has_no_problem(self):
        self.assertEqual(self.problems(), (1, 1, []))

    def test_the_record_holds_the_last_writers_bool_word_and_the_packet(self):
        import json
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            record = json.loads(made_record_capture(folder).read_text().splitlines()[2])["impostors"]["records"][0]
        self.assertEqual(record["pbool"], "0x00000110")                                    # "pbool_load", not the shadow's 0x100
        self.assertEqual(record["vfetch"], ["0x1F763703", "0x10000022"])                   # the packet's pair, bit 28 and all
        self.assertEqual(record["words"][:32], "4300E00042F3400041F8C000416FA000")         # x, y, z, s of the first quad
        self.assertEqual(len(record["vs_regs"]), 8 * 56)
        self.assertEqual(len(record["ps_regs"]), 8 * 20)

    def test_one_difference_is_one_problem(self):
        def one(change, word):
            frames, records, problems = self.problems(change)
            self.assertEqual(len(problems), 1, problems)
            self.assertIn(word, problems[0])

        one(lambda b: b["records"][0].__setitem__("vs_regs", _flip(b["records"][0]["vs_regs"], 8 * 9 + 7)), '"vs_regs"')    # one register bit
        one(lambda b: b["records"][0].__setitem__("ps_regs", _flip(b["records"][0]["ps_regs"], 3)), '"ps_regs"')
        one(lambda b: b["records"][0].__setitem__("words", _flip(b["records"][0]["words"], 8 * 5 + 7)), '"words"')         # one word bit
        one(lambda b: b["records"][0].__setitem__("quads", 3), '"quads"')                                                   # the quad count
        one(lambda b: b.__setitem__("quads", 3), "quads 3")
        one(lambda b: b.__setitem__("reason", {"quad": 1}), "reasons counted: quad: 1")
        one(lambda b: b["records"][0].__setitem__("pbool", "0x00000100"), '"pbool"')                                        # the device's shadow
        one(lambda b: b["records"][0].__setitem__("vfetch", ["0x1F763703", "0x00000022"]), '"vfetch"')                      # the pair without bit 28
        one(lambda b: b["records"][0].__setitem__("alpha_ref", "0x3F000000"), '"alpha_ref"')
        one(lambda b: b["records"][0]["fog_fetch"].reverse(), '"fog_fetch"')
        one(lambda b: b["records"][0].__setitem__("first", 1), "begin at quad 1")
        one(lambda b: b["records"][0].__setitem__("surface", "0x00000000"), '"surface"')

    def test_a_place_is_held_to_the_rows(self):
        # review of Tasks 8 and 9, Minor 1: 100 added to every place of a frame went unreported
        import json
        import tempfile
        self.assertEqual(self.problems(lambda b: b["records"][0].__setitem__("place", 0))[2], [])          # the first of the scene's calls
        frames, records, problems = self.problems(lambda b: b["records"][0].__setitem__("place", 1060))
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("its place 1060 is not among the scene's calls where its row lies (0 typed rows before it, 1059 calls without a row)", problems[0])
        with tempfile.TemporaryDirectory() as folder:
            path = made_record_capture(folder)
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            for calls in (None, 0):
                rows[2].pop("in_bracket", None)
                if calls is not None:
                    rows[2]["in_bracket"] = calls
                path.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in rows), encoding="utf-8")
                problems = impostor_check.check_records(path)[2]
                self.assertEqual(len(problems), 1, problems)
                self.assertIn('"in_bracket"', problems[0])

    def test_seen_is_the_frames_rows(self):
        frames, records, problems = self.problems(lambda b: b.__setitem__("seen", 3))
        self.assertEqual(len(problems), 2)                              # seen is not the rows', and taken is not seen
        self.assertIn("seen 3, the frame has 1 129F rows", problems[0])

    def test_a_frame_without_records_is_said(self):
        import json
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            path = made_record_capture(folder)
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            rows[2]["impostors"] = "off"
            path.write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in rows), encoding="utf-8")
            frames, records, problems = impostor_check.check_records(path)
            self.assertEqual((frames, records, len(problems)), (0, 0, 1))
            self.assertIn("the switch was off", problems[0])
            said = []
            self.assertEqual(impostor_check.main(["records", str(path)], say=said.append), 1)
            self.assertEqual(said[-1], "1 captures FAILED")
            said = []
            self.assertEqual(impostor_check.main(["records", str(made_record_capture(folder))], say=said.append), 0)
            self.assertEqual(said[-1], "every record equals its rows bit for bit")

    def test_a_component_that_is_no_number_is_held_as_no_number(self):
        # (ledger ruling 34: an unread component is carried whatever it holds; a row prints it as null)
        self.assertTrue(impostor_check._same_floats("7FC000003F800000", [None, "3F800000"]))
        self.assertFalse(impostor_check._same_floats("7F8000003F800000", [None, "3F800000"]))      # an infinity is a number's bits
        self.assertFalse(impostor_check._same_floats("3F8000003F800000", [None, "3F800000"]))
        self.assertFalse(impostor_check._same_floats("7FC00000", [None, "3F800000"]))

if __name__ == "__main__":
    unittest.main()
