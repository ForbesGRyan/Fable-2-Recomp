"""The numbers the C++ mirrors of the wind are pinned to (the foliage sub-project, plan Task 10: WindPlantMirror,
WindTrunkMirror, WindLeafMirror and ClayWindReach), made by light_formulas.py: wind_plant, wind_trunk, wind_leaf and
the three reach bounds.

Inputs, all in this file (nothing is read from a capture when the lines are made): the registers and the
vertex elements of real draw rows of two captures of 2026-10-10 (native_discovery_20261010_182313, the bridge,
and _182130, towards the sun), one vertex a row: the vertex the wind moves most among 24 of the draw's. Each
tuple names its shader, capture, line, frame and vertex; tests/test_wind_formulas.py reads the bridge's again
from the capture when it is there. Beside them a few made vertices that the real ones do not reach: a plant
vertex at and below its root and at the height where the sway is whole, a trunk vertex without weights, a
leaf vertex at its pivot (with a lift, and with the lift exactly 0: the zero-length case of ledger ruling 36,
whose position is the moved pivot) and one within the slack of it. Every number is a single-precision bit
pattern, as the renderer holds it.

What a line pins. The inputs as the native constants and wind words pack them, and the position the reference
gives AT THOSE packed inputs, computed in double precision and rounded to single:
  - a plant: the seed is one word, A3 + B3 of the copy (halves: the sum is exact in single precision), and
    c7.x + c7.y is one constant, rounded to single precision;
  - a tree: c15, c16, c17 whole; the four values of fetch 4 and the three of fetch 5 in the fetches' source
    order (x to w); the pivot as the position is read (yxw).
A mirror in single precision does not give these bits. The same expressions evaluated with every operation
rounded to single precision differ from these positions by at most 7e-6 of a unit on the plants' lines (world
coordinates near 170, where single precision's own step is 1.5e-5) and 2e-6 on the trees' (the offsets
themselves are 0.05 to 0.2 units): tests/test_wind_formulas.py, test_a_single_precision_evaluation_is_near. The
bound a test holds a mirror to is that test's choice and must say so.

The literals are not in the lines: a line names its shader, whose row of the generated wind table
(src/native/capture/wind_table.inc) has them in the family's order (light_formulas.WIND_LITERAL_ORDER).

Usage: python tools/xdk_sigmatch/wind_pins.py          the lines
       python tools/xdk_sigmatch/wind_pins.py --inc    tests/native/wind_pins.inc (plan Task 10): the same lines, each
                                                       section between #ifdef FABLE2_WIND_PINS_<FAMILY> and #endif, so
                                                       that a C++ test includes the file once a table
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import light_formulas as lf  # noqa: E402


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def from_bits(b):
    return struct.unpack('<f', struct.pack('<I', b))[0]


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


# (shader, p, A3, B3, z, c7.xy, c10.x, c21.xyz, c22.xyz, c25.x, c26.xy)
PLANT = (
    # 8123, capture 182313 line 2559 (frame 1) vertex 1122; moved 0.1059
    ("8123C16DBF583F92", (0x42A1C3F4, 0x432CCA4D, 0x423E33F3), 0xBD6FE000, 0x4140A000, 0x3F1B0000,
     (0x42A1DB60, 0x4320AE65), 0x427F8BDA, (0x3F6A8E4B, 0x3EB40102, 0x3E44BE08),
     (0xBEB33333, 0x3FC3D70A, 0x00000000), 0x3F800000, (0x3CCCCCCB, 0x3D827C4E)),
    # 8123, capture 182313 line 2817 (frame 1) vertex 1571; moved 0.0783
    ("8123C16DBF583F92", (0x42A8ABF5, 0x432EE73C, 0x423EADBC), 0xC01BC000, 0x4050A000, 0x3E18C000,
     (0x42ADBCC5, 0x432BE854), 0x427F8BDA, (0x3F6A8E4B, 0x3EB40102, 0x3E44BE08),
     (0xBEB33333, 0x3FC3D70A, 0x00000000), 0x3F800000, (0x3CCCCCCB, 0x3D827C4E)),
    # B636, capture 182313 line 2583 (frame 1) vertex 42; moved 0.0810
    ("B636821F95DC9D8E", (0x42A61997, 0x43252DAA, 0x42295D62), 0x402A8000, 0x40556000, 0x3E8EA000,
     (0x42A29206, 0x4321D66D), 0x427F8BDA, (0x3F6A8E4B, 0x3EB40102, 0x3E44BE08),
     (0xBEB33333, 0x3FC3D70A, 0x00000000), 0x3F800000, (0x3CCCCCCB, 0x3D827C4E)),
    # B636, capture 182313 line 3288 (frame 1) vertex 2449; moved 0.0920
    ("B636821F95DC9D8E", (0x430BDC6A, 0x43126F23, 0x4200E85A), 0x400D4000, 0xBFE66000, 0x3E9EC000,
     (0x4309B0BE, 0x4313D8C4), 0x427F8BDA, (0x3F6A8E4B, 0x3EB40102, 0x3E44BE08),
     (0xBEB33333, 0x3FC3D70A, 0x00000000), 0x3E4CCCCD, (0x3CCCCCCB, 0x3D827C4E)),
    # 48D3, capture 182313 line 2534 (frame 1) vertex 389; moved 0.0521
    ("48D30ECCF684F488", (0x42A22BA9, 0x4329F81C, 0x423DEA7F), 0x3FDF6000, 0x4110E000, 0x3DCCC000,
     (0x429E39FD, 0x4320B293), 0x427F8BDA, (0x3F6A8E4B, 0x3EB40102, 0x3E44BE08),
     (0xBEB33333, 0x3FC3D70A, 0x00000000), 0x3F800000, (0x3CCCCCCB, 0x3D827C4E)),
    # 33C0, capture 182130 line 4348 (frame 1) vertex 458; moved 0.0724
    ("33C00226C154C1F2", (0x42A302F4, 0x43142BE8, 0x424349AE), 0x3F424000, 0xC14B4000, 0x3E7C8000,
     (0x42A29206, 0x4321D66D), 0x425F9AD1, (0x3F693379, 0x3EC1630F, 0x3E29E0AC),
     (0xBEB33333, 0x3FC3D70A, 0x00000000), 0x3F800000, (0x3CCCCCCC, 0x3D827C4E)),
    # FC4F, capture 182130 line 4292 (frame 1) vertex 6614; moved 0.1069
    ("FC4F2EF6930768BE", (0x42A5B4B0, 0x43169080, 0x424389A9), 0x3FF06000, 0xC121E000, 0x3F0DE000,
     (0x42A1DB60, 0x4320AE65), 0x425F9AD1, (0x3F693379, 0x3EC1630F, 0x3E29E0AC),
     (0xBEB33333, 0x3FC3D70A, 0x00000000), 0x3F800000, (0x3CCCCCCC, 0x3D827C4E)),
    # A8A5, capture 182313 line 1184 (frame 1) vertex 2095; moved 0.1063
    ("A8A5B2A89F70336A", (0x42D05F9F, 0x430B7FA6, 0x41FC9A30), 0xC133E000, 0x3ED74000, 0x3F87C000,
     (0x42E833B0, 0x430B48F5), 0x427F8BDA, (0x3F6A8E4B, 0x3EB40102, 0x3E44BE08),
     (0xBEB33333, 0x3FC3D70A, 0x00000000), 0x3F800000, (0x3CCCCCCB, 0x3D827C4E)),
    # A8A5, capture 182313 line 879 (frame 1) vertex 46; moved 0.0000
    ("A8A5B2A89F70336A", (0x42EA9C03, 0x43323BF2, 0x421A2048), 0xBF0E0000, 0x411A0000, 0x3F100000,
     (0x42EB5385, 0x43279A8B), 0x427F8BDA, (0x3F6A8E4B, 0x3EB40102, 0x3E44BE08),
     (0xBEB33333, 0x3FC3D70A, 0x00000000), 0x00000000, (0x00000000, 0x00000000)),
)

# (shader, p, the four values of fetch 4, c15, c16, c17)
TRUNK = (
    # 475E, capture 182313 line 24 (frame 1) vertex 290; moved 0.1453
    ("475EC9F795E5EDBB", (0xC0EFC000, 0x3D3AE000, 0x416BE000), (0x3F62E2E3, 0x3F7AFAFB, 0x3F7EFEFF, 0x3E48C8C9),
     (0x42877076, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0xBF800000),
     (0xBF2C9A56, 0xBF1C59BB, 0x40000000, 0x42FF8BDA)),
    # 475E, capture 182313 line 173 (frame 1) vertex 369; moved 0.1096
    ("475EC9F795E5EDBB", (0xBF73A000, 0xC0126000, 0x40B26000), (0x3E8C8C8D, 0x3F109091, 0x3F0A8A8B, 0x3F058586),
     (0x4283D1A8, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0xBF800000),
     (0xBF42551F, 0xBD25B664, 0x40000000, 0x42FF8BDA)),
    # 475E, capture 182130 line 24 (frame 1) vertex 290; moved 0.2111
    ("475EC9F795E5EDBB", (0xC0EFC000, 0x3D3AE000, 0x416BE000), (0x3F62E2E3, 0x3F7AFAFB, 0x3F7EFEFF, 0x3E48C8C9),
     (0x426EEFDB, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0xBF800000),
     (0xBF2930D8, 0xBF21AE49, 0x40000000, 0x42DF9AD1)),
    # FBD3, capture 182130 line 342 (frame 1) vertex 11; moved 0.3353
    ("FBD39C64463E180B", (0xBF8E4000, 0xC0B90000, 0x40E1A000), (0x3ED4D4D5, 0x3F68E8E9, 0x3F149495, 0x3E54D4D5),
     (0x427ABE69, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0xBF800000),
     (0xBF674859, 0xBE150436, 0x40000000, 0x42DF9AD1)),
    # FBD3, capture 182313 line 6554 (frame 2) vertex 9; moved 0.2213
    ("FBD39C64463E180B", (0xC05B0000, 0xC0C4C000, 0x40C68000), (0x3ED4D4D5, 0x3F6AEAEB, 0x3F40C0C1, 0x3E48C8C9),
     (0x42905D21, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0xBF800000),
     (0x3E324E24, 0x3F5CCC09, 0x40000000, 0x42FFBB14)),
)

# (shader, p, the pivot, the four values of fetch 4, the three of fetch 5, c15, c16, c17)
LEAF = (
    # A584, capture 182313 line 45 (frame 1) vertex 754; moved 0.4399
    ("A5846836C90E1192", (0x401DE000, 0xC0EEE000, 0x419E0000), (0x3FD14000, 0xC08CC000, 0x41984000),
     (0x3F55D5D6, 0x3F800000, 0x3F6BEBEC, 0x3E48C8C9), (0x3F800000, 0x3E929293, 0x3ECECECF),
     (0x42860362, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0x3D0F5C29),
     (0x3F2CB77E, 0x3F356F51, 0x40000000, 0x42FF8BDA)),
    # A584, capture 182313 line 170 (frame 1) vertex 124; moved 0.1080
    ("A5846836C90E1192", (0xBF654000, 0xC00EA000, 0x40B00000), (0xBF738000, 0xC0126000, 0x40B26000),
     (0x3E8C8C8D, 0x3F109091, 0x3F0A8A8B, 0x3F058586), (0x3F800000, 0x3F2BABAC, 0x3E9C9C9D),
     (0x4283D1A8, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0x3D0F5C29),
     (0xBF42551F, 0xBD25B664, 0x40000000, 0x42FF8BDA)),
    # A584, capture 182130 line 46 (frame 1) vertex 550; moved 0.2484
    ("A5846836C90E1192", (0xC0A7C000, 0xC0272000, 0x41930000), (0xC09A0000, 0xBD6D0000, 0x41830000),
     (0x3F7DFDFE, 0x3F78F8F9, 0x3F59D9DA, 0x3E48C8C9), (0x3F800000, 0x3F58D8D9, 0x3E28A8A9),
     (0x426C15B3, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0x3D0F5C29),
     (0x3F28A20A, 0x3F3AE198, 0x40000000, 0x42DF9AD1)),
    # D7DD, capture 182130 line 30223 (frame 6) vertex 522; moved 0.3528
    ("D7DD31B9BF194DE8", (0xC0B36000, 0xC0DAC000, 0x41244000), (0xC0DAC000, 0xC0B78000, 0x411EE000),
     (0x3F51D1D2, 0x3F800000, 0x3F33B3B4, 0x3E48C8C9), (0x00000000, 0x3F77F7F8, 0x3DB0B0B1),
     (0x42886DC0, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0x3D0F5C29),
     (0x3D90DBDC, 0x3F6431A2, 0x40000000, 0x42EFDC59)),
    # 2D40, capture 182130 line 40473 (frame 8) vertex 747; moved 0.3513
    ("2D40B53C926109BE", (0xC105E000, 0xBFA84000, 0x415D6000), (0xC0D2C000, 0x3F36E000, 0x41408000),
     (0x3F62E2E3, 0x3F800000, 0x3F4CCCCD, 0x3E48C8C9), (0x3F800000, 0x3F149495, 0x3E28A8A9),
     (0x4286298A, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0x3D0F5C29),
     (0xBF566D3A, 0xBEAED7F0, 0x40000000, 0x42F5292C)),
    # 4325, capture 182313 line 595 (frame 1) vertex 302; moved 0.2856
    ("432563420047C96C", (0xC0806000, 0x3F080000, 0x41622000), (0xC06EA000, 0xC020E000, 0x4151A000),
     (0x3E5CDCDD, 0x3F0E8E8F, 0x3F7CFCFD, 0x3F008081), (0x00000000, 0x3EE4E4E5, 0x3F6AEAEB),
     (0x42904584, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0x3D0F5C29),
     (0x3E324E24, 0x3F5CCC09, 0x40000000, 0x42FF8BDA)),
    # 4325, capture 182130 line 345 (frame 1) vertex 150; moved 0.3991
    ("432563420047C96C", (0xC02C6000, 0xC1248000, 0x411D4000), (0xBF832000, 0xC0B7A000, 0x40E30000),
     (0x3ED4D4D5, 0x3F79F9FA, 0x3F159596, 0x3E54D4D5), (0x3F800000, 0x3F73F3F4, 0x3EF6F6F7),
     (0x427ABE69, 0x3F2147AE, 0x3F3851EC, 0x3EA3D70B), (0x3C449BA7, 0x3D75C290, 0x3ECCCCCD, 0x3D0F5C29),
     (0xBF674859, 0xBE150436, 0x40000000, 0x42DF9AD1)),
)

# Where each real tuple is from: (capture, line of the capture file, vertex index).
PLANT_ROWS = (("182313", 2559, 1122), ("182313", 2817, 1571), ("182313", 2583, 42), ("182313", 3288, 2449),
    ("182313", 2534, 389), ("182130", 4348, 458), ("182130", 4292, 6614), ("182313", 1184, 2095),
    ("182313", 879, 46),)
TRUNK_ROWS = (("182313", 24, 290), ("182313", 173, 369), ("182130", 24, 290), ("182130", 342, 11),
    ("182313", 6554, 9),)
LEAF_ROWS = (("182313", 45, 754), ("182313", 170, 124), ("182130", 46, 550), ("182130", 30223, 522),
    ("182130", 40473, 747), ("182313", 595, 302), ("182130", 345, 150),)

# Made vertices (the registers of the first real row of the family).
_P0, _T0, _L0 = PLANT[0], TRUNK[0], LEAF[0]
PLANT_MADE = (
    (_P0[0], _P0[1], _P0[2], _P0[3], bits(0.0)) + _P0[5:],          # at the root: stands
    (_P0[0], _P0[1], _P0[2], _P0[3], bits(-0.25)) + _P0[5:],        # below it: stands
    (_P0[0], _P0[1], _P0[2], _P0[3], bits(0.2)) + _P0[5:],          # 1 / height: the sway is whole from here
    (_P0[0], _P0[1], _P0[2], _P0[3], bits(0.0625)) + _P0[5:],       # on the ramp
)
TRUNK_MADE = (
    (_T0[0], _T0[1], (0, 0, 0, 0)) + _T0[3:],                       # no weights: stands
    (_T0[0], _T0[1], (bits(1.0),) * 4) + _T0[3:],                   # every weight whole
)
LEAF_MADE = (
    (_L0[0], _L0[2], _L0[2]) + _L0[3:],                             # at its pivot: goes with the pivot
    (_L0[0], _L0[2], _L0[2], (0, 0, 0, 0), (0, 0, 0)) + _L0[5:],    # at its pivot, no weights
    (_L0[0], tuple(bits(from_bits(b) + d) for b, d in zip(_L0[2], (0.125, -0.125, 0.0625))), _L0[2]) + _L0[3:],   # inside the slack
    (_L0[0], _L0[1], _L0[2], _L0[3], (bits(28.0 / 255.0),) + _L0[4][1:]) + _L0[5:],   # the hundredfold byte, neither 0 nor 255
    # At its pivot with the lift EXACTLY 0 (c15.w = 0 and c16.w = 0): the moved offset D' has no length. The
    # position is the moved pivot Q' (ledger ruling 36). The two rows above keep c15.w = 0.32, whose lift gives
    # D' a length, so they pass without the rule; this one does not: L * D' / |D'| is 0 * (0 / 0) in IEEE
    # arithmetic, where the console's product with a zero factor is +0. Such vertices are drawn: three of each
    # capture's leaf vertices are at their pivot bit for bit (vertex 230 of a 600-vertex mesh, 91 and 7863 of a
    # 14,334-vertex one), each indexed by every draw of its mesh (tests/test_wind_formulas.py,
    # LeafMeshesOfTheCaptures), and their lift passes through 0 with its sine.
    (_L0[0], _L0[2], _L0[2], _L0[3], _L0[4], _L0[5][:3] + (0,), _L0[6][:3] + (0,), _L0[7]),
)
# The |P - Q| the leaves' reach pins are made at: none, the median and the largest over the WHOLE leaf streams of
# captures 182130 and 182313 (54,582 vertices of 24 streams, 56,839 of 28; every one indexed by a leaf draw):
# medians 1.476 and 1.478, largest 19.299 and 19.908 (tests/test_wind_formulas.py, LeafMeshesOfTheCaptures). The
# "12.4 at most" of frame-map section 12 was of 8,890 sampled vertices of one frame's 22 draws and is not the
# largest (review of Tasks 5 to 7, M9).
REACH_LENGTHS = (0.0, 1.48, 19.91)


def _floats(t):
    return tuple(from_bits(b) for b in t)


def plant_pin(row):
    """(the packed inputs, the position): seed, c7.x + c7.y as singles; the registers as ClayWindConstants
    packs a plant's."""
    hash_, p, a3, b3, z, c7, c10x, c21, c22, c25x, c26 = row
    seed = f32(from_bits(a3) + from_bits(b3))
    c7sum = f32(from_bits(c7[0]) + from_bits(c7[1]))
    regs = ((from_bits(c10x), from_bits(c25x)) + _floats(c26), _floats(c21) + (c7sum,), _floats(c22) + (0.0,))
    pos = lf.wind_plant(_floats(p), from_bits(z), seed, 0.0, (c7sum, 0.0), from_bits(c10x), _floats(c21), _floats(c22),
                        from_bits(c25x), _floats(c26), lf.WIND_SHADERS[hash_]["lit"])
    return (hash_, _floats(p), seed, from_bits(z), regs), pos


