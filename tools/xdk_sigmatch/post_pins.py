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
# The scene's furniture (plan Task 13): the moon's points (texel, c31, c20.w) and the glare's (texel, c31). The
# first of each has the registers of capture native_discovery_20261010_052158 frame 1 (the moon's c31.w is 0
# there: nothing is drawn by day); the others exercise a lit moon, the scene's clamp, the alpha's clamp and
# values that are no numbers.
QUAD_MOON = (
    ((0.5, 0.25, 0.125, 0.75), (0.36961094, 0.36961094, 0.36961094, 0.0), 7.6728735),
    ((0.5, 0.25, 0.125, 0.75), (0.36961094, 0.36961094, 0.36961094, 1.0), 7.6728735),
    ((1.0, 0.8, 0.6, 1.0), (0.9, 0.7, 0.5, 0.5), 16.0),
    ((1.0, 1.0, 1.0, 1.0), (4.0, 0.5, 0.25, 3.0), 16.0),
    ((0.2, 0.4, 0.6, 0.0039215689), (1.0, 1.0, 1.0, 1.0), 8.45),
    ((NAN, 0.5, 0.5, 1.0), (1.0, 1.0, 1.0, 1.0), 8.0),
    ((0.5, 0.5, 0.5, NAN), (1.0, 1.0, 1.0, 1.0), 8.0),
    ((0.5, INF, -0.5, 0.5), (1.0, 1.0, 1.0, 1.0), 8.0),
    ((0.5, 0.5, 0.5, 0.5), (1.0, NAN, 1.0, 1.0), 8.0),
    ((0.5, 0.5, 0.5, 0.5), (1.0, 1.0, 1.0, 1.0), INF),
    ((0.5, 0.5, 0.5, 0.5), (-1.0, 1.0, 1.0, -1.0), 8.0),
)
QUAD_GLARE = (
    ((0.5, 0.25, 0.125, 0.75), (0.24488348, 0.19307472, 0.095351212, 0.0)),
    ((1.0, 1.0, 1.0, 0.0), (0.00069444446, 0.00069444446, 0.00069444446, 0.0)),
    ((1.0, 0.5, 0.25, 1.0), (64.0, 2.0, 1.0, 0.0)),
    ((NAN, 0.5, INF, 1.0), (1.0, 1.0, 1.0, 0.0)),
    ((0.5, 0.5, 0.5, 1.0), (1.0, NAN, 1.0, 0.0)),
    ((0.5, 0.5, -0.5, 1.0), (-1.0, 1.0, 1.0, NAN)),
)
# A quad's corners through the matrix of that capture's frame 1 (vertex c0..c3 as single-precision bits) and a
# made-up one: the sun's glare's first corner, the moon's last, the origin, a made-up point.
QUAD_MATRIX_BITS = (0x3EBA04AA, 0x3F684A6D, 0x33BBAA18, 0xC3306FC9, 0x3F3E8525, 0xBE989184, 0x3FC5551A, 0xC2B9462A,
                    0xBF52DBA3, 0x3EA8DAD8, 0x3EEC3766, 0x3DCDF642, 0xBF52DA8E, 0x3EA8D9FA, 0x3EEC3630, 0x3E4D6100)
QUAD_MATRIX_MADE = (1.5, 0.0, -0.25, 3.0, 0.0, 2.0, 0.5, -1.0, 0.125, 0.25, 1.0, 0.1, 0.125, 0.25, 1.0, 0.2)
QUAD_CORNERS = ((-2371.3755, -434.84604, 259.41992), (-2076.5493, 71.328873, -2866.1541), (0.0, 0.0, 0.0), (3.0, -4.0, 5.5))


