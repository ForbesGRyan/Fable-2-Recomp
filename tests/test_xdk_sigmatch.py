import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sigmatch", ROOT / "tools" / "xdk_sigmatch" / "sigmatch.py")
sm = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sm)

BASE = 0x82000000


def be(*words):
    return b"".join(struct.pack(">I", w & 0xFFFFFFFF) for w in words)


def make_image(code_words, text_at=0x1000, pdata=()):
    """Synthetic image: .pdata at BASE+0x100, .text at BASE+text_at."""
    data = bytearray(text_at + 4 * len(code_words))
    data[text_at:text_at + 4 * len(code_words)] = be(*code_words)
    pd = b"".join(struct.pack(">II", start, (length_words << 8)) for start, length_words in pdata)
    data[0x100:0x100 + len(pd)] = pd
    sections = [
        {"name": ".pdata", "address": BASE + 0x100, "size": len(pd), "executable": False},
        {"name": ".text", "address": BASE + text_at, "size": 4 * len(code_words), "executable": True},
    ]
    return sm.Image(BASE, bytes(data), sections), BASE + 0x100, len(pd)


def bl(src, dst):
    return 0x48000001 | ((dst - src) & 0x03FFFFFC)


class MaskTests(unittest.TestCase):
    def test_branch_displacements_masked_at_every_level(self):
        for level in ("strict", "loose"):
            self.assertEqual(sm.mask_for(0x48001235, level), 0xFC000003)  # bl
            self.assertEqual(sm.mask_for(0x4182000C, level), 0xFFFF0003)  # beq

    def test_immediates_masked_only_when_loose(self):
        lis = 0x3D608200   # lis r11,0x8200
        lwz = 0x816B1234   # lwz r11,0x1234(r11)
        self.assertEqual(sm.mask_for(lis, "strict"), 0xFFFFFFFF)
        self.assertEqual(sm.mask_for(lis, "loose"), 0xFFFF0000)
        self.assertEqual(sm.mask_for(lwz, "loose"), 0xFFFF0000)

    def test_stack_relative_kept_when_loose(self):
        stw_sp = 0x91810008  # stw r12,8(r1)
        self.assertEqual(sm.mask_for(stw_sp, "loose"), 0xFFFFFFFF)


class PdataTests(unittest.TestCase):
    def test_parse_pdata(self):
        img, addr, size = make_image([0x60000000] * 8, pdata=[(BASE + 0x1000, 4), (BASE + 0x1010, 4)])
        funcs = sm.parse_pdata(img, addr, size)
        self.assertEqual(funcs, {BASE + 0x1000: 16, BASE + 0x1010: 16})

    def test_parse_pdata_skips_invalid(self):
        img, addr, size = make_image([0x60000000] * 4,
                                     pdata=[(BASE + 0x1000, 4), (BASE + 0x1000, 0), (BASE + 0x100, 2), (0, 0)])
        funcs = sm.parse_pdata(img, addr, size)
        self.assertEqual(funcs, {BASE + 0x1000: 16})


def two_images(ref_code, tgt_code, ref_pdata, tgt_pdata):
    r, ra, rs = make_image(ref_code, pdata=ref_pdata)
    t, ta, ts = make_image(tgt_code, pdata=tgt_pdata)
    return r, sm.parse_pdata(r, ra, rs), t, sm.parse_pdata(t, ta, ts)


