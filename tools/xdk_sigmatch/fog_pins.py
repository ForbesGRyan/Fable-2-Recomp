"""The numbers the C++ mirrors of the fog are pinned to (tests/native/test_clay_logic.cpp, cases 580 on),
made by light_formulas.py: fog (the ground form), vertex_fog with fog_apply (the trees' form, evaluated at
the pixel: ruling B), fog_coords and table_1d. tests/test_fog_literals.py holds this file's inputs against
the C++ texts and this file's output against the lines of the C++ test.

Inputs, all tracked (nothing is read from a capture):
  - the two tables of frame 1 of capture native_discovery_20261009_092459: tests/native/fog_table_fixture.inc
    (the bytes the capture dumped), decoded by fog_tables.decode_tf4 / decode_tf5;
  - that frame's registers, written out below as the single-precision values the capture's rows hold
    (REAL: pixel c28, c66, c67, c68 and the eye c19; tests/native/test_fog_block.cpp has the same block);
  - a second, made-up register set (MADE) that exercises what the real one does not: an amount c28.x
    below 1, an in-scatter scale c28.y that is not 1, and a near slope c67.z small enough that pixels lie
    inside the blend from c68 (the real c67.z is 1: the blend is over one unit past the start).
Every number is rounded to single precision before use, as the renderer holds it; the arithmetic is the
reference's, in double.

Usage: python tools/xdk_sigmatch/fog_pins.py
"""
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fog_tables  # noqa: E402
import light_formulas as lf  # noqa: E402

FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tests', 'native', 'fog_table_fixture.inc')


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


# The tables' sizes, and the largest squared distance the product fogs (clay_logic.h, kFogDistance2Max):
# no pin lies near it.
SCATTER_TEXELS, TRANSMIT_TEXELS = 64, 256
DISTANCE2_MAX = 1e30

# Frame 1 of capture 092459 (frame-map section 16): the registers of its first fogged row.
REAL = {
    'c28': (1.0, 1.0),
    'c66': (-0.82958281040191650, 0.044566445052623749, 0.55660235881805420),
    'c67': (19.881736755371094, 1900.0, 1.0, 0.0005000000237487257),
    'c68': (0.26664465665817261, 0.39993393421173096, 0.46666669845581055),
    'eye': (82.07209777832031, 157.33926391601562, 50.17070388793945),
}
# Made up: the amount, the scale and a slope that leaves pixels inside the near blend; a nearer start and
# a table that ends sooner (667 units).
MADE = {
    'c28': (0.75, 1.5),
    'c66': REAL['c66'],
    'c67': (12.5, 1900.0, f32(0.02), f32(0.0015)),
    'c68': (f32(0.6), f32(0.2), f32(0.9)),
    'eye': REAL['eye'],
}
SETS = (('real', REAL), ('made', MADE))

# The trees' in-scatter scale, vertex c8.x: 1 in every captured row; 0.5 so that a mirror without it fails.
C8X = (1.0, 0.5)

# A pixel: the direction from the eye (any length; AXIS is the frame's c66, AWAY its opposite), the
# distance, the colour before fog.
AXIS, AWAY = 'axis', 'away'
POINTS = (
    ((0.0, 1.0, 0.0), 10.0, (0.35, 0.3, 0.12)),          # inside the start of both sets
    ((0.3, -1.0, 0.1), 12.9, (0.35, 0.3, 0.12)),         # inside the real start, 0.4 past the made one
    ((1.0, 0.25, -0.2), 20.3, (0.35, 0.3, 0.12)),        # 0.42 past the real start: inside its blend
    (AWAY, 60.0, (0.05, 0.06, 0.02)),                    # looking away from the axis: the table's first texel
    (AXIS, 254.0, (0.05, 0.06, 0.02)),                   # along the axis: its last texel
    ((-0.9, 0.5, 0.1), 400.0, (2.5, 0.01, 0.0)),         # between two texels of both tables; a colour above 1
    ((-0.5, 0.2, 0.9), 1004.0, (0.0, 0.0, 0.0)),         # black: the in-scatter alone
    ((-1.0, -0.3, 0.45), 3000.0, (0.2, 0.2, 0.2)),       # beyond the table's end: its last texel
    ((0.02, 1.0, 0.0), 150.0, (0.1, 0.4, 0.05)),         # the angle's coordinate inside the first half texel
    ((-0.6, 1.0, 0.25), 90.0, (0.9, 0.8, 0.7)),
)