# The sun layer's quads (plan Task 14): the beam source's and the flare's points (texel, c31), the moon glow's
# (texel), the sun disc's (texel, c31, c20.w) and the cloud occluder's (the texel's alpha, the pixel's world x y,
# the eye c47.xy, c77.x). The first of each has the registers of capture native_discovery_20261010_052158 frame 1
# (the flare's: of 20261010_034415 frame 1); the others exercise the target's clamp and values that are no numbers.
# The shaders' own literals are single-precision floats, as the microcode holds them (SUN_BEAM_SCALE and the
# others below): the pins are made with those, not with the doubles of light_formulas' defaults.
SUN_BEAM_SCALE = f32(lf.POST_BEAM_C255X)
SUN_GLOW_SCALE = f32(lf.POST_GLOW_C255X)
SUN_OCCLUDER_C255 = tuple(f32(v) for v in lf.POST_CLOUD_OCCLUDER_C255)
SUN_BEAM = (
    ((1.0, 1.0, 1.0, 1.0), (0.81996822, 0.64650178, 0.31927904, 0.0)),
    ((0.5, 0.25, 0.125, 0.75), (0.81996822, 0.64650178, 0.31927904, 0.0)),
    ((1.0, 0.5, 0.25, 0.0), (64.0, 6.0, 1.0, 0.0)),
    ((NAN, 0.5, INF, 1.0), (1.0, 1.0, 1.0, 0.0)),
    ((0.5, 0.5, 0.5, 1.0), (1.0, NAN, 1.0, 0.0)),
    ((0.5, 0.5, -0.5, 1.0), (-1.0, 1.0, 1.0, NAN)),
)
SUN_FLARE = (
    ((0.1, 0.2, 0.3, 1.0), (0.40544403, 0.18920721, 0.023849649, 0.2599)),
    ((0.9, 0.9, 0.9, 0.5), (0.40544403, 0.18920721, 0.023849649, 0.2599)),
    ((0.0, 0.0, 0.0, 1.0), (64.0, 1.0, 0.5, 0.0)),
    ((0.5, 0.5, 0.5, NAN), (1.0, 1.0, 1.0, 0.0)),
    ((0.5, 0.5, 0.5, INF), (1.0, 1.0, 1.0, 0.0)),
    ((0.5, 0.5, 0.5, 0.5), (1.0, INF, -1.0, 0.0)),
)
SUN_GLOW = (
    (1.0, 1.0, 1.0, 1.0),
    (0.5, 0.25, 0.125, 0.75),
    (1.0, 1.0, 1.0, 0.039215688),
    (0.2, 0.4, 0.6, 0.0),
    (NAN, 0.5, 0.5, 1.0),
    (0.5, 0.5, 0.5, NAN),
    (0.5, INF, -0.5, 0.5),
    (0.5, 0.5, 0.5, 3.0),
)
SUN_DISC = (
    ((1.0, 1.0, 1.0, 1.0), (0.87670386, 0.69123483, 0.34137076, 0.0), 1.0),
    ((0.5, 0.25, 0.125, 0.75), (0.87670386, 0.69123483, 0.34137076, 0.0), 1.0),
    ((1.0, 1.0, 1.0, 0.50196081), (0.87670386, 0.69123483, 0.34137076, 0.0), 1.0),
    ((1.0, 0.5, 0.25, 1.0), (2.0, 1.0, 0.5, 0.0), 8.0),
    ((NAN, 0.5, 0.5, 1.0), (1.0, 1.0, 1.0, 0.0), 1.0),
    ((0.5, 0.5, 0.5, NAN), (1.0, 1.0, 1.0, 0.0), 1.0),
    ((0.5, 0.5, 0.5, 0.5), (1.0, NAN, 1.0, 0.0), 1.0),
    ((0.5, 0.5, 0.5, 0.5), (1.0, 1.0, 1.0, 0.0), INF),
    ((0.5, 0.5, 0.5, 0.5), (-1.0, 1.0, 1.0, 0.0), 1.0),
)
SUN_OCCLUDER_EYE = (90.097015, 158.37025)
SUN_OCCLUDER = (
    (1.0, (90.0, 158.0), SUN_OCCLUDER_EYE, 0.5),                # under the eye: the fade is 1
    (0.29803923, (90.0, 158.0), SUN_OCCLUDER_EYE, 0.5),         # alpha 76 / 255, halved: the reference 38 / 255 itself (not above it)
    (0.29411766, (90.0, 158.0), SUN_OCCLUDER_EYE, 0.5),         # alpha 75 / 255: below it
    (1.0, (1290.0, 958.0), SUN_OCCLUDER_EYE, 0.5),              # 1,442 from the eye: inside the fade
    (1.0, (3000.0, -3000.0), SUN_OCCLUDER_EYE, 0.5),            # beyond 2,000: 0
    (0.5, (1090.0, 158.0), (90.0, 158.0), 2.0),                 # exactly at the fade's start; the clamp at 1
    (NAN, (90.0, 158.0), SUN_OCCLUDER_EYE, 0.5),
    (1.0, (NAN, 158.0), SUN_OCCLUDER_EYE, 0.5),
    (1.0, (90.0, 158.0), (INF, 0.0), 0.5),
    (1.0, (90.0, 158.0), SUN_OCCLUDER_EYE, NAN),
    (1.0, (90.0, 158.0), SUN_OCCLUDER_EYE, -1.0),
)


