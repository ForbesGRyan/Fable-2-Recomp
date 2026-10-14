import math,random,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/xdk_sigmatch'))
from ucode_eval import Machine,load
from wind_hlsl import Region,Vec,water_env

class EffectProof(unittest.TestCase):
    @unittest.skipUnless((ROOT/'out/shader_dump/shader_5D6768847B0602B8.ucode.frag').exists(),'local shader evidence unavailable')
    def test_depth_producer_uv_and_reversed_depth(self):
        vertex=load(ROOT/'out/shader_dump','D5E12A97B3A84881','vert')
        pixel=load(ROOT/'out/shader_dump','5D6768847B0602B8','frag')
        corners=[[-1,-1,.5,0,1],[-1,1,.5,0,0],[1,-1,.5,1,1],[1,1,.5,1,0]]
        for x,y,z,u,v in corners:
            output=Machine(vertex,{},lambda *a:None,
                vfetch=lambda slot,at,attrs,index:[x,y,z,1] if at==0 else [u,v,0,0]).run()
            self.assertEqual(output['oPos'],[x,y,z,1])
            self.assertEqual(output['o0'][:2],[u,v])
        source=(ROOT/'src/native/render/particle_shaders.h').read_text()
        self.assertIn('float half_depth(Out i):SV_Depth {\n  return scene_depth.SampleLevel(point_clamp,i.uv.xy,0);',source)
        rng=random.Random(14567)
        for half_pixel in [False,True]:
            offset=.5 if half_pixel else 0
            for _ in range(256):
                x,y=rng.randrange(560),rng.randrange(360)
                # The own quad is shifted with the captured viewport, just as
                # the billboards are. Interpolation retains its original UV.
                uv=[(x+.5-offset)/560,(y+.5-offset)/360,0,0]
                self.assertEqual(math.floor(uv[0]*1120+1e-9),2*x+(not half_pixel))
                self.assertEqual(math.floor(uv[1]*720+1e-9),2*y+(not half_pixel))
                native_depth=[0,1,rng.random()][x%3]
                sampled=[]
                def sample(slot,coords,attrs):
                    self.assertEqual(slot,13);sampled.append(list(coords[:2]))
                    return [1-native_depth]*4
                expected=Machine(pixel,{},sample,regs={0:uv}).run()['oDepth'][0]
                self.assertEqual(sampled,[uv[:2]])
                self.assertAlmostEqual(1-expected,native_depth,places=14)

    @unittest.skipUnless((ROOT/'out/shader_dump/shader_B863FDD3469557D4.ucode.frag').exists(),'local shader evidence unavailable')
    def test_effect_tone_actual_hlsl_against_microcode(self):
        shader=load(ROOT/'out/shader_dump','B863FDD3469557D4','frag')
        source=(ROOT/'src/native/fable2_native_shaders.h').read_text()
        rng=random.Random(14863)
        for n in range(512):
            scene=[rng.uniform(0,31.875) for _ in range(3)]+[1]
            effect=[rng.random() for _ in range(4)]
            bloom=[rng.random() for _ in range(4)]
            gain=[0,.5,16,rng.uniform(0,64)][n%4]
            c={31:[rng.uniform(0,8),0,0,0],46:[rng.uniform(.01,4),0,0,0],47:[gain,0,0,0],
               77:[rng.uniform(0,50),0,0,0],78:[rng.uniform(-.2,1),rng.uniform(.1,8),0,0],
               79:[rng.uniform(.1,1),0,0,0],255:[.0721,.2125,.7154,0]}
            table=rng.uniform(0,4)
            table_coords=[]
            def sample(slot,coords,attrs):
                if slot==1:table_coords.append(coords[0])
                return {0:scene,1:[table]*4,2:bloom,3:effect}[slot]
            expected=Machine(shader,c,sample,regs={0:[.2,.4,0,0]}).run()['oC0'][:3]
            env=water_env()
            env.update(p_pad=1,p_c31x=c[31][0],p_c46x=c[46][0],p_c47x=c[77][0],
                       p_c77x=c[78][0],p_c77y=c[78][1],p_c78x=c[79][0])
            env['post_scene']=lambda v:Vec(*(min(max(x,0),31.875) for x in v.values))
            env['post_lum']=lambda v:sum(x*y for x,y in zip(v.values,[.2125,.7154,.0721]))
            def native_table(u):
                self.assertAlmostEqual(u,table_coords[0],places=10)
                return table
            env['post_table']=native_table
            env['post_pow']=lambda v,p:Vec(*(abs(x)**p for x in v.values))
            fn=Region(source,'// PRESENTATION EFFECT TONE BEGIN\n','// PRESENTATION EFFECT TONE END\n',
                      ('PostComposite',),known=('post_scene','post_lum','post_table','post_pow','saturate')).functions(env)
            actual=fn['PostComposite'](Vec(*(scene[i]+effect[i]*gain for i in range(3))),Vec(*bloom[:3])).values
            for a,b in zip(actual,expected):self.assertAlmostEqual(a,min(max(b,0),1),places=10)

if __name__=='__main__':unittest.main()
