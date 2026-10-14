"""Prove the named particle formulas and optionally emit native-test fixtures."""
import argparse
import json
import random
import struct
from pathlib import Path
import particle_formulas as f
from ucode_eval import Machine, load

ROOT=Path(__file__).resolve().parents[2]
PIXELS={'DBFD88A80EDBCE36':1,'C300519EC8915346':2,'A753A2072F78F00F':3}

def check(dumps,fixtures=None):
    rng=random.Random(140029)
    fp32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    worst=0.0
    binary=bytearray(struct.pack('<I',512))
    def close(actual,expected):
        nonlocal worst
        for got,want in zip(actual,expected):
            error=abs(got-want)/max(1,abs(got),abs(want))
            worst=max(worst,error)
            assert error<1e-7,(actual,expected)
    vertex_source=load(dumps,f.VS,'vert')
    soft_vertex_source=load(dumps,'563E3BE17857DB59','vert')
    soft_pixel_source=load(dumps,'FF19CE696F916E0C','frag')
    pixel_sources={key:load(dumps,key,'frag') for key in PIXELS}
    for trial in range(512):
        c={i:[fp32(rng.uniform(.1,1)) for _ in range(4)] for i in range(256)}
        c[252]=[0,0,0,0]
        c[253]=list(map(fp32,[-.5,.1,0,0]))
        c[254]=list(map(fp32,[4,6.28318548,1,.159154937]))
        c[255]=list(map(fp32,[.25,.5,-2,-3.14159274]))
        c[47]=[0,0,fp32(.1),0]
        c[113]=[1000,fp32(.01),fp32(.8),0]
        for row in (0,1,2,3,72,73,74,114,115):
            c[row]=[fp32(rng.uniform(-1,1)) for _ in range(4)]
        position=[fp32(rng.uniform(-20,20)) for _ in range(4)]
        shape=list(map(fp32,[rng.uniform(.1,3),rng.uniform(.1,3),rng.uniform(-20,20),rng.uniform(0,18)]))
        index=rng.randrange(0,4096)
        values={0:position,4:shape,8:[rng.random() for _ in range(4)]}
        def fetch(stream,offset,attrs,particle_index):
            assert int(particle_index)==index//4,(particle_index,index)
            return values[offset]
        reference=Machine(vertex_source,c,lambda *args:None,vfetch=fetch,regs={0:[index,0,0,0]}).run()
        actual=f.vertex(index,position,shape,c)
        for key in actual:close(actual[key],reference[key])
        binary+=struct.pack('<I',index)
        binary+=struct.pack('<1024f',*(value for row in range(256) for value in c[row]))
        binary+=struct.pack('<20f',*(position+shape+reference['oPos']+reference['o0']+reference['o1']))
        soft_reference=Machine(soft_vertex_source,c,lambda *args:None,vfetch=fetch,regs={0:[index,0,0,0]}).run()
        for key,values in f.vertex(index,position,shape,c,soft=True).items():close(values,soft_reference[key])
        texels=[[rng.random() for _ in range(4)] for _ in range(3)]
        uv=[rng.uniform(-1,2) for _ in range(4)]
        colour=[rng.uniform(0,3) for _ in range(4)]
        exposure=rng.uniform(.1,8)
        thresholds=[rng.random(),rng.random()]
        rates=[rng.uniform(.1,10),rng.uniform(.1,10)]
        pc={20:[0,0,0,exposure],31:thresholds+[0,0],46:rates+[0,0]}
        for key,count in PIXELS.items():
            reference=Machine(pixel_sources[key],pc,lambda slot,coords,attrs:texels[slot],regs={0:uv,1:colour}).run()['oC0']
            close(f.pixel(uv,colour,texels[:count],exposure,thresholds,rates),reference)
        sc={0:[0,0,1/1120,1/720],20:pc[20],31:[rng.uniform(.1,10),0,0,0],
            254:[2,.5,0,0],255:[0,1,.01,-1]}
        for row in range(116,120):sc[row]=[rng.uniform(-1,1) for _ in range(4)]
        window=[rng.uniform(0,1120),rng.uniform(0,720)]
        depth=rng.random()
        screen=[2*window[0]/1120-1,1-2*window[1]/720,1-depth,1]
        reconstructed=[f.dot(sc[row],screen) for row in range(116,120)]
        world=[x/reconstructed[3] for x in reconstructed[:3]]
        target=[0,.25,.5,.75,1,1.5][trial%6]
        world[0]+=(target/sc[31][0])**.5
        sample=lambda slot,coords,attrs:texels[0] if slot==0 else [depth]*4
        reference=Machine(soft_pixel_source,sc,sample,regs={0:uv,1:world+[0],2:colour,3:window+[0,0]}).run()['oC0']
        close(f.soft_pixel(uv,world,colour,window,sc,texels[0],depth),reference)
    if fixtures:Path(fixtures).write_bytes(binary)
    return {'vertex_trials':512,'soft_vertex_trials':512,'soft_pixel_trials':512,
            'pixel_trials':{key:512 for key in PIXELS},'worst_relative_error':worst}

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--dumps',type=Path,default=ROOT/'out/shader_dump')
    parser.add_argument('--fixtures',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    report=check(args.dumps,args.fixtures)
    if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS particles',json.dumps(report))