def sun_occluder_pin(alpha, world, eye, c77x):
    """post_cloud_occluder with the texel's alpha given (its coordinate plays no part in the pin: c31 and c46
    are the identity) and the shader's single-precision literals."""
    tex = lambda u, v, dx, dy: (0.0, 0.0, 0.0, f32(alpha))      # noqa: E731
    return lf.post_cloud_occluder(tex, (0.0, 0.0), (f32(world[0]), f32(world[1]), 0.0), (1.0, 1.0), (0.0, 0.0),
                                  (f32(eye[0]), f32(eye[1])), f32(c77x), SUN_OCCLUDER_C255)


# The previous-frame blend (plan Task 15): (the composite's value, the previous picture's, the share). Values are
# levels of 255 (what the two 8-bit targets hold) but for those that are no numbers; no point lies within a
# hundredth of a level of a tie.
PREVIOUS_W = f32(lf.POST_PREVIOUS_WEIGHT)
PREVIOUS = (
    ((200 / 255.0, 100 / 255.0, 0.0), (100 / 255.0, 200 / 255.0, 1.0), PREVIOUS_W),
    ((1.0, 1.0, 1.0), (0.0, 0.0, 0.0), PREVIOUS_W),
    ((0.0, 0.0, 0.0), (1.0, 1.0, 1.0), PREVIOUS_W),
    ((37 / 255.0, 38 / 255.0, 39 / 255.0), (37 / 255.0, 38 / 255.0, 39 / 255.0), PREVIOUS_W),     # a standing picture: itself
    ((128 / 255.0, 128 / 255.0, 128 / 255.0), (133 / 255.0, 132 / 255.0, 124 / 255.0), PREVIOUS_W),  # five levels apart: half a level of the old one comes in
    ((200 / 255.0, 100 / 255.0, 50 / 255.0), (100 / 255.0, 200 / 255.0, 250 / 255.0), 0.0),      # no share: the composite's value
    ((200 / 255.0, 100 / 255.0, 50 / 255.0), (100 / 255.0, 200 / 255.0, 250 / 255.0), 1.0),
    ((200 / 255.0, 100 / 255.0, 50 / 255.0), (NAN, INF, -1.0), PREVIOUS_W),
    ((200 / 255.0, 100 / 255.0, 50 / 255.0), (100 / 255.0, 200 / 255.0, 250 / 255.0), NAN),
    ((200 / 255.0, 100 / 255.0, 50 / 255.0), (100 / 255.0, 200 / 255.0, 250 / 255.0), -0.5),
    ((200 / 255.0, 100 / 255.0, 50 / 255.0), (100 / 255.0, 200 / 255.0, 250 / 255.0), 1.5),
)


def quad_matrix():
    """The captured matrix: four rows of four single-precision floats."""
    v = struct.unpack('>16f', struct.pack('>16I', *QUAD_MATRIX_BITS))
    return [list(v[4 * i:4 * i + 4]) for i in range(4)]
