"""Check the closed formula and the actual own HLSL against independent microcode."""
import argparse,hashlib,json,math,random
from pathlib import Path
import snow_formulas as f
from ucode_eval import Machine,load
from wind_hlsl import Region,Vec,water_env,_each

ROOT=Path(__file__).resolve().parents[2]
def snow_region():
    return Region((ROOT/'src/native/render/particle_shaders.h').read_text(),
                  '// SNOW FORMULA BEGIN\n','// SNOW FORMULA END\n',
                  ('SnowWorld','SnowClip','SnowDiameter','SnowFade','SnowUv','SnowColour'),
                  known=('SnowC','dot','sqrt','frac','floor','cross','saturate','sin','cos','abs'))
def snow_hlsl(region,c):
    env=water_env();env.update({'SnowC':lambda n:Vec(*c[int(n)]),
        'frac':_each(f.frac),'floor':_each(math.floor),'sin':math.sin,'cos':math.cos,
        'cross':lambda a,b:Vec(*f.cross(a.values,b.values))})
    return region.functions(env)
def check(dumps):
    region=snow_region()
    rng=random.Random(1419);worst=0;forces=0
    def close(a,b):
        nonlocal worst
        for x,y in zip(a,b):
            err=abs(x-y)/max(1,abs(x),abs(y));worst=max(worst,err)
            assert err<1e-7,(a,b,err)
    vs=load(dumps,f.VS,'vert');ps=load(dumps,f.PS,'frag')
    for trial in range(512):
        c={i:[0.]*4 for i in range(256)}
        for i,row in enumerate(f.LITERALS):c[244+i]=row[:]
        for i in range(4):c[i]=[rng.uniform(-1,1) for _ in range(4)]
        c[3]=[0,0,0,1];c[46]=[16,16,16,rng.uniform(.1,2)]
        c[47]=[rng.uniform(0,10),0,0,0];c[8]=[rng.uniform(-10,10) for _ in range(4)]
        c[75]=[0,100,10,rng.uniform(.1,2)];c[144]=[rng.uniform(-1,1) for _ in range(4)]
        for i in range(113,128):c[i]=[0,0,0,1]
        # Complete plane sets exercise both rejection and preservation.
        if trial%8<3:
            first=(113,118,123)[trial%8]
            for i in range(first,first+5):c[i]=[0,0,0,-1]
        c[73]=[rng.uniform(.01,.2) for _ in range(4)];c[74]=[rng.uniform(.01,.2) for _ in range(4)]
        for i in range(128,144):c[i]=[rng.uniform(-10,20) for _ in range(3)]+[rng.uniform(-2,8)]
        if trial%16==0:c[129]=c[128][:] # Zero length capsule, including zero-product semantics.
        index=rng.randrange(4096)
        reference=Machine(vs,c,lambda *a:None,regs={0:[index,0,0,0]},zero_product=True).run()
        for key,value in f.vertex(index,c).items():close(value,reference[key])
        hlsl=snow_hlsl(region,c)
        world=hlsl['SnowWorld'](index);clip=hlsl['SnowClip'](world)
        close(clip.values,reference['oPos'])
        close([hlsl['SnowDiameter'](index,clip.w)],reference['oPts'][:1])
        close([hlsl['SnowFade'](index,world)],reference['o0'][:1])
        atlas=hlsl['SnowAtlas'](index)
        origin=hlsl['SnowAtlasOrigin'](atlas);basis_x=hlsl['SnowAtlasX'](atlas);basis_y=hlsl['SnowAtlasY'](atlas)
        close(origin.values,reference['o1'][:2]);close(basis_x.values,reference['o2'][:2]);close(basis_y.values,reference['o3'][:2])
        point=[rng.uniform(-1,1),rng.uniform(-1,1)];texel=[rng.random() for _ in range(4)]
        exposure=rng.uniform(.1,8);tint=[rng.random() for _ in range(3)];fade=rng.random()
        pc={20:[0,0,0,exposure],31:tint+[0],255:[-1,2,0,0]}
        m=Machine(ps,pc,lambda *a:texel,regs={0:[fade,0,0,0],1:origin.values+[0,0],
                  2:basis_x.values+[0,0],3:basis_y.values+[0,0],4:[0,0]+point})
        colour=m.run()['oC0'];uv=m.fetches[0][2]
        own_uv,own_colour=f.pixel(origin.values,basis_x.values,basis_y.values,point,texel,fade,exposure,tint)
        close(own_uv,uv);close(own_colour,colour)
        close(hlsl['SnowUv'](Vec(*point),origin,basis_x,basis_y).values,uv)
        close(hlsl['SnowColour'](Vec(*texel),fade,exposure,Vec(*tint)).values,colour)
    return {'vertex_trials':512,'pixel_trials':512,'own_hlsl_trials':512,'worst_relative_error':worst}