# Coordinates the two tables are read at for the table pins: the ends, the first and last centres, a
# centre, between two centres, beyond both ends.
TABLE_U = (-1.0, 0.0, 0.5 / 64, 0.5 / 256, 1.5 / 256, 0.127, 0.3, 0.5, 1.0 - 0.5 / 64, 0.998, 1.0, 2.5)


def load_tables(fixture=FIXTURE):
    """The fixture's two byte arrays, decoded: (64 x (r, g, b), 256 x (r, g, b)), single precision."""
    with open(fixture, encoding='ascii') as f:
        text = f.read()
    out = []
    for name, count in (('kFixtureTf4', 512), ('kFixtureTf5', 1024)):
        m = re.search(r'static const unsigned char %s\[%d\] = \{(.*?)\};' % (name, count), text, re.S)
        if not m:
            raise ValueError('fog_pins: %s not found in %s' % (name, fixture))
        data = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1)))
        if len(data) != count:
            raise ValueError('fog_pins: %s holds %d bytes' % (name, len(data)))
        out.append(data)
    tf4 = [tuple(f32(v) for v in t[:3]) for t in fog_tables.decode_tf4(out[0])]
    tf5 = [tuple(f32(v) for v in t[:3]) for t in fog_tables.decode_tf5(out[1])]
    return tf4, tf5


def position(regs, direction, distance):
    """The pixel's world position as the single-precision numbers a shader gets."""
    if direction == AXIS:
        direction = regs['c66']
    elif direction == AWAY:
        direction = [-x for x in regs['c66']]
    n = lf.normalize(list(direction))
    return [f32(regs['eye'][i] + n[i] * distance) for i in range(3)]


def one(regs, direction, distance, colour, tf4, tf5):
    """A pin: the position, the colour, (fogged, a, s, t), the ground form, the trees' form per c8.x."""
    P = position(regs, direction, distance)
    C = [f32(c) for c in colour]
    c28, c66, c67, c68, eye = regs['c28'], regs['c66'], regs['c67'], regs['c68'], regs['eye']
    coords = lf.fog_coords(P, eye, c66, c67)
    ground = lf.fog(C, P, eye, c28, c66, c67, c68, tf4, tf5)
    trees = []
    for c8x in C8X:
        factor, add = lf.vertex_fog(P, eye, c8x, c66, c67, tf5, lambda a, t: tf4(a))
        trees.append(lf.fog_apply(C, factor, add))
    return P, C, coords, ground, trees


def lines():
    """The pins as the lines of the C++ test."""
    tf4_texels, tf5_texels = load_tables()
    tf4, tf5 = lf.table_1d(tf4_texels), lf.table_1d(tf5_texels)
    out = []

    def floats(values):
        return '{' + ', '.join('%rf' % v for v in values) + '}'

    def doubles(values):
        return '{' + ', '.join('%r' % v for v in values) + '}'

    out.append('// table pins: u, tf4(u), tf5(u)')
    for u in TABLE_U:
        out.append('    {%r, %s, %s},' % (u, doubles(tf4(u)), doubles(tf5(u))))
    for name, regs in SETS:
        out.append('// %s registers: c28 %s c66 %s c67 %s c68 %s eye %s' % (
            name, floats(regs['c28']), floats(regs['c66']), floats(regs['c67']), floats(regs['c68']), floats(regs['eye'])))
        out.append('// %s pins: P, C, fogged, (a, s, t), the ground form, the trees\' form with c8.x = %r and %r' % (
            (name,) + C8X))
        for direction, distance, colour in POINTS:
            P, C, coords, ground, trees = one(regs, direction, distance, colour, tf4, tf5)
            d2 = sum((regs['eye'][i] - P[i]) ** 2 for i in range(3))
            if not d2 < DISTANCE2_MAX * 1e-6:
                raise ValueError('fog_pins: a pin lies near the product\'s distance bound')
            fogged, a, s, t = (1, *coords) if coords else (0, 0.0, 0.0, 0.0)
            out.append('    {%s, %s, %d, %s, %s, %s, %s},' % (
                floats(P), floats(C), fogged, doubles((a, s, t)), doubles(ground), doubles(trees[0]), doubles(trees[1])))
    return out


def main(argv=None):
    for line in lines():
        print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
