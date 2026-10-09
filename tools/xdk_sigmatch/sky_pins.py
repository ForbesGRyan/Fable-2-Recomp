"""The numbers the C++ mirror of the sky is pinned to (tests/native/test_clay_logic.cpp, cases 588 on), made
by light_formulas.py: sky_latlong (the game's own polynomials for the lat-long coordinate), sky_paths,
sky_extinction and sky. tests/test_sky_literals.py holds this file's inputs against the C++ texts and this
file's output against the lines of the C++ test. fog_pins.py is the same thing for the fog.

Inputs, all tracked (nothing is read from a capture):
  - the in-scatter table of frame 1 of capture native_discovery_20261009_092459 (fog_pins.load_tables: the
    bytes of tests/native/fog_table_fixture.inc);
  - that frame's registers (REAL: pixel c64, c65, c66, c67.y of the fog block's fixture, and c31.x = 150,
    c46.x = 100, c47.x = 0 as every captured sky row has them);
  - a second, made-up set (MADE) with other path lengths, extinctions and a texture blend that is not 0,
    because the real frame's textures are all zero and its blend is 0;
  - two made-up texture samples (S13, S14): what the two sky textures return is the caller's (a sampler's)
    matter; the mirror takes the two samples and the reference is given two constant textures.
Every number is rounded to single precision before use, as the renderer holds it; the arithmetic is the
reference's, in double.

Usage: python tools/xdk_sigmatch/sky_pins.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fog_pins  # noqa: E402
import light_formulas as lf  # noqa: E402


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


# Frame 1 of capture 092459 (frame-map section 16): the fog block's registers the sky reads, and the sky
# block of every captured row.
REAL = {
    'c31x': 150.0,
    'c46x': 100.0,
    'c47x': 0.0,
    'c64': (0.00030544365290552378, 0.00039379042573273182, 0.0010710508795455098),
    'c65': (0.0052510276436805725, 0.0059622661210596561, 0.0098329493775963783),
    'c66': fog_pins.REAL['c66'],
    'c67y': fog_pins.REAL['c67'][1],
}
# Made up: other zenith paths, a lower path height, other extinctions, the blend a quarter of the way to
# the second texture.
MADE = {
    'c31x': 120.5,
    'c46x': 80.25,
    'c47x': 0.25,
    'c64': (f32(0.0008), f32(0.0011), f32(0.0019)),
    'c65': (f32(0.004), f32(0.007), f32(0.012)),
    'c66': fog_pins.REAL['c66'],
    'c67y': 1500.0,
}
SETS = (('real', REAL), ('made', MADE))

# What the two sky textures return in the pins (r, g, b, a), whatever the coordinate.
S13 = tuple(f32(v) for v in (0.6, 0.5, 0.9, 0.35))
S14 = tuple(f32(v) for v in (0.2, 0.8, 0.4, 0.75))

# View directions (any length). AXIS is the frame's c66, AWAY its opposite.
AXIS, AWAY = 'axis', 'away'
DIRECTIONS = (
    (0.0, 0.0, 1.0),            # the zenith: u = 0.5 by the console's zero product, v = 1.00003
    (0.0, 0.0, -2.5),           # the nadir
    (1e-6, 0.0, 1.0),           # beside the zenith: no rule needed
    (1.0, 0.0, 0.0),            # the horizon on +x: u = 0.5; both paths 1.05 * c67.y
    (0.0, 3.0, 0.0),            # the horizon on +y
    (0.0, -1.0, 0.0),           # ... and on -y
    (-1.0, 0.0, 0.0),           # the seam, on the half plane itself: u = 1.0000147
    (-1.0, 1e-6, 0.0),          # just north of it: u just below that
    (-1.0, -1e-6, 0.0),         # just south: u = -0.0000147
    (-2.0, 0.0, 1.0),           # the seam above the horizon
    (-1.0, -1e-3, -0.4),        # ... and south of it, below the horizon
    (1.0, 1.0, 0.0),            # the octant boundaries, where the arctangent changes branch
    (-1.0, 1.0, 0.2),
    (1.0, -1.0, -0.3),
    (-1.0, -1.0, 0.0),
    (0.3, -0.4, -0.8),          # below the horizon: the paths are the mirror image
    (0.6, 0.2, 0.05),           # just above the horizon
    (0.1, -0.2, 0.97),          # near the zenith: the paths near c31.x, c46.x
    (-0.5, 0.7, 0.35),
    AXIS,                       # along the fog's axis: the table's last texel
    AWAY,                       # away from it: its first
    (-0.9, 0.3, 0.45),          # between two texels of the table
)


def direction(regs, d):
    if d == AXIS:
        d = regs['c66']
    elif d == AWAY:
        d = [-x for x in regs['c66']]
    return [f32(x) for x in d]


def one(regs, d, tf4):
    """A pin: the direction, (u, v), the table's coordinate, the two paths, E, and the sky without a texture,
    with the first alone and with both."""
    n = direction(regs, d)
    block = dict(regs)
    u, v = lf.sky_latlong(n)
    a = lf.sat(lf.dot(lf.normalize(n), list(regs['c66'])))
    l1, l2 = lf.sky_paths(n, regs['c67y'], regs['c31x'], regs['c46x'])
    E = lf.sky_extinction(l1, l2, regs['c64'], regs['c65'])
    clear = lf.sky(n, block, tf4, None, None)
    first = lf.sky(n, block, tf4, lambda u, v: S13, None)
    both = lf.sky(n, block, tf4, lambda u, v: S13, lambda u, v: S14)
    return n, (u, v), a, (l1, l2), E, clear, first, both


def lines():
    """The pins as the lines of the C++ test."""
    tf4_texels, _ = fog_pins.load_tables()
    tf4 = lf.table_1d(tf4_texels)
    out = []

    def floats(values):
        return '{' + ', '.join('%rf' % v for v in values) + '}'

    def doubles(values):
        return '{' + ', '.join('%r' % v for v in values) + '}'

    out.append('// sky texture samples: tf13 %s tf14 %s' % (floats(S13), floats(S14)))
    for name, regs in SETS:
        out.append('// %s sky registers: c31.x %rf c46.x %rf c47.x %rf c64 %s c65 %s c66 %s c67.y %rf' % (
            name, regs['c31x'], regs['c46x'], regs['c47x'], floats(regs['c64']), floats(regs['c65']), floats(regs['c66']),
            regs['c67y']))
        out.append('// %s sky pins: n, (u, v), a, (l1, l2), E, the clear sky, with tf13 alone, with both textures' % name)
        for d in DIRECTIONS:
            n, uv, a, paths, E, clear, first, both = one(regs, d, tf4)
            out.append('    {%s, %s, %r, %s, %s, %s, %s, %s},' % (
                floats(n), doubles(uv), a, doubles(paths), doubles(E), doubles(clear), doubles(first), doubles(both)))
    return out


def main(argv=None):
    for line in lines():
        print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