def check_capture(dumps,path):
    """Own HLSL and closed reference versus captured VS bank/literals, in double.

    Does not validate the GPU compiler's float arithmetic or visual rasterization.
    The captured bank precedes no-position discovery's early return (build 33+).
    """
    data=path.read_bytes();region=snow_region();vs=load(dumps,f.VS,'vert')
    rows=trials=0;worst=0
    def close(a,b):
        nonlocal worst
        for x,y in zip(a,b):
            assert math.isfinite(x) and math.isfinite(y),(x,y)
            error=abs(x-y)/max(1,abs(x),abs(y));worst=max(worst,error)
            assert error<1e-7,(rows,trials,a,b,error)
    for line in data.decode('utf-8-sig').splitlines():
        r=json.loads(line)
        if r.get('kind')!='draw' or r.get('vs_hash')!='0x'+f.VS or r.get('ps_hash')!='0x'+f.PS:continue
        assert len(r.get('bank',[]))==1024,'capture has no full snow bank; rerun build 33+'
        c={i:r['bank'][i*4:i*4+4] for i in range(256)}
        for literal in r['vs_lit']:
            values=literal['v'];assert len(values)%4==0
            for at in range(0,len(values),4):c[literal['reg']+at//4]=values[at:at+4]
        assert [c[i] for i in range(244,256)]==f.LITERALS
        first=int(r['args'][1],16);count=int(r['args'][2],16)
        assert count>0 and count<=4096 and first+count<=65536
        rows+=1;hlsl=snow_hlsl(region,c)
        for relative in sorted({0,1,count//4,count//2,3*count//4,count-2,count-1}):
            if relative<0 or relative>=count:continue
            index=first+relative;trials+=1
            reference=Machine(vs,c,lambda *a:None,regs={0:[index,0,0,0]},zero_product=True).run()
            for key,value in f.vertex(index,c).items():close(value,reference[key])
            world=hlsl['SnowWorld'](index);clip=hlsl['SnowClip'](world)
            close(clip.values,reference['oPos'])
            close([hlsl['SnowDiameter'](index,clip.w)],reference['oPts'][:1])
            close([hlsl['SnowFade'](index,world)],reference['o0'][:1])
            atlas=hlsl['SnowAtlas'](index)
            close(hlsl['SnowAtlasOrigin'](atlas).values,reference['o1'][:2])
            close(hlsl['SnowAtlasX'](atlas).values,reference['o2'][:2])
            close(hlsl['SnowAtlasY'](atlas).values,reference['o3'][:2])
    assert rows,'capture contains no exact snow pair'
    return {'capture':str(path),'sha256':hashlib.sha256(data).hexdigest(),
            'draw_rows':rows,'captured_vertex_trials':trials,'worst_relative_error':worst,
            'precision':'double; excludes compiler/GPU float and rasterization'}
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--dumps',type=Path,default=ROOT/'out/shader_dump');p.add_argument('--report',type=Path)
    p.add_argument('--capture',type=Path)
    args=p.parse_args();report=check(args.dumps)
    if args.capture:report['captured_inputs']=check_capture(args.dumps,args.capture)
    if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS snow',json.dumps(report))
