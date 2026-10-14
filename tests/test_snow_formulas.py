import sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/xdk_sigmatch'))
import snow_check
class SnowProof(unittest.TestCase):
    @unittest.skipUnless((ROOT/'out/shader_dump/shader_19A01C01290E20A7.ucode.vert').exists(),'local shader evidence unavailable')
    def test_closed_formula_and_actual_native_hlsl(self):
        report=snow_check.check(ROOT/'out/shader_dump')
        self.assertEqual(report['own_hlsl_trials'],512)
        self.assertLess(report['worst_relative_error'],1e-7)