class ReferenceTests(unittest.TestCase):
    def test_unique_loose_match(self):
        ref = [0x3D608200, 0x816B1234, 0x4E800020]
        tgt = [0x60000000, 0x3D608300, 0x816B5678, 0x4E800020]
        r, rf, t, tf = two_images(ref, tgt, [(BASE + 0x1000, 3)], [(BASE + 0x1000, 1), (BASE + 0x1004, 3)])
        res = sm.match_references(r, rf, t, tf, {"SetIndices": BASE + 0x1000})
        self.assertEqual(res[0]["target"], BASE + 0x1004)
        self.assertEqual(res[0]["confidence"], "loose")

    def test_ambiguous_candidates_reported(self):
        f = [0x38600000, 0x4E800020]
        r, rf, t, tf = two_images(f, f + f, [(BASE + 0x1000, 2)], [(BASE + 0x1000, 2), (BASE + 0x1008, 2)])
        res = sm.match_references(r, rf, t, tf, {"Dup": BASE + 0x1000})
        self.assertIsNone(res[0]["target"])
        self.assertEqual(res[0]["confidence"], "ambiguous")
        self.assertEqual(res[0]["candidates"], [BASE + 0x1000, BASE + 0x1008])

    def test_no_match_reported(self):
        r, rf, t, tf = two_images([0x38600000, 0x4E800020], [0x80800001, 0x4E800020],
                                  [(BASE + 0x1000, 2)], [(BASE + 0x1000, 2)])
        res = sm.match_references(r, rf, t, tf, {"Gone": BASE + 0x1000})
        self.assertEqual(res[0]["confidence"], "none")
        self.assertLess(res[0].get("score", 0.0), sm.FUZZY_MIN_SCORE)


class CallgraphTests(unittest.TestCase):
    def test_callee_propagated(self):
        # parent at +0x1000 calls child at +0x1010 (ref) / +0x1020 (target)
        p = BASE + 0x1000
        ref = [bl(p, BASE + 0x1010), 0x4E800020, 0x60000000, 0x60000000, 0x38600007, 0x4E800020]
        tgt = [bl(p, BASE + 0x1020), 0x4E800020] + [0x60000000] * 6 + [0x38600007, 0x4E800020]
        r, rf, t, tf = two_images(ref, tgt, [(p, 2), (BASE + 0x1010, 2)], [(p, 2), (BASE + 0x1020, 2)])
        res = sm.match_references(r, rf, t, tf, {"Parent": p})
        res = sm.propagate(r, rf, t, tf, res)
        child = [x for x in res if x["name"] == "Parent.callee0"][0]
        self.assertEqual(child["target"], BASE + 0x1020)
        self.assertEqual(child["confidence"], "callgraph")

    def test_callgraph_conflict_dropped(self):
        # A (2 words) and B (3 words, distinct shape) both call C in the
        # reference; in the target A calls X and B calls Y.
        a, b = BASE + 0x1000, BASE + 0x1008
        c = BASE + 0x1014
        li1, li2, blr = 0x38600001, 0x38600002, 0x4E800020
        ref = [bl(a, c), blr, li2, bl(b + 4, c), blr, li1, blr]
        x, y = BASE + 0x1014, BASE + 0x101C
        tgt = [bl(a, x), blr, li2, bl(b + 4, y), blr, li1, blr, li1, blr]
        r, rf, t, tf = two_images(ref, tgt, [(a, 2), (b, 3), (c, 2)], [(a, 2), (b, 3), (x, 2), (y, 2)])
        res = sm.match_references(r, rf, t, tf, {"A": a, "B": b})
        res = sm.propagate(r, rf, t, tf, res)
        conflicts = [e for e in res if e["confidence"] == "conflict"]
        self.assertEqual(len(conflicts), 1)
        self.assertIsNone(conflicts[0]["target"])

    def test_markdown_lists_every_result(self):
        md = sm.render_markdown([
            {"name": "SetIndices", "ref": BASE, "target": BASE + 4, "confidence": "loose", "candidates": []},
            {"name": "Gone", "ref": BASE + 8, "target": None, "confidence": "none", "candidates": []},
        ])
        self.assertIn("| SetIndices | 0x82000000 | 0x82000004 | loose |", md)
        self.assertIn("| Gone | 0x82000008 | - | none |", md)


