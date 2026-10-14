import math,random,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/xdk_sigmatch'))
from ucode_eval import Machine,load
from wind_hlsl import Region,Vec,water_env,_each
class HistoryProof(unittest.TestCase):
    @unittest.skipUnless((ROOT/'out/shader_dump/shader_93E7F7B5D05626AA.ucode.frag').exists(),'local shader evidence unavailable')
    def test_actual_mapping_and_history_with_ten_bit_targets(self):
        source=(ROOT/'src/native/render/presentation_shaders.h').read_text()
        env=water_env();env['floor']=_each(math.floor)
        env['pow']=lambda x,y:Vec(*(a**y for a in x.values))
        fn=Region(source,'// MENU HISTORY FORMULA BEGIN\n','// MENU HISTORY FORMULA END\n',
                  ('MenuMap','QuantizeHistory','ResolveHistory','HistoryRgb'),known=('pow','abs','floor','saturate','lerp')).functions(env)
        mapping=load(ROOT/'out/shader_dump','93E7F7B5D05626AA','frag')
        copy=load(ROOT/'out/shader_dump','686DE15FC84D8B73','frag')
        def quant(x):return math.floor(min(max(x,0),1)*1023+.5)/1023
        rng=random.Random(14686)
        for n in range(512):
            sampled=[rng.uniform(-.5,2) for _ in range(4)]
            power=[1,2.2,rng.uniform(.1,8)][n%3]
            mapped=Machine(mapping,{31:[power,0,0,0]},lambda *a:sampled,regs={0:[0,0,0,0]}).run()['oC0']
            current=fn['MenuMap'](Vec(*sampled[:3]),power).values
            for a,b in zip(current,mapped[:3]):self.assertAlmostEqual(a,b,places=10)
            previous=[rng.randrange(1024)/1023 for _ in range(3)]
            # Captured tf13 is format 6: resolve R10 destination to RGBA8,
            # then the copy shader samples that previous frame, not current.
            resolved=[math.floor(x*255+.5)/255 for x in previous]+[1]
            copied=Machine(copy,{},lambda *a:resolved,regs={0:[0,0,0,0]}).run()['oC0']
            share=[0,1,.101960791,rng.random()][n%4]
            expected=[quant(quant(a)*(1-share)+b*share) for a,b in zip(mapped[:3],copied[:3])]
            self.assertEqual(fn['HistoryRgb'](Vec(*current),Vec(*previous),share).values,expected)
    def test_actual_scanout_index_at_table_boundaries(self):
        source=(ROOT/'src/native/render/presentation_shaders.h').read_text()
        env=water_env();env['floor']=_each(math.floor)
        fn=Region(source,'// SCANOUT INDEX FORMULA BEGIN\n','// SCANOUT INDEX FORMULA END\n',
                  ('RampIndex',),known=('min','floor','saturate')).functions(env)['RampIndex']
        # Primary SDK apply_gamma_table_cs.h: mad(*255,+.5), ftou, three loads.
        # A table with a jump at index one exposes interpolation as wrong.
        ramp=[0]+[5+i for i in range(255)]
        self.assertEqual(ramp[int(fn(.49/255))],0)
        self.assertEqual(ramp[int(fn(.51/255))],5)
        for n in range(255):
            boundary=(n+.5)/255
            for offset in (-1e-8,1e-8):
                value=boundary+offset
                expected=int(min(max(value,0),1)*255+.5)
                self.assertEqual(fn(value),expected)
        self.assertEqual(fn(-1),0);self.assertEqual(fn(2),255)