REDUCE_PIXELS = ((0, 0), (3, 5), (7, 7))
# The radial blur with its centre on the picture's top edge: c31.y exactly 0, as captured with the sun just above
# the picture (frames 7 and 8 of capture native_discovery_20261010_065553); the factors are the real set's.
RADIAL_EDGE = {'c31': (f32(0.3821), 0.0), 'c46x': REAL['radial']['c46x'], 'c47x': REAL['radial']['c47x']}


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
    r = RADIAL_EDGE
    out.append('// edge radial pins (as the real ones), c31.xy, c46.x, c47.x %s: the pixel, the value' % floats((r['c31'][0], r['c31'][1], r['c46x'], r['c47x'])))
    for x, y in RADIAL_PIXELS:
        tex = lambda u, v: [0.25 * t for t in point(u * UNORM_W + 0.5, v * UNORM_H + 0.5)]      # noqa: E731
        out.append('    {%d, %d, %s},' % (x, y, doubles(lf.post_radial(tex, x / float(UNORM_W), y / float(UNORM_H), r['c31'], r['c46x'], r['c47x']))))
    out.append('// reduce pins (unorm_texel, %d x %d, linear, to %d x %d): the pixel, the value' % (
        UNORM_W, UNORM_H, UNORM_W // 2, UNORM_H // 2))
    for x, y in REDUCE_PIXELS:
        out.append('    {%d, %d, %s},' % (x, y, doubles(lf.post_reduce(linear, x, y))))
    out.append('// quad moon pins (light_formulas.post_moon): the texel, c31, c20.w, the value (rgb, alpha)')
    for t, c31, c20w in QUAD_MOON:
        out.append('    {%s, %s, %s, %s},' % (floats(t), floats(c31), _num(f32(c20w), True),
                                              doubles(lf.post_moon([f32(v) for v in t], [f32(v) for v in c31], f32(c20w)))))
    out.append('// quad glare pins (light_formulas.post_glare): the texel, c31, the value (rgb, alpha)')
    for t, c31 in QUAD_GLARE:
        out.append('    {%s, %s, %s},' % (floats(t), floats(c31), doubles(lf.post_glare([f32(v) for v in t], [f32(v) for v in c31]))))
    made = [[f32(v) for v in QUAD_MATRIX_MADE[4 * i:4 * i + 4]] for i in range(4)]
    for name, m in (('captured', quad_matrix()), ('made', made)):
        out.append('// quad position pins, the %s matrix (light_formulas.post_quad_position): the corner, the clip position' % name)
        for corner in QUAD_CORNERS:
            p = [f32(v) for v in corner]
            out.append('    {%s, %s},' % (floats(corner), doubles(lf.post_quad_position(p, m[0], m[1], m[2], m[3]))))
    out.append('// sun beam pins (light_formulas.post_beam_source, c255.x %s): the texel, c31, the value (rgb, alpha)' % _num(SUN_BEAM_SCALE, True))
    for t, c31 in SUN_BEAM:
        out.append('    {%s, %s, %s},' % (floats(t), floats(c31), doubles(lf.post_beam_source([f32(v) for v in t], [f32(v) for v in c31],
                                                                                           SUN_BEAM_SCALE))))
    out.append('// sun flare pins (light_formulas.post_flare): the texel, c31, the value (rgb, alpha)')
    for t, c31 in SUN_FLARE:
        out.append('    {%s, %s, %s},' % (floats(t), floats(c31), doubles(lf.post_flare([f32(v) for v in t], [f32(v) for v in c31]))))
    out.append('// sun glow pins (light_formulas.post_moon_glow, c255.x %s): the texel, the value (rgb, alpha)' % _num(SUN_GLOW_SCALE, True))
    for t in SUN_GLOW:
        out.append('    {%s, %s},' % (floats(t), doubles(lf.post_moon_glow([f32(v) for v in t], SUN_GLOW_SCALE))))
    out.append('// sun disc pins (light_formulas.post_sun_disc): the texel, c31, c20.w, the value (rgb, alpha)')
    for t, c31, c20w in SUN_DISC:
        out.append('    {%s, %s, %s, %s},' % (floats(t), floats(c31), _num(f32(c20w), True),
                                              doubles(lf.post_sun_disc([f32(v) for v in t], [f32(v) for v in c31], f32(c20w)))))
    out.append('// sun occluder pins (light_formulas.post_cloud_occluder, c255 %s): the alpha of the texel, the world x y, the eye c47.xy, '
               'c77.x, the alpha the test sees' % floats(SUN_OCCLUDER_C255))
    for alpha, world, eye, c77x in SUN_OCCLUDER:
        out.append('    {%s, %s, %s, %s, %s},' % (_num(f32(alpha), True), floats(world), floats(eye), _num(f32(c77x), True),
                                                  _num(sun_occluder_pin(alpha, world, eye, c77x), False)))
    out.append('// previous blend pins (light_formulas.post_previous_blend, the weight %s): the composite, the previous picture, the '
               'share, the front value' % _num(PREVIOUS_W, True))
    for new, previous, share in PREVIOUS:
        out.append('    {%s, %s, %s, %s},' % (floats(new), floats(previous), _num(f32(share), True),
                                              doubles(lf.post_previous_blend([f32(v) for v in new], [f32(v) for v in previous], f32(share)))))
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