class MatchTests(unittest.TestCase):
    def test_relocated_copy_matches_loose_not_strict(self):
        a = [0x3D608200, 0x816B1234, 0x4E800020]
        b = [0x3D608300, 0x816B5678, 0x4E800020]
        self.assertFalse(sm.matches(a, b, "strict"))
        self.assertTrue(sm.matches(a, b, "loose"))

    def test_different_opcode_never_matches(self):
        self.assertFalse(sm.matches([0x7C0802A6], [0x7C0903A6], "loose"))

    def test_length_mismatch_never_matches(self):
        self.assertFalse(sm.matches([0x4E800020], [0x4E800020, 0x60000000], "loose"))

    def test_load_image_roundtrip(self):
        img, _, _ = make_image([0x4E800020])
        with tempfile.TemporaryDirectory() as tmp:
            prefix = str(Path(tmp) / "g")
            Path(prefix + ".img").write_bytes(img.data)
            Path(prefix + ".json").write_text(json.dumps({
                "base": BASE, "size": len(img.data), "entry": BASE, "patched": False,
                "pdata": {"address": BASE + 0x100, "size": 0}, "sections": img.sections}))
            loaded = sm.load_image(prefix)
            self.assertEqual(loaded.word(BASE + 0x1000), 0x4E800020)
            self.assertTrue(loaded.is_executable(BASE + 0x1000))
            self.assertFalse(loaded.is_executable(BASE + 0x100))


# Distinct primary opcodes: addi, lwz, stw, ori, rlwinm, and (31), lbz, stb
OPS = [0x38600001, 0x80610004, 0x90610008, 0x60630010, 0x54630000, 0x7C632378, 0x88610001, 0x98610002]