def trunk_pin(row):
    hash_, p, w4, c15, c16, c17 = row
    regs = (_floats(c15), _floats(c16), _floats(c17))
    return (hash_, _floats(p), _floats(w4), regs), lf.wind_trunk(_floats(p), _floats(w4), *regs, lf.WIND_SHADERS[hash_]["lit"])


def leaf_pin(row):
    hash_, p, q, w4, v3, c15, c16, c17 = row
    regs = (_floats(c15), _floats(c16), _floats(c17))
    return (hash_, _floats(p), _floats(q), _floats(w4), _floats(v3), regs), \
        lf.wind_leaf(_floats(p), _floats(q), _floats(w4), _floats(v3), *regs, lf.WIND_SHADERS[hash_]["lit"])


def reach_pins():
    """[(shader, the registers as packed, the largest weight, the largest |P - Q|, the reach)]."""
    out = []
    for row in (PLANT[0], PLANT[3], PLANT[8]):
        (hash_, _p, _seed, _z, regs), _pos = plant_pin(row)
        out.append((hash_, regs, 0.0, 0.0, lf.wind_reach_plant(regs[1][:3], regs[2][:3], regs[0][2:])))
    for row in (TRUNK[0], TRUNK[3]):
        (hash_, _p, w4, regs), _pos = trunk_pin(row)
        for w in (1.0, max(w4), 0.0):
            out.append((hash_, regs, w, 0.0, lf.wind_reach_tree(w, *regs, lf.WIND_SHADERS[hash_]["lit"])))
    for row in (LEAF[0], LEAF[5]):
        (hash_, _p, _q, _w4, _v3, regs), _pos = leaf_pin(row)
        for length in REACH_LENGTHS:
            length = f32(length)
            out.append((hash_, regs, 1.0, length, lf.wind_reach_leaf(1.0, length, *regs, lf.WIND_SHADERS[hash_]["lit"])))
    return out


