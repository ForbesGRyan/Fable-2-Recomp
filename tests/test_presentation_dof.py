import math
import random
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/xdk_sigmatch'))
from presentation_post_formulas import composite_f51
from ucode_eval import Machine,load
from wind_hlsl import Region,Vec,water_env


class DofProof(unittest.TestCase):
    @unittest.skipUnless((ROOT/'out/shader_dump/shader_87E85199E1E9B260.ucode.frag').exists(),
                         'local shader evidence unavailable')
    def test_actual_hdr_downsample_and_both_blur_kernels(self):
        source=(ROOT/'src/native/fable2_native_shaders.h').read_text()
        down=Region(source,'// DOF DOWNSAMPLE BEGIN\n','// DOF DOWNSAMPLE END\n',
                    ('DofDownsample',),known=('DofDownTap',))
        blur=Region(source,'// DOF BLUR BEGIN\n','// DOF BLUR END\n',('DofBlur',),known=('DofTap',))
        shader_down=load(ROOT/'out/shader_dump','87E85199E1E9B260','frag')
        shaders=[load(ROOT/'out/shader_dump',h,'frag') for h in
                 ('EA83C7E7E24EC4C8','28DBE18F313350BF')]
        rng=random.Random(1413)
        for trial in range(512):
            at=[rng.randrange(280),rng.randrange(180)]
            # Continuous coordinate-dependent samples catch tap-coordinate swaps.
            def colour(x,y):return [((x*.017+y*.037+k*.413)%1)*31.875 for k in range(3)]+[1]
            calls=[]
            def down_sample(slot,coords,attrs):
                self.assertEqual(slot,0);self.assertEqual(list(coords[:2]),[4*at[0],4*at[1]])
                offset=[float(attrs['OffsetX']),float(attrs['OffsetY'])];calls.append(offset)
                return colour(coords[0]+offset[0],coords[1]+offset[1])
            expected=Machine(shader_down,{255:[.25,4,0,0]},down_sample,regs={0:at+[0,0]}).run()['oC0'][:3]
            native_calls=[]
            def native_down(p,offset):
                native_calls.append(list(offset.values))
                return Vec(*colour(p.x+offset.x,p.y+offset.y)[:3])
            actual=down.functions({'DofDownTap':native_down})['DofDownsample'](Vec(4*at[0],4*at[1])).values
            self.assertEqual(sorted(calls),sorted(native_calls))
            for a,b in zip(actual,expected):self.assertAlmostEqual(a,b,places=10)
            for axis,shader in enumerate(shaders):
                # Asymmetric made kernels expose mirrored/vertical weight errors.
                weights=[rng.random() for _ in range(7)];scale=weights[0]+2*sum(weights[1:])
                weights=[w/scale for w in weights];c46=weights[:4];c47=weights[4:]+[0]
                def lookup(x,y):return colour(min(max(x,0),279),min(max(y,0),179))
                def sample(slot,coords,attrs):
                    self.assertEqual(slot,0)
                    x,y=[math.floor(coords[i]+float(attrs['OffsetX' if i==0 else 'OffsetY'])) for i in range(2)]
                    return lookup(x,y)
                # Blur coordinates are normalized; independent interpreter forwards
                # them before applying fetch offsets, so convert to source texels.
                def normalized_sample(slot,coords,attrs):
                    return sample(slot,[coords[0]*280,coords[1]*180],attrs)
                expected=Machine(shader,{46:c46,47:c47},normalized_sample,
                                 regs={0:[at[0]/280,at[1]/180,0,0]}).run()['oC0'][:3]
                def native_tap(p,offset):
                    xy=[int(p.x),int(p.y)];xy[axis]+=offset;return Vec(*lookup(*xy)[:3])
                actual=blur.functions({'DofTap':native_tap})['DofBlur'](Vec(*at),Vec(*c46),Vec(*c47)).values
                for a,b in zip(actual,expected):self.assertAlmostEqual(a,b,places=10)

    @unittest.skipUnless((ROOT/'out/shader_dump/shader_F51AF7DB09EDED71.ucode.frag').exists(),
                         'local shader evidence unavailable')
    def test_actual_amount_tone_and_closed_reference(self):
        shader=load(ROOT/'out/shader_dump','F51AF7DB09EDED71','frag')
        source=(ROOT/'src/native/fable2_native_shaders.h').read_text()
        amount_region=Region(source,'// DOF AMOUNT BEGIN\n','// DOF AMOUNT END\n',('DofAmount',),
                             known=('min','max','saturate'))
        tone_region=Region(source,'// DOF TONE BEGIN\n','// DOF TONE END\n',('DofTone',),
                           known=('post_lum','DofTable','saturate','pow','abs'))
        rng=random.Random(1451)
        for trial in range(768):
            distance=[.1,1,2,4,6,8,50,100,2550,2550.5,2551,5000][trial%12]
            c={18:[1.00002,-.100002,0,0],31:[rng.uniform(0,8),0,0,0],
               46:[rng.uniform(.01,2),0,0,0],47:[rng.uniform(0,50),0,0,0],
               69:[rng.uniform(.1,1),0,0,0],77:[rng.uniform(-.2,1),rng.uniform(.1,8),0,0],
               78:[rng.uniform(.01,2),2,6,rng.uniform(.01,2)],79:[rng.random(),rng.random(),0,0],
               254:[1,2550,0,0],255:[.0721,.2125,.7154,0]}
            if trial%3==0:
                # The opening capture: infinite near slope with a zero cap.
                c[78]=[math.inf,0,50,.006666667];c[79]=[0,.358834,0,0]
            elif trial%3==1:
                # Exercise infinity/zero exactly at, inside and outside near focus.
                c[78][0]=math.inf;c[79][0]=0
            depth=1-c[18][0]-c[18][1]/distance
            sharp=[rng.uniform(0,31.875) for _ in range(3)]+[1]
            blurred=[rng.uniform(0,31.875) for _ in range(3)]+[1]
            bloom=[rng.random() for _ in range(4)];table=rng.uniform(0,4)
            uv=[rng.random(),rng.random(),0,0];calls=[]
            def sample(slot,coords,attrs=None):
                calls.append((slot,tuple(coords)))
                return {0:sharp,1:[depth]*4,2:[table]*4,3:bloom,4:blurred}[slot]
            expected=Machine(shader,c,sample,regs={0:uv},zero_product=True).run()['oC0'][:3]
            expected_calls=list(calls);calls.clear()
            closed=composite_f51(uv,c,sample)
            for a,b in zip(closed,expected):self.assertAlmostEqual(a,b,places=10)
            for slot,coords in calls:
                for a,b in zip(coords,next(at for s,at in expected_calls if s==slot)):
                    self.assertAlmostEqual(a,b,places=10)
            # Capture normalization substitutes zero only for +Inf slope / zero cap.
            focus=list(c[78]);focus[0]=0 if math.isinf(focus[0]) and c[79][0]==0 else focus[0]
            env=water_env();env.update(d_projection_caps=Vec(*c[18][:2],*c[79][:2]),d_focus=Vec(*focus))
            amount=amount_region.functions(env)['DofAmount'](depth)
            scene=Vec(*(sharp[i]+amount*(blurred[i]-sharp[i]) for i in range(3)))
            table_coord=next(at[0] for s,at in expected_calls if s==2)
            def native_table(u):
                self.assertAlmostEqual(u,table_coord,places=10);return table
            env.update(p_c31x=c[31][0],p_c46x=c[46][0],p_c47x=c[47][0],
                       p_c77x=c[77][0],p_c77y=c[77][1],p_c78x=c[69][0],DofTable=native_table,
                       post_lum=lambda v:sum(a*b for a,b in zip(v.values,[.2125,.7154,.0721])),
                       pow=lambda v,p:Vec(*(x**p for x in v.values)))
            actual=tone_region.functions(env)['DofTone'](scene,Vec(*bloom[:3])).values
            for a,b in zip(actual,expected):self.assertAlmostEqual(a,min(max(b,0),1),places=10)


if __name__=='__main__':unittest.main()