def body(n=20, shift=0):
    return [OPS[(i * 3 + shift + i // 5) % len(OPS)] for i in range(n - 1)] + [0x4E800020]


class FuzzyTests(unittest.TestCase):
    def test_swapped_instructions_and_register_accepted(self):
        ref = body()
        tgt = list(ref)
        i = next(k for k in range(3, 15) if (ref[k] >> 26) != (ref[k + 1] >> 26))
        tgt[i], tgt[i + 1] = tgt[i + 1], tgt[i]
        tgt[0] ^= 0x00200000  # different register, same opcode
        r, rf, t, tf = two_images(ref, [0x60000000] * 3 + tgt, [(BASE + 0x1000, 20)],
                                  [(BASE + 0x1000, 3), (BASE + 0x100C, 20)])
        res = sm.match_references(r, rf, t, tf, {"F": BASE + 0x1000})
        self.assertEqual(res[0]["confidence"], "fuzzy")
        self.assertEqual(res[0]["target"], BASE + 0x100C)
        self.assertGreaterEqual(res[0]["score"], sm.FUZZY_MIN_SCORE)

    def test_two_near_identical_candidates_not_accepted(self):
        ref = body()
        a = list(ref); a[5], a[6] = a[6], a[5]
        b = list(ref); b[8], b[9] = b[9], b[8]
        r, rf, t, tf = two_images(ref, a + b, [(BASE + 0x1000, 20)],
                                  [(BASE + 0x1000, 20), (BASE + 0x1050, 20)])
        res = sm.match_references(r, rf, t, tf, {"F": BASE + 0x1000})
        self.assertEqual(res[0]["confidence"], "none")
        self.assertIsNone(res[0]["target"])
        self.assertEqual(len(res[0]["candidates"]), 1)
        self.assertIn("score", res[0])

    def test_dissimilar_function_scores_low(self):
        ref = body()
        other = [OPS[(i * 5 + 1) % len(OPS)] if i % 2 else 0x48000000 for i in range(19)] + [0x4E800020]
        r, rf, t, tf = two_images(ref, other, [(BASE + 0x1000, 20)], [(BASE + 0x1000, 20)])
        res = sm.match_references(r, rf, t, tf, {"F": BASE + 0x1000})
        self.assertEqual(res[0]["confidence"], "none")
        self.assertLess(res[0]["score"], sm.FUZZY_MIN_SCORE)

    def test_fuzzy_match_seeds_callgraph(self):
        p = BASE + 0x1000
        child_r, child_t = BASE + 0x1100, BASE + 0x1200
        ref = body()
        ref[10] = bl(p + 40, child_r)
        tgt = list(ref)
        tgt[10] = bl(p + 40, child_t)
        k = next(k for k in range(12, 18) if (ref[k] >> 26) != (ref[k + 1] >> 26))
        tgt[k], tgt[k + 1] = tgt[k + 1], tgt[k]
        pad = [0x60000000] * (0x40 - 20)
        ref_all = ref + pad + [0x38600007, 0x4E800020] + [0x60000000] * (0x40 - 2)
        tgt_all = tgt + pad + [0x60000000] * 0x40 + [0x38600007, 0x4E800020]
        r, rf, t, tf = two_images(ref_all, tgt_all, [(p, 20), (child_r, 2)], [(p, 20), (child_t, 2)])
        res = sm.match_references(r, rf, t, tf, {"Parent": p})
        self.assertEqual(res[0]["confidence"], "fuzzy")
        res = sm.propagate(r, rf, t, tf, res)
        child = [x for x in res if x["name"] == "Parent.callee0"][0]
        self.assertEqual(child["target"], child_t)
        self.assertEqual(child["confidence"], "callgraph")


    def _two_callee_case(self, swap_calls):
        p = BASE + 0x1000
        ra, rb = BASE + 0x1100, BASE + 0x1200
        ta, tb = BASE + 0x1300, BASE + 0x1400
        blr = 0x4E800020
        parent = body()
        parent[6] = bl(p + 24, ra)
        parent[12] = bl(p + 48, rb)
        tparent = list(parent)
        tparent[6] = bl(p + 24, tb if swap_calls else ta)
        tparent[12] = bl(p + 48, ta if swap_calls else tb)
        k = next(k for k in range(13, 18) if (parent[k] >> 26) != (parent[k + 1] >> 26))
        tparent[k], tparent[k + 1] = tparent[k + 1], tparent[k]
        child_a = [0x80610004] * 11 + [blr]
        child_b = [0x90610008] * 11 + [blr]

        def build(par, a_at, b_at):
            code = [0x60000000] * 0x200
            for i, w in enumerate(par):
                code[i] = w
            for i, w in enumerate(child_a):
                code[(a_at - p) // 4 + i] = w
            for i, w in enumerate(child_b):
                code[(b_at - p) // 4 + i] = w
            return code
        r, rf, t, tf = two_images(build(parent, ra, rb), build(tparent, ta, tb),
                                  [(p, 20), (ra, 12), (rb, 12)], [(p, 20), (ta, 12), (tb, 12)])
        res = sm.match_references(r, rf, t, tf, {"Parent": p})
        self.assertEqual(res[0]["confidence"], "fuzzy")
        res = sm.propagate(r, rf, t, tf, res)
        return {x["ref"]: x for x in res}, ra, rb, ta, tb

    def test_fuzzy_parent_correct_call_order_accepted_via_fuzzy(self):
        by_ref, ra, rb, ta, tb = self._two_callee_case(False)
        self.assertEqual(by_ref[ra]["target"], ta)
        self.assertEqual(by_ref[rb]["target"], tb)
        self.assertEqual(by_ref[ra]["via"], "fuzzy")
        self.assertGreaterEqual(by_ref[ra]["score"], sm.FUZZY_MIN_SCORE)

    def test_fuzzy_parent_swapped_call_order_rejected(self):
        by_ref, ra, rb, ta, tb = self._two_callee_case(True)
        self.assertNotIn(ra, by_ref)
        self.assertNotIn(rb, by_ref)

    def test_exact_parent_callgraph_via_exact(self):
        p = BASE + 0x1000
        ref = [bl(p, BASE + 0x1010), 0x4E800020, 0x60000000, 0x60000000, 0x38600007, 0x4E800020]
        tgt = [bl(p, BASE + 0x1020), 0x4E800020] + [0x60000000] * 6 + [0x38600007, 0x4E800020]
        r, rf, t, tf = two_images(ref, tgt, [(p, 2), (BASE + 0x1010, 2)], [(p, 2), (BASE + 0x1020, 2)])
        res = sm.propagate(r, rf, t, tf, sm.match_references(r, rf, t, tf, {"Parent": p}))
        child = [x for x in res if x["name"] == "Parent.callee0"][0]
        self.assertIn(child["via"], ("strict", "loose"))


if __name__ == "__main__":
    unittest.main()
