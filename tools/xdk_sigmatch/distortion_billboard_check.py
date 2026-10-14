"""Prove the separate distortion producer; no runtime support is implied.

Optional fixtures contain the interpreter's outputs for the bounded native
vertex implementation. The consumer still needs its complete producer graph.
"""
import argparse
import json
import random
import struct
from pathlib import Path

import particle_formulas as formula
from ucode_eval import Machine, load

ROOT = Path(__file__).resolve().parents[2]
VS, PS = '00D85B797ECDDF7B', '8D2CB9BC0F8D6BDA'


def check(dumps, fixtures=None):
    rng = random.Random(140085)
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    vertex, pixel = load(dumps, VS, 'vert'), load(dumps, PS, 'frag')
    binary = bytearray(struct.pack('<I', 512))
    worst = 0.0

    def close(got, want):
        nonlocal worst
        for a, b in zip(got, want):
            error = abs(a-b)/max(1, abs(a), abs(b))
            worst = max(worst, error)
            assert error < 1e-7, (got, want)

    for trial in range(512):
        c = {row: [f32(rng.uniform(-1, 2)) for _ in range(4)] for row in range(256)}
        c[252] = [0, 0, 0, 0]
        c[253] = list(map(f32, [4, 6.28318548, 1, .159154937]))
        c[254] = list(map(f32, [.25, .5, -2, -3.14159274]))
        c[255] = [-.5, 0, 0, 0]
        c[46] = list(map(f32, [2, 0, .2, 0]))
        c[47] = list(map(f32, [25, .125, .7, 0]))
        position = [f32(rng.uniform(-20, 20)) for _ in range(4)]
        # Include the last legal curve address, integral age, and all corners.
        age = [0, 1, 98, 98.999, rng.uniform(0, 98.999)][trial % 5]
        shape = list(map(f32, [rng.uniform(.1, 3), rng.uniform(.1, 3), rng.uniform(-20, 20), age]))
        index = (rng.randrange(0, 16384)*4+trial % 4)
        values = {0: position, 4: shape, 8: [rng.random() for _ in range(4)]}

        def fetch(stream, offset, attrs, particle_index):
            assert int(particle_index) == index//4
            return values[offset]

        reference = Machine(vertex, c, lambda *args: None, vfetch=fetch,
                            regs={0: [index, 0, 0, 0]}).run()
        actual = formula.distortion_vertex(index, position, shape, c)
        for key in actual:
            close(actual[key], reference[key])
        binary += struct.pack('<I', index)
        binary += struct.pack('<1024f', *(value for row in range(256) for value in c[row]))
        binary += struct.pack('<20f', *(position+shape+reference['oPos']+reference['o0']+reference['o1']))
        # Nonunit alpha and arbitrary irrelevant exposure catch reuse of DBFD.
        colour = [rng.uniform(-1, 3) for _ in range(4)]
        texel = [rng.uniform(0, 2) for _ in range(4)]
        pc = {20: [0, 0, 0, rng.uniform(.1, 8)]}
        expected = Machine(pixel, pc, lambda *args: texel,
                           regs={0: [rng.random(), rng.random(), 0, 0], 1: colour}).run()['oC0']
        close(formula.distortion_pixel(colour, texel), expected)
    if fixtures:
        Path(fixtures).write_bytes(binary)
    return {'vertex_trials': 512, 'pixel_trials': 512, 'worst_relative_error': worst}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--dumps', type=Path, default=ROOT/'out/shader_dump')
    parser.add_argument('--fixtures', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = check(args.dumps, args.fixtures)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2)+'\n')
    print('PASS distortion billboard', json.dumps(result))