def _b(v):
    return '0x%08Xu' % bits(v)


def _v(values):
    return '{' + ', '.join(_b(v) for v in values) + '}'


def _regs(regs):
    return '{' + ', '.join(_v(r) for r in regs) + '}'


def lines():
    """The pins as lines of a C++ table (every number a float's bits)."""
    out = ['// wind plant pins: shader, p (the position at rest), the seed (A3 + B3 of the copy), z, the registers as the '
           'constants pack them ((c10.x, c25.x, c26.x, c26.y), (c21.xyz, c7.x + c7.y), (c22.xyz, 0)), the position '
           '(light_formulas.wind_plant at these inputs, rounded to single)']
    for k, row in enumerate(PLANT + PLANT_MADE):
        (hash_, p, seed, z, regs), pos = plant_pin(row)
        out.append('    {0x%sull, %s, %s, %s, %s, %s},  // %s' % (hash_, _v(p), _b(seed), _b(z), _regs(regs), _v(pos),
                                                                  'real' if k < len(PLANT) else 'made'))
    out.append('// wind trunk pins: shader, p, the four values of fetch 4 (source x to w), c15, c16, c17, the position '
               '(light_formulas.wind_trunk)')
    for k, row in enumerate(TRUNK + TRUNK_MADE):
        (hash_, p, w4, regs), pos = trunk_pin(row)
        out.append('    {0x%sull, %s, %s, %s, %s},  // %s' % (hash_, _v(p), _v(w4), _regs(regs), _v(pos),
                                                              'real' if k < len(TRUNK) else 'made'))
    out.append('// wind leaf pins: shader, p, the pivot, the four values of fetch 4, the three of fetch 5 (source x to z), c15, '
               'c16, c17, the position (light_formulas.wind_leaf)')
    for k, row in enumerate(LEAF + LEAF_MADE):
        (hash_, p, q, w4, v3, regs), pos = leaf_pin(row)
        out.append('    {0x%sull, %s, %s, %s, %s, %s, %s},  // %s' % (hash_, _v(p), _v(q), _v(w4), _v(v3), _regs(regs), _v(pos),
                                                                      'real' if k < len(LEAF) else 'made'))
    out.append('// wind reach pins: shader, the registers (a plant\'s as packed), the largest weight (trees), the largest '
               '|P - Q| (leaves), the reach (light_formulas.wind_reach_plant, wind_reach_tree, wind_reach_leaf), which a '
               'mirror must not fall below')
    for hash_, regs, w, length, reach in reach_pins():
        if not math.isfinite(reach) or reach < 0.0:
            raise ValueError('wind_pins: a reach of %r' % reach)
        out.append('    {0x%sull, %s, %s, %s, %s},' % (hash_, _regs(regs), _b(w), _b(length), _b(reach)))
    return out


