"""The numbers the C++ mirrors of the post chain are pinned to (plan Tasks 5, 8 and 14: PostTableTexel,
PostUnlitScale, PostRampMirror, PostCompositeMirror, PostDownsampleMirror, PostBrightMirror, PostBlurMirror and
the radial blur's and the reduction's), made by light_formulas.py: post_table_texel, post_unlit_scale,
post_ramp_index, post_composite, post_downsample, post_bright, post_blur13, post_radial, post_reduce.

Inputs, all tracked (nothing is read from a capture):
  - the composite's table and the post registers of frame 1 of capture native_discovery_20261009_092959
    (a read-back run at the bridge): tests/post_replay_excerpt.json, written by post_replay.py excerpt
    (the table's 2,048 bytes as dumped, decoded here as 512 big-endian floats; `--table` prints them as
    a C array, for a test that needs the same table). The set REAL;
  - a second, made-up register set (MADE) that exercises what the real one does not: a grey fade that
    reaches up to scene luminance 1 / 3, a saturation step of 25%, another power, a table coordinate
    that is not a quarter of the luminance, a bright pass with a low threshold;
  - small pictures given by integer formulas (scene_texel, unorm_texel below), so that a test can make the
    same texels without a file. Every number is rounded to single precision before use, as the renderer
    holds it; the arithmetic is the reference's, in double.
Values that are no numbers are among the inputs (review focus 2); the lines write them as kNaN and kInf.

Usage: python tools/xdk_sigmatch/post_pins.py [--table]
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import light_formulas as lf  # noqa: E402

EXCERPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tests', 'post_replay_excerpt.json')
NAN, INF = float('nan'), float('inf')


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def load_excerpt(path=EXCERPT):
    with open(path, encoding='ascii') as f:
        return json.load(f)


def load_table(path=EXCERPT):
    """The excerpt's table: 512 floats (single precision, exactly as the capture's bytes hold them)."""
    data = bytes.fromhex(load_excerpt(path)['table'])
    if len(data) != 4 * lf.POST_TABLE_TEXELS:
        raise ValueError('post_pins: the table holds %d bytes' % len(data))
    return list(struct.unpack('>%df' % lf.POST_TABLE_TEXELS, data))


def _real(path=EXCERPT):
    regs = load_excerpt(path)['regs']
    c, b = regs['composite'], regs['bright']
    return {
        'c31x': c['c31'][0], 'c46x': c['c46'][0], 'c47x': c['c47'][0], 'c77': (c['c77'][0], c['c77'][1]), 'c78x': c['c78'][0],
        'bright': {'c31x': b['c31'][0], 'c46x': b['c46'][0], 'c47x': b['c47'][0], 'c77': (b['c77'][0], b['c77'][1])},
        'blur': (regs['blur-x']['c46'] + regs['blur-x']['c47'][:3], regs['blur-y']['c46'] + regs['blur-y']['c47'][:3]),
        'radial': {'c31': (regs['radial']['c31'][0], regs['radial']['c31'][1]), 'c46x': regs['radial']['c46'][0],
                   'c47x': regs['radial']['c47'][0]},
    }


REAL = _real()
MADE = {
    'c31x': 1.5, 'c46x': 0.5, 'c47x': 3.0, 'c77': (0.25, f32(0.9)), 'c78x': f32(0.6),
    'bright': {'c31x': 2.0, 'c46x': 0.5, 'c47x': 1.5, 'c77': (f32(0.02), f32(0.2))},
    # Weights at their bounds: the centre alone; and seven equal ones (the sum is 1: 1 / 13 each).
    'blur': ([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0], [f32(1.0 / 13.0)] * 7),
    # A centre outside the picture, the factors at their upper bound (no shrink, no falloff: 14 equal taps).
    'radial': {'c31': (f32(-0.3), f32(1.4)), 'c46x': 1.0, 'c47x': 1.0},
}
SETS = (('real', REAL), ('made', MADE))

# Table coordinates: the plan's k / 512 -+ 1e-6, the boundaries half a texel on, both ends and beyond.
TABLE_K = (0, 1, 2, 12, 128, 511, 512)
TABLE_U = tuple(u for k in TABLE_K for u in (k / 512.0 - 1e-6, k / 512.0, k / 512.0 + 1e-6)) \
    + tuple(u for k in (0, 1, 12, 127, 128, 510, 511) for u in ((k + 0.5) / 512.0 - 1e-6, (k + 0.5) / 512.0 + 1e-6)) \
    + (-1.0, 2.5, 1e30, NAN, INF, -INF)

# Front-target values for the ramp's index.
RAMP_V = (0.0, 0.5 / 255.0 - 1e-6, 0.5 / 255.0 + 1e-6, 23.0 / 255.0, 0.5, 100.49 / 255.0, 100.51 / 255.0,
          254.5 / 255.0 - 1e-6, 254.5 / 255.0 + 1e-6, 1.0, 1.5, -0.25, NAN, INF, -INF)

# (c77.y, T[128]) for the unlit scale besides the two sets' own.
UNLIT = ((1.0, 0.0), (1.0, f32(1.0 / 64.0)), (1.0, f32(0.02)), (64.0, f32(3.999)), (NAN, 0.25), (1.0, NAN), (-1.0, 0.25),
         (INF, 0.25))

# The composite: (scene, bloom).
COMPOSITE_POINTS = (
    ((0.6, 0.55, 0.4), (0.0, 0.0, 0.0)),                # mid grey-brown, no bloom (variant A)
    ((2.5, 2.4, 2.2), (0.05, 0.05, 0.06)),              # a bright pixel under the veil
    ((0.003, 0.002, 0.001), (0.0, 0.0, 0.0)),           # real set: table texel 0
    ((0.004, 0.0045, 0.004), (0.0, 0.0, 0.0)),          # real set: u * 512 = 0.56: texel 1, where floor reads texel 0
    ((0.02, 0.03, 0.01), (0.01, 0.0, 0.0)),             # inside the steep first texels; inside the made set's grey fade
    ((0.2, 0.05, 0.3), (0.0, 0.0, 0.0)),                # a saturated colour inside the made set's grey fade
    ((1.0, 1.0, 1.0), (1.0, 1.0, 1.0)),                 # scene luminance 1 (the unlit scale's texel); the bloom saturates
    ((3.9, 3.9, 3.9), (0.0, 0.0, 0.0)),                 # the table's far end
    ((40.0, 0.5, 0.5), (0.0, 0.0, 0.0)),                # above the scene target's 31.875
    ((0.0, 0.0, 0.0), (0.2, 0.3, 0.4)),                 # black: the bloom alone
    ((NAN, 0.5, 0.5), (0.1, 0.1, 0.1)),
    ((INF, -1.0, 0.2), (NAN, INF, -0.5)),
)

# The bright pass: (d, tf1, tf2).
BRIGHT_POINTS = (
    ((2.0, 1.5, 1.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)),
    ((0.5, 0.5, 0.5), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)),            # real set: below the threshold
    ((3.0, 3.0, 3.0), (0.1, 0.1, 0.1), (0.2, 0.25, 0.3)),
    ((0.0, 0.0, 0.0), (0.1, 0.2, 0.3), (0.05, 0.0, 0.4)),           # L = 0 with the two layers
    ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)),            # L = 0 alone
    ((31.875, 31.875, 31.875), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)),   # the target's clamp
    ((NAN, 1.0, 1.0), (INF, 0.0, 0.0), (0.0, -1.0, NAN)),
    ((-2.0, INF, 0.5), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)),
)

SCENE_W, SCENE_H = 16, 8        # the downsample's scene: a 4 x 2 target
UNORM_W, UNORM_H = 16, 16       # the 10-bit pictures of the blurs, the radial blur and the reduction
DOWNSAMPLE_PIXELS = ((0, 0), (1, 0), (2, 1), (3, 1))
BLUR_PIXELS = ((0, 0), (7, 7), (3, 12), (15, 15))
RADIAL_PIXELS = ((0, 0), (12, 3), (8, 8), (15, 15))
REDUCE_PIXELS = ((0, 0), (3, 5), (7, 7))


def scene_texel(x, y):
    """The downsample's scene: channel c of texel (x, y) is ((7 x + 13 y + 5 c) mod 32) / 8 (0 to 3.875, exact
    in single precision), but for four texels that are no numbers or outside the range."""
    special = {(0, 0): (NAN, 1.0, 1.0), (2, 0): (INF, 1.0, 1.0), (0, 2): (-1.0, 1.0, 1.0), (10, 6): (40.0, -INF, NAN)}
    if (x, y) in special:
        return special[(x, y)]
    return tuple(((7 * x + 13 * y + 5 * c) % 32) / 8.0 for c in range(3))


def unorm_texel(x, y):
    """A 10-bit picture: channel c of texel (x, y) is ((37 x + 101 y + 59 c) mod 1024) / 1023, rounded to
    single precision."""
    return tuple(f32(((37 * x + 101 * y + 59 * c) % 1024) / 1023.0) for c in range(3))


def _rows(w, h, texel):
    return [[texel(x, y) for x in range(w)] for y in range(h)]


def composite_u(regs, scene):
    """The table coordinate of a composite point, from the single-precision inputs the lines print."""
    return f32(regs['c46x']) * lf.post_lum([lf.post_scene(f32(v)) for v in scene])


def composite_pin(regs, scene, bloom, table):
    return lf.post_composite([f32(v) for v in scene], [f32(v) for v in bloom], table, regs['c31x'], regs['c46x'],
                             regs['c47x'], regs['c77'], regs['c78x'])


def bright_pin(regs, d, tf1, tf2):
    b = regs['bright']
    return lf.post_bright([f32(v) for v in d], [f32(v) for v in tf1], [f32(v) for v in tf2], b['c31x'], b['c46x'],
                          b['c47x'], b['c77'])


def _num(v, single):
    if v != v:
        return 'kNaN'
    if v in (INF, -INF):
        return 'kInf' if v > 0 else '-kInf'
    return ('%rf' % v) if single else ('%r' % v)


def floats(values):
    return '{' + ', '.join(_num(f32(v), True) for v in values) + '}'


def doubles(values):
    return '{' + ', '.join(_num(v, False) for v in values) + '}'


def lines():
    """The pins as the lines of the C++ test."""
    table = load_table()
    out = []
    out.append('// table texel pins: u, the texel of a 512-texel table (light_formulas.post_table_texel: floor(u * 512 + 0.5), clamped)')
    for u in TABLE_U:
        out.append('    {%s, %d},' % (_num(f32(u), True), lf.post_table_texel(f32(u))))
    out.append('// unlit scale pins: c77.y, T[128], the scale (the two sets with the excerpt\'s table, then made-up pairs)')
    for name, regs in SETS:
        out.append('    {%s, %s, %r},  // %s' % (_num(f32(regs['c77'][1]), True), _num(table[128], True),
                                                 lf.post_unlit_scale(f32(regs['c77'][1]), table), name))
    for c77y, t in UNLIT:
        made = [0.0] * lf.POST_TABLE_TEXELS
        made[128] = f32(t)
        out.append('    {%s, %s, %r},' % (_num(f32(c77y), True), _num(f32(t), True), lf.post_unlit_scale(f32(c77y), made)))
    out.append('// ramp pins: the front target\'s value, the ramp\'s entry (min(255, floor(v * 255 + 0.5)); 0 for no number)')
    for v in RAMP_V:
        out.append('    {%s, %d},' % (_num(f32(v), True), lf.post_ramp_index(f32(v))))
    for name, regs in SETS:
        out.append('// %s registers: composite c31.x, c46.x, c47.x, c77.x, c77.y, c78.x %s; bright c31.x, c46.x, c47.x, c77.x, '
                   'c77.y %s' % (name, floats((regs['c31x'], regs['c46x'], regs['c47x'], regs['c77'][0], regs['c77'][1], regs['c78x'])),
                                 floats((regs['bright']['c31x'], regs['bright']['c46x'], regs['bright']['c47x'],
                                         regs['bright']['c77'][0], regs['bright']['c77'][1]))))
        out.append('// %s composite pins (the excerpt\'s table): scene, bloom, the table\'s texel, the front value' % name)
        for scene, bloom in COMPOSITE_POINTS:
            out.append('    {%s, %s, %d, %s},' % (floats(scene), floats(bloom), lf.post_table_texel(composite_u(regs, scene)),
                                                  doubles(composite_pin(regs, scene, bloom, table))))
        out.append('// %s bright pins: d, tf1, tf2, the bright target\'s value' % name)
        for d, tf1, tf2 in BRIGHT_POINTS:
            out.append('    {%s, %s, %s, %s},' % (floats(d), floats(tf1), floats(tf2), doubles(bright_pin(regs, d, tf1, tf2))))
    scene = lf.post_texture(_rows(SCENE_W, SCENE_H, scene_texel), linear=True)
    out.append('// downsample pins (scene_texel, %d x %d, linear): the pixel, the bias, the value' % (SCENE_W, SCENE_H))
    for bias in (0.0, 0.5):
        for x, y in DOWNSAMPLE_PIXELS:
            out.append('    {%d, %d, %r, %s},' % (x, y, bias, doubles(lf.post_downsample(scene, x, y, bias))))
    rows = _rows(UNORM_W, UNORM_H, unorm_texel)
    point, linear = lf.post_texture(rows), lf.post_texture(rows, linear=True)
    for name, regs in SETS:
        for weights, vertical in ((regs['blur'][0], False), (regs['blur'][1], True)):
            out.append('// %s blur pins along %s (unorm_texel, %d x %d, point), weights %s: the pixel, the value' % (
                name, 'y' if vertical else 'x', UNORM_W, UNORM_H, floats(weights)))
            for x, y in BLUR_PIXELS:
                out.append('    {%d, %d, %s},' % (x, y, doubles(lf.post_blur13(point, x, y, [f32(w) for w in weights], vertical))))
        r = regs['radial']
        out.append('// %s radial pins (a quarter of unorm_texel as a point texture read at texel floor(u * %d + 0.5), floor(v * %d + 0.5)), '
                   'c31.xy, c46.x, c47.x %s: the pixel (u = x / %d, v = y / %d), the value' % (
                       name, UNORM_W, UNORM_H, floats((r['c31'][0], r['c31'][1], r['c46x'], r['c47x'])), UNORM_W, UNORM_H))
        for x, y in RADIAL_PIXELS:
            tex = lambda u, v: [0.25 * t for t in point(u * UNORM_W + 0.5, v * UNORM_H + 0.5)]      # noqa: E731
            out.append('    {%d, %d, %s},' % (x, y, doubles(lf.post_radial(tex, x / float(UNORM_W), y / float(UNORM_H), r['c31'],
                                                                         r['c46x'], r['c47x']))))
    out.append('// reduce pins (unorm_texel, %d x %d, linear, to %d x %d): the pixel, the value' % (
        UNORM_W, UNORM_H, UNORM_W // 2, UNORM_H // 2))
    for x, y in REDUCE_PIXELS:
        out.append('    {%d, %d, %s},' % (x, y, doubles(lf.post_reduce(linear, x, y))))
    return out


def table_lines():
    """The excerpt's table as a C array of its 2,048 bytes."""
    data = bytes.fromhex(load_excerpt()['table'])
    out = ['static const unsigned char kPinsPostTable[%d] = {' % len(data)]
    for i in range(0, len(data), 16):
        out.append('    ' + ' '.join('0x%02X,' % b for b in data[i:i + 16]))
    out.append('};')
    return out


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    for line in (table_lines() if '--table' in argv else lines()):
        print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
