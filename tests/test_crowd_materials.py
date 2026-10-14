import math,random,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/xdk_sigmatch'))
import light_check as lc
from ucode_eval import Machine

class CrowdMaterials(unittest.TestCase):
    @unittest.skipUnless((ROOT/'out/shader_dump/shader_F2F103F16764043B.ucode.frag').exists(),
                         'local shader evidence unavailable')
    def test_hair_and_iris_uv_alpha_and_light(self):
        for ps,vs in [('F2F103F16764043B','D4D558DA6A82BDC8'),
                      ('A22982E86122DA41','79EAC49585797037')]:
            text,make=lc._object_driver(ps,vs,ROOT/'out/shader_dump',multi=True)
            rng=random.Random(14829)
            for _ in range(256):
                case=make(rng)
                m=Machine(text,case.consts,case.sampler,regs=case.regs,
                          bools=case.bools,tex_size=case.tex_size)
                out=m.run()['oC0']
                uv=next(uv for _,slot,uv in m.fetches if slot==0)
                want=[case.regs[0][n]*case.consts[8][n]+case.consts[8][n+2] for n in range(2)]
                for a,b in zip(uv,want):self.assertAlmostEqual(a,b,places=12)
                alpha=case.sampler(0,uv,{})[3]*case.consts[26][3]
                self.assertAlmostEqual(out[3],alpha,places=12)
                expected=lc.lf.lit_object(**case.args)
                for n in range(3):
                    self.assertTrue(math.isfinite(out[n]))
                    self.assertAlmostEqual(out[n]/case.scale[n],expected[n],places=10)
