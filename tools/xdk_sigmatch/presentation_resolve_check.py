"""Independent numerical fixtures for the menu's unsigned float10 resolve."""
import argparse
import bisect
import math
import struct
from pathlib import Path

VALUES=[i/512 for i in range(128)]+[
    math.ldexp(1+m/128,e-3) for e in range(1,8) for m in range(128)]

def resolved(value):
    value=min(max(value,0),31.875) if not math.isnan(value) else 0
    index=bisect.bisect_left(VALUES,value)
    if index==0:return 0
    if index==len(VALUES):return VALUES[-1]
    below,above=VALUES[index-1],VALUES[index]
    if value-below<above-value:return below
    if value-below>above-value:return above
    return VALUES[index if index%2==0 else index-1]

def fixtures():
    # Every possible stored half-float, including subnormals and negative
    # values. Choose nearest representable 7e3 by distance, not SDK bit packing.
    inputs=[struct.unpack('<e',struct.pack('<H',bits))[0] for bits in range(65536)]
    for a,b in zip(VALUES,VALUES[1:]):
        midpoint=(a+b)/2
        bits=struct.unpack('<H',struct.pack('<e',midpoint))[0]
        inputs.extend(struct.unpack('<e',struct.pack('<H',bits+delta))[0] for delta in (-1,0,1))
    return [(value,resolved(value)) for value in inputs]

def dof_fixtures():
    inputs=[value for value,_ in fixtures()]
    # Before 7e3 packing the HDR RTV stores half precision. Probe both F32
    # neighbours and exact ties around every positive finite-half midpoint.
    halves=[struct.unpack('<e',struct.pack('<H',bits))[0] for bits in range(0x7C00)]
    for a,b in zip(halves,halves[1:]):
        bits=struct.unpack('<I',struct.pack('<f',(a+b)/2))[0]
        inputs.extend(struct.unpack('<f',struct.pack('<I',bits+delta))[0] for delta in (-1,0,1))
    inputs.extend([1e38,-1e38])
    def stored(value):
        try:return struct.unpack('<e',struct.pack('<e',value))[0]
        except OverflowError:return math.copysign(math.inf,value)
    return [(value,resolved(stored(value))) for value in inputs]

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--fixtures',required=True,type=Path)
    parser.add_argument('--dof',action='store_true')
    args=parser.parse_args()
    cases=dof_fixtures() if args.dof else fixtures()
    args.fixtures.write_bytes(struct.pack('<I',len(cases))+b''.join(struct.pack('<2f',*case) for case in cases))
    print(f'{len(cases)} independent resolve cases: all half-floats and midpoint neighbours'+
          ('; DOF also probes F32-to-half rounding first' if args.dof else ''))