INC_SECTIONS = {"wind plant pins": "PLANT", "wind trunk pins": "TRUNK", "wind leaf pins": "LEAF", "wind reach pins": "REACH"}


def inc_lines():
    """tests/native/wind_pins.inc: lines() with each section under a macro of its own (the file is generated;
    tests/test_wind_literals.py holds the tracked file to this text)."""
    out = ['// Generated by tools/xdk_sigmatch/wind_pins.py --inc (the foliage sub-project, plan Task 10). Do not edit:',
           '// regenerate it. The pins of the wind mirrors: every number is the bits of a float, the positions the double',
           '// reference of light_formulas.py rounded to single. A test defines one of FABLE2_WIND_PINS_PLANT, _TRUNK,',
           '// _LEAF, _REACH and includes the file inside the braces of a table of the rows of that section.']
    open_ = False
    for line in lines():
        if line.startswith('// '):
            if open_:
                out.append('#endif')
            out.append('#ifdef FABLE2_WIND_PINS_' + INC_SECTIONS[line[3:line.index(':')]])
            open_ = True
        out.append(line)
    out.append('#endif')
    return out


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv not in ([], ['--inc']):
        print(__doc__)
        return 2
    print('\n'.join(inc_lines() if argv else lines()))
    return 0


if __name__ == '__main__':
    sys.exit(main())
