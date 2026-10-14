import math,random,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/xdk_sigmatch'))
from ucode_eval import Machine,load
from wind_hlsl import Region,Vec
class FadeProof(unittest.TestCase):
    @unittest.skipUnless((ROOT/'out/shader_dump/shader_95503A81AE52EA05.ucode.frag').exists(),'local shader evidence unavailable')
    def test_actual_black_fade_hlsl_and_geometry_against_microcode(self):
        source=(ROOT/'src/native/render/presentation_shaders.h').read_text()
        fn=Region(source,'// BLACK FADE FORMULA BEGIN\n','// BLACK FADE FORMULA END\n',('BlackFade',),known=()).functions({})['BlackFade']
        ps=load(ROOT/'out/shader_dump','95503A81AE52EA05','frag')
        vs=load(ROOT/'out/shader_dump','BD2477FEDE6BEAB1','vert')
        rng=random.Random(14955)
        for n in range(512):
            alpha=[0,1,rng.random()][n%3];opacity=[0,1,rng.random()][(n//3)%3]
            texel=[rng.uniform(0,4) for _ in range(3)]+[alpha]
            c={20:[0,0,0,rng.uniform(0,8)],31:[0,0,0,opacity]}
            ref=Machine(ps,c,lambda *a:texel,regs={0:[0,0,0,0]},zero_product=True).run()['oC0']
            self.assertEqual(fn(alpha,opacity).values,ref)
            position=[rng.uniform(-2000,2000) for _ in range(3)]+[1]
            uv=[rng.random(),rng.random(),0,0];offset=[rng.uniform(-1,1) for _ in range(4)]
            ref=Machine(vs,{8:offset},lambda *a:None,vfetch=lambda slot,at,attrs,index:position if at==0 else uv).run()
            self.assertEqual(ref['oPos'],[position[0]*offset[0]+offset[2],position[1]*offset[1]+offset[3],position[2],1])
            self.assertEqual(ref['o0'],uv)
