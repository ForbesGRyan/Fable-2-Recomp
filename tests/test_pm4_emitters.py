import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))
SPEC = importlib.util.spec_from_file_location("pm4_emitters", ROOT / "tools" / "xdk_sigmatch" / "pm4_emitters.py")
pe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pe)

LIS_R11_C003 = 0x3D60C003      # lis r11,0xC003
ORI_DRAW_INDX = 0x616B2200     # ori r11,r11,0x2200
ORI_BIN_MASK_LO = 0x616B6001   # ori r11,r11,0x6001 (predicated)
ORI_OTHER = 0x616B2D00         # ori r11,r11,0x2D00 (SET_CONSTANT, not listed)
NOP = 0x60000000


class HeaderSiteTests(unittest.TestCase):
    def test_finds_draw_header_built_by_lis_ori(self):
        sites = pe.header_sites([LIS_R11_C003, NOP, ORI_DRAW_INDX], 0x82000000)
        self.assertEqual(sites, [(0x82000008, "DRAW_INDX", 0xC0032200)])

    def test_predicate_bit_allowed(self):
        sites = pe.header_sites([LIS_R11_C003, ORI_BIN_MASK_LO], 0x82000000)
        self.assertEqual(sites, [(0x82000004, "SET_BIN_MASK_LO", 0xC0036001)])

    def test_ignores_unlisted_opcode_and_missing_lis(self):
        self.assertEqual(pe.header_sites([LIS_R11_C003, ORI_OTHER], 0x82000000), [])
        self.assertEqual(pe.header_sites([NOP, ORI_DRAW_INDX], 0x82000000), [])

    def test_lis_outside_lookback_ignored(self):
        words = [LIS_R11_C003] + [NOP] * (pe.LOOKBACK + 1) + [ORI_DRAW_INDX]
        self.assertEqual(pe.header_sites(words, 0x82000000), [])


if __name__ == "__main__":
    unittest.main()
