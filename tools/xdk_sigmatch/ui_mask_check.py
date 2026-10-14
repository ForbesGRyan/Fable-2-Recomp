"""Independent mask shader proofs and bounded PM4 fixture extraction.

The complete fourth-mask gap crosses a non-PM4 reservation after a shader-done
event. Keep that as an expected refusal as well as the mask's explicit copy
prefix. Runtime must observe its copy before this command segment boundary.
"""
import argparse
import json
import random
import struct
from pathlib import Path
from ucode_eval import Machine, load
from wind_hlsl import Region, Vec
import post_tables

ROOT=Path(__file__).resolve().parents[2]

def prove(dumps, fixtures=None):
    vs=load(dumps,'81BADE63A169E3DC','vert')
    ps=load(dumps,'06547412E4B72251','frag')
    source=(ROOT/'src/native/render/presentation_shaders.h').read_text()
    pixel=Region(source,'// MASK COPY FORMULA BEGIN\n','// MASK COPY FORMULA END\n',('MaskCopy',)).functions({})['MaskCopy']
    rng=random.Random(140654)
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    data=bytearray(struct.pack('<I',512))
    worst=0
    for trial in range(512):
        position=[f32(rng.uniform(-4000,4000)) for _ in range(4)]
        uv=[f32(rng.uniform(-1,2)) for _ in range(2)]
        c={i:[f32(rng.uniform(-2,2)) for _ in range(4)] for i in range(4)}
        def fetch(stream,offset,attrs,index):
            assert stream==0 and offset in (0,3)
            return position if offset==0 else uv+[17,23]
        reference=Machine(vs,c,lambda *a:None,vfetch=fetch,regs={0:[trial%4,0,0,0]}).run()
        clip=[sum(row[k]*(position[k] if k<3 else 1) for k in range(4)) for row in c.values()]
        for a,b in zip(clip,reference['oPos']):
            worst=max(worst,abs(a-b));assert abs(a-b)<1e-7*max(1,abs(a),abs(b))
        assert reference['o0'][:2]==uv
        data+=struct.pack('<28f',*(sum(c.values(),[])+position+uv+reference['oPos']+reference['o0'][:2]))
        sample=[rng.uniform(-1,2) for _ in range(4)]
        at=[rng.random(),rng.random(),0,0];calls=[]
        def texture(slot,coords,attrs):
            calls.append((slot,coords));return sample
        expected=Machine(ps,{},texture,regs={0:at}).run()['oC0']
        assert pixel(Vec(*sample)).values==expected
        assert len(calls)==1 and calls[0][0]==13 and list(calls[0][1][:2])==at[:2]
    if fixtures: fixtures.write_bytes(data)
    return {'vertex_trials':512,'actual_hlsl_pixel_trials':512,'worst_vertex_absolute_error':worst}

def copy_fixtures(capture,destination):
    rows=[json.loads(line) for line in capture.open()]
    cases=[];identities=[]
    producer=False
    for row in rows:
        if row.get('ps_hash')=='0x06547412E4B72251': producer=True
        if not producer or row.get('kind')!='cmdgap' or not row.get('cmd'): continue
        blob=bytes.fromhex(row['cmd']);words=list(struct.unpack('>'+str(len(blob)//4)+'I',blob))
        events=post_tables.draw_events(words)
        event=next((e for e in events if e.get('copy',{}).get('pitch')==640 and
                    e['copy']['height']==360 and e['copy']['format']==6),None)
        if not event: continue
        producer=False
        at=event['at'];length=((words[at]>>16)&16383)+2
        prefix=blob[:4*(at+length)]
        base=event['copy']['base']
        # Full-mask copies occur before any opaque trailing reservation. These
        # original bytes exercise the parser, including register restoration.
        packets=list(post_tables.pm4_packets(words))
        opaque=any(p.get('opcode') in (0x21,0x3F,0x37,0x2E,0x45) for p in packets)
        opaque|=any(p['type'] in (0,3) and
                    p['at']+((words[p['at']]>>16)&16383)+2>len(words) for p in packets)
        opaque|=any(p.get('predicate') and p.get('opcode') in (0x2D,0x55,0x56) for p in packets)
        cases.append((blob,base,not opaque));cases.append((prefix,base,True))
        identities.append({'frame':row['frame'],'row':row['n'],'base':hex(base),
                           'whole_dwords':len(words),'prefix_dwords':len(prefix)//4,'whole_refused':opaque})
    assert identities and len(identities)<1000
    destination.write_bytes(struct.pack('<I',len(cases))+b''.join(
        struct.pack('<III',len(blob)//4,base,int(accepted))+blob for blob,base,accepted in cases))
    return identities

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--dumps',type=Path,default=ROOT/'out/shader_dump')
    parser.add_argument('--vertices',type=Path)
    parser.add_argument('--capture',type=Path)
    parser.add_argument('--copies',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    result=prove(args.dumps,args.vertices)
    if args.capture:
        assert args.copies
        result['copies']=copy_fixtures(args.capture,args.copies)
    if args.report: args.report.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS native mask formula/copy proofs',json.dumps(result))
