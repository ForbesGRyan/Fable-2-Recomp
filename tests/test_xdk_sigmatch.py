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


if __name__ == "__main__":
    unittest.main()
